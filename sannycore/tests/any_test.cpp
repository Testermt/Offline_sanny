// Full cycle on a real main.scm: file -> decompileAny -> text -> compileAny -> file, compared with the original.
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include "sbapi.h"
int main(int argc, char** argv) {
  sb::Data db; std::string e; if (!db.loadMode(argv[1], argv[2], &e)) { std::cerr << e << "\n"; return 2; }
  if (std::string(argv[2]) == "sa_mobile") db.loadOpcodeIniText("00C3=0,mobile_00C3\n00C4=0,mobile_00C4\n0A93=0,terminate_this_custom_script\n");
  std::ifstream f(argv[3], std::ios::binary); std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
  sb::AnyOptions ao; ao.allowUnverified = true;
  auto t0 = std::chrono::steady_clock::now();
  auto tr = sb::decompileAny(d, db, ao);
  double s1 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::cout << "decompile: kind " << (int)tr.kind << ", " << tr.text.size() << " chars, " << tr.diags.size() << " diags, " << s1 << " s\n";
  if (argc > 4) std::ofstream(argv[4]) << tr.text;
  t0 = std::chrono::steady_clock::now();
  auto br = sb::compileAny(tr.text, db, ao);
  double s2 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::cout << "compile: " << br.bytes.size() << " bytes (original " << d.size() << "), " << br.diags.size() << " diags, " << s2 << " s\n";
  for (size_t i = 0; i < br.diags.size() && i < 5; i++) std::cout << "  diag ERR_" << br.diags[i].code << ": " << br.diags[i].msg.substr(0, 100) << "\n";
  sb::MainScm a, b; std::string pe;
  if (!sb::parseMainScm(d, &a, &pe) || !sb::parseMainScm(br.bytes, &b, &pe)) { std::cout << "re-parse failed: " << pe << "\n"; return 1; }
  size_t codeA = d.size(), codeB = br.bytes.size(); for (auto& x : a.blocks) codeA -= x.size(); for (auto& x : b.blocks) codeB -= x.size();
  bool hdr = codeA == codeB && std::equal(d.begin(), d.begin() + codeA, br.bytes.begin());
  std::cout << "header: original " << codeA << " bytes, rebuilt " << codeB << " bytes -> " << (hdr ? "IDENTICAL" : "DIFFERENT") << "\n";
  if (!hdr && codeA == codeB) { for (size_t i = 0; i < codeA; i++) if (d[i] != br.bytes[i]) { std::cout << "  first header diff @" << i << " orig " << (int)d[i] << " mine " << (int)br.bytes[i] << "\n"; break; } }
  std::cout << "blocks: " << a.blocks.size() << " vs " << b.blocks.size() << "\n";
  size_t same = 0, sizeEq = 0, strict = 0;
  size_t n = std::min(a.blocks.size(), b.blocks.size());
  for (size_t i = 0; i < n; i++) {
    if (a.blocks[i].size() == b.blocks[i].size()) sizeEq++;
    if (a.blocks[i] == b.blocks[i]) strict++;
  }
  std::cout << "blocks with equal size: " << sizeEq << " / " << n << ", byte-identical: " << strict << " / " << n << "\n";
  // semantic equality per block + classification of the remaining byte differences
  size_t semantic = 0, pad = 0, other = 0;
  size_t posA = d.size(); for (auto& x : a.blocks) posA -= x.size();
  size_t posB = posA;
  for (size_t i = 0; i < n; i++) {
    sb::Options o; o.allowUnverified = true; o.negativeLabels = i > 0; o.baseOffset = i == 0 ? (uint32_t)posA : 0; o.labelPrefix = "L"; o.externalPrefix = i > 0 ? "X" : "";
    sb::Options o2 = o; o2.baseOffset = i == 0 ? (uint32_t)posB : 0;
    auto ta = sb::decompile(a.blocks[i].data(), a.blocks[i].size(), db, o), tb = sb::decompile(b.blocks[i].data(), b.blocks[i].size(), db, o2);
    if (ta.text == tb.text) semantic++;
    if (a.blocks[i].size() == b.blocks[i].size()) for (size_t k = 0; k < a.blocks[i].size(); k++) if (a.blocks[i][k] != b.blocks[i][k]) { if (b.blocks[i][k] == 0) pad++; else other++; }
    posA += a.blocks[i].size(); posB += b.blocks[i].size();
  }
  std::cout << "blocks semantically identical (same decompiled text): " << semantic << " / " << n << "; differing bytes: " << pad << " junk padding (orig nonzero, mine zero), " << other << " other\n";
  std::cout << (d.size() == br.bytes.size() ? "total size equal\n" : "total size differs\n");
}
