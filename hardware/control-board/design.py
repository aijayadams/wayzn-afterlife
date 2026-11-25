"""wayzn-afterlife control board (Phase B): the single source of truth for parts, nets, mechanics and rules.

A drop-in replacement for the Wayzn WYP-BC-100: same outline, holes and connector positions, so the door's
cables, shell posts, button cap and light pipe line up. The ESP32-C5 drives the motor itself through a DRV8876
(see docs/CONTROL-BOARD.md). gen_sch.py turns this into control.kicad_sch, gen_pcb.py (+ layout.py) into
control.kicad_pcb.

Frame (mm): the board seen from the component side, J6/J7 end at the top. Origin = the corner at the J6/J7 end on
the side away from the button; x across the board (0 -> 33.94, toward the button side), y along it (0 -> 152.6,
toward J2/J5). Same frame as docs/hardware/WYP-BC-100-MECHANICAL.md (from the owner's 1200 dpi scans).
"""

# ---------------------------------------------------------------- mechanics (WYP-BC-100, scanned 2026-11-24)
OUTLINE = {"x": (0.0, 33.94), "y": (0.0, 152.6), "corner_r": 0.5}
HOLES = [(2.99, 3.08), (30.94, 3.16), (2.94, 149.42), (30.86, 149.49)]  # shell posts, 4.0 mm (verified)
WAYZN = {  # features of the original board our parts must match (verified = scan, inferred = front scan/photos)
    "j7_pins": [(9.36, 11.6), (13.29, 11.6)],      # 12V, GND (verified)
    "j6_pins": [(18.03, 11.6), (21.99, 11.6)],     # OUT_B, OUT_A (verified)
    "button_contacts": [(29.12, 12.19), (29.12, 16.70)],   # PTS645 right-angle (verified)
    "button_pegs": [(26.66, 10.92), (26.65, 17.89)],
    "light_pipe": {"x": (29.0, 33.94), "y": (37.5, 41.5)},  # inferred (front scan); sticks out ~4 mm past x 33.94
    "j5": {"x": (10.8, 16.2), "y": (138.5, 150.6)},  # encoder, PH SMD top entry (inferred, +-1 mm)
    "j2": {"x": (20.8, 26.2), "y": (138.5, 150.6)},  # IR sensor (inferred, +-1 mm)
}

G = "GND"
V = "+3V3"
VM = "VM"  # 12 V motor rail, after the reverse-polarity diode

# ---------------------------------------------------------------- parts
# ref: (symbol, footprint, value, LCSC, notes, {pin: net or None = no connect})
# LCSC "" = not chosen/verified yet (order.py and fab.py list them).
PARTS = {
    # --- radios / MCU
    "U1": ("Espressif:ESP32-C5-WROOM-1U", "Espressif:ESP32-C5-WROOM-1U", "ESP32-C5-WROOM-1U-N8R8", "C51950748",
           "main MCU and motor controller: Wi-Fi 6 dual band, BLE 5; U.FL antenna (2.4/5 GHz)",
           # The outputs that can move the motor (EN, nSLEEP, PH) avoid the strapping pins (MTMS/GPIO2, GPIO7,
           # GPIO27, GPIO28) and have external pull-downs, so reset and boot can never drive the bridge. Left-side
           # order (EN, PH, nSLEEP, nFAULT, then the two ADCs, bottom to top) matches the DRV8876's pin order, so
           # the six lines reach it without crossing.
           {"1": G, "2": V, "3": "C5_EN",
            "4": "H2_BOOT",                        # GPIO2 (MTMS: listed as a strap, no boot function): to H2 GPIO9
            "5": "H2_EN",                          # GPIO3: open-drain, resets the H2
            "6": None,                             # GPIO0: spare
            "7": "VM_ADC",                         # GPIO1 = ADC1_CH0: 12 V rail / 11
            "8": "IPROPI_ADC",                     # GPIO6 = ADC1_CH5: motor current, 3.0 V/A
            "9": "DRV_NFAULT",                     # GPIO7 (strap: JTAG source; no internal pull, so it must be
                                                   # driven at reset: the 10k pull-up does it): input
            "10": "DRV_NSLEEP",                    # GPIO8: driver enable, 10k pull-down (asleep at reset)
            "11": "DRV_PH",                        # GPIO9: direction, 10k pull-down
            "12": "DRV_EN",                        # GPIO10: PWM to EN/IN1, 10k pull-down
            "13": "H2_TXD",                        # GPIO13 (USB D-) <- H2 U0TXD (RCP UART, C5 RX)
            "14": "H2_RXD",                        # GPIO14 (USB D+) -> H2 U0RXD
            "15": "C5_BOOT",                       # GPIO28: boot strap
            "16": "ENC_B",                         # GPIO5: PCNT
            "17": "ENC_A",                         # GPIO4: PCNT
            "18": "I2C_SDA",                       # GPIO27 (strap): must be HIGH at reset for UART download
                                                   # (Joint Download Boot 0 = GPIO27 1, GPIO28 0): the 4.7k
                                                   # I2C pull-up holds it high. (INT1 here would idle low.)
            "19": None,                            # GPIO15 = PSRAM SPICS1: must stay unconnected
            "20": None, "22": None,
            "21": "ACC_INT1",                      # GPIO23
            "23": "I2C_SCL",                       # GPIO24
            "24": "C5_RXD0", "25": "C5_TXD0",      # UART0, programming header
            "26": "LED_STATUS",                    # GPIO25 (SDIO-timing strap only)
            "27": "BTN",                           # GPIO26 (download-only strap; idles high via its pull-up)
            "28": G, "29": G, "30": G, "31": None, "32": G}),
    "U2": ("Espressif:ESP32-H2-MINI-1", "Espressif:ESP32-H2-MINI-1U", "ESP32-H2-MINI-1U-H4S", "C49450282",
           "Thread radio (OpenThread RCP on UART0); the C5 resets and reflashes it. MHF III antenna socket",
           dict({"3": V, "15": V,
                 "8": "H2_EN", "23": "H2_BOOT", "22": "H2_GPIO8",
                 "30": "H2_RXD", "31": "H2_TXD",
                 "26": None, "27": None},
                **{p: None for p in ("4", "5", "6", "7", "9", "10", "12", "13", "16", "17", "18", "19", "20",
                                      "21", "24", "25", "28", "29", "32", "33", "34", "35")},
                **{str(p): G for p in [1, 2, 11, 14] + list(range(36, 54))})),
    "U3": ("Control:LIS2DH12", "Package_LGA:LGA-12_2x2mm_P0.5mm", "LIS2DH12TR", "C110926",
           "accelerometer (drive-lever pose, knocks). I2C 0x19. Optional: the board works without it", {}),
    # --- power
    "U4": ("Regulator_Switching:TPS54202DDC", "Package_TO_SOT_SMD:SOT-23-6", "TPS54202DDCR", "C191884",
           "12 V -> 3.3 V buck, 2 A, 28 V input (above the TVS clamp). TI table 7-2 values for 3.3 V",
           {"1": G, "2": "BUCK_SW", "3": VM, "4": "BUCK_FB", "5": None, "6": "BUCK_BST"}),  # EN floats = on
    "L1": ("Device:L", "Inductor_SMD:L_Sunlord_SWPA6045S", "10uH", "C79272", "buck inductor, shielded 6x6",
           {"1": "BUCK_SW", "2": V}),
    "D1": ("Device:D_Schottky", "Diode_SMD:D_SMA", "SS54", "C22452",
           "reverse-polarity protection, 40 V 5 A (~0.5 V drop at 1 A)", {"1": VM, "2": "VIN"}),  # K, A
    "D2": ("Device:D_TVS", "Diode_SMD:D_SMB", "SMBJ15A", "C188412",
           "12 V rail TVS: 15 V standoff, 24.4 V clamp (< buck 28 V, < DRV8876 37 V)", {"1": VM, "2": G}),
    # --- motor
    "U5": ("Control:DRV8876", "Package_SO:HTSSOP-16-1EP_4.4x5mm_P0.65mm_EP3.4x5mm_Mask2.46x2.31mm",  # thermal vias added in layout.py (0.3 mm: JLC default)
           "DRV8876PWPR", "C575551",
           "motor H-bridge (OUT2 -> J6 OUT_A, OUT1 -> OUT_B; direction is firmware). PH/EN mode (PMODE low); IMODE to GND: fixed off-time chopping, auto-retry, nFAULT only on real faults "
           "(20k would pulse nFAULT on every chop: review 2026-11-24); "
           "VREF = 3V3; ITRIP = VREF / (1000 uA/A x R_IPROPI) = 3.3 V / 3.0 V/A = 1.10 A (Wayzn caps at 1.03 A)",
           {"1": "DRV_EN", "2": "DRV_PH", "3": "DRV_NSLEEP", "4": "DRV_NFAULT", "5": V,
            "6": "DRV_IPROPI", "7": "DRV_IMODE", "8": "MOT_B", "9": G, "10": "MOT_A", "11": VM,
            "12": "DRV_VCP", "13": "DRV_CPH", "14": "DRV_CPL", "15": G, "16": G, "17": G}),
    # --- connectors (Wayzn's references and pin order, so the door's cables plug straight in)
    "J7": ("Connector_Generic:Conn_01x02", "Connector_JST:JST_VH_B2PS-VH_1x02_P3.96mm_Horizontal", "12V IN",
           "C158007", "power in (JST VH side entry, housing over the board end). Pin 1 = GND (next to J6), 2 = 12V",
           {"1": G, "2": "VIN"}),
    "J6": ("Connector_Generic:Conn_01x02", "Connector_JST:JST_VH_B2PS-VH_1x02_P3.96mm_Horizontal", "MOTOR",
           "C158007", "motor (JST VH side entry). Pin 1 = OUT_A (button side), 2 = OUT_B (next to J7's GND)",
           {"1": "MOT_A", "2": "MOT_B"}),
    "J5": ("Connector_Generic:Conn_01x04", "Connector_JST:JST_PH_B4B-PH-SM4-TB_1x04-1MP_P2.00mm_Vertical", "ENCODER",
           "C160354", "motor encoder, Wayzn pin order: GND, SIG B, SIG A, 3V3",
           {"1": G, "2": "ENC_B_IN", "3": "ENC_A_IN", "4": V, "MP": None}),
    "J2": ("Connector_Generic:Conn_01x04", "Connector_JST:JST_PH_B4B-PH-SM4-TB_1x04-1MP_P2.00mm_Vertical", "IR SENSOR",
           "C160354", "Grid-EYE, Wayzn pin order: GND, SCL, SDA, 3V3", {"1": G, "2": "I2C_SCL", "3": "I2C_SDA", "4": V,
                                                                      "MP": None}),
    "J1": ("Connector_Generic:Conn_01x06", "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical", "PROG", "",
           "UART programming: 3V3* (via JP1 only), GND, EN, TXO, RXI, BOOT",
           {"1": "HDR_3V3", "2": G, "3": "C5_EN", "4": "C5_TXD0", "5": "C5_RXD0", "6": "C5_BOOT"}),
    "JP1": ("Jumper:SolderJumper_2_Open", "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm", "BENCH 3V3", "",
            "open: J1 pin 1 unconnected. Bridge only to power a bench flash from the adapter, board off the door",
            {"1": "HDR_3V3", "2": V}),
    # --- buttons, LED
    "SW1": ("Switch:SW_Push", "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", "RESET", "C318884", "C5 EN",
            {"1": "C5_EN", "2": G}),
    "SW2": ("Switch:SW_Push", "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", "BOOT", "C318884",
            "C5 GPIO28: hold while pressing RESET for download mode", {"1": "C5_BOOT", "2": G}),
    "SW3": ("Switch:SW_Push", "Button_Switch_THT:SW_Tactile_SPST_Angled_PTS645Vx58-2LFS", "DOOR BUTTON", "C285524",
            "C&K PTS645VL58 right-angle, in Wayzn's switch position (the same footprint): the shell's cap fits its "
            "plunger", {"1": "BTN", "2": G}),
    "D3": ("LED:WS2812B-2020", "LED_SMD:LED_WS2812B-2020_PLCC4_2.0x2.0mm", "WS2812B-2020-V6", "C52917434",
           "addressable RGB status LED under the shell's light pipe (C5 RMT on GPIO25). V6: VDD 3.3-5.3 V, "
           "VIH 0.55 x VDD (2.04 V at the LDO's 3.71 V maximum: 1.2 V margin from 3.3 V logic)",
           {"1": None, "2": G, "3": "LED_STATUS", "4": "LED_VDD"}),
    "U6": ("Regulator_Linear:HT75xx-1-SOT89", "Package_TO_SOT_SMD:SOT-89-3", "HT7536-1", "C78876",
           "3.6 V LDO for the RGB LED from VM (genuine Holtek: 30 V operating, 33 V abs, survives the 24.4 V TVS "
           "clamp). LED VDD 3.6 V is in the V6's 3.3-5.3 V range; VIH 0.55 x 3.71 V = 2.04 V max. ~0.3 W at full "
           "white from 12 V (SOT-89: 0.5 W): limit brightness in firmware. Any HT75xx-1 SOT-89 3.6-4.4 V fits",
           {"1": G, "2": VM, "3": "LED_VDD"}),
    # --- mechanics and test points
    **{f"H{i + 1}": ("Mechanical:MountingHole", "MountingHole:MountingHole_4.3mm_M4", "POST", "",
                     "shell post: NPTH 4.3 mm (Wayzn's is ~4.0; 0.3 mm slack for the scan's scale error)", {})
       for i in range(4)},
    "TP1": ("Connector:TestPoint", "TestPoint:TestPoint_Pad_D1.5mm", "VM", "", "12 V rail", {"1": VM}),
    "TP2": ("Connector:TestPoint", "TestPoint:TestPoint_Pad_D1.5mm", "3V3", "", "3.3 V rail", {"1": V}),
    "TP3": ("Connector:TestPoint", "TestPoint:TestPoint_Pad_D1.5mm", "GND", "", "ground", {"1": G}),
    "TP4": ("Connector:TestPoint", "TestPoint:TestPoint_Pad_D1.5mm", "IPROPI", "", "motor current, 3.0 V/A "
            "(after the 1k/10n filter)", {"1": "IPROPI_ADC"}),
}

# LIS2DH12 (LGA-12), ST DocID025056 Rev 6 Table 2: I2C mode needs CS high; SA0 open (internal pull-up) = 0x19.
PIN_U3 = {"1": "I2C_SCL", "2": V, "3": None, "4": "I2C_SDA", "5": G, "6": G, "7": G, "8": G,
          "9": V, "10": V, "11": None, "12": "ACC_INT1"}

R0402, C0402 = "Resistor_SMD:R_0402_1005Metric", "Capacitor_SMD:C_0402_1005Metric"
C0603, C0805, C1206 = "Capacitor_SMD:C_0603_1608Metric", "Capacitor_SMD:C_0805_2012Metric", "Capacitor_SMD:C_1206_3216Metric"
CELEC = "Capacitor_SMD:CP_Elec_6.3x7.7"
# value -> LCSC (JLC Basic unless noted). "" = to choose/verify.
# Verified against JLC's parts library 2026-11-24 (E = Extended; the rest are Basic).
LCSC = {("R", "10k"): "C25744", ("R", "100"): "C25076", ("R", "1k"): "C11702", ("R", "4.7k"): "C25900",
        ("R", "100k"): "C25741", ("R", "0"): "C17168", ("R", "22.1k"): "C43473", ("R", "3.0k"): "C25784", ("R", "20k"): "C25765",  # 22.1k, 3.0k: E
        ("C", "100n"): "C1525", ("C", "100n/16V"): "C1525", ("C", "1u"): "C52923", ("C", "10u"): "C96446",
        ("C", "22u"): "C45783", ("C", "56p"): "C1572", ("C", "10n"): "C15195", ("C", "1n"): "C1523",  # 56p: E
        ("C", "100n/50V"): "C149620", ("C", "22n/50V"): "C21122", ("C", "10u/50V"): "C13585",
        ("CP", "100u/35V"): "C125975"}  # Nichicon UWT1V101MCL1GS, E

PASSIVES = [
    # ref, kind, value, footprint, pin 1 net, pin 2 net, purpose
    # 12 V input and motor rail (35 V+ parts: the TVS clamps at 24.4 V)
    ("C1", "CP", "100u/35V", CELEC, VM, G, "motor rail bulk (Wayzn: 100 uF can)"),
    ("C3", "C", "100n/50V", C0603, VM, G, "DRV8876 VM, at the pin"),
    ("C4", "C", "10u/50V", C1206, VM, G, "buck input"),
    ("C5", "C", "100n/50V", C0603, VM, G, "buck input, at the pin"),
    # buck (TPS54202, TI table 7-2 for 3.3 V)
    ("C6", "C", "100n", C0402, "BUCK_BST", "BUCK_SW", "buck bootstrap"),
    ("R1", "R", "100k", R0402, V, "BUCK_FB", "buck feedback, top (0.596 V x (1 + 100/22.1) = 3.29 V)"),
    ("R2", "R", "22.1k", R0402, "BUCK_FB", G, "buck feedback, bottom"),
    ("C7", "C", "56p", C0402, V, "BUCK_FB", "buck feed-forward"),
    ("C8", "C", "22u", C0805, V, G, "buck output"),
    ("C9", "C", "22u", C0805, V, G, "buck output"),
    ("R3", "R", "100k", R0402, VM, "VM_ADC", "12 V sense, top (/11: 12 V -> 1.09 V, 24 V -> 2.18 V)"),
    ("R4", "R", "10k", R0402, "VM_ADC", G, "12 V sense, bottom"),
    ("C10", "C", "100n", C0402, "VM_ADC", G, "12 V sense filter"),
    # motor driver
    ("C11", "C", "100n/16V", C0402, "DRV_VCP", VM, "DRV8876 charge pump (VCP to VM, 16 V)"),
    ("C12", "C", "22n/50V", C0603, "DRV_CPH", "DRV_CPL", "DRV8876 charge-pump flying cap (VM rated)"),
    ("R7", "R", "3.0k", R0402, "DRV_IPROPI", G, "IPROPI: 1000 uA/A x 3.0k = 3.0 V/A; trips at VREF = 3.3 V "
     "(1.10 A). At the C5 end of the line: the DRV8876 compares the pin voltage, so position doesn't matter"),
    ("R8", "R", "0", R0402, "DRV_IMODE", G, "IMODE tied low: fixed off-time chopping, auto-retry"),
    ("R9", "R", "1k", R0402, "DRV_IPROPI", "IPROPI_ADC", "IPROPI to ADC, RC filter"),
    ("C13", "C", "10n", C0402, "IPROPI_ADC", G, "IPROPI ADC filter (1k/10n: 16 kHz)"),
    ("R10", "R", "10k", R0402, "DRV_EN", G, "EN pull-down: motor off at reset"),
    ("R11", "R", "10k", R0402, "DRV_PH", G, "PH pull-down"),
    ("R12", "R", "10k", R0402, "DRV_NSLEEP", G, "nSLEEP pull-down: driver asleep at reset"),
    ("R13", "R", "10k", R0402, V, "DRV_NFAULT", "nFAULT pull-up"),
    # encoder (pull-ups, series R, RC against motor noise; ~2 kHz edges, 100R/1n = 100 ns)
    ("R14", "R", "10k", R0402, V, "ENC_A_IN", "encoder A pull-up"),
    ("R15", "R", "10k", R0402, V, "ENC_B_IN", "encoder B pull-up"),
    ("R16", "R", "100", R0402, "ENC_A_IN", "ENC_A", "encoder A series"),
    ("R17", "R", "100", R0402, "ENC_B_IN", "ENC_B", "encoder B series"),
    ("C14", "C", "1n", C0402, "ENC_A", G, "encoder A filter"),
    ("C15", "C", "1n", C0402, "ENC_B", G, "encoder B filter"),
    # I2C (Grid-EYE + accelerometer; the Wayzn board had these 4.7k pull-ups)
    ("R18", "R", "4.7k", R0402, V, "I2C_SCL", "I2C pull-up"),
    ("R19", "R", "4.7k", R0402, V, "I2C_SDA", "I2C pull-up"),
    # C5
    ("C16", "C", "22u", C0805, V, G, "C5 bulk (datasheet 22u + 100n)"),
    ("C17", "C", "100n", C0402, V, G, "C5 decoupling"),
    ("R20", "R", "10k", R0402, V, "C5_EN", "C5 EN pull-up (10k/1u)"),
    ("C18", "C", "1u", C0402, "C5_EN", G, "C5 EN delay"),
    ("R21", "R", "10k", R0402, V, "C5_BOOT", "C5 GPIO28 strap pull-up"),
    # H2
    ("C19", "C", "10u", C0603, V, G, "H2 bulk"),
    ("C20", "C", "100n", C0402, V, G, "H2 decoupling"),
    ("R22", "R", "10k", R0402, V, "H2_EN", "H2 EN pull-up"),
    ("C21", "C", "1u", C0402, "H2_EN", G, "H2 EN delay"),
    ("R23", "R", "10k", R0402, V, "H2_BOOT", "H2 GPIO9 strap pull-up"),
    ("R24", "R", "10k", R0402, V, "H2_GPIO8", "H2 GPIO8 pull-up (download mode needs it high)"),
    # accelerometer
    ("C22", "C", "100n", C0402, V, G, "accelerometer VDD"),
    ("C23", "C", "100n", C0402, V, G, "accelerometer VDD_IO"),
    ("C24", "C", "10u", C0603, V, G, "accelerometer VDD bulk"),
    # button, LED
    ("R25", "R", "10k", R0402, V, "BTN", "button pull-up"),
    ("C25", "C", "100n", C0402, "BTN", G, "button debounce"),
    ("C26", "C", "100n/50V", C0603, VM, G, "LED LDO input"),
    ("C27", "C", "10u", C0603, "LED_VDD", G, "LED LDO output, 2 mm from the LED: also its VDD bypass"),
    ("R27", "R", "100k", R0402, "LED_STATUS", G, "WS2812 DIN pull-down: no random colour while the C5 is in reset"),
    ("R28", "R", "0", R0402, V, "LED_VDD", "DNP: fit instead of U6 to run the LED from 3V3 (V6 rated 3.3-5.3 V)"),
]
DNP = {"R28"}  # fallback LED supply link, not fitted

SYM = {"R": "Device:R", "C": "Device:C", "CP": "Device:C_Polarized"}


def all_parts():
    """ref -> dict(symbol, footprint, value, lcsc, notes, pins{pin: net or None}, dnp)"""
    out = {}
    for ref, (sym, fp, val, lcsc, notes, pins) in PARTS.items():
        pins = dict(PIN_U3) if ref == "U3" else dict(pins)
        out[ref] = dict(symbol=sym, footprint=fp, value=val, lcsc=lcsc, notes=notes, pins=pins, dnp=ref in DNP)
    for ref, kind, val, fp, a, b, purpose in PASSIVES:
        out[ref] = dict(symbol=SYM[kind], footprint=fp, value=val, lcsc=LCSC.get((kind, val), ""),
                        notes=purpose, pins={"1": a, "2": b}, dnp=ref in DNP)
    return out


# ---------------------------------------------------------------- KiCad rules and net classes
# Clearance 0.12 mm: the LIS2DH12's 0.5 mm-pitch pads leave exactly 0.15 mm (JLC 4-layer minimum 0.09 mm).
# Motor and 12 V nets get 0.8 mm tracks (1.13 A cap; 1 oz outer copper).
RULES = {"clearance": 0.12, "track": 0.2, "power_track": 0.3, "motor_track": 0.8, "via_d": 0.6, "via_drill": 0.3,
         "min_track": 0.1, "min_clearance": 0.1, "edge_clearance": 0.3}
MOTOR_NETS = ("VIN", VM, "MOT_A", "MOT_B")


def write_project(path):
    import json
    r = RULES
    proj = {
        "meta": {"filename": "control.kicad_pro", "version": 3},
        "board": {"design_settings": {
            "defaults": {"board_outline_line_width": 0.05, "copper_line_width": r["track"], "silk_line_width": 0.12,
                         "silk_text_size_h": 0.8, "silk_text_size_v": 0.8, "silk_text_thickness": 0.12},
            "rules": {"min_clearance": r["min_clearance"], "min_track_width": r["min_track"],
                      "min_via_diameter": 0.45, "min_through_hole_diameter": 0.3, "min_hole_to_hole": 0.25,
                      "min_copper_edge_clearance": r["edge_clearance"], "min_silk_clearance": 0.0},
            # Courtyards are checked by gen_pcb.check_overlaps, with two documented exceptions KiCad can't express:
            # the VH housings 0.84 mm apart (as on the Wayzn board) and the posts (no screw heads).
            "rule_severities": {"courtyards_overlap": "warning"}}},
        "net_settings": {
            "classes": [
                {"name": "Default", "clearance": r["clearance"], "track_width": r["track"],
                 "via_diameter": r["via_d"], "via_drill": r["via_drill"]},
                {"name": "Power", "clearance": r["clearance"], "track_width": r["power_track"],
                 "via_diameter": r["via_d"], "via_drill": r["via_drill"]},
                {"name": "Motor", "clearance": 0.2, "track_width": r["motor_track"],
                 "via_diameter": 0.8, "via_drill": 0.4}],
            "netclass_patterns": [{"netclass": "Power", "pattern": "/+3V3"}, {"netclass": "Power", "pattern": "/GND"}]
            + [{"netclass": "Motor", "pattern": "/" + n} for n in MOTOR_NETS]},
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
        flag = "" if len(conns) > 1 else "   <-- single connection"
        print(f"{net:12} {' '.join(f'{r}.{p}' for r, p in conns)}{flag}")
