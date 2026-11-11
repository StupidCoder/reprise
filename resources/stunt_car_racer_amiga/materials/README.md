# Enhanced VR material sources

New presentation artwork, generated with the built-in image_gen tool on
2026-11-11. These are not extracted game assets or evidence for original art.
Original imported content remains required. Originals were copied from
`~/.codex/generated_images/01a1c996-7d5b-77c2-8df3-75f81b916fcf/`:

- `asphalt.png`: `exec-fda322dd-653f-46b6-9af2-3fcb25116a13.png`
- `grass.png`: `exec-7c6f402c-257b-4f60-a504-fa3e345d5048.png`

The sources were visually inspected. The generation request asks for seamless
maps, but pixel-identical opposite edges are not assumed. The renderer uses
mirrored repetition for continuous boundaries, 512px runtime images, linear-light
mip generation and trilinear filtering. The resulting mirrored pattern and its
scale need to be judged while driving. No baked lighting or physics changes.

## Asphalt prompt

Use case: stylized-concept. Asset type: seamless repeating game surface albedo texture, square 1024x1024. Create neutral medium-gray asphalt, straight orthographic overhead, filling every pixel. Fine restrained aggregate with softly irregular medium-scale worn patches to provide depth and motion cues in a VR racing game. Uniform diffuse illumination, no baked shadows, no perspective, no directional light, no markings, no cracks, no objects, no text, no border. Exactly seamless tile on both axes. Low to moderate contrast, matte dry surface. This is a texture map, not a picture of a road.

## Grass prompt

Use case: stylized-concept. Asset type: seamless repeating game surface albedo texture, square. Create a close-cut grassy field texture, straight orthographic overhead, filling every pixel. Muted natural olive and sage greens, softly varied patches of slightly dry grass and small tufts, enough medium-scale irregularity to judge distance in a VR racing game. Match a restrained vintage racing game's enhanced materials. Uniform diffuse illumination, no baked shadows, no perspective, no directional light, no flowers, no objects, no text, no border. Exactly seamless tile on both axes. Moderate contrast, no strong isolated features or mowing stripes. This is a texture map, not a landscape.
