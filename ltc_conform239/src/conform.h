#pragma once
#include "ltc_frame.h"
#include "segments.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ltc {

// Result of streaming one recorder's timecode channel through the decoder.
struct DecodedFile {
  std::vector<LtcFrame> frames;
  std::vector<Segment> segments;
  int channels = 0;
};

// Streams the timecode channel of a 48 kHz PCM16 WAV through the
// biphase/frame/segment pipeline. The file is only read, never modified.
bool decodeFile(const std::string& path, int channel, int chunkFrames,
                DecodedFile& out, std::string& error);

struct SyncRequest {
  std::string pathA;
  std::string pathB;
  int tcChannelA = -1;      // timecode channel in file A (0 or 1)
  int tcChannelB = -1;      // timecode channel in file B (0 or 1)
  int segmentA = 0;         // continuous-segment index in file A
  int segmentB = 0;         // continuous-segment index in file B
  Timecode start;           // common start timecode (25 fps)
  int frames = 0;           // number of frames to deliver (>0, < 24 h)
  int chunkFrames = 4096;
  std::string outWav;       // 48 kHz PCM16 stereo output path
  std::string jsonPath;     // handoff record path; empty = stdout
};

// Conforms the program channels of two recorders onto a common 25 fps
// timeline: output frame i takes 1920 samples per channel, linearly
// resampled from each recorder's real codeword interval of that frame.
// Left channel = program audio of A, right channel = program audio of B.
// Refuses (leaving no output WAV behind) when a segment is missing, the
// start timecode is absent or repeated inside the segment, or the range
// runs past the end of the continuous run.
bool conformSync(const SyncRequest& req, std::string& error);

}  // namespace ltc
