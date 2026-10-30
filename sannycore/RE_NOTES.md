# sanny.exe 4.2.0 reverse-engineering notes (static analysis, Delphi 32-bit, image base 0x400000)

## How to find things
- Delphi VMT/RTTI gives class names + published methods (see tools in the chat history: scan dwords == vmt-pointer, vmtClassName at VMT-44).
- Delphi puts string literals *after* the function that uses them (refcount -1, length, chars, NUL).
- Compiler + decompiler live in one unit, 0x552000-0x575aac (class `TCompiler`, instance size 15 MB).
  Decompiler ~0x553000-0x55c200 (main loop 0x555e74-0x558860), compiler ~0x55ce00-0x575900.
- New-language (SBL) features are NOT here: `lib/ssc.dll` (export `compile`, Rust crate somersault-core) and `lib/core.dll` (Rust).

## Games (edit mode `game` attribute)
`0x58befc` maps the string to an enum: gta3=0, vc=1, sa=2, lcs=3, vcs=4, vc_mobile=5, sa_mobile=6.
`0x58c29c(modeObj)` returns it. Per-game constants (getters read this enum):
| getter | meaning | lcs(3) | vcs(4) | others |
|---|---|---|---|---|
| 0x5a6d14 | `if` opcode | 0x00DB | 0x0078 | 0x00D6 |
| 0x5a6d40 | `jump_if_false` | 0x004D | 0x0022 | 0x004D |
| 0x5a6d60 | `gosub` | 0x0050 | 0x0025 | 0x0050 |
| 0x5a6ce8 | `start_mission` | 0x041C | 0x0289 | 0x0417 |
These are used by sannycore (`Data::ifOp/jfOp/gosubOp/startMissionOp`).

## Script buffer helpers
Global buffer ptr 0x5e8db8, read cursor 0x5e91bc. 0x5a6744 read byte, 0x5a6708/0x5a671c read int16/uint16, 0x5a662c read int32 (bounds
checked), 0x5a66f4 int32 unchecked; all little endian, advance the cursor.

## Decompiler parameter decoding (function 0x555e74)
Three families, selected before the per-parameter loop (selector not yet traced):
1. jump table at 0x556aeb: type byte 0..0xB, then for >= 0xC a cascade (0x556f0b): 0x0C-0x6B, 0x6C-0xCB, 0xCC-0xE5 (26), 0xE6-0xFF (26)
   with `(byte - base) << 8 + next byte` style values => compact LCS/VCS (portable) encoding.
2. jump table at 0x5571a3: types 0..0x13 => San Andreas family (verified, implemented).
3. further tables at 0x55a17c / 0x55a43d (cmp 0xB) and 0x55a67f (cmp 0x13): second pass / compile-side readers.
Open question: which of these serves gta3 / vc / vc_mobile (types 1..6, 16.16 fixed floats expected) and how string labels are laid out.

## TCompiler control flow (verified, implemented in lowerControlFlow)
for-handler 0x56fde0, for-end 0x56fb08, while-start 0x57056c, while-end 0x56fd65, until 0x571108, else/end 0x57073c,
jump emitter 0x5712b0 (opcode word + int32 label placeholder 0x7FFFFFFF + fixup), label emitter 0x571340.

## SA Android main.scm layout (read from a real mainV1.scm, 3,169,778 bytes; all offsets absolute file offsets)
Chain of `02 00 01 <next>` (jump int32) segments:
| seg | at | content |
|---|---|---|
| 0 | 0 | jump -> end of variable space; global variables are `$N` = offset/4 and start at file offset 0 (vars 0/1 overlap the jump) |
| 1 | 49212 | id 0 "DEFINE OBJECTS": u32 count (391), then count x 24-byte NUL padded names (entry 0 empty); `#NAME` = -index |
| 2 | 58608 | id 1 "DEFINE MISSIONS": u32 main-script size (262245 = end of MAIN), u32 largest mission size, u32 count (151), then **1 + count** u32 entries: entry 0 is a dummy (975), entries 1..count are the absolute mission offsets (the table is 628 bytes = 20 + 4*152) |
| 3 | 59236 | id 2 "DEFINE EXTERNAL_SCRIPTS": u32 largest external size, u32 count (79 = 78 + the special AAA entry), count x 28 bytes: name[20], u32 offset, u32 size (offsets point into script.img, here > file size) |
| 4 | 61464 | id 3 "DEFINE UNKNOWN_EMPTY_SEGMENT": u32 0 |
| 5 | 61476 | id 4 "DEFINE UNKNOWN_THREADS_MEMORY": u32 49204 (= variable space size in bytes), u32 4941 (meaning unknown) |
MAIN code starts right after seg 5 (61492, `03A4 script_name 'MAIN'`) and ends at the "main size" field; the missions follow back to back.
Mission code uses negative jump operands (relative to the mission start); main code uses absolute positive operands; a mission may also
jump/gosub to absolute main-script offsets (positive).

## Verified on the real binary (tests/mainscm_test.cpp)
Every code block (MAIN + 150 missions, 3.1 MB) is decompiled with the sa_mobile mode, recompiled and compared:
- 151/151 blocks: same size and same decompiled text (semantic identity); 28 blocks strictly byte-identical.
- The remaining byte differences are 5,606 bytes of junk in the NUL padding of 8-byte strings / 128-byte %k% buffers in Rockstar's binary
  (unreproducible from text) and 82 bytes = ~20 jump operands (mission 150) whose target is not an instruction boundary.
- %k% parameters (e.g. 05B6 save_string_to_debug_file) are raw 128-byte buffers without a type byte.
- Opcodes 00C3/00C4 (0 parameters) exist in the mobile binary but are missing from the shipped SASCM*.ini.

## GTA III Android main.scm (real binary, 606,709 bytes) - "old" encoding (gta3; vc / vc_mobile expected to share it, not verified yet)
- Segments: seg0 jump -> 17280 (variable space), seg1 @17280 objects (u32 count 186, 24-byte names), seg2 @21756 missions
  (u32 main size 108853, u32 largest mission 25299, u32 count 80, 80 x u32 offsets; mission 0 starts right at the main size), code at 22096
  (`A4 03 'MAIN'`). All segment ids are 0 and there are no external-script / threads segments. Detect the missions table by length (20 + 4*count).
- Parameter types: 1 int32, 2 global var (u16 byte offset), 3 local var (u16), 4 int8, 5 int16, 6 float = int16 / 16 (fixed point;
  `05 00 02 08 00 06 01 00` = `0005: $2 = 0.0625`). No other types.
- Text parameters (script_name, GXT keys, zones, cutscene names...) are 8 raw bytes with NO type byte; the decoder recognises them because their
  first byte is not a valid type (1..6). The ini spec letter is not reliable (`044D=1,load_splash %1d%`).
- Verified (tests/mainscm_test.cpp gta3 main.scm): 81/81 blocks identical after decompile -> compile (same size + same text); 2,338 differing bytes
  are all junk in the NUL padding of the 8-byte text fields.

## main.scm container API (include/sbmainscm.h, src/mainscm.cpp)
`parseMainScm` / `buildMainScm`. Check (tests/mainscm_build_test.cpp): build(parse(file)) is byte-identical for the real SA Android main.scm
(3,169,778 B) and the real GTA III Android main.scm (606,709 B). Rules confirmed by that: mission table length = (segment length - 20) / 4
(SA: count + 1 entries with a dummy entry 0 and the count field = entries - 1; III/VC: count entries, mission 0 starts at the main size);
largest mission = the biggest mission block; each mission offset = previous offset + previous block size, the first = main size.
Variable space: in GTA III the size is exactly (highest used global index + 1) * 4 (4320 slots, nothing spare); the SA Android original keeps
7 spare slots (12303 slots, highest used 12295) - Rockstar's own allocation, the rule Sanny uses when compiling is not known yet.
After the missions-table fix the SA oracle covers all 152 blocks: 152/152 semantically identical, 5,524 junk padding bytes, 0 other differences.
