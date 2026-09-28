#!/usr/bin/env python3
"""
Store Wi-Fi credentials on the CO2 monitor without them touching source
control or this terminal's history.

    tools/wifi_setup.py            # prompts for SSID and (hidden) password
    tools/wifi_setup.py --clear    # forget the stored network
    tools/wifi_setup.py --status   # ask the board how the link is doing

The credentials go over the USB serial link into the board's NVS. They are
never written to disk on this machine.
"""
import argparse, getpass, sys, time
import serial

def open_quiet(port):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 2
    s.dtr = False; s.rts = False
    s.open()
    return s

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-P", "--port", default="/dev/ttyUSB0")
    ap.add_argument("--clear", action="store_true")
    ap.add_argument("--status", action="store_true")
    a = ap.parse_args()

    s = open_quiet(a.port)
    time.sleep(6.5)                    # the CH340 resets the board on open; let it boot
    s.reset_input_buffer()

    if a.clear:
        s.write(b"wifi clear\n")
    elif a.status:
        s.write(b"wifi\n")
    else:
        ssid = input("Wi-Fi SSID: ").strip()
        if not ssid: sys.exit("no SSID given")
        pw = getpass.getpass("Wi-Fi password (hidden): ")
        if " " in ssid or " " in pw:
            sys.exit("spaces in the SSID or password are not supported by the serial command yet")
        s.write(f"wifi {ssid} {pw}\n".encode())

    t0 = time.time()
    while time.time() - t0 < 25:
        line = s.readline().decode("utf-8", "replace").rstrip()
        if line.startswith("net:") or line.startswith("wifi:"):
            print(line, flush=True)
            if "online" in line or "cleared" in line or "status" in line and a.status: break
            if "failed" in line: break

if __name__ == "__main__":
    main()
