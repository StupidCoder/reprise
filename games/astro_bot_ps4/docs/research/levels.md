# Levels, object placement and the object model (2026-10-12)

All findings from the user's v1.04 data and executable (`eboot.elf`, Ghidra project). No outside
material was used for this step. Code lives in `src/data/{Xml,ResourceArchive,ObjectDefinition}`,
`src/game/{LevelDocument,LevelSchema}`, `src/app/LevelScene`, tool `beacon-level`.

## The three data layers

| Layer | Files | What it is | Engine side (assert paths) |
|---|---|---|---|
| Level document | `levels/<name>/lvx/design.lvx` (+ `art.lvx` in 2 levels), plain XML | placements exported from the studio's Maya level editor | `MupLvxDocument`, `MupLevelDocument`, `MupLevelManager` |
| Level object class schema | `multi_platformer/lox/<class>.lox` (306), XML | typed attribute list per placeable class, default values, enum names | factory `0x6f2ae0` (MupLevelObjectGenerator area) |
| Object definition | `multi_platformer/odx.ra` (4,360 entries) + 67 loose `data/*/odx/*.odx`, XML | resources of one game object: model, skeleton + named animations, physics bodies, sounds, effects | `Game/ObjectDefinition/ObjectDefinition*.cpp` |

Plus tuning XML: `multi_platformer.xml` (13k lines: `Hero`, `VrPlayer`, `TvPlayer`, `Controller`,
`Gadget`, `TvCamera`, `Enemies`, `Gimmicks`, ...) and `property/*.xml` per subsystem. Level lists:
`mup_product_levels.xml` (shipping order; names 4 levels that are not on disc), `mup_levels.xml`.

## odx.ra (`@MRA`)

`"@MRA"`, u32 count, count x 24-byte entries {u32 hash, u32 nameOff, u32 nameLen, u32 dataOff,
u32 size, u32 0}, packed path strings, then 16-byte aligned **uncompressed** files.
Hash = FNV-1a of the lower-case path: 0 mismatches over 4,360 entries (`tools/ra.py list`).
Extracted copy: `.tools/odx_ra/`.

## Level document (verified on all 62 shipped levels, `beacon-level check`)

`<MupLevelData>` holds `<MupLevelObject>` placements (80,350 in total), then `ObjectSets`,
`LevelObjectTemplates`, `OdxList` (preload list), `Setting` (SoundSndx, SoundBuss, PlayerMode,
...), `OrderListTable`, `TimerGroupObjectList`, and in single levels `Cutscene`, `UnlockList`,
`CategorySettings`, `CrewBotSettings`.

Each placement: `UUID`, `MayaNode` (outliner path, e.g. `|FC_S04|floor|...`; top groups are
level sections), `translate`/`rotate`/`scale`, `LoxFile` (class), `Odx` (definition),
`Parent` (UUID), `ParentTranslate/Rotate/Scale`, `ParentJoint`, `UseParentJointOrigin`,
`AsTemplate`, `Suspendable`, then the class attributes.

- Parser `0x55ed00` (MupLvxDocument) checks the root name, reads LoxFile, Odx, UUID
  (asserts `uuid.size() > 0`), Locator, LocatorOdx, Parent, MayaNode, and keeps the whole element
  as an attribute set named "MayaAttributes". Object classes then read typed values by name.
  The common base reader `0x5ca50` binds LoxFile, MayaNode, translate, rotate, scale,
  RegenerateOnReset, Locator, LocatorOdx, Parent, ParentJoint, UseParentJointOrigin,
  ParentTranslate/Rotate/Scale, Suspendable through the engine's property system
  (`FUN_00ec0940` string, `FUN_00ec2d70` vec3, `Property__00ebe870` bool). The same reflection
  system binds the tuning XML.
- `0x55e9b0` builds `data/multi_platformer/levels/<name>/lvx/` + `art.lvx` / `design.lvx`.
- **Transforms are world-space.** Evidence: floor pieces under group `|FC_S04` (ParentTranslate
  y 21.5) have translate y 21.4..23.5; a child of a placed moving object sits next to its parent
  in world units. `Parent*` record the parent group's own transform (for moving with it).
  Parents: 29,822 are placed objects, 49,630 plain Maya groups, 124 unresolved.
- **Rotation: degrees, Maya order XYZ (R = Rz·Ry·Rx), world = T·R·S.** Verified by rendering
  `beanstalk` (692 multi-axis rotations) under XYZ and under ZYX: only XYZ makes grass blocks,
  marble trims and the boat meet flush (`evidence/levels/` close-ups were compared in the
  session; overview renders kept there).
- Links (`link="True"`): comma-separated UUID lists, `null` when empty, some entries followed by
  `,:1` (meaning of the port number: **open**). 17,853 link targets, 796 point to UUIDs not in the
  level (deleted objects or cross-document references: **open**).
- Every class used by a level has a schema; every Odx resolves; every enum value is in its
  schema. Missing model files belong to editor-only definitions (`mup_enemy_spawner`,
  `mup_teetering_wall`, `*_editor_object`, `mup_occlusion_plane`).
- A schema's top-level `<Odx>` default was never needed (all placements with a model name one).

## Object class factory

`0x6f2ae0` (136 KB, static registration) passes 264 class names to `0x713fe0` (most) or
`0x715190` (`test_jumping_enemy`, `trigger_box_for_*`, ...). Table:
`evidence/lox_registry.tsv` (`tools/lox_registry.py`). Some lox classes (art lights, `bg_mesh`,
`hero_*` markers) are not in that list and are handled by other code (MupLevelMesh, art
pipeline, level manager): to trace when those systems are recovered. Registered names include
the cameras `tv_camera` and `follow_camera`.

## Collision (first look, for the next step)

- `IPhysics/File` names a `.pfb` (1,378 files, `Module/Physics/PhysicsImportPfb.cpp`). A pfb
  is a serialized memory image (pointer-sized fields, heap addresses like `0x7fe90...`, body
  names such as `metalbeam`, `concretebeam_col1`, float shape data): needs the importer's code.
- `bg_mesh.lox` attributes `level_mesh_data` and `level_collision_data`; placements carry
  `HasCollision` (36k true), `IsLevelCollision`, `IsSlope`, `HitVrHero`, `HitTvHero`,
  `HitOther`. `pfx/platform_design.lcb` per level is 16..17,473 bytes (mostly 16 zero bytes):
  probably a small extra collision cache. Engine: `MupLevelCollision` (4 functions labelled),
  `MupLevelMesh`, `Module/Physics/PhysicsWorld*` (in-house, no middleware).

## Viewer

`beacon-level render GAME LEVEL OUT.png [--yaw --pitch --size --helpers --at X,Y,Z --radius R]`
draws every placement whose definition has a model, in bind pose, with one GPU model per
distinct mesh (render refactor: `ModelPipeline` + `TextureCache` + `ModelRenderer` with an
instance matrix). Editor helpers are hidden by a viewer-only name heuristic. Skinned characters
appear dark/blob-like in bind pose without their materials' animated channels: viewer limitation.

## Open

- `:n` link ports; dangling links; `ObjectSets`, `LevelObjectTemplates`, `OrderListTable`,
  `TimerGroupObjectList` semantics.
- Per-level `anim/<UUID>_locator_anim_play.anim` (258 in forest_castle): animation clips for
  moving placements keyed by UUID (moving_object_joint etc.). Next for moving platforms.
- How `Suspendable` / sections (`SuspendBySection` in multi_platformer.xml) stream the level.
