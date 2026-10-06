// ltc_gen: writes a reproducible 48 kHz PCM16 WAV containing synthetic
// LTC, for demos and tests. Mono or stereo (LTC on a chosen channel,
// the other channel silent).
#include "ltc_encoder.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
ltc::Timecode parseTc(const std::string& s) {
  ltc::Timecode tc;
  if (std::sscanf(s.c_str(), "%d:%d:%d:%d", &tc.h, &tc.m, &tc.s, &tc.f) != 4) {
    std::fprintf(stderr, "bad timecode: %s\n", s.c_str());
    std::exit(2);
  }
  return tc;
}
}  // namespace

int main(int argc, char** argv) {
  ltcgen::Options o;
  std::string out = "example.wav";
  int channels = 1, ltcChannel = 0;
  double programFreq = 0.0;   // sine tone on the non-LTC channel (Hz)
  int programAmp = 12000;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() { return std::string(argv[++i]); };
    if (a == "--out") out = next();
    else if (a == "--start") o.start = parseTc(next());
    else if (a == "--frames") o.frames = std::atoi(next().c_str());
    else if (a == "--speed") o.speed = std::atof(next().c_str());
    else if (a == "--invert") o.polarity = -1;
    else if (a == "--lead-silence") o.leadSilence = std::atoi(next().c_str());
    else if (a == "--tail-silence") o.tailSilence = std::atoi(next().c_str());
    else if (a == "--mid-frame") o.startMidFrame = true;
    else if (a == "--drop-flag") o.dropFrameFlag = true;
    else if (a == "--jump-at") o.jumpAt = std::atoi(next().c_str());
    else if (a == "--jump-to") o.jumpTo = parseTc(next());
    else if (a == "--gap-at") o.gapAt = std::atoi(next().c_str());
    else if (a == "--gap-samples") o.gapSamples = std::atoi(next().c_str());
    else if (a == "--bad-at") o.badAt = std::atoi(next().c_str());
    else if (a == "--channels") channels = std::atoi(next().c_str());
    else if (a == "--channel") ltcChannel = std::atoi(next().c_str());
    else if (a == "--program-freq") programFreq = std::atof(next().c_str());
    else if (a == "--program-amp") programAmp = std::atoi(next().c_str());
    else { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); return 2; }
  }

  const auto mono = ltcgen::encode(o);
  drwav_data_format fmt{};
  fmt.container = drwav_container_riff;
  fmt.format = DR_WAVE_FORMAT_PCM;
  fmt.channels = static_cast<drwav_uint32>(channels);
  fmt.sampleRate = 48000;
  fmt.bitsPerSample = 16;
  drwav wav;
  if (!drwav_init_file_write(&wav, out.c_str(), &fmt, nullptr)) {
    std::fprintf(stderr, "cannot write %s\n", out.c_str());
    return 1;
  }
  std::vector<int16_t> inter(mono.size() * channels, 0);
  for (size_t i = 0; i < mono.size(); ++i) inter[i * channels + ltcChannel] = mono[i];
  if (programFreq > 0.0 && channels == 2) {
    const int progCh = 1 - ltcChannel;
    // The tone models a programFreq source captured by a recorder whose
    // clock deviates by o.speed, so it drifts together with the LTC track.
    const double fileFreq = programFreq * o.speed;
    for (size_t i = 0; i < mono.size(); ++i) {
      const double t = static_cast<double>(i) / 48000.0;
      inter[i * channels + progCh] = static_cast<int16_t>(
          std::lround(programAmp * std::sin(2.0 * M_PI * fileFreq * t)));
    }
  }
  drwav_write_pcm_frames(&wav, mono.size(), inter.data());
  drwav_uninit(&wav);
  std::printf("wrote %s (%zu samples, %d ch, LTC on ch %d)\n", out.c_str(),
              mono.size(), channels, ltcChannel);
  return 0;
}
