#include "WifiPortal.h"
#include "WifiFiles.h"
#include "SdCard.h"
#include <WiFi.h>
#include <Arduino.h>
#include <string.h>

static WiFiServer portal(80);
static bool on = false;
static char statusLine[20] = "off";
static const int kUploadMax = 2 * 1024 * 1024;
static const char *kTemp = "/moth/_upload.tmp";

const char *wifiPortalStatus() {
  return statusLine;
}

bool wifiPortalUp() {
  return on;
}

static void radioOff() {
  portal.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  on = false;
  memcpy(statusLine, "off", 4);
}

bool wifiPortalToggle() {
  if (on) {
    radioOff();
    return true;
  }
  size_t blk = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (blk < 48 * 1024) {
    return false;
  }
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP("Mothdeck", "mothdeck")) {
    radioOff();
    return false;
  }
  portal.begin();
  IPAddress ip = WiFi.softAPIP();
  snprintf(statusLine, sizeof(statusLine), "%s", ip.toString().c_str());
  on = true;
  return true;
}

static bool ensureParents(const char *path) {
  char buf[96];
  int n = (int)strlen(path);
  if (n <= 1 || n >= (int)sizeof(buf)) {
    return false;
  }
  memcpy(buf, path, (size_t)n + 1);
  for (int i = 1; i < n; i++) {
    if (buf[i] == '/') {
      buf[i] = 0;
      if (!sdCard.Mkdir(buf)) {
        return false;
      }
      buf[i] = '/';
    }
  }
  return true;
}

static void sendText(WiFiClient &client, int code, const char *body) {
  client.printf("HTTP/1.0 %d OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s", code, body ? body : "");
}

static void sendIndex(WiFiClient &client) {
  static const char *dirs[] = {"/moth", "/moth/drums", "/moth/loops", "/moth/instruments", "/moth/sfx"};
  client.print("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
  client.print("<!doctype html><title>MothDeck</title><h1>MothDeck /moth</h1>");
  client.print("<p>Password mothdeck. A file input below uses PUT and does not replace a file until the upload finishes.</p>");
  client.print("<input id=f type=file><input id=p size=28 placeholder=drums/kick.wav>");
  client.print("<button onclick=\"var file=f.files[0];if(!file||!p.value)return;");
  client.print("fetch('/moth/'+p.value,{method:'PUT',body:file}).then(function(r){alert(r.status)})\" >Upload</button><hr>");
  if (!sdCard.Mounted() && !sdCard.Ensure()) {
    client.print("<p>No SD card</p>");
    return;
  }
  char names[12][24];
  for (int d = 0; d < 5; d++) {
    int n = sdCard.List(dirs[d], names, 12, false);
    for (int i = 0; i < n; i++) {
      if (strcmp(names[i], "_upload.tmp") == 0) {
        continue;
      }
      const char *rel = dirs[d] + 6;
      client.printf("<a href=\"%s/%s\">", dirs[d], names[i]);
      if (rel[0]) {
        client.printf("%s/", rel);
      }
      client.printf("%s</a><br>", names[i]);
    }
  }
}

static void sendFile(WiFiClient &client, const char *path) {
  int size = 0;
  if (!sdCard.SizeOf(path, &size) || size < 0) {
    sendText(client, 404, "missing");
    return;
  }
  client.printf("HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                size);
  uint8_t buf[512];
  int off = 0;
  while (off < size) {
    int n = 0;
    int ask = size - off;
    if (ask > (int)sizeof(buf)) {
      ask = (int)sizeof(buf);
    }
    if (!sdCard.ReadAt(path, (uint32_t)off, buf, ask, &n) || n <= 0) {
      break;
    }
    if (client.write(buf, (size_t)n) != (size_t)n) {
      break;
    }
    off += n;
    delay(0);
  }
}

static bool readBody(WiFiClient &client, int expect, const char *temp) {
  uint8_t buf[512];
  int got = 0;
  uint32_t last = millis();
  while (got < expect) {
    int avail = client.available();
    if (avail <= 0) {
      if ((uint32_t)(millis() - last) > 2000) {
        return false;
      }
      delay(1);
      continue;
    }
    last = millis();
    int ask = expect - got;
    if (ask > avail) {
      ask = avail;
    }
    if (ask > (int)sizeof(buf)) {
      ask = (int)sizeof(buf);
    }
    int n = client.read(buf, (size_t)ask);
    if (n <= 0) {
      return false;
    }
    if (!sdCard.WriteAt(temp, (uint32_t)got, buf, n)) {
      return false;
    }
    got += n;
    delay(0);
  }
  return got == expect;
}

static void takePut(WiFiClient &client, const char *path, int length) {
  if (length < 0 || length > kUploadMax) {
    sendText(client, 400, "length");
    return;
  }
  if (!ensureParents(path) || !ensureParents(kTemp)) {
    sendText(client, 400, "folder");
    return;
  }
  sdCard.Remove(kTemp);
  if (length == 0) {
    if (!sdCard.WriteAt(path, 0, (const uint8_t *)"", 0)) {
      sendText(client, 400, "write");
      return;
    }
    sendText(client, 200, "stored");
    return;
  }
  if (!readBody(client, length, kTemp)) {
    sdCard.Remove(kTemp);
    sendText(client, 400, "short");
    return;
  }
  if (!sdCard.Rename(kTemp, path)) {
    sdCard.Remove(kTemp);
    sendText(client, 400, "rename");
    return;
  }
  sendText(client, 200, "stored");
}

static void handle(WiFiClient &client) {
  char head[640];
  int n = 0;
  uint32_t start = millis();
  while (n < (int)sizeof(head) - 1 && (uint32_t)(millis() - start) < 1500) {
    int c = client.read();
    if (c < 0) {
      if (!client.connected()) {
        break;
      }
      delay(1);
      continue;
    }
    head[n++] = (char)c;
    head[n] = 0;
    if (n >= 4 && head[n - 4] == '\r' && head[n - 3] == '\n' && head[n - 2] == '\r' && head[n - 1] == '\n') {
      break;
    }
  }
  WifiHead req;
  char path[96];
  if (!wifiParseHead(head, &req)) {
    sendText(client, 400, "bad");
    return;
  }
  WifiAct act = wifiClassify(req, path, (int)sizeof(path));
  if (act == WIFI_ACT_INDEX && strcmp(req.method, "GET") == 0) {
    sendIndex(client);
    return;
  }
  if (act != WIFI_ACT_FILE) {
    sendText(client, 400, "path");
    return;
  }
  if (strcmp(req.method, "GET") == 0) {
    sendFile(client, path);
    return;
  }
  if (strcmp(req.method, "DELETE") == 0) {
    if (!sdCard.Remove(path)) {
      sendText(client, 404, "missing");
      return;
    }
    sendText(client, 200, "deleted");
    return;
  }
  if (strcmp(req.method, "PUT") == 0) {
    takePut(client, path, req.contentLength);
    return;
  }
  sendText(client, 400, "method");
}

void wifiPortalPoll() {
  if (!on) {
    return;
  }
  WiFiClient client = portal.accept();
  if (!client) {
    return;
  }
  client.setTimeout(2);
  handle(client);
  client.stop();
}
