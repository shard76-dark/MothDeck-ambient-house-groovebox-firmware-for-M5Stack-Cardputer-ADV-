## 2026-10-05

Exit to Launcher is enabled only when the APP_TEST slot contains a valid ESP32-S3 app image (magic E9). A full-flash layout, an empty slot, or a corrupt header greys the menu entry and shows "Launcher not found". Holding Esc does not restart, and otadata is erased only after that check passes.

An open menu takes the keyboard. The page list, the BLE name editor, and the Exit confirm consume every key, so notes, recording, transport, track, pattern, and BPM changes do not reach the page underneath. Fn arrows or Tab move the page list, Enter confirms it, and grave or Backspace cancels. Holding grave or the front button still leaves for Launcher.

Instrument assignment stays on the track that was selected. Each track keeps its own instrument, including a plugin or anything other than the drum bank. Selecting another track recalls that track's instrument and does not copy the previous one across. The Instrument page lists the instrument on tracks 1–4, and the Play page shows each track's name.
