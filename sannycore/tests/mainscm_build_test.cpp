// build(parse(file)) must reproduce the real main.scm byte for byte (header layout check)
#include <fstream>
#include <iostream>
#include <iterator>
#include "sbmainscm.h"
int main(int, char** argv) {
  std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
  sb::MainScm m; std::string e;
  if (!sb::parseMainScm(d, &m, &e)) { std::cout << "parse failed: " << e << "\n"; return 2; }
  auto b = sb::buildMainScm(m);
  std::cout << (m.saLayout ? "SA" : "III/VC") << " layout: vars " << m.varBytes << " B, objects " << m.objects.size() << ", missions " << m.missionOffsets.size()
            << ", externals " << m.externals.size() << ", blocks " << m.blocks.size() << "\n";
  std::cout << "rebuilt " << b.size() << " bytes vs original " << d.size() << " -> " << (b == d ? "IDENTICAL" : "DIFFERENT") << "\n";
  if (b != d) { size_t n = std::min(b.size(), d.size()); for (size_t i = 0; i < n; i++) if (b[i] != d[i]) { std::cout << "first diff @" << i << "\n"; break; } }
  return b != d;
}
