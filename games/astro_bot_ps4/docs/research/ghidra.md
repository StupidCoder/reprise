# Ghidra setup (2026-10-12)

Project: `.tools/ghidra-projects/Beacon` (program `image.bin`), reached through the non-dot
symlink `Beacon/ghidra` because Ghidra rejects project paths containing dot-folders.
Wrapper: `.analysis/tools/ghidra.sh` (`import`, `decomp OUT ADDR...`, `script NAME ARGS`).

Pipeline:
1. `self2elf.py` - SELF (plaintext segments) -> `.tools/elf/eboot.elf`.
2. `prep_elf.py ELF .tools/nid/aerolib.inl .tools/ghidra-input` - flat image at the ELF virtual
   addresses (base 0) with 79,441 RELATIVE and 199 symbol relocations applied; labels for 747
   import slots/stubs (744 named via shadPS4's NID table, GPL, research use only); 51,099
   function starts from the exception-frame index (.eh_frame_hdr).
3. `ghidra.sh import` - BinaryLoader x86:LE:64 gcc; `SetupProgram.java` (segments, labels,
   51,456 functions) before auto-analysis (~13 min), `LabelBySource.java` after it: 3,498
   functions named `<SourceFile>__<addr>` from the assert paths they cite
   (`../evidence/source_functions.tsv`).

Observations: asserts call FUN_00ebc550(file, line, 1, "[Asobi Dbg]", "Assertion failed: %s\n",
expr) and trap with int 0x41; expressions name members (`m_pAnim != nullptr`). Hash constants
(e.g. 0xd44d0932) are FNV-1a names, resolvable with `../evidence/fnv1a_names.tsv`.

Next: name the assert handler, resolve FNV constants as comments, find ResourceArchiveExtractor
(odx.ra decompression) and the Edge animation decoder.
