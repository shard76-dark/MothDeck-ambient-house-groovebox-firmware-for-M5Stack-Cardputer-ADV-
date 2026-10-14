# MothBoot 0.1.2 and MothDiag 0.1.5

MothBoot lives in the factory partition and is not overwritten when an app
is installed. MothDiag 0.1.5 is the app in slot A (`ota_0`) of the merged
image. Hold Esc (the `` ` `` key) or the front button while an app boots,
or send `launcher` on USB serial, to return to MothBoot. Slot status reads
the version from the whole application image, including a tag past 256 KB.

The first flash replaces the partition table. Use the merged image below.
Later app installs go through MothBoot and draw a progress bar.

## First flash (esptool 5.5)

Hold the front button while powering the Cardputer so the ROM bootloader
is running. The screen stays blank during this step. The ROM bootloader
cannot draw.

```
python -m esptool --chip esp32s3 --port COM6 --baud 921600 erase-flash
python -m esptool --chip esp32s3 --port COM6 --baud 921600 write-flash --flash-mode qio --flash-freq 80m --flash-size 8MB 0x0 releases/mothboot-0.1.2-full.bin
```

The image starts at `0x0` and contains the bootloader, the partition table,
MothBoot 0.1.2 at `0x10000`, and MothDiag 0.1.5 at `0x190000`. It boots MothDiag.
If MothDiag does not confirm, the next reset returns to MothBoot.

A board that already has the partition table can take just the factory
image. MothBoot 0.1.0 drops USB bytes at full speed, so do this from the
ROM bootloader rather than from `--flash`:

```
python -m esptool --chip esp32s3 --port COM6 --baud 921600 write-flash --flash-mode qio --flash-freq 80m --flash-size 8MB 0x10000 releases/mothboot-0.1.2.bin
```

## Later installs

From MothBoot 0.1.2's USB serial port. `diag.py --flash` sends `launcher`
when an app is running and waits for MothBoot. The port number can change.
Each chunk is acknowledged before
the next one is sent, and the command prints a progress bar. The merged
image above carries MothDiag 0.1.5. The current standalone diagnostic
image is 0.1.6:

```
python3 tools/blemidi-diag/diag.py --port COM6 --flash releases/mothdiag-0.1.6.bin --slot 0
python3 tools/blemidi-diag/diag.py --port COM6 --sd-put local.bin /apps/local.bin
python3 tools/blemidi-diag/diag.py --port COM6 --sd-ls /apps
python3 tools/blemidi-diag/diag.py --port COM6 --sd-rm /apps/local.bin
python3 tools/blemidi-diag/diag.py --port COM6 --sd-install /apps/local.bin --slot 0
```

## Card check

`sd-check full` writes and reads the card. It stops at the first sector
that does not come back, which is the real size when a card claims more
than it has. A true 1 GB region takes about 40 to 90 minutes. A card that
claims 32 GB and wraps near 1 GB does not get a 32 GB write. The log line
`phase` `done` has `bytes` (real) and `reported` (what the card claims).
`bps` is the measured rate.

`sd-format` then builds a FAT32 volume that ends at least 16 MB inside
that measured size, and creates `/apps`, `/moth`, `/moth/drums`,
`/moth/loops`, and `/moth/instruments`.

```
python3 tools/blemidi-diag/diag.py --port COM6 --sd-check-full
python3 tools/blemidi-diag/diag.py --port COM6 --sd-format
```
