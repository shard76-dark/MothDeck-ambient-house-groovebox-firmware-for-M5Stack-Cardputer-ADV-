// Host-only. Renders short WAVs of the real-time voices.
// Build: g++ -std=c++17 -Iinclude tools/render_previews.cpp src/ToneSynth.cpp -o /tmp/render_previews
#include "ToneSynth.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

static void writeWav(const char *path, const int16_t *samples, int n, int rate) {
  FILE *f = std::fopen(path, "wb");
  if (!f) {
    std::perror(path);
    return;
  }
  uint32_t dataBytes = (uint32_t)n * 2;
  uint32_t chunk = 36 + dataBytes;
  uint16_t audioFormat = 1, channels = 1, bits = 16;
  uint32_t byteRate = (uint32_t)rate * 2;
  uint16_t blockAlign = 2;
  std::fwrite("RIFF", 1, 4, f);
  std::fwrite(&chunk, 4, 1, f);
  std::fwrite("WAVE", 1, 4, f);
  std::fwrite("fmt ", 1, 4, f);
  uint32_t fmtSize = 16;
  std::fwrite(&fmtSize, 4, 1, f);
  std::fwrite(&audioFormat, 2, 1, f);
  std::fwrite(&channels, 2, 1, f);
  std::fwrite(&rate, 4, 1, f);
  std::fwrite(&byteRate, 4, 1, f);
  std::fwrite(&blockAlign, 2, 1, f);
  std::fwrite(&bits, 2, 1, f);
  std::fwrite("data", 1, 4, f);
  std::fwrite(&dataBytes, 4, 1, f);
  std::fwrite(samples, 2, (size_t)n, f);
  std::fclose(f);
}

static void render(const char *path, int id, int freq, int n) {
  ToneVoice voice;
  toneNoteOn(&voice, id);
  int16_t *dst = new int16_t[(size_t)n];
  for (int i = 0; i < n; i++) {
    int s = toneSample(&voice, id, freq);
    // Roughly the tracker's default volume and envelope start.
    dst[i] = (int16_t)(s * 2 / 3);
  }
  writeWav(path, dst, n, 44100);
  delete[] dst;
  std::printf("%s\n", path);
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : "/tmp/moth-previews";
  std::string base = dir;
  render((base + "/preview-sine.wav").c_str(), 2, 1000, 44100);
  render((base + "/preview-square.wav").c_str(), 3, 1000, 44100);
  render((base + "/preview-saw.wav").c_str(), 4, 1000, 44100);
  render((base + "/preview-tri.wav").c_str(), 5, 1000, 44100);
  render((base + "/preview-organ.wav").c_str(), 6, 1000, 66150);
  render((base + "/preview-pluck.wav").c_str(), 7, 1000, 22050);
  render((base + "/preview-bell.wav").c_str(), 8, 1000, 44100);
  render((base + "/preview-flute.wav").c_str(), 9, 1000, 66150);
  render((base + "/preview-bass.wav").c_str(), 10, 500, 44100);
  render((base + "/preview-pad.wav").c_str(), 11, 1000, 88200);
  return 0;
}
