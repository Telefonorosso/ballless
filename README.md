# Emu68 USB Mouse - AI CODED

**Use a standard USB mouse directly on a PiStorm-equipped Amiga — no AmigaOS driver required.**

This Emu68 experiment adds native USB host mouse support to **PiStorm Classic + Raspberry Pi 3A+**.

A normal wired USB HID mouse can be connected to the Pi and used directly in Workbench and Amiga software.

The physical Amiga mouse remains usable at the same time.

---

## What it does

The path is:

```text
USB HID mouse
    |
    v
Raspberry Pi DWC2
    |
    v
TinyUSB host stack
    |
    v
Emu68 USB mouse service
    |
    v
virtual Amiga mouse state
    |
    v
JOY0DAT / CIA / POTGOR
    |
    v
AmigaOS / Workbench
```

No AmigaOS USB stack or companion program is required.

From the Amiga point of view, the USB mouse behaves like native mouse hardware.

---

## Hardware validated

This milestone was validated on:

```text
Amiga 600
PiStorm Classic
Raspberry Pi 3A+
wired USB HID mouse
```

Validated behaviour:

- USB host mode on Pi 3A+;
- wired HID mouse enumeration;
- HID Boot Mouse reports;
- smooth Workbench pointer movement;
- physical Amiga mouse still usable;
- USB left button;
- USB right button;
- no AmigaOS-side driver;
- no AmigaOS-side helper program.

The mouse wheel is intentionally not implemented in this milestone.

---

## How movement reaches the Amiga

USB movement is translated into the same hardware-visible state that classic Amiga software already understands.

Movement is exposed through:

```text
JOY0DAT
$DFF00A
```

The USB mouse does not replace the original mouse completely.

Instead, Emu68 overlays the USB-generated movement onto the normal Amiga hardware reads, allowing the physical mouse to remain usable.

---

## Mouse buttons

The current milestone implements both main buttons.

### Left button

Mapped to:

```text
CIAAPRA
$BFE001
bit 6
active low
```

### Right button

Mapped to:

```text
POTGOR
$DFF016
bit 10
active low
```

This means software sees the USB buttons through the same classic hardware registers it already expects.

---

## Runtime behaviour

The USB subsystem is serviced independently of the 68k JIT.

A dedicated CPU2 housekeeper handles DWC2 and TinyUSB at approximately:

```text
1 kHz
```

USB mouse movement is accumulated at HID report rate and published toward the Amiga at:

```text
50 Hz
```

This avoids putting USB polling into normal 68k execution.

The milestone uses a 1:2 movement sensitivity with signed remainder preservation, which gives smooth motion without losing fractional movement over time.

---

## Startup

USB initialization happens automatically during Emu68 startup.

The current normal build does not display the old diagnostic splash and does not pause for diagnostics.

Once Emu68 is running, a compatible wired USB mouse can enumerate and begin controlling the Amiga pointer.

---

## TinyUSB

The project uses a deliberately reduced TinyUSB host stack containing only the pieces required for:

- USB host operation;
- HID host support;
- DWC2;
- common TinyUSB infrastructure.

This is not an untouched upstream TinyUSB snapshot.

The included DWC2 and host files contain adaptations required for Emu68's AArch64 big-endian environment.

Do not replace these files with stock upstream TinyUSB without reapplying those adaptations.

---

## Architecture

At a high level:

```text
                  USB mouse
                      |
                      v
                +-----------+
                |   DWC2    |
                | USB host  |
                +-----------+
                      |
                      v
                +-----------+
                | TinyUSB   |
                | HID host  |
                +-----------+
                      |
                      v
                +-----------+
                |   CPU2    |
                | ~1 kHz    |
                +-----------+
                      |
                      v
             accumulated motion
               + button state
                      |
                      v
                +-----------+
                | vectors.c |
                +-----------+
                      |
          +-----------+-----------+
          |                       |
          v                       v
       JOY0DAT              CIA / POTGOR
       movement              mouse buttons
```

The hardware-facing overlay is intentionally small.

USB protocol handling stays in the background service rather than being spread through the emulator.

---

## Building

Use the normal Emu68 PiStorm Classic build environment.

Configure:

```bash
rm -rf build

cmake -S . -B build \
  -DTARGET=raspi64 \
  -DVARIANT=pistorm-classic \
  -DCMAKE_TOOLCHAIN_FILE=toolchains/aarch64-linux-gnu.cmake
```

Build:

```bash
cmake --build build -j$(nproc)
```

The resulting Emu68 image contains USB mouse host support.

As with any experimental Emu68 build, keep a known-good image available for rollback.

---

## Source layout

This repository/overlay keeps the files in their normal Emu68 locations:

```text
CMakeLists.txt

src/
├── aarch64/
│   └── vectors.c
├── pistorm/
│   └── ps_classic_protocol.c
└── raspi/
    ├── start_rpi64.c
    ├── support_rpi.c
    ├── tusb_config.h
    ├── usb_mouse_host.c
    └── tinyusb/
        └── src/
            ├── class/hid/
            ├── common/
            ├── host/
            ├── osal/
            ├── portable/synopsys/dwc2/
            ├── tusb.c
            ├── tusb.h
            └── tusb_option.h
```

The included TinyUSB subtree is intentionally minimal rather than a full vendor import.

---

## Important files

### `src/raspi/usb_mouse_host.c`

Main Emu68 USB mouse integration.

It handles initialization, HID reports, movement accumulation and button state.

### `src/pistorm/ps_classic_protocol.c`

Runs the CPU2 runtime USB/TinyUSB servicing loop.

### `src/aarch64/vectors.c`

Publishes already-prepared USB mouse state through classic Amiga hardware-visible registers.

### `src/raspi/tinyusb/.../hcd_dwc2.c`

Contains the DWC2 host-controller path adapted for the Emu68 environment.

### `src/raspi/support_rpi.c`

Provides the Raspberry Pi firmware USB power helper required by the USB host path.

---

## Diagnostics

Development versions included a visible diagnostic splash and tracing.

The milestone keeps diagnostic support in the source but disables it by default:

```text
EMU68_USBMOUSE_DIAGNOSTICS=0
```

With diagnostics disabled:

- no 10-second startup wait is executed;
- no diagnostic splash appears;
- unused diagnostic helpers are removed by preprocessing;
- the tree builds cleanly with Emu68's `-Werror` policy.

Diagnostics can be re-enabled in the relevant source files when debugging is needed.

---

## Current limitations

This remains an experimental milestone.

Known intentional limitations:

- mouse wheel not implemented;
- designed and validated with a wired USB HID mouse;
- broader HID compatibility has not been exhaustively tested;
- hotplug and unusual USB error cases are still experimental;
- only the required TinyUSB host subset is included;
- this is not yet an upstream Emu68 feature.

---

## Why this is useful

The experiment is interesting because it gives old Amiga software a new physical input source without requiring that software to know anything about USB.

Workbench, games and applications continue to read the same mouse hardware state they always did.

Emu68 simply supplies an additional source for that state.

This makes USB mouse support feel much closer to a hardware extension than to a conventional operating-system USB driver.

---

## Milestone

```text
Emu68 PiStorm Classic USB Mouse
POC12
Left + Right buttons
No diagnostic delay
Hardware validated
```

The milestone deliberately preserves the known-good DWC2/TinyUSB implementation rather than rebasing it onto newer upstream TinyUSB code.

---

## Credits

This is an unofficial experimental Emu68 / PiStorm extension.

It is not an official Emu68 release.

The project builds on:

- Emu68;
- PiStorm;
- TinyUSB;
- the Raspberry Pi DWC2 USB controller;
- the classic Amiga mouse hardware interface.

TinyUSB is used under its respective upstream license, with project-specific adaptations for the Emu68 environment.

Upstream Emu68:

```text
https://github.com/michalsc/Emu68
```

Upstream TinyUSB:

```text
https://github.com/hathach/tinyusb
```

---

## License

Files retain their respective upstream licenses.

Check the source headers and the licensing terms of Emu68, PiStorm and TinyUSB before redistributing complete builds or modified source trees.
