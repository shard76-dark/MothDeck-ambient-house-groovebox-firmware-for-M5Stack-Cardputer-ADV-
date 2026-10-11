#include "WifiFiles.h"
#include <cstdio>
#include <cstring>

static int failures = 0;

static void expect(bool ok, const char *message) {
  if (ok) {
    std::printf("ok  %s\n", message);
    return;
  }
  std::printf("FAIL %s\n", message);
  failures++;
}

int main() {
  char path[96];
  expect(wifiMothPath(nullptr, path, (int)sizeof(path)) && std::strcmp(path, "/moth") == 0, "an empty path is /moth");
  expect(wifiMothPath("drums/kick.wav", path, (int)sizeof(path)) && std::strcmp(path, "/moth/drums/kick.wav") == 0,
         "a relative path joins /moth");
  expect(!wifiMothPath("/moth/drums", path, (int)sizeof(path)), "an absolute path is rejected");
  expect(!wifiMothPath("../secret", path, (int)sizeof(path)), "a parent segment is rejected");
  expect(!wifiMothPath("drums/../../x", path, (int)sizeof(path)), "a nested parent is rejected");
  expect(!wifiMothPath("drums\\kick.wav", path, (int)sizeof(path)), "a backslash is rejected");
  expect(!wifiMothPath("bad name.wav", path, (int)sizeof(path)), "a space is rejected");
  expect(!wifiMothPath("drums/", path, (int)sizeof(path)), "a trailing slash is rejected");

  const char *head =
      "GET /moth/sfx/blip.wav?x=1 HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 12\r\n\r\nbody";
  WifiHead req;
  expect(wifiParseHead(head, &req), "a request head parses");
  expect(std::strcmp(req.method, "GET") == 0, "the method is GET");
  expect(std::strcmp(req.target, "/moth/sfx/blip.wav") == 0, "the query is not part of the path");
  expect(req.contentLength == 12, "content length is kept");
  expect(wifiClassify(req, path, (int)sizeof(path)) == WIFI_ACT_FILE && std::strcmp(path, "/moth/sfx/blip.wav") == 0,
         "GET under /moth is a file");

  const char *index = "GET / HTTP/1.0\r\n\r\n";
  expect(wifiParseHead(index, &req) && wifiClassify(req, path, (int)sizeof(path)) == WIFI_ACT_INDEX,
         "GET / is the index");

  const char *put = "PUT /moth/loops/house.wav HTTP/1.0\r\nContent-Length: 4\r\n\r\n";
  expect(wifiParseHead(put, &req) && wifiClassify(req, path, (int)sizeof(path)) == WIFI_ACT_FILE, "PUT is a file write");
  expect(std::strcmp(path, "/moth/loops/house.wav") == 0, "PUT keeps the card path");

  const char *rootDel = "DELETE /moth HTTP/1.0\r\n\r\n";
  expect(wifiParseHead(rootDel, &req) && wifiClassify(req, path, (int)sizeof(path)) == WIFI_ACT_BAD,
         "DELETE /moth is rejected");
  const char *rootGet = "GET /moth HTTP/1.0\r\n\r\n";
  expect(wifiParseHead(rootGet, &req) && wifiClassify(req, path, (int)sizeof(path)) == WIFI_ACT_INDEX,
         "GET /moth is the index");

  const char *escape = "GET /moth/../boot.bin HTTP/1.0\r\n\r\n";
  expect(wifiParseHead(escape, &req) && wifiClassify(req, path, (int)sizeof(path)) == WIFI_ACT_BAD,
         "a path that leaves /moth is rejected");

  const char *other = "GET /secret HTTP/1.0\r\n\r\n";
  expect(wifiParseHead(other, &req) && wifiClassify(req, path, (int)sizeof(path)) == WIFI_ACT_BAD,
         "a path outside /moth is rejected");

  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
