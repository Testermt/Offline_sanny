// End-to-end demo: a CLEO-style script with labels, `if and`, strings, floats, arrays.
#include <iostream>
#include "sbcore.h"
int main(int argc, char** argv) {
  sb::Data db; std::string e;
  if (!db.loadSaMobile(argv[1], &e)) { std::cerr << e << "\n"; return 2; }
  const char* src =
    "{$CLEO .csa}\n"
    "0000: NOP\n"
    ":Loop\n"
    "0001: wait 250 ms\n"
    "00D6: if and\n"
    "0256:   player $PLAYER_CHAR defined\n"
    "00FF:   actor $PLAYER_ACTOR sphere 1 in_sphere 2488.5 -1666.9 13.3 radius 5.0 5.0 5.0 on_foot\n"
    "004D: jump_if_false @Loop\n"
    "00BA: show_text_styled GXT 'BEEFY' time 1000 style 2\n"
    "0002: jump @Loop\n";
  sb::Options opt;
  auto c = sb::compile(src, db, opt);
  for (auto& d : c.diags) std::cout << "diag ERR_" << d.code << " line " << d.line << ": " << d.msg << "\n";
  std::cout << "compiled " << c.bytes.size() << " bytes: ";
  for (auto b : c.bytes) printf("%02X ", b);
  std::cout << "\n--- decompiled ---\n";
  opt.cleoExt = ".csa";
  auto r = sb::decompile(c.bytes.data(), c.bytes.size(), db, opt);
  std::cout << r.text;
  return c.ok ? 0 : 1;
}
