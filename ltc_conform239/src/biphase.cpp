#include "biphase.h"

namespace ltc {

namespace {
constexpr int16_t kEdgeThreshold = 1000;

double clampPeriod(double p) {
  if (p < BiphaseDecoder::kMinBitPeriod) return BiphaseDecoder::kMinBitPeriod;
  if (p > BiphaseDecoder::kMaxBitPeriod) return BiphaseDecoder::kMaxBitPeriod;
  return p;
}
}  // namespace

void BiphaseDecoder::pushSample(int16_t sample, int64_t index) {
  if (sign_ <= 0 && sample > kEdgeThreshold) {
    sign_ = 1;
    processEdge(index);
  } else if (sign_ >= 0 && sample < -kEdgeThreshold) {
    sign_ = -1;
    processEdge(index);
  }
}

void BiphaseDecoder::processEdge(int64_t pos) {
  if (!haveEdge_) {
    haveEdge_ = true;
    lastEdge_ = pos;
    cellStart_ = pos;
    return;
  }
  const double interval = static_cast<double>(pos - lastEdge_);
  lastEdge_ = pos;
  const double t = bitPeriod_;

  if (interval > 1.5 * t) {
    halfPending_ = false;
    cellStart_ = pos;
    if (onGap) onGap();
    return;
  }

  if (interval < 0.75 * t) {
    bitPeriod_ = clampPeriod(0.85 * bitPeriod_ + 0.15 * interval * 2.0);
    if (halfPending_) {
      halfPending_ = false;
      if (onBit) onBit({1, cellStart_, pos});
      cellStart_ = pos;
    } else {
      halfPending_ = true;
    }
  } else {
    bitPeriod_ = clampPeriod(0.85 * bitPeriod_ + 0.15 * interval);
    if (halfPending_) {
      halfPending_ = false;
      cellStart_ = pos;
      if (onGap) onGap();
      return;
    }
    if (onBit) onBit({0, cellStart_, pos});
    cellStart_ = pos;
  }
}

void BiphaseDecoder::finish() {}

}  // namespace ltc
