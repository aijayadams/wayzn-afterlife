# Grid-EYE doorway detection

The firmware's live detector is `firmware/brain/src/presence.h` ("v1"). A second detector,
`firmware/brain/src/presence2.h` ("v2"), runs next to it in **shadow mode**: it sees every
frame and logs its transitions and disagreements, but decides nothing. This page describes
the measured sensor behaviour, how the two detectors compare, and the recommended path for v2.
Reference implementations and the replay tool are in `firmware/brain/research/`.

Tags: **verified** = measured on the dataset below; **inferred** = interpretation.

## Dataset

One in-door Grid-EYE recording at 10 Hz, made with `tools/uart-sniffer/capture.py` from the
brain's console: 53,575 frames (89 min, 5 runs separated by reboots), no dropped frames
according to the device ms counter, 13 door moves. It contains person and dog approaches,
weak dog signals at the edge of the view, dogs beside the door while it opens and closes,
door cycling with nobody present, and 25 min of empty doorway. Events are labelled by hand
to ±2 s: 11 labelled events (7 "should open" and 11 "must be occupied"). Indoor ambient was
about 17 C throughout.

## Summary

- **v1 misses weak single-pixel signals by design (verified).** `presence.h` compares each
  *raw* frame against a 0.7 C floor (2.7 σ), then sums and smooths. A dog beside the panel
  that warms a single pixel (r4c2) by 1.1-1.6 C adds only about 0.5 per frame, so v1's
  "occupied" level (0.6) is reached late and only in patches. Smoothing **each pixel first**
  (EMA, a = 0.2) lowers the noise from 0.26 C to 0.09 C; over 22 min of empty doorway no
  smoothed pixel rose above 0.45 C.
- **v2**: smooth each pixel and normalise it by its own noise, then use two levels.
  *Occupied* fires on any pixel ≥ 0.6 C or a spread-out score ≥ 0.4. *Present* fires on a
  summed score ≥ 1.0 held for 0.2 s. A cooling clamp handles the open door, and a recovery
  step handles the warm-back after it closes. Each pixel stops learning only while it is warm.
  About 2 KB of RAM and about 2k flops per frame.
- **Results on the labels:** no misses; median presence latency +0.4 s from the labelled
  start (v1: +0.6 s). For dogs beside the door during a close, occupied coverage is **1.00**
  (v1: 0.66 over the labelled 12 s, 0.18 over the surrounding 45 s). False positives in
  25 min of empty doorway: 0 (v1: 0). In 24 h of synthetic noise: 0 (v1: 0).
- **Margin:** with real dog signals scaled down and added to empty frames, v2 still marks the
  dogs-beside-the-door case *occupied* at **0.5×** amplitude (15/15). v1 catches none at 0.5×
  and 5/15 at 0.75×.
- **Cost:** *occupied* is held longer. After a visit it stays on for 25-95 s (v1: 1-6 s), and
  it fires in 14 unlabelled stretches, 443 s in total. Most of these are a step-like
  +0.5-0.9 C at the left edge (r4c0) seconds to minutes before a dog visit, most likely a dog
  waiting just beside the door (**inferred**). That holds the door open longer, which is the
  safe direction. Per-cell weights can trade it away.

## 1. What the sensor sees (verified unless tagged)

| Quantity | Value |
|---|---|
| Scene (empty, door closed) | 16.4-17.9 C. Rows 6-7 are ~0.6 C warmer |
| Pixel noise (white; frame-difference σ/√2) | 0.23 C in the centre, 0.29-0.38 C at the edges and corners (r7c7 is worst). Mean 0.26 C ≈ 1 LSB |
| Common-mode noise (frame mean) | 0.08 C |
| Drift | ~0.3 C over 30 min, uniform across the scene |
| After per-pixel EMA a = 0.2 | σ 0.087 C. Empty-doorway max 0.45 C (all 64 px, 22 min) |
| Person at the door | 2.0-2.9 C over ~10 px (rows 2-7, cols 0-3). Summed excess 9-17 |
| Dog, close | 2.5-3.1 C peak (r4c2) over 3-6 px. Summed excess 4-7 |
| Dog, weak, at the edge of the view | 1.2-1.9 C on 1-2 px (r4c0/r4c1) |
| Dogs beside the door during a close | 1.1-1.6 C on **one** px (r4c2), sometimes r4c3 +0.6 |
| Door opening | Cools the doorway column by up to **1.0 C**: col 3 rows 3-6 −0.9 to −1.0 C, rest −0.2 to −0.3 C (45 s open). It never warms anything |
| After a long open | The scene warms back over 1-2 min (col 3 +0.5 C, rest +0.2 C after a 110 s open) |
| Door motion alone (6 cycles) | No warming. Summed excess stays ≤ 0.7 above the 0.7 C floor |

Where things appear: everything lands in **rows 3-6, cols 0-3**. Of the 18 v2 presence
onsets, 13 start at col 0-1 and the warm centroid moves to col 2-3 within 1-2 s, so arrivals
come from the left. 4 don't move (something already at the edge or in the doorway), and 1
moves left (leaving). Rows 0-1 and cols 5-7 never carried a real signal. The dogs-beside-the-
door case is measured against the closed-door background (r4c2 +0.9 C mean); there the dog's
+1.2 C partly cancels the open-door cooling.

## 2. v1 (`presence.h`) on the labels

The Python port `detectors.Baseline` and the C++ header agree on 100% of frames.

| Event | Presence latency (s from label start) | Occupied coverage |
|---|---|---|
| person and dog approach | +0.9 | 0.94 |
| person | +0.4 | 0.99 |
| weak dog, edge of view | +1.3 | 1.00 |
| dog | +0.4 | 1.00 |
| dog (auto-open) | +0.7 | 0.92 |
| dogs while the door opens | +1.6 | 0.93 |
| **dogs beside the door during a close** | **miss** | **0.66** (first at +4.1 s); 0.18 over the surrounding 45 s |
| dog returning | +0.3 | 0.92 |
| dog / weaker second visit | +0.6 / +1.6 | 0.95 / 0.97 |

False positives: 0 presence and 0 occupied episodes in 25 min of empty doorway, and 0 in
5 min of door cycling. 0 events in 0.5 h only bounds the rate below about 6/h at 95%.

## 3. Alternatives compared

All variants detect all 7 "present" events and all 11 "occupied" events.

| Detector | Median presence latency | Dogs during close, occ coverage (12 s / 45 s) | Empty FP P/O | Unlabelled occ eps (time) | Noise-only 24 h O eps |
|---|---|---|---|---|---|
| B0 `presence.h` | +0.6 s | 0.66 / 0.18 | 0/0 | 6 (82 s) | 0 |
| B1 B0 with occ_on 0.4 | +0.6 | 1.00 / 0.31 | 0/0 | 10 (142 s) | - |
| B2 B0 with frame-median (common-mode) removal | +0.6 | 1.00 / 0.31 | 0/0 | 6 (78 s) | - |
| B3 B0 with doorway mask (rows 2-7, cols 0-4) | +0.6 | 0.63 / 0.17 | 0/0 | 6 (78 s) | - |
| **V2** | **+0.4** | **1.00 / 1.00** | 0/0 | 14 (443 s) | **0** |
| V2 with a fixed floor (no noise normalisation) | +0.4 | 1.00 / 1.00 | 0/1 | 14 (578 s) | **13** |
| V2 without cooling clamp | +0.4 | 1.00 / **0.48** | 0/0 | 13 (434 s) | - |
| V2 without post-close recovery | +0.4 | 1.00 / 1.00 | 0/0 | 15 (661 s) | - |
| V2 freezing all learning while door open | +0.4 | 1.00 / 1.00 | 0/0 | 15 (446 s) | - |
| V2 global (not per-pixel) learning freeze | +0.4 | 1.00 / 1.00 | 0/0 | 14 (607 s) | - |
| V2 with blob ≥ 2 px for presence | +0.4 | same as V2 | 0/0 | same | - |
| V2 with presence weights = doorway region | +0.4 | 1.00 / 1.00 | 0/0 | 14 (443 s) | - |
| V2 with occupied weight 0.5 on col 0 | +0.4 | 1.00 / 1.00 | 0/0 | **11 (260 s)** | - |

What each piece contributes (verified on this data):
- **Smoothing each pixel before the threshold** is the main gain. B1 shows that lowering the
  v1 threshold alone doesn't cover the dogs-during-close case (0.31 of the 45 s window).
- **Noise normalisation** matters for the corner pixels. With a fixed 0.6 C threshold, the
  noisier corners crossed it 13 times in 24 h of synthetic noise; normalised, 0 times.
- **Cooling clamp** (bg ≤ smoothed scene + 0.3 C): without it, dogs next to the opening door
  are half-hidden by the cold air (0.48 against 1.00).
- **Post-close recovery** (bg returns toward the pre-open background, never above it, for
  3 min): cuts the occupied time caused by warm-back after a 110 s open from 179 s to 14 s.
- **Per-pixel freeze** shortens the holds (607 s → 443 s) without costing detection.
- **Blob size, common-mode removal, doorway mask:** no measurable effect on this data. They
  are options, not defaults.
- **Approach direction / onset features:** every moving arrival entered from the left. 18
  onsets are too few to train or validate an "intends to go out" gate. **Not recommended.**
- **Learned model:** not supported by 11 labelled events (±2 s) at one temperature. See
  section 6.

### Margin (signal injection)

The rise (frames minus the pre-event median) of four real events is scaled and added to the
empty doorway every 90 s, 15 trials each. Detection rates:

| Template | Scale | v1 present / occ | v2 present / occ |
|---|---|---|---|
| dogs beside the door during a close | 0.50 | 0.00 / 0.00 | 0.00 / **1.00** |
| dogs beside the door during a close | 0.75 | 0.00 / 0.33 | 0.27 / **1.00** |
| dogs beside the door during a close | 1.00 | 0.87 / 1.00 | 1.00 / 1.00 |
| weak dog, edge of view | 0.50 | 0.00 / 0.07 | 0.00 / **1.00** |
| weak dog, edge of view | 0.75 | 1.00 / 1.00 | 1.00 / 1.00 |
| dog, close | 0.25 | 0.00 / 0.00 | 0.00 / **1.00** |
| dog, auto-open | 0.25 | 0.00 / 0.33 | 0.40 / **1.00** |

No run produced a false occupied episode between injections. Presence stays about as strict
as v1 on purpose: auto-open needs a clear signal. Occupied has about 2× margin on the weakest
real dog signal in the dataset.

### Sensitivity

With occ_peak anywhere from 0.5 to 1.0, the dogs-during-close case is fully covered and the
24 h noise test gives 0 FPs. occ_peak 0.8 cuts unlabelled occupied time from 443 s to 280 s;
0.5× injection of that case is then caught 0.93 of the time. Presence on_score 0.7-1.5
changes nothing measurable. At 2.0 the 0.75× weak dog is caught only 13% of the time and
median latency grows to +0.7 s.

## 4. Recommendation (staged)

1. **Shadow mode** (current firmware). `ge2::Detector` runs next to `ge::Detector` and logs
   its transitions (`# v2 occupied ON …`, `# v2 presence ON …`) and `v2 WOULD BLOCK` /
   `v2 WOULD REVERSE` warnings when it disagrees with v1 about a close. It acts on nothing.
   Run it over real dog traffic and long opens.
2. **Safety level: occupied = v1.occupied OR v2.occupied.** This can only block more closes,
   never fewer. It covers the dogs-beside-the-door case with ~2× amplitude margin. Cost:
   closes wait while something warm sits at the edge of the view (holds of 25-95 s after
   visits, instead of 1-6 s).
3. **Presence from v2** (latency −0.2 s, same strictness), once the shadow logs show no extra
   false opens.
4. **Expose the settings in section 5**, and collect labels for a learned model (section 6).

Risks and open points:
- **Long holds and possible stuck occupied** (**inferred** cause: dogs waiting at the left
  edge). Warm pixels learn with an 8 min time constant, so a heater or sun patch holds the
  door open for several minutes before it is absorbed. That is the fail-open direction
  (principles 2 and 4), but it must be logged and visible ("occupied by r4c0 for 6 min").
- **Door state is an input** to the clamp and recovery. If it is unknown, pass "not closed":
  the clamp stays on and recovery is skipped, which errs toward occupied. A wrong door state
  cannot make v2 miss a warm object.
- **Warm-back during a close.** During a 45 s close with dogs present, col 5 rows 5-7 rose
  0.6-0.75 C (warm-back or the dogs). After a very long open, warm-back during the close
  could reverse it. That is an extra open, not an oscillation: the firmware's 4 s motion
  dedupe applies and the next close comes after the hold.
- **One ambient temperature** (~17 C indoors) and one dog population. When the floor is close
  to dog temperature every margin shrinks. The noise normalisation and thresholds are
  relative; the dog's contrast is not.

## 5. User settings (replacing small/medium/large)

Wayzn's setting was a count of warm cells (1/2/4) on a 4×5 grid. v2 supports instead:

- **Auto-open sensitivity 1-5** (`applySensitivity`). It scales only the presence
  thresholds (on_score ×2.0/1.4/1.0/0.8/0.65). Level 3 is the tested default.
- **Zones on the 8×8 grid** (64 values 0-1, plus a heat-map view of the live smoothed rise so
  the user can see where the dog shows up):
  - *Presence weights:* e.g. 0 for a hallway that people walk past. This can't make the
    detector less safe.
  - *Occupied weights:* down to 0.5, or 0 only with an explicit "ignore this cell"
    confirmation, a log line and the cell shown in the status. Use case: a heater or a sun
    patch.
- **Occupied sensitivity:** not a free slider. occ_peak/occ_score can be raised to at most
  1.0/0.8 (hard caps `kOccPeakMax`/`kOccScoreMax` in the header), so a setting can't blind the
  safety level.
- **Diagnostics:** the cell holding "occupied", for how long, its rise in C, and the
  per-cell noise: the answer to "why is the door still open" (principle 1).

## 6. What a learned model would need

- **Labels:** per-frame dog/person/empty and position (in doorway vs nearby), e.g. from a
  camera classifier paired with the Grid-EYE frames. Aim for ≥ 200 dog visits, ≥ 50 "waiting
  beside the door" and ≥ 50 people, across day/night and at least two ambient temperatures.
- **Logging:** the raw `# F` frames plus door state at 10 Hz: ~250 B/frame as text, ~200 MB/day.
  Store them binary, or only for ±30 s around each v2 occupied episode.
- **Model size:** logistic regression or a tiny MLP on a few features (the 64 smoothed,
  normalised rises, door state, centroid velocity) fits in < 1 KB and runs at 10 Hz. It must
  only *add* sensitivity to occupied (OR with the rule), never remove it.

## Files

- `firmware/brain/src/presence2.h`: the v2 detector (`ge2::Detector`, header-only,
  host-buildable), run in shadow mode by `src/main.cpp`.
- `firmware/brain/research/replay2.cpp`: unit tests (`--test`: noise, single warm pixel,
  blob, door-open dog, warm-back, cooling, weights/caps, still dog) and a capture replay to
  CSV. Build: `c++ -std=c++17 -O2 -I../src replay2.cpp -o /tmp/replay2`; replay:
  `/tmp/replay2 <capture.log> [out.csv]`. The capture must contain Grid-EYE frames
  (`# F` lines) and door transitions (`# door:` lines), i.e. the brain's console recorded with
  `tools/uart-sniffer/capture.py --port socket://wayzn-esp.local:23`.
- `firmware/brain/research/gelog.py`: capture parser and time base. Frame times come from the
  device ms counter, because the log clock has Wi-Fi batching of up to 2.9 s.
- `firmware/brain/research/detectors.py`: Python reference implementations (v1 port, v2,
  ablations). The C++ replay of `presence2.h` and the Python reference agree on 100% of
  present/occupied frames (max score difference 0.02, from float32 rounding).
