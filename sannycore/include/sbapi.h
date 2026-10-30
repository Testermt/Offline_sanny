// High level API: every kind of GTA San Andreas (Android) script file in one place.
//   main.scm / mainV1.scm : header segments + MAIN + missions   <->  one text file ("DEFINE ..." header + "//-------------MAIN---" + "//----Mission N---")
//   CLEO scripts (.csa .cs .csi), with or without Sanny's "extra info" trailer   <->  Sanny text (hex .. end data blocks included)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "sbcore.h"
#include "sbmainscm.h"

namespace sb {

enum class FileKind { Unknown, MainScm, CleoScript };

FileKind detectKind(const std::vector<uint8_t>& file);   // by content (segment chain => main.scm)
FileKind detectSourceKind(const std::string& text);      // "DEFINE OBJECTS" + "//-------------MAIN" => main.scm source

struct TextResult { bool ok = false; FileKind kind = FileKind::Unknown; std::string text; std::vector<Diag> diags; };
struct BinaryResult {
  bool ok = false; FileKind kind = FileKind::Unknown; std::vector<uint8_t> bytes; std::vector<Diag> diags;
  // main.scm only: code of the external scripts ("//-------------External script N (NAME)---------------" sections). The header holds their
  // names / offsets / sizes; the code itself belongs into script.img (not written by sannycore yet).
  std::vector<std::pair<std::string, std::vector<uint8_t>>> externals;
};

struct AnyOptions {
  bool scriptStyle = true;         // CLEO scripts: keywords (wait 0, jf @L, thread 'X'); main.scm text always uses the plain "XXXX: opcode" form
  bool addExtraInfo = false;       // CLEO compile: embed the source like Sanny's "add extra info"
  uint32_t extraInfoFlags = 0x1420;
  std::string cleoDirective;       // CLEO decompile header, e.g. ".csa" -> "{$CLEO .csa}"
  bool allowUnverified = false;
  bool unknownModelsAsZero = false; // testing aid: unknown #MODEL names compile as 0 instead of ERR_0083 (load the game's .ide files with Data::loadIdeText instead)
  bool strictStringLimit = false; // Sanny 4.2.0 text-length limits (ERR_0027); default: accept longer text
};

TextResult decompileAny(const std::vector<uint8_t>& file, const Data& db, const AnyOptions& opt);
BinaryResult compileAny(const std::string& source, const Data& db, const AnyOptions& opt);

// main.scm specific entry points
TextResult decompileMainScm(const std::vector<uint8_t>& file, const Data& db, const AnyOptions& opt);
BinaryResult compileMainScm(const std::string& source, const Data& db, const AnyOptions& opt);

}  // namespace sb
