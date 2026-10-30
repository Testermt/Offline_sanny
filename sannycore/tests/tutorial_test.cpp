// Scripts from https://gtamods.com/wiki/CLEO/Tutorial (Flow of control section) + the "bare minimum" claims.
#include <iostream>
#include "sbcore.h"
static int fails = 0;
static void run(sb::Data& db, const char* title, const char* src) {
  sb::Options o;
  auto c = sb::compile(src, db, o);
  std::cout << (c.ok ? "OK   " : "FAIL ") << title << " (" << c.bytes.size() << " bytes)\n";
  for (auto& d : c.diags) std::cout << "      line " << d.line << " ERR_" << d.code << ": " << d.msg.substr(0, 100) << "\n";
  if (!c.ok) fails++;
  else {
    sb::Options so; so.scriptStyle = true; so.labelPrefix = "L"; so.autoLabelPrefix = false;
    auto d = sb::decompile(c.bytes.data(), c.bytes.size(), db, so);
    if (getenv("SHOW")) std::cout << d.text;
  }
}
int main(int, char** argv) {
  sb::Data db; std::string e; db.loadMode(argv[1], argv[2], &e);
  run(db, "hello world", "{$CLEO .cs}\nwait 1000 ms\n0ACD: show_text_highpriority \"Hello World!\" time 5000\n0A93: terminate_this_custom_script\n");
  run(db, "bare minimum (no descriptive text)", "{$CLEO .cs}\nwait 1000 ms\n0ACD: \"Hello World!\" 5000\n0A93:\n");
  run(db, "while true", "{$CLEO .cs}\n0000:\nwhile true\n    wait 0 ms\n    0ACD: show_text_highpriority \"Hello World!\" time 0\nend\n0A93: terminate_this_custom_script\n");
  run(db, "if then", "{$CLEO .cs}\n0000:\nwhile true\n    wait 0 ms\n    if\n        00E1:   player 0 pressed_button 17\n    then\n        0ACD: show_text_highpriority \"Hello World!\" time 0\n    end\nend\n0A93: terminate_this_custom_script\n");
  run(db, "if and", "{$CLEO .cs}\n0000:\nwhile true\n    wait 0 ms\n    if and\n        00E1:   player 0 pressed_button 13\n        00E1:   player 0 pressed_button 17\n    then\n        0ACD: show_text_highpriority \"Hello World!\" time 0\n    end\nend\n0A93: terminate_this_custom_script\n");
  run(db, "if or", "{$CLEO .cs}\n0000:\nwhile true\n    wait 0 ms\n    if or\n        00E1:   player 0 pressed_button 13\n        00E1:   player 0 pressed_button 17\n    then\n        0ACD: show_text_highpriority \"Hello World!\" time 0\n    end\nend\n0A93: terminate_this_custom_script\n");
  run(db, "gxt hook", "{$CLEO .cs}\nwait 1000 ms\n00BC: text_highpriority 'HELLO' time 5000 flag 1\n0A93: terminate_this_custom_script\n");
  std::cout << (fails ? "SOME FAILED\n" : "ALL OK\n");
  return fails != 0;
}
