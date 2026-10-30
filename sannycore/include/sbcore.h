// sannycore - C++ port of the Sanny Builder SCM compiler/decompiler (SA / SA Mobile first).
// Binary format + text conventions are derived from sanny.exe 4.2.0 (static analysis) and
// the sample output shipped in data/*/opcodes.txt.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sb {

struct Diag {
  int code = 0;          // Sanny ERR_xxxx number (lang/english.ini), 9xxx = sannycore-specific
  std::string msg;
  int offset = -1;       // binary offset (decompile)
  int line = -1;         // 1-based source line (compile)
};

struct OpDef {
  int nparams = 0;       // -1 = variadic (args end with a 0x00 type byte)
  std::string fmt;       // raw format string from SASCM*.ini, e.g. "wait %1d% ms"
  uint32_t smask = 0;    // bit i set: parameter i+1 is %s%/%g%/%z% (text); old-format games store it as 8 raw bytes without a type byte
  uint32_t kmask = 0;    // bit i set: parameter i+1 is a %k% raw 128-byte string buffer (no type byte), verified on SA Android main.scm
};

struct ModeInfo {
  std::string id, title, game, extends, type;
  bool sbl = false;  // new-language (SBL) mode: opcodes come from a JSON library instead of SCM.ini files
};
// Every <dataDir>/<mode>/mode.xml (the "game select" list of Sanny Builder).
std::vector<ModeInfo> listModes(const std::string& dataDir);

class Data {
 public:
  // Loads everything an "sa_mobile" edit mode needs, in the order mode.xml lists it.
  // dataDir is the Sanny Builder "data" folder (contains sa/, sa_mobile/ ...).
  // Game enum used by sanny.exe (0x58befc): gta3=0 vc=1 sa=2 lcs=3 vcs=4 vc_mobile=5 sa_mobile=6
  int gameEnum = 2;
  // per-game opcode constants recovered from sanny.exe (getters at 0x5a6d14 if, 0x5a6d40 jf, 0x5a6d60 gosub, 0x5a6ce8 start_mission)
  uint16_t ifOp = 0x00D6, jfOp = 0x004D, gosubOp = 0x0050, startMissionOp = 0x0417;
  // true when the binary encoding of this game has been verified against real compiled scripts (SA family only so far)
  // "old" parameter encoding of GTA III / VC / VC Mobile: types 1..6, float = int16 / 16, text params are raw 8-byte fields
  bool oldFormat() const { return gameEnum == 0 || gameEnum == 1 || gameEnum == 5; }
  bool formatVerified() const { return gameEnum == 0 || gameEnum == 2 || gameEnum == 6; }  // gta3 (Android main.scm), sa, sa_mobile (Android main.scm)
  std::string modeId, game, title;                        // set by loadMode (game: sa, sa_mobile, gta3, vc, lcs, vcs ...)
  bool loadSaMobile(const std::string& dataDir, std::string* err);
  // Generic: reads <dataDir>/<modeId>/mode.xml and loads its <opcodes>, <variables>, <constants> exactly like Sanny
  // (supports * globs). modeId e.g. "sa", "sa_mobile", "gta3", "vc".
  bool loadMode(const std::string& dataDir, const std::string& modeId, std::string* err);

  void loadOpcodeIniText(const std::string& text);        // later definitions override earlier ones
  bool loadOpcodeIniFile(const std::string& path, std::string* err);
  void loadCustomVariablesText(const std::string& text);  // "N=NAME" -> $NAME
  void loadConstantsText(const std::string& text);        // TIMERA=40@ ...
  void loadOpTypesText(const std::string& text);          // optypes.txt from tools/gen_optypes.py (compile-time disambiguation)
  // classes.db (Sanny "class syntax": Player.Defined($P), 0@ = Car.Create(#X, x, y, z), Player.Money($P) += 100)
  struct ClassEntry { uint16_t op = 0; std::string math; int pos = 0, type = 0; };
  struct ClassMember { bool prop = false; uint16_t op = 0; int type = 0; std::vector<ClassEntry> entries; std::vector<std::string> hints; std::map<int, std::string> depEnums; };  // depEnums: #DEPRECATED_ENUMS "Class.Member.N = ^A^B=22" (N = 0-based index in the hint list)
  void loadClassesText(const std::string& text);
  std::map<std::string, ClassMember> classMembers;        // lower("class.member")
  std::string classesFrom;                                // mode whose classes.db was used (a mobile mode without its own classes.db borrows the PC game's)
  std::map<std::string, std::string> classNames;          // lower(class) -> class
  void loadEnumsText(const std::string& text);            // enums.txt: "enum Name / Member=value / end"
  std::map<std::string, std::map<std::string, long long>> enums;  // lower(enum) -> lower(member) -> value
  std::map<std::string, long long> enumMembers;                   // lower(member) -> value (first definition wins)
  void loadKeywordsText(const std::string& text);         // "0001=wait" aliases (keywords.txt)
  std::map<std::string, uint16_t> keywords;               // lower(word) -> opcode
  void loadModelsText(const std::string& text);           // "NAME=ID" lines
  void loadIdeText(const std::string& text);              // GTA .ide: "id, name, ..." lines
  std::map<std::string, int> models;                      // UPPER(NAME) -> id   (#NAME)
  std::map<int, std::string> modelNames;                  // id -> NAME
  std::map<uint16_t, std::string> opTypes;                // "g:i,x:i" per parameter

  const OpDef* op(uint16_t opcode) const;

  std::map<uint16_t, OpDef> ops;
  std::map<int, std::string> globalNames;          // index -> NAME   ($index)
  std::map<std::string, int> globalByName;         // UPPER(NAME) -> index
  std::map<int, std::string> localNames;           // 40 -> TIMERA
  std::map<std::string, int> localByName;          // UPPER(TIMERA) -> 40
  std::map<std::string, long long> constants;      // false=0,true=1
};

struct Options {
  std::string labelPrefix = "Label";  // decompiler: @Label_<offset>
  bool negativeLabels = true;         // CLEO style: label operand is -(offset from script start)
  bool trailingSpace = true;          // Sanny prints "0001: wait 0 ms " (trailing blank)
  bool scriptStyle = false;           // Sanny script style: keywords (wait 0 / jf @L / thread 'X'), bare operator lines
  bool autoLabelPrefix = true;        // scriptStyle: labels are <thread name>_<offset> (e.g. FLOAT_11)
  std::vector<long long> extraLabelTargets;  // decompile: absolute offsets that must get a label although this block does not jump there
  std::vector<std::string> objectNames;     // compile: names of the "DEFINE OBJECT" entries (index i -> model id -i) when compiling a block of a main.scm
  std::map<std::string, int> allocNames;    // compile: "Alloc($NAME, n)" bindings (UPPER name -> slot) declared outside the block
  std::string externalPrefix;         // decompile of a relative-label block: positive operands (absolute offsets in other blocks) print as @<prefix>_<n>
  bool forceRelativeLabels = false;   // compile: label operands are negative offsets from the block start (mission scripts) without a {$E} line
  std::map<std::string, uint32_t> externalLabels;  // compile: labels of other blocks (UPPER name -> absolute offset) used as positive operands
  uint32_t baseOffset = 0;            // file offset of byte 0 of the code being (de)compiled; absolute jump operands refer to it (main.scm)
  bool strictStringLimit = false;     // true: Sanny 4.2.0 limits for 'text' parameters (7 chars; 15 on sa / vc_mobile) -> ERR_0027. false (default): longer text is accepted
  bool unknownModelsAsZero = false;   // testing aid: unknown #MODEL names compile as 0 instead of ERR_0058 / ERR_0083
  bool allowUnverified = false;       // compile/decompile games whose binary encoding is not verified yet (experimental)
  std::string cleoExt;                // decompile: emit "{$CLEO .csa}" header when non-empty
};

struct DecompileResult {
  bool ok = false;
  std::string text;
  std::vector<Diag> diags;
};
// main.scm header metadata parsed from "DEFINE ..." lines (binary header layout per game: not emitted yet)
struct ScriptDefs {
  std::vector<std::string> objects;                                   // DEFINE OBJECT name  (index i -> model id -i)
  int declaredObjects = -1, declaredMissions = -1, declaredExternals = -1;
  std::vector<std::pair<int, std::string>> missions;                  // DEFINE MISSION i AT @label
  std::vector<std::pair<std::string, std::string>> externalScripts;   // DEFINE SCRIPT name AT @label
  std::vector<std::pair<std::string, long long>> unknownSegments;     // DEFINE UNKNOWN_THREADS_MEMORY n, UNKNOWN_EMPTY_SEGMENT n
  std::vector<std::pair<int, size_t>> missionBlocks;                  // "//---- Mission N ----" marker -> byte offset in code
  size_t mainStart = (size_t)-1;                                      // "//---- MAIN ----" marker
};

struct CompileResult {
  bool ok = false;
  int maxGlobal = -1;                              // highest global variable slot referenced (arrays / string vars extended)
  std::map<std::string, uint32_t> labels;          // UPPER label name -> offset inside this block
  std::vector<uint8_t> bytes;
  std::vector<Diag> diags;
  ScriptDefs defs;
};

DecompileResult decompile(const uint8_t* data, size_t size, const Data& db, const Options& opt);
CompileResult compile(const std::string& source, const Data& db, const Options& opt);

// ---- Sanny "extra info" trailer: [code]["FLAG\0" u32]["SRC\0" u32 len, text][u32 codeSize]["__SBFTR\0"]
struct ExtraInfo {
  bool present = false;
  size_t codeSize = 0;   // bytes of real script before the trailer
  bool hasFlags = false;
  uint32_t flags = 0;
  std::string source;    // original source text embedded by "add extra info"
};
ExtraInfo readExtraInfo(const uint8_t* data, size_t size);
std::vector<uint8_t> writeExtraInfo(const std::vector<uint8_t>& code, uint32_t flags, const std::string& source);

}  // namespace sb
