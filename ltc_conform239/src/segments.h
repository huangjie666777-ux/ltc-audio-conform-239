#pragma once
#include "ltc_frame.h"
#include <functional>
#include <string>

namespace ltc {

struct Segment {
  LtcFrame first;
  LtcFrame last;
  int frames = 0;
  std::string endReason;
};

// Groups delivered frames into continuous runs. A run continues only
// when the next frame is adjacent on the audio track and its timecode
// is exactly +1 frame (midnight wrap allowed). Silence, bad frames and
// timecode jumps end the run; frames are never interpolated.
class SegmentBuilder {
public:
  std::function<void(const Segment&)> onSegment;

  void onFrame(const LtcFrame& frame);
  void onBreak(const std::string& reason);
  void finish();

private:
  void close(const std::string& reason);
  Segment current_;
  bool open_ = false;
};

}  // namespace ltc
