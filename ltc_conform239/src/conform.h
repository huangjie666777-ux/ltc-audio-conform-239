#pragma once
#include "audio_reader.h"
#include "ltc_frame.h"
#include "segments.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ltc {

constexpr int kOutputSamplesPerFrame = 1920;  // 48000 / 25
constexpr int64_t kFramesPerDay = 25LL * 24 * 60 * 60;

struct ConformOptions {
  std::string pathA;
  std::string pathB;
  int channelA = -1;   // timecode channel of file A (0 or 1)
  int channelB = -1;   // timecode channel of file B (0 or 1)
  int segmentA = 0;    // continuous-segment index into file A
  int segmentB = 0;    // continuous-segment index into file B
  Timecode start{};    // common start timecode
  int64_t frames = 0;  // frames to deliver (> 0, less than 24 h)
  std::string outWav;
  std::string jsonPath;  // empty = stdout
  int chunkFrames = 4096;
};

// Decodes the LTC track of one file with the standard pipeline
// (WavReader, BiphaseDecoder, FrameAssembler, SegmentBuilder).
bool decodeFile(const std::string& path, int channel, int chunkFrames,
                std::vector<LtcFrame>& frames, std::vector<Segment>& segments,
                int& channels, std::string& error);

// Selects count consecutive frames starting at the given timecode from
// one continuous segment. The segment must exist, contain the start
// timecode exactly once, and cover the whole range without a break.
bool selectRange(const std::vector<LtcFrame>& frames,
                 const std::vector<Segment>& segments, int segmentIndex,
                 const Timecode& start, int64_t count, const LtcFrame*& first,
                 std::string& error);

// Maps one output frame onto the real codeword interval of the source:
// output sample j reads source position start + j * len / outLen with
// linear interpolation, clamped so it never reads past the last sample.
void resampleFrame(const int16_t* src, int64_t len, int16_t* out,
                   int outLen);

// Runs the full dual-recorder conform. Returns 0 on success; on failure
// error is set and no complete-looking output WAV is left behind.
int runConform(const ConformOptions& opt, std::string& error);

}  // namespace ltc
