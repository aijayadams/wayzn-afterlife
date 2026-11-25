# wayzn-afterlife: Governing Principles

Principles 1–4 cover the whole dog door system and are shared with the `wayzn`
integration's AGENTS.md. They outrank implementation convenience and apply
equally to any firmware or hardware built here.

1. **Not lying matters.** State honesty above all. If state cannot be proven
   fresh, report "unknown"/unavailable. A wrong "open" is worse than an honest
   "unknown".
2. **Open is preferable to fast oscillations.** When state is uncertain, bias
   toward open/known-safe. Never flip-flop to chase shaky signals.
3. **Oscillating is an error.** Rapid open/close cycling is a fault: detect it,
   log it, alert on it.
4. **Never lock a dog out.** The failsafe bias is always toward open. Assumed
   state must never suppress an open command.

## Hardware-work rules (this repo)

- **The production door stays working.** Do destructive or risky work
  (desoldering, flash erase, RDP changes, cutting traces) only on a spare
  unit or board. The in-service door gets passive probing only.
- **Read before write.** Never erase, unlock or change option bytes on an MCU
  until its flash has been dumped and the dump verified (two reads, same hash).
  On STM32, dropping read protection from level 1 to 0 **mass-erases** the chip.
- **Passive taps first.** On the STM+IMP UART, connect analyzer RX/ground only
  and do not drive TX until the protocol is understood.
- **Mind the grounds and voltage.** Logic is 3.3 V; the input is 12 V and the
  motor bridge switches 12 V. Share ground with the board; never probe J6/J7
  with a 3.3 V tool.
- **The motor is strong enough to hurt.** A firmware bug can drive the panel.
  Test new motor code with the belt/drive disengaged or the panel clear, and keep
  a hand on the 12 V supply.
- **Wayzn's firmware is never published.** Flash dumps and their decompiles go in
  `firmware-dumps/`, which is gitignored here. Keep your own copy somewhere
  private and backed up (losing it would be unrecoverable), but never commit it
  to this repo or any public fork or mirror.
- Every finding goes in `docs/` with a confidence tag: **verified** (measured
  or read), **marking** (read off the part), **inferred** (educated guess).

## Publishing hygiene (this repo is public)

Everything committed here is public for good, so check every file before it goes in:

- **No personal details:** no names beyond the GitHub handle, no email addresses, street addresses, Wi-Fi
  names, IP addresses, hostnames or local paths (`/Users/...`). Commits use the GitHub noreply address.
- **Photos and scans:** strip all metadata (GPS, camera, dates: re-save through an image library and check
  that no EXIF is left). Blur the unit's serial/barcode label and the imp module's label before committing.
  Don't commit phone originals (`*.HEIC`) or full-resolution scans; commit cleaned, downscaled copies.
- **Generated files:** regenerate images and fab outputs from the cleaned sources in this repo, never copy
  them from a private checkout (an overlay drawn on an unblurred photo leaks the label).
- **Wayzn's firmware:** describing behaviour and scales is fine; the dump, the decompile and the code are
  not (see above).
- **Secrets:** only in gitignored files (`secrets.ini`); never in code, logs or docs.

