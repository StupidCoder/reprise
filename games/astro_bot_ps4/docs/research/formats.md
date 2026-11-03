# Asset formats (2026-10-12)

Evidence: the extracted game data (`.tools/game/CUSA12392/data`), the executable's strings, and
batch checks with the C++ readers (`build/beacon-gnf check`, `build/beacon-model check`).
Research tools: `.analysis/tools/jxm.py` (raw chunk/table dump), `prep_elf.py`, Ghidra scripts.

## Name hashes: FNV-1a 32-bit

All hashes in model files are FNV-1a (offset 0x811c9dc5, prime 0x01000193) of source identifiers.
Proven by the type hashes: `float` a6c45d85, `uint32_t` e9b20787, `uint16_t` 96c45519, `uint8_t`
306340a8, `uint64_t` 682fe470, `int16_t` 0dfe5eda, `int8_t` 40ffa2f9, `int32_t` c04a1fbc; render
categories `Opaque` b528da3e, `Shadowmap` cf81329d.
Hashing every identifier-like string of the executable (`../evidence/fnv1a_names.tsv`, 43,431
entries) resolves 748 of 932 hashes seen in a sample of 1,060 models: every table type
(ModelHeaderData, VertexBufferData, ModelSubMeshData, MaterialInstance, ...), every field
(m_vertexFormat, m_ibOffset, m_categoryHash, ...), material templates (JsPBRSingleData,
MaterialCommonData, lambertData, JsA_WaterData, ...) and their parameters (base0, normal0,
roughness0, color0X, uvScale0X, ...). Constants in decompiled code can be resolved the same way.

## GNF textures (src/data/Gnf.*)

- Header: `GNF `, u32 contents size, u8 version (2), u8 texture count (1), ...; at 0x10 the 32-byte
  GPU texture descriptor (hardware T# layout); pixel data at 8 + contents size.
- Descriptor dword 7 holds the data size. A size model (levels in order, each level holds all
  slices, pitch/height padded to 8x8-element micro tiles, pow2-pad bit rounds width, height,
  pitch and slice count up to powers of two) matches the stored size for all 7,196 non-empty
  textures. Cube maps (32 IBL textures) therefore store 8 slices.
- Formats in use: BC4 (2,899), BC7 (2,830, mostly sRGB), BC5 normal maps (1,408), BC6H (58),
  RGBA8 volume texture (1). One empty placeholder (format 0).
- Tiling: tile mode 13 (1D thin: 8x8 micro tiles in row order, element order inside a micro tile
  x0 y0 x1 y1 x2 y2) for all 2D/cube textures; tile mode 19 (1D thick, 8x8x4) for the volume.
  Verified visually (logos, UI art, face atlas, normal map, mip levels).

## JXMD models (src/data/Jxm.*, src/data/Model.*)

- Chunk sequence; table chunks carry their own schema (see Jxm.hpp). 5,478 of 7,418 files hold
  meshes; the others are material/light/skeleton-only. All 5,478 build.
- Vertex attributes: nibble lists in m_vertexFormat / m_vertexSemantics; byte sizes solved from
  4,699 (format, semantics, stride) combinations: format 2 float3, 3 float2, 5 half4, 9 8 bytes
  (blend indices), 11 unorm8x4, 14 packed normal. Semantics 6 position, 5 normal, 4 colour,
  10 texcoord, 9 tangent, 3 blend weights, 2 blend indices.
- Packed normal (format 14): unsigned 11:11:10 small floats (5-bit exponent) storing n*0.5+0.5.
  Test on ds4_stick_l: mean |dot(normal, tangent)| 0.007, mean length error 0.008. Integer
  10:10:10, 11:11:10, 8:8:8 and octahedral encodings all fail (>0.2).
- Explicit attribute offsets: m_vertexOffsetInWords0/1, one byte per attribute in 4-byte words
  (0 = packed in order); offsets may repeat (shared streams).
- Index format: m_indexFormat 2 = 16-bit, 1 = 32-bit (all 5,497 buffers consistent with size).
- Vertices are in model space. ModelSubMeshData's m_00..m_33 plus m_center/m_width/height/depth
  form an oriented bounding box; applying the matrix as a transform scrambles the controller
  shell (ds4_body_upper), identity renders it correctly.
- Materials: MATI rows point into MATD with a template hash; MTTB embeds the templates
  (schemas). JsPBRSingleData: MaterialCommonData block (48 bytes), floats (uvOffset0, uvScale0,
  color0, emissive, ...), then u16 texture slots base0, normal0, roughness0, metallic0, ...,
  emissive0, occlusion0, detail maps, aoMapTexture, IBLTexture -> ModelTextureInfo rows.
- Constant textures: names `sytx_...`; ConstantTextureInfo m_colorRGBA holds bytes R,G,B,A
  (flat normal stored as 0xffff8080 = 80 80 ff ff).

## Open

- Node transforms / skinning: TINT TransformData hierarchy, POSE blob, JINT/SCLT/BIND skeleton
  chunks, Edge Animation `.skel`/`.anim`.
- `odx.ra` resource archive (`@MRA`, compressed): object definitions such as `mup_hero`;
  decompressor in ResourceArchiveExtractor.cpp (Ghidra).
- Material templates other than JsPBRSingleData; blend states from MaterialCommonData.
- Levels (`.lvx`), physics bodies (`.pfb`), sound banks (`.sxd`).
