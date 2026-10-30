// Oracle: strip the "XXXX:" prefix from every decompiled line, compile by pattern only, compare with the original binary.
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include "sbcore.h"
int main(int, char** argv) {
  sb::Data db; std::string e; db.loadMode(argv[1], argv[2], &e);
  std::ifstream f(argv[3], std::ios::binary); std::vector<uint8_t> in((std::istreambuf_iterator<char>(f)), {});
  sb::Options o; o.cleoExt = ".cs";
  auto r = sb::decompile(in.data(), in.size(), db, o);
  std::istringstream ss(r.text); std::string line, src; int n = 0;
  std::vector<std::string> orig;
  while (std::getline(ss, line)) {
    bool pre = line.size() > 5 && line[4] == ':' && isxdigit((unsigned char)line[0]) && isxdigit((unsigned char)line[3]);
    if (pre) { orig.push_back(line); src += line.substr(6) + "\n"; n++; } else src += line + "\n";
  }
  sb::Options co;
  auto c = sb::compile(src, db, co);
  std::cout << n << " instruction lines, diags " << c.diags.size() << ", size " << c.bytes.size() << " vs " << in.size() << "\n";
  for (size_t i = 0; i < c.diags.size() && i < 8; i++) std::cout << " diag line " << c.diags[i].line << ": " << c.diags[i].msg.substr(0, 110) << "\n";
  if (c.bytes == in) { std::cout << "IDENTICAL bytes\n"; return 0; }
  // find first differing instruction by re-decompiling mine
  auto r2 = sb::decompile(c.bytes.data(), c.bytes.size(), db, o);
  std::istringstream a(r.text), b(r2.text); std::string la, lb; int shown = 0, k = 0;
  std::map<std::string,int> pairs;
  auto nl = [](std::string x) { size_t p; while ((p = x.find("Label_")) != std::string::npos) { size_t e = p + 6; while (e < x.size() && isdigit((unsigned char)x[e])) e++; x = x.substr(0, p) + "L#" + x.substr(e); } return x; };
  while (std::getline(a, la) && std::getline(b, lb)) { k++; if (nl(la) != nl(lb)) { pairs[la.substr(0,4) + " -> " + lb.substr(0,4)]++; if (shown++ < 6) std::cout << " L" << k << "\n  want " << la << "\n  got  " << lb << "\n"; } }
  for (auto& p : pairs) std::cout << p.first << " x" << p.second << "\n";
  return 1;
}
