# U300 STM32F051K8T6 pin map (continuity, bench, unpowered)

Measured by continuity from each STM32 pin. Pin numbers follow the
LQFP32 datasheet (pin 1 at the dot, counter-clockwise). JP300 pad numbers are
**this repo's labels** (see below), not Tag-Connect's numbering.

JP300 labels as seen from the component side (2 columns × 3 rows):

```
  2   1
  3   6
  4   5
```

| Pin | Function | Connects to | Interpretation | Conf. |
|---|---|---|---|---|
| 1 | VDD | JP300-2 | JP300 supplies Vtarget | verified |
| 2 | PF0 | not probed | | |
| 3 | PF1 | not probed | | |
| 4 | NRST | SW300 (EXP RST) top pad, 0.2 Ω. (Not J105-8: that is 3V3) | ESP32 reset tap = SW300 top pad (open-drain, pull low). Other routes (4-pad row, TPs) not yet checked | verified |
| 5 | VDDA | not probed | | |
| 6 | PA0 (ADC0) | R101, R102 | Divider: probably 12 V sense (`v` status field?) | inferred |
| 7 | PA1 (ADC1) | C308 | Filtered analog input: probably motor current (`H` effort?) | inferred |
| 8 | PA2 (USART2_TX) | JP300-3 | USART2, the STM's debug console ([STM-FIRMWARE.md](../STM-FIRMWARE.md)) | verified |
| 9 | PA3 (USART2_RX) | JP300-5 | (as above) | verified |
| 10, 11 | PA4, PA5 | U1 DRV8828 | PA4 = DECAY (tri-level), PA5 = nRESET (roles read from the firmware) | verified (continuity); roles read from code |
| 12 | PA6 (TIM3_CH1) | J5 SIG B | Encoder: TIM3 encoder mode | verified |
| 13 | PA7 (TIM3_CH2) | J5 SIG A | Encoder | verified |
| 14, 15 | PB0, PB1 | U1 DRV8828 | PB0 = nSLEEP, PB1 = nFAULT input (roles read from the firmware) | verified (continuity); roles read from code |
| 16 | VSS | GND | | verified |
| 18 | PA8 | NC | | verified |
| 19 | PA9 (USART1_TX) | STM+IMP header TX, 0.2 Ω | The imp UART, USART1, no series resistor | verified |
| 20 | PA10 (USART1_RX) | STM+IMP header RX, 0.2 Ω | | verified |
| 23 | PA13 SWDIO | JP300-1 | SWD | verified |
| 24 | PA14 SWCLK | JP300-6 | SWD | verified |
| 31 | BOOT0 | ~0 Ω to GND (reading unsteady); ~30 kΩ to 3V3 | Tied low: the STM32 always boots from flash; the imp **cannot** enter the ROM bootloader through BOOT0 | verified |
| 22 | PA12 | not probed | DRV8828 PHASE (direction), from the firmware | read from code |
| 26–30 | PB3–PB7 | not probed | DRV8828 I0–I4 current DAC, from the firmware | read from code |
| 17, 21, 25, 32 | | not probed (TP1/TP2 checked: not on any STM32 pin) | One is probably the DRV8828 ENBL PWM (a timer channel), one the button | |

JP300 carries SWD (1, 6), VDD (2) and USART2 (3, 5, the debug console); 4 is probably GND.

## Other nets

| Net | Connects to | Notes | Conf. |
|---|---|---|---|
| J2 SCL / DATA (Grid-EYE I2C) | TP1 / TP2 only. **No STM32 pin**, and nothing else visible | So the bus goes under the imp module (its pads are underneath): **the imp owns the Grid-EYE**. With the imp held in reset, the ESP32 can be the I2C master via TP1/TP2. Both read 4.5 kΩ to 3V3 (unpowered), so the pull-ups are probably 4.7 kΩ on the main board; the ESP32 needs no pull-ups of its own | verified |
| GND | J105-4 (0 Ω to GND; ~1 kΩ to 3V3, the same rail load as 3V3→GND) | | verified |
| IMP_RST (SW1, non-ground pair) | Runs straight under U200 (imp004m); not on J105 or the E/L/R/S TPs | To hold the imp in reset: bridge SW1's top-right pad to its bottom-left pad (see the README wiring table), or hold SW1 pressed | verified (not on headers) |
| J105 → GND, unpowered (readings drift as the caps charge) | 1: 100 kΩ, 2: 1.5 MΩ, 3: 100 kΩ, 4: 0, 5: 2 MΩ, 6: 2 MΩ, 7: 100 kΩ, 8: 100 kΩ | Signal pins; roles unknown | verified (raw) |
| 3V3 | J105-8 (0.2 Ω) | | verified |
| Motor current sense | R2 (large shunt, marked "10C") → U8 ("BJ50", probably a current-sense amplifier) → C308 → PA1 (pin 7) | The STM measures motor current: the basis of `H` (effort) and effort obstruction | inferred |
