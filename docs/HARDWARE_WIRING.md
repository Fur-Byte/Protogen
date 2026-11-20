# Prototype schematic and connection package

These connection-level schematics are for review. They are not a PCB fabrication release: converter/eFuse timing values, connector mechanics and current limits require the measurements in [ENGINEERING_PLAN.md](ENGINEERING_PLAN.md). Existing wiring is preserved in the working configuration; all added connections are behind disabled firmware flags. No purchases, wiring or flashing occurred.

## Sheet 1 — power distribution

```text
External protected USB-C PD bank (15 V / 3 A qualified)
  -> rated USB-C cable
  -> documented sink controller/module configured for 15 V
  -> F_INPUT / accessible master switch / reverse+surge protection
  -> INA228 #5832 VIN+ -> fitted 15 mOhm shunt -> VIN-
  -> PD15_MONITORED star
       +-> F_FACE_IN -> D36V50F5 -> FACE5_PRE -> display eFuse -> FACE5_SW
       |                                                    -> removable assembly
       +-> F_CORE_IN -> D24V10F5 -> CORE5 -> Teensy VIN + 74AHCT125 VCC
       |                                      VUSB isolated per PJRC
       +-> F_AUX_IN  -> D24V10F3 -> AUX3_PRE -> current-limited switch -> AUX3
                                                    +-> helmet ESP32 3V3
                                                    +-> protected sensor/OLED branches
Ground star: INA/bucks/core/aux/display return. Display return is heavy and separate.
```

U_INA logic VCC=3.3 V auxiliary, SDA/SCL pulled to auxiliary 3.3 V. INA high-side inputs carry the PD voltage, never an ESP GPIO. Route power through the shunt for every accessory whose consumption you need to measure. The shunt dissipates I²R; at 3 A that is 0.135 W and 45 mV drop. Keep its high-current loop short and retain the module's Kelvin measurement routing.

Fuses/current limits are qualified by measured load and weakest conductor. Do not tie buck outputs together. The display eFuse must implement reverse blocking, appropriate OVP/UVLO, qualified inrush slew and a limit chosen with tolerance. Use the selected TI variant's reference schematic and calculator for R_ILM/C_DVDT/C_ITIMER/OVLO/UVLO; these values intentionally remain unpopulated until load/capacitance are measured. A nominal 5 A limit is not preselected.

Supply each converter's required input/output capacitors and local load decoupling. Screen/sensor shorts need their own branch limiting if they must leave the helmet radio alive; otherwise their common AUX3 short loses radio/status but cannot directly short CORE5. Confirm the AUX fault limit and all-on face load do not trigger the bank/input protection together. An input cable or star-ground fault remains shared.

USB programming rule: detach external supply from the MCU being programmed until its power isolation is qualified. Teensy VIN/VUSB separation is mandatory if both are connected. DevKitM-1 accepts only the qualified single external/USB supply arrangement; never parallel its onboard regulator output and an external 3.3 V converter blindly. UART isolation below prevents an unpowered partner being fed from TX.

## Sheet 2 — core signals and quiet sensors

| From | Conditioning / connection | To |
| --- | --- | --- |
| Teensy 11 MOSI | Source-series resistor initially 33–100 Ω, 74AHCT125 channel A at 5 V | Matrix DIN |
| Teensy 13 SCK | Same buffer, source series/resolved by scope | Matrix CLK |
| Teensy 8 LOAD | Same buffer | Matrix LOAD/CS |
| Last MAX7219 DOUT | Unconnected at Teensy; daisy chain only between modules | No return signal |
| Existing button | Switch to ground, INPUT_PULLUP | Teensy 2 |
| Existing RGB indicator | Preserve wiring; each LED channel has a resistor/current driver sized for peak GPIO limit | Teensy 5 / 9 / 10 |
| Core 3.3 V | Protected quiet branch, local 100 nF and module decoupling | IR / Hall / microphone / hardware gates |
| IR analog OUT | 1 kΩ series, about 10 nF input filter, 47 kΩ pull-up to core 3.3 V; calibrate with it fitted | Teensy A7 / 21 ADC1 |
| MAX4466 OUT | 1 kΩ series, small HF shunt capacitor chosen without suppressing voice band; 100 kΩ midrail bias if unplugged | Teensy A0 / 14 ADC0 |
| DRV5032FC OUT | Open drain, external 10 kΩ to core 3.3 V; 1 kΩ / 10 nF filter; 100 nF sensor decoupling | Teensy 3 |
| FACE5_SW | 47 kΩ upper / 33 kΩ lower divider; midpoint 1 kΩ series + 10 nF to ground | Teensy A1 / 15 ADC1 |

5 V at the dock divider gives approximately 2.0625 V, or ADC code 639–640 with 10-bit conversion and nominal 3.3 V reference. The implemented window is 595–684 (about 4.65–5.35 V). Voltage acquisition stays active during sleep/Hall transitions; samples expire after 20 ms and each power request cancels/invalidates previous voltage acquisition, so reattachment cannot use a cached reading from the previous connection. Verify resistor/reference tolerances, response during load transients and ADC accuracy. This software window is complementary to hardware OVP, not a MAX7219 absolute-maximum limit claim.

Core 3.3 V peripheral current must remain within PJRC's available external-current budget. Do not use this rail for the ESP32, matrices or the wearer display. Keep analog return away from display and RF return. For a removable IR sensor, isolate its signal on pilot-open if weak pull-ups could feed an unpowered module; a TS5A3166-class analog switch powered from core 3.3 V and controlled by hardware PILOT_PRESENT is a candidate. Its measured leakage/bias must be included in IR threshold calibration. The fixed first prototype can omit this dock-specific switch.

## Sheet 3 — isolated UART

Use [TI TXU0202](https://www.ti.com/product/TXU0202), one channel in each direction, 3.3 V on both domains. This is electrical power-domain isolation, not galvanic isolation; grounds remain common.

```text
VCCA = Teensy/core 3.3 V       VCCB = AUX3 / helmet ESP 3.3 V
Teensy TX1 pin 1 -> A1        B1 -> ESP GPIO18 RX1
Teensy RX1 pin 0 <- A2        B2 <- ESP GPIO19 TX1
OE -> enabled per TI reference circuit from its permitted domain
100 nF at each supply; receiver pull-ups to each receiver's own supply
Optional series resistors at the local outputs; common ground
```

Verify the package's actual pin numbers/A1-B1/A2-B2 directions from its datasheet when drawing a PCB. Do not substitute an auto-direction I2C level-shifter module for push-pull UART. Ioff/VCC isolation suppress signal feeding when a supply is absent. Test either MCU off or USB-only before connecting the final harness. DevKitM-1 console GPIO20/21 stay with its USB bridge. GPIO18/19 on XIAO are reserved for native USB and are **not** its controller UART pins.

## Sheet 4 — protected removable face

For the first wired prototype, use separate keyed high-current power (XT30 class) and locking low-current signals (JST-GH). Switch upstream power off before disconnecting for repairs. Optional automatic magnetic docking requires the following independently working hardware:

```text
PILOT_N: 10k to CORE3V3, short pilot contact to removable ground
           +-> Teensy pin12 (LOW = engaged)
           +-> Schmitt inverter at 3.3V -> PILOT_PRESENT

CORE_POWER_REQUEST pin6 -- external default-low pull-down
PILOT_PRESENT AND CORE_POWER_REQUEST -> eFuse ENABLE

CORE_BUFFER_REQUEST pin4 -- external default-low pull-down
PILOT_PRESENT AND CORE_BUFFER_REQUEST AND qualified FACE_POWER_GOOD
  -> 2N7002 gate (with default-low resistor)
  -> pulls the three 74AHCT125 /OE pins LOW
/OE pins also have 10k pull-up to buffer VCC=CORE5: unpowered MCU = disabled

FACE5_SW -> power contacts -> removable 5V matrix distribution and local capacitors
Heavy ground -> separate ground contacts -> removable ground distribution
Buffered SPI -> signal contacts -> MAX7219 chain
Protected CORE3V3 + IR return -> low-current sensor contacts
```

Use logic whose unpowered outputs have defined behavior and compatible inputs; e.g. 3.3 V SN74LVC Schmitt/AND gates. Their outputs must not be tied to 5 V /OE directly: the MOSFET sinks a 5 V pull-up while keeping MCU/gate nodes at 3.3 V. Power the AHCT buffer from CORE5 so its inputs remain powered whenever the Teensy drives them; do not depend on an ordinary AHCT125 to isolate an unpowered display-side supply. AHCT inputs accept Teensy's 3.3 V levels under its TTL thresholds, while outputs supply the MAX7219's guaranteed 5 V logic levels. Hardware FACE_POWER_GOOD keeps outputs disabled when the matrices are unpowered. Tie unused AHCT inputs to defined values and unused /OE high. Datasheet connections and decoupling are mandatory.

Hardware must open the signal outputs and display supply on pilot loss even if firmware is stopped. The pilot engages **after** main contacts settle and disengages **before** power/signal contacts can break. Software adds a 20 ms contact-settle wait and at least 50 ms ramp/voltage qualification. On reattach it performs MAX7219 initialization in shutdown, uploads the complete frame, then enables the display. Check power-good timing; a slowly ramping rail may fail the first attempt and be retried after 500 ms rather than displayed half initialized.

A 14-contact candidate arrangement is listed in [hardware/connector-map.csv](../hardware/connector-map.csv). It is a topology, not a verified pin assignment for a purchased connector. Contact current sharing, partial engagement, magnet alignment and pilot timing must be proven. Add removable-side discharge selected from measured C and `t = RC ln(Vstart/Vtarget)`; a discharge circuit left on the stationary half cannot discharge a detached capacitor bank. A short first-break pilot is a mechanical design requirement, not a property of generic equal-height pogo pins. Use polarity keying/recesses so incorrect magnetic mating cannot apply +5 V to SPI/IR pins; input reverse protection alone does not solve rotated signal alignment.

## Sheet 5 — status peripherals and paw

Helmet SHT40 #4885 and INA228 #5832 connect to AUX3/GND/SDA GPIO0/SCL GPIO1, addresses 0x44/0x40. Pull the bus to **3.3 V**, account for all breakout pull-ups in parallel, keep the bus short and start at 100 kHz. Each I2C transfer times out at 10 ms, with backoff after failures; the status task runs separately from radio work.

OLED #938 must be configured for four-wire SPI and 128×64 SSD1306 operation:

| OLED pin | Helmet ESP connection |
| --- | --- |
| VIN / GND | Qualified AUX3 / ground |
| CLK | GPIO4 |
| DATA/DIN | GPIO6 |
| CS | GPIO7 |
| DC | GPIO5 |
| RST | GPIO10 |

OLED uses its own ESP32 SPI peripheral, 1 MHz, DMA and 5 Hz refresh. Fit/focus is a separate physical gate. Do not silently replace it with SH1106 hardware.

Optional bank-case NTC: AUX3 -> 15 kΩ -> ADC GPIO3 -> 10 kΩ B3950 NTC -> ground; 1 kΩ ADC series and 10–100 nF filter. Leads are low-voltage sense leads only. Firmware uses calibrated millivolts and rejects open/short/out-of-range values; it does not shut the face off. A 10 kΩ pull-up instead of 15 kΩ would require a matching conversion change.

Paw XIAO: GPIO4/D2, GPIO5/D3 and GPIO10/D10 buttons each switch to ground, with internal pull-ups plus optional external pull-ups for long/noisy leads. Only add an optional cell-voltage divider after enabling that feature: cell+ -> 100 kΩ -> GPIO3/D1 -> 100 kΩ -> ground, with filter/series protection; 4.2 V becomes 2.1 V. Do not route cell voltage directly to GPIO. BAT pads get a protected cell, rated positive-lead fuse/switch and verified polarity. Its USB charger current must match the cell; no simultaneous external charger/onboard charger without reviewed isolation.

## Current release gates

Firmware feature flags are independent: IR can be implemented on the existing working face before any removable connector, status screen or C3 hardware exists. Validate each interface on a current-limited bench, scope rail and signal behavior under worst load, inspect temperature/contact hot spots, and only then enable its flag on the physical helmet. This package does not claim CAD ERC, PCB routing approval, hot-plug safety, final converter temperature, four-hour runtime or on-body RF results.

## Mandatory v2 wireless pairing indicators

Each ESP32-C3 needs a visible, single-color LED for wireless pairing. The existing
helmet DevKit RGB LED is addressable and is **not** treated as a plain GPIO LED;
the XIAO paw has no suitable shared ordinary indicator. Firmware assigns GPIO2
on each board to a new buffered indicator. GPIO2 is a boot-strapping pin: do not
connect an LED/resistor that loads it directly or change the board's strap bias.
Use a high-input-impedance, 3.3 V logic buffer/transistor driver whose input leaves
the strap level valid during power-on/reset; LED current/resistor belong on the
buffer output. Verify strap levels and bootloader/normal boot on the exact board.
Do not connect a 5 V LED load directly to a C3 pin. Adjust `Device::PairingLed` only
after checking board schematics and every enabled peripheral/console pin.

Both indicators are mandatory for first-use authentication; omit neither merely
because key agreement completes. Position them so the user can compare six flash
groups with both devices together. One long pulse begins each repeat; a zero
has ten short flashes. Rapid 2 Hz blinking announces the bounded pairing window.
Test visibility, leading zeros, all digits, window expiry, and mismatched-code
rejection. LED/SAS comparison is the practical wireless out-of-band confirmation;
it needs no USB connection or phone. Hold helmet BOOT only after boot.
