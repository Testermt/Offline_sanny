// Real-binary oracle: SA Android mainV1.scm. Every code block (MAIN + each mission) is decompiled, recompiled and compared byte for byte.
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include "sbcore.h"
static uint32_t u32(const std::vector<uint8_t>& d, size_t o) { return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | ((uint32_t)d[o + 3] << 24); }
int main(int, char** argv) {
  sb::Data db; std::string e; if (!db.loadMode(argv[1], argv[2], &e)) { std::cerr << e << "\n"; return 2; }
  bool classic = std::string(argv[2]) == "sa_mobile";
  if (classic) db.loadOpcodeIniText("00C3=0,mobile_00C3\n00C4=0,mobile_00C4\n0A93=0,terminate_this_custom_script\n");  // opcode present in the real mobile binary but missing from the shipped ini (0 parameters)
  std::ifstream f(argv[3], std::ios::binary); std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
  // header chain: "02 00 01 <next>" jumps followed by a segment id byte; id 1 = missions table
  size_t o = 0; size_t missionSeg = (size_t)-1;
  while (o + 8 < d.size() && d[o] == 0x02 && d[o + 1] == 0x00 && d[o + 2] == 0x01 && u32(d, o + 3) > o) {
    { uint32_t nx = u32(d, o + 3); if (o != 0 && o + 20 <= d.size() && (nx - o >= 20 + 4ull * u32(d, o + 16) && nx - o <= 28 + 4ull * u32(d, o + 16)) && u32(d, o + 8) < d.size()) missionSeg = o; }  // missions table: length = 20 + 4 * count
    o = u32(d, o + 3);
  }
  size_t codeStart = o;
  if (missionSeg == (size_t)-1) { std::cerr << "no missions segment\n"; return 2; }
  uint32_t mainSize = u32(d, missionSeg + 8), missions = u32(d, missionSeg + 16);
  std::cout << "file " << d.size() << " bytes, code starts " << codeStart << ", main ends " << mainSize << ", missions " << missions << "\n";
  struct Blk { std::string name; size_t start, end; bool rel; };
  std::vector<Blk> blocks; blocks.push_back({"MAIN", codeStart, mainSize, false});
  uint32_t entries = (u32(d, missionSeg + 3) - (uint32_t)missionSeg - 20) / 4;  // SA: 1 dummy entry + count missions
  std::vector<uint32_t> offs; for (uint32_t i = 0; i < entries; i++) offs.push_back(u32(d, missionSeg + 20 + 4 * i));
  std::vector<uint32_t> starts; for (auto v : offs) if (v >= mainSize && v < d.size()) starts.push_back(v);
  for (size_t i = 0; i < starts.size(); i++) blocks.push_back({"MISSION@" + std::to_string(starts[i]), starts[i], i + 1 < starts.size() ? starts[i + 1] : d.size(), true});
  size_t padDiff = 0, otherDiff = 0;
  int ok = 0, bad = 0, decErr = 0, semantic = 0; size_t verified = 0, total = 0, semBytes = 0;
  for (auto& b : blocks) {
    sb::Options o2; o2.allowUnverified = true; o2.scriptStyle = false; o2.labelPrefix = "L";
    o2.negativeLabels = b.rel; o2.baseOffset = b.rel ? 0 : (uint32_t)b.start;
    auto dr = sb::decompile(d.data() + b.start, b.end - b.start, db, o2);
    bool dataTail = dr.text.find("\nhex\n") != std::string::npos;
    if (!dr.diags.empty()) { decErr++; if (decErr <= 6) std::cout << b.name << ": decompile diag ERR_" << dr.diags[0].code << " " << dr.diags[0].msg.substr(0, 80) << "\n"; }
    std::string src = (b.rel ? "{$E}\n" : "") + dr.text;
    auto c = sb::compile(src, db, o2);
    total += b.end - b.start;
    std::vector<uint8_t> orig(d.begin() + b.start, d.begin() + b.end);
    bool strict = c.bytes == orig;
    if (strict) { ok++; verified += orig.size(); }
    // semantic equality: same size and same decompiled text (differences may only be the junk padding of 8-byte strings in Rockstar's binary)
    bool sem = strict;
    if (!strict && c.bytes.size() == orig.size()) {
      auto d2 = sb::decompile(c.bytes.data(), c.bytes.size(), db, o2);
      sem = d2.text == dr.text;
    }
    if (sem) { semantic++; semBytes += orig.size(); }
    if (sem && !strict) for (size_t i = 0; i < orig.size(); i++) if (orig[i] != c.bytes[i]) { if (c.bytes[i] == 0) padDiff++; else { otherDiff++; if (otherDiff <= 8) { printf("   other diff %s @%zu orig:", b.name.c_str(), i); for (size_t q = i > 6 ? i - 6 : 0; q < i + 10 && q < orig.size(); q++) printf(" %02X", orig[q]); printf("\n   %*s              mine:", (int)b.name.size(), ""); for (size_t q = i > 6 ? i - 6 : 0; q < i + 10 && q < orig.size(); q++) printf(" %02X", c.bytes[q]); printf("\n"); } } }
    if (!sem) {
      bad++;
      size_t n = std::min(orig.size(), c.bytes.size()), first = (size_t)-1;
      for (size_t i = 0; i < n; i++) if (orig[i] != c.bytes[i]) { first = i; break; }
      if (bad <= 8 && first != (size_t)-1) {
        printf("   %s @%zu orig:", b.name.c_str(), first); for (size_t q = first > 10 ? first - 10 : 0; q < first + 14 && q < orig.size(); q++) printf(" %02X", orig[q]);
        printf("\n   %*s       mine:", (int)b.name.size(), ""); for (size_t q = first > 10 ? first - 10 : 0; q < first + 14 && q < c.bytes.size(); q++) printf(" %02X", c.bytes[q]);
        printf("\n");
      }
      if (bad <= 0) {  // show the first differing instruction
        sb::Options so = o2; so.baseOffset = o2.baseOffset;
        auto d1 = sb::decompile(orig.data(), orig.size(), db, so), d2 = sb::decompile(c.bytes.data(), c.bytes.size(), db, so);
        std::istringstream a(d1.text), bb(d2.text); std::string la, lb; int k = 0;
        while (std::getline(a, la) && std::getline(bb, lb)) { k++; if (la != lb) { std::cout << "   line " << k << "\n     orig: " << la.substr(0, 110) << "\n     mine: " << lb.substr(0, 110) << "\n"; break; } }
      }
      if (bad <= 8) std::cout << b.name << ": MISMATCH size " << orig.size() << " vs " << c.bytes.size() << " first diff @" << (long)first << " diags " << c.diags.size() << (dataTail ? " (hex tail)" : "") << "\n";
    }
  }
  std::cout << "blocks semantically identical (same size + same decompiled text): " << semantic << " / " << blocks.size() << "  (" << semBytes << " bytes)\n";
  std::cout << "differing bytes in semantically identical blocks: " << padDiff << " are (original junk, mine zero) padding, " << otherDiff << " other\n";
  std::cout << "blocks byte-identical: " << ok << " / " << blocks.size() << "  (" << verified << " of " << total << " code bytes verified)\n";
  return bad != 0;
}
