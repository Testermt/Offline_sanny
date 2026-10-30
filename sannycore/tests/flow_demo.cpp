#include <iostream>
#include "sbcore.h"
static void run(sb::Data& db, const char* title, const char* src, bool show = true) {
  sb::Options o;
  auto c = sb::compile(src, db, o);
  std::cout << "=== " << title << ": " << c.bytes.size() << " bytes, " << c.diags.size() << " diags\n";
  for (auto& d : c.diags) std::cout << "  ERR_" << d.code << " line " << d.line << ": " << d.msg.substr(0, 90) << "\n";
  if (!show || !c.ok) return;
  sb::Options so; so.scriptStyle = true; so.labelPrefix = "L"; so.autoLabelPrefix = false;
  auto d = sb::decompile(c.bytes.data(), c.bytes.size(), db, so);
  std::cout << d.text;
}
int main(int, char** argv) {
  sb::Data db; std::string e; db.loadMode(argv[1], argv[2], &e);
  run(db, "all constructs",
    "{$E}\n"
    "thread 'FLOWTST'\n"
    ":MAIN\n"
    "wait 0\n"
    "if and\n"
    "    0A51: is_widget_pressed 29\n"
    "    $531 > 5\n"
    "then\n"
    "    31@ = 0\n"
    "    while 31@ < 5\n"
    "        31@ += 1\n"
    "        if\n"
    "            31@ == 3\n"
    "        then\n"
    "            continue\n"
    "        else\n"
    "            if 31@ == 4 then\n"
    "                break\n"
    "            end\n"
    "        end\n"
    "        wait 0\n"
    "    end\n"
    "else\n"
    "    repeat\n"
    "        wait 0\n"
    "    until 0A51: is_widget_pressed 19\n"
    "end\n"
    "for 5@ = 0 to 9\n"
    "    wait 0\n"
    "end\n"
    "jump @MAIN\n");
  run(db, "missing end", "thread 'X'\nif\n  31@ == 1\nthen\n  wait 0\n", false);
  run(db, "stray end", "thread 'X'\nwait 0\nend\n", false);
  run(db, "break outside loop", "thread 'X'\nbreak\n", false);
  run(db, "low-level if still works", "thread 'X'\n:A\nwait 0\nif\n  31@ == 1\njf @A\nend_thread\n");
}
