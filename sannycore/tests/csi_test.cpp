// Oracle for scripts compiled with Sanny's "add extra info": the file embeds its own source, so
// compile(embedded source) must reproduce the code bytes exactly.
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include "sbcore.h"
static std::vector<std::string> lines(const std::string& t) { std::vector<std::string> r; std::istringstream s(t); std::string l; while (std::getline(s, l)) { while (!l.empty() && (l.back() == '\r')) l.pop_back(); r.push_back(l); } return r; }
static std::vector<std::string> toks(const std::string& l) { std::vector<std::string> r; std::istringstream s(l); std::string t; while (s >> t) r.push_back(t); return r; }
int main(int, char** argv) {
  sb::Data db; std::string e;
  if (!db.loadMode(argv[1], argv[2], &e)) { std::cerr << e << "\n"; return 2; }
  std::ifstream f(argv[3], std::ios::binary); std::vector<uint8_t> in((std::istreambuf_iterator<char>(f)), {});
  auto ex = sb::readExtraInfo(in.data(), in.size());
  std::cout << "extra info: " << ex.present << " codeSize " << ex.codeSize << " flags 0x" << std::hex << ex.flags << std::dec << " source " << ex.source.size() << " chars\n";
  if (!ex.present) return 2;
  sb::Options o; o.labelPrefix = "L";
  auto dr = sb::decompile(in.data(), ex.codeSize, db, o);
  for (auto& d : dr.diags) std::cout << "decompile diag ERR_" << d.code << ": " << d.msg << "\n";
  std::cout << "models from .ide files: " << db.models.size() << " (TEC9=" << db.models["TEC9"] << " SAWNOFF=" << db.models["SAWNOFF"] << ")\n";
  { sb::Options so; so.scriptStyle = true;
    auto sd = sb::decompile(in.data(), ex.codeSize, db, so);
    std::ofstream("/tmp/style_dec.txt") << sd.text;
    std::ofstream("/tmp/style_src.txt") << ex.source; }
  sb::Options co;
  auto c = sb::compile(ex.source, db, co);
  std::cout << "compile: " << c.bytes.size() << " bytes, " << c.diags.size() << " diags\n";
  std::map<int,int> byCode;
  for (size_t i = 0; i < c.diags.size(); i++) { byCode[c.diags[i].code]++; if (i < 12) std::cout << "  line " << c.diags[i].line << " ERR_" << c.diags[i].code << ": " << c.diags[i].msg.substr(0, 120) << "\n"; }
  for (auto& p : byCode) std::cout << "  ERR_" << p.first << " x" << p.second << "\n";
  std::vector<uint8_t> code(in.begin(), in.begin() + ex.codeSize);
  if (c.bytes == code) { std::cout << "IDENTICAL code bytes\n"; return 0; }
  size_t n = std::min(code.size(), c.bytes.size()), first = (size_t)-1, diff = 0;
  for (size_t i = 0; i < n; i++) if (code[i] != c.bytes[i]) { diff++; if (first == (size_t)-1) first = i; }
  for (size_t i = 0; i < n; i++) if (code[i] != c.bytes[i]) { printf("  @%zu orig:", i); for (size_t j = i > 8 ? i - 8 : 0; j < i + 8; j++) printf(" %02X", code[j]); printf("\n  @%zu mine:", i); for (size_t j = i > 8 ? i - 8 : 0; j < i + 8; j++) printf(" %02X", c.bytes[j]); printf("\n"); }
  std::cout << "differ: sizes " << code.size() << " vs " << c.bytes.size() << ", first diff " << (long)first << ", " << diff << " bytes\n";
  return 1;
}
