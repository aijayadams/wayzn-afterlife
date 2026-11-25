"""wayzn-afterlife carrier v3 layout: placement, hand-defined signal routes, silkscreen.

All coordinates in mm in the WYP-BC-100 frame (design.py): origin = the STM+IMP header's 3V3 pin, +x toward the
J2/J5 end of the Wayzn board, +y toward the header's GND pin. gen_pcb.py applies this; nothing is autorouted.
Power: every 3V3/GND pad gets its own via to the In2 (3V3) / In1 (GND) plane (route.fanout), so only signals
are routed here (plus the LIS2DH12's supply). F = top layer (red in out/plan.png), B = bottom (blue).

Floorplan (53.8 x 33.4 mm, inside one lid cell):
  x -30..-21  programming: J5 1x6 UART header on the edge, RESET/BOOT buttons, bench-3V3 jumper; J6 (2x4 into
              the Wayzn J105) underneath in the top-left corner
  x -21..-1   U1 ESP32-C5. Left pins (EN, NRST, LED, H2 EN/BOOT) face the programming zone; right pins (STM
              UART, UART0, I2C, accelerometer INT, H2 UART, BOOT) face J1 and the H2. Above it: LED, J4 (NRST).
  x   0..24   J1 (underside, onto STM+IMP), J2 (to the Wayzn J2) on the top edge, J3 (Grid-EYE) on the end,
              accelerometer between them, U2 ESP32-H2 in the bottom-right corner (rotated 180: UART faces the C5).
"""

# ref -> (x, y, rotation deg, side "F"/"B")
PLACE = {
    "U1": (-11.04, 19.29, 0, "F"),
    "U2": (16.76, 26.36, 180, "F"),
    "U3": (8.0, 11.0, 0, "F"),
    "J1": (0.0, 0.0, 0, "B"),
    "J6": (-25.39, -0.53, 0, "B"),         # J105 anchor (2x4 2.0 mm), underside
    "J2": (11.3, 0.6, 180, "F"),
    "J3": (21.6, 4.6, 270, "F"),
    "J4": (-12.0, 0.6, 0, "F"),
    "J5": (-28.8, 8.6, 0, "F"),
    "JP1": (-21.8, 3.2, 90, "F"),
    "SW1": (-23.75, 11.4, 90, "F"),     # RESET
    "SW2": (-23.75, 21.2, 90, "F"),     # BOOT
    "D1": (-16.8, -1.2, 180, "F"), "R6": (-16.0, 0.6, 270, "F"),
    "R1": (-18.6, 5.4, 270, "F"), "C1": (-17.2, 5.4, 90, "F"),
    "C3": (-6.5, 6.4, 0, "F"), "C4": (-3.6, 4.4, 0, "F"), "C9": (-2.4, 0.0, 90, "F"),
    "R9": (-14.6, 5.4, 0, "F"),
    "R7": (2.1, 2.5, 270, "F"), "R8": (1.6, 6.6, 90, "F"),
    "R2": (0.6, 29.0, 90, "F"),
    "R3": (21.8, 15.4, 180, "F"), "C2": (21.8, 16.8, 0, "F"),
    "R4": (12.4, 16.4, 270, "F"), "R5": (13.8, 16.4, 270, "F"),
    "C5": (17.8, 15.4, 0, "F"), "C6": (17.8, 16.9, 0, "F"),
    "C7": (10.4, 10.5, 0, "F"), "C8": (10.4, 11.9, 0, "F"), "C10": (10.4, 13.4, 0, "F"),
}

# Signal routes: net -> list of ("F"|"B", [points][, width mm]) tracks and ("V", [point]) vias. A point is (x, y)
# or "REF.PAD".
# Vias for left-edge C5 signals sit just inside the module (x -17.4), right-edge ones at x -3.8: both under the
# module body, clear of its pads, so the bottom layer carries them across.
VL, VN, VR = -17.4, -16.4, -3.8   # via columns under the C5, beside its edge pins
ROUTES = {
    # --- programming zone (left)
    "C5_EN": [("F", ["U1.3", (-25.62, 12.92)]),                                       # to SW1 RESET
              ("F", [(-25.62, 8.40), (-25.62, 14.40)]),                               # both SW1 pin-1 pads
              ("F", ["J5.3", (-25.62, 13.68)]),
              ("F", ["C1.1", "R1.2", (-20.6, 7.91), (-20.6, 12.92)])],                # 10k/1u delay
    "HDR_3V3": [("F", ["JP1.1", (-21.8, 4.6), (-23.9, 6.7), (-27.4, 6.7), (-28.8, 8.1), "J5.1"])],
    "LED_STATUS": [("F", ["U1.5", (VL, 15.46)]), ("V", [(VL, 15.46)]),
                   ("B", [(VL, 15.46), (VL, 0.3)]), ("V", [(VL, 0.3)]),
                   ("F", [(VL, 0.3), "D1.2"])],
    "LED_K": [("F", ["D1.1", "R6.1"])],
    "C5_TXD0": [("F", ["U1.25", (VR, 14.19)]), ("V", [(VR, 14.19)]),
                ("B", [(VR, 14.19), (-5.83, 16.22), "J5.4"])],
    "C5_RXD0": [("F", ["U1.24", (VR, 15.46)]), ("V", [(VR, 15.46)]),
                ("B", [(VR, 15.46), (-6.0, 17.66), (-26.4, 17.66), (-27.5, 18.76), "J5.5"])],
    "C5_BOOT": [("F", ["U1.15", (-0.4, 26.89), (0.6, 27.89), "R2.2"]),
                ("F", ["U1.15", (VR, 26.89)]), ("V", [(VR, 26.89)]),
                ("B", [(VR, 26.89), (-5.4, 28.49), (-24.22, 28.49), (-25.62, 27.09), (-25.62, 25.6)]),
                ("V", [(-25.62, 25.6)]),
                ("F", [(-25.62, 25.6), (-25.62, 18.2)]),                              # both SW2 BOOT pin-1 pads
                ("F", ["J5.6", (-25.62, 21.3)])],
    # --- STM32 link (J1, J4)
    "STM_NRST": [("F", ["U1.4", (VN, 14.19)]), ("V", [(VN, 14.19)]),
                 ("B", [(VN, 14.19), (VN, 7.4), (-15.6, 6.6)]), ("V", [(-15.6, 6.6)]),
                 ("F", [(-15.6, 6.6), "R9.1"])],
    "STM_NRST_W": [("F", ["R9.2", (-12.0, 3.31), "J4.1"])],
    "STM_RX": [("F", ["U1.27", (-0.9, 11.65), (1.6, 9.15), "R8.1"])],
    "STM_RX_W": [("F", ["R8.2", "J1.3"])],
    "STM_TX": [("F", ["U1.26", (0.0, 12.92), (2.6, 10.32), (2.6, 3.5), "R7.2"])],
    "STM_TX_W": [("F", ["R7.1", "J1.2"])],
    # --- I2C: SDA on top from the C5 to J2 with a tap to the accelerometer and a bottom run to J3; SCL from J2
    # on top to the accelerometer and J3, reaching the C5 on the bottom
    "I2C_SDA": [("F", ["U1.23", (3.5, 16.73), (5.9, 14.33), (5.9, 2.6), (7.3, 1.2), "J2.3"]),
                ("F", [(5.9, 11.1), (6.55, 11.75), "U3.4"]),
                ("V", [(5.9, 8.2)]),
                ("B", [(5.9, 8.2), (21.2, 8.2), "J3.3"])],
    "I2C_SCL": [("F", ["J2.2", (9.3, 2.0), (6.6, 4.7), (6.6, 9.61), "U3.1"]),
                ("F", [(9.3, 2.0), (13.9, 6.6), "J3.2"]),
                ("V", [(6.6, 9.0)]),
                ("B", [(6.6, 9.0), (3.6, 12.0), (3.6, 15.27), (-0.4, 19.27)]), ("V", [(-0.4, 19.27)]),
                ("F", ["U1.21", (-0.4, 19.27)])],
    "ACC_INT1": [("F", ["U1.18", (-0.4, 23.08)]), ("V", [(-0.4, 23.08)]),
                 ("B", [(-0.4, 23.08), (5.4, 17.28), (5.4, 12.0), (7.75, 9.65), (7.75, 9.2)]), ("V", [(7.75, 9.2)]),
                 ("F", [(7.75, 9.2), "U3.12"])],
    # accelerometer supply (0.5 mm pitch: no room for a via per pad): CS and the VDD pins to the caps beside it;
    # the four GND pads to one via below the chip.
    "+3V3": [("F", ["U3.2", "U3.9"], 0.15),                                         # CS high (I2C mode)
             ("F", ["U3.10", "U3.9"], 0.15),
             ("F", ["U3.10", "C7.1"], 0.15),
             ("F", ["C7.1", "C8.1", "C10.1"])],
    "GND": [("F", ["U3.8", "U3.7", "U3.6", "U3.5"], 0.15), ("F", ["U3.6", (8.25, 12.75)], 0.15),
            ("V", [(8.25, 12.75)])],
    # --- H2 link
    "H2_RXD": [("F", ["U1.17", (5.0, 24.35), (5.69, 23.66), "U2.30"])],
    "H2_TXD": [("F", ["U1.16", (6.0, 25.62), (7.16, 24.46), "U2.31"])],
    "H2_BOOT": [("F", ["U1.11", (VL, 23.08)]), ("V", [(VL, 23.08)]),
                ("B", [(VL, 23.08), (-15.48, 25.0), (8.4, 25.0), (8.4, 17.9)]), ("V", [(8.4, 17.9)]),
                ("F", [(8.4, 17.9), (12.76, 17.9), "U2.23"]),
                ("F", [(12.4, 17.9), "R4.2"])],
    "H2_GPIO8": [("F", ["U2.22", (13.56, 17.4), "R5.2"])],
    "H2_EN": [("F", ["U1.12", (VL, 24.35)]), ("V", [(VL, 24.35)]),
              ("B", [(VL, 24.35), (-15.85, 25.9), (20.2, 25.9), (21.0, 25.1), (21.0, 17.6)]),
              ("V", [(21.0, 22.06)]), ("F", [(21.0, 22.06), "U2.8"]),
              ("V", [(21.0, 17.6)]), ("F", [(21.0, 17.6), "C2.1", "R3.2"])],
}

# Silkscreen labels: (text, x, y, size, side "F"/"B", justify "L"/"C"/"R", rotation). gen_pcb checks every label:
# inside the board (0.3 mm), clear of pads, vias and each other; stitching vias keep out of them. Footprint silk
# that would leave the board is dropped; part values aren't drawn; references stay on the assembly layer.
SILK = [
    # programming header J5: pin names in the gap between it and the buttons (vertical, one per pin)
    ("3V3*", -27.0, 8.6, 0.6, "F", "C", 90),
    ("GND", -27.0, 11.14, 0.6, "F", "C", 90),
    ("EN", -27.0, 13.68, 0.6, "F", "C", 90),
    ("TXO", -27.0, 16.22, 0.6, "F", "C", 90),
    ("RXI", -27.0, 18.76, 0.6, "F", "C", 90),
    ("BOOT", -27.0, 21.3, 0.6, "F", "C", 90),
    ("*JP1", -20.8, 3.2, 0.7, "F", "L", 0),
    ("RESET", -23.6, 7.1, 0.7, "F", "C", 0),
    ("BOOT", -23.4, 25.9, 0.7, "F", "C", 0),
    # connectors
    ("NRST", -10.0, 4.6, 0.7, "F", "C", 0),
    ("TO WAYZN J2", 8.3, 3.7, 0.7, "F", "C", 0),
    ("GRID-EYE", 17.5, 7.6, 0.7, "F", "C", 90),
    # back, under the C5's centre (bottom traces only, no vias)
    ("wayzn-afterlife v3", -10.5, 19.2, 0.8, "B", "C", 0),
    ("PROG: hold BOOT,", -10.5, 20.8, 0.8, "B", "C", 0),
    ("tap RESET, flash", -10.5, 22.4, 0.8, "B", "C", 0),
    ("J5.1 3V3 = JP1", -10.5, 24.0, 0.8, "B", "C", 0),
]
