#include "sbapi.h"

#include <algorithm>
#include <cstring>
#include <regex>
#include <set>
#include <sstream>

namespace sb {
namespace {

std::string trimS(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
  return s.substr(a, b - a);
}
std::string upperS(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
std::vector<std::string> linesOf(const std::string& t) {
  std::vector<std::string> r; std::string cur;
  for (char c : t) { if (c == '\n') { r.push_back(cur); cur.clear(); } else if (c != '\r') cur += c; }
  if (!cur.empty()) r.push_back(cur);
  return r;
}
Diag mkd(int code, const std::string& msg) { Diag d; d.code = code; d.msg = msg; return d; }

// thread name of a code block: the first 03A4 script_name argument
std::string threadName(const std::vector<uint8_t>& block, const Data& db, const std::string& fallback) {
  Options o; o.allowUnverified = true; o.negativeLabels = true; o.externalPrefix = "X";
  size_t cap = std::min<size_t>(block.size(), 96);  // the name is the first instruction
  auto r = decompile(block.data(), cap, db, o);
  size_t p = r.text.find("03A4: script_name '");
  if (p == std::string::npos) p = r.text.find("script_name '");
  if (p == std::string::npos) return fallback;
  p = r.text.find('\'', p) + 1;
  size_t e = r.text.find('\'', p);
  std::string n = r.text.substr(p, e - p);
  for (auto& c : n) if (!isalnum((unsigned char)c) && c != '_') c = '_';
  return n.empty() ? fallback : upperS(n);
}

const char* kMainMarker = "//-------------MAIN---------------";

}  // namespace

FileKind detectKind(const std::vector<uint8_t>& d) {
  MainScm m; std::string e;
  if (d.size() > 64 && parseMainScm(d, &m, &e)) return FileKind::MainScm;
  return FileKind::CleoScript;
}

FileKind detectSourceKind(const std::string& text) {
  if (text.find("DEFINE OBJECTS") != std::string::npos && text.find("//-------------MAIN") != std::string::npos) return FileKind::MainScm;
  return FileKind::CleoScript;
}

// ============================================================================ main.scm -> text
TextResult decompileMainScm(const std::vector<uint8_t>& file, const Data& db, const AnyOptions& ao) {
  TextResult res; res.kind = FileKind::MainScm;
  MainScm m; std::string err;
  if (!parseMainScm(file, &m, &err)) { res.diags.push_back(mkd(9200, err)); return res; }
  size_t codeStart = file.size();
  for (auto& b : m.blocks) codeStart -= b.size();
  std::vector<std::string> names(m.blocks.size());
  for (size_t i = 0; i < m.blocks.size(); i++) names[i] = threadName(m.blocks[i], db, i == 0 ? "MAIN" : "MISSION_" + std::to_string(i - 1));
  // missions first: collect the main-script offsets they refer to
  std::vector<std::string> missionText(m.blocks.size());
  std::set<long long> mainTargets;
  std::regex rx("@MAIN_([0-9]+)");
  for (size_t i = 1; i < m.blocks.size(); i++) {
    Options o; o.allowUnverified = ao.allowUnverified; o.negativeLabels = true; o.externalPrefix = "MAIN"; o.labelPrefix = names[i];
    auto r = decompile(m.blocks[i].data(), m.blocks[i].size(), db, o);
    for (auto& d : r.diags) res.diags.push_back(d);
    missionText[i] = r.text;
    for (std::sregex_iterator it(r.text.begin(), r.text.end(), rx), end; it != end; ++it) mainTargets.insert(atoll((*it)[1].str().c_str()));
  }
  std::string out;
  out += std::string("// sannycore: ") + (m.saLayout ? "GTA San Andreas" : "GTA III / Vice City") + " main.scm\n";
  out += "DEFINE OBJECTS " + std::to_string(m.objects.size()) + "\n";
  for (auto& n : m.objects) out += "DEFINE OBJECT " + (n.empty() ? std::string("(noname)") : n) + "\n";
  out += "DEFINE MISSIONS " + std::to_string(m.blocks.size() - 1) + "\n";
  for (size_t i = 1; i < m.blocks.size(); i++) out += "DEFINE MISSION " + std::to_string(i - 1) + " AT @" + names[i] + "\n";
  if (m.saLayout) {
    out += "DEFINE EXTERNAL_SCRIPTS " + std::to_string(m.externals.size()) + "\n";
    for (auto& x : m.externals) out += "DEFINE EXTERNAL " + x.name + " " + std::to_string(x.offset) + " " + std::to_string(x.size) + "\n";
    out += "DEFINE X_LARGEST_EXTERNAL " + std::to_string(m.largestExternal) + "\n";
    out += "DEFINE UNKNOWN_EMPTY_SEGMENT " + std::to_string(m.emptySegment) + "\n";
    out += "DEFINE UNKNOWN_THREADS_MEMORY " + std::to_string(m.threadsMemory[0]) + " " + std::to_string(m.threadsMemory[1]) + "\n";
    out += "DEFINE X_MISSION_ENTRY0 " + std::to_string(m.firstMissionEntry) + "\n";
  }
  out += "DEFINE X_VARIABLE_BYTES " + std::to_string(m.varBytes) + "\n";
  for (size_t i = 0; i < m.varSpace.size(); i++)
    if (m.varSpace[i] && !(m.saLayout && i == 0 && m.varSpace[i] == 0x73)) out += "DEFINE X_VARIABLE_BYTE " + std::to_string(i + 7) + " " + std::to_string(m.varSpace[i]) + "\n";
  out += "\n" + std::string(kMainMarker) + "\n";
  {
    Options o; o.allowUnverified = ao.allowUnverified; o.baseOffset = (uint32_t)codeStart; o.labelPrefix = names[0];
    o.extraLabelTargets.assign(mainTargets.begin(), mainTargets.end());
    auto r = decompile(m.blocks[0].data(), m.blocks[0].size(), db, o);
    for (auto& d : r.diags) res.diags.push_back(d);
    out += r.text;
  }
  for (size_t i = 1; i < m.blocks.size(); i++) {
    out += "\n//-------------Mission " + std::to_string(i - 1) + "---------------\n\n:" + names[i] + "\n" + missionText[i];
  }
  res.text = out;
  res.ok = true;
  return res;
}

// ============================================================================ text -> main.scm
BinaryResult compileMainScm(const std::string& source, const Data& db, const AnyOptions& ao) {
  BinaryResult res; res.kind = FileKind::MainScm;
  auto lines = linesOf(source);
  // split: header, MAIN, missions, external scripts (Sanny's own source layout, also what decompileMainScm writes)
  enum Kind { KMain, KMission, KExternal };
  std::vector<std::string> header; std::vector<std::string>* cur = &header;
  std::vector<std::vector<std::string>> blocks; std::vector<int> kinds; std::vector<std::string> markerNames;
  for (auto& l : lines) {
    std::string t = trimS(l);
    int k = -1;
    if (t.rfind("//-------------MAIN", 0) == 0) k = KMain;
    else if (t.rfind("//-------------Mission", 0) == 0) k = KMission;
    else if (t.rfind("//-------------External script", 0) == 0) k = KExternal;
    if (k >= 0) {
      blocks.emplace_back(); kinds.push_back(k); cur = &blocks.back();
      std::string nm; size_t lp = t.find('('), rp = t.rfind(')');
      if (k == KExternal && lp != std::string::npos && rp != std::string::npos && rp > lp) nm = t.substr(lp + 1, rp - lp - 1);
      markerNames.push_back(nm);
      continue;
    }
    cur->push_back(l);
  }
  size_t mainIdx = (size_t)-1;
  for (size_t i = 0; i < kinds.size(); i++) if (kinds[i] == KMain) { mainIdx = i; break; }
  if (mainIdx == (size_t)-1) { res.diags.push_back(mkd(9201, "No //-------------MAIN--------------- section")); return res; }
  MainScm m;
  m.saLayout = db.gameEnum == 2 || db.gameEnum == 6;
  bool varBytesGiven = false; uint32_t varBytes = 0, entry0 = 0;
  std::vector<std::pair<size_t, uint8_t>> varBytesSet;
  size_t declaredMissions = 0; long long declaredExternals = -2; bool threadsGiven = false;
  std::vector<std::string> scriptNames;  // DEFINE SCRIPT NAME AT @LABEL (Sanny's own syntax)
  std::map<std::string, int> allocNames;
  for (auto& l : header) {
    std::string t = trimS(l);
    size_t cm = t.find("//");
    if (cm != std::string::npos) t = trimS(t.substr(0, cm));
    if (upperS(t).rfind("ALLOC(", 0) == 0 && t.back() == ')') {  // Alloc($NAME, n)
      std::string in = t.substr(6, t.size() - 7); size_t cc = in.find(',');
      std::string nm = trimS(in.substr(0, cc == std::string::npos ? in.size() : cc));
      if (cc != std::string::npos && !nm.empty() && nm[0] == '$') allocNames[upperS(nm.substr(1))] = atoi(in.substr(cc + 1).c_str());
      continue;
    }
    if (t.rfind("DEFINE ", 0) != 0) continue;
    std::istringstream ss(t.substr(7)); std::string kind; ss >> kind; kind = upperS(kind);
    std::string rest; std::getline(ss, rest); rest = trimS(rest);
    if (kind == "OBJECT") m.objects.push_back(rest == "(noname)" ? "" : rest);
    else if (kind == "MISSIONS") declaredMissions = (size_t)atoll(rest.c_str());
    else if (kind == "EXTERNAL_SCRIPTS") declaredExternals = atoll(rest.c_str());
    else if (kind == "SCRIPT") { std::istringstream rs(rest); std::string nm; rs >> nm; scriptNames.push_back(nm); }
    else if (kind == "EXTERNAL") { std::istringstream rs(rest); MainScm::External x; rs >> x.name >> x.offset >> x.size; m.externals.push_back(x); }
    else if (kind == "X_LARGEST_EXTERNAL") m.largestExternal = (uint32_t)atoll(rest.c_str());
    else if (kind == "UNKNOWN_EMPTY_SEGMENT") m.emptySegment = (uint32_t)atoll(rest.c_str());
    else if (kind == "UNKNOWN_THREADS_MEMORY") {
      std::istringstream rs(rest); long long a = 0, b2 = 0; int n = 0;
      if (rs >> a) n++;
      if (rs >> b2) n++;
      if (n == 2) { m.threadsMemory[0] = (uint32_t)a; m.threadsMemory[1] = (uint32_t)b2; threadsGiven = true; }  // sannycore's own form: both fields
    }
    else if (kind == "X_MISSION_ENTRY0") entry0 = (uint32_t)atoll(rest.c_str());
    else if (kind == "X_VARIABLE_BYTES") { varBytesGiven = true; varBytes = (uint32_t)atoll(rest.c_str()); }
    else if (kind == "X_VARIABLE_BYTE") { std::istringstream rs(rest); long long off = 0, v = 0; rs >> off >> v; varBytesSet.push_back({(size_t)off, (uint8_t)v}); }
  }
  m.firstMissionEntry = entry0;
  std::vector<size_t> missionBlocks, externalBlocks;
  for (size_t i = 0; i < kinds.size(); i++) { if (kinds[i] == KMission) missionBlocks.push_back(i); else if (kinds[i] == KExternal) externalBlocks.push_back(i); }
  size_t missionCount = missionBlocks.size();
  if (declaredMissions && declaredMissions != missionCount) res.diags.push_back(mkd(9202, "DEFINE MISSIONS says " + std::to_string(declaredMissions) + " but " + std::to_string(missionCount) + " mission sections found"));
  m.missionOffsets.assign(m.saLayout ? missionCount + 1 : missionCount, 0);
  // external scripts compiled from sections: header entries = the sections (+ the special AAA entry unless DEFINE EXTERNAL_SCRIPTS -1)
  bool externalsFromSections = m.saLayout && !externalBlocks.empty() && m.externals.empty();
  bool addAAA = externalsFromSections && declaredExternals != -1;
  if (externalsFromSections) {
    for (size_t k = 0; k < externalBlocks.size(); k++) {
      MainScm::External x; x.name = k < scriptNames.size() ? scriptNames[k] : markerNames[externalBlocks[k]];
      m.externals.push_back(x);
    }
    if (addAAA) { MainScm::External x; x.name = "AAA"; x.offset = 0; x.size = 8; m.externals.push_back(x); }
  }
  auto joinBlock = [&](size_t i) { std::string s; for (auto& l : blocks[i]) { s += l; s += "\n"; } return s; };
  auto relOpts = [&](size_t i) { Options o; o.allowUnverified = ao.allowUnverified; o.strictStringLimit = ao.strictStringLimit; o.forceRelativeLabels = i != mainIdx; o.negativeLabels = i != mainIdx; o.objectNames = m.objects; o.allocNames = allocNames; o.unknownModelsAsZero = ao.unknownModelsAsZero; return o; };

  // pass A: highest global variable referenced (the variable space is sized from it unless X_VARIABLE_BYTES is given)
  if (!varBytesGiven) {
    int maxGlobal = -1;
    for (size_t i = 0; i < blocks.size(); i++) maxGlobal = std::max(maxGlobal, compile(joinBlock(i), db, relOpts(i)).maxGlobal);
    // GTA III: exactly (highest used slot + 1) * 4 (verified); SA: the same plus 8 bytes at the end (verified: 12301 slots -> 49204 + 8 = 49212 in the real file,
    // and "UNKNOWN_THREADS_MEMORY" field 0 = 49204)
    varBytes = (uint32_t)(maxGlobal + 1) * 4 + (m.saLayout ? 8 : 0);
    if (varBytes < 8) varBytes = 8;
  }
  m.varBytes = varBytes;
  m.varSpace.assign(varBytes - 7, 0);
  if (m.saLayout) m.varSpace[0] = 0x73;
  for (auto& vb : varBytesSet) if (vb.first >= 7 && vb.first - 7 < m.varSpace.size()) m.varSpace[vb.first - 7] = vb.second;
  if (m.saLayout && !threadsGiven) { m.threadsMemory[0] = varBytes - 8; m.threadsMemory[1] = 4941; }  // Sanny's "UNKNOWN_THREADS_MEMORY 1": values taken from the real file (second one is an assumption)
  size_t headerSize = varBytes + (12 + 24 * m.objects.size()) + (20 + 4 * m.missionOffsets.size());
  if (m.saLayout) headerSize += 16 + 28 * m.externals.size() + 12 + 16;
  // pass B: MAIN with absolute labels, then missions and externals with MAIN's labels as external targets
  m.blocks.assign(1 + missionCount, {});
  Options mo = relOpts(mainIdx); mo.baseOffset = (uint32_t)headerSize;
  auto mr = compile(joinBlock(mainIdx), db, mo);
  for (auto& d : mr.diags) res.diags.push_back(d);
  m.blocks[0] = mr.bytes;
  std::map<std::string, uint32_t> mainLabels;
  for (auto& kv : mr.labels) mainLabels[kv.first] = kv.second + (uint32_t)headerSize;
  for (size_t k = 0; k < missionCount; k++) {
    Options o = relOpts(missionBlocks[k]); o.externalLabels = mainLabels;
    auto r = compile(joinBlock(missionBlocks[k]), db, o);
    for (auto& d : r.diags) { Diag dd = d; dd.msg = "[mission " + std::to_string(k) + "] " + dd.msg; res.diags.push_back(dd); }
    m.blocks[1 + k] = r.bytes;
  }
  for (size_t k = 0; k < externalBlocks.size(); k++) {
    Options o = relOpts(externalBlocks[k]); o.externalLabels = mainLabels;
    auto r = compile(joinBlock(externalBlocks[k]), db, o);
    for (auto& d : r.diags) { Diag dd = d; dd.msg = "[external script " + std::to_string(k) + "] " + dd.msg; res.diags.push_back(dd); }
    res.externals.push_back({m.externals.size() > k ? m.externals[k].name : markerNames[externalBlocks[k]], r.bytes});
  }
  bool bad = false;
  for (auto& d : res.diags) if (d.code != 9202) bad = true;
  if (externalsFromSections) {  // offsets: first = size of this file, then previous offset + size; the largest external size goes into the header
    size_t fileSize = headerSize;
    for (auto& b : m.blocks) fileSize += b.size();
    uint64_t run = fileSize; uint32_t largest = 0;
    for (size_t k = 0; k < externalBlocks.size(); k++) {
      m.externals[k].offset = (uint32_t)run; m.externals[k].size = (uint32_t)res.externals[k].second.size();
      run += m.externals[k].size; largest = std::max<uint32_t>(largest, m.externals[k].size);
    }
    m.largestExternal = largest;
  }
  res.bytes = buildMainScm(m);
  res.ok = !bad;
  return res;
}

// ============================================================================ any
TextResult decompileAny(const std::vector<uint8_t>& file, const Data& db, const AnyOptions& ao) {
  if (detectKind(file) == FileKind::MainScm) return decompileMainScm(file, db, ao);
  TextResult res; res.kind = FileKind::CleoScript;
  ExtraInfo ex = readExtraInfo(file.data(), file.size());
  Options o; o.scriptStyle = ao.scriptStyle; o.allowUnverified = ao.allowUnverified;
  if (!ao.cleoDirective.empty()) o.cleoExt = ao.cleoDirective;
  auto r = decompile(file.data(), ex.present ? ex.codeSize : file.size(), db, o);
  res.text = r.text; res.diags = r.diags; res.ok = r.ok;
  return res;
}

BinaryResult compileAny(const std::string& source, const Data& db, const AnyOptions& ao) {
  if (detectSourceKind(source) == FileKind::MainScm) return compileMainScm(source, db, ao);
  BinaryResult res; res.kind = FileKind::CleoScript;
  Options o; o.allowUnverified = ao.allowUnverified; o.strictStringLimit = ao.strictStringLimit;
  auto r = compile(source, db, o);
  res.diags = r.diags; res.ok = r.ok;
  res.bytes = ao.addExtraInfo && r.ok ? writeExtraInfo(r.bytes, ao.extraInfoFlags, source) : r.bytes;
  return res;
}

}  // namespace sb
