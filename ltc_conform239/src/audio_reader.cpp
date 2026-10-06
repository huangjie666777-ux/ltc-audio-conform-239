#include "audio_reader.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

namespace ltc {

bool WavReader::open(const std::string& path, std::string& error) {
  drwav* wav = new drwav;
  if (!drwav_init_file(wav, path.c_str(), nullptr)) {
    delete wav;
    error = "cannot open WAV file: " + path;
    return false;
  }
  if (wav->translatedFormatTag != DR_WAVE_FORMAT_PCM ||
      wav->bitsPerSample != 16) {
    error = "unsupported format: need 16-bit PCM WAV";
    drwav_uninit(wav);
    delete wav;
    return false;
  }
  if (wav->sampleRate != 48000) {
    error = "unsupported sample rate: need 48000 Hz";
    drwav_uninit(wav);
    delete wav;
    return false;
  }
  if (wav->channels < 1 || wav->channels > 2) {
    error = "unsupported channel count: need mono or stereo";
    drwav_uninit(wav);
    delete wav;
    return false;
  }
  wav_ = wav;
  path_ = path;
  channels_ = wav->channels;
  sampleRate_ = wav->sampleRate;
  totalFrames_ = wav->totalPCMFrameCount;
  return true;
}

uint64_t WavReader::readChannel(int ch, int16_t* out, uint64_t n) {
  drwav* wav = static_cast<drwav*>(wav_);
  if (channels_ == 1) {
    return drwav_read_pcm_frames_s16(wav, n, out);
  }
  std::vector<int16_t> interleaved(static_cast<size_t>(n) * channels_);
  const uint64_t got =
      drwav_read_pcm_frames_s16(wav, n, interleaved.data());
  for (uint64_t i = 0; i < got; ++i) out[i] = interleaved[i * channels_ + ch];
  return got;
}

bool WavReader::seekToStart() {
  return drwav_seek_to_pcm_frame(static_cast<drwav*>(wav_), 0) == DRWAV_TRUE;
}

}  // namespace ltc
