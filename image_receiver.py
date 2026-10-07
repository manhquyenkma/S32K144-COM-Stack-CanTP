#!/usr/bin/env python3
"""
PC Image Receiver — Receive ASCII art streamed from Slave ECU (Role 2) via UART.
Displays the ASCII artwork live on the terminal and saves it to a file.

Usage:
    python image_receiver.py <COM_PORT> [OUTPUT_FILE]

Example:
    python image_receiver.py COM4
    python image_receiver.py COM4 received_art.txt
"""
import sys
import os
import time

try:
    import serial
except ImportError:
    print("[-] pyserial not found. Please run: pip install pyserial")
    sys.exit(1)


def main():
    if len(sys.argv) < 2:
        print("S32K144 COM Stack — PC Image Receiver")
        print()
        print("Usage:")
        print("  python image_receiver.py <COM_PORT> [OUTPUT_FILE]")
        print()
        print("Examples:")
        print("  python image_receiver.py COM4")
        print("  python image_receiver.py COM4 received_cat.txt")
        sys.exit(1)

    port = sys.argv[1]
    outfile = sys.argv[2] if len(sys.argv) > 2 else None

    print(f"======================================================================")
    print(f"   S32K144 COM Stack — Slave ASCII Image Receiver                     ")
    print(f"======================================================================")
    print(f"[PC] Opening {port} at 115200 8N1...")

    try:
        ser = serial.Serial(port, 115200, timeout=0.1)
    except Exception as e:
        print(f"[-] Cannot open {port}: {e}")
        sys.exit(1)

    print(f"[PC] Listening on {port}. Waiting for ASCII artwork from Slave ECU...")
    print(f"[PC] (Press Ctrl+C to stop listening)")
    print(f"======================================================================\n")

    total_bytes = 0
    captured_data = bytearray()
    last_recv_time = None

    try:
        while True:
            chunk = ser.read(128)
            if chunk:
                # Print directly to stdout
                try:
                    sys.stdout.write(chunk.decode('ascii', errors='replace'))
                    sys.stdout.flush()
                except Exception:
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.flush()

                captured_data.extend(chunk)
                total_bytes += len(chunk)
                last_recv_time = time.time()
            else:
                # If idle for 2 seconds after receiving data, print summary
                if last_recv_time and (time.time() - last_recv_time > 2.0) and total_bytes > 0:
                    print(f"\n\n======================================================================")
                    print(f"[PC] Stream transfer finished. Total bytes received: {total_bytes}")
                    if outfile:
                        with open(outfile, 'wb') as f:
                            f.write(captured_data)
                        print(f"[PC] Saved received artwork to: {outfile}")
                    print(f"======================================================================\n")
                    last_recv_time = None
                    total_bytes = 0
                    captured_data.clear()

    except KeyboardInterrupt:
        print(f"\n\n[PC] Stopped by user.")
        if outfile and captured_data:
            with open(outfile, 'wb') as f:
                f.write(captured_data)
            print(f"[PC] Saved {len(captured_data)} bytes to: {outfile}")
    finally:
        ser.close()


if __name__ == '__main__':
    main()
