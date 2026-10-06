#include "ltc_frame.h"
#include <cstdio>

namespace ltc {

namespace {
constexpr uint16_t kSyncWord = 0x3FFD;  // bits 64..79: 0011111111111101

int bcd(const std::array<uint8_t, 80>& b, int lo, int n) {
  int v = 0;
  for (int i = n - 1; i >= 0; --i) v = (v << 1) | b[lo + i];
  return v;
}

bool validate(const std::array<uint8_t, 80>& b, Timecode& tc) {
  if (b[10] != 0) return false;  // drop-frame flag: unsupported
  const int fu = bcd(b, 0, 4), ft = bcd(b, 8, 2);
  const int su = bcd(b, 16, 4), st = bcd(b, 24, 3);
  const int mu = bcd(b, 32, 4), mt = bcd(b, 40, 3);
  const int hu = bcd(b, 48, 4), ht = bcd(b, 56, 2);
  if (fu > 9 || su > 9 || mu > 9 || hu > 9) return false;
  tc.f = ft * 10 + fu;
  tc.s = st * 10 + su;
  tc.m = mt * 10 + mu;
  tc.h = ht * 10 + hu;
  if (tc.f > 24 || tc.s > 59 || tc.m > 59 || tc.h > 23) return false;
  return true;
}
}  // namespace

std::string toString(const Timecode& tc) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d", tc.h, tc.m, tc.s, tc.f);
  return buf;
}

Timecode increment(const Timecode& tc) {
  Timecode r = tc;
  if (++r.f == 25) {
    r.f = 0;
    if (++r.s == 60) {
      r.s = 0;
      if (++r.m == 60) {
        r.m = 0;
        if (++r.h == 24) r.h = 0;  // midnight wrap
      }
    }
  }
  return r;
}

std::string bitsToHex(const std::array<uint8_t, 80>& bits) {
  std::string hex(20, '0');
  for (int i = 0; i < 80; ++i) {
    if (bits[i]) hex[i / 4] = static_cast<char>(hex[i / 4] | (1u << (i % 4)));
  }
  for (char& c : hex) {
    const int v = static_cast<unsigned char>(c) & 0x0F;
    c = static_cast<char>(v < 10 ? '0' + v : 'a' + (v - 10));
  }
  return hex;
}

void FrameAssembler::pushBit(int value, int64_t cellStart, int64_t cellEnd) {
  const size_t slot = static_cast<size_t>(count_ % 80);
  bits_[slot] = static_cast<uint8_t>(value);
  starts_[slot] = cellStart;
  ends_[slot] = cellEnd;
  ++count_;
  syncWindow_ = static_cast<uint16_t>((syncWindow_ << 1) | (value & 1));

  if (count_ >= 80 && syncWindow_ == kSyncWord) {
    LtcFrame frame;
    for (int i = 0; i < 80; ++i) {
      const size_t s = static_cast<size_t>((count_ - 80 + i) % 80);
      frame.bits[i] = bits_[s];
      if (i == 0) frame.startSample = starts_[s];
      if (i == 79) frame.endSample = ends_[s];
    }
    if (validate(frame.bits, frame.tc)) {
      if (onFrame) onFrame(frame);
    } else if (onBadFrame) {
      onBadFrame();
    }
  }
}

void FrameAssembler::reset() {
  count_ = 0;
  syncWindow_ = 0;
}

}  // namespace ltc
