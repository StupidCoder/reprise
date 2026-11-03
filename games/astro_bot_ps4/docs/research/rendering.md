# Rendering: lighting, shadows, post-processing

Findings for the port's renderer (src/render/Lighting.*, Offscreen.*, ModelRenderer.*,
shaders/model.frag, shadow.frag, bloom_*.frag, final.frag; src/game/SceneLighting.*).
Evidence: harness runs roof_light1/4 (shader dumps, decompiled with spirv-cross to GLSL),
roof_light3 (frame 2900: render-target dumps + flat screenshot), roof_light5 (draw trace).
All from this game's binary running in the harness; nothing from other sources.

## Frame structure (original, run roof_light5, 960x1080 flat mode)

1. Shadow map: depth-only passes, two cascades (111 + 101 draws in the traced frame), then a
   further depth pass (129 draws; the ZPrepass category).
2. Main pass: one colour target RGBA16F, 4x MSAA (`s4`), forward shading; then resolved.
3. Transparent / cloud draws after the resolve (the cloud shader samples the resolved image).
4. Bloom: clamp to BloomInputMax (40a1a43d), 4-tap downsample (0b3dbc9e), 17-tap separable
   Gaussian exp(-k^2/18) (e3702c05) down to 15x16, combine of four levels (532e2a98).
5. Composite into 0x515c40000 (bloom added; pass not identified), final output (2ed5e2b0), eye
   copy.

Render categories (fnv1a of the draw table names, resolved against eboot strings): Opaque,
Shadowmap, ShadowmapTransparent, ZPrepass, Transparent, CloudTransparent, Sky, AlphaToCoverage,
Distortion, DistortionTransparent, DistortionLava, UnderwaterSurface, LensFlare. Shadow casters
are separate draws (category Shadowmap) with ShadowmapData materials.

## Level lighting objects

- `art_light_directional`: direction = the transform's z axis (towards the light); Color x
  Intensity. Color is gamma 2.2 encoded: the engine's light constants (run roof_cloud_fs,
  lighting buffer words 16..23) are L = (0.5686, 0.7640, -0.3050), colour (1, 0.800585, 0.531049),
  intensity 3.9, i.e. Color^2.2 (first found as a fit: the sunlit minus shadowed path in the HDR
  frame has the ratio 1 : 0.79 : 0.50).
- `art_light_ambient` (ROOFTOPS: 0), `art_sky_ibl` (sky model, IblIntensity 0.87, IblColor,
  IblSphereRadius 100), `art_sun` (lens flare, not ported), `art_spx` (scene parameters).
- Sky probe record (112 bytes, run roof_cloud_fs probe buffer): centre (0, 0, 0), radius^2 10000,
  words 4..7 = 1, word 8 = 10000, word 9 = 100, words 10, 11 = 1; the cube is index 2 of a cube
  array (cloud shader's index buffer). The probe's intensity and colour are 1: IblIntensity 0.87 is
  **not** applied to the lighting (where it applies is open). The port's lighting uses 1.
- Sky cube (decoded from the data 2026-10-16; the old name rule "mup_bg_" -> "mup_bg_tex_" +
  "_ibl_01" only fitted ROOFTOPS): the sky model carries a light (JXMD table LIGI, rows
  LightInstance {m_lightType, m_lightName, m_dataTypeHash 0x75a8abfe, m_parameterOffset,
  m_parameterSize 176} into blob LIGD). Type 6 = image-based light; its parameters hold at
  +0x1c / +0x1e u16 string indices of the light's name and node path "/JsCubemapLocator01/<name>".
  The cube is the TEXI texture whose node string is that path + "_tex" (ROOFTOPS: light "_ibl_01",
  texture mup_bg_tex_city_rooftop_skybox_b_ibl_01, BC6H 256, 9 mips; canyons: light and texture
  mup_bg_skybox_canyons_ibl_01; cave: light mup_bg_skybox_cave_ibl_01, texture
  mup_bg_tex_cave_ibl_01). Checked: all 10 tested levels find their cube (data::ModelLight,
  LevelScene::setupLighting). How the engine itself binds it (code) is not traced.
- Scene parameters: spx/default.xml (Attribute list) overridden by spx/<SceneParamName>.spx
  (nested elements, flattened as Pipeline_Bloom_Scale ...).

## Material lighting (JsPBRSingleData, fs 0x96dc0fa2)

Inputs: world position (w = vertex occlusion), normal (w = alpha factor), uv.
**[hypothesis]** the vertex colour supplies both: b = occlusion, a = alpha (the vertex shader
passes input 2's z and w).

- F0 = incidence x (metal x albedo + 0.05 (1 - metal)), incidence = mix(incidenceMin,
  incidenceMax, (1 - N.V)^incidencePower); F90 = clamp(50 F0.g).
- Sun diffuse: N.L' / pi with L' = normalize(mix(L, N, fakeScatteringRatio)); light =
  colour x (w + s - w s), s = shadow, w = fakeScatteringRatio.
- Sun specular (r = max(0.1, roughness), a2 = r^4, k = (r+1)^2/8):
  clamp(N.L x F x N.V N.L a2 / (pi d^2 (N.L(1-k)+k)(N.V(1-k)+k))), F = F0 + (F90 - F0)
  2^((-5.55473 V.H - 6.98316) V.H).
- Ambient: ambient colour x vertex occlusion.
- IBL: diffuse = cube(N, lod 6.5); specular = cube(sphere-corrected R, lod clamp(6 + 1.15
  log2(max(r, 0.01)), 0, 6.5)) x clamp(1.3 N.R + 1)^2; environment BRDF (analytic, constants
  in model.frag); occlusion on diffuse, occlusion^2 on specular.
- Result: gameColorScale x ((occl x iblDiffuse + directDiffuse)(1 - metal) albedo + occl^2
  iblSpec envBrdf + emissive + directSpec); alpha = alphaScale x gameAlphaScale x vertex a x
  base a, discarded below alphaThreshold.
- **[guess]** occlusion0 (AO texture) multiplies the vertex occlusion (variant with it not
  decoded).
- Sky (JsA_SkyData, category Sky): colour texture x colorScaleRGB x colorScaleI, unlit; drawn
  around the camera (port: scale 1000, depth test only).

## Shadows

Decoded (shader): two cascades in a 2D-array compare texture. Cascade 0 when the point's
squared distance from the light axis through the cascade-0 centre is below r^2, else cascade 1.
Projective lookup (rows 118..121 + 4 x cascade). 9 taps: diagonals at +-0.7071 o (1/16 each),
axes at +-o (1/8 each), centre 1/4. Fade = clamp(dist x [637] - [636] x [637]) with dist the
distance to the camera; s = pcf + fade - fade pcf.

Port, **[hypothesis]**: split = 0.2 log + 0.8 uniform of [near, DirShadowFar 15]
(DirShadowSplitLogRatio); each slice's on-axis bounding sphere, orthographic light view
snapped to texels, depth from DirShadowLightRange (30) towards the light to r beyond; fade
start DirShadowFar - DirShadowFadeRange, scale 1 / FadeRange (DirShadowFadeEnable); map size
DirShadowMapSize 2048. **[guess]** tap offset o = 1 texel; DirShadowOffsetScale = slope bias,
DirShadowWorldOffset = constant offset towards the light.

## Post-processing

- Final output (2ed5e2b0): 3x3 matrix x HDR, levels (LevelMin/LevelMax), x^p, mix with
  ColorBlend, sRGB target. No tone curve: the original's final screenshot is the sRGB encoding
  of its HDR composite (path 0.959 -> 0.99, sky 0.044 -> 0.23).
  **[hypothesis]** matrix = exposure 2^Exposure x saturation around the bloom pass's luminance
  weights (0.265, 0.670, 0.064); p = 1 / Gamma.
- Bloom combine (532e2a98): c = sum of 4 levels x weights; L = dot(c, lum); out = blend x c x
  scale L^power / L. **[guess]** weights 0.25 each; scale/power/blend = Pipeline_Bloom_Scale,
  PowerScale, BlendRatio; bloom added before the final output; port levels at 1/8..1/64.

## Comparison with the original (frame 2900 view, port `beacon-level render ... --eye
0,1.15,3.035 --fov 90 --play 0 --exposure 2` reads HDR values)

| region | original HDR | port HDR |
|---|---|---|
| white path, sun | 0.965 0.886 0.808 | 1.008 0.934 0.864 |
| white path, shadow | 0.253 0.326 0.451 | 0.234 0.311 0.443 |
| sky, low | 0.397 0.594 0.865 | 0.387 0.591 0.872 |
| gravel, sun (region mean) | 0.254 0.208 0.175 | 0.149 0.129 0.124 |
| clouds, left (region mean, composite 004) | 0.871 0.882 0.898 | 0.884 0.893 0.909 |
| clouds, near band | 0.589 0.647 0.711 | 0.653 0.701 0.756 |

(After the gamma 2.2 light colour and probe intensity 1 fixes, 2026-10-15.)

## Cloud sea (JsA_CloudData, category CloudTransparent)

Shaders: vs 0x878bcb28, fs 0x3113b416 (runs roof_light1/4; spirv-opt -O before spirv-cross
gives readable GLSL). Constants captured with the harness's SHADPS4_DBG_DUMP_BUFFERS (runs
roof_cloud_fs / roof_cloud_vs, frame 2900; harness.md).

- Mesh: position, packed normal, uv, three half4 colour attributes = 12 per-vertex values
  (`Model::viewValues`). Material: "color" texture (a baked shadow map of the level seen from
  above, mup_bg_city_rooftop_cloud_floor_shadow_d), "noiseMap" (mup_bg_tex_cloud_noise, RGBA8
  128^3, 8 levels), 41 float fields.
- Constant order (fragment buffer, 40 words): colorScale xyz, alphaScale, gameColorScale xyz,
  gameAlphaScale, scatteringDirection, dirLightNormalDecay, ...Influence,
  dirLightIncidenseTransmittance, dirLightIncidenseBegin, noiseUvOffsetDirection xyz (13..15),
  ambLightMin (16), noiseUvOffset (17, animated), noiseUvScale, raymarchUvDistance,
  shadingBaseNoiseScale, shadingNoiseOctaveScale xyz, noiseBump, noiseBumpContrastMin, Max,
  edgeBaseNoiseScale (27), edgeAlphaContrastMin (28), edgeNoiseOctaveScale xyz (29..31),
  edgeAlphaContrastMax (32), softDepthDistance (33), distanceFadeEnable, Begin, End (34..36).
  Vertex buffer: edgeThickness, vertexDisplacementOffset, ...Distance, ...MipLevel (int).
- noiseUvOffset is the placement clip's user channel noiseUvOffsetX (the cloud floor's idle clip
  ramps it 0 -> 1 over 6 s, played at 0.25; 0.4 at frame 2900), not the template's 0.8167.
- The cloud floor hangs on skeleton joint 1 at z -14.188; the vertex stage's instance matrix is
  exactly that joint's world matrix. Rigid meshes are in their node's space (animation.md).
- Vertex: p = position + n x edgeThickness; layered noise d at p x scale - direction x offset
  (mip vertexDisplacementMipLevel): base term base x (n.x - 1), three octaves each
  d = (d - o(1 - n.yzw)) / (1 - o(1 - ...)); displaced p + n x Distance x (d - Offset). Baked
  value: 12 directions 30 degrees apart in the tangent plane (T = horizontal tangent, B = T x N),
  weights max(0, cos - 0.866) against the view direction projected into the plane.
- Fragment: five noise samples along the view ray (object space, step 0.02 x raymarchUvDistance /
  noiseUvScale); shading density per sample (start = colour alpha), edge density per sample
  (start 1, base edgeBase x (soft x alpha x n.x - 1), edge octaves); a sixth sample towards the
  light (3/256 / noiseUvScale) gives a bump term 1 + noiseBump (smoothstep(t) - 1).
  Sun: phase mix(HG(0.8, -L.V), HG(0.5, L.V), scatteringDirection); scatter = sum of
  0.2 s_k (1 - (1 - P) e^-tau_(k-1)); times min(1, transmittance x smoothstep(rim) + bump x
  mix(1, smoothstep(0.5 N.L + 0.5)^decay, influence)). Sky: probe diffuse (lod 6.5) x
  max(ambLightMin, ...). Colour = colour scales x texture x (sun + sky).
  Alpha: smoothstep of the mean edge density between the contrast limits, a hash dither
  (LCG 1664525 / 1013904223, x 1.397e-11), raised where the texture alpha is high; soft depth
  smoothstep((scene distance - distance) / softDepthDistance); distance fade 25..75 m.
  Output = mix(scene behind, cloud, soft x alpha x fade), no blending (the shader reads the
  resolved scene).
- ROOFTOPS has noiseBumpContrastMin = Max = 1: the shader divides by zero; the clamped result is 0
  (as the emulator's GPU gives it), so the bump sits at 1 - noiseBump = 0.1. With 1 instead the
  clouds come out 15-35 % too bright.
- Port: Offscreen::beginTransparent resolves colour and depth (sample 0), copies the scene, and
  draws clouds and blended draws single-sample with read-only depth (the original keeps 4x
  samples there **[difference]**). Comparison (frame 2900 view): far clouds within 1-4 %, the
  near band 6-10 % bright (noise phase: the port starts the clip at 0).

## Material render state (MaterialCommonData, every template's m_common)

Fields (names from the template table): m_blendStateMask (u64), m_materialHashName,
m_materialPathHashName, m_typeHashName, m_materialFlags, m_depthOffset, m_cullFace, m_zWrite,
m_zTest, stencil fields, m_priority, m_userDataOffset/Count.
- m_blendStateMask: low nibble = source factor, next nibble = destination factor, GNM BlendFactor
  enum (0 Zero, 1 One, 4 SrcAlpha, 5 OneMinusSrcAlpha, 8 DstColor), next = op/enable. Checked
  against the traced blend state (run roof_hover1): 0x111 additive (laser, sun flares), 0x154
  alpha (glass, clouds, miniblocks), 0x101 opaque. Exception: JsA_GlassSpecularData says 0x154
  but draws SrcAlpha / One (the engine's choice for that template).
- m_cullFace **[hypothesis]** 0 none, 1 front, 2 back; m_materialFlags bit 0x02000000
  **[hypothesis]** selects the billboard vertex shader (only the laser's start glow has it among
  the materials with billboardFaceEye).

## Effects (A_PT_EffectData)

Shaders: vs 0x1831ada0 (billboard variant 0x4c8df3b8), fs 0xfe1cd67f (also 0x851e9578,
0x9dedc1a4 variants with constants elsewhere). Run roof_hover1 (hover at frame 2960, draw
trace with program hashes, harness build det13), constants from runs roof_hover2/3.
- Vertex: two UV sets from one texture coordinate through a 2D affine matrix per texture
  (colour, base colour); a zOffset depth bias; transparent-fog colour by distance (attr5, used
  when TransparentFogEnable; off in ROOFTOPS); vertex colour passed on.
- Billboard (0x4c8df3b8): z towards the eye (or the view direction), up = world up when the roll
  is fixed else the view's up; the instance's scale and the joint's local rotation under it.
- Fragment: Fresnel fade smoothstep((N.V - fresnelBorderL) / (H - L)); colour texture x base
  colour texture (rgb) x colour scales x vertex colour; alpha = alphaScale x gameAlphaScale x
  colour alpha x vertex alpha x Fresnel; written premultiplied (rgb x alpha, alpha) for the
  material's blend state.
- Clip channels: offsetU / offsetV on a texture node (TEXI m_textureNodeIndex, e.g.
  "ef_hero_hover_01:file1") move that texture's UVs; alphaScale on a material (shading group).
- The game overrides colour constants per effect: a dumped fe1cd67f draw has colorScaleRGB
  (0, 0.41, 1) where the material says (1, 1, 1), another colorScale 5.97 **[open: which effect]**.
- Port: effect.vert / effect.frag, pipelines per blend state (ModelPipeline::effectPipeline),
  drawn in the transparent pass; an opaque-state effect (the laser's core) also there, One / Zero
  **[difference]**. **[hypothesis]** UV matrix = scale, rotate about (0.5, 0.5), offset.

## Hover laser

Code: hover state entry 0x4cc440, update 0x4cd6b0 (Ghidra, evidence/decomp/laser_setup.c).
- Entry: per foot three objects from the resident resources: mup_hero_hover_laser (beam),
  mup_hero_hover_laser_particle, mup_hero_hover_laser_start, each playing idle_<n> looped,
  n = the hero's player index + 1 (idle_1 .. idle_4).
- Update, per toe (the hero's toe joints): a ray 30 m straight down (0x11f24e0 = (0, -30, 0),
  filter 0xd804191f); length = toe.y - hit.y, 30 without a hit (0x11f2088). The beam and the
  particles go to the hit point (toe - (0, length, 0)), the beam scaled (1, length, 1)
  (0x11f2090 = 1); the start glow at the toe; no rotation. The rest of the update handles hits
  (enemy damage, ported earlier) and a surface splash (water: ef_hero_hovering_water, not
  ported).
- Draws (run roof_hover1, draws 983-990): per foot 36 (fs 0x851e9578), 12 (start, billboard
  vs), 240 (beam, fs 0xfe1cd67f), 336 (particles, 0x9dedc1a4), all additive, depth tested, no
  depth writes; the beam's opaque core earlier in the main pass (fs 0x6d1d9b3d, depth written).
- Colour: the game sets the beam's colorScaleRGB to (0, 0.41, 1) for player 1 (run roof_hover3,
  the beam draws #24 / #25 constants col (1 | 0, 0.41, 1 | alpha 0); the material says white).
  **[open]** the code path that sets it and the other players' colours; the water splash.
- Port: app/HoverLaserView (models, clips, channels) fed by LevelScene::hoverLaser.

## Glass (JsA_GlassData + JsA_GlassSpecularData)

Shaders fs 0x99054dec (body, alpha blended) and 0x96160537 (specular, added), vs 0xf368a48e;
alternating per pane in the transparent pass (run roof_hover1, draws 853-864, 903-973).
- Body: optical depth d = thicknessMax x ((2t - 1) |N.V| + (1 - t)) with t the thickness map,
  sigma = lightAbsorption + lightScattering; colour = base map x colorScale x colScaleRGB x
  colScaleI x gameColorScale x lightScattering x clamp((e^(sigma d) - 1) / sigma) x the diffuse
  light (sun N.L / pi with the shadow, ambient, sky probe at lod 6.5, light lists); alpha =
  1 - e^(-sigma d). Normal map scaled by normalScale. No scene sampling (refractionRange /
  refractiveIndex / chromatic aberration not used by this variant).
- Specular: the material lighting's specular terms only (sun GGX with shadow, probe specular x
  environment BRDF), roughness / metallic maps, incidence curve, x gameColorScale, alpha 1.
- Port: model.frag modes 2 and 3; the specular pass on a SrcAlpha / One pipeline.
- Category DistortionTransparent (the slide at ROOFTOPS' start, mup_bg_city_slide_*): glass
  that the original also refracts. Since the glass port it was drawn in the opaque pass (not in
  the blended categories), opaque dark teal; fixed 2026-10-16: drawn blended in the transparent
  pass. **[open]** the distortion (refraction of the scene behind) is not ported.

## Sun and lens flare (art_sun, JsA_LensFlareData)

- mup_bg_sun: joints along local -z (flares at z -1.085 / -0.77), idle clip (rotation and scale).
  The object follows the directional light's rotation (FollowDirLightRotation) times its own
  (rotate 180 about x): the draw's world matrix (run roof_hover2, vs 0x31f54873 buffers) puts the
  flares at L x 1.085 with no camera offset: sky space, drawn with the view rotation only.
- Flare shader fs 0x15df1c73 = the effect shader without Fresnel and base texture; Flare2 has
  colorScale 100, Flare3 1 (runtime constants match the material).
- The original's sun (run roof_sun1, head yaw -61.5 pitch 50) is a small bright white six-point
  star. Run roof_sun2 (traced): that star is the LensFlare-category quad (Flare1,
  bg_tex_sun_flare, vs 0x691c155e), drawn into the composite target 0x515c40000 after the
  resolve, additive, no depth test. Draw 522 before it is an occlusion test (fs 0xf4c31256,
  colour mask 0) whose result scales the flare **[hypothesis: the visibility read-back]**.
- Port: the sun model draws first in the transparent pass in sky space (light rotation x object
  rotation x 1000 around the camera), LensFlare category included; it is posed at load (an
  empty palette before the first object step drew it huge, with a streak). Result: the small
  white star where the original has it.
- **[open]** the occlusion test (the port's star shows through occluders); the sky branch of the
  flare vertex shader (vs 0x31f54873) is not fully decoded, the sky-space placement is a fit to
  the draw's world matrix.

## Decal shadow (Pipeline_DecalShadow)

The hero's shadow under it (as well as the cascaded sun shadow). Runs roof_decal1 (traced
draws), roof_decal2 / roof_decal3 (constants per update, harness build det15).
- Set-up (0x695790, the hero's handle): with <Disp> ShadowType Decal it hands the renderer
  (0x17d270 -> 0xbc7120 -> 0xf91bf0) DecalShadowColor (0 0 0 0.7: rgb colour, a strength),
  DecalShadowHeight 0.05, DecalShadowFadeHeightRatio 0.5, DecalShadowOffsetY 0, the constant 4.0
  and the filter 0xc804191f **[hypothesis: ray reach and collision filter of the ground ray]**.
- Mask: the hero drawn (vs 0x344b001e, fs 0x945f0061: constant 1) into a 256x256 R8 target
  with an orthographic camera straight down over the box's footprint (buf2: x scale
  2/0.7285, z scale 2/0.3979).
- Blur: separable 15 taps (fs 0x443de64c), ping-pong 0x52ee30000 -> 0x510b00000 -> 0x52ee30000;
  weights centre 0.0774399, pairs 0.0770267, 0.0752165, 0.0720647, 0.0677438, 0.0624819,
  0.0565427, 0.0502038; steps (1/512, 0) then (0, 1/1024) as passed **[open: why the two steps
  differ]**.
- Decal: a box (vs 0xe43f2138, fs 0xda89986d) drawn after the sky, before the clouds, alpha
  blended: the scene's position behind the pixel (from the linear distance in the HDR alpha) in
  box space, discarded outside |x|, |z| < 0.5; mask at (x, z) x strength x height fade (1 up to
  FadeHeightRatio of the half height, to 0 at the box's top / bottom), smoothstep; colour the
  shadow colour (Pipeline_DecalShadow_ShadowColor x DecalShadowColor).
- The box: world-aligned (yaw 180 hero, positive diagonal), 0.7285 x 0.1 x 0.3979, centred at
  the ground under the hero (0, 0.0239, 1.823; the hero at z 1.825). Run roof_decal3: the same
  box for every update from standing through the jump and the hover (hero y 0.17 -> 0.37), so
  the size does not follow the pose. Height = 2 x DecalShadowHeight. Constant [41] = -2.022 (the
  footprint's far z) unused by the port.
- Port: render/DecalShadow (mask, blur, decal), LevelScene::heroDecal (ground ray from the
  capsule centre, 4 m, filter 0xc804191f; box; mask camera); the decal reads the scene depth
  instead of the HDR alpha distance **[difference, same position]**.
- **[open / fit]** the rule behind the footprint 0.7285 x 0.3979 (not the model bounds x scale:
  0.175 x 0.093; not the blob-shadow model mup_decal_shadow, a 1 x 1 plane used elsewhere);
  whether the box turns with a hero facing another way; what happens without ground in reach
  (port: no shadow); the 2 mm z offset of the box from the hero.

## Fog

**[open]** Not located. ROOFTOPS has both fogs off. Levels starting with visible fog:
challenge_storm_city (height 0.99), ninja_stage (0.68), challenge_boss_octopus /
water_boss_final (1.0); heaven's start point has EffectScale 0. Run storm1 (frame 3000) is the
level's intro (transition mask over the scene): no full-screen fog pass, no fog in its material
shaders (0x174eb6b5) or compute shaders. Next: a gameplay frame in a fog level.

## Open items

- Where IblIntensity (0.87) applies (the probe has 1); IBL in shade still to recheck after that fix.
- Sunlit gravel (mup_bg_city_concrete_block_d, metal flecks, dark albedo 0.055) 1.7x darker:
  the normal-mapped / AO variant of the material shader is not decoded.
- Detail maps (baseDetail0, normDetail0, roughDetail0, detailScale0, detailMapWorldScaling)
  parsed, not applied.
- Cloud sea: point and spot lights not applied (none in ROOFTOPS); transparent pass single-sample.
- Lens-flare occlusion; glass not yet compared against an original frame; decal-shadow
  footprint rule (see those sections); fogs, distortion, transparent sorting, the bloom composite pass, bloom level weights.
- Sky dome placement and size (port: around the camera at 1000).
