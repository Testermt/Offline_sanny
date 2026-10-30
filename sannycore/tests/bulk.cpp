// bulk <data dir> <mode> <source.txt> [maxLines]: compile a big real-world source, report speed and diagnostics by kind.
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include "sbcore.h"
int main(int argc, char** argv) {
  sb::Data db; std::string e;
  if (!db.loadMode(argv[1], argv[2], &e)) { std::cerr << e << "\n"; return 2; }
  std::ifstream f(argv[3], std::ios::binary); std::string src((std::istreambuf_iterator<char>(f)), {});
  size_t maxLines = argc > 4 ? atoll(argv[4]) : (size_t)-1, n = 0, cut = src.size();
  for (size_t i = 0; i < src.size(); i++) if (src[i] == '\n' && ++n >= maxLines) { cut = i; break; }
  src.resize(cut);
  sb::Options o; o.allowUnverified = true; o.unknownModelsAsZero = true; o.negativeLabels = false;
  auto t0 = std::chrono::steady_clock::now();
  auto c = sb::compile(src, db, o);
  double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::cout << n << " lines, " << c.bytes.size() << " bytes, " << c.diags.size() << " diags, " << sec << " s\n";
  std::map<std::string, int> by; std::map<std::string, std::string> ex;
  for (auto& d : c.diags) {
    std::string key = "ERR_" + std::to_string(d.code);
    if (d.code == 9001) { std::string m = d.msg.substr(d.msg.find(':') + 2); size_t sp = m.find(' '); key += " " + m.substr(0, sp); if (m.size() > 0 && m[0] == ' ') key += "(indented)"; }
    by[key]++; if (!ex.count(key)) ex[key] = "line " + std::to_string(d.line) + ": " + d.msg.substr(0, 110);
  }
  std::vector<std::pair<int, std::string>> v; for (auto& p : by) v.push_back({p.second, p.first});
  std::sort(v.rbegin(), v.rend());
  for (size_t i = 0; i < v.size() && i < 25; i++) std::cout << v[i].first << "  " << v[i].second << "   e.g. " << ex[v[i].second] << "\n";
}
