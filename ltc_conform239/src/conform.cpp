// Dual-recorder conform: aligns the program channels of two 48 kHz PCM16
// stereo WAV recorders onto a common 25 fps LTC timeline. Reuses the
// decoder's real per-frame codeword positions; never modifies the sources.
#include "conform.h"

#include "audio_reader.h"
#include "biphase.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dr_wav.h"
#include "nlohmann/json.hpp"

namespace ltc {

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr int kSamplesPerFrame = 1920;  // 48000 / 25
constexpr int64_t kMaxRangeFrames = 25LL * 24 * 3600;  // 24 h at 25 fps

bool sameTc(const Timecode& a, const Timecode& b) {
  return a.h == b.h && a.m == b.m && a.s == b.s && a.f == b.f;
}

// Locates the requested continuous segment and the in-segment offset of
// the start timecode. The segment frames are frames[firstFrame, ...
// firstFrame + frames) of the decoded stream by construction.
bool selectRange(const DecodedFile& dec, int segmentIndex, const Timecode& start,
                 int frames, const std::string& tag, int64_t& offsetOut,
                 std::string& error) {
  if (segmentIndex < 0 || segmentIndex >= static_cast<int>(dec.segments.size())) {
    error = tag + ": segment " + std::to_string(segmentIndex) +
            " does not exist (" + std::to_string(dec.segments.size()) +
            " segment(s) found)";
    return false;
  }
  const Segment& seg = dec.segments[segmentIndex];
  int hits = 0;
  int64_t offset = -1;
  for (int i = 0; i < seg.frames; ++i) {
    if (sameTc(dec.frames[seg.firstFrame + i].tc, start)) {
      ++hits;
      offset = i;
    }
  }
  if (hits == 0) {
    error = tag + ": start timecode " + toString(start) +
            " not found in segment " + std::to_string(segmentIndex);
    return false;
  }
  if (hits > 1) {
    error = tag + ": start timecode " + toString(start) +
            " occurs " + std::to_string(hits) + " times in segment " +
            std::to_string(segmentIndex) + "; refusing to guess";
    return false;
  }
  if (offset + frames > seg.frames) {
    error = tag + ": range of " + std::to_string(frames) +
            " frame(s) from " + toString(start) +
            " runs past the end of segment " + std::to_string(segmentIndex) +
            " (break: " + seg.endReason + "); missing frames are not "
            "spliced or padded";
    return false;
  }
  offsetOut = seg.firstFrame + offset;
  return true;
}

// Reads one recorder's program channel window per output frame. Windows
// are monotonic, so the file is streamed forward in small chunks and the
// whole recording is never held in memory.
class ProgramCursor {
public:
  bool open(const std::string& path, int channel, std::string& error) {
    if (!wav_.open(path, error)) return false;
    channel_ = channel;
    return true;
  }

  bool readWindow(int64_t start, int64_t len, std::string& error) {
    if (start < 0 || len < 2) {
      error = "invalid source window";
      return false;
    }
    buf_.resize(static_cast<size_t>(len));
    if (!wav_.seekToFrame(static_cast<uint64_t>(start))) {
      error = "cannot seek to sample " + std::to_string(start);
      return false;
    }
    const uint64_t got =
        wav_.readChannel(channel_, buf_.data(), static_cast<uint64_t>(len));
    if (got != static_cast<uint64_t>(len)) {
      error = "short read at sample " + std::to_string(start);
      return false;
    }
    return true;
  }

  // Linear interpolation at fractional offset pos (in source samples from
  // the window start); never reads past the last source sample.
  int16_t at(double pos) const {
    const int64_t last = static_cast<int64_t>(buf_.size()) - 1;
    int64_t i0 = static_cast<int64_t>(std::floor(pos));
    if (i0 < 0) i0 = 0;
    if (i0 > last) i0 = last;
    const int64_t i1 = (i0 < last) ? i0 + 1 : last;
    const double frac = pos - std::floor(pos);
    const double a = buf_[static_cast<size_t>(i0)];
    const double b = buf_[static_cast<size_t>(i1)];
    long v = std::lround(a + (b - a) * frac);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return static_cast<int16_t>(v);
  }

private:
  WavReader wav_;
  int channel_ = 0;
  std::vector<int16_t> buf_;
};

}  // namespace

bool decodeFile(const std::string& path, int channel, int chunkFrames,
                DecodedFile& out, std::string& error) {
  WavReader wav;
  if (!wav.open(path, error)) return false;
  if (channel < 0 || channel >= wav.channels()) {
    error = "timecode channel " + std::to_string(channel) +
            " out of range for " + path;
    return false;
  }
  out.channels = wav.channels();

  BiphaseDecoder decoder;
  FrameAssembler assembler;
  SegmentBuilder segments;
  decoder.onBit = [&](const BitEvent& b) {
    assembler.pushBit(b.value, b.cellStart, b.cellEnd);
  };
  decoder.onGap = [&]() {
    assembler.reset();
    segments.onBreak("signal_loss");
  };
  assembler.onFrame = [&](const LtcFrame& f) {
    out.frames.push_back(f);
    segments.onFrame(f);
  };
  assembler.onBadFrame = [&]() { segments.onBreak("invalid_frame"); };
  segments.onSegment = [&](const Segment& s) { out.segments.push_back(s); };

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
  return true;
}

bool conformSync(const SyncRequest& req, std::string& error) {
  if (req.frames <= 0) {
    error = "frame count must be positive";
    return false;
  }
  if (req.frames >= kMaxRangeFrames) {
    error = "frame count must be less than 24 hours (2160000 frames)";
    return false;
  }
  if (req.outWav.empty()) {
    error = "output WAV path is required";
    return false;
  }

  DecodedFile decA, decB;
  if (!decodeFile(req.pathA, req.tcChannelA, req.chunkFrames, decA, error)) {
    error = "A: " + error;
    return false;
  }
  if (!decodeFile(req.pathB, req.tcChannelB, req.chunkFrames, decB, error)) {
    error = "B: " + error;
    return false;
  }
  if (decA.channels != 2 || decB.channels != 2) {
    error = "both inputs must be stereo (timecode channel + program channel)";
    return false;
  }

  int64_t offA = 0, offB = 0;
  if (!selectRange(decA, req.segmentA, req.start, req.frames, "A", offA, error))
    return false;
  if (!selectRange(decB, req.segmentB, req.start, req.frames, "B", offB, error))
    return false;

  ProgramCursor progA, progB;
  if (!progA.open(req.pathA, 1 - req.tcChannelA, error)) {
    error = "A: " + error;
    return false;
  }
  if (!progB.open(req.pathB, 1 - req.tcChannelB, error)) {
    error = "B: " + error;
    return false;
  }

  // Write to a temporary name and rename on success, so a failed run never
  // leaves a WAV that looks complete.
  const std::string tmpPath = req.outWav + ".part";
  drwav_data_format fmt{};
  fmt.container = drwav_container_riff;
  fmt.format = DR_WAVE_FORMAT_PCM;
  fmt.channels = 2;
  fmt.sampleRate = kSampleRate;
  fmt.bitsPerSample = 16;
  drwav out;
  if (!drwav_init_file_write(&out, tmpPath.c_str(), &fmt, nullptr)) {
    error = "cannot write " + tmpPath;
    return false;
  }

  nlohmann::json jframes = nlohmann::json::array();
  std::vector<int16_t> interleaved(kSamplesPerFrame * 2);
  bool ok = true;
  for (int i = 0; ok && i < req.frames; ++i) {
    const LtcFrame& fa = decA.frames[offA + i];
    const LtcFrame& fb = decB.frames[offB + i];
    const int64_t lenA = fa.endSample - fa.startSample;
    const int64_t lenB = fb.endSample - fb.startSample;
    if (!progA.readWindow(fa.startSample, lenA, error) ||
        !progB.readWindow(fb.startSample, lenB, error)) {
      ok = false;
      break;
    }
    for (int jdx = 0; jdx < kSamplesPerFrame; ++jdx) {
      // Output sample j maps to source start + j * len / 1920, per frame,
      // which cancels each recorder's constant speed deviation.
      const double posA = static_cast<double>(jdx) * lenA / kSamplesPerFrame;
      const double posB = static_cast<double>(jdx) * lenB / kSamplesPerFrame;
      interleaved[jdx * 2] = progA.at(posA);
      interleaved[jdx * 2 + 1] = progB.at(posB);
    }
    if (drwav_write_pcm_frames(&out, kSamplesPerFrame, interleaved.data()) !=
        static_cast<drwav_uint64>(kSamplesPerFrame)) {
      error = "failed writing output WAV";
      ok = false;
      break;
    }
    jframes.push_back({
        {"timecode", toString(fa.tc)},
        {"left", {fa.startSample, fa.endSample}},
        {"right", {fb.startSample, fb.endSample}},
    });
  }
  drwav_uninit(&out);
  if (!ok) {
    std::remove(tmpPath.c_str());
    return false;
  }
  if (std::rename(tmpPath.c_str(), req.outWav.c_str()) != 0) {
    std::remove(tmpPath.c_str());
    error = "cannot rename " + tmpPath + " to " + req.outWav;
    return false;
  }

  const Timecode last = decA.frames[offA + req.frames - 1].tc;
  nlohmann::json j;
  j["standard"] = "LTC 25fps non-drop, 48 kHz PCM16 stereo";
  j["start_timecode"] = toString(req.start);
  j["end_timecode"] = toString(last);
  j["frames"] = req.frames;
  j["output_samples"] = static_cast<int64_t>(req.frames) * kSamplesPerFrame;
  j["samples_per_frame"] = kSamplesPerFrame;
  j["output_wav"] = req.outWav;
  j["left"] = {{"file", req.pathA},
               {"timecode_channel", req.tcChannelA},
               {"program_channel", 1 - req.tcChannelA},
               {"segment", req.segmentA}};
  j["right"] = {{"file", req.pathB},
                {"timecode_channel", req.tcChannelB},
                {"program_channel", 1 - req.tcChannelB},
                {"segment", req.segmentB}};
  j["frame_map"] = jframes;

  const std::string text = j.dump(2);
  if (req.jsonPath.empty()) {
    std::fputs(text.c_str(), stdout);
    std::fputc('\n', stdout);
  } else {
    FILE* fp = std::fopen(req.jsonPath.c_str(), "w");
    if (!fp) {
      error = "cannot write " + req.jsonPath;
      return false;
    }
    std::fputs(text.c_str(), fp);
    std::fputc('\n', fp);
    std::fclose(fp);
  }
  return true;
}

}  // namespace ltc
