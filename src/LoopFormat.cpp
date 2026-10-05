#include "LoopFormat.h"
#include "PluginFormat.h"
#include <string.h>
#include <stdlib.h>

static void setLoopErr(LoopManifest *out, const char *msg) {
  memset(out, 0, sizeof(*out));
  strncpy(out->error, msg, sizeof(out->error) - 1);
}

static void setPatErr(PatternFile *out, const char *msg) {
  memset(out, 0, sizeof(*out));
  strncpy(out->error, msg, sizeof(out->error) - 1);
}

static const char *skipSpace(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) {
    p++;
  }
  return p;
}

bool parseLoopManifest(const char *text, int len, LoopManifest *out) {
  if (!out) {
    return false;
  }
  if (!text || len <= 0) {
    setLoopErr(out, "Empty loop manifest");
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->bpm = 120;
  out->bars = 1;
  const char *p = text;
  const char *end = text + len;
  bool sawMagic = false;
  bool sawName = false;
  bool sawBpm = false;
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
      const char *magic = "mothdeck-loops 1";
      int mlen = (int)strlen(magic);
      if ((int)(lineEnd - line) < mlen || memcmp(line, magic, mlen) != 0) {
        setLoopErr(out, "Bad loop magic");
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
      setLoopErr(out, "Bad loop line");
      return false;
    }
    char key[16];
    int klen = (int)(eq - line);
    if (klen <= 0 || klen >= (int)sizeof(key)) {
      setLoopErr(out, "Bad loop key");
      return false;
    }
    memcpy(key, line, klen);
    key[klen] = 0;
    const char *val = skipSpace(eq + 1, lineEnd);
    char value[48];
    int vlen = (int)(lineEnd - val);
    if (vlen < 0 || vlen >= (int)sizeof(value)) {
      setLoopErr(out, "Loop value too long");
      return false;
    }
    memcpy(value, val, vlen);
    value[vlen] = 0;
    if (strcmp(key, "name") == 0) {
      if (!manifestNameSafe(value)) {
        setLoopErr(out, "Bad library name");
        return false;
      }
      strncpy(out->name, value, sizeof(out->name) - 1);
      sawName = true;
    } else if (strcmp(key, "bpm") == 0) {
      int bpm = atoi(value);
      if (bpm < 40 || bpm > 240) {
        setLoopErr(out, "BPM out of range");
        return false;
      }
      out->bpm = bpm;
      sawBpm = true;
    } else if (strcmp(key, "bars") == 0) {
      int bars = atoi(value);
      if (bars < 1 || bars > 8) {
        setLoopErr(out, "Bars out of range");
        return false;
      }
      out->bars = bars;
    } else if (strcmp(key, "tags") == 0) {
      strncpy(out->tags, value, sizeof(out->tags) - 1);
    } else if (strcmp(key, "loop") == 0 || strcmp(key, "pattern") == 0) {
      if (!manifestNameSafe(value)) {
        setLoopErr(out, "Bad loop filename");
        return false;
      }
      if (out->entryCount >= kLoopEntryMax) {
        setLoopErr(out, "Too many loops");
        return false;
      }
      LoopEntry &e = out->entries[out->entryCount++];
      e.kind = (strcmp(key, "pattern") == 0) ? LOOP_PATTERN : LOOP_AUDIO;
      strncpy(e.file, value, sizeof(e.file) - 1);
    }
  }
  if (!sawMagic || !sawName || !sawBpm) {
    setLoopErr(out, "Loop manifest incomplete");
    return false;
  }
  return true;
}

bool parsePatternFile(const char *text, int len, PatternFile *out) {
  if (!out) {
    return false;
  }
  if (!text || len <= 0) {
    setPatErr(out, "Empty pattern");
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->bars = 1;
  const char *p = text;
  const char *end = text + len;
  bool sawMagic = false;
  bool sawSteps = false;
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
      const char *magic = "mothdeck-pattern 1";
      int mlen = (int)strlen(magic);
      if ((int)(lineEnd - line) < mlen || memcmp(line, magic, mlen) != 0) {
        setPatErr(out, "Bad pattern magic");
        return false;
      }
      sawMagic = true;
      continue;
    }
    const char *eq = line;
    while (eq < lineEnd && *eq != '=') {
      eq++;
    }
    if (eq == lineEnd) {
      setPatErr(out, "Bad pattern line");
      return false;
    }
    char key[16];
    int klen = (int)(eq - line);
    if (klen <= 0 || klen >= (int)sizeof(key)) {
      setPatErr(out, "Bad pattern key");
      return false;
    }
    memcpy(key, line, klen);
    key[klen] = 0;
    const char *val = skipSpace(eq + 1, lineEnd);
    if (strcmp(key, "bars") == 0) {
      int bars = atoi(val);
      if (bars < 1 || bars > 4) {
        setPatErr(out, "Pattern bars out of range");
        return false;
      }
      out->bars = bars;
    } else if (strcmp(key, "steps") == 0) {
      int n = 0;
      const char *s = val;
      while (s < lineEnd && n < kPatternStepsMax) {
        while (s < lineEnd && (*s == ' ' || *s == ',')) {
          s++;
        }
        if (s >= lineEnd) {
          break;
        }
        char *stop = 0;
        long v = strtol(s, &stop, 10);
        if (stop == s) {
          setPatErr(out, "Bad step list");
          return false;
        }
        if (v < 0 || v > 12) {
          setPatErr(out, "Step out of range");
          return false;
        }
        out->steps[n++] = (uint8_t)v;
        s = stop;
      }
      if (n == 0) {
        setPatErr(out, "No steps");
        return false;
      }
      out->stepCount = n;
      sawSteps = true;
    }
  }
  if (!sawMagic || !sawSteps) {
    setPatErr(out, "Pattern incomplete");
    return false;
  }
  return true;
}
