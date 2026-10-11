#include "WifiFiles.h"
#include <string.h>

static bool segmentOk(const char *s, int n) {
  if (n < 1 || n > 23) {
    return false;
  }
  int dots = 0;
  for (int i = 0; i < n; i++) {
    char c = s[i];
    if (c == '.') {
      dots++;
    }
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.';
    if (!ok) {
      return false;
    }
  }
  if (dots > 1) {
    return false;
  }
  if (n == 1 && s[0] == '.') {
    return false;
  }
  if (n == 2 && s[0] == '.' && s[1] == '.') {
    return false;
  }
  return true;
}

bool wifiMothPath(const char *rel, char *out, int outLen) {
  if (!out || outLen < 6) {
    return false;
  }
  if (!rel || !rel[0]) {
    memcpy(out, "/moth", 6);
    return true;
  }
  if (rel[0] == '/' || rel[0] == '\\') {
    return false;
  }
  int len = 5;
  memcpy(out, "/moth", 5);
  out[5] = 0;
  const char *p = rel;
  while (*p) {
    const char *slash = p;
    while (*slash && *slash != '/' && *slash != '\\') {
      slash++;
    }
    int n = (int)(slash - p);
    if (!segmentOk(p, n)) {
      return false;
    }
    if (len + 1 + n >= outLen) {
      return false;
    }
    out[len++] = '/';
    memcpy(out + len, p, (size_t)n);
    len += n;
    out[len] = 0;
    if (*slash == '\\') {
      return false;
    }
    if (*slash == '/') {
      p = slash + 1;
      if (!*p) {
        return false;
      }
    } else {
      break;
    }
  }
  return true;
}

static const char *skipLine(const char *p) {
  while (*p && *p != '\n') {
    p++;
  }
  if (*p == '\n') {
    p++;
  }
  return p;
}

bool wifiParseHead(const char *head, WifiHead *out) {
  if (!head || !out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->contentLength = -1;
  const char *sp = strchr(head, ' ');
  if (!sp || sp == head) {
    return false;
  }
  int mlen = (int)(sp - head);
  if (mlen < 3 || mlen > 6) {
    return false;
  }
  memcpy(out->method, head, (size_t)mlen);
  out->method[mlen] = 0;
  const char *path = sp + 1;
  const char *sp2 = strchr(path, ' ');
  if (!sp2 || sp2 == path) {
    return false;
  }
  int plen = (int)(sp2 - path);
  if (plen < 1 || plen >= (int)sizeof(out->target)) {
    return false;
  }
  int q = plen;
  for (int i = 0; i < plen; i++) {
    if (path[i] == '?') {
      q = i;
      break;
    }
  }
  if (q < 1) {
    return false;
  }
  memcpy(out->target, path, (size_t)q);
  out->target[q] = 0;
  const char *line = skipLine(head);
  while (*line && *line != '\r' && *line != '\n') {
    if (strncmp(line, "Content-Length:", 15) == 0) {
      const char *n = line + 15;
      while (*n == ' ') {
        n++;
      }
      int v = 0;
      if (*n < '0' || *n > '9') {
        return false;
      }
      while (*n >= '0' && *n <= '9') {
        v = v * 10 + (*n - '0');
        if (v > 8 * 1024 * 1024) {
          return false;
        }
        n++;
      }
      out->contentLength = v;
    }
    line = skipLine(line);
  }
  return true;
}

WifiAct wifiClassify(const WifiHead &head, char *path, int pathLen) {
  if (path && pathLen > 0) {
    path[0] = 0;
  }
  bool get = strcmp(head.method, "GET") == 0;
  bool put = strcmp(head.method, "PUT") == 0;
  bool del = strcmp(head.method, "DELETE") == 0;
  if (get && (strcmp(head.target, "/") == 0 || strcmp(head.target, "/index.html") == 0)) {
    return WIFI_ACT_INDEX;
  }
  if (!(get || put || del)) {
    return WIFI_ACT_BAD;
  }
  if (strncmp(head.target, "/moth", 5) != 0) {
    return WIFI_ACT_BAD;
  }
  const char *rel = head.target + 5;
  if (*rel == '/') {
    rel++;
  } else if (*rel != 0) {
    return WIFI_ACT_BAD;
  }
  if (put || del) {
    if (!rel[0]) {
      return WIFI_ACT_BAD;
    }
  }
  if (!path || !wifiMothPath(rel, path, pathLen)) {
    return WIFI_ACT_BAD;
  }
  if (strcmp(path, "/moth") == 0) {
    return get ? WIFI_ACT_INDEX : WIFI_ACT_BAD;
  }
  return WIFI_ACT_FILE;
}
