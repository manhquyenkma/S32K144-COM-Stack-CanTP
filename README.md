# S32K144 Mock COM Stack & ISO 15765-2 CanTP

## NXP S32K144 Multi-ECU Distributed Network Project

**Author:** QUYENNM8  
**Platform:** NXP S32K144 EVB-Q100 (ARM Cortex-M4F)  
**IDE & Toolchain:** S32 Design Studio for ARM v3.4 / GCC 9.2 (arm-none-eabi)  
**Communication Bus:** CAN Classic 500 kbps (SOSC 8 MHz external crystal)  

---

## 1. Project Introduction

This project implements an embedded automotive **Mock COM Stack (AUTOSAR-like architecture)** and **ISO 15765-2 Transport Protocol (CanTP)** on bare-metal NXP S32K144 microcontrollers.

### Key Capabilities:

- **KeepAlive Synchronization**: Master broadcasts ADC-modulated heartbeat messages (`CAN ID 0x100`, $10\text{ ms}$) to synchronize slave LED blink frequencies.
- **Slave Health & Deadline Supervision**: Slaves report online health status (`CAN ID 0x201` / `0x202`, $500\text{ ms}$). Master detects offline slaves after $2000\text{ ms}$ silence.
- **Large ASCII Image Streaming**: Reliable multi-frame transfer of large ASCII artwork (from $512\text{ B}$ up to $130\text{ KB}$) segmented into CanTP N-SDUs and streamed directly to a PC display terminal in real time.
- **Hardware-Buffered FlexCAN Driver**: 8-mailbox hardware receive FIFO (`MB1` through `MB8`) prevents dropped frames during high-speed $ST_{\min} = 5\text{ ms}$ burst transfers.
- **100% Test Coverage**: Verified by 25 comprehensive unit tests (14 CanTP + 11 COM & App tests).

---

## 2. Hardware Setup & Connection Guide

### 2.1 Requirements

1. **2x S32K144 EVB boards** (Board 1 = Master ECU, Board 2 = Slave 1 ECU).
2. **12V DC External Power Supply** connected to barrel jack **J16** on both boards *(mandatory: the on-board UJA1169 CAN physical transceiver requires 12V to power the CAN bus driver)*.
3. **CAN Bus Wiring**:
   - `CAN_H` connected to `CAN_H` (pin 1 of header J13).
   - `CAN_L` connected to `CAN_L` (pin 2 of header J13).
   - `GND` common ground wire connected between both boards.
   - Verify on-board $120\ \Omega$ termination jumpers (**J107**, **J108**) are fitted on both ends of the CAN bus.
4. **2x Micro-USB cables** connected to OpenSDA ports for programming and UART terminals.

### 2.2 Board Role Assignment

A single firmware binary ([`Debug_FLASH/MOCK_PROJECT.elf`](file:///c:/Users/PAV/workspaceS32DS.3.4/MOCK_PROJECT/Debug_FLASH/MOCK_PROJECT.elf)) runs on all boards:

- **Master ECU (COM11)**: Leave buttons unpressed. Board automatically boots into **Role 1 (Master)** by default.
- **Slave 1 ECU (COM9)**: Upon boot, press key `2` in the terminal (or hold hardware button **SW3**) to select **Role 2 (Slave 1)**.

---

## 3. Directory Layout

```
MOCK_PROJECT/
├── architecture.md             # Detailed system architecture & sequence diagrams
├── README.md                   # Project overview & running instructions
├── image_sender.py             # PC Python tool to stream ASCII files to Master
├── image_receiver.py           # PC Python tool to receive & capture stream from Slave
│
├── include/                    # Header files
│   ├── app/                    # Application layer (App.h, Role.h, UartRxQueue.h)
│   ├── cantp/                  # CanTP protocol headers & ISO 15765-2 config
│   ├── com/                    # COM layer headers & signal matrix
│   ├── pdur/                   # PDU Router configuration
│   ├── canif/                  # CAN Interface abstraction
│   ├── candrv/                 # FlexCAN bare-metal driver API
│   └── trace/                  # Non-blocking UART trace logging
│
├── src/                        # Source implementations
│   ├── main.c                  # Main scheduler loop & startup role selection
│   ├── Platform_Init.c         # SCG Clock & Peripheral initialization
│   ├── app/                    # App tasks, image sender/receiver, UART queue
│   ├── cantp/                  # CanTP state machine, timers, fragmentation
│   ├── com/                    # Signal packing/unpacking, UpdateBits, deadlines
│   ├── pdur/                   # Zero-copy PDU routing
│   ├── canif/                  # HTH/HRH mapping & filtering
│   ├── candrv/                 # Bare-metal FlexCAN driver (SOSC 8MHz, 8 Rx MBs)
│   └── trace/                  # Ring-buffer LPUART1 driver
│
├── tests/                      # Automated Test Suites
│   └── unit/                   # Host unit tests (Test_CanTp.c, Test_Com.c)
│
├── Debug_FLASH/                # Pre-built target ELF binary & S32DS makefiles
│   └── MOCK_PROJECT.elf        # Ready-to-flash target executable
│
└── test_images/                # Sample ASCII artwork test files
    ├── ascii_cat_512B_showcase.txt      (512 B - Basic test)
    ├── ascii_owl_2KB.txt                (2 KB - Multi N-SDU test)
    ├── ascii_monalisa_refstyle_16KB.txt (16 KB - Large showcase)
    └── ascii-monalisa-130KB.txt         (130 KB - XXL Stress showcase)
```

---

## 4. How to Build

### Option A: Using S32 Design Studio (GUI)

1. Open **S32 Design Studio for ARM v3.4**.
2. Select **File $\to$ Import $\to$ Existing Projects into Workspace**.
3. Browse to `c:\Users\PAV\workspaceS32DS.3.4\MOCK_PROJECT`.
4. Right-click project $\to$ **Build Project** (Configuration: `Debug_FLASH`).
5. Output binary generated: `Debug_FLASH/MOCK_PROJECT.elf` (0 errors, 0 warnings).

### Option B: Using Command Line (Make)

Run the following in Windows Command Prompt:

```cmd
set PATH=C:\NXP\S32DS.3.4\S32DS\build_tools\msys32\usr\bin;C:\NXP\S32DS.3.4\S32DS\build_tools\gcc_v9.2\gcc-9.2-arm32-eabi\bin;%PATH%
make -C Debug_FLASH all
```

### Option C: Running Host Unit Tests

Compile and run the 25 unit tests natively on PC (using MinGW GCC):

```cmd
gcc -Iinclude -Itests/unit tests/unit/test_all_host_runner.c tests/unit/Test_CanTp.c tests/unit/Test_Com.c src/cantp/CanTp.c src/com/Com.c src/com/Com_Cfg.c src/pdur/PduR.c src/pdur/PduR_Cfg.c src/canif/CanIf.c src/canif/CanIf_Cfg.c src/app/App.c src/app/UartRxQueue.c src/app/Role.c -o tests/unit/test_all_host.exe
tests\unit\test_all_host.exe
```

Expected output: **`ALL 14 CANTP TESTS PASSED (0 FAILURES)!`** and **`All Unit Tests PASSED.`**

---

## 5. How to Flash and Run

### Step 1: Flash Firmware

Flash `Debug_FLASH/MOCK_PROJECT.elf` to **both** S32K144 boards using S32DS Debugger or PE Micro GDB Server.

### Step 2: Open Display Terminal on Slave 1 (COM9)

1. Open **Tera Term** on `COM9` (Slave 1) with configuration:
   - **Baud Rate:** `115200`
   - **Data:** 8 bit, **Parity:** None, **Stop:** 1 bit, **Flow Control:** None
2. Reset or power on Slave 1. During the 1.5-second prompt, press key **`2`**.
3. The terminal displays:
   ```
   ========================================================
   >>> ASCII ART DISPLAY TERMINAL READY <<<
   ========================================================
   ```

   *(All diagnostic trace logging is automatically muted on Slave 1 to ensure a clean display channel for incoming ASCII artwork).*

### Step 3: Power On Master (COM11)

- Reset or power on Master.
- Do not touch any keys; Master automatically enters **Role 1 (Master ECU)** after 1.5 seconds.
- Turn the analog potentiometer (ADC0) on Master: Observe the Green LED on Slave 1 changing its blink frequency dynamically in sync with the potentiometer!

---

## 6. Streaming ASCII Images via CanTP

With Slave 1 listening on Tera Term (`COM9`), run `image_sender.py` from your PC command prompt:

### Basic Test (512 B):

```bash
python image_sender.py COM11 ascii_cat_512B_showcase.txt
```

### Medium Test (2 KB):

```bash
python image_sender.py COM11 ascii_owl_2KB.txt
```

### Showcase Test (16 KB):

```bash
python image_sender.py COM11 ascii_monalisa_refstyle_16KB.txt
```

### XXL Stress Showcase (130 KB):

```bash
python image_sender.py COM11 ascii-monalisa-130KB.txt
```

### Observation:

- The progress bar on PC reaches `100% (133132/133132 bytes) ✓ Transfer complete`.
- In Tera Term (`COM9`), the ASCII Mona Lisa artwork renders smoothly and cleanly from top to bottom with **zero corruption** and **zero log pollution**.

---

## 7. LED Diagnostic Indicators

| LED Color       | Node        | State         | Meaning                                                                                                                               |
| --------------- | ----------- | ------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| **Green** | Slave 1 / 2 | Blinking      | **NORMAL**: Synchronized KeepAlive messages received from Master. Blink speed reflects potentiometer RateLevel ($0 \dots 6$). |
| **Red**   | Slave 1 / 2 | Blinking / ON | **MASTER_LOST**: No KeepAlive received for $>2000\text{ ms}$ (CAN disconnected or Master unpowered).                          |
| **Blue**  | Master      | Toggling      | **HEARTBEAT**: Master KeepAlive broadcast activity indicator.                                                                   |

---

## 8. Technical Architecture Reference

For exhaustive architectural design notes, sequence diagrams, timing budgets, and layer-by-layer specifications, please refer to [`architecture.md`](file:///c:/Users/PAV/workspaceS32DS.3.4/MOCK_PROJECT/architecture.md).
