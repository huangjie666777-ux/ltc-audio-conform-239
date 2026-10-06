#pragma once
#include <cstdint>
#include <functional>

namespace ltc {

struct BitEvent {
  int value;
  int64_t cellStart;
  int64_t cellEnd;
};

class BiphaseDecoder {
public:
  static constexpr double kNominalBitPeriod = 24.0;
  static constexpr double kMinBitPeriod = 24.0 * 0.98;
  static constexpr double kMaxBitPeriod = 24.0 * 1.02;

  std::function<void(const BitEvent&)> onBit;
  std::function<void()> onGap;

  void pushSample(int16_t sample, int64_t index);
  void finish();

private:
  void processEdge(int64_t pos);
  int sign_ = 0;
  double bitPeriod_ = kNominalBitPeriod;
  bool haveEdge_ = false;
  int64_t lastEdge_ = 0;
  bool halfPending_ = false;
  int64_t cellStart_ = 0;
};

}  // namespace ltc
