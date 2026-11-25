"""Labels for captures/2026-11-21_P1_brain-door.log (log clock; operator narration, +-2 s).

Kinds:
  pos  : someone at the door. `need` says what a correct detector must do:
         "present" = should auto-open (a dog/person approaching), "occupied" = must block
         a close (safety), "either" = any detection is fine.
  neg  : doorway believed empty (false-positive test).
  negd : door cycling 17:33-17:38 under operator command, no label; nothing warm was seen in
         the frames (scene only cools). Treated as "probably empty".
Everything else is unlabelled: detections there are counted, not scored.
"""

POS = [
    # name, start, end, need, note
    ("approach 16:43", "16:43:52", "16:44:02", "present", "two approaches, person and/or dog"),
    ("person 17:24", "17:24:52", "17:25:03", "present", "operator walking up, strong"),
    ("dog weak 17:25", "17:25:17", "17:25:42", "present", "dog at left edge, weak"),
    ("dog 17:31", "17:31:59.5", "17:32:03", "present", "dog ~5 s, strong"),
    ("person 17:38", "17:38:41", "17:38:50", "either", "obstruction test: user stepped into view (from LOG)"),
    ("dog auto 17:41", "17:41:45", "17:41:51", "present", "first auto run"),
    ("dogs opening 17:42", "17:42:04", "17:42:12", "occupied", "dogs near the door while opening/open"),
    ("INCIDENT 17:42", "17:42:39", "17:42:51", "occupied", "door closed on dogs; must be occupied"),
    ("dog return 17:43", "17:43:53", "17:44:29", "present", "dog wanting back in"),
    ("dog 18:03", "18:03:05", "18:03:11", "present", "dog trip, auto open"),
    ("dog weak 18:03", "18:03:20", "18:03:40", "occupied", "weaker second visit (door open)"),
]

# The whole incident window (dogs by the door during the close): occupied coverage reported.
INCIDENT_WIDE = ("17:42:30", "17:43:15")

NEG = [
    ("empty 16:47-17:10", "16:47:10", "17:10:00"),
    ("clear 17:24", "17:24:05", "17:24:51"),
    ("empty 17:26-17:27", "17:26:00", "17:27:37"),
]
NEGD = [
    ("door cycling 17:33-17:38", "17:33:00", "17:38:10"),
]
