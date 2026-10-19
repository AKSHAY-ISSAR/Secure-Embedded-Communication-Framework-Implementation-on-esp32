#!/usr/bin/env python3
"""
Laptop front-end for the ESP32 secure link.

The QEMU version talked to a TCP socket; on real hardware we talk to the
USB-serial port of the ESP32 that is plugged in.

    pip install pyserial
    python display_front.py /dev/ttyUSB0          # Linux
    python display_front.py COM5                  # Windows
"""
import sys
import threading
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
PROMPT = "you: "

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.2)
except serial.SerialException as exc:
    print(f"[-] cannot open {PORT}: {exc}")
    sys.exit(1)

print(f"[+] attached to {PORT} @ {BAUD}")
print("-" * 50)


def listen():
    buf = b""
    while True:
        try:
            data = ser.read(256)
        except serial.SerialException:
            break
        if not data:
            continue
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", errors="ignore").rstrip("\r")
            if not text:
                continue
            
            # Identify the log type
            tag = "[log ]" if text[:2] in ("W ", "E ", "I ", "D ") else "[peer]"
            
            # --- MODIFIED: Write cleanly to the log file instead of stdout ---
            with open("telemetry.log", "a") as log_file:
                log_file.write(f"{tag} {text}\n")


threading.Thread(target=listen, daemon=True).start()

try:
    while True:
        msg = input(PROMPT)
        if msg:
            ser.write((msg + "\n").encode())
except (KeyboardInterrupt, EOFError):
    print("\n[*] closing")
    ser.close()