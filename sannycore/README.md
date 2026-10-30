# sannycore — Sanny Builder compiler/decompiler core for Android (SA Mobile first)

C++17 library + JNI bridge (`libsannycore.so`) + Kotlin wrapper. No Sanny files are bundled:
copy Sanny's own `data/sa` and `data/sa_mobile` folders into your app storage and pass that path to `SannyCore.init()`.

## Layout
- `include/sbcore.h`, `src/sbcore.cpp` – opcode INI loader, SCM decompiler, compiler
- `src/sb_jni.cpp`, `android/kotlin/com/sannycore/SannyCore.kt` – JNI bridge
- `tests/test_main.cpp` – golden test against `data/sa_mobile/opcodes.txt` (real Sanny decompiler output)
- `tests/cleo_demo.cpp` – compile + decompile of a CLEO-style script with labels / `if and`

## Android (Gradle)
```
android { externalNativeBuild { cmake { path "sannycore/CMakeLists.txt" } } defaultConfig { ndk { abiFilters "arm64-v8a","armeabi-v7a" } } }
```
## Host test
```
g++ -std=c++17 -Iinclude src/sbcore.cpp tests/test_main.cpp -o sbtest && ./sbtest <path-to-sanny>/data
```

## Real-script verification
`tests/cli.cpp` (`sbcli <data dir> <mode id> <script>`) decompiles a compiled script, recompiles the text and compares bytes.
Result on a real 10 442-byte CLEO script (Portal_Gun.cs, mode `sa`): **IDENTICAL bytes** (0 diagnostics).
This confirmed: int8/int16/int32 literal-size choice, negative CLEO label offsets, string encodings, `if and/or` counts and the
array flag byte (bit 7 set = index variable is GLOBAL; clear = local).

## Opcode-less source (Sanny's real syntax)
Lines may omit the `XXXX:` prefix (`wait 250 ms`, `0@ += 1`, `not actor $X dead`). The opcode is found by matching the INI
format patterns; ambiguous ones (`0004..0007`, `0084..008B`, compares) are resolved with the typed table `optypes.txt`
(`python3 tools/gen_optypes.py <data>/sa_sbl/sa.json > <data>/optypes.txt`, auto-loaded from the data dir) and with the
`// (int)` / `// (float)` / `// comment` hints the decompiler prints.
Oracle (`tests/hl_test.cpp`): Portal_Gun.cs decompiled -> every `XXXX:` stripped -> compiled = **IDENTICAL bytes** (1049 instructions).

## Class syntax in sa_mobile (own classes.db)
Every mode loads the `classes.db` of its own folder. Sanny ships an EMPTY one for `sa_mobile` (6 bytes), so class syntax does not work there out of the box.
`tools/gen_mobile_classes.py <sanny data dir> > <sanny data dir>/sa_mobile/classes.db` builds one (a ready file is delivered as `classes.db`): the PC classes
(Actor, Camera, Car, Model, Pickup, Object, Marker, Player, Garage) restricted to the opcodes of the sa_mobile opcode list, plus a `Widget` class for the mobile
commands (`Widget.IsPressed(29)`, `Widget.Value(29) = 1.0`, `0@ = Widget.Value(29)`, `Widget.Delete(5)`, flags, texture...). You can edit or replace the file freely.
Format notes: `^Prop,[opcode,math,pos,type,help],...,("hints")` for properties, `Name,opcode,type,help,("hints")` for methods; hints like `"Behaviour:^A^B=4^C"` and the
`#DEPRECATED_ENUMS` section (`Car.SetDriverBehaviour.1 = ^Nowhere^FollowRoad^KillThePlayer^DriveToPlayer=4^IgnoreRoadPaths=8`, index = position in the hint list)
give named values, enums.txt names work as well.
`tests/classes_test.cpp` compiles every class member and the equivalent plain opcode line and compares the bytes: sa_mobile (generated file) 157/157 identical,
sa 161/161 identical. `File.*` and `Audiostream.*` (CLEO PC opcodes missing from the mobile opcode list) are left out of the generated file.

## Sanny-native main.scm source (e.g. Improved Mobile mainV1.txt) -> mainV1.scm + external scripts
`compileAny` accepts Sanny's own layout: `DEFINE OBJECTS/OBJECT`, `DEFINE MISSIONS/MISSION n AT @L` (0-based), `DEFINE EXTERNAL_SCRIPTS n` (n >= 0 adds the special
`AAA` entry, -1 does not), `DEFINE SCRIPT NAME AT @L`, `UNKNOWN_*`, `Alloc($NAME, n)`, `//-------------MAIN---`, `//-------------Mission N---`,
`//-------------External script N (NAME)---`. External scripts are compiled to separate buffers (`BinaryResult::externals`, for script.img - not written yet);
their header entries are: offset = file size (first) / previous offset + size, size = code size, "largest external" = biggest size (all verified on the real header).
SA variable space = (highest used global slot + 1) * 4 + 8 (verified: 12301 slots -> 49204 + 8 = 49212 in the real file; field 0 of UNKNOWN_THREADS_MEMORY = 49204).
`UNKNOWN_THREADS_MEMORY 1` (single value, Sanny's form) -> field 0 as above, field 1 = 4941 (value of the real file; its meaning is unknown).
Test (tests/native_test.cpp): Improved Mobile "Only Hot Coffee" (547,659 lines) -> 3,169,998 bytes + 78 external scripts in ~4 s, 0 diagnostics
(23 model names of the mod are not in the shipped .ide files: `unknownModelsAsZero` is only a test aid, load the game's .ide data instead);
header = real original (var space 49212, header 61,492 B, 391 objects, 151 missions, 78 externals + AAA, MAIN 200,753 B).
That source is a *modification* of the original (renumbered text variables, 3-decimal floats, different object list), so it cannot be compared byte for byte.

## Text length of 'text' parameters (optional limit)
Sanny 4.2.0 refuses `'text'` longer than 7 chars (15 on sa / vc_mobile): ERR_0027, read from sanny.exe 0x56b000. The game does not care, so by default
sannycore accepts longer text: <= 7 chars -> type 09 (8 bytes), 8..15 -> type 0F (16 bytes), longer -> type 0E (length prefixed, printed back as "text").
`Options::strictStringLimit` / `AnyOptions::strictStringLimit` / `SannyCore.strictStrings(true)` restores the exact Sanny 4.2.0 limits.
The 8..15 and >15 encodings are not verified on a device; the 7-char case is verified on real files.

## GTA SA Android: every script file type in one API (include/sbapi.h)
`decompileAny(bytes)` / `compileAny(text)` (JNI: `SannyCore.decompileFile` / `compileFile`, Kotlin wrapper included) detect the kind from the content:
- **main.scm / mainV1.scm** <-> one text file: `DEFINE OBJECTS/OBJECT`, `DEFINE MISSIONS/MISSION n AT @NAME`, `DEFINE EXTERNAL_SCRIPTS`, `UNKNOWN_*` segments,
  `//-------------MAIN---------------`, `//-------------Mission N---------------` (plus a few `X_` lines for values Sanny's own source does not carry:
  variable-space size, dummy mission entry 0, external script table). Missions use relative jump operands, references from missions to MAIN
  resolve across blocks (`@MAIN_<offset>`), MAIN gets labels for every target a mission uses.
- **CLEO scripts** (.csa .cs .csi), with or without Sanny's "extra info" trailer, `hex .. end` data blocks included.
Verified on real files (tests/any_test.cpp, tests/any_cleo_test.cpp):
| file | result |
|---|---|
| SA Android mainV1.scm (3.17 MB) | file -> text (12.3 MB, 0.9 s) -> file (1.4 s, 0 diagnostics): same size, header 61,492 B identical, 152/152 blocks same decompiled text, only junk padding bytes differ |
| GTA III Android main.scm (0.6 MB) | same cycle: header identical, 81/81 blocks, only junk padding differs |
| box_hello_world.csa, TEST_77gun.csi (extra info), Portal_Gun.cs | code bytes identical after text round trip; trailer rebuilt |
Defaults that are not known from Sanny: variable space = (highest used global + 1) * 4 unless `DEFINE X_VARIABLE_BYTES n`; `DEFINE UNKNOWN_THREADS_MEMORY a b`
is taken literally (the meaning of `b` is unknown).

## main.scm container (header segments)
`include/sbmainscm.h`: `parseMainScm` / `buildMainScm` (SA 6-segment and GTA III/VC 3-segment layouts). build(parse(file)) is byte-identical
for the real SA Android (3.17 MB) and GTA III Android (0.6 MB) main.scm (`tests/mainscm_build_test.cpp`). Not done yet: building the file from
`DEFINE` source text (needs the variable-space rule of Sanny's compiler and the SA threads-memory fields).

## hex .. end data blocks (reference: tests/data/box_hello_world.txt + .csa, a CLEO Android script)
Inside `hex .. end`: `"text"` -> raw ASCII bytes (no terminator, no type byte), hex digits in pairs in any grouping (`20 00`, `0000`, `4D41`), mixed on one
line (`"H"00` = 48 00); bytes are emitted in place, labels may precede each block. `tests/ref_test.cpp`: the reference text compiles to the reference
csa byte for byte (139 bytes, IDENTICAL); decompile(csa) -> compile gives the same bytes too. The decompiler emits non-code data as `hex` rows of 16
bytes (sanny.exe 0x554330) and splits it at every label that points into it (`:Label_80` / `:Label_104` for the strings in the reference).

## Real binary check: SA Android mainV1.scm
`tests/mainscm_test.cpp`: all 151 code blocks (3.1 MB) decode with `sa_mobile`; compile(decompile(block)) has the same size and the same
decompiled text in 151/151 blocks (28 strictly byte-identical; the rest differ only by junk padding of fixed-size string fields in the
original binary and ~20 jump operands whose targets are not instruction boundaries). See RE_NOTES.md for the header segment layout.

## Real-world sources (bulk compile, `tests/bulk.cpp`)
| source | mode | lines | result |
|---|---|---|---|
| Improved Mobile main.scm "Only Hot Coffee" (SA Android) | sa_mobile | 547,659 | 3.58 MB in 1.7 s, 3 diagnostics (opcode 00C4 missing from the shipped ini) |
| Improved Mobile main.scm "Standart" / "PS Vita" | sa_mobile | 588,514 | 3.85 MB in 2.1 s, 86 diagnostics (custom variables / >7-char strings of the author's setup) |
| GTA Vice City main.scm (decompiled text) | vc | 196,329 | 1.23 MB in 6 s, 12 diagnostics (059A takes no parameter in the shipped ini) |
These are *text* sources: they exercise the compiler front end (DEFINE blocks, class syntax, enums, hints) but cannot verify bytes.
Supported by them: `DEFINE OBJECTS/OBJECT/MISSIONS/MISSION n AT @L/EXTERNAL_SCRIPTS/SCRIPT/UNKNOWN_*` (parsed into `CompileResult::defs`;
`#NAME` resolves to -index for DEFINE OBJECT names), `Alloc($NAME, n)`, classes.db syntax (`Player.Defined($P)`, `0@ = Car.Create(#X, x, y, z)`,
`Player.Money($P) += 100`, `Camera.Restore_WithJumpCut`, hint enums such as `Car.SetDriverBehaviour($c, KillThePlayer)`), enums.txt,
`%Nb:a/b%word` boolean placeholders, `//` ini comments (VC/III), annotation tokens `(NAME)`, 7/15 character string limits from sanny.exe.
NOT done yet: emitting the real main.scm segment layout (objects table, missions, external scripts, mission code blocks); the
`DEFINE` data is only parsed. That layout has to be taken from sanny.exe and checked against a real compiled main.scm.

## Per-game status
Verified binary encoding (against real Android main.scm files): `sa`, `sa_mobile`, `gta3`. GTA III uses the "old" encoding (types 1..6, float = int16/16, raw 8-byte text). For gta3, vc, lcs, vcs, vc_mobile (and PS2/PSP variants) the modes load and the
per-game opcode constants from sanny.exe (if / jf / gosub / start_mission) are in place, but compile/decompile return error 9100
unless `Options::allowUnverified` / `SannyCore.allowUnverified(true)` is set (then the SA encoding is used = experimental).
To verify a game I need one real compiled script of it (main.scm / .cs) - the oracle tests then check the encoding byte by byte.
See RE_NOTES.md for what has been recovered from sanny.exe so far.

## Game select (edit modes)
`listModes(dataDir)` / `SannyCore.listModes()` reads every `data/*/mode.xml`; `loadMode(dataDir, id)` loads a mode like Sanny does
(`<opcodes>` with `*` globs, `<variables>`, `<constants>`, `<keywords>`, `<ide>`, `extends` inheritance). Check: `tests/modes_test.cpp`.
Loadable (legacy engine): gta3, lcs, lcs_mobile, sa, sa_mobile, sa_ps2, sa_v2, vc, vc_mobile, vcs_psp, vcs_ps2.
Listed but not supported yet (new SBL language: JSON library, functions, typed variables, classes, expressions):
gta3_sbl, vc_sbl, sa_sbl, sa_sbl_sf, sa_sbl_ssc.
Binary encoding is currently the San Andreas one (verified on sa / sa_mobile); gta3/vc/lcs/vcs differ (e.g. 16.16 fixed-point floats)
and must be ported from sanny.exe's per-game branches before those modes produce correct bytes.

## Sanny "extra info" scripts (.csi / .cs / .csa compiled with the option on)
Layout (verified on TEST_77gun.csi from SA Mobile): `[code]["FLAG\0" u32]["SRC\0" u32 len + source][u32 codeSize]["__SBFTR\0"]`.
`readExtraInfo / writeExtraInfo` handle it. Oracle (`tests/csi_test.cpp`): the embedded source (hand-edited, with keywords
`wait 0 / jf @L / jump / thread 'X'`, `#MODEL` names, operator lines, `{$E}`) compiled in mode `sa_mobile` -> **IDENTICAL code bytes**
(4956 bytes, 0 diagnostics). Script-style decompile (`Options::scriptStyle`) matches the embedded source on 99% of lines
(the rest are hand-typed formatting such as `10.20`).

## Control flow (high-level syntax)
Supported by the compiler (lowered to `if/and/or` + condition lines + `jf @L` / `jump @L` / internal labels before compiling):
`if [and|or] <conds> then .. [else ..] end` (conditions on following lines or inline), `while <cond> .. end` / `while true`,
`repeat .. until <cond>`, `for v = a to|downto b [step s] .. end`, `break`, `continue`, nesting of all of them.
Comparison sugar: `a < b` -> `b > a`, `a <= b` -> `b >= a`, `a <> b` -> `not a == b`. Errors: ERR_0057 (missing `end`),
9002 (stray `end` / `end` for repeat), 9003 (break/continue outside loop). Low-level `if` + `jf` code compiles exactly as before.
Demo: `tests/flow_demo.cpp`.

### Per the official docs (docs.sannybuilder.com/language/control-flow) - `tests/docs_test.cpp`
`step 2` and `step = 2`; `while false` (whole loop ignored); `jf Break` / `jump Continue` as label operands and standalone
`Break` / `Continue`; `if and|or` up to 8 conditions; `switch <var> / case a, b, c / default / end` (up to 8 values per case, no
fall-through, `default` last) lowered to an `if or` chain with `jf` to the next case and `jump @END` after each non-last case;
errors: ERR_0025 (literal counter range does not match to/downto), ERR_0102/0103/0104 (switch), ERR_0057, ERR_0071.
Not verified against sanny.exe: the exact switch layout and the conditions under which ERR_0025 fires.

### Verification against sanny.exe (static analysis of the Delphi `TCompiler`, 0x56fb08-0x5712b0)
Verified from the original code (strings, VMT and call sequence): `for` start (0x56fde0) and end (0x56fb08), `while` start (0x57056c) and
end (0x56fd65), `until` (0x571108), `else`/`end` of `if` (0x57073c); jump helper 0x5712b0 (opcode word + int32 label placeholder);
opcode getters 0x5a6d40 (= 004D jf) / 0x5a6d14 (= 00D6 if).
- `for v = a to b [step s]`: `v = a`, [`if b >= a`, `jf @END` only when a or b is not a literal], `:LOOP`, body, `:CONT`, `v += s`, `if v > b`, `jf @LOOP`, `:END`
  (`downto`: `v -= s`, entry `a >= b`, loop test `b > v`; step defaults to 1). It is a post-test loop.
- `while c`: `:LOOP`, [`if c`, `jf @END` unless c is `true`], body, `:CONT`, `jump @LOOP`, `:END`; `continue` -> `:CONT`, `break` -> `:END`.
- `repeat .. until c`: `:LOOP`, body, `:CONT`, [`if c`, `jf @LOOP`] (`until false` -> `jump @LOOP`, `until true` -> nothing), `:END`.
- `if .. else .. end`: `jf @ELSE`, then-part, `jump @END`, `:ELSE`, else-part, `:END` (no else: `jf` target is the end label).
Still unverified: (1) whether sanny.exe emits `00D6: if` implicitly before loop conditions (we emit it explicitly, as in every
Sanny-decompiled game script); (2) `a < b` is compiled by operand swap (`b > a`) rather than `not a >= b`.
A script compiled by Sanny with such constructs would settle both in one test.

## Descriptive text after `XXXX:` is ignored (as in Sanny)
`0ACD: show_text_highpriority "Hello" time 5000` and `0ACD: "Hello" 5000` compile to the same bytes; `0000:` / `0A93:` need no text.
If the line does not match the ini pattern, the valid arguments are taken positionally (plain words / operator symbols skipped);
a wrong count gives "0ACD: expecting 2 params". More than 8 conditions in one `if` -> error 9004.
Tests: `tests/tutorial_test.cpp` runs every script of https://gtamods.com/wiki/CLEO/Tutorial (hello, `while true`, `if`, `if and`, `if or`, GXT hook).

## Model names (#NAME)
`loadMode()` also reads `<ide>` from mode.xml (`custom.dat` -> default.ide, peds.ide, vehicles.ide, veh_mods.ide: 759 models for sa_mobile).
Objects that live in the game's own map IDE files (e.g. `#AD_FLATDOOR`) need those files at runtime:
`SannyCore.loadModels(ideText, ide = true)` for each `data/maps/**/*.ide` of the installed game.

## Status (SA Mobile)
Verified by golden test (1750 of 1796 non-model sample lines round-trip byte-for-text identical):
opcode lookup (SASCM.ini + Mobile + Android CLEO overlays, same load order as mode.xml), argument types
01-0E/10-13, float formatting, `not` / comment / spacing rules, `if / if and / if or`, labels, variadic opcodes,
CustomVariables/TIMERA names, `{$CLEO}` negative label offsets.

Not done yet (next steps, in order):
1. `#MODEL` names (needs `.ide` parsing: custom.dat, default.ide, peds/vehicles) and GXT comments
2. `DEFINE ...`, mission/external-script headers of main.scm, `hex..end` exact layout
3. High-level syntax: `if/while/for/repeat/switch`, `$var = ...`, `0@ += ...`, `{$INCLUDE}`, constants, arrays `$N[k]`
4. Verify int8/int16/int32 literal-size choice and array flag bits against sanny.exe's emit routine (0x55ce00-0x575900)
5. Other edit modes (SA PC, VC, III, LCS...) - same engine, different arg encodings (GTA3/VC use 16.16 fixed floats)
