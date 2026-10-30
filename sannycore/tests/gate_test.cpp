#include <iostream>
#include "sbcore.h"
int main(int, char** argv) {
  int bad = 0;
  for (const char* m : {"vc", "gta3", "lcs", "vcs_psp", "sa", "sa_mobile"}) {
    sb::Data db; std::string e; if (!db.loadMode(argv[1], m, &e)) { std::cout << m << ": load failed " << e << "\n"; bad++; continue; }
    sb::Options o; auto c = sb::compile("wait 0 ms\n", db, o);
    o.allowUnverified = true; auto c2 = sb::compile("0001: wait 0 ms\n", db, o);
    printf("%-10s game=%-9s enum=%d if=%04X jf=%04X gosub=%04X mission=%04X verified=%d  default:%s  allow:%zu bytes\n", m, db.game.c_str(), db.gameEnum, db.ifOp, db.jfOp, db.gosubOp, db.startMissionOp, db.formatVerified(), c.diags.empty() ? "ok" : "gated(9100)", c2.bytes.size());
  }
  return bad;
}
