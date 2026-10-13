#!/usr/bin/env python3
"""Capture MothDiag JSON from the Cardputer ADV and print a short report.

Finds the USB CDC serial port on Windows and Linux, records for N seconds,
writes the raw log, and summarises BPM stability, hanging notes, and jitter.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from typing import Any, Iterable, Optional


def _nums(rows: Iterable[dict], key: str) -> list[float]:
    out = []
    for row in rows:
        value = row.get(key)
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            out.append(float(value))
    return out


def _spread(values: list[float]) -> Optional[dict]:
    if not values:
        return None
    return {
        "n": len(values),
        "mean": statistics.fmean(values),
        "min": min(values),
        "max": max(values),
        "stdev": statistics.pstdev(values) if len(values) > 1 else 0.0,
    }


def _fmt_spread(label: str, spread: Optional[dict], unit: str) -> str:
    if not spread:
        return f"{label}: no samples"
    return (
        f"{label}: mean {spread['mean']:.2f}{unit}  "
        f"min {spread['min']:.2f}  max {spread['max']:.2f}  "
        f"stdev {spread['stdev']:.2f}  (n={spread['n']})"
    )


def held_from_midi(rows: list[dict]) -> list[dict]:
    """Replay note messages. Velocity 0 and note-off release. CC 120 and 123 clear a channel."""
    held: dict[tuple[int, int], dict] = {}
    for row in rows:
        if row.get("t") != "midi":
            continue
        kind = row.get("msg")
        channel = row.get("ch")
        data = row.get("d")
        if kind in ("note_on", "note_off") and isinstance(channel, int) and isinstance(data, list) and len(data) >= 2:
            key = (channel, int(data[0]))
            if kind == "note_on" and int(data[1]) > 0 and not row.get("vel0"):
                held[key] = {"ch": channel, "note": int(data[0]), "vel": int(data[1])}
            else:
                held.pop(key, None)
        elif kind == "cc" and isinstance(channel, int) and isinstance(data, list) and data and int(data[0]) in (120, 123):
            for key in [k for k in held if k[0] == channel]:
                held.pop(key, None)
    return [held[k] for k in sorted(held)]


def summarize(lines: Iterable[str]) -> dict[str, Any]:
    rows: list[dict] = []
    bad = 0
    for line in lines:
        text = line.strip()
        if not text:
            continue
        try:
            item = json.loads(text)
        except json.JSONDecodeError:
            bad += 1
            continue
        if isinstance(item, dict):
            rows.append(item)
    stats = [row for row in rows if row.get("t") == "stats"]
    midi = [row for row in rows if row.get("t") == "midi"]
    connects = sum(1 for row in rows if row.get("t") == "connect")
    disconnects = sum(1 for row in rows if row.get("t") == "disconnect")
    last_stats = stats[-1] if stats else {}
    hanging_series = _nums(stats, "hanging")
    gap_series = _nums(stats, "max_gap_us")
    jitter_series = _nums(stats, "jitter_us")
    held = held_from_midi(midi)
    last_hanging = int(last_stats.get("hanging") or 0) if last_stats else len(held)
    note_on = last_stats.get("note_on_total")
    note_off = last_stats.get("note_off_total")
    if note_on is None:
        note_on = sum(1 for row in midi if row.get("msg") == "note_on")
    if note_off is None:
        note_off = sum(1 for row in midi if row.get("msg") == "note_off")
    parse_errors = last_stats.get("parse_errors_total")
    if parse_errors is None:
        parse_errors = sum(int(row.get("parse_errors") or 0) for row in stats)
    queue_drops = last_stats.get("queue_drops") or 0
    mismatch = last_stats.get("parser_mismatch") or 0
    clocks = last_stats.get("clocks_total")
    if clocks is None:
        clocks = sum(1 for row in midi if row.get("msg") == "clock")
    packets = sum(int(row.get("packets") or 0) for row in stats)
    if packets == 0:
        packets = sum(1 for row in rows if row.get("t") == "pkt")
    return {
        "lines": len(rows),
        "bad_lines": bad,
        "connects": connects,
        "disconnects": disconnects,
        "clocks": int(clocks or 0),
        "packets": int(packets or 0),
        "bpm_arrival": _spread(_nums(stats, "bpm_arrival")),
        "bpm_midi": _spread(_nums(stats, "bpm_midi")),
        "jitter_us": _spread(jitter_series),
        "max_gap_us": max(gap_series) if gap_series else 0,
        "note_on": int(note_on or 0),
        "note_off": int(note_off or 0),
        "hanging": max(last_hanging, len(held)),
        "held": held,
        "parse_errors": int(parse_errors or 0),
        "queue_drops": int(queue_drops or 0),
        "parser_mismatch": int(mismatch or 0),
        "extra_offs": int(last_stats.get("extra_offs") or 0),
    }


def render_report(summary: dict[str, Any], port: str, seconds: float, log_path: str) -> str:
    arrival = summary["bpm_arrival"]
    midi = summary["bpm_midi"]
    lines = [
        "MothDiag 0.1.0 capture",
        f"port: {port}",
        f"seconds: {seconds:.1f}",
        f"log: {log_path}",
        f"json lines: {summary['lines']}",
        f"link: connect {summary['connects']}, disconnect {summary['disconnects']}",
        f"packets: {summary['packets']}",
        f"clocks: {summary['clocks']}",
        _fmt_spread("BPM arrival", arrival, ""),
        _fmt_spread("BPM midi", midi, ""),
        _fmt_spread("jitter", summary["jitter_us"], " us"),
        f"max packet gap: {summary['max_gap_us']} us",
        (
            f"notes: on {summary['note_on']}  off {summary['note_off']}  "
            f"hanging {summary['hanging']}  extra offs {summary['extra_offs']}"
        ),
    ]
    if summary["held"]:
        listed = ", ".join(f"ch {n['ch']} note {n['note']}" for n in summary["held"])
        lines.append(f"hanging notes: {listed}")
    else:
        lines.append("hanging notes: none")
    lines.append(
        f"parse errors: {summary['parse_errors']}  "
        f"queue drops: {summary['queue_drops']}  "
        f"parser mismatch: {summary['parser_mismatch']}"
    )
    stable = True
    notes = []
    if arrival and arrival["stdev"] > 1.0:
        stable = False
        notes.append(f"arrival BPM stdev {arrival['stdev']:.2f}")
    if summary["hanging"]:
        stable = False
        notes.append(f"{summary['hanging']} hanging")
    if summary["parse_errors"] or summary["queue_drops"] or summary["parser_mismatch"]:
        stable = False
        notes.append("parse, queue, or parser mismatch")
    if not arrival:
        lines.append("stability: not enough clock windows to judge BPM")
    elif stable:
        lines.append("stability: BPM steady, no hanging notes")
    else:
        lines.append("stability: " + "; ".join(notes))
    return "\n".join(lines)


def self_check() -> int:
    sample = [
        '{"t":"hello","name":"MothDiag","version":"0.1.0","baud":115200}',
        '{"t":"connect","us":10,"interval":6,"interval_ms":7.5,"mtu":256,"rssi":-40}',
        '{"t":"midi","us":100,"msg":"note_on","ch":1,"d":[60,100],"running":false,"ble_ts":1}',
        '{"t":"midi","us":200,"msg":"note_off","ch":1,"d":[60,0],"vel0":true,"running":false,"ble_ts":2}',
        '{"t":"midi","us":300,"msg":"note_on","ch":2,"d":[64,90],"running":false,"ble_ts":3}',
        '{"t":"stats","us":1000000,"window_ms":1000,"clocks":48,"clocks_total":48,"bpm_midi":119.5,"bpm_arrival":120.0,"note_on":2,"note_off":1,"note_on_total":2,"note_off_total":1,"hanging":1,"extra_offs":0,"parse_errors":0,"parse_errors_total":0,"parser_mismatch":0,"packets":20,"pps":20.0,"max_gap_us":8000,"jitter_us":400,"queue_drops":0}',
        '{"t":"stats","us":2000000,"window_ms":1000,"clocks":48,"clocks_total":96,"bpm_midi":120.5,"bpm_arrival":121.0,"note_on":0,"note_off":0,"note_on_total":2,"note_off_total":1,"hanging":1,"extra_offs":0,"parse_errors":0,"parse_errors_total":0,"parser_mismatch":0,"packets":20,"pps":20.0,"max_gap_us":9000,"jitter_us":500,"queue_drops":0}',
        "not json",
    ]
    summary = summarize(sample)
    report = render_report(summary, "COM5", 2.0, "mothdiag-self-check.log")
    problems = []
    if summary["hanging"] != 1 or summary["held"] != [{"ch": 2, "note": 64, "vel": 90}]:
        problems.append("hanging note")
    if summary["note_on"] != 2 or summary["note_off"] != 1:
        problems.append("note counts")
    if not summary["bpm_arrival"] or abs(summary["bpm_arrival"]["mean"] - 120.5) > 0.01:
        problems.append("bpm mean")
    if summary["bpm_arrival"]["stdev"] < 0.4:
        problems.append("bpm stdev")
    if summary["max_gap_us"] != 9000 or not summary["jitter_us"]:
        problems.append("jitter")
    if summary["bad_lines"] != 1 or summary["connects"] != 1:
        problems.append("line counts")
    if "hanging notes: ch 2 note 64" not in report:
        problems.append("report text")
    if "stability:" not in report:
        problems.append("stability line")
    if problems:
        sys.stderr.write("diag.py self-check failed: " + ", ".join(problems) + "\n")
        sys.stderr.write(report + "\n")
        return 1
    print("diag.py self-check ok")
    return 0


def _port_score(port: Any) -> int:
    blob = " ".join(
        str(part or "")
        for part in (
            getattr(port, "device", ""),
            getattr(port, "name", ""),
            getattr(port, "description", ""),
            getattr(port, "manufacturer", ""),
            getattr(port, "hwid", ""),
        )
    ).lower()
    if "bluetooth" in blob:
        return -1
    score = 0
    if "mothdiag" in blob:
        score += 100
    if "303a" in blob or "espressif" in blob:
        score += 40
    if "ttyacm" in blob or "ttyusb" in blob or "cdc" in blob or "usb" in blob or "jtag" in blob:
        score += 10
    device = str(getattr(port, "device", "") or "")
    if device.upper().startswith("COM") or "ttyACM" in device or "ttyUSB" in device:
        score += 1
    return score


def _looks_like_diag(text: str) -> bool:
    if "MothDiag" in text:
        return True
    try:
        item = json.loads(text)
    except json.JSONDecodeError:
        return False
    return isinstance(item, dict) and item.get("t") in ("hello", "status", "radio", "stats", "help")


def find_port(explicit: Optional[str], baud: int):
    import serial
    from serial.tools import list_ports

    if explicit:
        candidates = [explicit]
    else:
        ranked = []
        for port in list_ports.comports():
            score = _port_score(port)
            if score >= 0:
                ranked.append((score, port.device))
        ranked.sort(key=lambda item: (-item[0], item[1]))
        candidates = [device for _, device in ranked]
    if not candidates:
        return None, "no serial ports found", []
    errors = []
    for device in candidates:
        try:
            handle = serial.Serial(device, baud, timeout=0.2, write_timeout=1)
        except serial.SerialException as exc:
            errors.append(f"{device}: {exc}")
            continue
        prefix: list[str] = []
        try:
            handle.reset_input_buffer()
            handle.write(b"status\n")
            handle.flush()
            deadline = time.time() + 1.8
            buf = b""
            matched = False
            while time.time() < deadline:
                chunk = handle.read(handle.in_waiting or 1)
                if not chunk:
                    continue
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    text = raw.decode("utf-8", "replace").strip()
                    if text:
                        prefix.append(text)
                    if _looks_like_diag(text):
                        matched = True
                        break
                if matched:
                    break
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("utf-8", "replace").strip()
                if text:
                    prefix.append(text)
            if buf.strip():
                prefix.append(buf.decode("utf-8", "replace").strip())
            if matched or explicit:
                return handle, device, prefix
        except serial.SerialException as exc:
            errors.append(f"{device}: {exc}")
        handle.close()
    detail = "; ".join(errors) if errors else "no port answered as MothDiag"
    return None, detail, []


def capture(handle, seconds: float, log_path: str, prefix: Optional[list[str]] = None) -> list[str]:
    deadline = time.time() + seconds
    lines: list[str] = []
    buf = b""
    with open(log_path, "w", encoding="utf-8") as log:
        for text in prefix or []:
            if text:
                log.write(text + "\n")
                lines.append(text)
        while time.time() < deadline:
            waiting = handle.in_waiting
            chunk = handle.read(waiting or 1)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("utf-8", "replace").strip()
                if not text:
                    continue
                log.write(text + "\n")
                lines.append(text)
        if buf.strip():
            text = buf.decode("utf-8", "replace").strip()
            if text:
                log.write(text + "\n")
                lines.append(text)
        log.flush()
    return lines


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Capture MothDiag BLE MIDI diagnostics over USB serial.")
    parser.add_argument("-n", "--seconds", type=float, default=10.0, help="how long to record (default 10)")
    parser.add_argument("--port", help="serial port, for example COM5 or /dev/ttyACM0")
    parser.add_argument("-o", "--output", help="raw JSON log path")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--self-check", action="store_true", help="check the report against a built-in sample")
    args = parser.parse_args(argv)
    if args.self_check:
        return self_check()
    if args.seconds <= 0:
        sys.stderr.write("seconds must be greater than zero\n")
        return 1
    try:
        import serial  # noqa: F401
    except ImportError:
        sys.stderr.write("pyserial is required: pip install pyserial\n")
        return 1
    handle, found, prefix = find_port(args.port, args.baud)
    if handle is None:
        sys.stderr.write(f"MothDiag: {found}. Plug in the Cardputer, or pass --port.\n")
        return 1
    stamp = time.strftime("%Y%m%d-%H%M%S")
    log_path = args.output or f"mothdiag-{stamp}.log"
    try:
        lines = capture(handle, args.seconds, log_path, prefix)
    finally:
        handle.close()
    summary = summarize(lines)
    sys.stdout.write(render_report(summary, str(found), args.seconds, log_path) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
