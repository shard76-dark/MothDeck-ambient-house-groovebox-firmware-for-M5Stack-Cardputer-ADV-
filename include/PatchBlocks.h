#ifndef PatchBlocks_h
#define PatchBlocks_h
#include <stdint.h>

// Scale lock. mode 1 major, 2 natural minor, 3 harmonic minor,
// 4 mixolydian, 5 phrygian, 6 chromatic. root is a pitch class 0-11.
// Nearest scale tone, and a tie goes to the lower one. Notes stay in 0..127.
int scaleLock(int note, int root, int mode);

// Multiply a tracker frequency by 2^(coarse/12). coarse is clamped to -24..24.
int shiftSemi(int freq, int coarse);

// Linear glide. pos 0 is `from`. pos >= len is `to`.
int glideAt(int from, int to, int pos, int len);

// Held chord, eight notes. add returns 1 when this is the first note,
// 2 when it joined, 0 when it was already there or the set is full.
int heldAdd(uint8_t *notes, int *count, int note);
// Returns how many notes remain. *sounding is the index that should play.
int heldRemove(uint8_t *notes, int *count, int *index, int note);

#endif
