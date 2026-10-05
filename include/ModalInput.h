#ifndef ModalInput_h
#define ModalInput_h

// Foreground menus. While one is active it owns the keyboard: the page
// underneath must not see the key.
enum ModalKind {
  MODAL_NONE = 0,
  MODAL_PAGE = 1,
  MODAL_NAME = 2,
  MODAL_EXIT = 3
};

struct KeyEvent {
  bool tab;
  bool enter;
  bool del;
  bool space;
  bool fn;
  bool shift;
  bool ctrl;
  bool alt;
  bool opt;
  char ch;
};

struct ModalAction {
  bool consumed;
  int pageDelta;
  bool closeMenu;
  bool cancelToPlay;
  bool append;
  bool backspace;
  bool applyName;
  bool cancelName;
  bool confirmExit;
};

// exitPage is the Exit confirm (page 7). The page list wins, then the name editor.
ModalKind activeModal(bool overlay, bool naming, bool exitPage);
void dispatchModal(ModalKind kind, const KeyEvent &ev, ModalAction *out);

#endif
