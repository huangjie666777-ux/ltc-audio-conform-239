// Dual-recorder conform: selects a common timecode range from two LTC
// recordings and delivers both program channels as one 48 kHz PCM16
// stereo WAV, resampled per frame against the real codeword intervals.
#include "conform.h"
#include "biphase.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "nlohmann/json.hpp"

namespace ltc {

namespace {

constexpr uint32_t kSampleRate = 48000;

bool sameTc(const Timecode& a, const Timecode& b) {
  return a.h == b.h && a.m == b.m && a.s == b.s && a.f == b.f;
}

// Forward-only cursor over the program channel. Frame intervals are
// non-decreasing, so a small sliding buffer satisfies every read without
// loading the file into memory.
class ProgramCursor {
public:
  bool open(const std::string& path, int programChannel, int chunk,
            std::string& error) {
    if (!wav_.open(path, error)) return false;
    ch_ = programChannel;
    chunk_ = chunk > 0 ? chunk : 4096;
    return true;
  }

  bool seek(int64_t frame, std::string& error) {
    if (!wav_.seekToFrame(static_cast<uint64_t>(frame))) {
      error = "cannot seek to sample " + std::to_string(frame);
      return false;
    }
    pos_ = frame;
    head_ = 0;
    buf_.clear();
    return true;
  }

  // Copies the half-open interval [start, end) into out.
  bool readInterval(int64_t start, int64_t end, std::vector<int16_t>& out,
                    std::string& error) {
    if (start < pos_ + static_cast<int64_t>(head_)) {
      error = "internal: backward read on program channel";
      return false;
    }
    head_ += static_cast<size_t>(start - (pos_ + static_cast<int64_t>(head_)));
    const int64_t need = end - start;
    while (static_cast<int64_t>(buf_.size() - head_) < need) {
      const size_t keep = buf_.size() - head_;
      if (head_ > 0) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(head_));
        head_ = 0;
        pos_ = start;
      }
      const size_t want = std::max<int64_t>(chunk_, need - keep);
      const size_t off = buf_.size();
      buf_.resize(off + want);
      const uint64_t got = wav_.readChannel(ch_, buf_.data() + off, want);
      buf_.resize(off + got);
      if (got == 0) break;
    }
    if (static_cast<int64_t>(buf_.size() - head_) < need) {
      error = "program audio ends before the LTC range is covered";
      return false;
    }
    out.assign(buf_.begin() + static_cast<long>(head_),
               buf_.begin() + static_cast<long>(head_) + need);
    return true;
  }

private:
  WavReader wav_;
  int ch_ = 0;
  int64_t chunk_ = 4096;
  int64_t pos_ = 0;     // absolute sample index of buf_[0]
  size_t head_ = 0;     // consumed prefix of buf_
  std::vector<int16_t> buf_;
};

}  // namespace

bool decodeFile(const std::string& path, int channel, int chunkFrames,
                std::vector<LtcFrame>& frames, std::vector<Segment>& segments,
                int& channels, std::string& error) {
  WavReader wav;
  if (!wav.open(path, error)) return false;
  channels = wav.channels();
  if (channel < 0 || channel >= channels) {
    error = "timecode channel out of range for " + path;
    return false;
  }
  BiphaseDecoder decoder;
  FrameAssembler assembler;
  SegmentBuilder segBuilder;
  decoder.onBit = [&](const BitEvent& b) {
    assembler.pushBit(b.value, b.cellStart, b.cellEnd);
  };
  decoder.onGap = [&]() {
    assembler.reset();
    segBuilder.onBreak("signal_loss");
  };
  assembler.onFrame = [&](const LtcFrame& f) {
    frames.push_back(f);
    segBuilder.onFrame(f);
  };
  assembler.onBadFrame = [&]() { segBuilder.onBreak("invalid_frame"); };
  segBuilder.onSegment = [&](const Segment& s) { segments.push_back(s); };

  std::vector<int16_t> buf(static_cast<size_t>(chunkFrames));
  int64_t base = 0;
  uint64_t got;
  while ((got = wav.readChannel(channel, buf.data(), chunkFrames)) > 0) {
    for (uint64_t i = 0; i < got; ++i)
      decoder.pushSample(buf[i], base + static_cast<int64_t>(i));
    base += static_cast<int64_t>(got);
  }
  decoder.finish();
  segBuilder.finish();
  return true;
}

bool selectRange(const std::vector<LtcFrame>& frames,
                 const std::vector<Segment>& segments, int segmentIndex,
                 const Timecode& start, int64_t count, const LtcFrame*& first,
                 std::string& error) {
  if (count <= 0) {
    error = "frame count must be positive";
    return false;
  }
  if (count >= kFramesPerDay) {
    error = "range must be shorter than 24 hours";
    return false;
  }
  if (segmentIndex < 0 || segmentIndex >= static_cast<int>(segments.size())) {
    error = "segment " + std::to_string(segmentIndex) + " does not exist";
    return false;
  }
  int64_t offset = 0;
  for (int i = 0; i < segmentIndex; ++i) offset += segments[i].frames;
  const Segment& seg = segments[segmentIndex];
  const LtcFrame* segFrames = frames.data() + offset;

  int64_t startPos = -1;
  for (int64_t i = 0; i < seg.frames; ++i) {
    if (sameTc(segFrames[i].tc, start)) {
      if (startPos >= 0) {
        error = "start timecode " + toString(start) +
                " occurs more than once in segment " +
                std::to_string(segmentIndex);
        return false;
      }
      startPos = i;
    }
  }
  if (startPos < 0) {
    error = "start timecode " + toString(start) + " not found in segment " +
            std::to_string(segmentIndex);
    return false;
  }
  if (startPos + count > seg.frames) {
    error = "range of " + std::to_string(count) +
            " frames from " + toString(start) +
            " crosses a break in segment " + std::to_string(segmentIndex) +
            " (only " + std::to_string(seg.frames - startPos) +
            " frames available)";
    return false;
  }
  // Verify frame-by-frame continuity (25 fps, midnight wrap allowed).
  Timecode expect = start;
  for (int64_t i = 0; i < count; ++i) {
    if (!sameTc(segFrames[startPos + i].tc, expect)) {
      error = "segment " + std::to_string(segmentIndex) +
              " is not continuous over the requested range (missing " +
              toString(expect) + ")";
      return false;
    }
    expect = increment(expect);
  }
  first = segFrames + startPos;
  return true;
}

void resampleFrame(const int16_t* src, int64_t len, int16_t* out, int outLen) {
  for (int j = 0; j < outLen; ++j) {
    const double pos = static_cast<double>(j) * static_cast<double>(len) /
                       static_cast<double>(outLen);
    int64_t i0 = static_cast<int64_t>(pos);
    const double frac = pos - static_cast<double>(i0);
    double v;
    if (i0 + 1 >= len) {
      v = src[len - 1];  // never interpolate past the last source sample
    } else {
      v = src[i0] * (1.0 - frac) + src[i0 + 1] * frac;
    }
    out[j] = static_cast<int16_t>(std::lround(v));
  }
}

int runConform(const ConformOptions& opt, std::string& error) {
  if (opt.frames <= 0) {
    error = "frame count must be positive";
    return 1;
  }
  if (opt.frames >= kFramesPerDay) {
    error = "range must be shorter than 24 hours";
    return 1;
  }
  if (opt.outWav.empty()) {
    error = "missing output WAV path";
    return 1;
  }

  std::vector<LtcFrame> framesA, framesB;
  std::vector<Segment> segsA, segsB;
  int channelsA = 0, channelsB = 0;
  if (!decodeFile(opt.pathA, opt.channelA, opt.chunkFrames, framesA, segsA,
                  channelsA, error))
    return 1;
  if (!decodeFile(opt.pathB, opt.channelB, opt.chunkFrames, framesB, segsB,
                  channelsB, error))
    return 1;
  if (channelsA != 2 || channelsB != 2) {
    error = "both inputs must be stereo (timecode channel + program channel)";
    return 1;
  }

  const LtcFrame* rangeA = nullptr;
  const LtcFrame* rangeB = nullptr;
  if (!selectRange(framesA, segsA, opt.segmentA, opt.start, opt.frames, rangeA,
                   error)) {
    error = opt.pathA + ": " + error;
    return 1;
  }
  if (!selectRange(framesB, segsB, opt.segmentB, opt.start, opt.frames, rangeB,
                   error)) {
    error = opt.pathB + ": " + error;
    return 1;
  }

  ProgramCursor progA, progB;
  if (!progA.open(opt.pathA, 1 - opt.channelA, opt.chunkFrames, error))
    return 1;
  if (!progB.open(opt.pathB, 1 - opt.channelB, opt.chunkFrames, error))
    return 1;
  if (!progA.seek(rangeA[0].startSample, error)) return 1;
  if (!progB.seek(rangeB[0].startSample, error)) return 1;

  // Write to a temporary file first; only a fully rendered result is
  // renamed into place, so a failure never leaves a plausible WAV.
  const std::string tmpPath = opt.outWav + ".tmp";
  WavWriter writer;
  if (!writer.open(tmpPath, 2, error)) return 1;

  const int outFrame = kOutputSamplesPerFrame;
  std::vector<int16_t> srcA, srcB, outA(outFrame), outB(outFrame);
  std::vector<int16_t> interleaved;
  interleaved.reserve(static_cast<size_t>(outFrame) * 2 * 64);
  nlohmann::json map = nlohmann::json::array();
  bool ok = true;
  for (int64_t i = 0; ok && i < opt.frames; ++i) {
    const LtcFrame& fa = rangeA[i];
    const LtcFrame& fb = rangeB[i];
    if (!progA.readInterval(fa.startSample, fa.endSample, srcA, error) ||
        !progB.readInterval(fb.startSample, fb.endSample, srcB, error)) {
      ok = false;
      break;
    }
    resampleFrame(srcA.data(), fa.endSample - fa.startSample, outA.data(),
                  outFrame);
    resampleFrame(srcB.data(), fb.endSample - fb.startSample, outB.data(),
                  outFrame);
    for (int j = 0; j < outFrame; ++j) {
      interleaved.push_back(outA[j]);
      interleaved.push_back(outB[j]);
    }
    map.push_back({
        {"index", i},
        {"timecode", toString(fa.tc)},
        {"a", {{"sample_start", fa.startSample}, {"sample_end", fa.endSample}}},
        {"b", {{"sample_start", fb.startSample}, {"sample_end", fb.endSample}}},
    });
    if (interleaved.size() >= static_cast<size_t>(outFrame) * 2 * 64) {
      if (!writer.write(interleaved.data(),
                        interleaved.size() / 2)) {
        error = "failed writing " + tmpPath;
        ok = false;
        break;
      }
      interleaved.clear();
    }
  }
  if (ok && !interleaved.empty() &&
      !writer.write(interleaved.data(), interleaved.size() / 2)) {
    error = "failed writing " + tmpPath;
    ok = false;
  }
  if (ok && !writer.close()) {
    error = "failed finalizing " + tmpPath;
    ok = false;
  }
  if (!ok) {
    std::remove(tmpPath.c_str());
    return 1;
  }
  if (std::rename(tmpPath.c_str(), opt.outWav.c_str()) != 0) {
    error = "cannot rename " + tmpPath + " to " + opt.outWav;
    std::remove(tmpPath.c_str());
    return 1;
  }

  Timecode end = opt.start;
  for (int64_t i = 1; i < opt.frames; ++i) end = increment(end);
  nlohmann::json j;
  j["standard"] = "LTC 25fps non-drop, 80-bit SMPTE frame";
  j["start_timecode"] = toString(opt.start);
  j["end_timecode"] = toString(end);
  j["frames"] = opt.frames;
  j["output_samples"] = opt.frames * outFrame;
  j["sample_rate"] = kSampleRate;
  j["output"] = opt.outWav;
  j["inputs"] = {
      {"a",
       {{"file", opt.pathA},
        {"timecode_channel", opt.channelA},
        {"program_channel", 1 - opt.channelA},
        {"segment", opt.segmentA}}},
      {"b",
       {{"file", opt.pathB},
        {"timecode_channel", opt.channelB},
        {"program_channel", 1 - opt.channelB},
        {"segment", opt.segmentB}}},
  };
  j["frame_map"] = map;

  const std::string text = j.dump(2);
  if (opt.jsonPath.empty()) {
    std::fputs(text.c_str(), stdout);
    std::fputc('\n', stdout);
  } else {
    FILE* fp = std::fopen(opt.jsonPath.c_str(), "w");
    if (!fp) {
      error = "cannot write " + opt.jsonPath;
      return 1;
    }
    std::fputs(text.c_str(), fp);
    std::fputc('\n', fp);
    std::fclose(fp);
  }
  return 0;
}

}  // namespace ltc
