// main.scm container: header segments + MAIN code + mission code blocks (layouts verified on real GTA SA Android and GTA III Android files).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sb {

struct MainScm {
  bool saLayout = true;                    // true: SA (segments with ids 0..4 incl. external scripts); false: GTA III / VC (3 segments)
  std::vector<uint8_t> varSpace;           // raw bytes of file[7 .. varBytes): initial values of the global variables (usually zeros)
  uint32_t varBytes = 0;                   // target of the first jump = size of the variable space (byte offset of the objects segment)
  std::vector<std::string> objects;        // DEFINE OBJECT names (24-byte NUL padded fields; index 0 is the unnamed slot)
  std::vector<uint32_t> missionOffsets;    // as stored in the file; build() recomputes them from the block sizes
  struct External { std::string name; uint32_t offset = 0, size = 0; };
  std::vector<External> externals;         // SA only (offsets point into script.img)
  uint32_t largestExternal = 0;            // SA only
  uint32_t emptySegment = 0;               // SA segment id 3 payload
  uint32_t threadsMemory[2] = {0, 0};      // SA segment id 4 payload
  std::vector<std::vector<uint8_t>> blocks;  // [0] = MAIN code, [1..] = mission code blocks (offsets >= main size)
  uint32_t firstMissionEntry = 0;          // SA keeps a dummy value in mission table entry 0 (975 in the real file)
};

bool parseMainScm(const std::vector<uint8_t>& file, MainScm* out, std::string* err);
std::vector<uint8_t> buildMainScm(const MainScm& m);

}  // namespace sb
