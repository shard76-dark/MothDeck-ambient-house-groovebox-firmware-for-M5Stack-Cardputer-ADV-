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

// One honest gap. The tempo is the sum of these over the last one or two
// beats, not a guess scaled by the deck's current BPM.
struct ClockSpan {
  uint16_t ms;
  uint8_t ticks;
};

// Running picture of a clock. `smooth` is the tempo the window last
// measured. Seed it with the deck tempo; 0 means 120 until a real gap
// arrives, and that seed is only the fallback for a thinned clock.
// Short runs of sub-tick clocks still count (a chord packet). A longer
// run is folded into the seeded tick so it cannot race the playhead or
// teach the tempo.
struct ClockFold {
  uint16_t lastTs;
  uint8_t have;
  uint16_t burstMs;
  uint8_t fragRun;
  uint8_t openWire;
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
  ClockSpan span[48];
  uint8_t spanN;
  uint8_t spanAt;
  uint8_t tuned;
};

inline int clockFoldBpm(const ClockFold *fold) {
  if (fold && fold->smooth >= 40 && fold->smooth <= 200) {
    return fold->smooth;
  }
  return 120;
}

inline int clockBpmOf(uint32_t ticks, uint32_t ms) {
  if (ticks == 0 || ms == 0) {
    return 0;
  }
  uint32_t den = 24u * ms;
  uint32_t num = 60000u * ticks + den / 2u;
  int raw = (int)(num / den);
  if (raw < 40) {
    raw = 40;
  } else if (raw > 200) {
    raw = 200;
  }
  return raw;
}

// Newest spans that add up to `want` ticks. 0 until that many have arrived.
inline int clockWindowBpm(const ClockFold *fold, int want) {
  if (!fold || want < 1 || fold->spanN == 0) {
    return 0;
  }
  uint32_t ticks = 0;
  uint32_t ms = 0;
  int n = fold->spanN;
  for (int i = 0; i < n; i++) {
    int idx = (fold->spanAt + 48 - 1 - i) % 48;
    ticks += fold->span[idx].ticks;
    ms += fold->span[idx].ms;
    if ((int)ticks >= want) {
      return clockBpmOf(ticks, ms);
    }
  }
  return 0;
}

// One or two beats of tick count over elapsed time. A jump shows up in the
// one-beat window; a steady clock uses two beats, which keeps 80–138 inside
// half a BPM after rounding.
inline void clockFoldLearn(ClockFold *fold, int musical, uint16_t charged) {
  if (!fold || musical < 1 || charged == 0) {
    return;
  }
  if (musical > 8) {
    musical = 8;
  }
  fold->span[fold->spanAt].ms = charged;
  fold->span[fold->spanAt].ticks = (uint8_t)musical;
  fold->spanAt = (uint8_t)((fold->spanAt + 1) % 48);
  if (fold->spanN < 48) {
    fold->spanN++;
  }
  int fast = clockWindowBpm(fold, 24);
  if (fast == 0) {
    return;
  }
  int slow = clockWindowBpm(fold, 48);
  int pick = fast;
  if (slow > 0) {
    int diff = fast > slow ? fast - slow : slow - fast;
    if (diff < 2) {
      pick = slow;
    }
  }
  fold->smooth = pick;
  fold->tuned = 1;
}

// One clock event. `wire` clocks share `ts`. `us` is the arrival time, or 0
// when the caller has none. Returns how many musical ticks to advance.
// How many musical ticks `count` clocks are worth across `delta` ms.
// Several 0xF8 bytes on one timestamp are real ticks when the slice is a
// tick long. A pile of copies is shorter than 8 ms and counts as one.
// A gap much longer than one tick at `bpm` is a thinned clock.
inline int clockGapTicks(int count, uint16_t delta, int bpm) {
  int tickMs = 2500 / bpm;
  if (tickMs < 8) {
    tickMs = 8;
  }
  if (count < 1) {
    count = 1;
  }
  int slice = (int)delta / count;
  bool batched = count > 1 && slice >= 8 && slice <= 70;
  bool oneTick = !batched && delta >= 8 && delta <= 70 && (int)delta * 2 <= tickMs * 5;
  if (batched) {
    return count > 8 ? 8 : count;
  }
  if (oneTick) {
    return 1;
  }
  return bleClockEstimate(delta, bpm);
}

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
  int tempoTicks = 0;
  uint16_t charged = 0;
  if (!fold->have) {
    fold->have = 1;
    fold->lastTs = ts;
    musical = wire > 8 ? 1 : wire;
    if (wire > musical) {
      fold->dup += (uint32_t)(wire - musical);
    }
    fold->openWire = (uint8_t)musical;
  } else if (ts == fold->lastTs) {
    fold->dup += (uint32_t)wire;
  } else {
    uint16_t delta = bleClockDelta(fold->lastTs, ts);
    fold->lastTs = ts;
    int bpm = clockFoldBpm(fold);
    // A run of gaps under 4 ms is a copy flood. One gap of 4–8 ms is a
    // timestamp nudged off 0xF8–0xFF, and it still belongs in the window
    // so the longer gap after it averages back to the real tempo.
    bool subTick = delta > 0 && delta < 2000 && delta <= 8;
    bool fragment = subTick && (delta < 4 || fold->fragRun > 0);
    if (delta == 0 || delta >= 2000) {
      // The clock in hand still moves the playhead. The gap is not elapsed
      // time, so it does not teach the tempo.
      fold->fragRun = 0;
      fold->burstMs = 0;
      musical = 1;
      if (wire > 1) {
        fold->dup += (uint32_t)(wire - 1);
      }
      fold->openWire = 1;
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
      fold->openWire = 1;
    } else {
      fold->fragRun = subTick ? 1 : 0;
      if (fold->burstMs > 0) {
        uint32_t sum = (uint32_t)delta + fold->burstMs;
        fold->burstMs = 0;
        if (sum >= 2000) {
          sum = 1999;
        }
        delta = (uint16_t)sum;
      }
      // The gap belongs to the clocks already seen. The clocks in hand
      // move the playhead, and their own gap arrives with the next stamp.
      int credit = fold->openWire > 0 ? fold->openWire : 1;
      tempoTicks = clockGapTicks(credit, delta, bpm);
      musical = clockGapTicks(wire, delta, bpm);
      if (wire > musical) {
        fold->dup += (uint32_t)(wire - musical);
      }
      charged = delta;
      clockFoldLearn(fold, tempoTicks, delta);
      fold->openWire = (uint8_t)(musical > 8 ? 8 : musical);
    }
  }
  if (musical > 0) {
    fold->playSteps += (uint32_t)musical;
    if (charged > 0) {
      fold->tempoSteps += (uint32_t)(tempoTicks > 0 ? tempoTicks : musical);
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
  if (!nextIsStatus && running && b >= 0x90 && b < 0xF0) {
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
