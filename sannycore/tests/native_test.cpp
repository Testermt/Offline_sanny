// Sanny's own main.scm source (e.g. Improved Mobile mainV1.txt) -> main.scm bytes + external scripts
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include "sbapi.h"
int main(int, char** argv) {
  sb::Data db; std::string e; if (!db.loadMode(argv[1], argv[2], &e)) { std::cerr << e << "\n"; return 2; }
  db.loadOpcodeIniText("00C3=0,mobile_00C3\n00C4=0,mobile_00C4\n0A93=0,terminate_this_custom_script\n");
  std::ifstream f(argv[3], std::ios::binary); std::string src((std::istreambuf_iterator<char>(f)), {});
  sb::AnyOptions ao; ao.allowUnverified = true; ao.strictStringLimit = false; ao.unknownModelsAsZero = true;
  std::cout << "source kind: " << (sb::detectSourceKind(src) == sb::FileKind::MainScm ? "main.scm" : "cleo") << "\n";
  auto t0 = std::chrono::steady_clock::now();
  auto r = sb::compileAny(src, db, ao);
  std::cout << "compile: " << r.bytes.size() << " bytes in " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s, " << r.diags.size() << " diags, " << r.externals.size() << " external scripts\n";
  std::map<int, int> by; for (auto& d : r.diags) by[d.code]++;
  for (auto& p : by) { std::cout << "  ERR_" << p.first << " x" << p.second; for (auto& d : r.diags) if (d.code == p.first) { std::cout << "  e.g. " << d.msg.substr(0, 70); break; } std::cout << "\n"; }
  sb::MainScm m; std::string pe;
  if (!sb::parseMainScm(r.bytes, &m, &pe)) { std::cout << "result does not parse: " << pe << "\n"; return 1; }
  size_t codeStart = r.bytes.size(); for (auto& b : m.blocks) codeStart -= b.size();
  std::cout << "header: vars " << m.varBytes << " B, objects " << m.objects.size() << ", mission entries " << m.missionOffsets.size() << ", externals " << m.externals.size()
            << " (largest " << m.largestExternal << "), threadsMemory " << m.threadsMemory[0] << "/" << m.threadsMemory[1] << ", main code " << m.blocks[0].size() << " B at " << codeStart << "\n";
  size_t es = 0; for (auto& x : r.externals) es += x.second.size();
  std::cout << "external code total " << es << " B; last entries:";
  for (size_t i = m.externals.size() >= 2 ? m.externals.size() - 2 : 0; i < m.externals.size(); i++) std::cout << " " << m.externals[i].name << "@" << m.externals[i].offset << "+" << m.externals[i].size;
  std::cout << "\n";
}
