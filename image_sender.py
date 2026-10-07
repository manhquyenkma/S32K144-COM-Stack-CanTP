#!/usr/bin/env python3
"""
PC Image Sender — Send ASCII art files to Master ECU via UART.
Assignment v0.7 §5.1: [ImageLength:uint16_LE][Raw ASCII bytes]

Usage:
    python image_sender.py COM3 ascii_cat_512B_showcase.txt
    python image_sender.py COM3 ascii_owl_2KB.txt
    python image_sender.py COM3 ascii_monalisa_refstyle_8KB.txt
    python image_sender.py COM3 ascii_monalisa_refstyle_16KB.txt
    python image_sender.py COM3 all              # Send all 4 files sequentially
"""
import serial
import struct
import sys
import time
import os

FILES = [
    "ascii_cat_512B_showcase.txt",
    "ascii_owl_2KB.txt",
    "ascii_monalisa_refstyle_8KB.txt",
    "ascii_monalisa_refstyle_16KB.txt",
    "ascii-monalisa-130KB.txt",
]

def send_image(ser, filepath, chunk_delay=0.05):
    """Send one ASCII image file over UART with length header."""
    if not os.path.exists(filepath):
        print(f"[-] File not found: {filepath}")
        return False

    with open(filepath, 'rb') as f:
        data = f.read()

    length = len(data)
    print(f"\n{'='*60}")
    print(f"[PC] Sending '{os.path.basename(filepath)}' ({length} bytes)")
    print(f"{'='*60}")

    # Flush serial buffers and ensure clean start
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    time.sleep(0.1)

    # Send length header
    if length < 0xFFFF:
        # Standard 2-byte length header (little-endian uint16)
        ser.write(struct.pack('<H', length))
    else:
        # Extended length header: 0xFFFF marker followed by 4-byte uint32 LE length
        ser.write(struct.pack('<H', 0xFFFF))
        ser.write(struct.pack('<I', length))
    time.sleep(0.05)

    # Send raw image data in small UART blocks (up to 60 bytes per CanTp N-SDU)
    block_size = 60
    sent = 0
    for i in range(0, length, block_size):
        block = data[i:i + block_size]
        ser.write(block)
        sent += len(block)
        pct = 100 * sent // length
        bar = '█' * (pct // 5) + '░' * (20 - pct // 5)
        print(f"\r  [PC] [{bar}] {pct:3d}% ({sent}/{length} bytes)", end='', flush=True)
        time.sleep(chunk_delay)

    print(f"\n[PC] ✓ Transfer complete: {sent} bytes sent")

    # Wait for CanTp to finish processing on MCU side
    time.sleep(1.0)
    return True


def main():
    if len(sys.argv) < 3:
        print("AUTOSAR Mock COM Stack — PC Image Sender for CanTp Demo")
        print()
        print("Usage:")
        print("  python image_sender.py <COM_PORT> <FILE | all>")
        print()
        print("Examples:")
        print("  python image_sender.py COM3 ascii_cat_512B_showcase.txt")
        print("  python image_sender.py COM3 all")
        print()
        print("Available files:")
        for f in FILES:
            size = os.path.getsize(f) if os.path.exists(f) else "?"
            print(f"  - {f}  ({size} bytes)")
        sys.exit(1)

    port = sys.argv[1]
    target = sys.argv[2]

    # Optional: chunk delay in ms (default 150ms gives CanTp enough time to finish each 60B chunk
    # A 60B N-SDU requires 1 FF + 8 CF + 2 FC = 11 frames, at STmin=5ms ≈ 60-80ms total)
    chunk_delay = 0.15
    if len(sys.argv) > 3:
        chunk_delay = float(sys.argv[3]) / 1000.0

    print(f"[PC] Opening {port} at 115200 8N1 (DTR disabled to prevent board reset)...")
    try:
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = 115200
        ser.timeout = 1
        ser.write_timeout = 10
        ser.dsrdtr = False   # Do NOT toggle DTR — prevents MCU reset on OpenSDA
        ser.dtr = False
        ser.rts = False
        ser.open()
    except Exception as e:
        print(f"[-] Cannot open {port}: {e}")
        sys.exit(1)

    print("[PC] Waiting for MCU boot initialization...")
    time.sleep(1.8)  # Allow board to finish 1.5s boot countdown into Master Role
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    if target == "all":
        for f in FILES:
            send_image(ser, f, chunk_delay)
            time.sleep(2.0)  # Pause between files
    else:
        send_image(ser, target, chunk_delay)

    ser.close()
    print(f"\n[PC] Done. Port {port} closed.")


if __name__ == "__main__":
    main()
