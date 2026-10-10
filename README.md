# Morse Trainer

A Qt 6 / C++17 desktop trainer for CW with a **real straight key**.

- **Key inputs:** serial port modem line (CTS/DSR/DCD/RI), Linux GPIO line, or Space bar / mouse.
  Edge timestamps are taken in a dedicated thread (GPIO: by the kernel IRQ handler), so GUI load
  doesn't distort your timing.
- **Sidetone:** click-free sine (raised-cosine ramps), selectable output device, pitch, volume,
  buffer size (latency).
- **Adaptive decoder:** follows your speed (5–50+ WPM) by clustering recent marks into dits/dahs,
  and adapts word-space detection to your letter spacing (handles Farnsworth-style sending).
- **Paper tape view:** last 6 s of keying, dits blue / dahs orange on a 1-unit grid, plus live
  dah:dit **ratio** (ideal 3.0) and **weight** (dit ÷ element gap, ideal 1.0). This is the bit that
  actually fixes a straight-key fist.
- **Send drill (Koch):** shows a group, you key it, it's aligned and graded per character.
- **Receive (copy):** plays Koch groups with Farnsworth spacing, you type what you heard.
- Rolling accuracy, weakest characters, and a prompt to add the next Koch character at ≥ 90 %.
- Reference table (double-click to hear any character).

## Build on Fedora

```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtmultimedia-devel
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build          # decoder / timing / scoring unit tests
./build/morse-trainer
```

Needs Qt ≥ 6.4. No other dependencies (serial and GPIO use the kernel interfaces directly).

## Wiring the key

### Serial port (any PC, USB-serial adapter)

The app raises **DTR** and **RTS** and reads a status input. Wire the key between DTR and the
input line you pick in the app (default CTS):

| DB9 pin | Signal | Connect to |
|--------:|--------|------------|
| 4 | DTR (+V when raised) | key contact A |
| 8 | CTS | key contact B (default) |
| 6 | DSR | …or here |
| 5 | GND | not needed for the key |

Optional but recommended: a 10 kΩ resistor from the input pin to GND (pin 5) so the line reads
cleanly "up" when the key is open. Most adapters are fine without it.

- Permissions: `sudo usermod -aG dialout $USER`, then log out/in.
- **FTDI adapters** report status changes in USB packets every *latency timer* ms (default 16).
  Set it to 1 ms for accurate timing:
  `echo 1 | sudo tee /sys/bus/usb-serial/devices/ttyUSB0/latency_timer`
  (or a udev rule: `ACTION=="add", SUBSYSTEM=="usb-serial", DRIVER=="ftdi_sio", ATTR{latency_timer}="1"`).
- Uses `TIOCMIWAIT` (instant wake-up on edge) where the driver supports it; otherwise falls back to
  1 kHz polling automatically.
- If key-down shows as key-up, tick **Invert**.

### GPIO (single-board computers)

Key between a free GPIO pin and **GND**; leave *Active low* and *Internal pull-up* ticked. Find the
chip/line offset for your header pin with:

```bash
gpioinfo            # from libgpiod-tools; pick an unused line
```

then choose that `/dev/gpiochipN` and line offset in the app. Check the board's schematic to
confirm the pin is 3.3 V GPIO and not claimed by the device tree (an `EBUSY` error means it's taken).
Non-root access needs a udev rule, e.g. `SUBSYSTEM=="gpio", KERNEL=="gpiochip*", GROUP="dialout", MODE="0660"`.

## Using it

1. Pick the key source, port/line, press **Connect**. Key a few `E`s and `T`s — the tape should
   show blue and orange bars.
2. Set **Character speed** (e.g. 20 WPM) and **Effective speed** (Farnsworth, e.g. 10 WPM).
3. **Free keying:** send anything, watch ratio/weight. Aim for ratio ≈ 3.0 and weight ≈ 1.0.
4. **Receive** at Koch level 2 (K, M). When it says ≥ 90 %, raise *Characters* by one.
5. **Send drill:** key the shown group. Pause one word space or send the full group to grade it.

Settings are saved in `~/.config/morse-trainer/morse-trainer.conf`.

## Layout

```
src/morse.*          table, prosigns, PARIS/Farnsworth timing, edit distance   (plain C++)
src/decoder.*        adaptive straight-key decoder                             (plain C++)
src/trainer.*        Koch groups, alignment-based grading, stats               (plain C++)
src/keysource.*      backend interface + monotonic clock
src/serialkeysource.*  modem-status-line key (TIOCMIWAIT / polling, leading-edge debounce)
src/gpiokeysource.*  GPIO uAPI v2 key with kernel edge timestamps
src/tonegenerator.*  sample-accurate sidetone + playback (QAudioSink pull mode)
src/keyscope.*       paper-tape widget
src/mainwindow.*     UI
tests/core_test.cpp  unit tests for the plain C++ core
```
