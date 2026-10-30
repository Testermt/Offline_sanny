// Sanny-native source (Improved Mobile "Only Hot Coffee") compiled by sannycore vs. the real Rockstar mainV1.scm: header + block comparison.
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include "sbapi.h"
int main(int, char** argv) {
  sb::Data db; std::string e; db.loadMode(argv[1], "sa_mobile", &e);
  db.loadOpcodeIniText("00C3=0,mobile_00C3\n00C4=0,mobile_00C4\n0A93=0,terminate_this_custom_script\n");
  std::ifstream f1(argv[2], std::ios::binary); std::string src((std::istreambuf_iterator<char>(f1)), {});
  std::ifstream f2(argv[3], std::ios::binary); std::vector<uint8_t> real((std::istreambuf_iterator<char>(f2)), {});
  sb::AnyOptions ao; ao.allowUnverified = true; ao.unknownModelsAsZero = true;
  auto r = sb::compileAny(src, db, ao);
  sb::MainScm a, b; std::string pe;
  sb::parseMainScm(real, &a, &pe); sb::parseMainScm(r.bytes, &b, &pe);
  size_t ha = real.size(), hb = r.bytes.size(); for (auto& x : a.blocks) ha -= x.size(); for (auto& x : b.blocks) hb -= x.size();
  std::cout << "diags " << r.diags.size() << "; header sizes real " << ha << " mine " << hb << "; blocks real " << a.blocks.size() << " mine " << b.blocks.size() << "\n";
  // header field by field
  std::cout << "objects: " << a.objects.size() << " vs " << b.objects.size();
  size_t on = 0; for (size_t i = 0; i < std::min(a.objects.size(), b.objects.size()); i++) if (a.objects[i] != b.objects[i]) on++;
  std::cout << " (" << on << " names differ)\n";
  std::cout << "externals: " << a.externals.size() << " vs " << b.externals.size();
  size_t en = 0, es = 0; for (size_t i = 0; i < std::min(a.externals.size(), b.externals.size()); i++) { if (a.externals[i].name != b.externals[i].name) en++; if (a.externals[i].size != b.externals[i].size) es++; }
  std::cout << " (" << en << " names differ, " << es << " sizes differ)\n";
  std::cout << "var space " << a.varBytes << " vs " << b.varBytes << ", threadsMemory " << a.threadsMemory[0] << "/" << a.threadsMemory[1] << " vs " << b.threadsMemory[0] << "/" << b.threadsMemory[1] << ", largest external " << a.largestExternal << " vs " << b.largestExternal << "\n";
  size_t n = std::min(a.blocks.size(), b.blocks.size()), sizeEq = 0, strict = 0, sem = 0, pad = 0, other = 0;
  size_t posA = ha, posB = hb; int shown = 0;
  std::ofstream fa("/tmp/real_blocks.txt"), fb("/tmp/mine_blocks.txt");
  for (size_t i = 0; i < n; i++) {
    bool se = a.blocks[i].size() == b.blocks[i].size();
    if (se) sizeEq++;
    if (a.blocks[i] == b.blocks[i]) strict++;
    sb::Options oa; oa.allowUnverified = true; oa.negativeLabels = i > 0; oa.baseOffset = i == 0 ? (uint32_t)posA : 0; oa.labelPrefix = "L"; oa.externalPrefix = i > 0 ? "X" : "";
    sb::Options ob = oa; ob.baseOffset = i == 0 ? (uint32_t)posB : 0;
    auto ta = sb::decompile(a.blocks[i].data(), a.blocks[i].size(), db, oa), tb = sb::decompile(b.blocks[i].data(), b.blocks[i].size(), db, ob);
    if (i == 0) { /* nothing */ }
    fa << "##BLOCK " << i << "\n" << ta.text; fb << "##BLOCK " << i << "\n" << tb.text;
    if (ta.text == tb.text) sem++;
    else if (shown < 6 && se) {
      shown++;
      std::istringstream x(ta.text), y(tb.text); std::string lx, ly; int ln = 0, cnt = 0;
      while (std::getline(x, lx) && std::getline(y, ly)) { ln++; if (lx != ly && cnt < 3) { cnt++; std::cout << "  block " << i << " line " << ln << "\n    real: " << lx.substr(0, 110) << "\n    mine: " << ly.substr(0, 110) << "\n"; } }
    }
    if (se) for (size_t k = 0; k < a.blocks[i].size(); k++) if (a.blocks[i][k] != b.blocks[i][k]) { if (b.blocks[i][k] == 0) pad++; else other++; }
    posA += a.blocks[i].size(); posB += b.blocks[i].size();
  }
  std::cout << "blocks: equal size " << sizeEq << "/" << n << ", byte-identical " << strict << "/" << n << ", same decompiled text " << sem << "/" << n << "; differing bytes in equal-size blocks: " << pad << " junk padding, " << other << " other\n";
}
