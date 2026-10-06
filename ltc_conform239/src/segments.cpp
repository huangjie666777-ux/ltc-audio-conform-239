#include "segments.h"
#include <cstdlib>

namespace ltc {

namespace {
// Frames of a continuous signal start within a couple of bit cells of
// the previous frame's end (80 * 24 samples nominal at 48 kHz).
constexpr int64_t kAdjacencyTolerance = 64;

bool sameTc(const ltc::Timecode& a, const ltc::Timecode& b) {
  return a.h == b.h && a.m == b.m && a.s == b.s && a.f == b.f;
}
}  // namespace

void SegmentBuilder::onFrame(const LtcFrame& frame) {
  if (open_) {
    const int64_t gap = frame.startSample - current_.last.endSample;
    const bool adjacent = std::llabs(gap) <= kAdjacencyTolerance;
    const bool consecutive = sameTc(frame.tc, increment(current_.last.tc));
    if (adjacent && consecutive) {
      current_.last = frame;
      ++current_.frames;
      return;
    }
    close(!adjacent ? "position_gap" : "timecode_jump");
  }
  current_ = Segment{};
  current_.first = frame;
  current_.last = frame;
  current_.frames = 1;
  open_ = true;
}

void SegmentBuilder::onBreak(const std::string& reason) { close(reason); }

void SegmentBuilder::finish() { close("end_of_stream"); }

void SegmentBuilder::close(const std::string& reason) {
  if (!open_) return;
  current_.endReason = reason;
  open_ = false;
  if (onSegment) onSegment(current_);
}

}  // namespace ltc
