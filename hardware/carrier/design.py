"""wayzn-afterlife carrier board: the single source of truth for parts, nets and mechanics.

gen_sch.py turns this into carrier.kicad_sch (a label-connected schematic), gen_pcb.py into
carrier.kicad_pcb (footprints, nets, placement, outline). See README.md.

Coordinates (mm) are in the WYP-BC-100's own top view, so the carrier lines up with it when stacked:
origin = the STM+IMP header's 3V3 pin (square pad), +x toward the J2/J5 end of the board, +y toward
the header's GND pin. Measured 2026-11-24 from photos/2026-11-20_bench_stm-imp-connectors.jpeg and
2026-11-20_main_back.jpeg (scale from known parts: the 2.54 mm header, the 7x7 mm STM32 body, the 2.0 mm J105 and
JST PH pitches; corrected for the camera angle), cross-checked with the owner's caliper readings (board 34 mm
wide, end holes 28 mm apart, 4 mm). Still check the 1:1 print (out/fit-check-1to1.pdf) before ordering.
"""

# ---------------------------------------------------------------- mechanics (WYP-BC-100 frame)
WAYZN = {
    "header": [(0.0, 0.0), (0.0, 2.54), (0.0, 5.08), (0.0, 7.62)],  # STM+IMP: 3V3, TX, RX, GND. Small holes:
                                                                    # standard square pins don't fit (round pins do)
    # 2026-11-24: from the owner's 1200 dpi scans (docs/hardware/WYP-BC-100-MECHANICAL.md), replacing the photo
    # estimates. The posts moved 1.9 mm toward the header: carrier v3's outline (to x 23.8) now hits them.
    "board_y": (-2.83, 31.11),                     # long edges (33.94 mm); the top edge touches the shell
    "board_end_x": 27.05,                          # the J2/J5 end edge
    "posts": [(23.9, 0.25), (23.9, 28.17)],        # shell posts through the end holes (4 mm, 27.95 mm apart)
    "ribs_x": (-33.1, 23.9),                       # lid ribs either side of the carrier's cell: the one at the
                                                   # posts (owner), the other ~57 mm away (owner; inferred)
    "tp1": (-2.15, 11.79), "tp2": (-2.15, 13.95),  # I2C SCL / SDA test points
    "sw300_nrst": (-20.0, 13.3),                   # SW300 (EXP RST); NRST = its top-right pad
    "j2": {"x": (13.0, 25.1), "y": (4.9, 10.3)},   # IR SENSOR socket (JST PH, SMD; front scan, +-1 mm; 6.6 mm tall)
    "j5": {"x": (13.0, 25.1), "y": (14.9, 20.3)},  # encoder socket (JST PH, SMD; front scan, +-1 mm)
    "j105": {"pins_x": (-27.37, -25.39), "pins_y": (-0.53, 1.49, 3.47, 5.48)},  # 2x4 socket, 2.0 mm pitch:
                                                   # the carrier's second anchor (mechanical only)
}
STACK_MM = 11.0  # board-to-board gap: set by J105 + the 2.0 mm header's plastic, and matched by the J1 stack

# Carrier outline (same frame): inside the lid cell between the ribs at -31 and +25.8 (1 mm clear of each), and
# inside the Wayzn board's top edge (which already touches the shell). 53.8 x 33.4 mm.
OUTLINE = {"x": (-30.0, 23.8), "y": (-2.6, 30.8), "corner_r": 1.0}

G = "GND"
V = "+3V3"

# ---------------------------------------------------------------- parts
# ref: (symbol, footprint, value, LCSC, notes, {pin: net or None = no connect})
# "Espressif:" symbols and footprints are vendored in lib/ (espressif/kicad-libraries, see lib/ESPRESSIF-SOURCE.txt);
# "Carrier:" symbols are in lib/Carrier.kicad_sym; everything else is a KiCad 10 standard library.
PARTS = {
    "U1": ("Espressif:ESP32-C5-WROOM-1U", "Espressif:ESP32-C5-WROOM-1U", "ESP32-C5-WROOM-1U-N8R8", "C51950748",
           "main MCU: dual-band Wi-Fi 6, BLE 5, 802.15.4; 8 MB flash, 8 MB PSRAM. U.FL (MHF I) antenna socket "
           "on the module: use a 2.4/5 GHz dual-band antenna",
           # Pins chosen so no two routes cross. Left edge (pins 1..14, top to bottom) faces the programming
           # zone; right edge (pins 28..15, top to bottom) faces J1 (STM), the I2C parts and the H2.
           {"1": G, "2": V, "3": "C5_EN",
            "4": "STM_NRST",                       # GPIO2: open-drain, pulls the STM32 reset (100 R series, J4)
            "5": "LED_STATUS",                     # GPIO3 (MTDI strap: SDIO timing only, unused here)
            "6": None, "7": None, "8": None,       # GPIO0, GPIO1, GPIO6: unused
            "9": None, "10": None,                 # GPIO7 (JTAG-select strap), GPIO8: unused
            "11": "H2_BOOT",                       # GPIO9: open-drain, holds H2 GPIO9 low (download mode)
            "12": "H2_EN",                         # GPIO10: open-drain, resets the H2
            "13": None, "14": None,                # GPIO13/14 (native USB D-/D+): unused, kept free
            "15": "C5_BOOT",                       # GPIO28: boot strap (10k pull-up, BOOT button, header)
            "16": "H2_TXD",                        # GPIO5 <- H2 U0TXD (RCP UART, C5 RX)
            "17": "H2_RXD",                        # GPIO4 -> H2 U0RXD (RCP UART, C5 TX)
            "18": "ACC_INT1",                      # GPIO27 (strap): LIS2DH12 INT1 idles low, which only selects
                                                   # download variant 1 instead of 0; UART download works in both
            "19": None,                            # SPICS1 of the in-package PSRAM: must stay unconnected
            "20": None, "22": None,
            "21": "I2C_SCL",                       # GPIO23 (SCL below SDA here, so the two reach J2 uncrossed)
            "23": "I2C_SDA",                       # GPIO24
            "24": "C5_RXD0", "25": "C5_TXD0",      # GPIO12/11: UART0, to the header
            "26": "STM_TX",                        # GPIO25 <- STM32 PA9 (USART1 TX); UART1 RX. Strap: SDIO edge only
            "27": "STM_RX",                        # GPIO26 -> STM32 PA10 (USART1 RX); UART1 TX. Strap: download only
            "28": G, "29": G, "30": G, "31": None, "32": G}),
    "U2": ("Espressif:ESP32-H2-MINI-1", "Espressif:ESP32-H2-MINI-1U", "ESP32-H2-MINI-1U-H4S", "C49450282",
           "Thread/Zigbee radio, OpenThread RCP on UART0 (460800). W.FL (MHF III) antenna socket on the module: "
           "a different pigtail from the C5's",
           dict({"3": V, "15": V,                  # VBAT tied to 3V3
                 "8": "H2_EN", "23": "H2_BOOT",    # GPIO9 = boot strap
                 "22": "H2_GPIO8",                 # strap: must be high for download mode (pull-up)
                 "30": "H2_RXD", "31": "H2_TXD",
                 "26": None, "27": None},                # native USB unused: the C5 reflashes it
                **{p: None for p in ("4", "5", "6", "7", "9", "10", "12", "13", "16", "17", "18", "19", "20",
                                      "21", "24", "25", "28", "29", "32", "33", "34", "35")},
                **{str(p): G for p in [1, 2, 11, 14] + list(range(36, 54))})),
    # Pin map: see lib/Carrier.kicad_sym (LIS2DH12 datasheet "Pin description"); filled by PIN_U3 below.
    "U3": ("Carrier:LIS2DH12", "Package_LGA:LGA-12_2x2mm_P0.5mm", "LIS2DH12TR", "C110926",
           "accelerometer for the drive-lever pose (the Wayzn board's KXTJ3 no longer answers). I2C 0x19", {}),
    "J1": ("Connector_Generic:Conn_01x04", "Connector_PinSocket_2.54mm:PinSocket_1x04_P2.54mm_Vertical",
           "STM+IMP", "", "round-hole (machined) socket on the carrier's underside, onto a round-pin 1x4 header "
           "soldered into the Wayzn STM+IMP holes (square pins don't fit them). Pin order = the Wayzn silkscreen",
           {"1": V, "2": "STM_TX_W", "3": "STM_RX_W", "4": G}),
    "J6": ("Connector_Generic:Conn_02x04_Odd_Even", "Connector_PinHeader_2.00mm:PinHeader_2x04_P2.00mm_Vertical",
           "J105 ANCHOR", "C2905990", "2x4 2.0 mm male header on the underside, plugged into the Wayzn J105 socket: the "
           "carrier's second mounting point. Mechanical only: every pin unconnected",
           {str(p): None for p in range(1, 9)}),
    "J2": ("Connector_Generic:Conn_01x04", "Connector_JST:JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical",
           "TO WAYZN J2", "C131334", "PH-PH jumper to the Wayzn IR SENSOR socket: I2C (with the board's 4.7k pull-ups), 3V3",
           {"1": G, "2": "I2C_SCL", "3": "I2C_SDA", "4": V}),
    "J3": ("Connector_Generic:Conn_01x04", "Connector_JST:JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical",
           "GRID-EYE", "C131334", "the Grid-EYE cable moves here from the Wayzn J2 (same pin order; a longer PH cable if "
           "the original doesn't reach)", {"1": G, "2": "I2C_SCL", "3": "I2C_SDA", "4": V}),
    "J4": ("Connector_Generic:Conn_01x02", "Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical",
           "STM NRST", "C131337", "wire to SW300's top-right pad (STM32 NRST)", {"1": "STM_NRST_W", "2": G}),
    "J5": ("Connector_Generic:Conn_01x06", "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
           "PROG", "", "UART programming with any 3.3 V USB-serial adapter: GND, TXO (board TX), RXI (board RX), EN, "
           "BOOT. Pin 1 is 3V3 only through JP1 (open by default): bridge it for a bench flash off the door",
           {"1": "HDR_3V3", "2": G, "3": "C5_EN", "4": "C5_TXD0", "5": "C5_RXD0", "6": "C5_BOOT"}),
    "JP1": ("Jumper:SolderJumper_2_Open", "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm", "BENCH 3V3", "",
            "open: J5 pin 1 unconnected (the door powers the board). Bridge only to power a bench flash from the "
            "adapter with the board OFF the door", {"1": "HDR_3V3", "2": V}),
    "SW1": ("Switch:SW_Push", "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", "RESET", "C318884", "C5 EN",
            {"1": "C5_EN", "2": G}),
    "SW2": ("Switch:SW_Push", "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", "BOOT", "C318884",
            "C5 GPIO28: hold while pressing RESET for download mode", {"1": "C5_BOOT", "2": G}),
    "D1": ("Device:LED", "LED_SMD:LED_0603_1608Metric", "green", "C965804", "status", {"1": "LED_K", "2": "LED_STATUS"}),
}

# LIS2DH12 (LGA-12), ST DocID025056 Rev 6 Table 2: I2C mode needs CS high; SA0 left open (internal pull-up)
# = address 0x19 (firmware must not disable the pull-up via CTRL_REG0); Res (5) to GND; INT2 unused.
PIN_U3 = {"1": "I2C_SCL", "2": V, "3": None, "4": "I2C_SDA", "5": G, "6": G, "7": G, "8": G,
          "9": V, "10": V, "11": None, "12": "ACC_INT1"}

PASSIVES = [
    # ref, kind, value, net on pin 1, net on pin 2, purpose
    ("R1", "R", "10k", V, "C5_EN", "C5 EN pull-up (datasheet RC: 10k / 1u)"),
    ("C1", "C", "1u", "C5_EN", G, "C5 EN delay"),
    ("R2", "R", "10k", V, "C5_BOOT", "C5 GPIO28 strap pull-up (normal boot)"),
    ("R3", "R", "10k", V, "H2_EN", "H2 EN pull-up"),
    ("C2", "C", "1u", "H2_EN", G, "H2 EN delay"),
    ("R4", "R", "10k", V, "H2_BOOT", "H2 GPIO9 strap pull-up"),
    ("R5", "R", "10k", V, "H2_GPIO8", "H2 GPIO8 strap pull-up (download mode needs it high)"),
    ("R6", "R", "1k", "LED_K", G, "LED current (~1.3 mA)"),
    ("R7", "R", "100", "STM_TX_W", "STM_TX", "series: STM32 TX -> C5"),
    ("R8", "R", "100", "STM_RX", "STM_RX_W", "series: C5 -> STM32 RX"),
    ("R9", "R", "100", "STM_NRST", "STM_NRST_W", "series: NRST pull"),
    ("C3", "C", "22u", V, G, "C5 bulk (datasheet: 22u + 100n)"),
    ("C4", "C", "100n", V, G, "C5 decoupling"),
    ("C5", "C", "10u", V, G, "H2 bulk (datasheet: 10u + 100n)"),
    ("C6", "C", "100n", V, G, "H2 decoupling"),
    ("C7", "C", "100n", V, G, "accelerometer VDD"),
    ("C8", "C", "100n", V, G, "accelerometer VDD_IO"),
    ("C10", "C", "10u", V, G, "accelerometer VDD bulk (ST Fig. 5: 100n + 10u on pin 9)"),
    ("C9", "C", "22u", V, G, "3V3 entry bulk (from the Wayzn rail)"),
]
DNP = set()  # the Wayzn board has the I2C pull-ups (4.7k)

PASSIVE_FP = {"R": "Resistor_SMD:R_0402_1005Metric", "C": "Capacitor_SMD:C_0402_1005Metric"}
PASSIVE_FP_BY_VALUE = {"10u": "Capacitor_SMD:C_0603_1608Metric", "22u": "Capacitor_SMD:C_0805_2012Metric"}
PASSIVE_SYM = {"R": "Device:R", "C": "Device:C"}
# JLCPCB/LCSC, checked on jlcpcb.com/partdetail 2026-11-23 (B = Basic, E = Extended)
LCSC = {"R10k": "C25744", "R100": "C25076", "R1k": "C11702", "R4.7k": "C25900",   # B, UNI-ROYAL 0402 1%
        "C100n": "C1525", "C1u": "C52923", "C10u": "C19702", "C22u": "C45783"}    # B, Samsung


def all_parts():
    """ref -> dict(symbol, footprint, value, lcsc, notes, pins{pin: net or None}, dnp)"""
    out = {}
    for ref, (sym, fp, val, lcsc, notes, pins) in PARTS.items():
        pins = dict(PIN_U3) if ref == "U3" else dict(pins)
        out[ref] = dict(symbol=sym, footprint=fp, value=val, lcsc=lcsc, notes=notes, pins=pins, dnp=ref in DNP)
    for ref, kind, val, a, b, purpose in PASSIVES:
        fp = PASSIVE_FP_BY_VALUE.get(val, PASSIVE_FP[kind]) if kind == "C" else PASSIVE_FP[kind]
        out[ref] = dict(symbol=PASSIVE_SYM[kind], footprint=fp, value=val, lcsc=LCSC.get(kind + val, ""),
                        notes=purpose, pins={"1": a, "2": b}, dnp=ref in DNP)
    return out


# ---------------------------------------------------------------- KiCad rules and net classes
# Written to carrier.kicad_pro by the generators on every run (KiCad rewrites that file when it saves a board).
# Clearance 0.12 mm: the LIS2DH12's 0.5 mm-pitch pads leave exactly 0.15 mm; JLC's 4-layer minimum is 0.09 mm.
RULES = {"clearance": 0.12, "track": 0.2, "power_track": 0.3, "via_d": 0.6, "via_drill": 0.3,
         "min_track": 0.1, "min_clearance": 0.1, "edge_clearance": 0.3}


def write_project(path):
    import json
    r = RULES
    proj = {
        "meta": {"filename": "carrier.kicad_pro", "version": 3},
        "board": {"design_settings": {
            "defaults": {"board_outline_line_width": 0.05, "copper_line_width": r["track"], "silk_line_width": 0.12,
                         "silk_text_size_h": 0.8, "silk_text_size_v": 0.8, "silk_text_thickness": 0.12},
            "rules": {"min_clearance": r["min_clearance"], "min_track_width": r["min_track"],
                      "min_via_diameter": 0.45, "min_through_hole_diameter": 0.25, "min_hole_to_hole": 0.25,
                      "min_copper_edge_clearance": r["edge_clearance"], "min_silk_clearance": 0.0}}},
        "net_settings": {
            "classes": [
                {"name": "Default", "clearance": r["clearance"], "track_width": r["track"],
                 "via_diameter": r["via_d"], "via_drill": r["via_drill"]},
                {"name": "Power", "clearance": r["clearance"], "track_width": r["power_track"],
                 "via_diameter": r["via_d"], "via_drill": r["via_drill"]}],
            "netclass_patterns": [{"netclass": "Power", "pattern": "/+3V3"}, {"netclass": "Power", "pattern": "/GND"}]},
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [], "text_variables": {}}
    with open(path, "w") as f:
        json.dump(proj, f, indent=2)


def nets():
    n = {}
    for ref, p in all_parts().items():
        for pin, net in p["pins"].items():
            if net:
                n.setdefault(net, []).append((ref, pin))
    return n


if __name__ == "__main__":
    for net, conns in sorted(nets().items()):
        print(f"{net:12} {' '.join(f'{r}.{p}' for r, p in conns)}")
