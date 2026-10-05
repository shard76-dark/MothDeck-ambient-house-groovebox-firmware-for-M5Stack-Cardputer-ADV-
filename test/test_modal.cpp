#include "ModalInput.h"
#include "Tracker.h"
#include "InstrumentBank.h"
#include <cstdio>
#include <cstring>

bool instrumentView(int id, ExtSampleView *out) {
  (void)id;
  if (out) {
    std::memset(out, 0, sizeof(*out));
  }
  return false;
}

static int failures = 0;

static void expect(bool ok, const char *message) {
  if (ok) {
    std::printf("ok  %s\n", message);
    return;
  }
  std::printf("FAIL %s\n", message);
  failures++;
}

struct Shot {
  int track;
  int voice;
  int pattern;
  int bpm;
  bool playing;
  uint8_t steps[4];
  uint8_t voices[4];
};

static Shot shoot(const Tracker &tracker) {
  Shot shot;
  shot.track = tracker.selectedTrack;
  shot.voice = tracker.currentVoice;
  shot.pattern = tracker.currentPattern;
  shot.bpm = tracker.Bpm();
  shot.playing = tracker.isPlaying;
  for (int i = 0; i < 4; i++) {
    shot.steps[i] = tracker.tracks[i][0];
    shot.voices[i] = tracker.trackVoice[i];
  }
  return shot;
}

static bool same(const Shot &a, const Shot &b) {
  return a.track == b.track && a.voice == b.voice && a.pattern == b.pattern && a.bpm == b.bpm && a.playing == b.playing &&
         std::memcmp(a.steps, b.steps, sizeof(a.steps)) == 0 && std::memcmp(a.voices, b.voices, sizeof(a.voices)) == 0;
}

// What the Play page would do with this key if the menu failed to catch it.
static void leakToTracker(Tracker &tracker, const KeyEvent &ev) {
  if (ev.space) {
    tracker.SetCommand('P', 0);
    return;
  }
  if (ev.ch >= '1' && ev.ch <= '4') {
    tracker.SetCommand('T', ev.ch - '1');
    return;
  }
  if (ev.ch >= '5' && ev.ch <= '8') {
    tracker.SetCommand('$', ev.ch - '5');
    return;
  }
  if (ev.ch == '-' || ev.ch == '=') {
    tracker.SetCommand('b', ev.ch == '=' ? 1 : -1);
    return;
  }
  if (ev.ch == 'z' || ev.ch == 'n') {
    tracker.SetCommand('N', ev.ch == 'z' ? 0 : 2);
  }
}

static KeyEvent key(char ch) {
  KeyEvent ev;
  std::memset(&ev, 0, sizeof(ev));
  ev.ch = ch;
  return ev;
}

static void testMenuBlocksPage() {
  const char *names[] = {"page list", "name editor", "exit confirm"};
  const ModalKind kinds[] = {MODAL_PAGE, MODAL_NAME, MODAL_EXIT};
  const char *leaks[] = {"z", "n", "1", "5", "-", "=", " "};
  for (int k = 0; k < 3; k++) {
    for (int i = 0; i < 7; i++) {
      Tracker tracker;
      tracker.SetCommand('I', 6);
      Shot before = shoot(tracker);
      KeyEvent ev = key(leaks[i][0]);
      if (leaks[i][0] == ' ') {
        ev.ch = 0;
        ev.space = true;
      }
      ModalAction action;
      dispatchModal(kinds[k], ev, &action);
      expect(action.consumed, names[k]);
      expect(!action.confirmExit && !action.applyName, "menu key is not a page command");
      if (!action.consumed) {
        leakToTracker(tracker, ev);
      }
      char msg[64];
      std::snprintf(msg, sizeof(msg), "%s keeps tracker still on '%s'", names[k], leaks[i]);
      expect(same(before, shoot(tracker)), msg);
      Tracker open;
      open.SetCommand('T', 3);
      open.SetCommand('$', 2);
      Shot openBefore = shoot(open);
      leakToTracker(open, ev);
      expect(!same(openBefore, shoot(open)), "the same key changes the tracker when no menu is open");
    }
  }
}

static void testMenuKeysOnly() {
  KeyEvent up;
  std::memset(&up, 0, sizeof(up));
  up.fn = true;
  up.ch = ';';
  ModalAction action;
  dispatchModal(MODAL_PAGE, up, &action);
  expect(action.consumed && action.pageDelta == -1 && !action.closeMenu, "page list moves up");

  KeyEvent down = up;
  down.ch = '.';
  dispatchModal(MODAL_PAGE, down, &action);
  expect(action.consumed && action.pageDelta == 1, "page list moves down");

  KeyEvent tab;
  std::memset(&tab, 0, sizeof(tab));
  tab.tab = true;
  dispatchModal(MODAL_PAGE, tab, &action);
  expect(action.consumed && action.pageDelta == 1 && !action.closeMenu, "tab moves the page list");

  KeyEvent enter;
  std::memset(&enter, 0, sizeof(enter));
  enter.enter = true;
  dispatchModal(MODAL_PAGE, enter, &action);
  expect(action.consumed && action.closeMenu && !action.cancelToPlay && action.pageDelta == 0, "enter closes the page list");

  KeyEvent cancel = key('`');
  dispatchModal(MODAL_PAGE, cancel, &action);
  expect(action.consumed && action.closeMenu && action.cancelToPlay, "grave cancels the page list");

  dispatchModal(MODAL_NAME, enter, &action);
  expect(action.consumed && action.applyName && !action.append, "enter applies the name");
  KeyEvent typed = key('s');
  dispatchModal(MODAL_NAME, typed, &action);
  expect(action.consumed && action.append && !action.applyName, "letters type into the name");
  KeyEvent space;
  std::memset(&space, 0, sizeof(space));
  space.space = true;
  dispatchModal(MODAL_NAME, space, &action);
  expect(action.consumed && !action.append && !action.applyName, "space does not type or play");
  dispatchModal(MODAL_NAME, cancel, &action);
  expect(action.consumed && action.cancelName, "grave cancels naming");

  dispatchModal(MODAL_EXIT, enter, &action);
  expect(action.consumed && action.confirmExit, "enter confirms exit");
  dispatchModal(MODAL_EXIT, space, &action);
  expect(action.consumed && !action.confirmExit && !action.cancelToPlay, "space does not confirm exit");
  dispatchModal(MODAL_EXIT, cancel, &action);
  expect(action.consumed && action.cancelToPlay && !action.confirmExit, "grave leaves the exit confirm");

  expect(activeModal(true, true, true) == MODAL_PAGE, "page list is in front of other menus");
  expect(activeModal(false, true, true) == MODAL_NAME, "name editor is in front of exit");
  expect(activeModal(false, false, true) == MODAL_EXIT, "exit page is a confirm");
  expect(activeModal(false, false, false) == MODAL_NONE, "play has no menu");
}

int main() {
  testMenuBlocksPage();
  testMenuKeysOnly();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
