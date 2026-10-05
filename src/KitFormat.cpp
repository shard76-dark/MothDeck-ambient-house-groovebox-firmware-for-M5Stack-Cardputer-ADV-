#include "KitFormat.h"
#include "PluginFormat.h"
#include <string.h>
#include <stdlib.h>

static void setErr(KitManifest *out, const char *msg) {
  memset(out, 0, sizeof(*out));
  strncpy(out->error, msg, sizeof(out->error) - 1);
}

static const char *skipSpace(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) {
    p++;
  }
  return p;
}

bool parseKitManifest(const char *text, int len, KitManifest *out) {
  if (!out) {
    return false;
  }
  if (!text || len <= 0) {
    setErr(out, "Empty kit manifest");
    return false;
  }
  memset(out, 0, sizeof(*out));
  const char *p = text;
  const char *end = text + len;
  bool sawMagic = false;
  bool sawName = false;
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
      const char *magic = "mothdeck-kit 1";
      int mlen = (int)strlen(magic);
      if ((int)(lineEnd - line) < mlen || memcmp(line, magic, mlen) != 0) {
        setErr(out, "Bad kit magic");
        return false;
      }
      sawMagic = true;
      continue;
    }
    const char *eq = line;
    while (eq < lineEnd && *eq != '=') {
      eq++;
    }
    if (eq >= lineEnd) {
      setErr(out, "Bad kit line");
      return false;
    }
    char key[16];
    int klen = (int)(eq - line);
    while (klen > 0 && (line[klen - 1] == ' ' || line[klen - 1] == '\t')) {
      klen--;
    }
    if (klen <= 0 || klen > 12) {
      setErr(out, "Bad kit key");
      return false;
    }
    memcpy(key, line, (size_t)klen);
    key[klen] = 0;
    const char *val = skipSpace(eq + 1, lineEnd);
    int vlen = (int)(lineEnd - val);
    while (vlen > 0 && (val[vlen - 1] == ' ' || val[vlen - 1] == '\t')) {
      vlen--;
    }
    if (vlen <= 0 || vlen > 23) {
      setErr(out, "Kit value too long");
      return false;
    }
    char value[32];
    memcpy(value, val, (size_t)vlen);
    value[vlen] = 0;
    if (strcmp(key, "name") == 0) {
      if (!manifestNameSafe(value) && strchr(value, ' ') == 0) {
        setErr(out, "Bad kit name");
        return false;
      }
      // Display names may contain spaces. Reject path characters.
      for (int i = 0; value[i]; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < 32 || c == '/' || c == '\\' || c == '.') {
          setErr(out, "Bad kit name");
          return false;
        }
      }
      if (strlen(value) > 24) {
        setErr(out, "Bad kit name");
        return false;
      }
      strncpy(out->name, value, sizeof(out->name) - 1);
      sawName = true;
    } else if (strcmp(key, "pad") == 0) {
      if (!manifestNameSafe(value)) {
        setErr(out, "Bad pad filename");
        return false;
      }
      if (out->padCount >= kKitPads) {
        setErr(out, "Too many pads");
        return false;
      }
      strncpy(out->pads[out->padCount], value, 31);
      out->padCount++;
    } else {
      setErr(out, "Unknown kit key");
      return false;
    }
  }
  if (!sawMagic || !sawName) {
    setErr(out, "Kit manifest incomplete");
    return false;
  }
  if (out->padCount != kKitPads) {
    setErr(out, "Kit needs 12 pads");
    return false;
  }
  return true;
}
