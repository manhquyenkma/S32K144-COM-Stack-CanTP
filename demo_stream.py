#!/usr/bin/env python3
"""
AUTOSAR Mock COM Stack - CanTp ASCII Image Transfer Demo
Stream 4 ASCII files via CanTp and verify 100% data integrity.
"""

import os
import sys
import subprocess
import time

FILES = [
    ("ascii_cat_512B_showcase.txt", "Small Showcase: ASCII Cat (512 Bytes)"),
    ("ascii_owl_2KB.txt", "Medium Showcase: ASCII Owl (2 KB)"),
    ("ascii_monalisa_refstyle_8KB.txt", "Large Showcase: ASCII Mona Lisa RefStyle (8 KB)"),
    ("ascii_monalisa_refstyle_16KB.txt", "XL Stress Showcase: ASCII Mona Lisa RefStyle (16 KB)")
]

def print_banner():
    print("=" * 72)
    print("  AUTOSAR Mock COM Stack v0.7 - CanTp ASCII File Streaming Demo")
    print("  Mentored Wire Format: SF(6B max), FF(6B payload), CF(7B payload), FC(CTS/BS=4/STmin=5ms)")
    print("=" * 72)

def run_demo():
    print_banner()

    exe_path = os.path.join(".", "demo_cantp_stream.exe")
    if not os.path.exists(exe_path):
        print("[-] Executable 'demo_cantp_stream.exe' not found. Compiling...")
        cmd = [
            "gcc", "-Iinclude", "demo_cantp_stream.c",
            "src/cantp/CanTp.c", "src/canif/CanIf.c", "src/canif/CanIf_Cfg.c",
            "src/pdur/PduR.c", "src/pdur/PduR_Cfg.c", "src/app/App.c",
            "-o", "demo_cantp_stream.exe"
        ]
        res = subprocess.run(cmd)
        if res.returncode != 0:
            print("[-] Compilation failed!")
            sys.exit(1)
        print("[+] Compiled demo_cantp_stream.exe successfully!\n")

    arg = "all"
    if len(sys.argv) > 1:
        arg = sys.argv[1]

    proc = subprocess.run([exe_path, arg], capture_output=False, text=True)
    if proc.returncode == 0:
        print("\n[+] DEMO SUCCESS: All requested ASCII files transferred and verified via CanTp!")
    else:
        print(f"\n[-] DEMO COMPLETED WITH RETURN CODE {proc.returncode}")

if __name__ == "__main__":
    run_demo()
