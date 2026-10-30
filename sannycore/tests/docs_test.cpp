// Examples from docs.sannybuilder.com (control-flow: loops, conditions, switch).
#include <iostream>
#include "sbcore.h"
static int fails = 0;
static void run(sb::Data& db, const char* title, const char* src, int expectDiags = 0, bool show = false) {
  sb::Options o;
  auto c = sb::compile(src, db, o);
  bool pass = (int)c.diags.size() == expectDiags;
  std::cout << (pass ? "OK   " : "FAIL ") << title << " (" << c.bytes.size() << " bytes, " << c.diags.size() << " diags)\n";
  for (auto& d : c.diags) std::cout << "      line " << d.line << " ERR_" << d.code << ": " << d.msg.substr(0, 90) << "\n";
  if (!pass) fails++;
  if (show && c.ok) {
    sb::Options so; so.scriptStyle = true; so.labelPrefix = "L"; so.autoLabelPrefix = false;
    std::cout << sb::decompile(c.bytes.data(), c.bytes.size(), db, so).text;
  }
}
int main(int, char** argv) {
  sb::Data db; std::string e; db.loadMode(argv[1], argv[2], &e);
  bool show = getenv("SHOW") != nullptr;
  run(db, "conditions: if/else", "{$CLEO .cs}\n0000:\nif\n  00E1: player 0 pressed_key 17\nthen\n  0ace: \"player is ready\"\nelse\n  0ace: \"player is not ready\"\nend\n0A93:\n");
  run(db, "while true + if and + Break (docs hint)", "{$CLEO .cs}\n0000:\nwhile true\n  wait 0 ms\n  if and\n    31@ >= 0\n    31@ <= 100\n  then\n    Break\n  end\nend\n0A93:\n");
  run(db, "repeat .. until false + Break", "{$CLEO .cs}\n0000:\nrepeat\n  wait 0 ms\n  if\n    31@ == 5\n  then\n    break\n  end\nuntil false\n0A93:\n");
  run(db, "repeat .. until true (single iteration)", "{$CLEO .cs}\n0000:\nrepeat\n  wait 0 ms\nuntil true\n0A93:\n");
  run(db, "jf Break / jf Continue as operands", "{$CLEO .cs}\n0000:\nwhile true\n  wait 0 ms\n  if\n    31@ == 1\n  jf Break\n  if\n    31@ == 2\n  then\n    Continue\n  end\nend\n0A93:\n");
  run(db, "while false is ignored", "{$CLEO .cs}\n0000:\nwhile false\n  wait 0 ms\n  if\n    31@ == 1\n  then\n    wait 1 ms\n  end\nend\n0A93:\n", 0, show);
  run(db, "for with step", "{$CLEO .cs}\n0000:\nfor 0@ = 1 to 100 step 2\n  wait 0 ms\nend\n0A93:\n");
  run(db, "for with step = N, variable bounds", "{$CLEO .cs}\n0000:\nfor 0@ = 1 to 1@ step = 2\n  wait 0 ms\nend\n0A93:\n", 0, show);
  run(db, "for downto", "{$CLEO .cs}\n0000:\nfor 0@ = 10 downto 1\n  wait 0 ms\nend\n0A93:\n");
  run(db, "ERR_0025 for 10 to 1", "{$CLEO .cs}\n0000:\nfor 0@ = 10 to 1\n  wait 0 ms\nend\n0A93:\n", 1);
  run(db, "ERR_0025 for 1 downto 10", "{$CLEO .cs}\n0000:\nfor 0@ = 1 downto 10\n  wait 0 ms\nend\n0A93:\n", 1);
  run(db, "switch (docs example)", "{$CLEO .cs}\n0000:\nswitch 0@\n   case 1, 2, 3\n     0ace: \"Value is 1, 2, or 3\"\n   case 5\n     0ace: \"Value is 5\"\n   default\n     0ace: \"Value is not 1, 2, 3, or 5\"\nend\n0A93:\n", 0, show);
  run(db, "switch without default", "{$CLEO .cs}\n0000:\nswitch 0@\n   case 7\n     wait 1 ms\nend\n0A93:\n");
  run(db, "ERR_0102 switch without variable", "{$CLEO .cs}\nswitch\n  case 1\n    wait 0 ms\nend\n", 1);
  run(db, "ERR_0103 stray line in switch", "{$CLEO .cs}\nswitch 0@\n  wait 0 ms\n  case 1\n    wait 0 ms\nend\n", 1);
  run(db, "ERR_0104 case without value", "{$CLEO .cs}\nswitch 0@\n  case\n    wait 0 ms\nend\n", 1);
  std::cout << (fails ? "SOME FAILED\n" : "ALL OK\n");
  return fails != 0;
}
