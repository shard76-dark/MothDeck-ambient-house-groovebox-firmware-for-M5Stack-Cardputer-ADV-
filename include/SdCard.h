#ifndef SdCard_h
#define SdCard_h
#include <stdint.h>
#include <stddef.h>

// Optional microSD. A missing card is a normal state, not a fatal error.
class SdCard {
public:
  bool Ensure();
  bool Mounted() const { return mounted; }
  bool Exists(const char *path);
  bool Mkdir(const char *path);
  bool Remove(const char *path);
  // Reads at most maxBytes. Returns false on a missing card, a missing
  // file, or a file larger than maxBytes.
  bool ReadAll(const char *path, uint8_t *dst, int maxBytes, int *outLen);
  bool ReadText(const char *path, char *dst, int maxBytes);
  bool WriteAll(const char *path, const uint8_t *src, int len);
  int List(const char *path, char names[][24], int maxNames, bool directories);

private:
  bool mounted;
  uint32_t lastTry;
  bool Mount();
};

extern SdCard sdCard;

void *deckAlloc(size_t bytes);
void deckFree(void *p);

#endif
