#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace ltc {

struct Timecode {
  int h = 0, m = 0, s = 0, f = 0;
};

std::string toString(const Timecode& tc);  // "HH:MM:SS:FF"
Timecode increment(const Timecode& tc);    // +1 frame at 25 fps, midnight wrap

struct LtcFrame {
  Timecode tc;
  std::array<uint8_t, 80> bits{};  // bit i = SMPTE frame bit i (0..79)
  int64_t startSample = 0;         // half-open sample interval of the
  int64_t endSample = 0;           // 80-bit codeword on the audio track
};

std::string bitsToHex(const std::array<uint8_t, 80>& bits);

// Assembles SMPTE 80-bit LTC frames from a bit stream. Searches for the
// sync word 0011111111111101 (bits 64..79), validates BCD time fields
// (h 0-23, m/s 0-59, f 0-24) and rejects drop-frame-flagged frames.
// Only complete frames are delivered; partial frames at stream edges or
// across gaps are discarded.
class FrameAssembler {
public:
  std::function<void(const LtcFrame&)> onFrame;
  std::function<void()> onBadFrame;  // sync found but validation failed

  void pushBit(int value, int64_t cellStart, int64_t cellEnd);
  void reset();  // called on signal gaps

private:
  std::array<uint8_t, 80> bits_{};
  std::array<int64_t, 80> starts_{};
  std::array<int64_t, 80> ends_{};
  uint64_t count_ = 0;      // bits since last reset (indexes ring above)
  uint16_t syncWindow_ = 0; // last 16 bits, most recent in LSB
};

}  // namespace ltc
