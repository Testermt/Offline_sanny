#include <fstream>
#include <iostream>
#include <iterator>
#include "sbcore.h"
int main(int, char** argv) {
  sb::Data db; std::string e; db.loadMode(argv[1], argv[2], &e);
  std::ifstream f(argv[3], std::ios::binary); std::string src((std::istreambuf_iterator<char>(f)), {});
  std::ifstream g(argv[4], std::ios::binary); std::vector<uint8_t> ref((std::istreambuf_iterator<char>(g)), {});
  sb::Options o; o.allowUnverified = true; o.negativeLabels = true;
  auto c = sb::compile(src, db, o);
  for (auto& d : c.diags) std::cout << "diag line " << d.line << " ERR_" << d.code << ": " << d.msg.substr(0, 90) << "\n";
  std::cout << "compiled " << c.bytes.size() << " bytes, reference " << ref.size() << " bytes -> " << (c.bytes == ref ? "IDENTICAL" : "DIFFERENT") << "\n";
  if (c.bytes != ref) { size_t n = std::min(c.bytes.size(), ref.size()); for (size_t i = 0; i < n; i++) if (c.bytes[i] != ref[i]) { std::cout << "first diff @" << i << "\n"; break; } }
}
