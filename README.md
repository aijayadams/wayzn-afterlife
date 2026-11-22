# wayzn-afterlife

**Your Wayzn dog door doesn't have to die with the cloud, and it can come back better.**

Wayzn is shutting its service down in March 2027. The door on your slider is still a
good machine: a strong motor, a careful motor controller and a thermal sensor that sees
your dog. Only its "brain" was tied to the cloud. Swap that brain for a small ESP32
board and the door keeps working, entirely on your own network.

![Before: the app, Wayzn's cloud and the Electric Imp cloud stood between you and your door. After: an ESP32 on the door, controlled over your home network.](docs/before-after.svg)

## What you get

- **A door that keeps working.** No account, no cloud, no company that can switch it
  off.
- **Local control.** A small HTTP API on your network: open, close, live status (open,
  closed, opening, closing) and the door's settings, such as auto-open, how far it
  opens, how long it holds and how sensitive it is. A Home Assistant integration that
  uses it is available (pictured below).
- **Smarter dog detection.** The brain sees every raw reading from the door's thermal
  sensor, so it can do much better than a grid of cells and a small/medium/large
  setting. Fewer false opens, and a door that doesn't close on a dog standing beside it.
- **Safety first.** The original motor-control and current-limit (strength) firmware
  keeps running, untouched, so the door only ever pushes as hard as Wayzn designed it
  to. The door never closes on something it can see, and anything unexpected leaves it
  **open**: a dog is never locked out.
- **All open.** Firmware, wiring, measurements and the door's protocol are documented
  here, so you can fix and improve your own door.

<p align="center"><img src="docs/images/ha-controls.png" width="380" alt="The door's controls and settings as shown by the Home Assistant integration: auto open, open and close, auto close, hold open time, obstruction detection, open position in inches, presence and occupied thresholds"></p>

## How it sees your dog

![An 8x8 thermal view with a dog shows up as a few warm pixels. "Presence" opens the door; the more sensitive "occupied" level means the door never closes on anything in the doorway.](docs/presence.svg)

A tiny 8×8 thermal camera watches the doorway ten times a second. The brain keeps two
judgements apart: **presence** (a dog wants out, so open) and **occupied** (something is
in the doorway, so never close, and reverse if closing). Occupied is deliberately the
more sensitive one: a false alarm only keeps the door open a little longer. A new
detector that learns each pixel's own noise is already running alongside the current one
and catches dogs at the edge of the view that the original approach misses
([the numbers](docs/DETECTION-RESEARCH.md)).

## What it takes

If you're comfortable soldering a few wires to small pads, this is an evening's project.

1. **Open the door's case.** The shell is friction fit: no tools needed.
2. **Add an ESP32 and eight connections.** Seven wires and one jumper, all to test
   points and pads that Wayzn conveniently left on the board:

   ![Wiring the ESP32 to the Wayzn board](docs/wiring.svg)

3. **Flash the firmware.** It's in [firmware/brain](firmware/brain/) (PlatformIO);
   updates after that go over Wi-Fi. The door then answers on its
   [local API](docs/API.md). For Home Assistant, the `wayzn` integration's local mode
   ([aijayadams/wayzn](https://github.com/aijayadams/wayzn)) talks to it.

Never connect the ESP32's USB while its 3V3 wire is connected, and strain-relieve the
wires: they're small and run next to the motor.

## Go deeper

| | |
|---|---|
| [How it works](docs/HOW-IT-WORKS.md) | The board, its buses, what the motor controller does on its own, and how the brain is built |
| [Wiring](docs/HOW-IT-WORKS.md#wiring-the-esp32) | The connection table behind the diagram above |
| [API](docs/API.md) | The brain's local HTTP API and every setting |
| [Detection research](docs/DETECTION-RESEARCH.md) | Thermal sensor measurements and the detector comparison |
| [STM32 firmware](docs/STM-FIRMWARE.md) · [protocol](docs/STM-PROTOCOL.md) | Wayzn's motor controller: commands, states and status lines |
| [Hardware](docs/hardware/) | The main board, the STM32 pin map and the sensor board |
| [AGENTS.md](AGENTS.md) | The principles every change follows |

Wayzn's own STM32 firmware isn't published here; [tools/swd-dump](tools/swd-dump/) reads
your board's copy if you want one.

## Join in

Own a Wayzn door? [Open an issue](https://github.com/aijayadams/wayzn-afterlife/issues)
with questions, your board's revision, photos, or ideas. Every door that keeps running
is a win.
