// Integration tests for the dual-recorder conform path (src/conform.*).
// Builds two reproducible recorder WAVs (different speed, channel layout
// and program tone) and verifies synchronized delivery and refusals.
#include "../src/audio_reader.h"
#include "../src/conform.h"
#include "../src/ltc_frame.h"
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

bool fileExists(const std::string& path) {
  std::ifstream f(path);
  return f.good();
}

// Writes a stereo recorder WAV: LTC on ltcChannel, sine tone on the other.
void writeRecorder(const std::string& path, const ltcgen::Options& o,
                   int ltcChannel, double toneFreq) {
  const auto mono = ltcgen::encode(o);
  std::vector<int16_t> inter(mono.size() * 2, 0);
  const int prog = 1 - ltcChannel;
  for (size_t i = 0; i < mono.size(); ++i) {
    inter[i * 2 + ltcChannel] = mono[i];
    const double t = static_cast<double>(i) / 48000.0;
    inter[i * 2 + prog] = static_cast<int16_t>(
        std::lround(12000.0 * std::sin(2.0 * 3.14159265358979323846 *
                                       toneFreq * t)));
  }
  drwav_data_format fmt{};
  fmt.container = drwav_container_riff;
  fmt.format = DR_WAVE_FORMAT_PCM;
  fmt.channels = 2;
  fmt.sampleRate = 48000;
  fmt.bitsPerSample = 16;
  drwav wav;
  drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr);
  drwav_write_pcm_frames(&wav, mono.size(), inter.data());
  drwav_uninit(&wav);
}

ltc::SyncRequest baseRequest() {
  ltc::SyncRequest req;
  req.pathA = "sync_a.wav";
  req.tcChannelA = 1;
  req.segmentA = 0;
  req.pathB = "sync_b.wav";
  req.tcChannelB = 0;
  req.segmentB = 0;
  req.start = {1, 0, 0, 10};
  req.frames = 200;
  req.outWav = "sync_out.wav";
  req.jsonPath = "sync_out.json";
  return req;
}

void cleanup() {
  for (const char* p : {"sync_a.wav", "sync_b.wav", "sync_out.wav",
                        "sync_out.json", "sync_out.wav.part"})
    std::remove(p);
}

}  // namespace

int main() {
  ltcgen::Options oa;
  oa.start = {1, 0, 0, 0};
  oa.frames = 300;
  oa.speed = 1.0;
  oa.leadSilence = 4800;
  oa.tailSilence = 4800;
  writeRecorder("sync_a.wav", oa, 1, 440.0);

  ltcgen::Options ob;
  ob.start = {1, 0, 0, 0};
  ob.frames = 300;
  ob.speed = 1.01;  // constant speed deviation the conform must cancel
  ob.leadSilence = 2400;
  writeRecorder("sync_b.wav", ob, 0, 550.0);

  // Happy path.
  {
    ltc::SyncRequest req = baseRequest();
    std::string error;
    check(ltc::conformSync(req, error), "sync: conforms (" + error + ")");

    ltc::WavReader out;
    std::string oerr;
    check(out.open("sync_out.wav", oerr) && out.channels() == 2 &&
              out.totalFrames() == 200ULL * 1920,
          "sync: output is stereo 200*1920 samples");

    std::ifstream js("sync_out.json");
    std::stringstream ss;
    ss << js.rdbuf();
    const auto j = nlohmann::json::parse(ss.str());
    check(j["frames"] == 200 && j["output_samples"] == 200 * 1920 &&
              j["start_timecode"] == "01:00:00:10" &&
              j["end_timecode"] == "01:00:08:09" &&
              j["frame_map"].size() == 200,
          "sync: handoff JSON summary");
    check(j["left"]["program_channel"] == 0 &&
              j["right"]["program_channel"] == 1,
          "sync: program channels recorded");

    // Per-frame source intervals must match the decoder's real positions.
    ltc::DecodedFile decA, decB;
    std::string derr;
    ltc::decodeFile("sync_a.wav", 1, 4096, decA, derr);
    ltc::decodeFile("sync_b.wav", 0, 4096, decB, derr);
    bool intervalsOk = decA.frames.size() >= 210 && decB.frames.size() >= 210;
    for (int i = 0; intervalsOk && i < 200; ++i) {
      const auto& m = j["frame_map"][i];
      intervalsOk = m["left"][0] == decA.frames[10 + i].startSample &&
                    m["left"][1] == decA.frames[10 + i].endSample &&
                    m["right"][0] == decB.frames[10 + i].startSample &&
                    m["right"][1] == decB.frames[10 + i].endSample;
    }
    check(intervalsOk, "sync: frame_map matches real codeword intervals");

    // Output sample j=0 of frame i must equal the program sample at the
    // codeword start; both program channels must be non-silent.
    ltc::WavReader srcA;
    std::string serr;
    srcA.open("sync_a.wav", serr);
    std::vector<int16_t> prog(1);
    srcA.seekToFrame(static_cast<uint64_t>(decA.frames[10].startSample));
    srcA.readChannel(0, prog.data(), 1);
    std::vector<int16_t> outBuf(2);
    out.seekToFrame(0);
    out.readChannel(0, outBuf.data(), 1);
    check(outBuf[0] == prog[0], "sync: frame start maps to codeword start");

    std::vector<int16_t> buf(1920);
    out.seekToFrame(0);
    out.readChannel(0, buf.data(), 1920);
    double peakL = 0;
    for (int16_t v : buf) peakL = std::max(peakL, std::abs((double)v));
    out.seekToFrame(0);
    out.readChannel(1, buf.data(), 1920);
    double peakR = 0;
    for (int16_t v : buf) peakR = std::max(peakR, std::abs((double)v));
    check(peakL > 8000 && peakR > 8000, "sync: both program channels live");
  }

  // Refusals must leave no output WAV behind.
  std::remove("sync_out.wav");
  {
    ltc::SyncRequest req = baseRequest();
    req.start = {2, 0, 0, 0};  // not in the segment
    std::string error;
    check(!ltc::conformSync(req, error) && !fileExists("sync_out.wav") &&
              !fileExists("sync_out.wav.part"),
          "sync: refuses unknown start timecode");
  }
  {
    ltc::SyncRequest req = baseRequest();
    req.frames = 1000;  // runs past the segment end
    std::string error;
    check(!ltc::conformSync(req, error) && !fileExists("sync_out.wav"),
          "sync: refuses range crossing the break");
  }
  {
    ltc::SyncRequest req = baseRequest();
    req.segmentB = 3;  // no such segment
    std::string error;
    check(!ltc::conformSync(req, error) && !fileExists("sync_out.wav"),
          "sync: refuses missing segment");
  }
  {
    ltc::SyncRequest req = baseRequest();
    req.frames = 0;
    std::string error;
    check(!ltc::conformSync(req, error), "sync: refuses zero frame count");
  }

  cleanup();
  std::printf(failures ? "FAILED: %d\n" : "ALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
