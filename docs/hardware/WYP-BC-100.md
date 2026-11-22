# Main board: Wayzn WYP-BC-100 Rev 2.6

From an in-service unit, in the enclosure and on the bench. Tags:
**marking** = read off the part or silkscreen, **inferred** = guess,
**verified** = measured.

## Block diagram

```
 12V in (J7) ──► SK84 (D250) reverse/OR diode ──► buck (U30/L30) ──► 3V3 ──► everything
      │                                     (U301/L271: second rail?)
      └──► DRV8828 (U1) H-bridge ──► J6 OUT_A/OUT_B ──► gear motor
                ▲                     motor encoder ──► J5 SIG A/B ──┐
                │ PA4/PA5/PB0/PB1                                     │
          STM32F051K8 (U300) ◄────────────────────────────────────────┘
           ▲     ▲  SWD + USART2 = JP300 "STM" (Tag-Connect)
           │     └── USART1 ◄──► "STM+IMP" header (GND RX TX 3V3)
         button                   ▲
                              imp004m (U200) ── Wi-Fi/BLE ── Electric Imp cloud
                                  ├── SPI ── AT25SF161B 16 Mbit flash (U210)
                                  └── I2C (TP1/TP2) ──┬── IR SENSOR (J2) ──► Grid-EYE
                                                      └── KXTJ3 accelerometer (U50)
```

The imp owns the I2C bus (Grid-EYE and accelerometer); the STM32 owns the motor, the
encoder and the button (verified). The power path and the second rail are **inferred**.
Pin details: [STM32-PINMAP.md](STM32-PINMAP.md).

## Parts

| Ref | Part | Tag | Notes |
|---|---|---|---|
| U200 | Electric Imp **imp004m**, Murata "Type 1MD" module (FCC ID VPYLB1MD, IC 7720C-LB1MDIMP004) | marking | Wi-Fi + BLE (BLE used for BlinkUp pairing) plus the MCU that runs impOS and Wayzn's Squirrel code. Only runs code pushed from the impCloud account the device is assigned to, so it can't be reprogrammed without that account. The ESP32 brain holds it in reset |
| U210 | Adesto **AT25SF161B** 16 Mbit SPI NOR flash, SOIC-8 | marking | imp004m needs external SPI flash: impOS, the cached app and settings. Probably encrypted (inferred). Not dumped |
| U300 | ST **STM32F051K8T6**, LQFP32, Cortex-M0, 64 KB flash / 8 KB RAM | marking | The "motor controller" (`STM_V` in cloud state). Its firmware is updated over the air via the Imp (`STM_App_on_WSPD`) |
| JP300 "STM" | Tag-Connect footprint (TC2030-style, 6 pads + locating holes) | verified | SWD (SWDIO, SWCLK), VDD and USART2 (the STM's debug console) for U300; pad map in [STM32-PINMAP.md](STM32-PINMAP.md) |
| U1 | TI **DRV8828** (HTSSOP-28) | marking | **One** H-bridge with current regulation, intended for one stepper phase or a brushed DC motor. J6 has only two pins: the motor is a **brushed DC gear motor with a quadrature encoder** on J5, not a stepper |
| J6 | Motor OUT_A / OUT_B | marking | 2-pin, large pitch (JST VH-style?) |
| J7 | 12 V / GND input | marking | 2-pin, same family as J6 |
| J5 | GND / SIG B / SIG A / 3V3 | marking | Quadrature encoder from the motor's rear PCB. Matches the "counts" in `Position`, `OpenTo`, `DoorOffset` (≈767 counts/inch) |
| J2 "IR SENSOR" | GND / SCL / DATA / 3V3 (pin 1 = GND) | marking | I2C to the thermal sensor board; see [ir-sensor-board.md](ir-sensor-board.md) |
| STM+IMP | GND / RX / TX / 3V3, 0.1" through-hole, unpopulated; square pad = 3V3 | verified | The STM32 USART1 (PA9/PA10) ↔ imp link, 115200 8N1. RX/TX are named from the STM's side. The ESP32 brain connects here ([STM-PROTOCOL.md](../STM-PROTOCOL.md)) |
| J105 | 2×4 female header, 2.54 mm | marking | Unknown. Possibly a socket for the pet-tag "Bridge" receiver (cloud state has `Bridge.Connected` and `Bridge_App_on_WSPD`) or a factory/expansion port (inferred). J105-4 = GND, J105-8 = 3V3 |
| TP3/TP5/TP7/TP8 "E L R S" | test pads | marking | Unknown |
| TP1/TP2 | test pads near U300 | verified | I2C SCL / SDA (the J2 IR SENSOR bus), 4.7 kΩ pull-ups to 3V3. Not on any STM32 pin |
| SW300 "EXP RST" | tactile switch | verified | NRST of U300 (top pad) |
| SW1 "IMP_RST" | tactile switch | verified | imp004m reset. Held (or bridged), the imp stays silent and the STM runs on its own |
| D1A/D1B, Q1, D1004 (+J1001) | LEDs; D1004 under a clear light pipe on the front edge | marking | Status LED(s) (`LedBrightness` setting). Q1 could be a phototransistor or the LED driver |
| U50 (+C50–C53) | small LGA/DFN near STM+IMP header | verified | Kionix KXTJ3 accelerometer, I2C 0x0F (cloud `X/Y/Z`); the imp uses it to sense the drive lever ([ir-sensor-board.md](ir-sensor-board.md)) |
| U30 + L30 + D1005 | buck regulator, Schottky catch diode | inferred | 12 V → 3.3 V (or 5 V) |
| U301 + L271 | second small regulator | inferred | Second rail. Measure it |
| U8 | SOT-23-6, marking "BJ50" | inferred | Probably the motor current-sense amplifier (R2 → U8 → PA1) |
| D250 | MCC **SK84** 8 A 40 V Schottky | marking | Input reverse-polarity / OR-ing |
| R2 | 2512 resistor "10C" | inferred | Motor current-sense shunt (the `H` status field) |
| T1, T2 | SOT-23-6 | inferred | MOSFETs or level shifting |
| Button (by J6, under black tape, orange actuator, white cap) | Through-hole tactile switch | marking | The physical door button (`ButtonOperation`), read by the STM32 |
| C3 | 100 µF electrolytic | marking | Bulk capacitance on the motor rail |

## Not yet measured

- Rail voltages (buck output, U301 output).
- J105 and TP3/5/7/8 destinations.
- Motor nameplate, winding resistance, encoder lines per revolution.
- Connector families and pitch for J5, J6, J7 and J2.
