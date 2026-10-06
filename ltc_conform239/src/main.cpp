// ltc_conform239: offline LTC (SMPTE 80-bit, 25 fps, non-drop) scanner for
// 48 kHz PCM16 WAV files. No UI, no playback, no video, no network.
#include "audio_reader.h"
#include "biphase.h"
#include "ltc_frame.h"
#include "segments.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr int kDefaultChunk = 4096;

void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s <input.wav> [--channel 0|1|auto] [--chunk N] "
               "[--json out.json]\n",
               argv0);
}

// Counts zero-crossing edges in a short probe to pick the LTC channel.
int pickChannel(ltc::WavReader& wav) {
  const uint64_t probeFrames = std::min<uint64_t>(wav.totalFrames(), kSampleRate * 2);
  std::vector<int16_t> buf(probeFrames);
  int best = 0;
  uint64_t bestEdges = 0;
  for (int ch = 0; ch < wav.channels(); ++ch) {
    wav.seekToStart();
    const uint64_t got = wav.readChannel(ch, buf.data(), probeFrames);
    uint64_t edges = 0;
    int sign = 0;
    for (uint64_t i = 0; i < got; ++i) {
      if (sign <= 0 && buf[i] > 1000) { sign = 1; ++edges; }
      else if (sign >= 0 && buf[i] < -1000) { sign = -1; ++edges; }
    }
    if (edges > bestEdges) { bestEdges = edges; best = ch; }
  }
  wav.seekToStart();
  return best;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { usage(argv[0]); return 2; }
  std::string inputPath;
  int channel = -1;  // -1 = auto
  int chunkFrames = kDefaultChunk;
  std::string jsonPath;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--channel" && i + 1 < argc) {
      const std::string v = argv[++i];
      channel = (v == "auto") ? -1 : std::atoi(v.c_str());
    } else if (a == "--chunk" && i + 1 < argc) {
      chunkFrames = std::atoi(argv[++i]);
    } else if (a == "--json" && i + 1 < argc) {
      jsonPath = argv[++i];
    } else if (!a.empty() && a[0] == '-') {
      usage(argv[0]); return 2;
    } else {
      inputPath = a;
    }
  }
  if (inputPath.empty() || chunkFrames <= 0) { usage(argv[0]); return 2; }

  ltc::WavReader wav;
  std::string error;
  if (!wav.open(inputPath, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  if (channel < 0 || channel >= wav.channels()) channel = pickChannel(wav);

  ltc::BiphaseDecoder decoder;
  ltc::FrameAssembler assembler;
  ltc::SegmentBuilder segments;
  std::vector<ltc::LtcFrame> frames;
  std::vector<ltc::Segment> segs;
  std::string breakReason = "signal_loss";

  decoder.onBit = [&](const ltc::BitEvent& b) {
    assembler.pushBit(b.value, b.cellStart, b.cellEnd);
  };
  decoder.onGap = [&]() {
    assembler.reset();
    segments.onBreak("signal_loss");
  };
  assembler.onFrame = [&](const ltc::LtcFrame& f) {
    frames.push_back(f);
    segments.onFrame(f);
  };
  assembler.onBadFrame = [&]() { segments.onBreak("invalid_frame"); };
  segments.onSegment = [&](const ltc::Segment& s) { segs.push_back(s); };

  // Stream the selected channel in chunks; state carries across chunk
  // boundaries so the chunk size never affects the decoded output.
  std::vector<int16_t> buf(chunkFrames);
  int64_t base = 0;
  uint64_t got;
  while ((got = wav.readChannel(channel, buf.data(), chunkFrames)) > 0) {
    for (uint64_t i = 0; i < got; ++i) {
      decoder.pushSample(buf[i], base + static_cast<int64_t>(i));
    }
    base += static_cast<int64_t>(got);
  }
  decoder.finish();
  segments.finish();

  nlohmann::json j;
  j["file"] = inputPath;
  j["sample_rate"] = kSampleRate;
  j["channels"] = wav.channels();
  j["channel"] = channel;
  j["chunk_frames"] = chunkFrames;
  j["standard"] = "LTC 25fps non-drop, 80-bit SMPTE frame";
  j["frame_count"] = frames.size();
  j["frames"] = nlohmann::json::array();
  for (const auto& f : frames) {
    j["frames"].push_back({
        {"timecode", ltc::toString(f.tc)},
        {"bits_hex", ltc::bitsToHex(f.bits)},
        {"sample_start", f.startSample},
        {"sample_end", f.endSample},
        {"seconds_start", f.startSample / static_cast<double>(kSampleRate)},
        {"seconds_end", f.endSample / static_cast<double>(kSampleRate)},
    });
  }
  j["segments"] = nlohmann::json::array();
  for (const auto& s : segs) {
    j["segments"].push_back({
        {"start_timecode", ltc::toString(s.first.tc)},
        {"end_timecode", ltc::toString(s.last.tc)},
        {"frames", s.frames},
        {"start_sample", s.first.startSample},
        {"end_sample", s.last.endSample},
        {"start_seconds", s.first.startSample / static_cast<double>(kSampleRate)},
        {"end_seconds", s.last.endSample / static_cast<double>(kSampleRate)},
        {"end_reason", s.endReason},
    });
  }

  const std::string text = j.dump(2);
  if (jsonPath.empty()) {
    std::fputs(text.c_str(), stdout);
    std::fputc('\n', stdout);
  } else {
    FILE* fp = std::fopen(jsonPath.c_str(), "w");
    if (!fp) { std::fprintf(stderr, "error: cannot write %s\n", jsonPath.c_str()); return 1; }
    std::fputs(text.c_str(), fp);
    std::fputc('\n', fp);
    std::fclose(fp);
  }
  return 0;
}
