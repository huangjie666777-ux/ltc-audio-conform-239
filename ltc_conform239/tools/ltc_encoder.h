// Header-only LTC encoder used by the sample generator and the tests to
// produce reproducible 48 kHz PCM16 waveforms. Not part of the decoder.
#pragma once
#include "../src/ltc_frame.h"
#include <array>
#include <cstdint>
#include <vector>

namespace ltcgen {

struct Options {
  ltc::Timecode start{1, 0, 0, 0};
  int frames = 100;             // number of 80-bit frames to emit
  double speed = 1.0;           // playback speed factor (bit rate scales)
  int polarity = 1;             // -1 inverts the waveform
  int leadSilence = 0;          // leading silence in samples
  int tailSilence = 0;          // trailing silence in samples
  bool startMidFrame = false;   // begin halfway through the first frame
  bool dropFrameFlag = false;   // set bit 10 (decoder must reject)
  int jumpAt = -1;              // frame index at which the timecode jumps
  ltc::Timecode jumpTo{0, 0, 0, 0};
  int gapAt = -1;               // frame index after which silence is cut in
  int gapSamples = 0;           // length of that silence in samples
  int badAt = -1;               // frame index corrupted with drop flag
  int amplitude = 24000;
};

inline std::array<uint8_t, 80> frameBits(const ltc::Timecode& tc,
                                         bool dropFlag) {
  std::array<uint8_t, 80> b{};
  auto put = [&](int pos, int value, int n) {
    for (int i = 0; i < n; ++i) b[pos + i] = (value >> i) & 1;
  };
  put(0, tc.f % 10, 4); put(8, tc.f / 10, 2);
  b[10] = dropFlag ? 1 : 0;
  put(16, tc.s % 10, 4); put(24, tc.s / 10, 3);
  put(32, tc.m % 10, 4); put(40, tc.m / 10, 3);
  put(48, tc.h % 10, 4); put(56, tc.h / 10, 2);
  const uint16_t sync = 0xBFFC;  // bits 64..79 = 0011111111111101
  for (int i = 0; i < 16; ++i) b[64 + i] = (sync >> i) & 1;
  return b;
}

// Renders the biphase-mark waveform. Bit cells are placed by sample
// position (cell k spans [k*T, (k+1)*T) with T = 24/speed), so a
// constant speed deviation shifts the bit period uniformly.
inline std::vector<int16_t> encode(const Options& o) {
  std::vector<uint8_t> bits;
  ltc::Timecode tc = o.start;
  for (int i = 0; i < o.frames; ++i) {
    if (i == o.jumpAt) tc = o.jumpTo;
    const bool drop = o.dropFrameFlag || i == o.badAt;
    const auto fb = frameBits(tc, drop);
    bits.insert(bits.end(), fb.begin(), fb.end());
    tc = ltc::increment(tc);
  }
  size_t firstBit = 0;
  if (o.startMidFrame) firstBit = 40;  // drop the first half frame

  const double t = 24.0 / o.speed;
  const int64_t totalCells = static_cast<int64_t>(bits.size());
  const int64_t lead = o.leadSilence;
  const int64_t audioLen = static_cast<int64_t>(totalCells * t) + 2;
  const int64_t gapPos =
      (o.gapAt >= 0) ? lead + static_cast<int64_t>(o.gapAt * 80.0 * t) : -1;

  std::vector<int16_t> out;
  out.reserve(static_cast<size_t>(lead + audioLen + o.gapSamples + o.tailSilence));
  out.insert(out.end(), lead, 0);

  int level = o.polarity > 0 ? 1 : -1;
  int64_t k = static_cast<int64_t>(firstBit);  // current cell index
  bool midScheduled = false;
  double nextToggle = static_cast<double>(k) * t;  // absolute schedule
  for (int64_t s = 0; s < audioLen; ++s) {
    if (gapPos >= 0 && lead + s >= gapPos && lead + s < gapPos + o.gapSamples) {
      out.push_back(0);
      continue;
    }
    while (s >= nextToggle - 1e-9) {
      level = -level;
      if (!midScheduled && k < totalCells && bits[k]) {
        nextToggle = static_cast<double>(k) * t + t / 2.0;  // mid-cell toggle
        midScheduled = true;
      } else {
        ++k;
        nextToggle = static_cast<double>(k) * t;  // cell boundary
        midScheduled = false;
      }
    }
    out.push_back(static_cast<int16_t>(level * o.amplitude));
  }
  out.insert(out.end(), o.tailSilence, 0);
  return out;
}

}  // namespace ltcgen
