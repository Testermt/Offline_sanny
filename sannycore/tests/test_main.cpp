// Golden test: data/sa_mobile/opcodes.txt holds ~1800 lines of REAL Sanny Builder decompiler output.
// For each line: compile it with sannycore, decompile the bytes, and compare with the original line.
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include "sbcore.h"

static std::string stripComment(const std::string& s) {
  bool inq = false; char q = 0;
  for (size_t i = 0; i + 1 < s.size(); i++) {
    if (inq) { if (s[i] == q) inq = false; continue; }
    if (s[i] == '\'' || s[i] == '"') { inq = true; q = s[i]; continue; }
    if (s[i] == '/' && s[i + 1] == '/') return s.substr(0, i);
  }
  return s;
}
static std::string rtrim(std::string s) { while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; }
static std::string normLabels(const std::string& s) { static std::regex r("@[A-Za-z0-9_]+"); return std::regex_replace(s, r, "@L"); }

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "usage: sbtest <sanny data dir>\n"; return 2; }
  sb::Data db; std::string err;
  if (!db.loadSaMobile(argv[1], &err)) { std::cerr << err << "\n"; return 2; }
  std::cout << "opcodes loaded: " << db.ops.size() << "\n";
  std::ifstream f(std::string(argv[1]) + "/sa_mobile/opcodes.txt");
  std::string line; int total = 0, pass = 0, skipModel = 0, compileFail = 0, mismatch = 0;
  std::vector<std::string> bad;
  std::regex lineRe("^[0-9A-F]{4}:.*");
  sb::Options opt; opt.negativeLabels = false;
  while (std::getline(f, line)) {
    line = rtrim(line);
    if (!std::regex_match(line, lineRe)) continue;
    total++;

    std::string orig = rtrim(stripComment(line));
    std::string src = orig + "\n";
    std::set<std::string> labs; std::smatch m; std::string t = orig;
    static std::regex lr("@([A-Za-z0-9_]+)");
    while (std::regex_search(t, m, lr)) { labs.insert(m[1]); t = m.suffix(); }
    for (auto& l : labs) src += ":" + l + "\n";
    auto cr = sb::compile(src, db, opt);
    if (!cr.ok) { compileFail++; if (bad.size() < 400) bad.push_back("COMPILE  " + orig + "   <- " + cr.diags[0].msg); continue; }
    auto dr = sb::decompile(cr.bytes.data(), cr.bytes.size(), db, opt);
    std::string first = dr.text.substr(0, dr.text.find('\n'));
    // the labels sit past the end of code so decompile prints them as data; take the first line only
    std::string got = rtrim(stripComment(first));
    if (normLabels(got) == normLabels(orig)) pass++;
    else { mismatch++; if (bad.size() < 400) bad.push_back("MISMATCH [" + orig + "]\n          got [" + got + "]"); }
  }
  std::cout << "total sample lines : " << total << "\n"
            << "skipped (#MODEL)   : " << skipModel << "\n"
            << "round-trip PASS    : " << pass << "\n"
            << "compile failures   : " << compileFail << "\n"
            << "mismatches         : " << mismatch << "\n";
  for (auto& b : bad) std::cout << b << "\n";
  return (compileFail + mismatch) ? 1 : 0;
}
