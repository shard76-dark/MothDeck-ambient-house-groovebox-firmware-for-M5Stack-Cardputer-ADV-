#include "PluginFormat.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

bool manifestNameSafe(const char *name) {
  if (!name || !name[0] || strlen(name) > 23) {
    return false;
  }
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
    return false;
  }
  int dots = 0;
  for (const char *p = name; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '.') {
      if (++dots > 1) {
        return false;
      }
    } else {
      dots = 0;
    }
    if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
      return false;
    }
  }
  return true;
}

static void setErr(InstrumentManifest *out, const char *msg) {
  memset(out, 0, sizeof(*out));
  strncpy(out->error, msg, sizeof(out->error) - 1);
}

static const char *skipSpace(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) {
    p++;
  }
  return p;
}

static int clamp100(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > 100) {
    return 100;
  }
  return v;
}

bool parseInstrumentManifest(const char *text, int len, InstrumentManifest *out) {
  if (!out) {
    return false;
  }
  if (!text || len <= 0) {
    setErr(out, "Empty manifest");
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->rootMidi = 60;
  out->gain = 100;
  out->sustain = 80;
  out->cutoff = 100;
  out->fmRatio = 2;
  out->fmIndex = 40;
  out->sampleRate = 22050;

  const char *p = text;
  const char *end = text + len;
  bool sawMagic = false;
  bool sawName = false;
  bool sawType = false;
  while (p < end) {
    const char *line = p;
    while (p < end && *p != '\n') {
      p++;
    }
    const char *lineEnd = p;
    if (p < end && *p == '\n') {
      p++;
    }
    if (lineEnd > line && lineEnd[-1] == '\r') {
      lineEnd--;
    }
    line = skipSpace(line, lineEnd);
    if (line >= lineEnd || *line == '#') {
      continue;
    }
    if (!sawMagic) {
      const char *magic = "mothdeck-instrument 1";
      int mlen = (int)strlen(magic);
      if ((int)(lineEnd - line) < mlen || memcmp(line, magic, mlen) != 0) {
        setErr(out, "Bad instrument magic");
        return false;
      }
      sawMagic = true;
      continue;
    }
    const char *eq = line;
    while (eq < lineEnd && *eq != '=') {
      eq++;
    }
    if (eq == lineEnd || eq == line) {
      setErr(out, "Bad manifest line");
      return false;
    }
    char key[24];
    int klen = (int)(eq - line);
    if (klen <= 0 || klen >= (int)sizeof(key)) {
      setErr(out, "Bad manifest key");
      return false;
    }
    memcpy(key, line, klen);
    key[klen] = 0;
    const char *val = skipSpace(eq + 1, lineEnd);
    char value[40];
    int vlen = (int)(lineEnd - val);
    if (vlen < 0 || vlen >= (int)sizeof(value)) {
      setErr(out, "Value too long");
      return false;
    }
    memcpy(value, val, vlen);
    value[vlen] = 0;
    if (strcmp(key, "name") == 0) {
      if (!manifestNameSafe(value)) {
        setErr(out, "Bad instrument name");
        return false;
      }
      strncpy(out->name, value, sizeof(out->name) - 1);
      sawName = true;
    } else if (strcmp(key, "type") == 0) {
      if (strcmp(value, "sample") == 0) {
        out->kind = INST_SAMPLE;
      } else if (strcmp(value, "subtractive") == 0) {
        out->kind = INST_SUBTRACTIVE;
      } else if (strcmp(value, "fm") == 0) {
        out->kind = INST_FM;
      } else if (strcmp(value, "wavetable") == 0) {
        out->kind = INST_WAVETABLE;
      } else {
        setErr(out, "Unknown instrument type");
        return false;
      }
      sawType = true;
    } else if (strcmp(key, "wave") == 0) {
      if (strcmp(value, "sine") == 0) {
        out->wave = WAVE_SINE;
      } else if (strcmp(value, "square") == 0) {
        out->wave = WAVE_SQUARE;
      } else if (strcmp(value, "saw") == 0) {
        out->wave = WAVE_SAW;
      } else if (strcmp(value, "triangle") == 0) {
        out->wave = WAVE_TRIANGLE;
      } else {
        setErr(out, "Unknown wave");
        return false;
      }
    } else if (strcmp(key, "sample") == 0) {
      if (!manifestNameSafe(value)) {
        setErr(out, "Bad sample filename");
        return false;
      }
      strncpy(out->sampleFile, value, sizeof(out->sampleFile) - 1);
    } else if (strcmp(key, "root") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 127) {
        setErr(out, "Root note out of range");
        return false;
      }
      out->rootMidi = (uint8_t)n;
    } else if (strcmp(key, "loop_start") == 0) {
      out->loopStart = atoi(value);
    } else if (strcmp(key, "loop_end") == 0) {
      out->loopEnd = atoi(value);
    } else if (strcmp(key, "rate") == 0) {
      out->sampleRate = atoi(value);
      if (out->sampleRate < 1000 || out->sampleRate > 96000) {
        setErr(out, "Sample rate out of range");
        return false;
      }
    } else if (strcmp(key, "attack") == 0) {
      out->attack = (uint8_t)clamp100(atoi(value));
    } else if (strcmp(key, "decay") == 0) {
      out->decay = (uint8_t)clamp100(atoi(value));
    } else if (strcmp(key, "sustain") == 0) {
      out->sustain = (uint8_t)clamp100(atoi(value));
    } else if (strcmp(key, "release") == 0) {
      out->release = (uint8_t)clamp100(atoi(value));
    } else if (strcmp(key, "gain") == 0) {
      out->gain = (uint8_t)clamp100(atoi(value));
    } else if (strcmp(key, "cutoff") == 0) {
      out->cutoff = (uint8_t)clamp100(atoi(value));
    } else if (strcmp(key, "resonance") == 0) {
      out->resonance = (uint8_t)clamp100(atoi(value));
    } else if (strcmp(key, "fm_ratio") == 0) {
      int n = atoi(value);
      if (n < 1 || n > 16) {
        setErr(out, "FM ratio out of range");
        return false;
      }
      out->fmRatio = (uint8_t)n;
    } else if (strcmp(key, "fm_index") == 0) {
      out->fmIndex = (uint8_t)clamp100(atoi(value));
    } else if (strcmp(key, "oneshot") == 0) {
      out->oneshot = (uint8_t)(atoi(value) ? 1 : 0);
    }
  }
  if (!sawMagic) {
    setErr(out, "Bad instrument magic");
    return false;
  }
  if (!sawName || !sawType) {
    setErr(out, "Manifest needs name and type");
    return false;
  }
  if ((out->kind == INST_SAMPLE || out->kind == INST_WAVETABLE) && out->sampleFile[0] == 0) {
    setErr(out, "Sample file missing");
    return false;
  }
  if (out->loopStart < 0 || out->loopEnd < 0) {
    setErr(out, "Loop point negative");
    return false;
  }
  if (out->loopEnd != 0 && out->loopEnd <= out->loopStart) {
    setErr(out, "Loop end before start");
    return false;
  }
  return true;
}

static bool lineIs(const char *line, const char *end, const char *magic) {
  int mlen = (int)strlen(magic);
  return (int)(end - line) >= mlen && memcmp(line, magic, mlen) == 0;
}

bool patchManifestMagic(const char *text, int len) {
  if (!text || len <= 0) {
    return false;
  }
  const char *p = text;
  const char *end = text + len;
  while (p < end) {
    const char *line = p;
    while (p < end && *p != '\n') {
      p++;
    }
    const char *lineEnd = p;
    if (lineEnd > line && lineEnd[-1] == '\r') {
      lineEnd--;
    }
    line = skipSpace(line, lineEnd);
    if (line < lineEnd && *line != '#') {
      return lineIs(line, lineEnd, "mothdeck-patch 1");
    }
    if (p < end && *p == '\n') {
      p++;
    }
  }
  return false;
}

static void patchErr(PatchAssign *out, const char *msg) {
  memset(out, 0, sizeof(*out));
  strncpy(out->error, msg, sizeof(out->error) - 1);
}

static int builtinId(const char *name) {
  static const char *kNames[] = {
    "drums", "sfx", "sine", "square", "saw", "tri", "organ", "pluck", "bell", "flute", "bass", "pad"
  };
  for (int i = 0; i < 12; i++) {
    if (strcmp(name, kNames[i]) == 0) {
      return i;
    }
  }
  if (strcmp(name, "triangle") == 0) {
    return 5;
  }
  return -1;
}

static int waveId(const char *name) {
  if (strcmp(name, "sine") == 0) return WAVE_SINE;
  if (strcmp(name, "square") == 0) return WAVE_SQUARE;
  if (strcmp(name, "saw") == 0) return WAVE_SAW;
  if (strcmp(name, "triangle") == 0 || strcmp(name, "tri") == 0) return WAVE_TRIANGLE;
  return -1;
}

static int scaleId(const char *name) {
  if (strcmp(name, "off") == 0) return 0;
  if (strcmp(name, "major") == 0) return 1;
  if (strcmp(name, "minor") == 0) return 2;
  if (strcmp(name, "harmonic") == 0) return 3;
  if (strcmp(name, "mixolydian") == 0) return 4;
  if (strcmp(name, "phrygian") == 0) return 5;
  if (strcmp(name, "chromatic") == 0) return 6;
  return -1;
}

static bool readKey(const char *line, const char *lineEnd, char *key, int keyLen, char *value, int valueLen, PatchAssign *out) {
  const char *eq = line;
  while (eq < lineEnd && *eq != '=') {
    eq++;
  }
  if (eq == lineEnd || eq == line) {
    patchErr(out, "Bad manifest line");
    return false;
  }
  int klen = (int)(eq - line);
  if (klen <= 0 || klen >= keyLen) {
    patchErr(out, "Bad manifest key");
    return false;
  }
  memcpy(key, line, klen);
  key[klen] = 0;
  const char *val = skipSpace(eq + 1, lineEnd);
  int vlen = (int)(lineEnd - val);
  if (vlen < 0 || vlen >= valueLen) {
    patchErr(out, "Value too long");
    return false;
  }
  memcpy(value, val, vlen);
  value[vlen] = 0;
  return true;
}

bool parsePatchManifest(const char *text, int len, PatchAssign *out) {
  if (!out) {
    return false;
  }
  if (!text || len <= 0) {
    patchErr(out, "Empty manifest");
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->active = 1;
  out->audio.gain = 100;
  out->audio.cutoff = 100;
  out->audio.rootMidi = 60;
  out->audio.fmRatio = 2;
  out->audio.fmIndex = 40;
  out->audio.sampleRate = 22050;

  bool sawSource = false;
  bool sawBuiltin = false;
  bool fxOpen = false;
  const char *p = text;
  const char *end = text + len;
  bool sawMagic = false;
  while (p < end) {
    const char *line = p;
    while (p < end && *p != '\n') {
      p++;
    }
    const char *lineEnd = p;
    if (p < end && *p == '\n') {
      p++;
    }
    if (lineEnd > line && lineEnd[-1] == '\r') {
      lineEnd--;
    }
    line = skipSpace(line, lineEnd);
    if (line >= lineEnd || *line == '#') {
      continue;
    }
    if (!sawMagic) {
      if (!lineIs(line, lineEnd, "mothdeck-patch 1")) {
        patchErr(out, "Bad patch magic");
        return false;
      }
      sawMagic = true;
      continue;
    }
    char key[24];
    char value[40];
    if (!readKey(line, lineEnd, key, (int)sizeof(key), value, (int)sizeof(value), out)) {
      return false;
    }
    if (strcmp(key, "name") == 0) {
      if (!manifestNameSafe(value)) {
        patchErr(out, "Bad patch name");
        return false;
      }
      strncpy(out->name, value, sizeof(out->name) - 1);
    } else if (strcmp(key, "source") == 0) {
      if (strcmp(value, "builtin") == 0) {
        out->source = PATCH_BUILTIN;
      } else if (strcmp(value, "sample") == 0) {
        out->source = PATCH_SAMPLE;
        out->audio.kind = INST_SAMPLE;
      } else if (strcmp(value, "wavetable") == 0) {
        out->source = PATCH_WAVE;
        out->audio.kind = INST_WAVETABLE;
      } else if (strcmp(value, "subtractive") == 0) {
        out->source = PATCH_SUB;
        out->audio.kind = INST_SUBTRACTIVE;
      } else if (strcmp(value, "fm") == 0) {
        out->source = PATCH_FM;
        out->audio.kind = INST_FM;
      } else {
        patchErr(out, "Unknown source");
        return false;
      }
      sawSource = true;
    } else if (strcmp(key, "builtin") == 0) {
      int id = builtinId(value);
      if (id < 0) {
        patchErr(out, "Bad builtin");
        return false;
      }
      out->instrument = (uint8_t)id;
      sawBuiltin = true;
    } else if (strcmp(key, "wave") == 0) {
      int w = waveId(value);
      if (w < 0) {
        patchErr(out, "Unknown wave");
        return false;
      }
      out->audio.wave = (uint8_t)w;
    } else if (strcmp(key, "sample") == 0) {
      if (!manifestNameSafe(value)) {
        patchErr(out, "Bad sample filename");
        return false;
      }
      strncpy(out->audio.sampleFile, value, sizeof(out->audio.sampleFile) - 1);
    } else if (strcmp(key, "root") == 0 || strcmp(key, "scaleroot") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 127) {
        patchErr(out, "Root note out of range");
        return false;
      }
      bool sampleRoot = strcmp(key, "root") == 0 && (out->source == PATCH_SAMPLE || out->source == PATCH_WAVE);
      if (sampleRoot) {
        out->audio.rootMidi = (uint8_t)n;
      }
      if (!sampleRoot || (out->blockMask & PATCH_SCALE)) {
        out->block.scaleRoot = (uint8_t)(n % 12);
        out->blockMask |= PATCH_ROOT;
      }
    } else if (strcmp(key, "rate") == 0) {
      int n = atoi(value);
      if (n < 1000 || n > 96000) {
        patchErr(out, "Sample rate out of range");
        return false;
      }
      out->audio.sampleRate = n;
    } else if (strcmp(key, "loop_start") == 0) {
      out->audio.loopStart = atoi(value);
    } else if (strcmp(key, "loop_end") == 0) {
      out->audio.loopEnd = atoi(value);
    } else if (strcmp(key, "oneshot") == 0) {
      out->audio.oneshot = (uint8_t)(atoi(value) ? 1 : 0);
    } else if (strcmp(key, "gain") == 0) {
      out->audio.gain = (uint8_t)clamp100(atoi(value));
    } else if (strcmp(key, "fm_ratio") == 0) {
      int n = atoi(value);
      if (n < 1 || n > 16) {
        patchErr(out, "FM ratio out of range");
        return false;
      }
      out->audio.fmRatio = (uint8_t)n;
    } else if (strcmp(key, "fm_index") == 0) {
      out->audio.fmIndex = (uint8_t)clamp100(atoi(value));
    } else if (strcmp(key, "resonance") == 0) {
      int n = clamp100(atoi(value));
      if (n > 90) {
        n = 90;
      }
      out->audio.resonance = (uint8_t)n;
    } else if (strcmp(key, "cutoff") == 0) {
      int n = atoi(value);
      if (n < 0) {
        n = 0;
      }
      if (sawSource && out->source == PATCH_SUB && !fxOpen) {
        if (n < 1) {
          n = 1;
        }
        if (n > 100) {
          n = 100;
        }
        out->audio.cutoff = (uint8_t)n;
      } else {
        if (n > 127) {
          n = 127;
        }
        out->fx.cutoff = (uint8_t)n;
        out->fxMask |= PATCH_CUTOFF;
        fxOpen = true;
      }
    } else if (strcmp(key, "scale") == 0) {
      int id = scaleId(value);
      if (id < 0) {
        patchErr(out, "Unknown scale");
        return false;
      }
      out->block.scaleMode = (uint8_t)id;
      out->blockMask |= PATCH_SCALE;
    } else if (strcmp(key, "arp") == 0) {
      if (strcmp(value, "held") == 0) {
        out->block.arpMode = 3;
      } else {
        int n = atoi(value);
        if (n < 0 || n > 2) {
          patchErr(out, "Bad arp");
          return false;
        }
        out->block.arpMode = (uint8_t)n;
      }
      out->voice.chordMult = out->block.arpMode == 3 ? 0 : out->block.arpMode;
      out->voiceMask |= PATCH_ARP;
      out->blockMask |= PATCH_ARP;
    } else if (strcmp(key, "osc2") == 0) {
      if (strcmp(value, "off") == 0) {
        out->block.osc2Wave = 0;
      } else {
        int w = waveId(value);
        if (w < 0) {
          patchErr(out, "Unknown osc2");
          return false;
        }
        out->block.osc2Wave = (uint8_t)(w + 1);
      }
      out->blockMask |= PATCH_OSC2;
    } else if (strcmp(key, "coarse") == 0 || strcmp(key, "osc2coarse") == 0) {
      int n = atoi(value);
      if (n < -24 || n > 24) {
        patchErr(out, "Coarse out of range");
        return false;
      }
      out->block.osc2Coarse = (int8_t)n;
      out->blockMask |= PATCH_COARSE;
    } else if (strcmp(key, "blend") == 0) {
      out->block.blend = (uint8_t)clamp100(atoi(value));
      out->blockMask |= PATCH_BLEND;
    } else if (strcmp(key, "glide") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 2000) {
        patchErr(out, "Glide out of range");
        return false;
      }
      out->block.glideMs = (uint16_t)n;
      out->blockMask |= PATCH_GLIDE;
    } else if (strcmp(key, "volume") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 8) {
        patchErr(out, "Volume out of range");
        return false;
      }
      out->voice.volume = (uint8_t)n;
      out->voiceMask |= PATCH_VOLUME;
    } else if (strcmp(key, "octave") == 0) {
      int n = atoi(value);
      if (n < -8 || n > 8) {
        patchErr(out, "Octave out of range");
        return false;
      }
      out->voice.octave = (int8_t)n;
      out->voiceMask |= PATCH_OCTAVE;
    } else if (strcmp(key, "sampler") == 0) {
      out->voice.samplerMode = (uint8_t)(atoi(value) ? 1 : 0);
      out->voiceMask |= PATCH_SAMPLER;
    } else if (strcmp(key, "overdrive") == 0) {
      out->voice.overdrive = (uint8_t)(atoi(value) ? 1 : 0);
      out->voiceMask |= PATCH_OVERDRIVE;
    } else if (strcmp(key, "envelope") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 3) {
        patchErr(out, "Envelope out of range");
        return false;
      }
      out->voice.envelopeNum = (uint8_t)n;
      out->voiceMask |= PATCH_ENVELOPE;
    } else if (strcmp(key, "envlen") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 8) {
        patchErr(out, "Env length out of range");
        return false;
      }
      out->voice.envelopeLength = (uint8_t)n;
      out->voiceMask |= PATCH_ENVLEN;
    } else if (strcmp(key, "lowpass") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 2) {
        patchErr(out, "Lowpass out of range");
        return false;
      }
      out->voice.lowPassMult = (uint8_t)n;
      out->voiceMask |= PATCH_LOWPASS;
    } else if (strcmp(key, "whoosh") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 2) {
        patchErr(out, "Whoosh out of range");
        return false;
      }
      out->voice.whooshMult = (uint8_t)n;
      out->voiceMask |= PATCH_WHOOSH;
    } else if (strcmp(key, "wobble") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 2) {
        patchErr(out, "Wobble out of range");
        return false;
      }
      out->voice.phaserMult = (uint8_t)n;
      out->voiceMask |= PATCH_WOBBLE;
    } else if (strcmp(key, "pitch") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 2) {
        patchErr(out, "Pitch out of range");
        return false;
      }
      out->voice.pitchMult = (uint8_t)n;
      out->voiceMask |= PATCH_PITCH;
    } else if (strcmp(key, "filter") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 2) {
        patchErr(out, "Filter out of range");
        return false;
      }
      out->fx.filter = (uint8_t)n;
      out->fxMask |= PATCH_FILTER;
      fxOpen = true;
    } else if (strcmp(key, "res") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 80) {
        patchErr(out, "Res out of range");
        return false;
      }
      out->fx.res = (uint8_t)n;
      out->fxMask |= PATCH_RES;
      fxOpen = true;
    } else if (strcmp(key, "delay") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 3) {
        patchErr(out, "Delay out of range");
        return false;
      }
      out->fx.delayDiv = (uint8_t)n;
      out->fxMask |= PATCH_DELAY;
      fxOpen = true;
    } else if (strcmp(key, "feedback") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 70) {
        patchErr(out, "Feedback out of range");
        return false;
      }
      out->fx.delayFb = (uint8_t)n;
      out->fxMask |= PATCH_FEEDBACK;
      fxOpen = true;
    } else if (strcmp(key, "mix") == 0) {
      out->fx.delayMix = (uint8_t)clamp100(atoi(value));
      out->fxMask |= PATCH_MIX;
      fxOpen = true;
    } else if (strcmp(key, "reverb") == 0) {
      out->fx.reverb = (uint8_t)clamp100(atoi(value));
      out->fxMask |= PATCH_REVERB;
      fxOpen = true;
    } else if (strcmp(key, "crush") == 0) {
      int n = atoi(value);
      if (n < 0 || n > 4) {
        patchErr(out, "Crush out of range");
        return false;
      }
      out->fx.crush = (uint8_t)n;
      out->fxMask |= PATCH_CRUSH;
      fxOpen = true;
    } else if (strcmp(key, "drive") == 0) {
      out->fx.drive = (uint8_t)clamp100(atoi(value));
      out->fxMask |= PATCH_DRIVE;
      fxOpen = true;
    } else if (strcmp(key, "chorus") == 0) {
      out->fx.chorus = (uint8_t)clamp100(atoi(value));
      out->fxMask |= PATCH_CHORUS;
      fxOpen = true;
    } else if (strcmp(key, "tremolo") == 0) {
      out->fx.tremolo = (uint8_t)clamp100(atoi(value));
      out->fxMask |= PATCH_TREMOLO;
      fxOpen = true;
    } else {
      patchErr(out, "Unknown key");
      return false;
    }
  }
  if (!sawMagic || out->name[0] == 0 || !sawSource) {
    patchErr(out, "Patch needs name and source");
    return false;
  }
  if (out->source == PATCH_BUILTIN && !sawBuiltin) {
    patchErr(out, "Need builtin");
    return false;
  }
  if ((out->source == PATCH_SAMPLE || out->source == PATCH_WAVE) && out->audio.sampleFile[0] == 0) {
    patchErr(out, "Sample file missing");
    return false;
  }
  if (out->audio.loopStart < 0 || out->audio.loopEnd < 0 ||
      (out->audio.loopEnd != 0 && out->audio.loopEnd <= out->audio.loopStart)) {
    patchErr(out, "Loop end before start");
    return false;
  }
  strncpy(out->audio.name, out->name, sizeof(out->audio.name) - 1);
  return true;
}
