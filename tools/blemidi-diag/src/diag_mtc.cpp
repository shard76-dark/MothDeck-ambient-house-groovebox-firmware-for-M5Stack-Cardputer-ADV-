#include "diag_mtc.h"

#include <string.h>

void diagMtcReset(DiagMtcAsm *asmState) {
  if (!asmState) {
    return;
  }
  memset(asmState, 0, sizeof(*asmState));
  asmState->rateCode = -1;
  asmState->hours = -1;
}

const char *diagMtcRateName(int rateCode) {
  if (rateCode == 0) {
    return "24";
  }
  if (rateCode == 1) {
    return "25";
  }
  if (rateCode == 2) {
    return "29.97";
  }
  if (rateCode == 3) {
    return "30";
  }
  return "";
}

static void decode(const DiagMtcAsm *asmState, DiagMtcEvent *out) {
  out->frames = (int)(((asmState->nib[1] & 0x01) << 4) | (asmState->nib[0] & 0x0F));
  out->seconds = (int)(((asmState->nib[3] & 0x03) << 4) | (asmState->nib[2] & 0x0F));
  out->minutes = (int)(((asmState->nib[5] & 0x03) << 4) | (asmState->nib[4] & 0x0F));
  out->hours = (int)(((asmState->nib[7] & 0x01) << 4) | (asmState->nib[6] & 0x0F));
  out->rateCode = (int)((asmState->nib[7] >> 1) & 0x03);
  out->hasPos = true;
}

bool diagMtcQuarter(DiagMtcAsm *asmState, uint8_t byte, uint64_t us, DiagMtcEvent *out) {
  if (!asmState || !out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->rateCode = -1;
  byte = (uint8_t)(byte & 0x7F);
  int piece = (byte >> 4) & 0x07;
  int nibble = byte & 0x0F;
  out->quarter = true;
  out->piece = piece;
  out->nibble = nibble;
  if (asmState->lastQfUs != 0 && us >= asmState->lastQfUs) {
    uint64_t gap = us - asmState->lastQfUs;
    if (gap > 0xFFFFFFFFull) {
      gap = 0xFFFFFFFFull;
    }
    out->hasGap = true;
    out->gapUs = (uint32_t)gap;
    asmState->gapSum += gap;
    asmState->gapN++;
    if ((uint32_t)gap > asmState->gapMax) {
      asmState->gapMax = (uint32_t)gap;
    }
  }
  asmState->lastQfUs = us;
  asmState->qfWindow++;
  if (piece == 0) {
    asmState->got = 0;
  }
  asmState->nib[piece] = (uint8_t)nibble;
  asmState->got = (uint8_t)(asmState->got | (1u << piece));
  if (piece == 7 && asmState->got == 0xFF) {
    decode(asmState, out);
    out->complete = true;
    asmState->hours = out->hours;
    asmState->minutes = out->minutes;
    asmState->seconds = out->seconds;
    asmState->frames = out->frames;
    asmState->rateCode = out->rateCode;
    asmState->hasPos = true;
    asmState->got = 0;
  }
  return true;
}

bool diagMtcFull(const uint8_t *payload, int len, uint64_t us, DiagMtcAsm *asmState, DiagMtcEvent *out) {
  if (!payload || len < 8 || !out) {
    return false;
  }
  if (payload[0] != 0x7F || payload[2] != 0x01 || payload[3] != 0x01) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  uint8_t hr = (uint8_t)(payload[4] & 0x7F);
  out->full = true;
  out->hours = hr & 0x1F;
  out->rateCode = (hr >> 5) & 0x03;
  out->minutes = payload[5] & 0x3F;
  out->seconds = payload[6] & 0x3F;
  out->frames = payload[7] & 0x1F;
  out->hasPos = true;
  out->complete = true;
  if (asmState) {
    if (asmState->lastFullUs != 0 && us >= asmState->lastFullUs) {
      uint64_t gap = us - asmState->lastFullUs;
      if (gap > 0xFFFFFFFFull) {
        gap = 0xFFFFFFFFull;
      }
      out->hasGap = true;
      out->gapUs = (uint32_t)gap;
    }
    asmState->lastFullUs = us;
    asmState->fullWindow++;
    asmState->hours = out->hours;
    asmState->minutes = out->minutes;
    asmState->seconds = out->seconds;
    asmState->frames = out->frames;
    asmState->rateCode = out->rateCode;
    asmState->hasPos = true;
  }
  return true;
}
