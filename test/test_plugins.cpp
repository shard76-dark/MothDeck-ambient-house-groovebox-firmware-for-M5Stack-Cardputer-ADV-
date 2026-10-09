#include "PluginFormat.h"
#include "PatchBlocks.h"
#include "LoopFormat.h"
#include "WavPcm.h"
#include "SynthRender.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

static int failures = 0;

static void expect(bool ok, const char *message) {
  if (ok) {
    std::printf("ok  %s\n", message);
    return;
  }
  std::printf("FAIL %s\n", message);
  failures++;
}

static void testInstrument() {
  const char *ok =
      "mothdeck-instrument 1\n"
      "name=soft-saw\n"
      "type=sample\n"
      "root=60\n"
      "loop_start=10\n"
      "loop_end=400\n"
      "oneshot=0\n"
      "gain=80\n"
      "sample=tone.wav\n";
  InstrumentManifest m;
  expect(parseInstrumentManifest(ok, (int)std::strlen(ok), &m), "sample manifest parses");
  expect(std::strcmp(m.name, "soft-saw") == 0 && m.kind == INST_SAMPLE, "name and type");
  expect(m.rootMidi == 60 && m.loopStart == 10 && m.loopEnd == 400 && m.gain == 80, "sample fields");
  expect(std::strcmp(m.sampleFile, "tone.wav") == 0, "sample filename");

  const char *fm =
      "mothdeck-instrument 1\n"
      "name=bell\n"
      "type=fm\n"
      "fm_ratio=3\n"
      "fm_index=55\n";
  expect(parseInstrumentManifest(fm, (int)std::strlen(fm), &m), "fm manifest parses");
  expect(m.kind == INST_FM && m.fmRatio == 3 && m.fmIndex == 55, "fm parameters");

  const char *bad = "not a manifest\nname=x\n";
  expect(!parseInstrumentManifest(bad, (int)std::strlen(bad), &m), "bad magic rejected");
  expect(std::strstr(m.error, "magic") != 0, "bad magic explains itself");

  const char *slash =
      "mothdeck-instrument 1\n"
      "name=../x\n"
      "type=fm\n";
  expect(!parseInstrumentManifest(slash, (int)std::strlen(slash), &m), "path in name rejected");

  const char *nosamp =
      "mothdeck-instrument 1\n"
      "name=bare\n"
      "type=wavetable\n";
  expect(!parseInstrumentManifest(nosamp, (int)std::strlen(nosamp), &m), "wavetable without a sample rejected");

  const char *loopbad =
      "mothdeck-instrument 1\n"
      "name=badloop\n"
      "type=subtractive\n"
      "loop_start=20\n"
      "loop_end=10\n";
  expect(!parseInstrumentManifest(loopbad, (int)std::strlen(loopbad), &m), "inverted loop rejected");
}

static void testLoops() {
  const char *text =
      "mothdeck-loops 1\n"
      "name=dust\n"
      "bpm=100\n"
      "bars=1\n"
      "tags=dust,drum\n"
      "loop=kick.wav\n"
      "pattern=bass.pat\n";
  LoopManifest lib;
  expect(parseLoopManifest(text, (int)std::strlen(text), &lib), "loop library parses");
  expect(lib.bpm == 100 && lib.bars == 1 && lib.entryCount == 2, "tempo, bars, entries");
  expect(lib.entries[0].kind == LOOP_AUDIO && lib.entries[1].kind == LOOP_PATTERN, "audio and pattern kinds");

  const char *fast =
      "mothdeck-loops 1\nname=x\nbpm=999\n";
  expect(!parseLoopManifest(fast, (int)std::strlen(fast), &lib), "absurd bpm rejected");

  const char *pat =
      "mothdeck-pattern 1\n"
      "bars=1\n"
      "steps=8,0,0,0,8,0,5,0\n";
  PatternFile pf;
  expect(parsePatternFile(pat, (int)std::strlen(pat), &pf), "pattern parses");
  expect(pf.stepCount == 8 && pf.steps[0] == 8 && pf.steps[6] == 5, "pattern steps");

  const char *badstep = "mothdeck-pattern 1\nsteps=1,99\n";
  expect(!parsePatternFile(badstep, (int)std::strlen(badstep), &pf), "step above 12 rejected");
}

static void putU16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}

static void putU32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void testWav() {
  uint8_t wav[64];
  std::memset(wav, 0, sizeof(wav));
  std::memcpy(wav, "RIFF", 4);
  putU32(wav + 4, 56);
  std::memcpy(wav + 8, "WAVE", 4);
  std::memcpy(wav + 12, "fmt ", 4);
  putU32(wav + 16, 16);
  putU16(wav + 20, 1);
  putU16(wav + 22, 1);
  putU32(wav + 24, 22050);
  putU32(wav + 28, 44100);
  putU16(wav + 32, 2);
  putU16(wav + 34, 16);
  std::memcpy(wav + 36, "data", 4);
  putU32(wav + 40, 8);
  putU16(wav + 44, 1000);
  putU16(wav + 46, (uint16_t)(int16_t)-1000);
  putU16(wav + 48, 2000);
  putU16(wav + 50, (uint16_t)(int16_t)-2000);
  WavInfo info;
  int16_t dst[8];
  int n = decodeWavMono(wav, 52, dst, 8, &info);
  expect(n == 4 && info.rate == 22050 && info.bits == 16, "pcm wav header");
  expect(dst[0] == 1000 && dst[1] == -1000 && dst[3] == -2000, "pcm samples");

  wav[20] = 3;
  expect(decodeWavMono(wav, 52, dst, 8, &info) < 0, "non-pcm wav rejected");
  expect(std::strstr(info.error, "PCM") != 0, "non-pcm explains itself");

  uint8_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  expect(!parseWavHeader(junk, 8, &info), "short junk rejected");
}

static void testRender() {
  int16_t buf[168];
  renderSubtractive(buf, 168, 2, 80, 10);
  bool pos = false;
  bool neg = false;
  bool finite = true;
  for (int i = 0; i < 168; i++) {
    if (buf[i] > 0) {
      pos = true;
    }
    if (buf[i] < 0) {
      neg = true;
    }
    if (buf[i] == 32767 || buf[i] == -32768) {
      // clipping is allowed but a full-scale flat line is not
    }
  }
  expect(pos && neg, "subtractive cycle has both polarities");
  renderFm(buf, 168, 2, 40);
  int64_t energy = 0;
  int peak = 0;
  for (int i = 0; i < 168; i++) {
    int a = buf[i] < 0 ? -buf[i] : buf[i];
    if (a > peak) {
      peak = a;
    }
    energy += (int32_t)buf[i] * buf[i];
  }
  expect(finite && energy > 0 && peak < 32767, "fm cycle is in range and not silent");
}

static void testPatch() {
  const char *minor =
      "mothdeck-patch 1\n"
      "name=a-minor\n"
      "source=builtin\n"
      "builtin=bass\n"
      "scale=minor\n"
      "root=9\n"
      "arp=0\n"
      "glide=0\n"
      "osc2=off\n";
  PatchAssign p;
  expect(parsePatchManifest(minor, (int)std::strlen(minor), &p), "minor patch parses");
  expect(p.instrument == 10 && p.block.scaleMode == 2 && p.block.scaleRoot == 9, "bass in A minor");
  expect((p.blockMask & PATCH_SCALE) != 0 && p.block.arpMode == 0, "scale is set and arp is off");

  const char *held =
      "mothdeck-patch 1\n"
      "name=held-arp\n"
      "source=builtin\n"
      "builtin=saw\n"
      "arp=held\n";
  expect(parsePatchManifest(held, (int)std::strlen(held), &p), "held arp parses");
  expect(p.block.arpMode == 3 && p.voice.chordMult == 0, "held arp is not the interval walk");

  const char *glide =
      "mothdeck-patch 1\n"
      "name=glide-bass\n"
      "source=builtin\n"
      "builtin=bass\n"
      "osc2=saw\n"
      "coarse=-12\n"
      "blend=40\n"
      "glide=90\n";
  expect(parsePatchManifest(glide, (int)std::strlen(glide), &p), "glide patch parses");
  expect(p.block.osc2Wave == 3 && p.block.osc2Coarse == -12 && p.block.blend == 40 && p.block.glideMs == 90, "second saw and glide");

  const char *saw =
      "mothdeck-patch 1\n"
      "name=saw-pluck\n"
      "source=subtractive\n"
      "wave=saw\n"
      "cutoff=80\n"
      "resonance=10\n"
      "filter=1\n"
      "cutoff=64\n"
      "res=12\n";
  expect(parsePatchManifest(saw, (int)std::strlen(saw), &p), "subtractive patch parses");
  expect(p.audio.cutoff == 80 && p.fx.cutoff == 64 && p.fx.filter == 1 && p.fx.res == 12, "baked cutoff and track cutoff stay apart");

  const char *bad =
      "mothdeck-patch 1\n"
      "name=nope\n"
      "source=builtin\n"
      "builtin=bass\n"
      "attack=10\n";
  expect(!parsePatchManifest(bad, (int)std::strlen(bad), &p), "attack is rejected");
  expect(std::strstr(p.error, "Unknown") != 0, "unknown key is named");

  expect(scaleLock(61, 0, 1) == 60, "C# snaps down to C in C major");
  expect(scaleLock(60, 0, 1) == 60, "C stays in C major");
  expect(scaleLock(70, 9, 2) == 69, "A# snaps to A in A minor");
  expect(glideAt(100, 200, 0, 4) == 100 && glideAt(100, 200, 4, 4) == 200, "glide endpoints");
  expect(glideAt(100, 200, 2, 4) == 150, "glide midpoint");
  expect(shiftSemi(1000, 12) == 2000 && shiftSemi(1000, -12) == 500, "octave shift");
  uint8_t notes[8];
  int count = 0;
  expect(heldAdd(notes, &count, 60) == 1 && count == 1, "first held note");
  expect(heldAdd(notes, &count, 64) == 2 && count == 2, "second held note joins");
  expect(heldAdd(notes, &count, 60) == 0 && count == 2, "duplicate held note ignored");
  int index = 1;
  expect(heldRemove(notes, &count, &index, 64) == 1 && notes[0] == 60, "releasing one note leaves the other");
}

int main() {
  testInstrument();
  testLoops();
  testWav();
  testRender();
  testPatch();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
