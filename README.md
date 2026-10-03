# RTR-OS — Real-time Raspberry Operating System

A real-time operating system for the **Raspberry Pi 4 Model B**, written in C, aimed at control and automation.

RTR-OS starts from two ideas:

- **The system is synchronous.** All cores read the same hardware counter, driven by the board crystal. Instants are always absolute, so a delay in one cycle never accumulates into the next.
- **The processor core is the unit of guarantee.** A process can be given a whole core, with no scheduler, no tick and no interrupts at all, and run on it in real time. The other cores work concurrently, under fixed priority with a time limit enforced by the kernel.

The project is at an early stage. The [Current state](#current-state) section says exactly what works and what has not been verified yet.

## Contents

- [Architecture](#architecture)
- [Current state](#current-state)
- [Repository layout](#repository-layout)
- [Required tools](#required-tools)
- [Build](#build)
- [Test in the emulator](#test-in-the-emulator)
- [Run on the board](#run-on-the-board)
- [Coding rules](#coding-rules)
- [Roadmap](#roadmap)
- [Documentation](#documentation)
- [License](#license)

## Architecture

The full design, with the reason behind each choice, is in [`docs/PLANO.md`](docs/PLANO.md) (in Portuguese, the language of the planning discussion). In short:

| Topic | Decision |
|-------|----------|
| Cores | Core 0 belongs to the kernel and the system. Each of cores 1 to 3 can be **dedicated** to one process or **shared** among tasks. The allocation is fixed at boot |
| Dedicated core | The process runs isolated (EL0) on a core that receives no interrupts in normal operation. It reads the counter and accesses its own devices directly, without entering the kernel. Another core supervises it through a heartbeat |
| Shared cores | Fixed priority with preemption. Every task has a CPU time limit per period; one that exceeds it is suspended until the next period. If the limits add up to more than 100%, the system scales them down |
| Isolation | Kernel in EL1, processes in EL0, each in its own address space. Permissions are a fixed table, applied at boot |
| Drivers | Live in processes. The kernel keeps only the timer, the interrupt controller and a debug console |
| Communication | Lock-free shared-memory channels, created by the kernel. The real-time process never waits |
| Programs | Files on the SD card, loaded by the system according to a manifest read at boot |
| Observability | The kernel offers one interface for any process to push data and another to read it, plus a parameter interface in the opposite direction |
| Web interface | Served by the board itself, over Ethernet, by shared-core processes. It is how the system is operated and monitored |

## Current state

Stages 0 to 5 of the plan, implemented and verified **in the emulator**: the system comes up and the statistics page opens in the browser. Nothing has been verified on the physical board yet.

The system today:

- boots in EL1, initializes the serial console (UART0, 115200 8N1) and turns on the MMU and the caches, with per-section permissions;
- runs isolated processes in EL0, each in its own address space, loaded from programs embedded in the image;
- schedules by fixed priority with preemption, with a time limit per period and scaling when the declared load exceeds the ceiling;
- terminates a process that faults and contains one that overruns its limit, without affecting the others;
- gives a process the registers of a device and memory to exchange data with it;
- starts a single built-in process, `system` (the boot set), which owns the SD card and the Ethernet controller;
- reads the installation manifest, `MANIFEST.TXT`, from the SD card (writing the default one if the card has none), asks the kernel to create the processes it describes and seals the installation;
- runs the network stack (lwIP) and a web server in the `system` process, with its own Ethernet driver;
- serves a web interface with two tabs: **Statistics** (measured by the kernel, also as JSON at `/api/stats`) and **Configuration**, which edits everything the manifest holds and, on save, writes it to the card and reboots the board. Three themes: light, dark and amber.

To see it working:

```powershell
scripts\build.ps1
wsl -d Ubuntu -- bash /mnt/d/Tootega/Source/RTR-SO/scripts/make-sd-image.sh   # once: card image for the emulator
scripts\run-web.ps1        # then open http://localhost:8080/
```

Applications are installed from the browser as `.rpkg` packages ("Install app" in the Configuration tab): the package is unpacked onto the card. The build produces one package per program in `build\` (`pwm.rpkg`, for instance); `tools\rpkg\make-rpkg.py` builds packages with any set of files. Programs are `.BIN` files on the card; the only program inside the kernel image is the boot set. Each program's header says whether it was written for real-time scheduling (`REALTIME` in `rtr_add_program`), and the manifest gives each process a class, `realtime` or `standard`: real-time processes run before every standard one, and a program without the flag cannot be given the real-time class. The Configuration tab lists the programs found on the card when a process is added.

What does **not** exist yet: interrupt delivery to processes, multiple cores, the dedicated core, channels. See the [Roadmap](#roadmap).

## Repository layout

```text
kernel/
  boot/       assembly entry, exception vectors and context switch, embedded programs
  board/      board devices used by the kernel: UART, GIC
  core/       scheduler, processes, MMU, timer, system calls, console
  include/    headers; arch.h concentrates processor access
abi/          interface between the kernel and the programs
user/
  lib/        program startup, system calls, part of the C library
  demo/       console report and test program (standard class)
  pulse/      periodic job with bounded work (real-time class)
  pwm/        software PWM on a GPIO pin (real-time class)
tools/
  qemu-pi4/   rtr-scope, the GPIO capture device added to the emulator
  rpkg/       application package builder
  scope/      the virtual oscilloscope
  system/     the boot set: SD card driver, FAT32, manifest, Ethernet driver, lwIP port, web server, page
cmake/        cross-compilation setup
scripts/      build, emulator tests, SD card assembly
sdcard/       config.txt for the board firmware
docs/         project plan and requirements
```

## Required tools

Development is done on Windows, with PowerShell 7.

| Tool | Use | Install |
|------|-----|---------|
| LLVM (clang, lld, clang-tidy) | AArch64 cross compilation and static analysis | `winget install LLVM.LLVM` |
| CMake and Ninja | Build system | `winget install Kitware.CMake Ninja-build.Ninja` |
| QEMU 9.0 or newer | Raspberry Pi 4 emulation (`raspi4b`) | `winget install SoftwareFreedomConservancy.QEMU` |
| WSL with Ubuntu | Building and running qemu-pi4, the emulator with networking | `wsl --install -d Ubuntu` |

The scripts look for LLVM in `C:\Program Files\LLVM\bin` and QEMU in `C:\Program Files\qemu` when they are not on the `PATH`.

To run on the board:

- Raspberry Pi 4 Model B and power supply;
- microSD card formatted as FAT32;
- **3.3 V** USB-serial adapter (a 5 V adapter can damage the board).

## Build

```powershell
scripts\build.ps1
```

Produces `build\kernel8.img`, the image the board firmware loads, and `build\kernel8.elf`, with symbols for debugging. The build fails on any compiler or static analyzer warning. The first configuration downloads the lwIP stack (version 2.2.1) with git.

## Test in the emulator

### Emulator with networking: qemu-pi4

The official QEMU does not emulate the Pi 4 Ethernet controller. For the network stages the project uses [qemu-pi4](https://github.com/kmehltretter82/qemu-pi4), a QEMU 11.1 fork that also emulates the board's Ethernet, USB, GPIO, SPI and PWM. It has no prebuilt binary; it is built once, inside WSL (Ubuntu), without `sudo`:

```powershell
wsl -d Ubuntu -- bash /mnt/d/Tootega/Source/RTR-SO/scripts/build-qemu-pi4.sh
```

The script creates `~/rtr-tools` in WSL with its own build environment (micromamba and conda-forge), the source and the build. After that:

```powershell
scripts\run-web.ps1               # system up, web interface at http://localhost:8080/, Ctrl+C stops
scripts\test-qemu-pi4.ps1         # 6 seconds, no interaction, serial console shown at the end
scripts\test-qemu-pi4.ps1 -Seconds 15
scripts\screenshot.ps1            # captures the web interface in the three themes (needs Edge)
```

The emulated SD card is `build/sd.img`, created by `scripts/make-sd-image.sh` (inside WSL; put a `sdcard/manifest.txt` next to it to start from a given manifest). The scripts attach it when it exists. Two more test scripts run inside WSL: `scripts/test-web.sh` requests the page and the JSON, and `scripts/test-config.sh` posts a modified manifest and shows the console across the reboot it triggers.

In this emulator the counter runs at 54000000 Hz, as on the board, and the board gets its address by DHCP (10.0.2.15 inside the emulated network).

### Virtual oscilloscope

The emulator build includes a probe, `rtr-scope`, that records every GPIO transition with its virtual-time instant. `scripts\run-web.ps1 -Scope` runs the system with the probe on; `scripts\scope.ps1` then renders `build\scope.log` into a page with the waveforms, pan and zoom, cursors and, per pin, frequency, duty cycle and period jitter, and opens it in the browser. `scripts\test-scope.sh` (inside WSL) is a complete example: it runs the `pwm` program on GPIO 18 at 1 kHz, 30 % duty, and renders the capture; with an icount shift as second argument (`test-scope.sh 12 3`) the emulator's deterministic clock gives a clean waveform. The oscilloscope shows the logic of the program, not the timing of the real board.

### Official QEMU

Without networking, but with a deterministic virtual clock:

```powershell
scripts\test-qemu.ps1             # 6 seconds, Windows clock
scripts\test-qemu.ps1 -Virtual    # deterministic virtual clock
scripts\run-qemu.ps1              # interactive; to quit: Ctrl+A then X
scripts\run-qemu.ps1 -Gdb         # waits for a debugger on localhost:1234
```

With the Windows clock the emulator delivers timer interrupts 1 to 2 ms late and skips periods; that is an effect of the Windows scheduler, not of the kernel. With `-Virtual`, time advances by instruction count and the measured delay is constant.

Memory protection test, which builds a kernel that tries to write to its own code and checks that the MMU blocks the write:

```powershell
scripts\test-fault.ps1
```

### Limits of the emulators

- they validate logic, not time; no latency measured in them holds for the board;
- the qemu-pi4 network model was written from the Linux driver, without manufacturer documentation; a driver may work in it and fail on the board.

## Run on the board

1. Assemble the card contents. The script downloads, once, the firmware files from the official Raspberry Pi repository and puts `config.txt` and `kernel8.img` next to them:

   ```powershell
   scripts\make-sdcard.ps1               # only assembles in build\sdcard
   scripts\make-sdcard.ps1 -Drive E:     # assembles and copies to the card in E:
   ```

   With `-Drive`, the script refuses drives that are not removable or not FAT32.

2. Connect the USB-serial adapter to the board's GPIO header:

   | Adapter | Board pin |
   |---------|-----------|
   | GND | 6 (GND) |
   | RX | 8 (GPIO14, TXD) |
   | TX | 10 (GPIO15, RXD) |

   Do not connect the adapter's 5 V wire.

3. Open a serial terminal at 115200 baud, 8 bits, no parity, 1 stop bit.

4. Insert the card into the board and power it on. On the first boot the system writes `MANIFEST.TXT` to the card with the default installation; once the network gets an address, the console prints the URL of the web interface, where the manifest can be edited.

## Coding rules

Mandatory in the kernel, checked on every build:

- no recursion;
- every loop has a fixed bound;
- no dynamic memory allocation after startup;
- fixed-width types and no undefined behavior of the language;
- every return value checked; warnings treated as errors.

Two endless loops are allowed in the kernel: the idle loop and the system halt. Inline assembly and integer-to-pointer conversions are confined to `kernel/include/arch.h`.

The list of static analyzer checks is in [`.clang-tidy`](.clang-tidy). Third-party code (lwIP) is not held to these rules; it runs isolated in a process.

## Roadmap

The first goal was the system up and reachable from the browser; the dedicated cores come next.

| Stage | Deliverable | Status |
|-------|-------------|--------|
| 0 | Bench: toolchain, emulator, SD card, serial console | Verified in the emulator |
| 1 | Base: MMU and caches, exceptions, GIC, timer | Verified in the emulator |
| 2 | Isolated processes and fixed priority with time limits | Verified in the emulator |
| 3 | Devices in processes and the permission table | Partial: interrupt delivery to processes is missing |
| 4 | Network: Ethernet driver and lwIP stack | Verified in the emulator |
| 5 | Web service and statistics page | Verified in the emulator |
| 6 | Observability and parameters | To do |
| 7 | Multicore | To do |
| 8 | Hardware floor and the dedicated core | To do |
| 9 | Channels between cores | To do |
| 10 | Loading programs from the SD card and the manifest | Verified in the emulator |
| 11 | Automation I/O: GPIO, serial, SPI, I2C | To do |

The acceptance criteria of each stage are in the plan.

## Documentation

- [`docs/PLANO.md`](docs/PLANO.md) — decisions, requirements, architecture, stages and risks (Portuguese).
- [`docs/ESTATISTICAS.md`](docs/ESTATISTICAS.md) — requirements of the system statistics page (Portuguese, proposal under review).

## License

Closed source. All rights reserved.

The network stack (lwIP) is third-party software distributed under the BSD license.
