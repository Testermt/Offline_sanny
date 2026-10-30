// sbcli <sanny data dir> <script.cs|csa>  : decompile, recompile, compare bytes with the original
#include <fstream>
#include <iostream>
#include <iterator>
#include "sbcore.h"
int main(int argc, char** argv) {
  if (argc < 4) { std::cerr << "usage: sbcli <data dir> <mode id> <file>\n"; return 2; }
  sb::Data db; std::string e;
  if (!db.loadMode(argv[1], argv[2], &e)) { std::cerr << e << "\n"; return 2; }
  std::ifstream f(argv[3], std::ios::binary);
  std::vector<uint8_t> in((std::istreambuf_iterator<char>(f)), {});
  sb::Options opt; opt.cleoExt = ".cs";
  auto r = sb::decompile(in.data(), in.size(), db, opt);
  std::ofstream(std::string(argv[3]) + ".txt") << r.text;
  for (auto& d : r.diags) std::cerr << "decompile diag ERR_" << d.code << ": " << d.msg << "\n";
  std::cout << "input " << in.size() << " bytes, decompiled " << r.text.size() << " chars\n";
  sb::Options co;
  auto c = sb::compile(r.text, db, co);
  for (size_t i = 0; i < c.diags.size() && i < 15; i++) std::cerr << "compile diag ERR_" << c.diags[i].code << " line " << c.diags[i].line << ": " << c.diags[i].msg << "\n";
  std::cout << "recompiled " << c.bytes.size() << " bytes, " << c.diags.size() << " diags\n";
  size_t n = std::min(in.size(), c.bytes.size()), diff = 0, first = (size_t)-1;
  for (size_t i = 0; i < n; i++) if (in[i] != c.bytes[i]) { diff++; if (first == (size_t)-1) first = i; }
  if (in.size() == c.bytes.size() && !diff) std::cout << "IDENTICAL bytes\n";
  else std::cout << "differ: sizes " << in.size() << " vs " << c.bytes.size() << ", first diff at " << (long)first << ", " << diff << " bytes differ\n";
}
