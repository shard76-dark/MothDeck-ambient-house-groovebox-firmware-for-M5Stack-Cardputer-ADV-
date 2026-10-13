# MothDiag 0.1.3

Standalone BLE MIDI diagnostics for the [M5Stack Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv). It advertises as a BLE MIDI peripheral named **MothDiag**, using the same service, characteristic, advertising packet, and Just Works pairing as MothDeck. There is no synth, no audio, and no sequencer.

Plug the Cardputer into a Windows PC or a uConsole over USB. An assistant reads the USB serial port at 115200 and talks to the firmware with the commands below. The usual source is an Akai MPC Live II, or anything else that sends BLE MIDI.

The application image is [`releases/mothdiag-cardputer-adv.bin`](../../releases/mothdiag-cardputer-adv.bin).

- Version 0.1.3
- 821,200 bytes, magic `E9`, chip `0x0009`
- SHA-256 `2bfb2859a237dbdf8815bf4c8602cddcaf0f819c6a45b36bd18ce197baf9eb4e`
- Also at [`releases/mothdiag-0.1.3.bin`](../../releases/mothdiag-0.1.3.bin)
- Also listed in [`releases/SHA256SUMS`](../../releases/SHA256SUMS)

Installing this image replaces the app in the Cardputer's boot slot. Install `releases/mothdeck-cardputer-adv.bin` again to return to MothDeck. This tool does not play sound.

## Flash it

### From M5Stack Launcher

Launcher is [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher). Use a Cardputer ADV build.

1. Copy `releases/mothdiag-cardputer-adv.bin` onto a FAT32 microSD card. Copy the application image, not a full-flash image.
2. Insert the card and power on.
3. On the splash that says to press the button to enter Launcher, press Enter or the front button before the timeout.
4. Open the SD browser, select `mothdiag-cardputer-adv.bin`, and choose Install.

The screen then shows `MothDiag`, `0.1.3`, `Waiting` or `Connected`, the test name, MIDI BPM, arrival BPM, the clock rate, and the MTC rate. Each line redraws only when its text changes. The words stay plain.

### USB, from this repo

The PlatformIO environment is `mothdiag`, in this folder. It is not the MothDeck `cardputer-adv` environment.

```bash
pio run -d tools/blemidi-diag -e mothdiag -t upload --upload-port /dev/ttyACM0
```

On Windows the port is a COM port, for example `--upload-port COM5`. If the port does not show up, hold the front button (GPIO0) and tap reset to enter the ROM download mode, then release the button.

A successful build writes `.pio/build/mothdiag/firmware.bin`. The copy checked in for flashing is `releases/mothdiag-cardputer-adv.bin`.

## Serial

USB CDC, 115200, 8N1. Every line is one JSON object.

| Line `t` | When |
| --- | --- |
| `hello` | Boot. `name` is `MothDiag`, `version` is `0.1.3`. |
| `test` | One step of `run`. `phase` is `wait`, `run`, or `done`. `go` starts the script when none is running. |
| `interval` | A connection-parameter request. The negotiated interval is logged again when the central answers, including when it stays the same. |
| `radio` | Advertising started, or the radio did not come up. |
| `connect`, `disconnect`, `link` | Link up, link down, or a later interval / MTU update. Fields that are not known yet are `null`: `interval` (1.25 ms units), `interval_ms`, `latency`, `timeout` (10 ms units), `timeout_ms`, `mtu`, `rssi`. |
| `pkt` | One BLE notification, when verbose is on. `hex` is the raw packet, `ble_ts` is the 13-bit MIDI timestamp, `us` is the arrival time in microseconds, `n` is how many MIDI messages were inside. |
| `midi` | One parsed message. `msg` is the type (`note_on`, `note_off`, `cc`, `pitch`, `clock`, `start`, `continue`, `stop`, `song_pos`, `sysex`, and the other MIDI types). `ch` is 1–16 for channel voice. `d` is the data bytes. `running` is true when the status byte was omitted. `ble_ts` is that message's timestamp. Note On with velocity 0 is `note_off` and `vel0` is true. SysEx uses `hex` and `ended`. |
| `stats` | Once per interval (default 1000 ms). |

`stats` carries the clock count, `bpm_midi` (from BLE MIDI timestamps) and `bpm_arrival` (from arrival time), note-on and note-off counts for the window and the session, `hanging` (note-ons with no note-off), `extra_offs`, `parse_errors`, `packets`, `pps`, `max_gap_us`, `jitter_us` (standard deviation of the gaps between packets), and `queue_drops`. A BPM is `null` until two clocks have a usable span. Timestamps are 13-bit milliseconds and wrap every 8192 ms. `bpm_midi` uses `(clocks - 1) / span`, so several clocks that share one timestamp pull it up compared with `bpm_arrival`.

Channel numbers in the log are 1–16. MothDeck's parser stores them as 0–15; the self-test checks that those are the same messages.

### Commands

Send one command per line.

| Command | Effect |
| --- | --- |
| `help` | List the commands. |
| `status` | Name, link, interval, MTU, RSSI, totals. |
| `reset` | Clear counters, BPM, and held notes. The link stays up. |
| `verbose on` / `verbose off` | Raw `pkt` lines on or off. MIDI lines stay on. Default is on. |
| `stats <ms>` | Set the stats interval, from 100 to 60000. `stats` alone prints the current interval. |
| `bpm` | Session clock summary: both BPMs, arrival jitter, timestamp jitter, and the spans. |
| `notes` | Notes still held. |
| `selftest` | Run the built-in parser checks. `self-test` is the same command. |

The self-test feeds MothDeck's parser (`parseBleMidiPacket`) and checks running status, several messages in one packet, a 13-bit timestamp that wraps, a SysEx split across two packets, and clocks mixed with notes (a clock between the two data bytes of a note, a status byte that follows a clock, and several clocks that share one timestamp). A timestamp low byte of 0xF8–0xFF is a real-time message, so the wrap case steps from 0x1FF0 to 0. The result is JSON on the serial port. The screen does not show it.

The screen shows the name, version, `Connected` or `Waiting`, the test name, MIDI BPM, arrival BPM (`Arr`), the clock rate (`Clk`), and the MTC rate. A figure stays `--` until a window has a span. It redraws only the line that changed, and it does not show faults.

## diag.py

Requires pyserial (`pip install pyserial`). It looks for a USB CDC port on Windows and Linux, prefers an Espressif or `ttyACM` device, and keeps the one that answers `status` as MothDiag. It then records for N seconds, saves the raw log, and prints BPM stability, hanging notes, and jitter.

```bash
pip install pyserial
python3 tools/blemidi-diag/diag.py -n 15
python3 tools/blemidi-diag/diag.py --port COM5 -n 30 -o capture.log
python3 tools/blemidi-diag/diag.py --port /dev/ttyACM0 -n 10
```

`-n` is seconds (default 10). `-o` is the log path. The default log name is `mothdiag-YYYYMMDD-HHMMSS.log` in the current directory. Opening the port leaves DTR and RTS clear so a second capture does not restart the board, sends `status`, and does not reset the counters. Verbose lines are dropped when the USB buffer is full.

BPM stability is the standard deviation of the once-per-second `bpm_arrival` samples. The report also lists the max packet gap, mean jitter, note on/off totals, and any note that was still held at the end of the log.

## Build

Host checks, then the firmware:

```bash
bash tools/blemidi-diag/test_host.sh
pio run -d tools/blemidi-diag -e mothdiag
```

The MothDeck environments `cardputer-adv` and `cardputer-adv-dev` are unchanged. CI builds both.

Copy `.pio/build/mothdiag/firmware.bin` to `releases/mothdiag-cardputer-adv.bin` when cutting an image. The file starts with the byte `E9`.
