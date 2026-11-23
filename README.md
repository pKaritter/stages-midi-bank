# Behringer Steps & Mutable Instruments Stages: Custom MIDI & Movement Looping Firmware

Custom firmware for the **Behringer Steps** and **Mutable Instruments Stages** Eurorack modules.

This firmware transforms one or two modules into a **24-channel MIDI controller**, an **analog CV gesture looper**, and an **analog CV macro source**—simultaneously outputting both analog control voltages ($0\text{V}$ to $+8\text{V}$) on the front jacks and MIDI CC over the rear 3.5mm jacks.

---

## Features

- **Movement Recording & Ping-Pong Looping (Faders & Knobs — Simultaneously!):**
  - Record hand gestures in real time up to 10 seconds per loop—**independently or simultaneously** on both the Fader and the Knob of the same channel!
  - Always plays back in smooth **Ping-Pong** mode by default.
- **Super-Simple Controls:**
  - **Single Short Press:** Arms the channel (**Blinking Red**). Move the fader, the knob, or **both at the same time** to start recording (**Solid Red**). Press again to close and loop in Ping-Pong mode.
  - **Touch-to-Clear:** Simply move a looping fader or knob with your hand to instantly remove its loop and return to live control!
  - **Double Press:** Switches what the channel's **Time/Level CV input** modulates:
    - **Green LED:** CV modulates the **Fader** (default).
    - **Orange LED:** CV modulates the **Knob**.
    - *(Solid = idle/live; Pulsing = loop playing).*
  - **Long Press ($\ge 800\text{ ms}$):** Clears both loops on that channel immediately.
- **Real-Time CV Modulation (Time/Level Jack):**
  - Patch an external LFO, envelope, or CV source ($-5\text{V}$ to $+5\text{V}$ or $0\text{V}$ to $+8\text{V}$) into the Time/Level jack to modulate the selected control (Fader or Knob) in real time, updating MIDI CC, front-panel analog CV ($0\text{V}$ to $+8\text{V}$), and the slider LED.
- **24-Channel MIDI Output (Rear Jacks):**
  - Streams standard 7-bit MIDI Continuous Controller (CC) messages at standard 31,250 baud whenever any fader, knob, recorded loop, or incoming Time/Level CV/LFO changes.
  - Uses atomic 3-byte packet merging between Module 1 and Module 2 so simultaneous gestures and LFOs never corrupt each other.
  - Uses the rear 3.5mm **Link IN** jack configured as **TRS MIDI Type B** (+3.3V on Tip, TX on Ring, GND on Sleeve)—plug directly into compatible synths (e.g. Oxi Coral, ORN8, etc.) via a 3.5mm TRS-to-MIDI adapter/cable.
- **Dual-Module Chaining:**
  - Connect two modules with a single standard 3.5mm TRS stereo patch cable.
  - Module 1 (Master) maps to **CC 1–12**.
  - Module 2 (Slave) maps to **CC 13–24**.
  - Module 2 streams its CC data into Module 1 via hardware DMA. Module 1 seamlessly merges both streams and outputs all 24 CCs out of its **Link IN** port to your synth.
- **Parallel Analog Outputs ($0\text{V}$ to $+8\text{V}$):**
  - All 12 front jacks (6 per module) continuously output calibrated analog CV in parallel with MIDI.

---

## Hardware Compatibility: Behringer Steps vs. Original MI Stages

> [!IMPORTANT]
> **Behringer Steps vs. Original Mutable Instruments Stages Rear Jacks:**
> - **Behringer Steps:** Features **physical 3.5mm TRS jacks** on the rear PCB (`Link IN` and `Link OUT`). It is **100% plug-and-play** using standard 3.5mm audio patch cables and 3.5mm TRS-to-MIDI cables.
> - **Original MI Stages:** Uses **3-pin pin headers** (ribbon cable connectors) instead of 3.5mm jacks. To get MIDI output from an original Stages, you need a simple **3-pin header to 3.5mm TRS female jack (or 5-pin DIN) adapter cable**:
>   - **Pin 1:** Ground (Sleeve)
>   - **Pin 2:** USART TX Data (Ring)
>   - **Pin 3:** +3.3V Power (Tip)
>
> *(All front-panel movement looping and 0–8V analog CV features work identically on both hardware versions without any adapter).*

---

## Chaining & Cabling Setup

On both modules, the **Link IN** (left rear jack) serves as the primary MIDI output port:

```
+------------------------------------+          +------------------------------------+
|         MODULE 1 (MASTER)          |          |          MODULE 2 (SLAVE)          |
|                                    |          |                                    |
|   Link IN (Left)   Link OUT (Right)|          |   Link IN (Left)   Link OUT (Right)|
+---------+-----------------+--------+          +---------+-----------------+--------+
          |                 |                             |                 |
          |                 +=============================+               (Empty)
          |                   Standard 3.5mm TRS cable
          |                  (Module 2 TX -> Module 1 RX)
          v
   3.5mm TRS-to-MIDI cable
          v
   SYNTH MIDI IN
   (Receives all 24 CCs!)
```

1. **Module 1 (Master) Link IN $\longrightarrow$ Synth MIDI IN:**
   - Connect your 3.5mm TRS-to-MIDI cable from **Module 1 Link IN** (left rear jack) into your synth's MIDI input.
2. **Module 2 (Slave) Link IN $\longrightarrow$ Module 1 Link OUT:**
   - Connect a standard 3.5mm TRS stereo patch cable from **Module 2 Link IN** into **Module 1 Link OUT**.
   - Module 2's transmitter connects directly to Module 1's receiver line.

---

## MIDI CC Mapping (Channel 1)

| Module | Control | Target Parameter / CC |
| :--- | :--- | :--- |
| **Module 1 (Master)** | Faders 1 – 6 | **CC 1 – 6** |
| **Module 1 (Master)** | Knobs 1 – 6 | **CC 7 – 12** |
| **Module 2 (Slave)** | Faders 1 – 6 | **CC 13 – 18** |
| **Module 2 (Slave)** | Knobs 1 – 6 | **CC 19 – 24** |

*(CC numbers and MIDI channels can be customized in `stages/midi_config.h`)*.

---

## Flashing Instructions

Pre-compiled audio bootloader files are located in [`firmware_releases/`](firmware_releases/):

- [`stages_master.wav`](firmware_releases/stages_master.wav) (For Module 1 / Master / CC 1–12)
- [`stages_slave.wav`](firmware_releases/stages_slave.wav) (For Module 2 / Slave / CC 13–24)

### Steps to Flash:
1. Load the corresponding `.wav` file into your DAW (e.g. Ableton Live, Audacity, or a standalone audio player).
   - Ensure playback speed/pitch is unaltered (turn **Warp OFF** in Ableton).
   - Set output volume to unity gain (**0.0 dB**).
2. Connect your audio interface output to **Time/CV Jack 1** on the module.
3. Power on the Eurorack case while holding **Button 6** on the module.
   - The LEDs will cycle or indicate that the audio bootloader is waiting for signal.
4. Press **Play** on your audio player.
   - The module LEDs will illuminate progressively as data packets are verified.
   - When finished, all LEDs will flash green and the module will restart with the new firmware.

> [!TIP]
> **Flashing sweet spot (tested on Behringer Steps):**
> Most reliable flashing results were achieved using **Ableton Live** at **48,000 Hz** sample rate (Warp **OFF**), output gain at **+4.3 dB**, and the **fader on Steps set to roughly 75%**.

---

## Building from Source

To compile the firmware yourself using the GCC ARM embedded toolchain (`arm-none-eabi-gcc`):

### Build Master Firmware:
```bash
make -f stages/makefile clean
make -f stages/makefile BUILD_ROOT=build/master/ ROLE=1 -j4
arm-none-eabi-objcopy -O binary build/master/stages/stages.elf build/master/stages/stages.bin
python3 stm_audio_bootloader/qpsk/encoder.py -t stm32f3 -k -s 48000 -b 12000 -c 6000 -p 256 build/master/stages/stages.bin
```

### Build Slave Firmware:
```bash
make -f stages/makefile clean
make -f stages/makefile BUILD_ROOT=build/slave/ ROLE=2 -j4
arm-none-eabi-objcopy -O binary build/slave/stages/stages.elf build/slave/stages/stages.bin
python3 stm_audio_bootloader/qpsk/encoder.py -t stm32f3 -k -s 48000 -b 12000 -c 6000 -p 256 build/slave/stages/stages.bin
```

---

## License

Based on the original Mutable Instruments Stages hardware and firmware design by Émilie Gillet, released under the MIT License.
