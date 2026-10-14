#ifndef ClockTempo_h
#define ClockTempo_h

#include <stdint.h>
#include <string.h>

// BLE MIDI timestamps are 13-bit milliseconds. A repeated timestamp is the
// same clock delivered again. A backwards step lands near 8192 ms and is a
// break in the stream, not elapsed time.

inline uint16_t bleClockDelta(uint16_t prev, uint16_t next) {
  return (uint16_t)(((uint16_t)(next - prev)) & 0x1FFF);
}

// Ticks a gap is worth at `bpm`. One tick is 2500/bpm ms. Clamped to 1..8
// so a thinned clock can cover several ticks and a dropout cannot run away.
inline int bleClockEstimate(uint16_t deltaMs, int bpm) {
  if (bpm < 1) {
    bpm = 1;
  }
  uint32_t num = (uint32_t)deltaMs * (uint32_t)bpm + 1250u;
  int estimate = (int)(num / 2500u);
  if (estimate < 1) {
    estimate = 1;
  } else if (estimate > 8) {
    estimate = 8;
  }
  return estimate;
}

// Running picture of a clock. `smooth` is the tempo used to tell a real tick
// from a burst. Seed it with the deck tempo; 0 means 120 until a real gap
// arrives. Short runs of sub-tick clocks still count (a chord packet). A
// longer run is folded into the smoothed tick so it cannot race the playhead.
struct ClockFold {
  uint16_t lastTs;
  uint8_t have;
  uint16_t burstMs;
  uint8_t fragRun;
  int smooth;
  uint32_t raw;
  uint32_t dup;
  uint32_t burst;
  uint32_t playSteps;
  uint32_t tempoSteps;
  uint32_t spanMs;
  uint32_t arrivalSteps;
  uint64_t arrivalSpanUs;
  uint64_t lastUs;
  uint8_t haveUs;
  uint32_t winSteps;
  uint32_t winMs;
  uint8_t tuned;
};

inline int clockFoldBpm(const ClockFold *fold) {
  if (fold && fold->smooth >= 40 && fold->smooth <= 200) {
    return fold->smooth;
  }
  return 120;
}

// One bar of honest gaps. The first bar snaps, later bars ease, and a source
// faster than the cap lands on 200 in that bar.
inline void clockFoldLearn(ClockFold *fold, int musical, uint16_t charged) {
  if (!fold || musical < 1 || charged == 0) {
    return;
  }
  fold->winSteps += (uint32_t)musical;
  fold->winMs += charged;
  if (fold->winSteps < 24 || fold->winMs == 0) {
    return;
  }
  uint32_t num = 60000u * fold->winSteps + 12u * fold->winMs;
  int raw = (int)(num / (24u * fold->winMs));
  if (raw < 40) {
    raw = 40;
  } else if (raw > 200) {
    raw = 200;
  }
  if (!fold->tuned) {
    fold->smooth = raw;
    fold->tuned = 1;
  } else if (raw >= 200 || fold->smooth >= 200) {
    fold->smooth = raw >= 200 ? 200 : raw;
  } else {
    int next = (fold->smooth * 3 + raw + 2) / 4;
    if (next == fold->smooth && raw != fold->smooth) {
      next += raw > fold->smooth ? 1 : -1;
    }
    if (next < 40) {
      next = 40;
    } else if (next > 200) {
      next = 200;
    }
    fold->smooth = next;
  }
  fold->winSteps = 0;
  fold->winMs = 0;
}

// One clock event. `wire` clocks share `ts`. `us` is the arrival time, or 0
// when the caller has none. Returns how many musical ticks to advance.
inline int clockFoldFeed(ClockFold *fold, uint16_t ts, int wire, uint64_t us) {
  if (!fold || wire < 1) {
    return 0;
  }
  if (wire > 255) {
    wire = 255;
  }
  ts = (uint16_t)(ts & 0x1FFF);
  fold->raw += (uint32_t)wire;
  int musical = 0;
  uint16_t charged = 0;
  if (!fold->have) {
    fold->have = 1;
    fold->lastTs = ts;
    musical = wire > 8 ? 1 : wire;
  } else if (ts == fold->lastTs) {
    fold->dup += (uint32_t)wire;
  } else {
    uint16_t delta = bleClockDelta(fold->lastTs, ts);
    fold->lastTs = ts;
    int bpm = clockFoldBpm(fold);
    // Faster than a 240 BPM tick is a fragment. 240 itself (about 10 ms)
    // still counts, and the tempo clamp brings it back to 200.
    bool fragment = delta > 0 && delta < 2000 && (delta < 6 || (2500 / delta) > 280);
    if (delta == 0 || delta >= 2000) {
      // The clock in hand still moves the playhead. The gap is not elapsed
      // time, so it does not teach the tempo.
      fold->fragRun = 0;
      fold->burstMs = 0;
      musical = 1;
      if (wire > 1) {
        fold->dup += (uint32_t)(wire - 1);
      }
    } else if (fragment) {
      if (wire > 1) {
        fold->dup += (uint32_t)(wire - 1);
      }
      if (fold->fragRun < 255) {
        fold->fragRun++;
      }
      if (fold->fragRun <= 8) {
        musical = 1;
      } else {
        int tickMs = 2500 / bpm;
        if (tickMs < 1) {
          tickMs = 1;
        }
        fold->burst++;
        uint32_t acc = (uint32_t)fold->burstMs + delta;
        while (acc >= (uint32_t)tickMs && musical < 8) {
          acc -= (uint32_t)tickMs;
          musical++;
          charged = (uint16_t)(charged + (uint16_t)tickMs);
        }
        fold->burstMs = (uint16_t)acc;
      }
    } else {
      fold->fragRun = 0;
      if (fold->burstMs > 0) {
        uint32_t sum = (uint32_t)delta + fold->burstMs;
        fold->burstMs = 0;
        if (sum >= 2000) {
          sum = 1999;
        }
        delta = (uint16_t)sum;
      }
      int estimate = bleClockEstimate(delta, bpm);
      musical = estimate;
      // A group a tick or two past the estimate is the clock rounding, not a
      // pile of copies. A count several times the gap is the pile.
      if (wire > estimate && wire <= estimate + 2) {
        musical = wire;
      } else if (wire > musical) {
        fold->dup += (uint32_t)(wire - musical);
      }
      charged = delta;
      clockFoldLearn(fold, musical, delta);
    }
  }
  if (musical > 0) {
    fold->playSteps += (uint32_t)musical;
    if (charged > 0) {
      fold->tempoSteps += (uint32_t)musical;
      fold->spanMs += charged;
    }
    if (us > 0) {
      if (fold->haveUs && us > fold->lastUs) {
        fold->arrivalSpanUs += us - fold->lastUs;
        fold->arrivalSteps += (uint32_t)musical;
      }
      fold->lastUs = us;
      fold->haveUs = 1;
    }
  }
  return musical;
}

// Walk one BLE MIDI packet the same way the parser does, and report each
// 0xF8 with the timestamp the parser would give it. `other` is set when the
// packet also carries a note, a controller, or transport.
struct BleClockEar {
  uint16_t ts;
  uint8_t sysex;
};

typedef void (*BleClockFn)(uint16_t ts, void *user);

inline uint16_t bleClockLow(uint16_t cursor, uint8_t timestampByte) {
  uint16_t low = (uint16_t)(timestampByte & 0x7F);
  uint16_t prevLow = (uint16_t)(cursor & 0x7F);
  uint16_t high = (uint16_t)(cursor & 0x1F80);
  if (low < prevLow) {
    high = (uint16_t)((high + 0x80) & 0x1F80);
  }
  return (uint16_t)(high | low);
}

inline bool bleClockTakeTs(const uint8_t *data, int len, int *i, uint16_t *ts, bool running) {
  if (!data || !i || !ts || *i >= len) {
    return false;
  }
  uint8_t b = data[*i];
  if ((b & 0x80) == 0 || b >= 0xF8) {
    return false;
  }
  bool nextIsStatus = *i + 1 >= len || (data[*i + 1] & 0x80) != 0;
  if (!nextIsStatus && !running) {
    return false;
  }
  *ts = bleClockLow(*ts, b);
  (*i)++;
  return true;
}

inline int bleClockScan(BleClockEar *ear, const uint8_t *data, int len, BleClockFn fn, void *user, int *other) {
  if (other) {
    *other = 0;
  }
  if (!ear || !data || len < 2 || (data[0] & 0x80) == 0) {
    if (other) {
      *other = 1;
    }
    if (ear) {
      ear->sysex = 0;
    }
    return 0;
  }
  uint16_t ts = (uint16_t)((data[0] & 0x3F) << 7);
  int i = 1;
  uint8_t running = 0;
  bool sysex = ear->sysex != 0;
  int clocks = 0;
  bool more = false;
  if ((data[i] & 0x80) == 0 && sysex) {
    more = true;
  } else if (data[i] >= 0xF8) {
    // Header time stands.
  } else if ((data[i] & 0x80) == 0) {
    ear->sysex = 0;
    if (other) {
      *other = 1;
    }
    return 0;
  } else {
    bleClockTakeTs(data, len, &i, &ts, false);
  }
  auto emit = [&](uint8_t status) {
    if (status == 0xF8) {
      if (fn) {
        fn(ts, user);
      }
      clocks++;
      return;
    }
    if (status >= 0xF8) {
      return;
    }
    more = true;
  };
  while (i < len) {
    if (sysex) {
      bool ended = false;
      while (i < len) {
        uint8_t b = data[i];
        if (b == 0xF7) {
          sysex = false;
          ended = true;
          more = true;
          i++;
          break;
        }
        if (b >= 0xF8) {
          emit(b);
          i++;
          continue;
        }
        if (b & 0x80) {
          if (i + 1 >= len || (data[i + 1] & 0x80) == 0 || data[i + 1] == 0xF7 || data[i + 1] >= 0xF8) {
            ts = bleClockLow(ts, b);
            i++;
            continue;
          }
          sysex = false;
          break;
        }
        more = true;
        i++;
      }
      if (!ended && sysex) {
        break;
      }
      bleClockTakeTs(data, len, &i, &ts, running != 0);
      continue;
    }
    uint8_t b = data[i];
    bool usingRunning = false;
    if (b >= 0xF8) {
      emit(b);
      i++;
      bleClockTakeTs(data, len, &i, &ts, running != 0);
      continue;
    }
    if ((b & 0x80) == 0) {
      if (running == 0) {
        i++;
        continue;
      }
      usingRunning = true;
      b = running;
    } else {
      i++;
    }
    if (b == 0xF0) {
      sysex = true;
      more = true;
      continue;
    }
    if (b == 0xF7 || b == 0xF6) {
      more = true;
      bleClockTakeTs(data, len, &i, &ts, running != 0);
      continue;
    }
    if ((b & 0xF0) == 0xF0 && b < 0xF8) {
      int needed = (b == 0xF1 || b == 0xF3) ? 1 : (b == 0xF2 ? 2 : 0);
      if (needed == 0) {
        bleClockTakeTs(data, len, &i, &ts, running != 0);
        continue;
      }
      more = true;
      int got = 0;
      while (got < needed && i < len) {
        uint8_t d = data[i];
        if (d >= 0xF8) {
          emit(d);
          i++;
          continue;
        }
        if (d & 0x80) {
          break;
        }
        i++;
        got++;
      }
      bleClockTakeTs(data, len, &i, &ts, running != 0);
      continue;
    }
    int needed = 2;
    uint8_t kind = (uint8_t)(b & 0xF0);
    if (kind == 0xC0 || kind == 0xD0) {
      needed = 1;
    }
    more = true;
    int got = 0;
    int dataAt = i;
    bool broke = false;
    while (got < needed && i < len) {
      uint8_t d = data[i];
      if (d >= 0xF8) {
        emit(d);
        i++;
        continue;
      }
      if (d & 0x80) {
        broke = true;
        break;
      }
      i++;
      got++;
    }
    if (got < needed) {
      if (!broke) {
        break;
      }
      if (!usingRunning && dataAt == i) {
        // Status followed by another status.
      }
      if (i < len && (data[i] & 0x80) && data[i] < 0xF8 && i + 1 < len && (data[i + 1] & 0x80) && data[i + 1] < 0xF8) {
        ts = bleClockLow(ts, data[i]);
        i++;
      }
      continue;
    }
    if (b < 0xF0) {
      running = b;
    }
    bleClockTakeTs(data, len, &i, &ts, running != 0);
  }
  ear->ts = ts;
  ear->sysex = sysex ? 1 : 0;
  if (other && more) {
    *other = 1;
  }
  return clocks;
}

#endif
