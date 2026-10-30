// Every classes.db member must compile to the same bytes as the equivalent plain opcode line.
#include <iostream>
#include "sbcore.h"
static std::string args(int n, const char* sep) { std::string s; for (int i = 0; i < n; i++) s += (i ? sep : "") + std::to_string(i) + "@"; return s; }
int main(int, char** argv) {
  sb::Data db; std::string e; db.loadMode(argv[1], argv[2], &e);
  std::cout << argv[2] << ": " << db.classMembers.size() << " class members (classes from " << db.classesFrom << ")\n";
  int ok = 0, mismatch = 0, missingOp = 0, skipped = 0; std::string firstBad, firstMissing;
  auto compileOne = [&](const std::string& t) { sb::Options o; o.allowUnverified = true; return sb::compile(t + "\n", db, o); };
  for (auto& kv : db.classMembers) {
    size_t dot = kv.first.find('.');
    std::string cls = db.classNames[kv.first.substr(0, dot)], name = kv.first.substr(dot + 1);
    auto test = [&](uint16_t op, const std::string& classLine, int nparams, const std::string& rawArgs) {
      const sb::OpDef* d = db.op(op);
      if (!d) { missingOp++; if (firstMissing.empty()) firstMissing = kv.first + " -> " + std::to_string(op); if (getenv("VERBOSE")) { char b2[8]; snprintf(b2, sizeof b2, "%04X", op); std::cout << "   missing: " << kv.first << " (" << b2 << ")\n"; } return; }
      if (d->nparams < 0 || d->nparams != nparams || d->kmask) { skipped++; return; }
      char hx[8]; snprintf(hx, sizeof hx, "%04X", op);
      auto a = compileOne(classLine), b = compileOne(std::string(hx) + ": " + rawArgs);
      if (a.ok && b.ok && a.bytes == b.bytes) ok++; else { mismatch++; if (firstBad.empty()) firstBad = classLine + "  vs  " + hx + ": " + rawArgs + (a.ok ? "" : "  [" + a.diags[0].msg.substr(0, 60) + "]"); }
    };
    const auto& m = kv.second;
    // number of hint-declared parameters is unknown here: use the opcode's own parameter count
    if (!m.prop) {
      const sb::OpDef* d = db.op(m.op);
      int n = d ? d->nparams : 0;
      if (n < 0) { skipped++; continue; }
      test(m.op, cls + "." + name + (n ? "(" + args(n, ", ") + ")" : ""), n, args(n, " "));
    } else {
      for (auto& en : m.entries) {
        const sb::OpDef* d = db.op(en.op);
        int n = d ? d->nparams : 0;
        if (n < 1) { if (!d) test(en.op, "", 0, ""); else skipped++; continue; }
        std::string callArgs = args(n - 1, ", ");
        if (en.pos == 2) test(en.op, std::to_string(n - 1) + "@ = " + cls + "." + name + "(" + callArgs + ")", n, args(n - 1, " ") + (n > 1 ? " " : "") + std::to_string(n - 1) + "@");
        else test(en.op, cls + "." + name + "(" + callArgs + ") " + en.math + " " + std::to_string(n - 1) + "@", n, args(n, " "));
      }
    }
  }
  std::cout << "identical to the plain opcode form: " << ok << ", mismatches: " << mismatch << ", opcode missing in this mode: " << missingOp << ", skipped (variadic/%k%/count): " << skipped << "\n";
  if (!firstBad.empty()) std::cout << "first mismatch: " << firstBad << "\n";
  if (!firstMissing.empty()) std::cout << "first missing opcode: " << firstMissing << "\n";
  return mismatch != 0;
}
