#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ltc {

// Chunked reader for 48 kHz, 16-bit PCM, mono/stereo WAV files.
// Only the selected channel is deinterleaved; the file is never modified.
class WavReader {
public:
  bool open(const std::string& path, std::string& error);
  int channels() const { return channels_; }
  uint32_t sampleRate() const { return sampleRate_; }
  uint64_t totalFrames() const { return totalFrames_; }

  // Reads up to n frames of channel ch into out; returns frames read.
  uint64_t readChannel(int ch, int16_t* out, uint64_t n);
  bool seekToStart();
  bool seekToFrame(uint64_t frameIndex);

private:
  std::string path_;
  int channels_ = 0;
  uint32_t sampleRate_ = 0;
  uint64_t totalFrames_ = 0;
  void* wav_ = nullptr;  // drwav*
};

// Chunked writer for 48 kHz, 16-bit PCM WAV files (interleaved frames).
class WavWriter {
public:
  ~WavWriter();
  bool open(const std::string& path, int channels, std::string& error);
  bool write(const int16_t* interleaved, uint64_t frames);
  bool close();  // finalizes the file; safe to call once

private:
  void* wav_ = nullptr;  // drwav*
};

}  // namespace ltc
