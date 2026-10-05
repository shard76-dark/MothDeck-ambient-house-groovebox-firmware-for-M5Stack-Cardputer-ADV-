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
