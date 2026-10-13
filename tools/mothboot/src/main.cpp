#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <esp_ota_ops.h>
#include <string.h>
#include <stdlib.h>

#include "BoardConfig.h"
#include "FatPlan.h"
#include "MothBootJump.h"
#include "MothCrc.h"
#include "SdLimit.h"
#include "screen.h"
#include "sd_ops.h"
#include "version.h"

SET_LOOP_TASK_STACK_SIZE(16384);

static const uint32_t kXferChunk = 4096;
static uint8_t gXfer[kXferChunk];
static char gLine[1024];

static void emit(const char *line) {
  if (line) {
    Serial.println(line);
  }
}

static int readExact(uint8_t *dst, int n) {
  int got = 0;
  uint32_t last = millis();
  while (got < n) {
    int avail = Serial.available();
    if (avail > 0) {
      int want = n - got;
      if (want > avail) {
        want = avail;
      }
      while (want > 0) {
        int c = Serial.read();
        if (c < 0) {
          break;
        }
        dst[got++] = (uint8_t)c;
        want--;
        last = millis();
      }
      continue;
    }
    if ((uint32_t)(millis() - last) > 20000) {
      return got;
    }
    delay(1);
  }
  return got;
}

static void emitReady(const char *kind) {
  snprintf(gLine, sizeof(gLine), "{\"t\":\"%s\",\"phase\":\"ready\",\"chunk\":%u}", kind, (unsigned)kXferChunk);
  emit(gLine);
  Serial.flush();
}

static void emitAck(const char *kind, uint32_t got, int pct) {
  snprintf(
    gLine,
    sizeof(gLine),
    "{\"t\":\"%s\",\"phase\":\"ack\",\"got\":%lu,\"pct\":%d}",
    kind,
    (unsigned long)got,
    pct);
  emit(gLine);
  Serial.flush();
}

static const esp_partition_t *slotPart(int slot) {
  esp_partition_subtype_t sub = slot == 1 ? ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0;
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, sub, nullptr);
}

static void readImageTag(const esp_partition_t *part, char *dst, int n) {
  if (dst && n > 0) {
    snprintf(dst, n, "empty");
  }
  if (!part || !dst || n < 2) {
    return;
  }
  uint8_t buf[160];
  const char *key = "MOTHIMG:";
  for (uint32_t off = 0; off + sizeof(buf) < part->size && off < 262144u; off += 96) {
    if (esp_partition_read(part, off, buf, sizeof(buf)) != ESP_OK) {
      return;
    }
    for (int i = 0; i + 12 < (int)sizeof(buf); i++) {
      if (memcmp(buf + i, key, 8) != 0) {
        continue;
      }
      int k = 0;
      for (int j = i + 8; j < (int)sizeof(buf) && k + 1 < n; j++) {
        char c = (char)buf[j];
        if (c <= 32 || c > 126) {
          break;
        }
        dst[k++] = c;
      }
      dst[k] = 0;
      if (k > 0) {
        return;
      }
    }
  }
}

static bool bootSlot(int slot) {
  const esp_partition_t *part = slotPart(slot);
  if (!part) {
    return false;
  }
  uint8_t magic = 0;
  if (esp_partition_read(part, 0, &magic, 1) != ESP_OK || magic != 0xE9) {
    bootScreenShow("Slot is empty", slot == 1 ? "B" : "A", "", -1);
    return false;
  }
  char tag[24];
  readImageTag(part, tag, (int)sizeof(tag));
  bootScreenShow("Restarting", tag, "", 100);
  if (esp_ota_set_boot_partition(part) != ESP_OK) {
    bootScreenShow("Try again", tag, "", -1);
    return false;
  }
  emit("{\"t\":\"boot\",\"phase\":\"restarting\"}");
  Serial.flush();
  delay(200);
  esp_restart();
  return true;
}

static bool receiveImage(const esp_partition_t *part, uint32_t size, uint32_t expectCrc, const char *toVer) {
  char from[24];
  readImageTag(part, from, (int)sizeof(from));
  if (!part || size < 16 || size > part->size) {
    bootScreenShow("Try again", toVer, "", -1);
    emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
    return false;
  }
  esp_ota_handle_t handle = 0;
  if (esp_ota_begin(part, size, &handle) != ESP_OK) {
    bootScreenShow("Try again", toVer, "", -1);
    emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
    return false;
  }
  emitReady("flash");
  uint32_t got = 0;
  uint32_t crc = 0;
  int lastPct = -1;
  bool ioOk = true;
  while (got < size) {
    uint32_t chunk = size - got;
    if (chunk > kXferChunk) {
      chunk = kXferChunk;
    }
    int n = readExact(gXfer, (int)chunk);
    if (n != (int)chunk) {
      ioOk = false;
      break;
    }
    if (got == 0 && gXfer[0] != 0xE9) {
      ioOk = false;
      break;
    }
    if (esp_ota_write(handle, gXfer, (size_t)n) != ESP_OK) {
      ioOk = false;
      break;
    }
    crc = mothCrc32Update(crc, gXfer, (size_t)n);
    got += (uint32_t)n;
    int pct = (int)((uint64_t)got * 100ull / size);
    if (pct != lastPct) {
      char detail[48];
      snprintf(detail, sizeof(detail), "%s to %s", from, toVer && toVer[0] ? toVer : "app");
      bootScreenShow("Updating", detail, "", pct);
      lastPct = pct;
    }
    if (got < size) {
      emitAck("flash", got, pct);
    }
  }
  if (!ioOk || crc != expectCrc) {
    esp_ota_abort(handle);
    bootScreenShow("Try again", toVer, "", -1);
    emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
    return false;
  }
  bootScreenShow("Verifying", toVer, "", 100);
  if (esp_ota_end(handle) != ESP_OK) {
    bootScreenShow("Try again", toVer, "", -1);
    emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
    return false;
  }
  bootScreenShow("Restarting", toVer, "", 100);
  if (esp_ota_set_boot_partition(part) != ESP_OK) {
    bootScreenShow("Try again", toVer, "", -1);
    emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
    return false;
  }
  snprintf(gLine, sizeof(gLine), "{\"t\":\"flash\",\"phase\":\"restarting\",\"version\":\"%s\"}", toVer ? toVer : "");
  emit(gLine);
  Serial.flush();
  delay(250);
  esp_restart();
  return true;
}

static bool pathOk(const char *path) {
  if (!path || path[0] != '/' || strlen(path) < 2 || strlen(path) > 63) {
    return false;
  }
  return strstr(path, "..") == nullptr;
}

static bool volumeAllowsWrites() {
  if (!sdMountFiles()) {
    bootScreenShow("Insert a card", "", "", -1);
    return false;
  }
  if (!mothSdVolumeOk(SD.totalBytes())) {
    bootScreenShow("Check the card", "", "", -1);
    return false;
  }
  return true;
}

static void onCheck(int percent, const char *phase, uint64_t bytes, uint64_t reported) {
  char detail[48];
  unsigned mb = (unsigned)(bytes / (1024ull * 1024ull));
  snprintf(detail, sizeof(detail), "%u MB", mb);
  bootScreenShow(phase ? phase : "Checking card", detail, "", percent);
  snprintf(
    gLine,
    sizeof(gLine),
    "{\"t\":\"sd\",\"phase\":\"%s\",\"percent\":%d,\"bytes\":%llu,\"reported\":%llu}",
    phase ? phase : "check",
    percent,
    (unsigned long long)bytes,
    (unsigned long long)reported);
  emit(gLine);
}

static void cmdStatus() {
  char a[24];
  char b[24];
  readImageTag(slotPart(0), a, (int)sizeof(a));
  readImageTag(slotPart(1), b, (int)sizeof(b));
  uint64_t verified = mothSdVerifiedBytes();
  snprintf(
    gLine,
    sizeof(gLine),
    "{\"t\":\"status\",\"name\":\"MothBoot\",\"version\":\"%s\",\"slot_a\":\"%s\",\"slot_b\":\"%s\",\"verified\":%llu}",
    kMothBootVersion,
    a,
    b,
    (unsigned long long)verified);
  emit(gLine);
  bootScreenShow("USB or keys", "1 A   2 B   S apps", "C check needs USB", -1);
}

static void cmdLs(const char *path) {
  if (!path || !path[0]) {
    path = "/apps";
  }
  if (!pathOk(path) || !sdMountFiles()) {
    emit("{\"t\":\"sd\",\"phase\":\"retry\"}");
    bootScreenShow("Insert a card", "", "", -1);
    return;
  }
  File dir = SD.open(path);
  if (!dir) {
    emit("{\"t\":\"sd\",\"phase\":\"retry\"}");
    return;
  }
  char names[384];
  names[0] = 0;
  int count = 0;
  File entry = dir.openNextFile();
  while (entry && count < 12) {
    const char *name = entry.name();
    if (name && name[0]) {
      if (names[0]) {
        strncat(names, ",", sizeof(names) - strlen(names) - 1);
      }
      strncat(names, "\"", sizeof(names) - strlen(names) - 1);
      strncat(names, name, sizeof(names) - strlen(names) - 1);
      strncat(names, "\"", sizeof(names) - strlen(names) - 1);
      count++;
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  snprintf(gLine, sizeof(gLine), "{\"t\":\"sd\",\"phase\":\"list\",\"path\":\"%s\",\"names\":[%s]}", path, names);
  emit(gLine);
}

static void cmdPut(uint32_t size, uint32_t expectCrc, const char *path) {
  if (!pathOk(path) || size > 6u * 1024u * 1024u || !volumeAllowsWrites()) {
    emit("{\"t\":\"sd\",\"phase\":\"retry\"}");
    return;
  }
  if (strncmp(path, "/apps/", 6) == 0) {
    SD.mkdir("/apps");
  }
  if (strncmp(path, "/moth/", 6) == 0) {
    SD.mkdir("/moth");
  }
  SD.remove(path);
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    emit("{\"t\":\"sd\",\"phase\":\"retry\"}");
    bootScreenShow("Try again", path, "", -1);
    return;
  }
  emitReady("sd");
  uint32_t got = 0;
  uint32_t crc = 0;
  int lastPct = -1;
  bool ok = true;
  while (got < size) {
    uint32_t chunk = size - got;
    if (chunk > kXferChunk) {
      chunk = kXferChunk;
    }
    int n = readExact(gXfer, (int)chunk);
    if (n != (int)chunk || file.write(gXfer, (size_t)n) != (size_t)n) {
      ok = false;
      break;
    }
    crc = mothCrc32Update(crc, gXfer, (size_t)n);
    got += (uint32_t)n;
    int pct = (int)((uint64_t)got * 100ull / size);
    if (pct != lastPct) {
      bootScreenShow("Copying", path, "", pct);
      lastPct = pct;
    }
    if (got < size) {
      emitAck("sd", got, pct);
    }
  }
  file.close();
  if (!ok || crc != expectCrc) {
    SD.remove(path);
    bootScreenShow("Try again", path, "", -1);
    emit("{\"t\":\"sd\",\"phase\":\"retry\"}");
    return;
  }
  bootScreenShow("Card ready", path, "", 100);
  snprintf(gLine, sizeof(gLine), "{\"t\":\"sd\",\"phase\":\"done\",\"path\":\"%s\",\"bytes\":%lu}", path, (unsigned long)size);
  emit(gLine);
}

static void cmdInstall(const char *path, int slot) {
  if (!pathOk(path) || !sdMountFiles()) {
    emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
    bootScreenShow("Insert a card", "", "", -1);
    return;
  }
  File file = SD.open(path, FILE_READ);
  const esp_partition_t *part = slotPart(slot);
  if (!file || !part) {
    emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
    bootScreenShow("Try again", path, "", -1);
    return;
  }
  uint32_t size = (uint32_t)file.size();
  if (size < 16 || size > part->size) {
    file.close();
    emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
    bootScreenShow("Try again", path, "", -1);
    return;
  }
  esp_ota_handle_t handle = 0;
  if (esp_ota_begin(part, size, &handle) != ESP_OK) {
    file.close();
    emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
    return;
  }
  uint8_t buf[2048];
  uint32_t got = 0;
  int lastPct = -1;
  bool ok = true;
  while (got < size) {
    uint32_t chunk = size - got;
    if (chunk > sizeof(buf)) {
      chunk = sizeof(buf);
    }
    int n = file.read(buf, chunk);
    if (n != (int)chunk || (got == 0 && buf[0] != 0xE9) || esp_ota_write(handle, buf, (size_t)n) != ESP_OK) {
      ok = false;
      break;
    }
    got += (uint32_t)n;
    int pct = (int)((uint64_t)got * 100ull / size);
    if (pct != lastPct) {
      bootScreenShow("Updating", path, "", pct);
      lastPct = pct;
    }
  }
  file.close();
  if (!ok || esp_ota_end(handle) != ESP_OK) {
    if (!ok) {
      esp_ota_abort(handle);
    }
    bootScreenShow("Try again", path, "", -1);
    emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
    return;
  }
  bootScreenShow("Restarting", path, "", 100);
  if (esp_ota_set_boot_partition(part) != ESP_OK) {
    emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
    return;
  }
  emit("{\"t\":\"flash\",\"phase\":\"restarting\"}");
  Serial.flush();
  delay(250);
  esp_restart();
}

static void handleCommand(char *line) {
  while (*line == ' ') {
    line++;
  }
  size_t n = strlen(line);
  while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) {
    line[--n] = 0;
  }
  if (n == 0) {
    return;
  }
  for (size_t i = 0; i < n; i++) {
    if (line[i] >= 'A' && line[i] <= 'Z') {
      line[i] = (char)(line[i] - 'A' + 'a');
    }
  }
  if (strcmp(line, "help") == 0) {
    snprintf(
      gLine,
      sizeof(gLine),
      "{\"t\":\"help\",\"name\":\"MothBoot\",\"version\":\"%s\",\"commands\":[\"status\",\"flash\",\"sd-put\",\"sd-ls\",\"sd-rm\",\"sd-df\",\"sd-install\",\"sd-check\",\"sd-check full\",\"sd-format\",\"boot\"]}",
      kMothBootVersion);
    emit(gLine);
    return;
  }
  if (strcmp(line, "status") == 0) {
    cmdStatus();
    return;
  }
  if (strcmp(line, "sd-df") == 0) {
    uint64_t verified = mothSdVerifiedBytes();
    uint64_t total = sdVolumeBytes();
    uint64_t freeB = sdFreeBytes();
    snprintf(
      gLine,
      sizeof(gLine),
      "{\"t\":\"sd\",\"phase\":\"space\",\"total\":%llu,\"free\":%llu,\"verified\":%llu}",
      (unsigned long long)total,
      (unsigned long long)freeB,
      (unsigned long long)verified);
    emit(gLine);
    return;
  }
  if (strcmp(line, "sd-ls") == 0) {
    cmdLs("/apps");
    return;
  }
  if (strncmp(line, "sd-ls ", 6) == 0) {
    cmdLs(line + 6);
    return;
  }
  if (strncmp(line, "sd-rm ", 6) == 0) {
    if (!pathOk(line + 6) || !volumeAllowsWrites()) {
      emit("{\"t\":\"sd\",\"phase\":\"retry\"}");
      return;
    }
    bool removed = SD.remove(line + 6);
    snprintf(gLine, sizeof(gLine), "{\"t\":\"sd\",\"phase\":\"%s\",\"path\":\"%s\"}", removed ? "done" : "retry", line + 6);
    emit(gLine);
    return;
  }
  if (strcmp(line, "sd-check") == 0 || strcmp(line, "sd-check full") == 0) {
    uint64_t verified = 0;
    uint64_t reported = 0;
    uint32_t bps = 0;
    bool full = strcmp(line, "sd-check full") == 0;
    if (!full) {
      emit("{\"t\":\"sd\",\"phase\":\"retry\",\"why\":\"use full\"}");
      bootScreenShow("Use full check", "sd-check full", "", -1);
      return;
    }
    bootScreenShow("Checking card", "Whole card", "", 0);
    bool ok = sdCheckFull(onCheck, &verified, &reported, &bps);
    snprintf(
      gLine,
      sizeof(gLine),
      "{\"t\":\"sd\",\"phase\":\"%s\",\"bytes\":%llu,\"reported\":%llu,\"bps\":%lu,\"margin\":%llu}",
      ok ? "done" : "retry",
      (unsigned long long)verified,
      (unsigned long long)reported,
      (unsigned long)bps,
      (unsigned long long)kFatMarginBytes);
    emit(gLine);
    if (ok) {
      unsigned mb = (unsigned)(verified / (1024ull * 1024ull));
      char detail[32];
      snprintf(detail, sizeof(detail), "%u MB", mb);
      bootScreenShow("Card checked", detail, "sd-format next", 100);
    }
    return;
  }
  if (strcmp(line, "sd-format") == 0 || strncmp(line, "sd-format ", 10) == 0) {
    uint64_t requested = 0;
    if (line[9] == ' ') {
      requested = strtoull(line + 10, nullptr, 10);
    }
    bool ok = sdFormatCard(requested, onCheck);
    uint64_t verified = mothSdVerifiedBytes();
    snprintf(
      gLine,
      sizeof(gLine),
      "{\"t\":\"sd\",\"phase\":\"%s\",\"verified\":%llu,\"volume\":%llu}",
      ok ? "formatted" : "retry",
      (unsigned long long)verified,
      ok ? (unsigned long long)sdVolumeBytes() : 0ull);
    emit(gLine);
    return;
  }
  if (strncmp(line, "boot", 4) == 0) {
    int slot = 0;
    if (line[4] == ' ' && line[5] == '1') {
      slot = 1;
    }
    bootSlot(slot);
    return;
  }
  if (strncmp(line, "sd-install ", 11) == 0) {
    char path[64];
    int slot = 0;
    const char *rest = line + 11;
    const char *sp = strrchr(rest, ' ');
    if (sp && sp[1] >= '0' && sp[1] <= '1' && sp[2] == 0) {
      slot = sp[1] - '0';
      size_t len = (size_t)(sp - rest);
      if (len >= sizeof(path)) {
        len = sizeof(path) - 1;
      }
      memcpy(path, rest, len);
      path[len] = 0;
    } else {
      strncpy(path, rest, sizeof(path) - 1);
      path[sizeof(path) - 1] = 0;
    }
    cmdInstall(path, slot);
    return;
  }
  if (strncmp(line, "flash ", 6) == 0 || strncmp(line, "sd-put ", 7) == 0) {
    bool put = line[0] == 's';
    char *cursor = line + (put ? 7 : 6);
    char *sizeEnd = nullptr;
    unsigned long size = strtoul(cursor, &sizeEnd, 10);
    if (sizeEnd == cursor || *sizeEnd != ' ') {
      emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
      return;
    }
    char *crcEnd = nullptr;
    unsigned long crc = strtoul(sizeEnd + 1, &crcEnd, 0);
    if (crcEnd == sizeEnd + 1 || *crcEnd != ' ') {
      emit("{\"t\":\"flash\",\"phase\":\"retry\"}");
      return;
    }
    char *ver = crcEnd + 1;
    if (put) {
      cmdPut((uint32_t)size, (uint32_t)crc, ver);
      return;
    }
    int slot = 0;
    char *sp = strrchr(ver, ' ');
    if (sp && sp[1] >= '0' && sp[1] <= '1' && sp[2] == 0) {
      slot = sp[1] - '0';
      *sp = 0;
    }
    receiveImage(slotPart(slot), (uint32_t)size, (uint32_t)crc, ver);
    return;
  }
  emit("{\"t\":\"cmd\",\"phase\":\"retry\"}");
}

static void pollSerial() {
  static char cmd[160];
  static size_t used = 0;
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch < 0) {
      return;
    }
    if (ch == '\n') {
      cmd[used] = 0;
      used = 0;
      handleCommand(cmd);
      continue;
    }
    if (ch == '\r') {
      continue;
    }
    if (used + 1 < sizeof(cmd)) {
      cmd[used++] = (char)ch;
    } else {
      used = 0;
    }
  }
}

static void pollKeys() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    if (M5Cardputer.Keyboard.isKeyPressed('1')) {
      bootSlot(0);
    } else if (M5Cardputer.Keyboard.isKeyPressed('2')) {
      bootSlot(1);
    } else if (M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) {
      cmdLs("/apps");
      bootScreenShow("SD apps", "/apps", "", -1);
    }
  }
}

void setup() {
  // One chunk is 4096 bytes. The USB-Serial-JTAG FIFO is much smaller, so
  // the software queue has to hold a chunk while the previous one is written.
  if (Serial.setRxBufferSize(8192) == 0) {
    Serial.setRxBufferSize(4608);
  }
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);
  mothMarkAppValid();
  bootScreenBegin();
  (void)kMothBootImage;
  delay(200);
  snprintf(
    gLine,
    sizeof(gLine),
    "{\"t\":\"hello\",\"name\":\"MothBoot\",\"version\":\"%s\",\"image\":\"%s\"}",
    kMothBootVersion,
    kMothBootImage);
  emit(gLine);
}

void loop() {
  pollSerial();
  pollKeys();
  delay(4);
}
