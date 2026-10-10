# Install MothDeck

Two images are produced by `./build.sh`:

| File | What it is |
| --- | --- |
| `mothdeck-cardputer-adv.bin` | Application only. This is the file Launcher installs. |
| `mothdeck-cardputer-adv-full.bin` | Bootloader + partition table + app. USB flash only. It replaces whatever is on the flash, including Launcher. |

Checksums are in `SHA256SUMS` next to the images. The application image starts with the byte `E9`.

This folder's application image is 1.2.2 (1,151,680 bytes, SHA-256 `db42620b1a79ca2c581ffc5df771400e3f5b2e7fb88eb7f2b3b9718e03121629`). BLE memory is reserved at boot, so turning it on always starts advertising and does not allocate. Unload BLE frees that memory for large loops on the next boot. Incoming MIDI notes only play while Rec is off; they are not written into the pattern. Space turns Rec on, and then they record. Unzip `mothdeck-sd-pack.zip` onto the card root for the kits, loops, sample instruments, and patches. That zip is unchanged from 1.2.1.

## Listening

Use earphones or headphones in the 3.5 mm jack. The Cardputer ADV internal speaker does not work with this firmware. Unplugging the earphones does not move the sound to the speaker.

## From M5Stack Launcher

Launcher is [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher), installed first with [Launcher Flasher](https://bmorcelli.github.io/Launcher/) or M5Burner as the Cardputer image. Use a Cardputer ADV build of Launcher (the current Cardputer target covers the ADV keyboard).

1. Copy `mothdeck-cardputer-adv.bin` onto a FAT32 microSD card. Do not copy the full-flash image into Launcher's installer.
2. Insert the card and power on.
3. On the splash that says to press the button to enter the Launcher, press Enter or the front button before the timeout.
4. Open the SD browser, select `mothdeck-cardputer-adv.bin`, and choose Install.
5. Launcher writes the image into an OTA slot in the free flash after its own app and sets the boot selection to that slot.

The next boot that follows the boot selection starts MothDeck. Launcher's own splash still appears on a power-on with current Launcher builds: press nothing and the installed app starts, or press Enter to stay in Launcher.

The application image is built `qio_qspi`, the same memory type as Launcher's Cardputer environment and the Arduino-ESP32 M5Cardputer board. The Stamp-S3A (ESP32-S3FN8) has 8MB flash and no PSRAM. The firmware keeps running from internal RAM when PSRAM does not answer.

MothDeck does not assume a SPIFFS partition and does not need to be the first app on the flash. Keep the application image under the free space Launcher reports (the linker cap in this repo is 3.75MB, well under the ~6.5MB left on an 8MB card after Launcher's own slot).

## Return to Launcher

From inside MothDeck:

- Hold Esc (the `` ` `` key at the top left) or the front button for about 0.7 seconds, or
- Open the Exit page (Tab until Exit) and press Enter, or
- Hold Esc or the front button while MothDeck is starting.

MothDeck enables those controls only after it finds Launcher. The expected slot is the `APP_TEST` partition (subtype `0x20`), which is where Launcher keeps its own image. The first 24 bytes of that image must be a valid ESP32-S3 app header, starting with `E9`. An OTA slot with an app in it does not count, and neither does an erased or corrupt test slot.

When that check passes, Exit clears the OTA boot selection and restarts. It does not call `esp_ota_set_boot_partition()` on Launcher's slot: ESP-IDF treats the low nibble of subtype `0x20` as OTA slot 0, so the call would select the wrong image. Erasing `otadata` is the same operation as Launcher's `launcherPartitionClearOtaBoot`. The erase runs only after the header check succeeds.

After the restart, press Enter on the splash to remain in Launcher. If a Launcher build still boots the first OTA image when `otadata` is blank, power the device off and on and press Enter on the splash. That splash is the return path Launcher documents.

A USB flash of `mothdeck-cardputer-adv-full.bin` replaces the partition table and has no `APP_TEST` slot. Exit stays grey, the page list says "Launcher not found", and holding Esc does not erase `otadata`.

## USB flash, development only

This replaces Launcher.

```bash
python3 ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0 releases/mothdeck-cardputer-adv-full.bin
```

If the port does not show up, hold the front button (GPIO0) and tap reset to enter the ROM download mode, then release the button.

The first frame after power-on is a one-second amber moth on a grey panel, then the Play page. The card scan, the speaker, and BLE start after that frame. Exit during those first moments still works when Launcher is present: hold Esc or the front button.

## SD contents

Optional. Unzip `mothdeck-sd-pack.zip` onto the card root so `moth/` is at the top. It holds the 808 and Dusty drum kits, four one-bar house loops, six sample instruments, and six patches under `moth/instruments/` (`a-minor`, `held-arp`, `glide-bass`, `saw-pluck`, `body`, `room-drive`). The Ambient House kit is already in the firmware. Every WAV in the pack is unsigned 8-bit mono at 22050 Hz. On the Instrument page, `,` and `/` load a kit from `/moth/drums` onto Drums. Enter assigns a plugin or a patch. Layout and file formats are in the pack README and in `docs/FORMATS.md`.
