// "Game select": list all edit modes of a Sanny data folder and load each one.
#include <iostream>
#include "sbcore.h"
int main(int, char** argv) {
  auto modes = sb::listModes(argv[1]);
  std::cout << modes.size() << " modes\n";
  int bad = 0;
  for (auto& m : modes) {
    sb::Data d; std::string e;
    bool ok = d.loadMode(argv[1], m.id, &e);
    printf("%-12s %-34s game=%-10s %s%s  ops=%zu models=%zu kw=%zu vars=%zu  %s\n", m.id.c_str(), m.title.c_str(), m.game.c_str(),
           m.extends.empty() ? "" : "extends ", m.extends.c_str(), d.ops.size(), d.models.size(), d.keywords.size(), d.globalNames.size(),
           ok ? "LOADED" : (m.sbl ? "SBL (not supported yet)" : ("FAILED: " + e).c_str()));
    if (!ok && !m.sbl) bad++;
  }
  return bad;
}
