// Tests for the dual-recorder conform path: range selection, per-frame
// resampling, end-to-end delivery and failure behavior.
#include "../src/audio_reader.h"
#include "../src/conform.h"
#include "../src/ltc_frame.h"
#include "../src/segments.h"
#include "../tools/ltc_encoder.h"
#include "dr_wav.h"
#include "nlohmann/json.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& name) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  if (!ok) ++failures;
}

// Writes a stereo recorder file: LTC on ltcChannel, sine tone on the
// other channel (frequency scaled by speed, like a drifting recorder).
void writeMachine(const std::string& path, const ltcgen::Options& o,
                  int ltcChannel, double toneFreq) {
  const auto mono = ltcgen::encode(o);
  drwav_data_format fmt{};
  fmt.container = drwav_container_riff;
  fmt.format = DR_WAVE_FORMAT_PCM;
  fmt.channels = 2;
  fmt.sampleRate = 48000;
  fmt.bitsPerSample = 16;
  drwav wav;
  if (!drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr)) {
    std::fprintf(stderr, "cannot write %s\n", path.c_str());
    std::exit(1);
  }
  std::vector<int16_t> inter(mono.size() * 2, 0);
  const double fileFreq = toneFreq * o.speed;
  for (size_t i = 0; i < mono.size(); ++i) {
    inter[i * 2 + ltcChannel] = mono[i];
    const double t = static_cast<double>(i) / 48000.0;
    inter[i * 2 + (1 - ltcChannel)] =
        static_cast<int16_t>(std::lround(12000.0 * std::sin(2.0 * M_PI * fileFreq * t)));
  }
  drwav_write_pcm_frames(&wav, mono.size(), inter.data());
  drwav_uninit(&wav);
}

std::vector<int16_t> readChannel(const std::string& path, int ch) {
  ltc::WavReader r;
  std::string err;
  if (!r.open(path, err)) return {};
  std::vector<int16_t> out(r.totalFrames());
  out.resize(r.readChannel(ch, out.data(), r.totalFrames()));
  return out;
}

double toneFreq(const std::vector<int16_t>& x) {
  long crossings = 0;
  for (size_t i = 1; i < x.size(); ++i)
    if (x[i - 1] <= 0 && x[i] > 0) ++crossings;
  const double seconds = static_cast<double>(x.size()) / 48000.0;
  return crossings / seconds;
}

std::vector<ltc::LtcFrame> syntheticFrames(int n, ltc::Timecode start) {
  std::vector<ltc::LtcFrame> v;
  ltc::Timecode tc = start;
  int64_t pos = 0;
  for (int i = 0; i < n; ++i) {
    ltc::LtcFrame f;
    f.tc = tc;
    f.startSample = pos;
    f.endSample = pos + 1920;
    pos += 1920;
    v.push_back(f);
    tc = ltc::increment(tc);
  }
  return v;
}

void testSelectRange() {
  const auto frames = syntheticFrames(100, {1, 0, 0, 0});
  std::vector<ltc::Segment> segs(2);
  segs[0].frames = 60;
  segs[0].first = frames[0];
  segs[0].last = frames[59];
  segs[1].frames = 40;
  segs[1].first = frames[60];
  segs[1].last = frames[99];

  const ltc::LtcFrame* first = nullptr;
  std::string err;
  check(ltc::selectRange(frames, segs, 1, {1, 0, 2, 20}, 30, first, err) &&
        first == &frames[70],
        "select: range from second segment");
  check(!ltc::selectRange(frames, segs, 2, {1, 0, 0, 0}, 10, first, err),
        "select: missing segment rejected");
  check(!ltc::selectRange(frames, segs, 0, {9, 0, 0, 0}, 10, first, err),
        "select: unknown start timecode rejected");
  check(!ltc::selectRange(frames, segs, 1, {1, 0, 2, 20}, 31, first, err),
        "select: range crossing segment break rejected");
  check(!ltc::selectRange(frames, segs, 0, {1, 0, 0, 0}, 0, first, err),
        "select: zero frame count rejected");
  check(!ltc::selectRange(frames, segs, 0, {1, 0, 0, 0},
                          ltc::kFramesPerDay, first, err),
        "select: 24h-or-longer range rejected");

  // A segment containing the start timecode twice must be refused.
  auto dup = syntheticFrames(4, {2, 0, 0, 0});
  dup[2].tc = dup[0].tc;
  std::vector<ltc::Segment> oneSeg(1);
  oneSeg[0].frames = 4;
  check(!ltc::selectRange(dup, oneSeg, 0, {2, 0, 0, 0}, 1, first, err),
        "select: repeated start timecode rejected");

  // A gap inside the requested range must be refused.
  auto broken = syntheticFrames(10, {3, 0, 0, 0});
  broken[5].tc = ltc::increment(broken[5].tc);
  oneSeg[0].frames = 10;
  check(!ltc::selectRange(broken, oneSeg, 0, {3, 0, 0, 0}, 10, first, err),
        "select: discontinuous range rejected");

  // Midnight wrap is legal.
  const auto wrap = syntheticFrames(20, {23, 59, 59, 20});
  oneSeg[0].frames = 20;
  check(ltc::selectRange(wrap, oneSeg, 0, {23, 59, 59, 22}, 12, first, err) &&
        first == &wrap[2],
        "select: range across midnight accepted");
}

void testResampleFrame() {
  // Ramp source: exact positions must be hit, end must clamp.
  std::vector<int16_t> src(3840);
  for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<int16_t>(i);
  std::vector<int16_t> out(1920);
  ltc::resampleFrame(src.data(), 3840, out.data(), 1920);
  check(out[0] == 0 && out[1] == 2 && out[1919] == 3838,
        "resample: 2:1 ramp mapping");
  std::vector<int16_t> src2(1920);
  for (size_t i = 0; i < src2.size(); ++i) src2[i] = static_cast<int16_t>(i * 2);
  ltc::resampleFrame(src2.data(), 1920, out.data(), 1920);
  check(out[0] == 0 && out[1919] == 3838, "resample: identity mapping");
  // Last sample must never interpolate past the final source sample.
  std::vector<int16_t> src3(1919, 1000);
  src3[1918] = -1000;
  ltc::resampleFrame(src3.data(), 1919, out.data(), 1920);
  check(out[1919] == -1000, "resample: clamped at last source sample");
}

void testConformEndToEnd() {
  ltcgen::Options oa;
  oa.start = {10, 0, 0, 0};
  oa.frames = 100;
  oa.leadSilence = 4800;
  oa.speed = 1.0;
  ltcgen::Options ob = oa;
  ob.leadSilence = 9600;
  ob.speed = 1.01;  // second recorder runs with a constant clock offset
  writeMachine("test_machine_a.wav", oa, 1, 440.0);
  writeMachine("test_machine_b.wav", ob, 0, 550.0);

  ltc::ConformOptions opt;
  opt.pathA = "test_machine_a.wav";
  opt.pathB = "test_machine_b.wav";
  opt.channelA = 1;
  opt.channelB = 0;
  opt.segmentA = 0;
  opt.segmentB = 0;
  ltc::parseTimecode("10:00:00:05", opt.start);
  opt.frames = 50;
  opt.outWav = "test_conform_out.wav";
  opt.jsonPath = "test_conform.json";

  std::string err;
  check(ltc::runConform(opt, err) == 0, "conform: dual delivery succeeds");

  ltc::WavReader out;
  check(out.open("test_conform_out.wav", err) && out.channels() == 2 &&
        out.sampleRate() == 48000 &&
        out.totalFrames() == 50 * 1920,
        "conform: output is 48k stereo with 1920 samples per frame");

  const auto left = readChannel("test_conform_out.wav", 0);
  const auto right = readChannel("test_conform_out.wav", 1);
  check(std::fabs(toneFreq(left) - 440.0) < 3.0,
        "conform: left channel carries A program audio");
  check(std::fabs(toneFreq(right) - 550.0) < 3.0,
        "conform: right channel carries B program audio (speed corrected)");

  std::ifstream in("test_conform.json");
  std::stringstream ss;
  ss << in.rdbuf();
  const auto j = nlohmann::json::parse(ss.str());
  check(j["frames"] == 50 && j["output_samples"] == 50 * 1920 &&
        j["start_timecode"] == "10:00:00:05" &&
        j["end_timecode"] == "10:00:02:04" &&
        j["frame_map"].size() == 50 &&
        j["frame_map"][0]["a"]["sample_start"].get<int64_t>() >= 4800 &&
        j["frame_map"][0]["b"]["sample_start"].get<int64_t>() >= 9600,
        "conform: JSON handoff lists range, counts and per-frame intervals");

  // Failure: start timecode absent from B -> no output WAV left behind.
  std::remove("test_conform_out.wav");
  ltc::parseTimecode("11:00:00:00", opt.start);
  check(ltc::runConform(opt, err) != 0 &&
        std::ifstream("test_conform_out.wav").good() == false,
        "conform: failure leaves no output WAV");

  std::remove("test_machine_a.wav");
  std::remove("test_machine_b.wav");
  std::remove("test_conform_out.wav");
  std::remove("test_conform_out.wav.tmp");
  std::remove("test_conform.json");
}

}  // namespace

int main() {
  testSelectRange();
  testResampleFrame();
  testConformEndToEnd();
  std::printf(failures ? "FAILED: %d\n" : "ALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
