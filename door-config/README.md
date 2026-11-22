# Door calibration

The calibration the imp learned for one door, as needed to bring the STM32 up without the
imp. Every door learns its own profile: read yours with `get_profile` (or relearn it with
`auto_zero`) and use these files as the format.

- `profile-2026-11-20.json`: the 2×144 motor-current profile (open, close) read from the STM
  with `get_profile`. `open[0]` is set to 130 by hand (that value was garbled in the read).
  Pushing it with `set_profile` and reading it back with `get_profile` round-trips (verified).
- `bringup-2026-11-20.txt`: the STM shell lines to send, in order, **one every ~0.4 s**
  (a burst overflows the ESP32's USB input buffer). Then tell the STM where the arm is:
  `zero -72 14` if closed and engaged, `zero -6136 15` if at the open position. The STM only
  knows the position it's given. If unsure, use `auto_zero` (moves the full travel, relearns
  the profile). See [docs/STM-FIRMWARE.md](../docs/STM-FIRMWARE.md).
- The imp's settings for this door: `set_open_to -6136` (8 in), `set_dooroffset 13588`,
  `set_open_wait -1`, `set_button_op 1`. This file uses `set_open_wait 15` (unused: the brain
  closes the door itself) and `set_button_op 1` (the physical button opens from closed).
- Record only: the brain no longer sends this file. It keeps the profile the STM learns in a
  calibration (read back with `get_profile`) and takes the travel from the STM or from the
  `expected_travel` setting; see [firmware/brain/README.md](../firmware/brain/README.md).
