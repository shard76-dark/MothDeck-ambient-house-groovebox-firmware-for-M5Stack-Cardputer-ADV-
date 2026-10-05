#include "ModalInput.h"
#include <string.h>

ModalKind activeModal(bool overlay, bool naming, bool exitPage) {
  if (overlay) {
    return MODAL_PAGE;
  }
  if (naming) {
    return MODAL_NAME;
  }
  if (exitPage) {
    return MODAL_EXIT;
  }
  return MODAL_NONE;
}

static bool arrowUp(const KeyEvent &ev) {
  return ev.fn && ev.ch == ';';
}

static bool arrowDown(const KeyEvent &ev) {
  return ev.fn && ev.ch == '.';
}

static bool fnChord(const KeyEvent &ev) {
  return ev.fn && (ev.ch == ';' || ev.ch == '.' || ev.ch == ',' || ev.ch == '/' || ev.ch == '-' || ev.ch == '=');
}

void dispatchModal(ModalKind kind, const KeyEvent &ev, ModalAction *out) {
  if (!out) {
    return;
  }
  memset(out, 0, sizeof(*out));
  if (kind == MODAL_NONE) {
    return;
  }
  out->consumed = true;
  if (kind == MODAL_PAGE) {
    if (arrowUp(ev) || (ev.tab && (ev.shift || ev.ctrl))) {
      out->pageDelta = -1;
    } else if (arrowDown(ev) || ev.tab) {
      out->pageDelta = 1;
    } else if (ev.enter) {
      out->closeMenu = true;
    } else if (ev.del || (ev.ch == '`' && !ev.fn)) {
      out->closeMenu = true;
      out->cancelToPlay = true;
    }
    return;
  }
  if (kind == MODAL_NAME) {
    if (ev.enter) {
      out->applyName = true;
    } else if (ev.del) {
      out->backspace = true;
    } else if (ev.ch == '`' && !ev.fn) {
      out->cancelName = true;
    } else if (!ev.space && !ev.tab && !ev.ctrl && !fnChord(ev) && ev.ch) {
      out->append = true;
    }
    return;
  }
  if (kind == MODAL_EXIT) {
    if (ev.enter) {
      out->confirmExit = true;
    } else if (ev.del || (ev.ch == '`' && !ev.fn)) {
      out->cancelToPlay = true;
    }
  }
}
