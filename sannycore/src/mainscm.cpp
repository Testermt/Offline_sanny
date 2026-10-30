#include "sbmainscm.h"

#include <algorithm>

namespace sb {
namespace {
uint32_t rd32(const std::vector<uint8_t>& d, size_t o) { return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | ((uint32_t)d[o + 3] << 24); }
void wr32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((v >> (8 * i)) & 0xFF); }
void jumpTo(std::vector<uint8_t>& o, uint32_t target) { o.push_back(0x02); o.push_back(0x00); o.push_back(0x01); wr32(o, target); }
bool isJump(const std::vector<uint8_t>& d, size_t o) { return o + 8 <= d.size() && d[o] == 0x02 && d[o + 1] == 0x00 && d[o + 2] == 0x01; }
}  // namespace

bool parseMainScm(const std::vector<uint8_t>& d, MainScm* m, std::string* err) {
  auto fail = [&](const char* e) { if (err) *err = e; return false; };
  *m = MainScm();
  std::vector<size_t> seg;  // start offsets of the chained segments
  size_t o = 0;
  while (isJump(d, o) && rd32(d, o + 3) > o && rd32(d, o + 3) <= d.size()) { seg.push_back(o); o = rd32(d, o + 3); }
  size_t codeStart = o;
  if (seg.size() < 3) return fail("not a main.scm (segment chain too short)");
  m->varBytes = rd32(d, 3);
  m->varSpace.assign(d.begin() + 7, d.begin() + m->varBytes);
  size_t s1 = seg[1];  // objects: id byte, u32 count, 24-byte names
  uint32_t oc = rd32(d, s1 + 8);
  if (s1 + 12 + 24ull * oc != seg[2]) return fail("objects segment size mismatch");
  for (uint32_t i = 0; i < oc; i++) {
    size_t e = s1 + 12 + 24ull * i;
    std::string nm;
    for (size_t k = 0; k < 24 && d[e + k]; k++) nm += (char)d[e + k];
    m->objects.push_back(nm);
  }
  size_t s2 = seg[2];  // missions: id, main size, largest mission, count, offsets
  uint32_t mainSize = rd32(d, s2 + 8), mc = rd32(d, s2 + 16);
  m->saLayout = seg.size() >= 6;
  uint32_t entries = (uint32_t)(((seg.size() > 3 ? seg[3] : codeStart) - s2 - 20) / 4);  // SA: 1 dummy entry + count missions; III/VC: count entries
  (void)mc;
  for (uint32_t i = 0; i < entries; i++) m->missionOffsets.push_back(rd32(d, s2 + 20 + 4ull * i));
  if (m->saLayout) {
    size_t s3 = seg[3];
    m->largestExternal = rd32(d, s3 + 8);
    uint32_t ec = rd32(d, s3 + 12);
    for (uint32_t i = 0; i < ec; i++) {
      size_t e = s3 + 16 + 28ull * i;
      MainScm::External x;
      for (size_t k = 0; k < 20 && d[e + k]; k++) x.name += (char)d[e + k];
      x.offset = rd32(d, e + 20); x.size = rd32(d, e + 24);
      m->externals.push_back(x);
    }
    m->emptySegment = rd32(d, seg[4] + 8);
    m->threadsMemory[0] = rd32(d, seg[5] + 8);
    m->threadsMemory[1] = rd32(d, seg[5] + 12);
  }
  if (mainSize < codeStart || mainSize > d.size()) return fail("main size outside the file");
  m->blocks.push_back(std::vector<uint8_t>(d.begin() + codeStart, d.begin() + mainSize));
  std::vector<uint32_t> starts;
  for (uint32_t i = 0; i < entries; i++) {
    if (m->missionOffsets[i] >= mainSize && m->missionOffsets[i] < d.size()) starts.push_back(m->missionOffsets[i]);
    else if (i == 0) m->firstMissionEntry = m->missionOffsets[i];
  }
  for (size_t i = 0; i < starts.size(); i++)
    m->blocks.push_back(std::vector<uint8_t>(d.begin() + starts[i], d.begin() + (i + 1 < starts.size() ? starts[i + 1] : d.size())));
  return true;
}

std::vector<uint8_t> buildMainScm(const MainScm& m) {
  size_t objSegLen = 12 + 24 * m.objects.size(), missCount = m.missionOffsets.size(), missSegLen = 20 + 4 * missCount;
  size_t extSegLen = m.saLayout ? 16 + 28 * m.externals.size() : 0;
  size_t s1 = m.varBytes, s2 = s1 + objSegLen, s3 = s2 + missSegLen, s4 = s3 + extSegLen, s5 = s4 + 12, codeStart = m.saLayout ? s5 + 16 : s3;
  size_t mainSize = codeStart + (m.blocks.empty() ? 0 : m.blocks[0].size());
  std::vector<uint8_t> o;
  jumpTo(o, (uint32_t)s1);
  o.insert(o.end(), m.varSpace.begin(), m.varSpace.end());
  jumpTo(o, (uint32_t)s2); o.push_back(0); wr32(o, (uint32_t)m.objects.size());
  for (auto& n : m.objects) for (size_t k = 0; k < 24; k++) o.push_back(k < n.size() ? (uint8_t)n[k] : 0);
  uint32_t largest = 0;
  for (size_t i = 1; i < m.blocks.size(); i++) largest = std::max<uint32_t>(largest, (uint32_t)m.blocks[i].size());
  jumpTo(o, (uint32_t)s3); o.push_back(m.saLayout ? 1 : 0); wr32(o, (uint32_t)mainSize); wr32(o, largest); wr32(o, (uint32_t)(m.saLayout ? missCount - 1 : missCount));
  {
    uint32_t pos = (uint32_t)mainSize;
    size_t bi = 1;
    for (size_t i = 0; i < missCount; i++) {
      if (m.saLayout && i == 0) { wr32(o, m.firstMissionEntry); continue; }  // dummy entry 0 of the SA table
      wr32(o, pos);
      if (bi < m.blocks.size()) pos += (uint32_t)m.blocks[bi++].size();
    }
  }
  if (m.saLayout) {
    jumpTo(o, (uint32_t)s4); o.push_back(2); wr32(o, m.largestExternal); wr32(o, (uint32_t)m.externals.size());
    for (auto& x : m.externals) {
      for (size_t k = 0; k < 20; k++) o.push_back(k < x.name.size() ? (uint8_t)x.name[k] : 0);
      wr32(o, x.offset); wr32(o, x.size);
    }
    jumpTo(o, (uint32_t)s5); o.push_back(3); wr32(o, m.emptySegment);
    jumpTo(o, (uint32_t)codeStart); o.push_back(4); wr32(o, m.threadsMemory[0]); wr32(o, m.threadsMemory[1]);
  }
  for (auto& b : m.blocks) o.insert(o.end(), b.begin(), b.end());
  return o;
}

}  // namespace sb
