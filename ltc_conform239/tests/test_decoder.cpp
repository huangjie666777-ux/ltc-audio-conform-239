// Unit/integration tests for the LTC decoder pipeline. Encodes synthetic
// LTC in memory (tools/ltc_encoder.h) and verifies decoding invariants.
#include "../src/audio_reader.h"
#include "../src/biphase.h"
#include "../src/ltc_frame.h"
#include "../src/segments.h"
#include "../tools/ltc_encoder.h"
#include "dr_wav.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& name) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  if (!ok) ++failures;
}

struct DecodeResult {
  std::vector<ltc::LtcFrame> frames;
  std::vector<ltc::Segment> segments;
};

DecodeResult decode(const std::vector<int16_t>& samples, size_t chunk) {
  DecodeResult r;
  ltc::BiphaseDecoder dec;
  ltc::FrameAssembler asm_;
  ltc::SegmentBuilder seg;
  dec.onBit = [&](const ltc::BitEvent& b) { asm_.pushBit(b.value, b.cellStart, b.cellEnd); };
  dec.onGap = [&]() { asm_.reset(); seg.onBreak("signal_loss"); };
  asm_.onFrame = [&](const ltc::LtcFrame& f) { r.frames.push_back(f); seg.onFrame(f); };
  asm_.onBadFrame = [&]() { seg.onBreak("invalid_frame"); };
  seg.onSegment = [&](const ltc::Segment& s) { r.segments.push_back(s); };
  for (size_t i = 0; i < samples.size(); i += chunk) {
    const size_t n = std::min(chunk, samples.size() - i);
    for (size_t j = 0; j < n; ++j) dec.pushSample(samples[i + j], static_cast<int64_t>(i + j));
  }
  dec.finish();
  seg.finish();
  return r;
}

bool tcEq(const ltc::Timecode& t, int h, int m, int s, int f) {
  return t.h == h && t.m == m && t.s == s && t.f == f;
}

void testBasic() {
  ltcgen::Options o; o.start = {1, 2, 3, 4}; o.frames = 100;
  const auto r = decode(ltcgen::encode(o), 4096);
  check(r.frames.size() == 100, "basic: frame count");
  check(tcEq(r.frames.front().tc, 1, 2, 3, 4), "basic: first tc");
  check(tcEq(r.frames.back().tc, 1, 2, 7, 3), "basic: last tc");
  check(r.segments.size() == 1 && r.segments[0].frames == 100 &&
        r.segments[0].endReason == "end_of_stream", "basic: one segment");
  const int64_t len = r.frames[0].endSample - r.frames[0].startSample;
  check(std::abs(len - 1920) <= 2, "basic: frame spans 80*24 samples");
  bool monotonic = true;
  for (size_t i = 1; i < r.frames.size(); ++i)
    if (r.frames[i].startSample < r.frames[i - 1].endSample - 2) monotonic = false;
  check(monotonic, "basic: monotonic positions");
}

void testPolarity() {
  ltcgen::Options o; o.start = {10, 0, 0, 0}; o.frames = 50; o.polarity = -1;
  const auto r = decode(ltcgen::encode(o), 4096);
  check(r.frames.size() == 50 && tcEq(r.frames.front().tc, 10, 0, 0, 0),
        "polarity: inverted signal decodes");
}

void testSpeed() {
  for (double speed : {0.98, 1.02}) {
    ltcgen::Options o; o.start = {2, 0, 0, 0}; o.frames = 150; o.speed = speed;
    const auto r = decode(ltcgen::encode(o), 4096);
    check(r.frames.size() == 150 && tcEq(r.frames.back().tc, 2, 0, 5, 24),
          "speed: decodes at " + std::to_string(speed));
  }
}

void testMidFrameAndSilence() {
  ltcgen::Options o;
  o.start = {3, 0, 0, 0}; o.frames = 60; o.leadSilence = 9600;
  o.startMidFrame = true; o.tailSilence = 4800;
  const auto r = decode(ltcgen::encode(o), 4096);
  check(r.frames.size() == 59, "mid-frame: partial head frame dropped");
  check(tcEq(r.frames.front().tc, 3, 0, 0, 1), "mid-frame: first full frame");
  check(r.frames.front().startSample >= 9600, "mid-frame: starts after silence");
  check(r.segments.size() == 1, "mid-frame: single segment");
}

void testChunkInvariance() {
  ltcgen::Options o; o.start = {4, 5, 6, 7}; o.frames = 80; o.speed = 1.01;
  const auto samples = ltcgen::encode(o);
  const auto ref = decode(samples, 100000);
  for (size_t chunk : {1, 7, 64, 4096}) {
    const auto r = decode(samples, chunk);
    bool same = r.frames.size() == ref.frames.size();
    for (size_t i = 0; same && i < ref.frames.size(); ++i)
      same = r.frames[i].startSample == ref.frames[i].startSample &&
             r.frames[i].endSample == ref.frames[i].endSample &&
             tcEq(r.frames[i].tc, ref.frames[i].tc.h, ref.frames[i].tc.m,
                  ref.frames[i].tc.s, ref.frames[i].tc.f);
    check(same, "chunk invariance: chunk=" + std::to_string(chunk));
  }
}

void testDropFlagRejected() {
  ltcgen::Options o; o.start = {1, 0, 0, 0}; o.frames = 40; o.dropFrameFlag = true;
  const auto r = decode(ltcgen::encode(o), 4096);
  check(r.frames.empty() && r.segments.empty(), "drop-frame flag: nothing delivered");
}

void testBadFrameBreaksSegment() {
  ltcgen::Options o; o.start = {1, 0, 0, 0}; o.frames = 60; o.badAt = 30;
  const auto r = decode(ltcgen::encode(o), 4096);
  check(r.frames.size() == 59, "bad frame: one frame not delivered");
  check(r.segments.size() == 2 && r.segments[0].endReason == "invalid_frame" &&
        r.segments[0].frames == 30 && r.segments[1].frames == 29,
        "bad frame: segment split");
}

void testMidnightWrap() {
  ltcgen::Options o; o.start = {23, 59, 59, 20}; o.frames = 20;
  const auto r = decode(ltcgen::encode(o), 4096);
  check(r.frames.size() == 20 && r.segments.size() == 1 &&
        tcEq(r.frames.back().tc, 0, 0, 0, 14), "midnight wrap: single segment");
}

void testTimecodeJump() {
  ltcgen::Options o;
  o.start = {1, 0, 0, 0}; o.frames = 60; o.jumpAt = 30; o.jumpTo = {5, 0, 0, 0};
  const auto r = decode(ltcgen::encode(o), 4096);
  check(r.segments.size() == 2 && r.segments[0].endReason == "timecode_jump" &&
        tcEq(r.segments[1].first.tc, 5, 0, 0, 0), "jump: segment split");
}

void testGap() {
  ltcgen::Options o;
  o.start = {1, 0, 0, 0}; o.frames = 60; o.gapAt = 30; o.gapSamples = 4800;
  const auto r = decode(ltcgen::encode(o), 4096);
  check(r.segments.size() == 2 && r.segments[0].endReason == "signal_loss",
        "gap: segment ends with signal_loss");
}

void testWavStereo() {
  ltcgen::Options o; o.start = {7, 8, 9, 10}; o.frames = 50;
  const auto mono = ltcgen::encode(o);
  std::vector<int16_t> inter(mono.size() * 2, 0);
  for (size_t i = 0; i < mono.size(); ++i) inter[i * 2 + 1] = mono[i];
  drwav_data_format fmt{};
  fmt.container = drwav_container_riff;
  fmt.format = DR_WAVE_FORMAT_PCM;
  fmt.channels = 2; fmt.sampleRate = 48000; fmt.bitsPerSample = 16;
  drwav wav;
  check(drwav_init_file_write(&wav, "test_stereo.wav", &fmt, nullptr) == DRWAV_TRUE,
        "wav: write stereo");
  drwav_write_pcm_frames(&wav, mono.size(), inter.data());
  drwav_uninit(&wav);

  ltc::WavReader reader;
  std::string err;
  check(reader.open("test_stereo.wav", err) && reader.channels() == 2, "wav: open");
  std::vector<int16_t> buf(mono.size());
  const uint64_t got = reader.readChannel(1, buf.data(), mono.size());
  check(got == mono.size() && buf == mono, "wav: channel 1 readback");
  const auto r = decode(buf, 4096);
  check(r.frames.size() == 50 && tcEq(r.frames.front().tc, 7, 8, 9, 10),
        "wav: decode from channel 1");
  std::vector<int16_t> silent(mono.size());
  reader.seekToStart();
  reader.readChannel(0, silent.data(), mono.size());
  check(decode(silent, 4096).frames.empty(), "wav: silent channel has no frames");
  std::remove("test_stereo.wav");
}

}  // namespace

int main() {
  testBasic();
  testPolarity();
  testSpeed();
  testMidFrameAndSilence();
  testChunkInvariance();
  testDropFlagRejected();
  testBadFrameBreaksSegment();
  testMidnightWrap();
  testTimecodeJump();
  testGap();
  testWavStereo();
  std::printf(failures ? "FAILED: %d\n" : "ALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
