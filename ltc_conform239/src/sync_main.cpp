// ltc_conform239_sync: delivers the program channels of two LTC-stamped
// recorder WAVs as one synchronized 48 kHz PCM16 stereo WAV plus a JSON
// handoff record. The original scanner (ltc_conform239) is untouched.
#include "conform.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s --a A.wav --a-channel 0|1 --a-segment N "
               "--b B.wav --b-channel 0|1 --b-segment N "
               "--start HH:MM:SS:FF --frames N --out out.wav "
               "[--json handoff.json] [--chunk N]\n",
               argv0);
}

bool parseTc(const std::string& s, ltc::Timecode& tc) {
  return std::sscanf(s.c_str(), "%d:%d:%d:%d", &tc.h, &tc.m, &tc.s, &tc.f) ==
             4 &&
         tc.h >= 0 && tc.h <= 23 && tc.m >= 0 && tc.m <= 59 && tc.s >= 0 &&
         tc.s <= 59 && tc.f >= 0 && tc.f <= 24;
}

}  // namespace

int main(int argc, char** argv) {
  ltc::SyncRequest req;
  bool haveA = false, haveB = false, haveStart = false, haveFrames = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--a") { req.pathA = next("--a"); haveA = true; }
    else if (a == "--a-channel") req.tcChannelA = std::atoi(next("--a-channel").c_str());
    else if (a == "--a-segment") req.segmentA = std::atoi(next("--a-segment").c_str());
    else if (a == "--b") { req.pathB = next("--b"); haveB = true; }
    else if (a == "--b-channel") req.tcChannelB = std::atoi(next("--b-channel").c_str());
    else if (a == "--b-segment") req.segmentB = std::atoi(next("--b-segment").c_str());
    else if (a == "--start") {
      if (!parseTc(next("--start"), req.start)) {
        std::fprintf(stderr, "bad --start timecode\n");
        return 2;
      }
      haveStart = true;
    }
    else if (a == "--frames") { req.frames = std::atoi(next("--frames").c_str()); haveFrames = true; }
    else if (a == "--out") req.outWav = next("--out");
    else if (a == "--json") req.jsonPath = next("--json");
    else if (a == "--chunk") req.chunkFrames = std::atoi(next("--chunk").c_str());
    else { usage(argv[0]); return 2; }
  }
  if (!haveA || !haveB || !haveStart || !haveFrames || req.outWav.empty() ||
      req.tcChannelA < 0 || req.tcChannelA > 1 || req.tcChannelB < 0 ||
      req.tcChannelB > 1 || req.chunkFrames <= 0) {
    usage(argv[0]);
    return 2;
  }

  std::string error;
  if (!ltc::conformSync(req, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  return 0;
}

