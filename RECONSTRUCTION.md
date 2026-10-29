# ROM-to-native reconstruction approach

This is the shared approach for games in this workspace. The deliverables are
readable, annotated C++ game code, an asset extractor, and a faithful native
macOS application. Recovering original source text or producing a byte-identical
ROM is not required. Preserve observable game behavior, including awkward edge
cases and bugs, unless an enhancement is explicitly requested.

## Evidence and source restrictions

Derive game-specific findings only from the supplied ROM and execution of it.
Do not consult or reuse existing game decompilations, disassemblies, ports,
symbol/asset maps or game-specific reverse-engineering guides. General-purpose
tools, hardware documentation and emulator implementations are permitted;
record their sources and versions. Research emulation measures the original;
an emulator must not replace recovered game logic inside the native port.

Keep each game, its ROM identity, assets, notes and evidence in its own directory.
Record prior exposure to outside game-specific work, if any. Shared methodology
does not make findings from one game evidence for another.

## Recover game logic conservatively

Use Ghidra as a reconstruction workspace, not merely as a pseudocode exporter.
Establish code boundaries, processor mode, data types, structures, globals,
calling conventions and indirect-call targets. Save annotations or reproducible
annotation scripts so corrected analysis survives later sessions. Inferred
types, signatures and control flow are hypotheses until checked against the
instructions and relevant execution evidence.

For game logic, start from corrected decompilation and retain its branches,
state mutations, call/update order and arithmetic semantics. Unusual behavior
may matter even when its purpose is not understood. Do not simplify a condition
because it was never reached during one replay. Preserve integer widths,
signedness, truncation, wraparound, shifts, byte order and floating-point behavior
where relevant. Express machine behavior explicitly where a direct C/C++ spelling
would be undefined or implementation-dependent.

Clean names, recovered types, small helpers and comments are encouraged. Larger
structural changes should follow validation, with a recorded mapping to the
original behavior. Readable source and faithful logic are compatible; raw
decompiler formatting is not itself a fidelity requirement.

Recovered DS/console addresses are not native pointers. Reconstruct ownership
and references deliberately, with address-to-object mappings or explicit guest
address types where still necessary. Never rely on a 32-bit guest layout
accidentally matching the host ABI. A temporary address model is a research or
porting aid, not a reason to embed CPU emulation in the game.

## Code versus data (user rule, 2026-10-12)

Decide for every part of a game whether the original implements it in native
code or drives it from data, and follow that split:

- **Native game code is rewritten from scratch** as clean C++: analyse its
  logic and write equivalent code of our own. No CPU emulator, no use of the
  original system's memory maps, no translated routines. This includes the
  game's script interpreters and the native services scripts call.
- **Anything the original drives from data is executed from that data.**
  Scripts run on the clean re-implementation of the game's script
  interpreter; animation timings, layouts, texts, tables and the like are read
  from the game's files at run time.
- **Do not write a native re-implementation of something that is data in the
  original** (for example a hand-coded copy of a scripted scene, or timings
  measured and typed into C++ when the game reads them from a file). Measured
  behaviour may serve as a test oracle for the interpreter, not as its
  replacement.
- In short: never use game code, always use game data (scripts included).

When a feature turns out to be script-driven, build or extend the script
interpreter first, then let the scripts run the feature. Engines shared
between games apply to native code (e.g. a puzzle type that is native in
several games); a game that scripts the same feature shares only the native
building blocks its script commands call.

## Target form of the shipped code

(User rule, 2026-10-12.) The goal is a fresh, clean re-implementation of the
game's rules and concepts, not a translation of its code. The shipped game
code models what the game *is* (a level, the player, enemies, a boss fight,
water, a score tally) in idiomatic C++ with its own types, names and
ownership, the way BlueStreak's Sonic 1 module does.

- Do not ship the original's data layout: no packed RAM or work-memory images,
  no guest addresses as state or as routine names, no fixed-size guest records
  (e.g. 64-byte object slots) addressed by offset, no state dispatched by ROM
  address, no video memory or hardware register images as the game's model of
  the screen. Draw the picture from game state.
- Do not mirror the original's routine-by-routine structure or its machine
  idioms (bank switching, interrupt enable/disable, self-modifying tables,
  register-passing side effects) when they carry no gameplay meaning.
- Do keep the rules exact. Fidelity is measured by behaviour: the same
  positions, speeds, timings, collision outcomes, randomness, edge cases and
  bugs that a player can observe. Integer widths, rounding, wraparound and
  update order stay wherever they change an outcome; say so in a comment.
- Randomness exception (user rule, 2026-10-29): preserve the game generator
  when meaningful to gameplay/seeded content. Use standard algorithms for
  insignificant cosmetic noise and when reconstruction would require extensive
  system-memory/raster/hardware emulation. Keep cosmetic streams separate and
  document behavioral/seed compatibility limits. See
  [Adding a game](ADDING_A_GAME.md#3-randomness-preserve-meaning-without-rebuilding-a-machine).
- Hardware failures are not part of the game: no sprite-per-line dropout and
  no slowdown/lag. Deliberate effects the game itself produces (flashing,
  blinking, loading pauses where the user wants them) stay.
- Annotate the clean code with the original routine addresses and evidence it
  reproduces, so it stays traceable (see "Required traceability" below).

A close translation of the original (RAM image, routines by address, emulated
hardware state) is a legitimate *research and validation* tool: it is the
fastest way to reach frame-exact agreement with the original. Keep it in the
analysis tooling or treat it as a temporary stage, and convert it into the
clean form before calling a game done. Keep the comparison ability through the
conversion: a research-side mapping from the clean state to the original's
memory fields (and back, for injecting reference states) lets lockstep tests
continue against the re-implementation.

## Implement native platform services explicitly

Identify the boundary between game decisions and platform services. Write native
implementations for windows, graphics submission, audio output, input, timing,
files and other hardware-facing services. Define each interface by the semantics
the game expects, not just by the host API that is convenient to call.

Keep game-specific behavior on the recovered side of that boundary. For example,
the host reports input, but original button state, delays and puzzle rules remain
game logic. Replacing a graphics service still requires matching its blending,
ordering and coordinate behavior. Platform replacement is not automatically
behavior-preserving even when all game functions are faithfully recovered.

Choose boundaries from actual call sites and consumers. They may cross or split
original functions. Record such changes and unresolved services. Unsupported
commands should fail clearly or stop at an explicit test-build boundary; a silent
no-op must not stand in for a recovered feature. Limited builds may explicitly
omit services such as audio, but must identify that limitation to testers.

## Work in small, verifiable slices

1. Pin the ROM and establish reproducible reference execution. Keep hashes,
   tooling versions, input timelines and captures.
2. Trace one concrete path into its functions and resources. Recover all relevant
   branches statically; distinguish branches observed in execution from those
   not yet exercised.
3. Correct decompiler analysis and produce compiling, annotated functions.
   Introduce the minimum platform interfaces needed for this slice.
4. Compare against the original at useful boundaries: input/output state, memory
   fields, script dispatch, ordered service calls, images and audio. Include edge
   cases and alternative paths where they challenge the reconstruction.
5. Refactor only after the baseline works, then repeat the affected comparisons.
   Extend toward the next real gameplay path rather than filling gaps with
   invented behavior.

Do not require a whole-program equivalence proof before making progress. Equally,
do not treat a successful launch, matching screenshot, large instruction count
or thousands of repeated observations as proof of all-game correctness. Coverage
must describe what was actually checked. Exact frame timing, sampling, rendering
and audio can remain separate open questions.

## Required traceability and status

For each recovered unit, record the processor, module/overlay identity, runtime
address and ROM or decoded offset as applicable. An address alone may refer to
different functions after an overlay load. Match code identity before interpreting
traces. Link native functions to the relevant disassembly/decompiler export,
annotations and validation evidence.

Distinguish these statuses explicitly:

- **Observed:** bytes, instructions, values or behavior captured from this ROM.
- **Interpreted:** a supported explanation, with remaining assumptions named.
- **Recovered:** compiling source tied to the original logic, with its tested and
  untested branches stated.
- **Native service:** newly written host implementation with a documented contract.
- **Unresolved:** behavior or format not yet recovered; retained evidence is not
  an implementation.

Keep sealed reference runs immutable. New experiments get new output directories.

**Disk budget (user rule, 2026-10-10).** Disk space is shared by parallel
projects and is tight. Keep evidence small:
- Store hashes, inputs, probe logs, reports and small images, not bulk dumps.
  Delete per-frame RAM/VRAM dumps, unneeded savestates, uncompressed screen
  copies, raw PCM and large traces after sealing; the hash manifest is the proof.
- Keep only savestates that later runs restore from.
- Keep only the latest app build (no old `dist/` versions) and one build
  directory per configuration you actually use; delete experiment builds and
  scratch outputs when done.
- Check free space (`df -h`) before batch runs. If a job would add more than
  ~5 GB, ask first.
- Determinism makes deleted outputs regenerable; record how in the report.
Validation reports should identify their inputs, source/binary versions and
limits. Fixes to research tools require checking that observations remain valid.
Preserve original asset payloads alongside decoded data and pin extraction
provenance; matching a rendered frame does not establish an entire format.

## Existing code and test releases

Do not discard validated work merely because it was written in a cleaner style
than Ghidra's output. Audit it against the original and retain the parts that
preserve the recovered behavior. Mark deliberately narrowed implementations and
validation scaffolds, then recover omitted branches as their consumers are added.

Package self-contained native test builds at meaningful interactive milestones.
State what the tester can do and where the build ends. An opening menu test is
not a reconstructed puzzle or complete playable game. Validate bundle contents,
dependencies, launch and input separately; a headless smoke test does not verify
GUI interaction. Keep known limitations and reproduction commands next to the
build report.

The default is therefore: recover game logic closely, replace platform services
deliberately, and earn larger cleanup through evidence, ending in the clean
re-implementation described in "Target form of the shipped code". Apply this judgment per
function rather than choosing either wholesale rewriting or blindly compiling
raw decompiler output for the entire game.
