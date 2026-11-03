# Skeletons, skinning and animation (2026-10-12)

Derived from the data files; the library's decoder was not located in the executable (its asserts
are compiled out, Ghidra call trees stop at virtual calls). Research tools: `tools/edgeanim.py`
(parser + layout checks), C++ checker `build/beacon-anim check DIR`.

## Model side (JXMD)

- TINT TransformData: node hierarchy (59 nodes in mup_hero), POSE: one 4x4 per node.
- SCLT SkinClusterData (m_jointOffset/m_jointCount) -> JINT JointData (m_localIndex = TINT node);
  BIND: one inverse bind matrix per JINT row, **column-major**. MeshData.m_modifierIndex selects
  the cluster; vertex blend indices (format 9, u16x4) are cluster-local, weights half4 (sum 1.0).
- Check (mup_hero, 48 cluster joints): skeleton base-pose world * BIND = identity, max error
  7.9e-7 (row-major interpretation: error 1.7). Joints map model <-> skeleton by FNV-1a name hash.
- Rigid meshes: MeshNodeData.m_transformIndex gives the node; the vertices are in the node's
  space, drawn with World(node) (2026-10-15; before: World * inverse(base World), which only agreed
  where base poses are near identity). Evidence: the cloud floor's node at z -14.188 is the matrix
  the original's vertex stage gets (run roof_cloud_vs); mup_controller's 28 rigid parts are each
  centred on their node origin (`beacon-model info`: per-entry vertex centroids ~0).
  Static placements without a skeleton still draw rigid meshes untransformed **[open]**: a
  model whose nodes have non-identity base poses would need its base pose there.
- Render categories seen: Opaque, Shadowmap, and a third (hash 0x300a9a6f, name not resolved).

## Skeleton (.skel, "60SE")

Offsets self-relative. 0x10 u16 joints, 0x12 user channels, 0x16 locomotion joint (hypothesis:
1 = AR_Locomotion for the player; used for root-motion camera follow). +0x18 base pose (float4
quat, float4 translation, float4 scale per joint), +0x1c joint -> hierarchy entry, +0x20 joint name
hashes (FNV-1a; 54/55 resolve to mup_hero node names), +0x3c entry count, entries at 0x40:
{u16 joint, u16 parent | 0x8000 flag (meaning unknown)}, groups of 4 padded by repeating the last
entry, parents first (checked). All 1,465 skeletons load.

## Clip (.anim, "80AE")

Header: see src/data/Animation.hpp. Findings:
- 0x0c header size = start of frame data; 0x0e joints; 0x10 frames (duration * rate + 1);
  0x12 frame sets; 0x16..0x24 constant / animated R, T, S, user counts; 0x26 skeleton user channels.
- Joint lists at 0x60: cR, cT, cS, cU, aR, aT, aS, aU; padding value = joint count (user lists:
  user channel count); cR padded to a multiple of 8, all others to 4. Brute-force fit over all
  11,799 clips: with this rule every list is strictly increasing and constant/animated lists of a
  kind are disjoint. (First attempt with 4 everywhere rotated whole bodies.)
- Rotation: 48-bit big-endian; bits 46..32, 31..17, 16..2 = the three smaller components
  ((v - 16384) / (16384 * sqrt 2)), bits 1..0 = index of the omitted largest (x y z w), positive.
  Found by search against base pose on mup_hero_anim_blink constant channels (median |dot| 0.991);
  decoded keys over all clips have unit norm within 6e-8.
- Frame-set table (+0x38): {u32 ?, u32 offset}; info (+0x3c): {u16 first frame, u16 keyed frames}.
  Set: header u32[8] = byte sizes {R0, T0, S0(+pad), U0, R, T, S(+pad), U(+pad)}, first-frame
  values, key bitmask (keyed x channels bits), keys channel-major. 299,824 sets match the size
  sum exactly; every bitmask in the game is all ones (no keyframe reduction). Some clips use one set
  per frame whose single key repeats the next set's first frame (the last one past the clip end).
- Unsupported: 12 clips with bit-packed channels (section sizes smaller than raw), e.g.
  mup_inflate_enemy_eye_anim_disappear; packing specs presumably at +0x5c.
- Unresolved: header 0x14 (8192), frame-set table first word (set size + 192?), bitmask bit order
  (all ones, so untested), +0x54 custom data (same hash block as the skeleton), +0x58 list.

## Verification

Headless renders (`beacon-viewer --snapshot --anim --time`; now `beacon --snapshot --model --anim --time`): hero walk_on_the_spot cycle upright
with alternating legs/arms; celebration (mid-flip), run_01 (with root-motion follow), basic enemy
attack, bird boss appear. Interactive GUI checked via `--ui-snapshot` (app-rendered PNG).
User channels: see Corrections below (now decoded and applied).

## Corrections (2026-10-12, user report)

- Scale compensation: the 0x8000 bit of a skeleton hierarchy entry marks Maya "segment scale
  compensate" (a child's rotation/scale ignore the parent's scale; its translation still follows
  the parent). Evidence: mup_hero_anim_air_attack animates scale ~2x on the hand chain with every
  child storing its parent's value (joints 23/24/25: 1.85/2.22/2.17); with inheritance the fist
  reached ~400x (2 x 5.5 x 7 x 5 along 23 -> 28 -> 29 -> 30). Fixed in anim/Pose.cpp
  (T * inverse(parent scale) * R * S); the fist now grows ~2x during the punch.
- User channels: skeleton +0x24/+0x28 hold channel name / target node hashes. Player skeleton:
  channels 2/3 = "uvOffset0X"/"uvOffset0Y" on "hero_eyes_matSG" (material hero_eyes_mat, texture
  mup_tex_arbot_eyes: 4x4 atlas of eye expressions); channels 0/1 = "offsetU"/"offsetV" (names
  found by hashing Maya place2dTexture attribute names) on the texture node
  "mup_tex_arbot_eyes_1". In all 238 hero clip channel pairs, offsetU/V equal uvOffset0X/Y frame
  for frame: the exporter wrote the eye offset on both the texture node and the material; one pair
  suffices (the viewer applies uvOffset0X/Y). Visual check: celebration/after_goal show the heart
  cell, caught_by_hand the squeezed "> <" cell; negative V offsets wrap as expected. Clip layout: user list after the constant S list (cU) and after the
  animated S list (aU); constant values at +0x4c (float); per set intrinsic after S (section h3),
  keys after S keys (section h7), 4 bytes each. blink: uvOffset0X 0 -> 0.5 -> 0.25 -> 0 = open,
  closed, half-closed cells. Values are stepped, not interpolated (atlas offsets). Applied in the
  viewer to materials whose name or name + "SG" hashes to the target node.
- Category "ZPrepass" (hash 0x300a856f, JsZPrepassData template) is a depth pre-pass; the viewer
  skips it along with Shadowmap.
- mup_hero_anim_bazooka_run: 49 frames, no animated channels (24 constant R incl. legs, 7 T, 7 S):
  a static held pose in the data, not a decoding error.
