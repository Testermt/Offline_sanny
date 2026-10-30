// CLEO script files through the unified API: file -> text -> file (and the "add extra info" trailer).
#include <fstream>
#include <iostream>
#include <iterator>
#include "sbapi.h"
int main(int argc, char** argv) {
  sb::Data db; std::string e; if (!db.loadMode(argv[1], argv[2], &e)) { std::cerr << e << "\n"; return 2; }
  int bad = 0;
  for (int i = 3; i < argc; i++) {
    std::ifstream f(argv[i], std::ios::binary); std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    sb::AnyOptions ao; ao.allowUnverified = true; ao.cleoDirective = ".csa";
    auto k = sb::detectKind(d);
    auto tr = sb::decompileAny(d, db, ao);
    sb::AnyOptions co = ao;
    auto ex = sb::readExtraInfo(d.data(), d.size());
    co.addExtraInfo = ex.present;
    auto br = sb::compileAny(tr.text, db, co);
    // compare the code part (extra info stores the text it was given, so compare code bytes only)
    size_t codeLen = ex.present ? ex.codeSize : d.size();
    bool codeEq = br.bytes.size() >= codeLen && std::equal(d.begin(), d.begin() + codeLen, br.bytes.begin());
    auto ex2 = sb::readExtraInfo(br.bytes.data(), br.bytes.size());
    printf("%-22s kind=%s extra=%d  decompile diags %zu  compile diags %zu  code %zu B -> %s%s\n", argv[i] + std::string(argv[i]).rfind('/') + 1, k == sb::FileKind::MainScm ? "main.scm" : "cleo", (int)ex.present,
           tr.diags.size(), br.diags.size(), codeLen, codeEq ? "IDENTICAL" : "DIFFERENT", ex.present ? (ex2.present && ex2.codeSize == ex.codeSize ? " (trailer rebuilt)" : " (trailer MISSING)") : "");
    if (!codeEq) bad++;
  }
  return bad;
}
