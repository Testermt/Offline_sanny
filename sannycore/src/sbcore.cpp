#include "sbcore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace sb {
namespace {

// ---------------------------------------------------------------- string helpers
std::string ltrim(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
  return s.substr(i);
}
std::string rtrim(const std::string& s) {
  size_t e = s.size();
  while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
  return s.substr(0, e);
}
std::string trim(const std::string& s) { return ltrim(rtrim(s)); }
std::string upper(std::string s) {
  for (auto& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}
std::string lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
std::string hex4(unsigned v) {
  char b[16];
  snprintf(b, sizeof b, "%04X", v & 0xFFFF);
  return b;
}
std::vector<std::string> splitLines(const std::string& t) {
  std::vector<std::string> r;
  std::string cur;
  for (char c : t) {
    if (c == '\n') {
      r.push_back(rtrim(cur));
      cur.clear();
    } else
      cur += c;
  }
  if (!cur.empty()) r.push_back(rtrim(cur));
  return r;
}
bool readFile(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}
Diag mk(int code, const std::string& msg, int offset = -1, int line = -1) {
  Diag d;
  d.code = code;
  d.msg = msg;
  d.offset = offset;
  d.line = line;
  return d;
}
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof buf, f, ap);
  va_end(ap);
  return buf;
}

// ---------------------------------------------------------------- format patterns
struct Piece {
  bool ph = false;
  int idx = 0;       // 1-based
  char spec = 'd';
  std::string lit;
};

// Splits "wait %1d% ms ; comment" into pieces; comment (after ';') is returned separately.
void splitComment(const std::string& f, std::string* body, std::string* comment) {
  bool inq = false;
  char q = 0;
  for (size_t i = 0; i < f.size(); i++) {
    char c = f[i];
    if (inq) {
      if (c == q) inq = false;
    } else if (c == '\'' || c == '"') {
      inq = true;
      q = c;
    } else if (c == ';') {
      *body = f.substr(0, i);
      *comment = f.substr(i + 1);
      return;
    } else if (c == '/' && i + 1 < f.size() && f[i + 1] == '/') {  // VC / III ini files use "//" for comments
      *body = f.substr(0, i);
      *comment = f.substr(i + 2);
      return;
    }
  }
  *body = f;
  comment->clear();
}

std::vector<Piece> parsePattern(const std::string& body) {
  std::vector<Piece> out;
  std::string lit;
  for (size_t i = 0; i < body.size();) {
    if (body[i] == '%') {
      size_t j = i + 1;
      int n = 0;
      bool dig = false;
      while (j < body.size() && std::isdigit((unsigned char)body[j])) {
        n = n * 10 + (body[j] - '0');
        j++;
        dig = true;
      }
      if (dig && j + 1 < body.size() && body[j] == 'b' && body[j + 1] == ':') {
        // %Nb:textA/textB%[word]  -> a boolean parameter printed as a number; the alternatives (and a word glued after "/%") are descriptive only
        size_t e = body.find('%', j + 2);
        if (e != std::string::npos) {
          bool glued = e > j + 2 && body[e - 1] == '/' && e + 1 < body.size() && body[e + 1] != ' ';
          size_t after = e + 1;
          if (glued) while (after < body.size() && body[after] != ' ') after++;
          if (!lit.empty()) { Piece p; p.lit = lit; out.push_back(p); lit.clear(); }
          Piece p; p.ph = true; p.idx = n; p.spec = 'b'; out.push_back(p);
          i = after;
          continue;
        }
      }
      if (dig && j + 1 < body.size() && std::isalpha((unsigned char)body[j]) && body[j + 1] == '%') {
        if (!lit.empty()) {
          Piece p;
          p.lit = lit;
          out.push_back(p);
          lit.clear();
        }
        Piece p;
        p.ph = true;
        p.idx = n;
        p.spec = body[j];
        out.push_back(p);
        i = j + 2;
        continue;
      }
    }
    lit += body[i++];
  }
  if (!lit.empty()) {
    Piece p;
    p.lit = lit;
    out.push_back(p);
  }
  return out;
}

// ---------------------------------------------------------------- SCM argument model (SA)
enum ArgType : uint8_t {
  T_END = 0x00, T_I32 = 0x01, T_GVAR = 0x02, T_LVAR = 0x03, T_I8 = 0x04, T_I16 = 0x05, T_F32 = 0x06,
  T_GARR = 0x07, T_LARR = 0x08, T_STR8 = 0x09, T_GSTR8 = 0x0A, T_LSTR8 = 0x0B, T_GSTR8ARR = 0x0C,
  T_LSTR8ARR = 0x0D, T_VSTR = 0x0E, T_STR16 = 0x0F, T_GSTR16 = 0x10, T_LSTR16 = 0x11,
  T_GSTR16ARR = 0x12, T_LSTR16ARR = 0x13
};

static const uint8_t T_RAW8 = 0xFD;    // pseudo type: old-format text parameter, 8 raw bytes without a type byte
static const uint8_t T_RAW128 = 0xFE;  // pseudo type: %k% parameter, 128 raw bytes without a type byte

struct Arg {
  uint8_t type = 0;
  long long i = 0;
  float f = 0;
  uint16_t a = 0, b = 0;  // var offset/index; array: base, index var
  uint8_t size = 0, flags = 0;
  std::string s;
};

struct Insn {
  uint32_t off = 0;
  uint16_t op = 0;  // without the NOT bit
  bool neg = false;
  std::vector<Arg> args;
};

bool readArg(const uint8_t* d, size_t n, size_t& p, Arg& a, bool old = false) {
  if (p >= n) return false;
  a = Arg();
  a.type = d[p++];
  if (old) {  // GTA III / VC: only types 1..6 exist; type 6 is a fixed point number stored as int16 (value * 16)
    if (a.type < 1 || a.type > 6) return false;
    if (a.type == T_F32) {
      if (p + 2 > n) return false;
      int16_t v = (int16_t)(d[p] | (d[p + 1] << 8));
      a.f = (float)v / 16.0f; p += 2; return true;
    }
  }
  auto need = [&](size_t k) { return p + k <= n; };
  auto u16 = [&](size_t o) { return (uint16_t)(d[o] | (d[o + 1] << 8)); };
  switch (a.type) {
    case T_I32: if (!need(4)) return false; a.i = (int32_t)(d[p] | (d[p+1] << 8) | (d[p+2] << 16) | ((uint32_t)d[p+3] << 24)); p += 4; return true;
    case T_I16: if (!need(2)) return false; a.i = (int16_t)u16(p); p += 2; return true;
    case T_I8:  if (!need(1)) return false; a.i = (int8_t)d[p]; p += 1; return true;
    case T_F32: { if (!need(4)) return false; uint32_t u = d[p] | (d[p+1] << 8) | (d[p+2] << 16) | ((uint32_t)d[p+3] << 24); memcpy(&a.f, &u, 4); p += 4; return true; }
    case T_GVAR: case T_LVAR: case T_GSTR8: case T_LSTR8: case T_GSTR16: case T_LSTR16:
      if (!need(2)) return false; a.a = u16(p); p += 2; return true;
    case T_GARR: case T_LARR: case T_GSTR8ARR: case T_LSTR8ARR: case T_GSTR16ARR: case T_LSTR16ARR:
      if (!need(6)) return false; a.a = u16(p); a.b = u16(p + 2); a.size = d[p + 4]; a.flags = d[p + 5]; p += 6; return true;
    case T_STR8: case T_STR16: {
      size_t k = a.type == T_STR8 ? 8 : 16;
      if (!need(k)) return false;
      for (size_t j = 0; j < k && d[p + j]; j++) a.s += (char)d[p + j];
      p += k; return true;
    }
    case T_VSTR: {
      if (!need(1)) return false;
      size_t k = d[p++];
      if (!need(k)) return false;
      a.s.assign((const char*)d + p, k);
      p += k; return true;
    }
    default: return false;
  }
}

// Sanny prints floats in the shortest form that round-trips as float32 and always has a '.'.
std::string fmtFloat(float v) {
  if (std::isnan(v) || std::isinf(v)) return "0.0";
  char buf[64];
  for (int d = 1; d <= 20; d++) {
    snprintf(buf, sizeof buf, "%.*f", d, (double)v);
    if (strtof(buf, nullptr) == v) return buf;
  }
  snprintf(buf, sizeof buf, "%.1f", (double)v);
  return buf;
}

std::string varName(const Data& db, bool global, int idx, const char* pre, const char* post) {
  if (global) {
    auto it = db.globalNames.find(idx);
    std::string n = it != db.globalNames.end() ? it->second : std::to_string(idx);
    return std::string(pre) + "$" + n;  // pre is "" / "s" / "v"
  }
  auto it = db.localNames.find(idx);
  if (it != db.localNames.end() && std::strlen(post) == 0) return it->second;
  return std::to_string(idx) + "@" + post;
}

char elemLetter(uint8_t flags) {
  switch (flags & 0x7F) { case 1: return 'f'; case 2: return 's'; case 3: return 'v'; default: return 'i'; }
}

std::string printArg(const Arg& a, const Data& db) {
  switch (a.type) {
    case T_I32: case T_I16: case T_I8: return std::to_string(a.i);
    case T_F32: return fmtFloat(a.f);
    case T_GVAR: return varName(db, true, a.a / 4, "", "");
    case T_LVAR: return varName(db, false, a.a, "", "");
    case T_GSTR8: return varName(db, true, a.a / 4, "s", "");
    case T_LSTR8: return varName(db, false, a.a, "", "s");
    case T_GSTR16: return varName(db, true, a.a / 4, "v", "");
    case T_LSTR16: return varName(db, false, a.a, "", "v");
    case T_STR8: case T_RAW8: case T_STR16: return "'" + a.s + "'";
    case T_VSTR: case T_RAW128: return "\"" + a.s + "\"";
    case T_GARR: case T_LARR: case T_GSTR8ARR: case T_LSTR8ARR: case T_GSTR16ARR: case T_LSTR16ARR: {
      bool g = a.type == T_GARR || a.type == T_GSTR8ARR || a.type == T_GSTR16ARR;
      std::string base = varName(db, g, g ? a.a / 4 : a.a, "", "");
      bool idxLocal = (a.flags & 0x80) == 0;  // verified on Portal_Gun.cs: bit7 set = index var is GLOBAL
      std::string idx = varName(db, !idxLocal, idxLocal ? a.b : a.b / 4, "", "");
      return base + "(" + idx + "," + std::to_string(a.size) + elemLetter(a.flags) + ")";
    }
  }
  return "?";
}

bool isLabelArgType(uint8_t t) { return t == T_I32 || t == T_I16 || t == T_I8; }

long long labelTarget(long long v) { return v < 0 ? -v : v; }

}  // namespace

// ================================================================= Data
const OpDef* Data::op(uint16_t opcode) const {
  auto it = ops.find(opcode & 0x7FFF);
  return it == ops.end() ? nullptr : &it->second;
}

void Data::loadOpcodeIniText(const std::string& text) {
  for (const auto& line : splitLines(text)) {
    if (line.size() < 6 || line[4] != '=') continue;
    bool hex = true;
    for (int i = 0; i < 4; i++) hex = hex && std::isxdigit((unsigned char)line[i]);
    if (!hex) continue;
    size_t comma = line.find(',', 5);
    if (comma == std::string::npos) continue;
    OpDef d;
    d.nparams = atoi(line.substr(5, comma - 5).c_str());
    d.fmt = line.substr(comma + 1);
    for (size_t q = 0; q + 3 < d.fmt.size(); q++)  // %Nk%
      if (d.fmt[q] == '%' && std::isdigit((unsigned char)d.fmt[q + 1])) {
        size_t r = q + 1; int num = 0;
        while (r < d.fmt.size() && std::isdigit((unsigned char)d.fmt[r])) num = num * 10 + (d.fmt[r++] - '0');
        if (r + 1 < d.fmt.size() && d.fmt[r] == 'k' && d.fmt[r + 1] == '%' && num >= 1 && num <= 31) d.kmask |= 1u << (num - 1);
        if (r + 1 < d.fmt.size() && (d.fmt[r] == 's' || d.fmt[r] == 'g' || d.fmt[r] == 'z') && d.fmt[r + 1] == '%' && num >= 1 && num <= 31) d.smask |= 1u << (num - 1);
      }
    ops[(uint16_t)strtoul(line.substr(0, 4).c_str(), nullptr, 16)] = d;
  }
}

bool Data::loadOpcodeIniFile(const std::string& path, std::string* err) {
  std::string t;
  if (!readFile(path, &t)) {
    if (err) *err = fmt("Ini file %s not found. The compiler won't work!", path.c_str());
    return false;
  }
  loadOpcodeIniText(t);
  return true;
}

void Data::loadOpTypesText(const std::string& text) {
  for (const auto& line : splitLines(text))
    if (line.size() > 5 && line[4] == '=') opTypes[(uint16_t)strtoul(line.substr(0, 4).c_str(), nullptr, 16)] = line.substr(5);
}

void Data::loadClassesText(const std::string& text) {
  auto splitTop = [](const std::string& str, char sep) {
    std::vector<std::string> r; std::string cur; int depth = 0; bool inq = false;
    for (char ch : str) {
      if (ch == '"') inq = !inq;
      if (!inq) { if (ch == '[' || ch == '(') depth++; else if (ch == ']' || ch == ')') depth--; }
      if (!inq && depth == 0 && ch == sep) { r.push_back(trim(cur)); cur.clear(); continue; }
      cur += ch;
    }
    r.push_back(trim(cur));
    return r;
  };
  std::string cls;
  bool inList = false, inDep = false;
  std::map<std::string, std::map<int, std::string>> pendingEnums;  // from #DEPRECATED_ENUMS (it precedes the class blocks)
  for (const auto& raw : splitLines(text)) {
    std::string l = trim(raw);
    if (l.empty() || l[0] == ';') continue;
    if (l == "#CLASSESLIST") { inList = true; inDep = false; continue; }
    if (l == "#DEPRECATED_ENUMS") { inList = false; inDep = true; continue; }
    if (l == "#CLASSES") { inList = false; inDep = false; continue; }
    if (inDep) {  // Class.Member.N = ^A^B=22^C
      size_t eq = l.find('=');
      if (eq == std::string::npos) continue;
      std::string key = trim(l.substr(0, eq)), val = trim(l.substr(eq + 1));
      size_t d2 = key.rfind('.');
      if (d2 == std::string::npos) continue;
      pendingEnums[lower(key.substr(0, d2))][atoi(key.substr(d2 + 1).c_str())] = val;
      continue;
    }
    if (inList) { classNames[lower(l)] = l; continue; }
    if (l[0] == '$') {
      std::string w = upper(l);
      if (w == "$BEGIN" || w == "$END") continue;
      cls = l.substr(1);
      classNames[lower(cls)] = cls;
      continue;
    }
    if (cls.empty()) continue;
    auto parts = splitTop(l, ',');
    if (parts.empty()) continue;
    ClassMember m;
    {  // ("Hint1" "Hint2:^OptionA^OptionB" ...) -> m.hints
      size_t hp = l.find("(\"");
      if (hp != std::string::npos) {
        size_t q = hp;
        while ((q = l.find('"', q)) != std::string::npos) {
          size_t e = l.find('"', q + 1);
          if (e == std::string::npos) break;
          m.hints.push_back(l.substr(q + 1, e - q - 1));
          q = e + 1;
        }
      }
    }
    if (parts[0][0] == '^') {
      m.prop = true;
      std::string name = parts[0].substr(1);
      for (size_t k = 1; k < parts.size(); k++) {
        const std::string& pt = parts[k];
        if (pt.size() > 2 && pt[0] == '[' && pt.back() == ']') {
          auto f = splitTop(pt.substr(1, pt.size() - 2), ',');
          if (f.size() >= 4) {
            ClassEntry e;
            e.op = (uint16_t)strtoul(f[0].c_str(), nullptr, 16); e.math = f[1]; e.pos = atoi(f[2].c_str()); e.type = atoi(f[3].c_str());
            m.entries.push_back(e);
          }
        }
      }
      classMembers[lower(cls) + "." + lower(name)] = m;
    } else if (parts.size() >= 3) {
      m.op = (uint16_t)strtoul(parts[1].c_str(), nullptr, 16);
      m.type = atoi(parts[2].c_str());
      classMembers[lower(cls) + "." + lower(parts[0])] = m;
    }
  }
  for (auto& pe : pendingEnums) { auto it = classMembers.find(pe.first); if (it != classMembers.end()) it->second.depEnums = pe.second; }
}

void Data::loadEnumsText(const std::string& text) {
  std::string cur;
  for (const auto& raw : splitLines(text)) {
    std::string l = trim(raw.substr(0, raw.find("//")));
    if (l.empty() || l[0] == ';') continue;
    std::string lw = lower(l);
    if (lw.rfind("enum ", 0) == 0) { cur = lower(trim(l.substr(5))); enums[cur]; continue; }
    if (lw == "end") { cur.clear(); continue; }
    size_t eq = l.find('=');
    if (cur.empty() || eq == std::string::npos) continue;
    std::string k = lower(trim(l.substr(0, eq)));
    long long v = strtoll(trim(l.substr(eq + 1)).c_str(), nullptr, 0);
    enums[cur][k] = v;
    enumMembers.emplace(k, v);
  }
}

void Data::loadKeywordsText(const std::string& text) {
  for (const auto& line : splitLines(text)) {
    std::string l = trim(line);
    if (l.size() < 6 || l[0] == ';' || l[4] != '=') continue;
    keywords[lower(trim(l.substr(5)))] = (uint16_t)strtoul(l.substr(0, 4).c_str(), nullptr, 16);
  }
}

void Data::loadModelsText(const std::string& text) {
  for (const auto& line : splitLines(text)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos || line.empty() || line[0] == ';') continue;
    std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
    if (k.empty() || v.empty()) continue;
    models[upper(k)] = atoi(v.c_str());
    modelNames[atoi(v.c_str())] = k;
  }
}

void Data::loadIdeText(const std::string& text) {
  for (const auto& line : splitLines(text)) {
    std::string l = trim(line);
    if (l.empty() || !(std::isdigit((unsigned char)l[0]) || l[0] == '-')) continue;
    size_t c1 = l.find(',');
    if (c1 == std::string::npos) continue;
    int id = atoi(l.substr(0, c1).c_str());
    // IDE columns are separated by commas and/or tabs ("dodo<TAB><TAB>dodo,"): the model name is the first token after the id
    size_t b = c1 + 1;
    while (b < l.size() && (l[b] == ' ' || l[b] == '\t')) b++;
    size_t e = b;
    while (e < l.size() && l[e] != ',' && l[e] != ' ' && l[e] != '\t') e++;
    std::string name = l.substr(b, e - b);
    if (name.empty()) continue;
    models[upper(name)] = id;
    modelNames[id] = name;
  }
}

void Data::loadCustomVariablesText(const std::string& text) {
  for (const auto& line : splitLines(text)) {
    std::string l = trim(line);
    if (l.empty() || l[0] == ';') continue;
    size_t eq = l.find('=');
    if (eq == std::string::npos) continue;
    std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
    if (k.empty() || v.empty() || !std::isdigit((unsigned char)k[0])) continue;
    int idx = atoi(k.c_str());
    globalNames[idx] = v;
    globalByName[upper(v)] = idx;
  }
}

void Data::loadConstantsText(const std::string& text) {
  for (const auto& line : splitLines(text)) {
    std::string l = trim(line);
    size_t eq = l.find('=');
    if (l.empty() || l[0] == ';' || eq == std::string::npos) continue;
    std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
    if (!v.empty() && v.back() == '@') {
      int idx = atoi(v.c_str());
      localNames[idx] = k;
      localByName[upper(k)] = idx;
    } else if (!v.empty() && (std::isdigit((unsigned char)v[0]) || v[0] == '-')) {
      constants[upper(k)] = atoll(v.c_str());
    }
  }
}


namespace {
std::string xmlAttr(const std::string& tag, const std::string& name) {
  std::string key = name + "=\"";
  size_t p = 0;
  while ((p = tag.find(key, p)) != std::string::npos) {
    if (p == 0 || tag[p - 1] == ' ' || tag[p - 1] == '\t' || tag[p - 1] == '\n') {
      size_t e = tag.find('"', p + key.size());
      return e == std::string::npos ? "" : tag.substr(p + key.size(), e - p - key.size());
    }
    p += key.size();
  }
  return "";
}
std::string modeTag(const std::string& xml) {  // the opening <mode ...> tag (skips comments and <?xml?>)
  size_t p = 0;
  while ((p = xml.find("<mode", p)) != std::string::npos) {
    char n = p + 5 < xml.size() ? xml[p + 5] : 0;
    if (n == ' ' || n == '\t' || n == '\r' || n == '\n') { size_t e = xml.find('>', p); return e == std::string::npos ? "" : xml.substr(p, e - p); }
    p += 5;
  }
  return "";
}
}  // namespace

std::vector<ModeInfo> listModes(const std::string& dataDir) {
  namespace fs = std::filesystem;
  std::vector<ModeInfo> r;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(dataDir, ec)) {
    if (!e.is_directory()) continue;
    std::string xml;
    std::string mp;
    for (auto& f : fs::directory_iterator(e.path(), ec))
      if (lower(f.path().filename().string()) == "mode.xml") mp = f.path().string();
    if (mp.empty() || !readFile(mp, &xml)) continue;
    std::string tag = modeTag(xml);
    ModeInfo m;
    m.id = xmlAttr(tag, "id");
    if (m.id.empty()) continue;
    m.title = xmlAttr(tag, "title");
    m.game = xmlAttr(tag, "game");
    m.extends = xmlAttr(tag, "extends");
    m.type = xmlAttr(tag, "type");
    m.sbl = xml.find("<library") != std::string::npos && xml.find("<opcodes>") == std::string::npos;
    r.push_back(m);
  }
  for (int pass = 0; pass < 3; pass++)  // a child without its own opcodes inherits "sbl" from its parent
    for (auto& m : r)
      if (!m.extends.empty())
        for (auto& pm : r) if (pm.id == m.extends && pm.sbl) m.sbl = true;
  std::sort(r.begin(), r.end(), [](const ModeInfo& a, const ModeInfo& b) { return a.title < b.title; });
  return r;
}

bool Data::loadSaMobile(const std::string& dataDir, std::string* err) { return loadMode(dataDir, "sa_mobile", err); }

bool Data::loadMode(const std::string& dataDir, const std::string& modeId, std::string* err) {
  namespace fs = std::filesystem;
  auto findCI = [&](const fs::path& dir, const std::string& name) -> std::string {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec))
      if (lower(e.path().filename().string()) == lower(name)) return e.path().string();
    return "";
  };
  std::string modeDir = findCI(dataDir, modeId);
  std::string xml;
  if (modeDir.empty() || !readFile(findCI(modeDir, "mode.xml"), &xml)) {
    if (err) *err = "Invalid edit mode";
    return false;
  }
  // tiny matcher for  <tag ...>@sb:\data\x\y.ini</tag>
  std::string tag0 = modeTag(xml);
  std::string parent = xmlAttr(tag0, "extends");
  if (!parent.empty() && parent != modeId) {  // inherit everything from the parent mode first, then add / override
    if (!loadMode(dataDir, parent, err)) return false;
  }
  if (xml.find("<opcodes>") == std::string::npos && ops.empty() && xml.find("<library") != std::string::npos) {
    if (err) *err = "SBL mode (new language, JSON opcode library) is not supported yet; use the legacy mode of the same game";
    return false;
  }
  this->modeId = modeId;
  this->title = xmlAttr(tag0, "title");
  this->game = xmlAttr(tag0, "game");
  {
    static const char* names[] = {"gta3", "vc", "sa", "lcs", "vcs", "vc_mobile", "sa_mobile"};
    for (int gi = 0; gi < 7; gi++) if (lower(this->game) == names[gi]) gameEnum = gi;
    ifOp = gameEnum == 3 ? 0x00DB : gameEnum == 4 ? 0x0078 : 0x00D6;
    jfOp = gameEnum == 4 ? 0x0022 : 0x004D;
    gosubOp = gameEnum == 4 ? 0x0025 : 0x0050;
    startMissionOp = gameEnum == 3 ? 0x041C : gameEnum == 4 ? 0x0289 : 0x0417;
  }
  auto tagValues = [&](const std::string& tag) {
    std::vector<std::string> r;
    std::string open = "<" + tag, close = "</" + tag + ">";
    size_t p = 0;
    while ((p = xml.find(open, p)) != std::string::npos) {
      size_t gt = xml.find('>', p);
      size_t cl = xml.find(close, gt);
      if (gt == std::string::npos || cl == std::string::npos) break;
      r.push_back(trim(xml.substr(gt + 1, cl - gt - 1)));
      p = cl;
    }
    return r;
  };
  auto resolve = [&](std::string v) {  // @sb:\data\sa\x.ini -> <dataDir>/sa/x.ini
    for (auto& ch : v) if (ch == '\\') ch = '/';
    const std::string pre = "@sb:/data/";
    if (v.rfind(pre, 0) == 0) v = v.substr(pre.size());
    return (fs::path(dataDir) / v).string();
  };
  auto globMatch = [](const std::string& pat, const std::string& name) {
    std::string p = lower(pat), n = lower(name);
    size_t star = p.find('*');
    if (star == std::string::npos) return p == n;
    return n.size() >= p.size() - 1 && n.compare(0, star, p, 0, star) == 0 &&
           n.compare(n.size() - (p.size() - star - 1), std::string::npos, p, star + 1, std::string::npos) == 0;
  };
  std::set<std::string> loaded;
  for (auto& v : tagValues("opcodes")) {
    fs::path full = resolve(v);
    if (full.filename().string().find('*') == std::string::npos) {
      std::string f = findCI(full.parent_path(), full.filename().string());
      if (!f.empty() && loaded.insert(lower(f)).second) { std::string e; loadOpcodeIniFile(f, &e); }
    } else {
      std::vector<std::string> names;
      std::error_code ec;
      for (auto& e : fs::directory_iterator(full.parent_path(), ec))
        if (globMatch(full.filename().string(), e.path().filename().string())) names.push_back(e.path().string());
      std::sort(names.begin(), names.end());
      for (auto& f : names) if (loaded.insert(lower(f)).second) { std::string e; loadOpcodeIniFile(f, &e); }
    }
  }
  if (ops.empty()) {
    if (err) *err = "Opcodes list not found";
    return false;
  }
  std::string t;
  if (readFile((fs::path(dataDir) / "optypes.txt").string(), &t)) loadOpTypesText(t);
  for (auto& v : tagValues("ide")) {  // custom.dat: "IDE name.ide" lines, paths relative to the .dat file
    fs::path dat = resolve(v);
    std::string datPath = findCI(dat.parent_path(), dat.filename().string()), dt;
    if (datPath.empty() || !readFile(datPath, &dt)) continue;
    for (const auto& line : splitLines(dt)) {
      std::string l = trim(line);
      if (l.size() > 4 && upper(l.substr(0, 3)) == "IDE" && l[3] == ' ') {
        std::string rel = trim(l.substr(4));
        for (auto& ch : rel) if (ch == '\\') ch = '/';
        fs::path ip = fs::path(datPath).parent_path() / rel;
        std::string f = findCI(ip.parent_path(), ip.filename().string()), it;
        if (!f.empty() && readFile(f, &it)) loadIdeText(it);
      }
    }
  }
  for (auto& v : tagValues("enums")) { fs::path f = resolve(v); if (readFile(findCI(f.parent_path(), f.filename().string()), &t)) loadEnumsText(t); }
  for (auto& v : tagValues("classes")) { fs::path f = resolve(v); if (readFile(findCI(f.parent_path(), f.filename().string()), &t)) loadClassesText(t); }
  if (!classMembers.empty()) classesFrom = modeId;  // the mode's own classes.db (Sanny ships an empty one for sa_mobile: see tools/gen_mobile_classes.py)
  for (auto& v : tagValues("keywords")) { fs::path f = resolve(v); if (readFile(findCI(f.parent_path(), f.filename().string()), &t)) loadKeywordsText(t); }
  for (auto& v : tagValues("variables")) { fs::path f = resolve(v); if (readFile(findCI(f.parent_path(), f.filename().string()), &t)) loadCustomVariablesText(t); }
  for (auto& v : tagValues("constants")) { fs::path f = resolve(v); if (readFile(findCI(f.parent_path(), f.filename().string()), &t)) loadConstantsText(t); }
  return true;
}

// ================================================================= decompiler
namespace {


// shortest keyword wins, ties -> the later one listed (matches names seen in Sanny output: jf, jump, thread, end_thread)
std::string keywordFor(const Data& db, uint16_t op) {
  std::string best;
  for (const auto& kv : db.keywords) {
    if (kv.second != op) continue;
    if (best.empty() || kv.first.size() <= best.size()) best = kv.first;
  }
  return best;
}

bool isOperatorFmt(const std::string& fmtBody, const std::string& comment) {
  if (!trim(comment).empty()) return false;
  std::string b = trim(fmtBody);
  static const char* ops[] = {"=", "+=", "-=", "*=", "/=", "==", ">", ">=", "<", "<="};
  for (const char* o : ops)
    if (b == std::string("%1d% ") + o + " %2d%") return true;
  return false;
}


// label operand text: relative blocks print positive operands (targets in other blocks) with the external prefix
std::string labelRef(const Options& opt, long long v) {
  if (opt.negativeLabels && v >= 0 && !opt.externalPrefix.empty()) return "@" + opt.externalPrefix + "_" + std::to_string(v);
  return "@" + opt.labelPrefix + "_" + std::to_string(labelTarget(v));
}
bool isExternalLabel(const Options& opt, long long v) { return opt.negativeLabels && v >= 0 && !opt.externalPrefix.empty(); }

std::string renderInsn(const Insn& in, const OpDef& def, const Data& db, const Options& opt) {
  std::string body, comment;
  splitComment(def.fmt, &body, &comment);
  if (opt.scriptStyle) {
    auto argText = [&](const Arg& a) {
      if (isLabelArgType(a.type) && def.fmt.find("%p%") == std::string::npos) {}
      return printArg(a, db);
    };
    if (in.op == db.ifOp && !in.neg && in.args.size() == 1 && isLabelArgType(in.args[0].type)) {
      long long v = in.args[0].i;
      return v == 0 ? "if " : (v >= 1 && v <= 7) ? "if and" : (v >= 21 && v <= 27) ? "if or" : "if " + std::to_string(v);
    }
    std::string kw = in.neg ? "" : keywordFor(db, in.op);
    if (!kw.empty()) {
      std::vector<Piece> kp = parsePattern(body);
      std::set<int> lbl;
      for (auto& pc : kp) if (pc.ph && pc.spec == 'p') lbl.insert(pc.idx);
      std::string line = kw;
      for (size_t k = 0; k < in.args.size(); k++) {
        const Arg& a = in.args[k];
        if (lbl.count((int)k + 1) && isLabelArgType(a.type)) line += " " + labelRef(opt, a.i);
        else line += " " + argText(a);
      }
      return line + " ";
    }
    if (isOperatorFmt(body, comment)) {
      std::vector<Piece> pc = parsePattern(body);
      std::string t = ltrim(body);
      std::string a1 = in.args.size() > 0 ? printArg(in.args[0], db) : "", a2 = in.args.size() > 1 ? printArg(in.args[1], db) : "";
      size_t lead = body.size() - ltrim(body).size();
      std::string mid = trim(body);
      mid = mid.substr(5, mid.size() - 5 - 5);  // between "%1d% " and " %2d%"
      std::string txt = a1 + " " + trim(mid) + " " + a2;
      if (in.neg) return "   not " + txt + " ";
      return std::string(lead, ' ') + txt + " ";
    }
  }
  std::vector<Piece> pieces = parsePattern(body);
  std::set<int> used;
  std::string text;
  bool phFirst = ltrim(body).rfind("%", 0) == 0;
  bool firstLit = true;
  for (const auto& p : pieces) {
    if (!p.ph) {
      // runs of spaces inside the format collapse to one; the leading indent of the line is kept
      std::string l = p.lit, r;
      size_t k = 0;
      if (firstLit) { while (k < l.size() && l[k] == ' ') r += l[k++]; }
      for (bool prevSp = false; k < l.size(); k++) {
        if (l[k] == ' ') { if (!prevSp) r += ' '; prevSp = true; } else { r += l[k]; prevSp = false; }
      }
      firstLit = false;
      text += r;
      continue;
    }
    firstLit = false;
    used.insert(p.idx);
    if (p.idx < 1 || p.idx > (int)in.args.size()) {
      text += "%" + std::to_string(p.idx) + p.spec + "%";
      continue;
    }
    const Arg& a = in.args[p.idx - 1];
    if (p.spec == 'p' && isLabelArgType(a.type)) {
      text += labelRef(opt, a.i);
    } else if ((p.spec == 'o' || p.spec == 'm') && isLabelArgType(a.type) && db.modelNames.count((int)a.i)) {
      text += "#" + upper(db.modelNames.at((int)a.i));
    } else {
      text += printArg(a, db);
    }
  }
  // variadic: arguments that have no placeholder follow the text
  if (def.nparams < 0) {
    for (size_t k = 0; k < in.args.size(); k++)
      if (!used.count((int)k + 1)) text += " " + printArg(in.args[k], db);
  }
  if (in.op == db.ifOp && in.args.size() == 1 && isLabelArgType(in.args[0].type) && !in.neg) {
    long long v = in.args[0].i;  // Sanny prints "if", "if and", "if or"; the count is recomputed on compile
    text = v == 0 ? "if" : (v >= 1 && v <= 7) ? "if and" : (v >= 21 && v <= 27) ? "if or" : "if " + std::to_string(v);
  }
  std::string line;
  if (in.neg) {
    // empirical rule from data/sa_mobile/opcodes.txt: "8018:   not  $2899 > 0" vs "80DF:   not actor ..."
    line = hex4(in.op | 0x8000) + ": " + "  not " + (phFirst ? " " : "") + ltrim(text);
  } else {
    line = hex4(in.op) + ": " + text;
  }
  std::string c = trim(comment);
  if (!c.empty()) {
    line = rtrim(line) + " // " + c;
  }
  if (opt.trailingSpace) line = rtrim(line) + " ";
  return line;
}

}  // namespace

DecompileResult decompile(const uint8_t* d, size_t n, const Data& db, const Options& opt) {
  DecompileResult res;
  if (!db.formatVerified() && !opt.allowUnverified) {
    res.diags.push_back(mk(9100, "Binary encoding of game '" + db.game + "' is not verified yet (SA / SA Mobile only). Set allowUnverified to try the SA encoding anyway."));
    return res;
  }
  std::vector<Insn> code;
  size_t p = 0;
  bool dataTail = false;
  // Non-code data in the middle of a script (string tables, arrays skipped by a jump) is written as "hex .. end" blocks.
  // Decoding resumes at the next jump / gosub target that is already known, so the code after the data stays readable.
  std::vector<std::pair<size_t, size_t>> dataSegs;
  std::set<long long> jumpTargets;
  auto resync = [&](size_t start) -> bool {
    size_t nt = n;
    for (long long t : jumpTargets) {
      long long rel = t - (long long)opt.baseOffset;
      if (rel > (long long)start && rel < (long long)nt) nt = (size_t)rel;
    }
    dataSegs.push_back({start, nt});
    dataTail = true;
    p = nt;
    return nt < n;
  };
  while (p < n) {
    size_t start = p;
    if (p + 2 > n) { resync(start); break; }
    uint16_t raw = (uint16_t)(d[p] | (d[p + 1] << 8));
    Insn in;
    in.off = (uint32_t)start;
    in.neg = (raw & 0x8000) != 0;
    in.op = raw & 0x7FFF;
    const OpDef* def = db.op(in.op);
    if (!def) {
      res.diags.push_back(mk(11, fmt("Unknown opcode %s at offset %d (written as hex data)", hex4(raw).c_str(), (int)start), (int)start));
      if (resync(start)) continue;
      break;
    }
    size_t q = p + 2;
    bool ok = true;
    if (def->nparams >= 0) {
      for (int i = 0; i < def->nparams && ok; i++) {
        Arg a;
        if (i < 32 && ((def->kmask >> i) & 1)) {  // %k%: 128 raw bytes, text up to the first NUL
          if (q + 128 > n) { ok = false; break; }
          a.type = T_RAW128;
          for (size_t z = 0; z < 128 && d[q + z]; z++) a.s += (char)d[q + z];
          q += 128;
          in.args.push_back(a);
          continue;
        }
        if (db.oldFormat() && q < n && (d[q] < 1 || d[q] > 6)) {  // old format: a byte that is not a type (1..6) starts an 8-byte raw text parameter
          if (q + 8 > n) { ok = false; break; }
          a.type = T_RAW8;
          for (size_t z = 0; z < 8 && d[q + z]; z++) a.s += (char)d[q + z];
          q += 8;
          in.args.push_back(a);
          continue;
        }
        if (!readArg(d, n, q, a, db.oldFormat())) {
          res.diags.push_back(mk(4, fmt("Unknown parameter type %02X at offset %d", q - 1 < n ? d[q - 1] : 0, (int)q - 1), (int)q));
          ok = false;
        } else in.args.push_back(a);
      }
    } else {
      while (ok) {
        if (q >= n) { ok = false; break; }
        if (d[q] == T_END) { q++; break; }
        Arg a;
        if (!readArg(d, n, q, a, db.oldFormat())) { res.diags.push_back(mk(4, fmt("Unknown parameter type %02X at offset %d", d[q], (int)q), (int)q)); ok = false; }
        else in.args.push_back(a);
      }
    }
    if (!ok) { if (resync(start)) continue; break; }
    code.push_back(in);
    p = q;
    {  // remember where jumps / gosubs / thread starts point to: code resumes there after a data block
      std::string body, cm;
      splitComment(def->fmt, &body, &cm);
      std::string lb = lower(body);
      bool jumpish = in.op == 0x0002 || in.op == 0x004C || in.op == 0x004D || in.op == 0x0050 || in.op == (uint16_t)db.jfOp ||
                     in.op == (uint16_t)db.gosubOp || lb.find("jump") != std::string::npos || lb.find("goto") != std::string::npos ||
                     lb.find("gosub") != std::string::npos;
      if (jumpish)
        for (auto& pc : parsePattern(body))
          if (pc.ph && pc.spec == 'p' && pc.idx >= 1 && pc.idx <= (int)in.args.size() && isLabelArgType(in.args[pc.idx - 1].type) &&
              !isExternalLabel(opt, in.args[pc.idx - 1].i))
            jumpTargets.insert(labelTarget(in.args[pc.idx - 1].i));
    }
  }

  // collect label targets
  std::set<uint32_t> insnOffsets;
  for (auto& in : code) insnOffsets.insert(in.off);
  std::set<long long> labels;
  for (auto& in : code) {
    const OpDef* def = db.op(in.op);
    std::string body, cm;
    splitComment(def->fmt, &body, &cm);
    for (auto& pc : parsePattern(body))
      if (pc.ph && pc.spec == 'p' && pc.idx >= 1 && pc.idx <= (int)in.args.size() && isLabelArgType(in.args[pc.idx - 1].type))
        { if (!isExternalLabel(opt, in.args[pc.idx - 1].i)) labels.insert(labelTarget(in.args[pc.idx - 1].i)); }
  }
  for (long long t : opt.extraLabelTargets) labels.insert(t);
  auto inData = [&](long long rel) { for (auto& sg : dataSegs) if (rel >= (long long)sg.first && rel <= (long long)sg.second) return true; return false; };
  for (long long t : labels)
    if (!insnOffsets.count((uint32_t)(t - (long long)opt.baseOffset)) && !inData(t - (long long)opt.baseOffset))
      res.diags.push_back(mk(12, fmt("Incorrect jump instruction %lld found at %lld", t, t), (int)t));

  Options o2 = opt;
  if (opt.scriptStyle && opt.autoLabelPrefix)
    for (auto& in : code)
      if (in.op == 0x03A4 && !in.args.empty() && !in.args[0].s.empty()) { o2.labelPrefix = upper(in.args[0].s); break; }
  const Options& opt2 = o2;
  std::string out;
  if (!opt.cleoExt.empty()) out += opt.cleoExt[0] == '{' ? opt.cleoExt + "\n\n" : "{$CLEO " + opt.cleoExt + "}\n\n";
  bool first = true;
  auto emitData = [&](size_t seg0, size_t seg1) {
    // data is split at every label that points into it, so jumps / get_label_addr into the middle of the data keep their targets
    std::vector<size_t> cuts;
    cuts.push_back(seg0);
    for (long long t : labels) {
      long long rel = t - (long long)opt.baseOffset;
      if (rel > (long long)seg0 && rel < (long long)seg1) cuts.push_back((size_t)rel);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    cuts.push_back(seg1);
    for (size_t ci = 0; ci + 1 < cuts.size(); ci++) {
      size_t a0 = cuts[ci], b0 = cuts[ci + 1];
      if (labels.count((long long)a0 + opt.baseOffset)) out += "\n:" + opt2.labelPrefix + "_" + std::to_string((long long)a0 + opt.baseOffset) + "\n";
      else if (!first) out += "\n";
      out += "hex\n";
      for (size_t i = a0; i < b0; i += 16) {
        std::string row;
        for (size_t j = i; j < std::min(b0, i + 16); j++) {
          char bb[8];
          snprintf(bb, sizeof bb, "%02X ", d[j]);
          row += bb;
        }
        out += row + "\n";
      }
      out += "end\n";
      first = false;
    }
  };
  size_t si = 0;
  for (auto& in : code) {
    while (si < dataSegs.size() && dataSegs[si].first < in.off) { emitData(dataSegs[si].first, dataSegs[si].second); si++; }
    if (labels.count((long long)in.off + opt.baseOffset)) {
      if (!first) out += "\n";
      out += ":" + opt2.labelPrefix + "_" + std::to_string((long long)in.off + opt.baseOffset) + "\n";
    }
    out += renderInsn(in, *db.op(in.op), db, opt2) + "\n";
    first = false;
  }
  for (; si < dataSegs.size(); si++) emitData(dataSegs[si].first, dataSegs[si].second);
  res.text = out;
  res.ok = res.diags.empty() || !dataTail;
  return res;
}

// ================================================================= compiler
namespace {

struct Fixup {
  size_t pos;
  std::string name;
  int line;
};

struct Ctx {
  const Data& db;
  const Options& opt;
  std::vector<uint8_t> out;
  std::map<std::string, uint32_t> labels;
  std::vector<Fixup> fixups;
  std::vector<Diag> diags;
  bool cleo = false;
  size_t ifPos = std::string::npos;  // pending "if and/or": position of the condition-count byte
  char ifKind = 0;
  int ifCount = 0;
  ScriptDefs defs;
  int maxGlobal = -1;
  std::map<std::string, int> allocNames;  // Alloc($NAME, N)
};

void put16(std::vector<uint8_t>& o, unsigned v) { o.push_back(v & 0xFF); o.push_back((v >> 8) & 0xFF); }
void put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((v >> (8 * i)) & 0xFF); }

bool isIntText(const std::string& t, long long* v) {
  if (t.empty()) return false;
  const char* s = t.c_str();
  char* e = nullptr;
  bool neg = *s == '-';
  const char* digits = neg ? s + 1 : s;
  if (!*digits) return false;
  if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
    unsigned long long u = strtoull(digits + 2, &e, 16);
    if (e == digits + 2 || *e) return false;
    *v = neg ? -(long long)u : (long long)u;
    if (!neg && u > 0x7FFFFFFFull && u <= 0xFFFFFFFFull) *v = (int32_t)(uint32_t)u;
    return true;
  }
  for (const char* c = digits; *c; c++) if (!std::isdigit((unsigned char)*c)) return false;
  *v = strtoll(s, &e, 10);
  return true;
}

bool isFloatText(const std::string& t) {
  if (t.empty()) return false;
  const char* s = t.c_str();
  if (*s == '-' || *s == '+') s++;
  bool dot = false, dig = false;
  for (; *s; s++) {
    if (*s == '.') { if (dot) return false; dot = true; }
    else if (std::isdigit((unsigned char)*s)) dig = true;
    else if (*s == 'e' || *s == 'E') { return dig && (dot || true); }
    else return false;
  }
  return dig && dot;
}

// "$12" "$NAME" "s$5" "v$5" -> kind: 'g' + string kind, returns false when t is not a global var
bool parseGlobal(const Ctx& c, const std::string& t, int* idx, char* skind) {
  size_t i = 0;
  *skind = 'i';
  if (t.size() > 1 && (t[0] == 's' || t[0] == 'v') && t[1] == '$') { *skind = t[0]; i = 1; }
  if (i >= t.size() || t[i] != '$') return false;
  std::string n = t.substr(i + 1);
  if (n.empty()) return false;
  size_t lb = n.find('[');
  if (lb != std::string::npos && n.back() == ']') {  // $base[k] -> variable base+k
    std::string bs = n.substr(0, lb), ks = n.substr(lb + 1, n.size() - lb - 2);
    int bidx;
    if (std::isdigit((unsigned char)bs[0])) bidx = atoi(bs.c_str());
    else { auto it = c.db.globalByName.find(upper(bs)); if (it == c.db.globalByName.end()) return false; bidx = it->second; }
    for (char ch : ks) if (!std::isdigit((unsigned char)ch)) return false;
    *idx = bidx + atoi(ks.c_str());
    return true;
  }
  if (std::isdigit((unsigned char)n[0])) {
    for (char ch : n) if (!std::isdigit((unsigned char)ch)) return false;
    *idx = atoi(n.c_str());
    return true;
  }
  auto it = c.db.globalByName.find(upper(n));
  if (it == c.db.globalByName.end()) {
    auto al = c.allocNames.find(upper(n));
    if (al == c.allocNames.end()) return false;
    *idx = al->second;
    return true;
  }
  *idx = it->second;
  return true;
}

// "5@" "5@s" "5@v" "TIMERA"
bool parseLocal(const Ctx& c, const std::string& t, int* idx, char* skind) {
  *skind = 'i';
  auto it = c.db.localByName.find(upper(t));
  if (it != c.db.localByName.end()) { *idx = it->second; return true; }
  size_t i = 0;
  while (i < t.size() && std::isdigit((unsigned char)t[i])) i++;
  if (i == 0 || i >= t.size() || t[i] != '@') return false;
  *idx = atoi(t.substr(0, i).c_str());
  std::string rest = t.substr(i + 1);
  if (rest.empty()) return true;
  if (rest == "s" || rest == "v") { *skind = rest[0]; return true; }
  return false;
}

bool encodeArg(Ctx& c, const std::string& raw, int line, std::vector<uint8_t>& o) {
  std::string t = trim(raw);
  auto err = [&](int code, const std::string& m) { c.diags.push_back(mk(code, m, -1, line)); return false; };
  if (t.empty()) return err(58, "Invalid parameter ");
  int idx; char sk;
  // strings
  if (c.db.oldFormat() && (t[0] == '\'' || t[0] == '"')) {  // GTA III / VC: text parameters are 8 raw bytes, no type byte
    if (t.size() < 2 || t.back() != t[0]) return err(28, "Unterminated string " + t);
    std::string s = t.substr(1, t.size() - 2);
    if (s.size() > 7) return err(27, fmt("String %s is too long. \nMax %d symbols", t.c_str(), 7));
    for (size_t i = 0; i < 8; i++) o.push_back(i < s.size() ? (uint8_t)s[i] : 0);
    return true;
  }
  if (t[0] == '\'') {  // text parameter. sanny.exe 0x56b000: <=7 chars -> type 09 (8 bytes); 8..15 chars -> type 0F (16 bytes) only for sa / vc_mobile
    if (t.size() < 2 || t.back() != '\'') return err(28, "Unterminated string " + t);
    std::string s = t.substr(1, t.size() - 2);
    bool longOk = c.db.gameEnum == 2 || c.db.gameEnum == 5;
    if (c.opt.strictStringLimit) {
      size_t maxLen = longOk ? 15 : 7;
      if (s.size() > maxLen) return err(27, fmt("String %s is too long. \nMax %d symbols", t.c_str(), (int)maxLen));
    } else if (s.size() > 255) return err(27, fmt("String %s is too long. \nMax %d symbols", t.c_str(), 255));
    if (s.size() > 15) {  // lenient mode only: a length-prefixed string (type 0E)
      o.push_back(T_VSTR); o.push_back((uint8_t)s.size()); o.insert(o.end(), s.begin(), s.end());
      return true;
    }
    size_t w = s.size() > 7 ? 16 : 8;
    o.push_back(s.size() > 7 ? T_STR16 : T_STR8);
    for (size_t i = 0; i < w; i++) o.push_back(i < s.size() ? (uint8_t)s[i] : 0);
    return true;
  }
  if (t[0] == '"') {
    if (t.size() < 2 || t.back() != '"') return err(28, "Unterminated string " + t);
    std::string s = t.substr(1, t.size() - 2);
    if (s.size() > 255) return err(27, fmt("String %s is too long. \nMax %d symbols", t.c_str(), 255));
    o.push_back(T_VSTR);
    o.push_back((uint8_t)s.size());
    o.insert(o.end(), s.begin(), s.end());
    return true;
  }
  if (t[0] == '#') {  // model name
    std::string mn = upper(t.substr(1));
    auto mi = c.db.models.find(mn);
    if (mi != c.db.models.end()) return encodeArg(c, std::to_string(mi->second), line, o);
    for (size_t oi = c.defs.objects.size(); oi-- > 1;)  // DEFINE OBJECT n: model id is -n (index 0 is the unnamed slot)
      if (upper(c.defs.objects[oi]) == mn) return encodeArg(c, std::to_string(-(long long)oi), line, o);
    if (c.opt.unknownModelsAsZero) return encodeArg(c, "0", line, o);
    return err(83, "Unknown model #" + t.substr(1) + ".\nCheck if the game directory is selected in the options and the model name is correct.");
  }
  // label reference
  if (t[0] == '@') {
    o.push_back(T_I32);
    c.fixups.push_back({o.size(), upper(t.substr(1)), line});
    put32(o, 0);
    return true;
  }
  // array: base(index,Nt)
  size_t lp = t.find('(');
  if (lp != std::string::npos && t.back() == ')') {
    std::string base = t.substr(0, lp), inner = t.substr(lp + 1, t.size() - lp - 2);
    size_t cm = inner.find(',');
    if (cm == std::string::npos) return err(45, "Invalid array syntax: a variable name is missing");
    std::string iv = trim(inner.substr(0, cm)), sz = trim(inner.substr(cm + 1));
    if (sz.size() < 2) return err(48, "Error reading the size of the array " + base);
    char tl = (char)std::tolower((unsigned char)sz.back());
    if (tl != 'i' && tl != 'f' && tl != 's' && tl != 'v') return err(38, "Unknown type of array " + sz + ". Use i, f, s or v");
    int size = atoi(sz.substr(0, sz.size() - 1).c_str());
    if (size < 1) return err(47, "Invalid array size.\nSize can not be less than one");
    int bi, ii; char k1, k2;
    bool bg = parseGlobal(c, base, &bi, &k1), bl = !bg && parseLocal(c, base, &bi, &k1);
    if (!bg && !bl) return err(44, "Invalid array name " + base + ".\nUse a global or a local variable");
    bool ig = parseGlobal(c, iv, &ii, &k2), il = !ig && parseLocal(c, iv, &ii, &k2);
    if (!ig && !il) return err(43, "Invalid index " + iv + " of the array " + base + ".\nUse a global or a local variable");
    int et = tl == 'i' ? 0 : tl == 'f' ? 1 : tl == 's' ? 2 : 3;
    uint8_t type = bg ? (et == 2 ? T_GSTR8ARR : et == 3 ? T_GSTR16ARR : T_GARR)
                      : (et == 2 ? T_LSTR8ARR : et == 3 ? T_LSTR16ARR : T_LARR);
    if (bg) c.maxGlobal = std::max(c.maxGlobal, bi + size * (et == 2 ? 2 : et == 3 ? 4 : 1) - 1);
    if (ig) c.maxGlobal = std::max(c.maxGlobal, ii);
    o.push_back(type);
    put16(o, bg ? bi * 4 : bi);
    put16(o, ig ? ii * 4 : ii);
    o.push_back((uint8_t)size);
    o.push_back((uint8_t)(et | (il ? 0 : 0x80)));  // bit7 set = index var is global
    return true;
  }
  if (parseGlobal(c, t, &idx, &sk)) {
    c.maxGlobal = std::max(c.maxGlobal, idx + (sk == 's' ? 1 : sk == 'v' ? 3 : 0));
    o.push_back(sk == 's' ? T_GSTR8 : sk == 'v' ? T_GSTR16 : T_GVAR);
    put16(o, idx * 4);
    return true;
  }
  if (parseLocal(c, t, &idx, &sk)) {
    o.push_back(sk == 's' ? T_LSTR8 : sk == 'v' ? T_LSTR16 : T_LVAR);
    put16(o, idx);
    return true;
  }
  long long v;
  if (isIntText(t, &v)) {
    if (v < INT32_MIN || v > INT32_MAX) return err(24, "Error reading integer number: " + t + " found");
    if (v >= -128 && v <= 127) { o.push_back(T_I8); o.push_back((uint8_t)(int8_t)v); }
    else if (v >= -32768 && v <= 32767) { o.push_back(T_I16); put16(o, (uint16_t)(int16_t)v); }
    else { o.push_back(T_I32); put32(o, (uint32_t)(int32_t)v); }
    return true;
  }
  if (isFloatText(t)) {
    float f = strtof(t.c_str(), nullptr);
    uint32_t u; memcpy(&u, &f, 4);
    if (c.db.oldFormat()) {  // fixed point int16 = value * 16
      double fx = std::floor((double)f * 16.0 + 0.5);
      if (fx < -32768.0 || fx > 32767.0) return err(24, "Error reading float number: " + t + " found");
      o.push_back(T_F32); put16(o, (uint16_t)(int16_t)fx);
      return true;
    }
    o.push_back(T_F32); put32(o, u);
    return true;
  }
  {
    size_t dot = t.find('.');
    if (dot != std::string::npos) {  // Enum.Member
      auto e = c.db.enums.find(lower(t.substr(0, dot)));
      if (e != c.db.enums.end()) { auto m = e->second.find(lower(t.substr(dot + 1))); if (m != e->second.end()) return encodeArg(c, std::to_string(m->second), line, o); }
    }
  }
  if (upper(t) == "TRUE" || upper(t) == "FALSE") return encodeArg(c, upper(t) == "TRUE" ? "1" : "0", line, o);
  auto ci = c.db.constants.find(upper(t));
  if (ci != c.db.constants.end()) {
    long long cv = ci->second;
    std::string tt = std::to_string(cv);
    return encodeArg(c, tt, line, o);
  }
  { auto em = c.db.enumMembers.find(lower(t)); if (em != c.db.enumMembers.end()) return encodeArg(c, std::to_string(em->second), line, o); }
  return err(58, "Invalid parameter " + t);
}

// whitespace-collapse outside quotes
std::string normalize(const std::string& s) {
  std::string r;
  bool inq = false; char q = 0, prev = 0;
  for (char ch : s) {
    if (inq) { r += ch; if (ch == q) inq = false; prev = ch; continue; }
    if (ch == '\'' || ch == '"') { inq = true; q = ch; r += ch; prev = ch; continue; }
    if (ch == ' ' || ch == '\t') { if (prev != ' ') r += ' '; prev = ' '; continue; }
    r += ch; prev = ch;
  }
  return trim(r);
}

// find literal outside quotes
size_t findOutside(const std::string& s, const std::string& lit, size_t from) {
  bool inq = false; char q = 0;
  for (size_t i = 0; i + lit.size() <= s.size(); i++) {
    char ch = s[i];
    if (inq) { if (ch == q) inq = false; continue; }
    if (ch == '\'' || ch == '"') { inq = true; q = ch; continue; }
    if (i >= from && s.compare(i, lit.size(), lit) == 0) return i;
  }
  return std::string::npos;
}

std::vector<std::string> splitTokens(const std::string& s) {
  std::vector<std::string> r; std::string cur; bool inq = false; char q = 0;
  for (char ch : s) {
    if (inq) { cur += ch; if (ch == q) inq = false; continue; }
    if (ch == '\'' || ch == '"') { inq = true; q = ch; cur += ch; continue; }
    if (ch == ' ') { if (!cur.empty()) r.push_back(cur); cur.clear(); continue; }
    cur += ch;
  }
  if (!cur.empty()) r.push_back(cur);
  return r;
}

bool matchPattern(const std::vector<Piece>& pcs, const std::string& text, std::map<int, std::string>* caps,
                  std::string* extra) {
  size_t pos = 0;
  for (size_t k = 0; k < pcs.size(); k++) {
    const Piece& p = pcs[k];
    if (!p.ph) {
      std::string lit = p.lit;
      if (k == 0) lit = ltrim(lit);
      if (k + 1 == pcs.size()) lit = rtrim(lit);
      if (lit.empty()) continue;
      if (text.compare(pos, lit.size(), lit) != 0) return false;
      pos += lit.size();
      continue;
    }
    std::string nextLit;
    for (size_t j = k + 1; j < pcs.size(); j++) if (!pcs[j].ph) { nextLit = pcs[j].lit; break; } else break;
    if (k + 1 < pcs.size() && !pcs[k + 1].ph) {
      nextLit = pcs[k + 1].lit;
      if (k + 2 == pcs.size()) nextLit = rtrim(nextLit);
      size_t f = nextLit.empty() ? text.size() : findOutside(text, nextLit, pos);
      if (f == std::string::npos) return false;
      (*caps)[p.idx] = trim(text.substr(pos, f - pos));
      pos = f;
    } else {
      // last placeholder (or adjacent placeholders): take remainder, split on first token
      std::string rest = trim(text.substr(pos));
      std::vector<std::string> tk = splitTokens(rest);
      if (tk.empty()) return false;
      (*caps)[p.idx] = tk[0];
      std::string ex;
      for (size_t i = 1; i < tk.size(); i++) ex += (i > 1 ? " " : "") + tk[i];
      *extra = ex;
      pos = text.size();
    }
  }
  return pos >= text.size() || trim(text.substr(pos)).empty();
}

std::string stripLineComment(const std::string& s) {
  bool inq = false; char q = 0;
  for (size_t i = 0; i < s.size(); i++) {
    char ch = s[i];
    if (inq) { if (ch == q) inq = false; continue; }
    if (ch == '\'' || ch == '"') { inq = true; q = ch; continue; }
    if (ch == '/' && i + 1 < s.size() && s[i + 1] == '/') return s.substr(0, i);
  }
  return s;
}


// finalizes the pending "if and/or" count byte when the closing jump_if_false / gosub_if_false / return_if_false arrives
void afterInsn(Ctx& c, unsigned base, int ln) {
  if (c.ifPos == std::string::npos) return;
  if (base == c.db.jfOp || base == 0x0AA0 || base == 0x0AA1) {
    if (c.ifCount > 8) c.diags.push_back(mk(9004, "Too many conditions: the game allows up to 8 conditions in one if", -1, ln));
    if (c.ifKind == 'a' && c.ifCount >= 1) c.out[c.ifPos] = (uint8_t)(c.ifCount - 1);
    else if (c.ifKind == 'o') c.out[c.ifPos] = (uint8_t)(c.ifCount + 19);
    c.ifPos = std::string::npos;
  } else {
    c.ifCount++;
  }
}

void compileInsn(Ctx& c, const std::string& line, int ln);

// ---- opcode-less form: "wait 0 ms", "0@ = 5", "not actor $X dead" -> find the opcode by its format pattern
struct Cand { uint16_t op; int score; };

int kindOf(const std::string& t) {  // 0 int literal, 1 float literal, 2 global var, 3 local var, 4 string, 5 other
  std::string x = trim(t);
  long long v;
  if (x.empty()) return 5;
  if (x[0] == '\'' || x[0] == '"') return 4;
  if (isIntText(x, &v)) return 0;
  if (isFloatText(x)) return 1;
  if (x[0] == '$' || (x.size() > 1 && (x[0] == 's' || x[0] == 'v') && x[1] == '$')) return 2;
  return 3;
}


// Sanny annotations like "4 (SLOT_MACHINE)": a "(" that follows a space opens a comment-like group; array syntax has no space before "("
std::string stripAnnotations(const std::string& s) {
  std::string r;
  bool inq = false; char q = 0;
  for (size_t i = 0; i < s.size(); i++) {
    char ch = s[i];
    if (inq) { r += ch; if (ch == q) inq = false; continue; }
    if (ch == '\'' || ch == '"') { inq = true; q = ch; r += ch; continue; }
    if (ch == '(' && i > 0 && s[i - 1] == ' ') {
      size_t e = s.find(')', i);
      if (e != std::string::npos) { i = e; if (!r.empty() && r.back() == ' ' && i + 1 < s.size() && s[i + 1] == ' ') i++; continue; }
    }
    r += ch;
  }
  return r;
}

// encodes one operand; %k% parameters are raw 128-byte buffers (no type byte)
bool encodeOpArg(Ctx& c, const OpDef* def, size_t idx, const std::string& txt, int ln, std::vector<uint8_t>& o) {
  if (def && idx < 32 && ((def->kmask >> idx) & 1)) {
    std::string t = trim(txt);
    if (t.size() < 2 || (t[0] != '"' && t[0] != '\'') || t.back() != t[0]) { c.diags.push_back(mk(58, "Invalid parameter " + t, -1, ln)); return false; }
    std::string s = t.substr(1, t.size() - 2);
    if (s.size() > 127) { c.diags.push_back(mk(27, fmt("String %s is too long. \nMax %d symbols", t.c_str(), 127), -1, ln)); return false; }
    for (size_t i = 0; i < 128; i++) o.push_back(i < s.size() ? (uint8_t)s[i] : 0);
    return true;
  }
  if (def && c.db.oldFormat() && idx < 32 && ((def->smask >> idx) & 1)) {
    std::string t = trim(txt);
    if (t.size() < 2 || (t[0] != '"' && t[0] != '\'') || t.back() != t[0]) { c.diags.push_back(mk(58, "Invalid parameter " + t, -1, ln)); return false; }
    std::string s = t.substr(1, t.size() - 2);
    if (s.size() > 7) { c.diags.push_back(mk(27, fmt("String %s is too long. \nMax %d symbols", t.c_str(), 7), -1, ln)); return false; }
    for (size_t i = 0; i < 8; i++) o.push_back(i < s.size() ? (uint8_t)s[i] : 0);
    return true;
  }
  return encodeArg(c, txt, ln, o);
}

std::string firstTok(const std::string& t);
// ---- class syntax from classes.db
std::vector<std::string> splitArgsTop(const std::string& s) {
  std::vector<std::string> r; std::string cur; int depth = 0; bool inq = false; char q = 0;
  for (char ch : s) {
    if (inq) { cur += ch; if (ch == q) inq = false; continue; }
    if (ch == '\'' || ch == '"') { inq = true; q = ch; cur += ch; continue; }
    if (ch == '(' || ch == '[') depth++; else if (ch == ')' || ch == ']') depth--;
    if (ch == ',' && depth == 0) { r.push_back(trim(cur)); cur.clear(); continue; }
    cur += ch;
  }
  if (!trim(cur).empty() || !r.empty()) r.push_back(trim(cur));
  return r;
}

bool compileClassCall(Ctx& c, const std::string& bodyIn, bool neg, int ln) {
  if (c.db.classMembers.empty()) return false;
  std::string body = trim(bodyIn), lhs, rhs = body;
  {  // "lhs = Class.Prop(args)": the first top-level " = " whose right side starts with a class name
    int depth = 0; bool inq = false; char q = 0;
    for (size_t i = 0; i + 2 < body.size(); i++) {
      char ch = body[i];
      if (inq) { if (ch == q) inq = false; continue; }
      if (ch == '\'' || ch == '"') { inq = true; q = ch; continue; }
      if (ch == '(') depth++; else if (ch == ')') depth--;
      if (depth == 0 && body.compare(i, 3, " = ") == 0) {
        std::string r = trim(body.substr(i + 3));
        size_t dot = r.find('.');
        if (dot != std::string::npos && c.db.classNames.count(lower(r.substr(0, dot)))) { lhs = trim(body.substr(0, i)); rhs = r; }
        break;
      }
    }
  }
  size_t dot = rhs.find('.');
  if (dot == std::string::npos || !c.db.classNames.count(lower(rhs.substr(0, dot)))) return false;
  size_t i = dot + 1;
  while (i < rhs.size() && (std::isalnum((unsigned char)rhs[i]) || rhs[i] == '_')) i++;
  std::string key = lower(rhs.substr(0, i));
  auto mi = c.db.classMembers.find(key);
  if (mi == c.db.classMembers.end()) return false;
  const Data::ClassMember& m = mi->second;
  std::vector<std::string> params;
  size_t j = i;
  while (j < rhs.size() && rhs[j] == ' ') j++;
  if (j < rhs.size() && rhs[j] == '(') {
    int depth = 0; size_t e = j; bool inq = false; char q = 0;
    for (; e < rhs.size(); e++) {
      char ch = rhs[e];
      if (inq) { if (ch == q) inq = false; continue; }
      if (ch == '\'' || ch == '"') { inq = true; q = ch; continue; }
      if (ch == '(') depth++;
      else if (ch == ')' && --depth == 0) break;
    }
    if (e >= rhs.size()) { c.diags.push_back(mk(9001, "Unbalanced parentheses: " + body, -1, ln)); return true; }
    params = splitArgsTop(rhs.substr(j + 1, e - j - 1));
    j = e + 1;
  }
  std::string rest = trim(rhs.substr(j));
  uint16_t op = 0;
  if (!lhs.empty()) {  // result = Class.Prop(args): entry with pos 2, the result is the last parameter
    if (!rest.empty() || !m.prop) return false;
    const Data::ClassEntry* ent = nullptr;
    for (auto& e : m.entries) if (e.pos == 2 && e.math == "=") ent = &e;
    if (!ent) { c.diags.push_back(mk(9006, "'" + key + "' cannot be read into a variable", -1, ln)); return true; }
    op = ent->op;
    params.push_back(lhs);
  } else if (rest.empty()) {
    if (m.prop) { c.diags.push_back(mk(9006, "'" + key + "' is a property: use an assignment or a comparison", -1, ln)); return true; }
    op = m.op;
  } else {  // Class.Prop(args) <math> value
    static const char* ops[] = {"==", ">=", "<=", "+=", "-=", "*=", "/=", "=", ">", "<"};
    std::string mo;
    for (const char* o : ops) if (rest.compare(0, std::strlen(o), o) == 0) { mo = o; break; }
    if (!m.prop || mo.empty()) return false;
    const Data::ClassEntry* ent = nullptr;
    for (auto& e : m.entries) if (e.pos == 1 && e.math == mo) ent = &e;
    if (!ent) { c.diags.push_back(mk(9006, "'" + key + "' does not support the operator " + mo, -1, ln)); return true; }
    op = ent->op;
    params.push_back(trim(rest.substr(mo.size())));
  }
  // hint strings "Name:^OptA^OptB" define named values by position (e.g. Car.SetDriverBehaviour($c, KillThePlayer))
  for (size_t k = 0; k < params.size(); k++) {
    size_t h = lhs.empty() ? k : k + 1;
    static const std::string kNoHint;
    const std::string& hint = h < m.hints.size() ? m.hints[h] : kNoHint;
    size_t at = hint.find(":^");
    std::string opts;
    auto dep = m.depEnums.find((int)h);  // "#DEPRECATED_ENUMS": Class.Member.N = ^A^B (N = index in the hint list: handle / result first, e.g. Car.SetDriverBehaviour.1 = Behaviour)
    if (at != std::string::npos) opts = hint.substr(at + 2);
    else if (dep != m.depEnums.end()) opts = dep->second.substr(dep->second.find('^') == std::string::npos ? 0 : dep->second.find('^') + 1);
    else continue;
    std::string want = lower(trim(params[k]));
    long long idxo = 0;
    size_t pos = 0;
    while (pos <= opts.size()) {  // "A^B^C=4^D": position numbering, "Name=N" sets the value (next options continue from N+1)
      size_t nx = opts.find('^', pos);
      std::string o1 = trim(opts.substr(pos, nx == std::string::npos ? std::string::npos : nx - pos));
      size_t eqp = o1.find('=');
      if (eqp != std::string::npos) { idxo = strtoll(o1.substr(eqp + 1).c_str(), nullptr, 0); o1 = trim(o1.substr(0, eqp)); }
      if (lower(o1) == want && !want.empty()) { params[k] = std::to_string(idxo); break; }
      idxo++;
      if (nx == std::string::npos) break;
      pos = nx + 1;
    }
  }
  const OpDef* def = c.db.op(op);
  if (!def) { c.diags.push_back(mk(10, "Unknown opcode " + hex4(op), -1, ln)); return true; }
  if (def->nparams >= 0 && (int)params.size() != def->nparams) {
    c.diags.push_back(mk((int)params.size() < def->nparams ? 49 : 50, fmt("%s: expecting %d params", hex4(op).c_str(), def->nparams), -1, ln));
    return true;
  }
  put16(c.out, op | (neg ? 0x8000 : 0));
  for (size_t pi = 0; pi < params.size(); pi++) encodeOpArg(c, def, pi, params[pi], ln, c.out);
  if (def->nparams < 0) c.out.push_back(T_END);
  afterInsn(c, op, ln);
  return true;
}

struct PatInfo { uint16_t op; const OpDef* def; std::vector<Piece> pcs; std::string fc; };
struct PatIndex {
  std::vector<PatInfo> all;
  std::map<std::string, std::vector<int>> byWord;  // first literal word (lower case) -> candidates
  std::vector<int> varFirst;                       // patterns that start with a placeholder
};
const PatIndex& patIndexFor(const Data& db) {
  static std::map<std::pair<const Data*, size_t>, PatIndex> cache;
  auto key = std::make_pair(&db, db.ops.size() * 1315423911u + db.keywords.size());
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  PatIndex ix;
  for (const auto& kv : db.ops) {
    std::string fb, fc;
    splitComment(kv.second.fmt, &fb, &fc);
    std::string nb = normalize(fb);
    if (nb.empty()) continue;
    PatInfo pi{kv.first, &kv.second, parsePattern(nb), fc};
    int id = (int)ix.all.size();
    if (!pi.pcs.empty() && !pi.pcs[0].ph) {
      std::string w = lower(firstTok(trim(pi.pcs[0].lit)));
      ix.byWord[w].push_back(id);
    } else ix.varFirst.push_back(id);
    ix.all.push_back(std::move(pi));
  }
  return cache.emplace(key, std::move(ix)).first->second;
}

bool compileByPattern(Ctx& c, const std::string& rawLine, int ln, const std::string& hint) {
  std::string body = normalize(stripAnnotations(normalize(rawLine)));
  bool neg = false;
  if (body.compare(0, 4, "not ") == 0) { neg = true; body = ltrim(body.substr(4)); }
  if (body == "if" || body == "if and" || body == "if or") {
    put16(c.out, c.db.ifOp);
    c.ifPos = c.out.size() + 1;
    c.ifKind = body == "if" ? 0 : body == "if and" ? 'a' : 'o';
    c.ifCount = 0;
    c.out.push_back(T_I8);
    c.out.push_back(0);
    return true;
  }
  struct M { uint16_t op; std::map<int, std::string> caps; std::string extra; std::vector<Piece> pcs; int score; };
  std::vector<M> found;
  if (compileClassCall(c, body, neg, ln)) return true;
  const PatIndex& pix = patIndexFor(c.db);
  std::vector<int> cands = pix.varFirst;
  { auto wi = pix.byWord.find(lower(firstTok(body))); if (wi != pix.byWord.end()) cands.insert(cands.end(), wi->second.begin(), wi->second.end()); }
  struct KV { uint16_t first; const OpDef& second; };
  for (int ci0 : cands) {
    const PatInfo& pinf = pix.all[ci0];
    KV kv{pinf.op, *pinf.def};
    const std::string& fc = pinf.fc;
    const std::vector<Piece>& pcs = pinf.pcs;
    M m;
    m.op = kv.first; m.pcs = pcs; m.score = 0;
    if (!matchPattern(pcs, body, &m.caps, &m.extra)) continue;
    if (!m.extra.empty() && kv.second.nparams >= 0) continue;  // leftover tokens: this is not the right pattern
    // every captured argument must be encodable
    Ctx tmp{c.db, c.opt, {}, {}, {}, {}, false, std::string::npos, 0, 0};
    tmp.defs = c.defs; tmp.allocNames = c.allocNames;
    bool ok = true;
    int maxIdx = 0;
    for (auto& p : pcs) if (p.ph) maxIdx = std::max(maxIdx, p.idx);
    int need = kv.second.nparams >= 0 ? kv.second.nparams : maxIdx;
    if ((int)m.caps.size() < need) ok = false;
    for (auto& cp : m.caps) { if (!ok) break; std::vector<uint8_t> o; ok = encodeArg(tmp, cp.second, ln, o); }
    if (!ok) continue;
    // prefer the opcode whose declared "(int)"/"(float)" matches the operand kinds
    std::string cm = lower(fc);
    bool anyFloat = false, anyInt = false;
    for (auto& cp : m.caps) { int k = kindOf(cp.second); anyFloat |= k == 1; anyInt |= k == 0; }
    bool cf = cm.find("float") != std::string::npos, ci = cm.find("int") != std::string::npos;
    if (anyFloat && cf) m.score += 2;
    if (anyInt && !anyFloat && ci) m.score += 2;
    if (anyFloat && ci && !cf) m.score -= 2;
    // global-vs-local variants: first operand kind
    auto first = m.caps.find(1);
    if (first != m.caps.end()) {
      int k = kindOf(first->second);
      std::string nm = lower(kv.second.fmt);
      if (k == 2 && nm.find("global") != std::string::npos) m.score += 1;
      if (k == 3 && nm.find("local") != std::string::npos) m.score += 1;
    }
    // typed signature from optypes.txt: source (literal/global/local) and int/float must agree
    auto ot = c.db.opTypes.find(kv.first);
    if (ot != c.db.opTypes.end()) {
      std::vector<std::string> sig;
      std::stringstream ss(ot->second); std::string tok;
      while (std::getline(ss, tok, ',')) sig.push_back(tok);
      for (size_t i = 0; i < sig.size() && sig[i].size() >= 3; i++) {
        auto a = m.caps.find((int)i + 1);
        if (a == m.caps.end()) continue;
        int k = kindOf(a->second);
        char src = sig[i][0], ty = sig[i][2];
        if (k == 0 || k == 1) {  // literal
          if (src == 'x') m.score += 3; else if (src != 'o') m.score -= 20;
          if (k == 0 && ty == 'i') m.score += 3; else if (k == 0 && ty == 'f') m.score -= 1;
          if (k == 1 && ty == 'f') m.score += 3; else if (k == 1 && ty == 'i') m.score -= 20;
        } else if (k == 2) { if (src == 'g' || src == 'a') m.score += 3; else if (src == 'x' || src == 'l') m.score -= 20; }
        else if (k == 3) { if (src == 'l' || src == 'a') m.score += 3; else if (src == 'x' || src == 'g') m.score -= 20; }
      }
    }
    { std::string h = lower(hint);  // "// (int)" / "// (float)" hint printed by the decompiler
      bool hf = h.find("(float)") != std::string::npos, hi = h.find("(int)") != std::string::npos;
      if (hf && cf) m.score += 2; else if (hf && ci) m.score -= 2;
      if (hi && ci && !cf) m.score += 2; else if (hi && cf) m.score -= 2; }
    { // the decompiler prints the ini comment after the text: a candidate with a comment the line lacks is less likely
      std::string tc = trim(lower(fc)), th = trim(lower(hint));
      if (th.rfind("//", 0) == 0) th = trim(th.substr(2));
      if (!tc.empty()) { if (!th.empty() && th.find(tc) != std::string::npos) m.score += 3; else m.score -= 3; }
    }
    // more specific (longer literal text) wins over generic "%1d% = %2d%"
    int lit = 0;
    for (auto& p : pcs) if (!p.ph) lit += (int)p.lit.size();
    m.score = m.score * 100 + lit;
    found.push_back(m);
  }
  if (found.empty()) {
    // comparison sugar: "a < b" == "b > a", "a <= b" == "b >= a", "a <> b" == "not a == b"
    struct Sugar { const char* op; const char* repl; bool swap; bool flipNot; };
    static const Sugar sugars[] = {{" <= ", " >= ", true, false}, {" < ", " > ", true, false}, {" <> ", " == ", false, true}, {" != ", " == ", false, true}};
    for (const auto& sg : sugars) {
      size_t at = findOutside(body, sg.op, 0);
      if (at == std::string::npos) continue;
      std::string l = trim(body.substr(0, at)), r = trim(body.substr(at + std::strlen(sg.op)));
      if (l.empty() || r.empty()) continue;
      std::string nb = sg.swap ? r + sg.repl + l : l + sg.repl + r;
      bool n2 = sg.flipNot ? !neg : neg;
      return compileByPattern(c, (n2 ? "not " : "") + nb, ln, hint);
    }
    // keyword form: "<keyword> arg arg ..." -> opcode from keywords.txt, args positional
    std::vector<std::string> tk = splitTokens(body);
    if (tk.empty()) return false;
    auto kw = c.db.keywords.find(lower(tk[0]));
    if (kw == c.db.keywords.end()) return false;
    const OpDef* def = c.db.op(kw->second);
    if (!def) return false;
    put16(c.out, kw->second | (neg ? 0x8000 : 0));
    size_t given = tk.size() - 1;
    if (def->nparams >= 0 && (int)given != def->nparams) {
      c.diags.push_back(mk(49, fmt("Not enough actual parameters. Expected %d params", def->nparams), -1, ln));
      return true;
    }
    for (size_t i = 1; i < tk.size(); i++) encodeOpArg(c, def, i - 1, tk[i], ln, c.out);
    if (def->nparams < 0) c.out.push_back(T_END);
    afterInsn(c, kw->second, ln);
    return true;
  }
  std::sort(found.begin(), found.end(), [](const M& a, const M& b) { return a.score != b.score ? a.score > b.score : a.op < b.op; });
  const M& best = found.front();
  std::string synthetic = hex4(best.op | (neg ? 0x8000 : 0)) + ": " + (neg ? "not " : "") + body;
  compileInsn(c, synthetic, ln);
  return true;
}

void compileInsn(Ctx& c, const std::string& line, int ln) {
  unsigned raw = (unsigned)strtoul(line.substr(0, 4).c_str(), nullptr, 16);
  bool neg = (raw & 0x8000) != 0;
  const OpDef* def = c.db.op((uint16_t)raw);
  if (!def) { c.diags.push_back(mk(10, "Unknown opcode " + upper(line.substr(0, 4)), -1, ln)); return; }
  std::string body = normalize(stripAnnotations(normalize(line.substr(5))));
  if ((raw & 0x7FFF) == c.db.ifOp && !neg && (body == "if" || body == "if and" || body == "if or")) {
    put16(c.out, raw);
    c.ifPos = c.out.size() + 1;  // value byte of the int8 argument
    c.ifKind = body == "if" ? 0 : body == "if and" ? 'a' : 'o';
    c.ifCount = 0;
    c.out.push_back(T_I8);
    c.out.push_back(0);
    return;
  }
  if (neg) {
    if (body.compare(0, 4, "not ") == 0) body = ltrim(body.substr(4));  // the 8xxx bit already means "not"; the word is optional
  }
  std::string fb, fc;
  splitComment(def->fmt, &fb, &fc);
  std::vector<Piece> pcs = parsePattern(normalize(fb));
  std::map<int, std::string> caps;
  std::string extra;
  bool matched = matchPattern(pcs, body, &caps, &extra);
  if (matched && !extra.empty() && def->nparams >= 0) matched = false;
  if (!matched) {
    // Sanny ignores the descriptive words after "XXXX:": only the arguments matter ("0ACD: \"Hello\" 5000" is enough).
    // Take every token that is a valid argument, in order; skip plain words and operator symbols.
    std::vector<std::string> args;
    for (auto& tk : splitTokens(body)) {
      if (tk[0] == '(') continue;  // Sanny annotation such as "(SLOT_MACHINE)"
      Ctx probe{c.db, c.opt, {}, {}, {}, {}, false, std::string::npos, 0, 0};
      probe.defs = c.defs; probe.allocNames = c.allocNames;
      std::vector<uint8_t> tmp;
      if (encodeArg(probe, tk, ln, tmp)) { args.push_back(tk); continue; }
      if (tk[0] == '(') continue;  // Sanny annotation such as "(SLOT_MACHINE)"
      bool word = std::isalpha((unsigned char)tk[0]) || tk[0] == '_';
      bool symbol = true;
      for (char ch : tk) if (std::isalnum((unsigned char)ch) || ch == '_') symbol = false;
      if (!word && !symbol) args.push_back(tk);  // malformed argument: let encodeArg report it below
    }
    int want = def->nparams;
    if (want >= 0 && (int)args.size() > want) {
      // descriptive words that happen to be enum / constant names ("player", "car") are not arguments: drop the weak candidates
      std::vector<std::string> strong;
      for (auto& a : args) {
        char c0 = a[0];
        bool word = std::isalpha((unsigned char)c0) || c0 == '_';
        bool var = c0 == '$' || ((c0 == 's' || c0 == 'v') && a.size() > 1 && a[1] == '$') || a.find('@') != std::string::npos;
        if (!word || var || upper(a) == "TRUE" || upper(a) == "FALSE") strong.push_back(a);
      }
      if ((int)strong.size() == want) args = strong;
    }
    if ((want >= 0 && (int)args.size() != want) || (want < 0 && args.empty() && false)) {
      if ((int)args.size() < want)
        c.diags.push_back(mk(49, fmt("%s: expecting %d params", upper(line.substr(0, 4)).c_str(), want), -1, ln));
      else
        c.diags.push_back(mk(50, fmt("%s: too many parameters, expecting %d", upper(line.substr(0, 4)).c_str(), want), -1, ln));
      return;
    }
    put16(c.out, raw);
    for (size_t ai = 0; ai < args.size(); ai++) encodeOpArg(c, def, ai, args[ai], ln, c.out);
    if (want < 0) c.out.push_back(T_END);
    afterInsn(c, raw & 0x7FFF, ln);
    return;
  }
  put16(c.out, raw);
  int n = def->nparams;
  int maxIdx = 0;
  for (auto& p : pcs) if (p.ph) maxIdx = std::max(maxIdx, p.idx);
  int fixedCount = n >= 0 ? n : maxIdx;
  for (int i = 1; i <= fixedCount; i++) {
    auto it = caps.find(i);
    if (it == caps.end()) {
      c.diags.push_back(mk(49, fmt("Not enough actual parameters. Expected %d params", fixedCount), -1, ln));
      return;
    }
    encodeOpArg(c, def, (size_t)i - 1, it->second, ln, c.out);
  }
  if (n < 0) {
    for (auto& tk : splitTokens(extra)) encodeArg(c, tk, ln, c.out);
    c.out.push_back(T_END);
  }
  afterInsn(c, raw & 0x7FFF, ln);
}


// ---------------------------------------------------------------- high-level control flow -> low-level lines
// if [and|or] <conds> then .. [else ..] end | while <cond> .. end | repeat .. until <cond>
// for v = a to|downto b [step s] .. end | break | continue
// Everything is lowered to "if/and/or + condition lines + jf @L / jump @L / :L" which the line compiler already handles.
struct LLine { std::string text; int ln; };

struct Frame {
  char kind;  // 'i' if, 'w' while, 'r' repeat, 'f' for
  std::string a, b, c;  // labels: if: else,end | loops: loop,end,cont
  bool hasElse = false;
  std::string var, step, endV;
  bool down = false, inCase = false, inDefault = false;
  int ln = 0;
};

std::string firstTok(const std::string& t) {
  size_t i = 0;
  while (i < t.size() && t[i] != ' ' && t[i] != '\t') i++;
  return t.substr(0, i);
}

std::vector<LLine> lowerControlFlow(const std::string& src, std::vector<Diag>* diags) {
  std::vector<std::string> raw = splitLines(src);
  std::vector<LLine> out;
  std::vector<Frame> st;
  int labelN = 0;
  auto newLabel = [&]() { return "__SBL_" + std::to_string(labelN++); };
  auto emit = [&](const std::string& t, int ln) { out.push_back({t, ln}); };
  auto clean = [&](size_t i) { return trim(stripLineComment(raw[i])); };
  auto isHexOpLine = [](const std::string& t) { return t.size() >= 5 && t[4] == ':' && std::isxdigit((unsigned char)t[0]) && std::isxdigit((unsigned char)t[3]); };
  auto condText = [&](std::string t) {  // strips a trailing "then" / "do"
    std::string lt = lower(t);
    for (const char* w : {" then", " do"})
      if (lt.size() > std::strlen(w) && lt.compare(lt.size() - std::strlen(w), std::strlen(w), w) == 0) t = trim(t.substr(0, t.size() - std::strlen(w)));
    return t;
  };
  auto isHlIf = [&](size_t i) -> bool {
    std::string t = clean(i), rest = trim(t.substr(firstTok(t).size()));
    bool hl = lower(rest).size() >= 4 && (lower(rest) == "then" || lower(condText(rest)) != lower(rest));
    for (size_t j = i + 1; !hl && j < raw.size(); j++) {
      std::string tj = clean(j);
      if (tj.empty()) continue;
      std::string wj = lower(firstTok(tj));
      if (wj == "then") return true;
      if (wj == "jf" || wj == "else_jump" || wj == "if" || wj == "jump" || wj == "goto" || (isHexOpLine(tj) && (upper(tj.substr(0, 4)) == "004D" || upper(tj.substr(0, 4)) == "804D"))) break;
    }
    return hl;
  };
  // index of the line that closes the block opened at line i (end / until), or raw.size() when missing
  auto blockEnd = [&](size_t i) -> size_t {
    int depth = 1;
    for (size_t j = i + 1; j < raw.size(); j++) {
      std::string tj = clean(j);
      if (tj.empty()) continue;
      std::string wj = lower(firstTok(tj)), rj = trim(tj.substr(firstTok(tj).size()));
      bool opener = (wj == "while" && !rj.empty()) || wj == "for" || wj == "switch" || (wj == "repeat" && rj.empty()) || (wj == "if" && isHlIf(j));
      bool closer = (wj == "end" && rj.empty()) || (wj == "until" && !rj.empty());
      if (opener) depth++;
      else if (closer && --depth == 0) return j;
    }
    return raw.size();
  };
  auto loopFrame = [&]() -> int { int k = (int)st.size() - 1; while (k >= 0 && (st[k].kind == 'i' || st[k].kind == 's')) k--; return k; };
  bool inHex = false;
  for (size_t i = 0; i < raw.size(); i++) {
    int ln = (int)i + 1;
    std::string t = clean(i);
    if (t.empty()) continue;
    std::string w = lower(firstTok(t));
    std::string rest = trim(t.substr(firstTok(t).size()));
    if (inHex) { emit(t, ln); if (w == "end") inHex = false; continue; }
    if (w == "hex") { inHex = true; emit(t, ln); continue; }

    if (w == "if") {
      // high-level when a "then" line follows the conditions (or is on the header line); otherwise low-level "if" passes through
      bool hl = lower(rest).size() >= 4 && (lower(rest) == "then" || lower(condText(rest)) != lower(rest));
      for (size_t j = i + 1; !hl && j < raw.size(); j++) {
        std::string tj = clean(j);
        if (tj.empty()) continue;
        std::string wj = lower(firstTok(tj));
        if (wj == "then") { hl = true; break; }
        if (wj == "jf" || wj == "else_jump" || wj == "if" || wj == "jump" || wj == "goto" || isHexOpLine(tj) && (upper(tj.substr(0, 4)) == "004D" || upper(tj.substr(0, 4)) == "804D")) break;
      }
      if (!hl) { emit(t, ln); continue; }
      Frame f; f.kind = 'i'; f.a = newLabel(); f.b = newLabel(); f.ln = ln;
      std::string r = condText(rest), lr = lower(r);
      bool hasThenInline = lower(rest) != lr || lower(rest) == "then";
      std::string kind = "";
      if (lr == "and" || lr == "or") { kind = " " + lr; r.clear(); }
      else if (lr.rfind("and ", 0) == 0 || lr.rfind("or ", 0) == 0) { size_t sp = r.find(' '); kind = " " + lower(r.substr(0, sp)); r = trim(r.substr(sp)); }
      if (lower(rest) == "then") r.clear();
      emit("if" + kind, ln);
      if (!r.empty()) emit(r, ln);
      st.push_back(f);
      if (hasThenInline) emit("jf @" + f.a, ln);
      else {
        // conditions follow on the next lines up to "then"
        size_t j = i + 1;
        for (; j < raw.size(); j++) {
          std::string tj = clean(j);
          if (tj.empty()) continue;
          if (lower(firstTok(tj)) == "then") { emit("jf @" + f.a, (int)j + 1); break; }
          emit(tj, (int)j + 1);
        }
        i = j;
      }
      continue;
    }
    if (w == "else" && !st.empty() && st.back().kind == 'i' && rest.empty()) {
      Frame& f = st.back();
      emit("jump @" + f.b, ln);
      emit(":" + f.a, ln);
      f.hasElse = true;
      continue;
    }
    if (w == "while" && !rest.empty()) {
      std::string cnd = condText(rest);
      if (lower(cnd) == "false") {  // docs: "this loop is ignored by the compiler as the condition is never met"
        size_t e = blockEnd(i);
        if (e >= raw.size()) diags->push_back(mk(57, "Internal label not found\nCheck out your high-level constructions. Some of them do not have closing \"end\"", -1, ln));
        i = e;
        continue;
      }
      Frame f; f.kind = 'w'; f.a = newLabel(); f.b = newLabel(); f.c = newLabel(); f.ln = ln;
      emit(":" + f.a, ln);
      if (lower(cnd) != "true") { emit("if", ln); emit(cnd, ln); emit("jf @" + f.b, ln); }
      st.push_back(f);
      continue;
    }
    if (w == "repeat" && rest.empty()) {
      Frame f; f.kind = 'r'; f.a = newLabel(); f.b = newLabel(); f.c = newLabel(); f.ln = ln;
      emit(":" + f.a, ln);
      st.push_back(f);
      continue;
    }
    if (w == "until" && !rest.empty() && !st.empty() && st.back().kind == 'r') {
      Frame f = st.back(); st.pop_back();
      emit(":" + f.c, ln);
      if (lower(rest) == "false") emit("jump @" + f.a, ln);  // sanny.exe 0x571108: until False -> unconditional jump
      else if (lower(rest) != "true") { emit("if", ln); emit(rest, ln); emit("jf @" + f.a, ln); }
      emit(":" + f.b, ln);
      continue;
    }
    if (w == "for") {  // for v = a to|downto b [step s]
      std::vector<std::string> tk = splitTokens(rest);
      size_t toAt = 0;
      for (size_t k = 0; k < tk.size(); k++) { std::string l = lower(tk[k]); if (l == "to" || l == "downto") toAt = k; }
      if (tk.size() < 5 || tk[1] != "=" || !toAt) {
        diags->push_back(mk(23, "'for <var> = <start> to|downto <end> [step <n>]' expected", -1, ln));
        continue;
      }
      Frame f; f.kind = 'f'; f.a = newLabel(); f.b = newLabel(); f.c = newLabel(); f.ln = ln;
      f.var = tk[0]; f.down = lower(tk[toAt]) == "downto"; f.step = "1";
      std::string startV, endV;
      for (size_t k = 2; k < toAt; k++) startV += (k > 2 ? " " : "") + tk[k];
      size_t k = toAt + 1;
      for (; k < tk.size() && lower(tk[k]) != "step"; k++) endV += (endV.empty() ? "" : " ") + tk[k];
      if (k + 1 < tk.size()) f.step = tk[k + 1] == "=" && k + 2 < tk.size() ? tk[k + 2] : tk[k + 1];
      f.endV = endV;
      {  // literal bounds must agree with the direction (ERR_0025 Incorrect counter range)
        long long iv1, iv2; bool lit = (isIntText(startV, &iv1) && isIntText(endV, &iv2)), fl = false;
        double a1 = 0, a2 = 0;
        if (lit) { a1 = (double)iv1; a2 = (double)iv2; }
        else if ((isIntText(startV, &iv1) || isFloatText(startV)) && (isIntText(endV, &iv2) || isFloatText(endV))) { a1 = atof(startV.c_str()); a2 = atof(endV.c_str()); fl = true; }
        if ((lit || fl) && (f.down ? a1 < a2 : a1 > a2)) diags->push_back(mk(25, "Incorrect counter range", -1, ln));
      }
      emit(f.var + " = " + startV, ln);
      long long dummy;
      auto isConst = [&](const std::string& x) { return isIntText(x, &dummy) || isFloatText(x) || (!x.empty() && x[0] == '#'); };
      if (!(isConst(startV) && isConst(endV))) {  // entry check is skipped when both bounds are literals
        emit("if", ln);
        emit(f.down ? startV + " >= " + endV : endV + " >= " + startV, ln);
        emit("jf @" + f.b, ln);
      }
      emit(":" + f.a, ln);  // loop start
      st.push_back(f);
      continue;
    }
    if (w == "break" || w == "continue") {
      int k = loopFrame();
      if (k < 0) { diags->push_back(mk(9003, w + " outside of a loop", -1, ln)); continue; }
      emit("jump @" + (w == "break" ? st[k].b : st[k].c), ln);
      continue;
    }
    if (w == "switch") {
      if (rest.empty()) diags->push_back(mk(102, "Switch statement needs a variable", -1, ln));
      Frame f; f.kind = 's'; f.var = rest; f.b = newLabel(); f.ln = ln;
      st.push_back(f);
      continue;
    }
    if ((w == "case" || w == "default") && !st.empty() && st.back().kind == 's') {
      Frame& f = st.back();
      if (f.inDefault) { diags->push_back(mk(9005, "'default' must be the last case of a switch", -1, ln)); continue; }
      if (f.inCase) { emit("jump @" + f.b, ln); emit(":" + f.a, ln); }  // finish the previous case: no fall-through
      f.inCase = true;
      if (w == "default") { f.inDefault = true; continue; }
      if (f.var.empty()) continue;  // ERR_0102 already reported
      std::vector<std::string> vals;
      {  // comma separated values, commas inside quotes are kept
        std::string cur; bool inq = false; char q = 0;
        for (char ch : rest) {
          if (inq) { cur += ch; if (ch == q) inq = false; continue; }
          if (ch == '\'' || ch == '"') { inq = true; q = ch; cur += ch; continue; }
          if (ch == ',') { vals.push_back(trim(cur)); cur.clear(); continue; }
          cur += ch;
        }
        if (!trim(cur).empty() || !vals.empty()) vals.push_back(trim(cur));
      }
      if (vals.empty() || vals.back().empty()) { diags->push_back(mk(104, "Expected a value for the switch case", -1, ln)); continue; }
      if (vals.size() > 8) { diags->push_back(mk(71, "Incorrect number of conditions: a case takes up to 8 values", -1, ln)); continue; }
      f.a = newLabel();
      emit(vals.size() > 1 ? "if or" : "if", ln);
      for (auto& v : vals) emit(f.var + " == " + v, ln);
      emit("jf @" + f.a, ln);
      continue;
    }
    if (!st.empty() && st.back().kind == 's' && !st.back().inCase && w != "end") {
      diags->push_back(mk(103, "Expected a switch case", -1, ln));  // nothing is allowed between switch and the first case / outside case blocks
      continue;
    }
    if (w == "end" && rest.empty() && !st.empty()) {
      Frame f = st.back(); st.pop_back();
      if (f.kind == 's') { if (f.inCase && !f.inDefault) emit(":" + f.a, ln); emit(":" + f.b, ln); }
      else if (f.kind == 'i') emit(":" + (f.hasElse ? f.b : f.a), ln);
      else if (f.kind == 'w') { emit(":" + f.c, ln); emit("jump @" + f.a, ln); emit(":" + f.b, ln); }  // sanny.exe 0x56fd65: label, jump, label
      else if (f.kind == 'f') {  // sanny.exe 0x56fb08: :cont  v += s  if v > end  jf @loop  :end   (downto: v -= s, end > v)
        emit(":" + f.c, ln);
        emit(f.var + (f.down ? " -= " : " += ") + f.step, ln);
        emit("if", ln);
        emit(f.down ? f.endV + " > " + f.var : f.var + " > " + f.endV, ln);
        emit("jf @" + f.a, ln);
        emit(":" + f.b, ln);
      } else {
        diags->push_back(mk(9002, "'end' cannot close 'repeat'; use 'until <condition>'", -1, ln));
      }
      continue;
    }
    if (w == "end" && rest.empty() && st.empty()) { diags->push_back(mk(9002, "Unexpected 'end'", -1, ln)); continue; }
    {  // "jf Break" / "jump Continue": the keyword replaces a label operand
      std::vector<std::string> tk = splitTokens(t);
      if (tk.size() >= 2) {
        std::string last = lower(tk.back());
        if (last == "break" || last == "continue") {
          int k = loopFrame();
          if (k < 0) { diags->push_back(mk(9003, tk.back() + " outside of a loop", -1, ln)); continue; }
          std::string nt;
          for (size_t q = 0; q + 1 < tk.size(); q++) nt += tk[q] + " ";
          emit(nt + "@" + (last == "break" ? st[k].b : st[k].c), ln);
          continue;
        }
      }
    }
    emit(t, ln);
  }
  for (auto& f : st) diags->push_back(mk(57, "Internal label not found\nCheck out your high-level constructions. Some of them do not have closing \"end\"", -1, f.ln));
  return out;
}

}  // namespace


// "DEFINE OBJECTS n", "DEFINE OBJECT name", "DEFINE MISSIONS n", "DEFINE MISSION i AT @L", "DEFINE EXTERNAL_SCRIPTS n",
// "DEFINE SCRIPT name AT @L", "DEFINE UNKNOWN_* n"  -> metadata only (binary header layout is not emitted yet)
bool handleDefine(Ctx& c, const std::string& line, int ln) {
  std::string t = trim(stripLineComment(line));
  std::vector<std::string> tk = splitTokens(t);
  if (tk.size() < 2 || upper(tk[0]) != "DEFINE") return false;
  std::string kind = upper(tk[1]);
  auto num = [&](size_t i) { return i < tk.size() ? atoll(tk[i].c_str()) : 0LL; };
  if (kind == "OBJECTS") c.defs.declaredObjects = (int)num(2);
  else if (kind == "OBJECT") {
    std::string name = trim(t.substr(t.find(tk[1]) + tk[1].size()));
    c.defs.objects.push_back(name == "(noname)" ? "" : name);
  } else if (kind == "MISSIONS") c.defs.declaredMissions = (int)num(2);
  else if (kind == "EXTERNAL_SCRIPTS") c.defs.declaredExternals = (int)num(2);
  else if ((kind == "MISSION" || kind == "SCRIPT") && tk.size() >= 5 && upper(tk[3]) == "AT") {
    std::string lbl = tk[4][0] == '@' ? tk[4].substr(1) : tk[4];
    if (kind == "MISSION") c.defs.missions.push_back({(int)num(2), lbl});
    else c.defs.externalScripts.push_back({tk[2], lbl});
  } else if (kind.rfind("UNKNOWN_", 0) == 0) c.defs.unknownSegments.push_back({kind, num(2)});
  else { c.diags.push_back(mk(9001, "Unsupported syntax (unknown DEFINE): " + t, -1, ln)); }
  return true;
}

// "Alloc($NAME, N)": binds a global variable name to its index
bool handleAlloc(Ctx& c, const std::string& line, int ln) {
  std::string t = trim(stripLineComment(line));
  if (upper(t).rfind("ALLOC(", 0) != 0 || t.back() != ')') return false;
  std::string in = t.substr(6, t.size() - 7);
  size_t cm = in.find(',');
  if (cm == std::string::npos) { c.diags.push_back(mk(9001, "Alloc($NAME, index) expected: " + t, -1, ln)); return true; }
  std::string nm = trim(in.substr(0, cm)), ix = trim(in.substr(cm + 1));
  if (nm.empty() || nm[0] != '$') { c.diags.push_back(mk(9001, "Alloc($NAME, index) expected: " + t, -1, ln)); return true; }
  c.allocNames[upper(nm.substr(1))] = atoi(ix.c_str());
  return true;
}

CompileResult compile(const std::string& source, const Data& db, const Options& opt) {
  Ctx c{db, opt, {}, {}, {}, {}, false, std::string::npos, 0, 0};
  c.cleo = opt.forceRelativeLabels;
  c.allocNames = opt.allocNames;
  c.defs.objects = opt.objectNames;
  if (!db.formatVerified() && !opt.allowUnverified) {
    CompileResult r;
    r.diags.push_back(mk(9100, "Binary encoding of game '" + db.game + "' is not verified yet (SA / SA Mobile only). Set allowUnverified to try the SA encoding anyway."));
    return r;
  }
  std::vector<LLine> lines = lowerControlFlow(source, &c.diags);
  bool inHex = false;
  std::vector<std::string> rawLines = splitLines(source);
  for (const auto& L : lines) {
    int ln = L.ln;
    std::string line = L.text;
    if (inHex) {
      if (lower(firstTok(line)) == "end") { inHex = false; continue; }
      // hex .. end data block (reference: box_hello_world.csa): "text" -> raw ASCII bytes, hex digits in pairs ("20 00", "0000", "4D41")
      for (size_t hi = 0; hi < line.size();) {
        char ch = line[hi];
        if (ch == ' ' || ch == '\t') { hi++; continue; }
        if (ch == '"' || ch == '\'') {
          size_t e = line.find(ch, hi + 1);
          if (e == std::string::npos) { c.diags.push_back(mk(28, "Unterminated string " + line.substr(hi), -1, ln)); break; }
          for (size_t k = hi + 1; k < e; k++) c.out.push_back((uint8_t)line[k]);
          hi = e + 1;
          continue;
        }
        if (std::isxdigit((unsigned char)ch) && hi + 1 < line.size() && std::isxdigit((unsigned char)line[hi + 1])) {
          c.out.push_back((uint8_t)strtoul(line.substr(hi, 2).c_str(), nullptr, 16));
          hi += 2;
          continue;
        }
        c.diags.push_back(mk(9007, "Invalid data in a hex block: " + line.substr(hi, 12), -1, ln));
        break;
      }
      continue;
    }
    if (lower(line) == "hex") { inHex = true; continue; }
    // keep the trailing "// comment" of the original line: it is a hint for choosing among identical patterns
    std::string rawLine = ln >= 1 && ln <= (int)rawLines.size() && trim(stripLineComment(rawLines[ln - 1])) == line ? rawLines[ln - 1] : line;
    if (line.empty()) continue;
    if (handleDefine(c, line, ln) || handleAlloc(c, line, ln)) continue;
    if (line[0] == '{' && line.size() > 2 && line[1] == '$') {
      if (upper(line).rfind("{$CLEO", 0) == 0 || upper(line).rfind("{$E}", 0) == 0) c.cleo = true;
      continue;
    }
    if (line[0] == ':') {
      std::string name = upper(trim(line.substr(1)));
      if (c.labels.count(name)) c.diags.push_back(mk(56, "Label \"" + name + "\" redeclared", -1, ln));
      c.labels[name] = (uint32_t)c.out.size();
      continue;
    }
    if (line.size() >= 5 && line[4] == ':' && std::isxdigit((unsigned char)line[0]) && std::isxdigit((unsigned char)line[1]) &&
        std::isxdigit((unsigned char)line[2]) && std::isxdigit((unsigned char)line[3])) {
      compileInsn(c, line, ln);
      continue;
    }
    if (!compileByPattern(c, line, ln, rawLine.substr(stripLineComment(rawLine).size())))
      c.diags.push_back(mk(9001, "Unsupported syntax (unknown command): " + line, -1, ln));
  }
  for (auto& f : c.fixups) {
    auto it = c.labels.find(f.name);
    int32_t v = 0;
    bool external = false;
    auto ext = opt.externalLabels.find(f.name);
    if (it == c.labels.end() && ext != opt.externalLabels.end()) { v = (int32_t)ext->second; external = true; }
    else if (it == c.labels.end()) {
      // "Label_68981": a label outside this block (e.g. a mission calling main-script code): keep the absolute number
      size_t us = f.name.rfind('_');
      bool digits = us != std::string::npos && us + 1 < f.name.size();
      for (size_t q = us + 1; digits && q < f.name.size(); q++) digits = std::isdigit((unsigned char)f.name[q]);
      if (!digits) { c.diags.push_back(mk(54, "Label \"" + f.name + "\" not found", -1, f.line)); continue; }
      v = (int32_t)atoll(f.name.c_str() + us + 1);
      external = true;
    } else v = (int32_t)it->second;
    if (external) {}
    else if (c.cleo && opt.negativeLabels) v = -v;
    else v += (int32_t)opt.baseOffset;  // absolute label operand = file offset
    uint32_t u = (uint32_t)v;
    for (int i = 0; i < 4; i++) c.out[f.pos + i] = (u >> (8 * i)) & 0xFF;
  }
  CompileResult r;
  r.maxGlobal = c.maxGlobal;
  r.labels = c.labels;
  r.defs = std::move(c.defs);
  r.bytes = std::move(c.out);
  r.diags = std::move(c.diags);
  r.ok = r.diags.empty();
  return r;
}

ExtraInfo readExtraInfo(const uint8_t* d, size_t n) {
  ExtraInfo e;
  static const char mark[8] = {'_', '_', 'S', 'B', 'F', 'T', 'R', 0};
  if (n < 12 || memcmp(d + n - 8, mark, 8) != 0) return e;
  uint32_t cs = d[n - 12] | (d[n - 11] << 8) | (d[n - 10] << 16) | ((uint32_t)d[n - 9] << 24);
  if (cs > n - 12) return e;
  e.present = true;
  e.codeSize = cs;
  size_t p = cs, end = n - 12;
  while (p < end) {  // chunks: NAME\0 u32 [payload for SRC]
    size_t q = p;
    while (q < end && d[q]) q++;
    if (q + 5 > end) break;
    std::string name((const char*)d + p, q - p);
    uint32_t v = d[q + 1] | (d[q + 2] << 8) | (d[q + 3] << 16) | ((uint32_t)d[q + 4] << 24);
    p = q + 5;
    if (name == "FLAG") { e.hasFlags = true; e.flags = v; }
    else if (name == "SRC") { if (p + v > end) break; e.source.assign((const char*)d + p, v); p += v; }
    else break;  // unknown chunk: layout not known
  }
  return e;
}

std::vector<uint8_t> writeExtraInfo(const std::vector<uint8_t>& code, uint32_t flags, const std::string& source) {
  std::vector<uint8_t> o = code;
  auto s0 = [&](const char* n) { for (const char* c = n; *c; c++) o.push_back((uint8_t)*c); o.push_back(0); };
  s0("FLAG"); put32(o, flags);
  s0("SRC"); put32(o, (uint32_t)source.size());
  o.insert(o.end(), source.begin(), source.end());
  put32(o, (uint32_t)code.size());
  s0("__SBFTR");
  return o;
}

}  // namespace sb
