# Wiring — ESP32-C6 SuperMini NICU monitor

Everything on one 3.3 V rail, two sensors sharing one I2C bus, one 1-Wire probe.

> Prototype hardware. Not a certified medical device — keep it USB- or battery-powered,
> never mains-referenced near a patient, and don't use its numbers for care decisions.

---

## 1. Bill of materials

| # | Part | Notes |
|---|---|---|
| 1 | ESP32-C6 SuperMini | MakerGO or a clone. WiFi 6, but still **2.4 GHz only** |
| 1 | MAX30100 breakout (purple GY-MAX30100) | **Needs the rework in §4 before it will respond** |
| 1 | MPU-6050 breakout (GY-521) | Works as shipped |
| 1 | DS18B20 waterproof probe | 3-wire: red / black / yellow |
| 0–3 | 4.7 kΩ resistors | **You can build this with none.** See §5 — the GY-521 already carries the I2C pull-ups, and there's a software fallback for 1-Wire |
| — | Breadboard + jumpers, or perfboard | Keep I2C runs under ~20 cm |

---

## 2. Master connection table

| C6 SuperMini pin | Goes to |
|---|---|
| **3V3** | MAX30100 `VIN` · MPU-6050 `VCC` · DS18B20 red · top of every pull-up |
| **GND** | MAX30100 `GND` · MPU-6050 `GND` · DS18B20 black |
| **GPIO20** (silkscreen `SDA`) | MAX30100 `SDA` · MPU-6050 `SDA` |
| **GPIO19** (silkscreen `SCL`) | MAX30100 `SCL` · MPU-6050 `SCL` |
| **GPIO18** | DS18B20 yellow (data) |

GPIO20/19 are the board's own labelled `SDA`/`SCL`, which is what the core reports as the
default I2C pair for this variant — so the silkscreen and the firmware agree.

> **The 1-Wire pin moved.** On the WROOM build this was GPIO4. On the C6, **GPIO4 is a
> strapping pin** (JTAG source select, sampled at reset), so it is not a safe place for a
> sensor that idles high through a pull-up. GPIO18 is a plain GPIO on this chip and is broken
> out on the header. If you are rewiring from the older build, this is the one wire to move.

**Leave unconnected:** MAX30100 `INT`, `IRD`, `RD` · MPU-6050 `XDA`, `XCL`, `INT`, `AD0`

`INT` is unused because `MAX30100lib` is poll-driven — the firmware calls `pox.update()` in a
tight loop rather than waiting on an interrupt. `AD0` floating or grounded gives address
**0x68**, which is what the firmware expects; tying it high moves the part to 0x69.

Nothing gets wired to the **BOOT** button (GPIO9 on this board) — it is already on the PCB.
Hold it 3 seconds to reopen the WiFi setup page.

---

## 3. Diagram — no resistors, wire exactly this

Every connection below is a plain jumper. Nothing else gets added.

```
POWER
                       ┌──── MAX30100  VIN
     ESP32 3V3 ────────┼──── MPU-6050  VCC
                       └──── DS18B20   RED

                       ┌──── MAX30100  GND
     ESP32 GND ────────┼──── MPU-6050  GND
                       └──── DS18B20   BLACK


I2C BUS  (both sensors share these two wires)

   GPIO20 ──────┬──── MAX30100 SDA
    (SDA)       └──── MPU-6050  SDA          pull-ups live on the
                                             GY-521, already fitted
   GPIO19 ──────┬──── MAX30100 SCL           and already tied to 3V3
    (SCL)       └──── MPU-6050  SCL


1-WIRE                                       no pull-up fitted; the
                                             firmware turns on the
   GPIO18 ────────────── DS18B20 YELLOW      C6's internal one
                                             (§5b)
```

Nine jumpers total. Some probes use blue instead of yellow for data.

**The GY-521 must stay connected.** It is the only thing pulling SDA and SCL high. Unplug it
and the whole bus dies, taking the MAX30100 with it — that trips people up when they try to
test one sensor alone.

If your DS18B20 came on a small terminal adapter, it almost certainly already has a 4.7 kΩ on
it. Look for one resistor next to the 3 pins. If it's there, you have a proper pull-up and §5b
doesn't apply to you.

---

## 4. ⚠ The MAX30100 rework — do this first

On most purple GY-MAX30100 boards the SDA/SCL pull-ups are tied to the module's internal
**1.8 V** rail instead of 3.3 V. The ESP32 then never sees the sensor and an I2C scan comes up
empty. This is the single most common reason this build stalls.

**With no resistors, remove and don't replace:**

1. Find the two pull-up resistors connecting `SDA` and `SCL` to the 1.8 V rail (usually
   labelled R1/R2, right by the header — exact designators vary by board revision).
2. Remove them, or cut their traces. Hot air, or a soldering iron dragged across both ends.
3. **Add nothing.** The GY-521's own pull-ups now serve the whole shared bus.

That last step is the part that saves you: the rework exists to get the pull-ups off the 1.8 V
rail, not to add new ones. Once the MAX30100's are gone, the GY-521 supplies both lines at
3.3 V for every device on the bus.

Once its own pull-ups are removed, the MAX30100's SDA/SCL sit at 3.3 V when idle instead of
1.8 V. I could not retrieve the MAX30100's own Absolute Maximum Ratings table to quote — the
two datasheet mirrors I tried timed out or were blocked — but the
[MAX30101](https://www.analog.com/media/en/technical-documentation/data-sheets/max30101.pdf),
same family and same pin structure, rates all pins other than the supplies at **−0.3 V to
+6.0 V**. 3.3 V is comfortably inside that, which is consistent with this being the standard
published rework for these boards.

Do not skip this and hope. Run `i2c_scanner/i2c_scanner.ino` and confirm both `0x57` and
`0x68` report found before flashing the main firmware.

**If the pulse signal is weak** once it's working: the MAX30100's LED driver wants a little
more headroom than 3.3 V comfortably gives. Move only its `VIN` to the board's `5V`/`VIN` pin
and leave everything else alone. That is safe *after* the rework, because the I2C lines are
then pulled to 3.3 V by the GY-521 rather than to anything on the module.

---

## 5. Building this with no resistors

Both buses are open-drain: the chips can only pull a line *low*, nothing drives it high. With
no pull-up the line floats and the bus never works. So the pull-ups have to exist — but you
don't have to be the one who supplies them.

| Line | Where the pull-up comes from | Needs a resistor from you? |
|---|---|---|
| SDA / SCL | The GY-521 ships with 2.2 kΩ pull-ups already tied to its 3.3 V rail | **No** |
| DS18B20 data | Nothing on the bus provides one | **No** — §5b, with a caveat |

### 5b. The 1-Wire line without a resistor

This is the one genuine compromise. The firmware enables the C6's internal pull-up on
GPIO18 (`initSensors()`, `esp32_wroom_standalone.ino`), which is roughly **45 kΩ** — about
ten times weaker than 1-Wire wants.

Whether that works comes down to rise time. 45 kΩ against the ~30 pF of a short breadboard
lead recovers well inside the 15 µs sampling window, so it usually reads fine. Long or coiled
probe cable adds capacitance until it doesn't. Two rules make the difference:

- **Keep the probe lead short.** Under ~30 cm. This is the one that actually matters.
- **Red must go to 3V3.** Parasite power — red tied to GND, drawing power off the data line —
  needs a *strong* pull-up and will never work on 45 kΩ.

You will know within seconds of the first boot. Serial prints the temperature every 2 s:

| Serial shows | Meaning |
|---|---|
| A plausible room temperature | Working. Leave it alone |
| `-127` | No response at all — check wiring first, then rise time |
| Exactly `85.0` | Power-on reset value. It was asked for a conversion and never answered — this is the rise-time failure |

If you get `-127` or `85.0` and the wiring is right, the internal pull-up isn't enough on your
lead, and you need a real resistor. Anything from 2.2 kΩ to 10 kΩ works — 4.7 kΩ is convention,
not a requirement, so a scavenged 3.3 kΩ or 10 kΩ off any dead board is fine.

**A missing temperature reading is not cosmetic.** `-127` fails the plausibility check in
`takeReading()`, which clears `sensor_ok` and marks every reading abnormal — so the dashboard
fills with alerts. Get temperature working, or expect that.

When you do get resistors, fit a 4.7 kΩ from GPIO18 to 3V3. Leaving the internal pull-up
enabled alongside it is harmless — 45 kΩ across 4.7 kΩ is ~4.3 kΩ, still well in spec.

### 5c. OneWire needs a one-line patch for the C6

**The sketch will not compile until you do this.** OneWire 2.3.8 — the current release — does
not support the ESP32-C6. Its `util/OneWire_direct_gpio.h` special-cases the ESP32-C3 but not
the C6, so the C6 falls through to the "plain ESP32" branch and does:

```c
GPIO.out_w1ts = ((uint32_t)1 << pin);
```

On the classic ESP32 that register is a plain `uint32_t`. On the C6 it is a struct, so the
assignment fails to compile with `no match for 'operator='`. The C3 branch right above it
already does the correct thing (`GPIO.out_w1ts.val = ...`), and the C6 has the same
struct-based registers and the same single 32-bit GPIO bank — so the fix is just to let the
C6 use the C3 path.

In `Documents/Arduino/libraries/OneWire/util/OneWire_direct_gpio.h`, change all **five**
occurrences of:

```c
#if CONFIG_IDF_TARGET_ESP32C3
```

to:

```c
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6
```

They are at roughly lines 170, 185, 198, 211 and 237. After that the firmware compiles clean.

> **This lives outside the project folder.** Library Manager will silently overwrite it the
> next time OneWire updates, and the sketch will abruptly stop compiling with the error above.
> If that happens, redo the five edits. The durable alternative is to switch to `OneWireNg`,
> which supports the C6 natively — that costs a library swap and a small change to the include
> and constructor in the sketch, which is why it isn't the default here.

---

## 6. Pins you must not use on this board

| Range | Why |
|---|---|
| GPIO4, 5, 8, 9, 15 | Strapping pins, sampled at reset. 8 and 9 decide boot mode, so a sensor holding either at the wrong level stops the board booting. 4/5 select the JTAG source |
| GPIO12, GPIO13 | Native USB D−/D+. Using them costs you the USB serial port you read logs on |
| GPIO24–30 | SPI flash. Using them stops the board booting |
| GPIO16, GPIO17 | UART0 TX/RX — where `Serial` goes if USB CDC On Boot is off |

GPIO20, GPIO19 and GPIO18 sit clear of all of that, which is why they were chosen.

GPIO8 also drives the on-board RGB LED and GPIO15 the blue one, so both are spoken for even
setting the strapping aside.

The firmware does read **GPIO9**, but only as the on-board **BOOT** button — hold it 3 seconds
to reopen the WiFi setup page. That's the button and pull-up the board already has, so nothing
gets wired there and its strapping role at reset is untouched.

> **Coming from the WROOM-32 build?** Three things move: SDA 21→20, SCL 22→19, and 1-Wire
> 4→18. The old forbidden ranges no longer apply — GPIO6–11 are ordinary pins on the C6, and
> there are no input-only pins at all, so every GPIO here can take a pull-up.

---

## 7. Power budget

| Load | Typical |
|---|---|
| MAX30100 (IR LED at 7.6 mA, as the firmware sets it) | ~20 mA |
| MPU-6050 | ~4 mA |
| DS18B20 during conversion | ~1.5 mA |
| **Sensor total** | **~26 mA** |

Comfortably inside what the SuperMini's regulator supplies from USB. The ESP32-C6 itself is
the real consumer — WiFi TX peaks pull 200–300 mA in bursts, so use a USB port or supply that
can actually deliver ~500 mA. Brownouts during TX show up as random reboots with a
`Brownout detector was triggered` message on serial.

The SuperMini's **3V3 pin is an output** from its on-board regulator. Don't feed it from a
bench supply while USB is also connected — you would be driving two regulator outputs
together.

For battery use: the board has no LiPo connector or charger, so you need an external TP4056 or
similar. Deep sleep is much better than the old WROOM-32 dev board managed — there's no
USB-UART bridge burning 10–20 mA — but measure your own board before trusting a number, since
the LDO and the RGB LED vary by clone.

---

## 8. Bring-up order

0. Do the §4 MAX30100 rework **before wiring anything**. It is easier with the board loose,
   and an un-reworked MAX30100 clamps the bus so hard that step 2 will show you nothing at
   all and tell you nothing about the rest of your wiring.
1. Wire power and ground only. Plug in USB and confirm the board enumerates as a COM port.
2. Add the I2C pair — **both sensors together**, since the GY-521 carries the pull-ups. Flash
   `i2c_scanner/i2c_scanner.ino` with the board settings below. You want both `0x57` and
   `0x68`. The scanner measures the two lines electrically first, so it will tell you if
   they're floating rather than leaving you to guess.
3. Only once the scan is clean, apply the §5c OneWire patch, add the DS18B20 and flash
   `esp32_wroom_standalone/esp32_wroom_standalone.ino`. Nothing needs editing in that sketch —
   it will host a `NICU-Setup-XXXX` network on first boot (password `12345678`) so you can pick
   your WiFi from a phone. WiFi is the only thing you type — the Supabase details are already
   compiled into the sketch.

**Arduino IDE settings — both sketches:**

| Setting | Value |
|---|---|
| Board | `MakerGO ESP32 C6 SuperMini` (or `ESP32C6 Dev Module`) |
| USB CDC On Boot | **Enabled** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| Serial Monitor | 115200 baud |

**USB CDC On Boot is the setting that wastes an evening.** This board has no USB-UART bridge —
its USB port is the C6's native USB Serial/JTAG. Left disabled (the factory default for this
board), `Serial` goes to UART0 on GPIO16/17, which is not connected to the USB socket, so
Serial Monitor stays blank while the firmware runs perfectly. The main sketch emits a compiler
warning if you forget.

**Partition Scheme** matters because the main firmware is large — TLS, a web server, JSON and
three sensor libraries come to ~1.21 MB. The default scheme allows 1.25 MB, so it fits with 8%
to spare and the next feature you add will not. "Huge APP" moves the ceiling to 3 MB, taking
it from 92% full to 38%. Nothing here uses OTA or SPIFFS, so that space costs nothing.

If uploads fail, hold **BOOT**, tap **RST**, release **BOOT**, and upload again.

---

## 9. Symptom → cause

| What you see | Usually means |
|---|---|
| **Serial Monitor completely blank**, board seems dead | **USB CDC On Boot is Disabled** — §8. The firmware is almost certainly running fine and printing to UART0, which isn't wired to USB |
| `no match for 'operator='` on `GPIO.out_w1ts` when compiling | OneWire hasn't been patched for the C6, or a library update reverted it — §5c |
| `Sketch too big` / `text section exceeds available space` | Partition Scheme is still on Default. Switch to Huge APP — §8 |
| Scan finds nothing at all | Power or ground not common; SDA/SCL swapped; **or the GY-521 is unplugged** — it holds up the whole bus |
| `0x68` found, `0x57` missing | The MAX30100 pull-up problem — §4 |
| `0x57` found, `0x68` missing | GY-521 not powered, or `AD0` pulled high (it'd show at 0x69) |
| Addresses appear then vanish | I2C wires too long, or the GY-521's pull-ups stretched over too much bus. The firmware already forces the bus back to 100 kHz at the end of `initSensors()` (the MAX30100 library raises it to 400 kHz on its own); drop that `Wire.setClock()` to 50000 if it still glitches |
| Board won't boot after wiring | Something landed on a strapping pin (GPIO4/5/8/9/15) or the flash pins — §6 |
| Random reboots, `Brownout detector` | USB supply can't handle WiFi TX peaks |
| Temp reads −127 | Data wire on the wrong pin (it's **GPIO18** now, not GPIO4), or the internal pull-up isn't coping — §5b |
| Temp reads exactly 85.0 | Power-on reset value: a conversion was requested and never answered. Classic weak-pull-up rise-time failure — §5b |
| BOOT button does nothing | On this board BOOT is GPIO9, not GPIO0. If you changed `PORTAL_BUTTON_PIN`, change it back |
| Serial shows `POST failed (401/403)` | Not wiring — the baby code / device secret entered in the setup page don't match the `devices` row |
| Setup page reopens on every boot | Also not wiring. The config has never produced a successful POST, so it's treated as wrong — check the Supabase key and device secret |

---

## 10. Physical notes for a foot probe

- MAX30100 goes against the sole or the top of the foot, held with a soft self-adhering wrap.
  Don't put adhesive tape directly on preterm skin.
- Keep the sensor flat against skin with light, even pressure. Gaps and motion are what wreck
  the SpO2 reading, and the firmware's 3-reading debounce only partly covers for it.
- Keep the I2C leads short. Past ~20 cm, bus capacitance starts eating your rise times.
- Strain-relieve everything at the sensor end — the probe gets tugged.
