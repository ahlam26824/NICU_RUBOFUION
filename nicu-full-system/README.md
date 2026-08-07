# NICU Baby Monitoring System — Complete Package

Everything for the whole system: device firmware + web app + database schema.

```
nicu-full-system/
├── 01-device-firmware/
│   ├── WIRING.md                 <- full circuit + bring-up guide
│   ├── i2c_scanner/              <- flash this FIRST, checks both sensors
│   └── esp32_wroom_standalone/   <- MAIN firmware (ESP32-C6, WiFi setup from a phone)
│
└── 02-web-app/                   <- Nurse/Doctor/Parent dashboard (React + Supabase)
    └── supabase/
        ├── RUN_ALL.sql           <- the whole database, one paste (run this first)
        ├── seed_demo_accounts.sql <- three fixed demo logins + demo baby + device
        └── fix_existing_users.sql <- repairs accounts made before RUN_ALL.sql
```

## Architecture

One ESP32-C6 SuperMini does everything — read sensors, check thresholds, post to Supabase:

```
  ESP32-C6 SuperMini (sensors on the baby's foot)
    MAX30100  -> HR / SpO2        I2C 0x57
    MPU-6050  -> motion           I2C 0x68
    DS18B20   -> temperature      1-Wire
         |
         |  threshold check on-device, 3-reading debounce
         |
         v  HTTPS POST /rest/v1/rpc/device_push_vitals
     Supabase (Postgres + RLS + realtime)
         |
         v
     React web app  ->  nurse / doctor / parent views
```

An earlier design used a second ESP32 as a BLE-to-WiFi relay. WiFi is on the same chip that
reads the sensors, so that board was pure overhead, and those two sketches have been removed.
The single-module path above is the only supported one.

The firmware folder is still named `esp32_wroom_standalone` because Arduino requires the folder
and the `.ino` to share a name, and renaming both would break every path in these docs. The
code inside targets the ESP32-C6 SuperMini.

## Hardware

| Part | Interface | Notes |
|---|---|---|
| ESP32-C6 SuperMini | — | MakerGO or a clone. WiFi 6, but still 2.4 GHz only |
| MAX30100 | I2C 0x57 | See the pull-up warning below |
| MPU-6050 (GY-521) | I2C 0x68 | Works as-is |
| DS18B20 waterproof | 1-Wire | Terminal adapter usually includes the 4.7k pull-up |

**Wiring**

| Signal | GPIO |
|---|---|
| SDA (MAX30100 + MPU-6050 shared) | 20 (silkscreen `SDA`) |
| SCL (shared) | 19 (silkscreen `SCL`) |
| DS18B20 data | 18 |

Power all three sensors from the board's **3V3** pin, not 5V, and keep GND common.

Full circuit, resistor placement, bring-up order and a symptom→cause table:
**[`01-device-firmware/WIRING.md`](01-device-firmware/WIRING.md)**

> **Pins to keep clear on this board.** GPIO4/5/8/9/15 are strapping pins — 8 and 9 decide boot
> mode, so a sensor holding either at the wrong level stops the board booting. GPIO12/13 are the
> native USB data lines, GPIO24–30 are SPI flash, and GPIO16/17 are UART0. GPIO20/19 (I2C) and
> GPIO18 (1-Wire) avoid all of that.
>
> **Note the 1-Wire pin changed from the WROOM-32 build.** GPIO4 was fine there but is a
> strapping pin on the C6, so temperature moved to GPIO18. Coming from the old wiring, three
> wires move: SDA 21→20, SCL 22→19, DS18B20 4→18.

### ⚠ Read this before anything else: the MAX30100 pull-up problem

Most purple GY-MAX30100 breakout boards wire their SDA/SCL pull-up resistors to the module's
internal **1.8 V** rail instead of 3.3 V. The ESP32 then cannot see the sensor at all, and an
I2C scan comes up empty. **This is the single most common reason this build stalls.**

Fix (one-time rework):
1. Remove the two 4.7 kΩ pull-up resistors on the module, or cut their traces
2. Add your own 4.7 kΩ pull-ups: SDA → 3.3 V and SCL → 3.3 V

Also note the MAX30100 is a *reflectance* sensor that Maxim lists as not-recommended-for-new-
designs, while clinical neonatal SpO2 measures transmission through the foot. It is fine for a
working prototype — it is not a calibrated clinical measurement, and nothing here should be
used for actual patient care decisions.

---

## Setup Order

### Step 1 — Supabase database

1. Create a project at [supabase.com](https://supabase.com)
2. In **SQL Editor**, paste all of `02-web-app/supabase/RUN_ALL.sql` and Run

   > That's the whole database — tables, RLS policies, the device ingest function and the
   > app's RPCs — in one paste. It's idempotent, so re-running it is safe.

3. Then paste all of `02-web-app/supabase/seed_demo_accounts.sql` and Run

   > Creates three fixed logins (`nurse@nicu.com` / `doctor@nicu.com` / `parent@nicu.com`,
   > password `12345678` for all three), a demo baby, and a device row. Re-running resets
   > those passwords, so they can't drift. Prototype only — three well-known accounts on a
   > shared password must not go near real patient data.

4. From **Project Settings > API**, copy the `Project URL` and the `anon public` key

If you signed up in the app *before* running `RUN_ALL.sql`, that account has no profile row
and most pages will break. `fix_existing_users.sql` backfills it. Skip this if you're only
using the demo logins.

### Step 2 — Web app

```bash
cd 02-web-app
npm install
cp .env.example .env
# put your Supabase URL and anon key in .env
npm run dev
```

Sign in with one of the demo accounts from Step 1 — `nurse@nicu.com`, `doctor@nicu.com` or
`parent@nicu.com`, password `12345678`. Signing up a new account also works; details in
`02-web-app/README.md`.

### Step 3 — Add a baby and register the device

`seed_demo_accounts.sql` already created `baby_01`, linked all three demo accounts to it, and
registered a device with the secret `demo-device-secret-change-me`. If you ran it, you can skip
to Step 4 and use that secret in the setup page — then rotate it before this is used for real.

For a baby of your own, in Supabase **SQL Editor**:

```sql
-- 1. the baby (baby_code must match the baby code you enter in the device's setup page)
insert into public.babies (baby_code, display_name, gestational_age_weeks, birth_weight_grams, bed_number, room_number)
values ('baby_01', 'Baby of Karim', 34, 2100, 'B-3', 'NICU-1');

-- 2. the device — pick a long random secret, you will paste it into the firmware
insert into public.devices (baby_code, label, secret_hash)
values ('baby_01', 'ESP32 foot unit #1', crypt('choose-a-long-random-secret', gen_salt('bf')));
```

Then link the baby to people, via **Table Editor**:
- `baby_care_team` — nurse/doctor user id + baby id
- `baby_parents` — parent user id + baby id

Only the bcrypt hash of the secret is stored, so keep your plaintext copy — you type it into the
device's setup page in Step 6, and if you lose it, set a new one rather than trying to recover it.

### Step 4 — Verify the sensors before flashing the real firmware

Open `01-device-firmware/i2c_scanner/i2c_scanner.ino` and set **Tools** to:

| Setting | Value |
|---|---|
| Board | `MakerGO ESP32 C6 SuperMini` (or `ESP32C6 Dev Module`) |
| USB CDC On Boot | **Enabled** |

Flash, and open Serial Monitor at 115200.

> **If Serial Monitor is completely blank, it's the CDC setting, not a dead board.** The
> SuperMini has no USB-UART bridge — its USB port is the C6's native USB Serial/JTAG. With
> USB CDC On Boot disabled (the factory default for this board) `Serial` goes to UART0 on
> GPIO16/17, which isn't connected to USB, so a perfectly healthy board prints to nothing.

Confirm **both** `0x57` and `0x68` are reported found. If `0x57` is missing, do the pull-up
rework above — do not skip ahead, the main firmware cannot work without it.

### Step 5 — Flash the main firmware

1. Set **Tools**:

   | Setting | Value |
   |---|---|
   | Board | `MakerGO ESP32 C6 SuperMini` (or `ESP32C6 Dev Module`) |
   | USB CDC On Boot | **Enabled** |
   | Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |

   If the board isn't listed, add `esp32 by Espressif Systems` in **Boards Manager** first —
   C6 support needs core 3.x.

   > Partition Scheme is not optional here. The firmware is ~1.21 MB; the default scheme
   > allows 1.25 MB, so it fits with 8% to spare and anything you add overflows it. Huge APP
   > raises the ceiling to 3 MB (92% full → 38%). Nothing here uses OTA or SPIFFS.

2. Install these libraries via **Library Manager**:
   - `MAX30100lib` by OXullo Intersecans
   - `Adafruit MPU6050`, `Adafruit Unified Sensor`
   - `OneWire`, `DallasTemperature`
   - `ArduinoJson`

   > Not the SparkFun MAX3010x library — that one only drives the MAX30102/MAX30105, which
   > is different silicon with a different register map.

3. **Patch OneWire for the C6.** OneWire 2.3.8 does not compile for this chip as shipped —
   you'll get `no match for 'operator='` on `GPIO.out_w1ts`. It's a five-line fix, written out
   in [`WIRING.md` §5c](01-device-firmware/WIRING.md). Library Manager reverts it whenever
   OneWire updates, so if the sketch suddenly stops compiling later, that's why.

4. Open `01-device-firmware/esp32_wroom_standalone/esp32_wroom_standalone.ino`. **There is
   nothing to fill in** — WiFi and Supabase details are entered from a phone after flashing
   (Step 6).

   The setup network is currently **open**, no password, which keeps the prototype quick to
   join. Set `PORTAL_AP_PASSWORD` to 8+ characters before this is used anywhere real — see
   [Security model](#security-model) for what an open setup AP exposes.

5. Leave `POWER_MODE` as `POWER_MODE_USB_CONTINUOUS` for the first flash.

6. Flash and watch Serial Monitor.

   > If the upload stalls on `Connecting........`, hold **BOOT**, tap **RST**, release
   > **BOOT**, and upload again.

7. The device has no stored config yet, so it opens its setup portal — see
   [Step 6 below](#step-6--set-up-wifi-from-your-phone).

8. Once it reboots and joins your network, expect the MAX30100 to settle for ~5s, then
   `POST ok` roughly every 2 seconds. Open the web app on that baby's page and vitals
   should update live.

### Step 6 — Set up WiFi from your phone

No credentials are compiled into the firmware. On a device with nothing stored, the ESP32 hosts
its own access point and serves a config page.

1. On a phone or laptop, join the WiFi network **`NICU-Setup-XXXX`** (the last four hex digits
   are that board's MAC, so two units on a bench stay distinguishable). It's open by default —
   no password — unless you set `PORTAL_AP_PASSWORD`.
2. A "sign in to network" page should open by itself. If it doesn't, browse to
   **`http://192.168.4.1`**.
3. Pick your WiFi from the scanned list — or choose *Other / hidden* and type the name — and
   enter its password.
4. Fill in the Supabase project URL, the **anon** key, the baby code, and the device secret from
   Step 3. These are remembered, so a later WiFi-only change doesn't mean retyping them.
5. Save. The device stores everything, reboots, joins your network, and starts posting.

Serial Monitor prints the AP name, its password (or `none - open network`) and the IP too, if
you'd rather read it there.

> The C6 is 2.4 GHz only — WiFi 6 adds throughput and efficiency on that band, not the 5 GHz
> band. A 5 GHz-only SSID will never appear in the scan and will never connect.

**Reconnecting.** Credentials live in NVS, a separate flash partition, so they survive power
loss and reflashing the sketch. Every later boot rejoins the last network on its own — the
portal stays shut. The BSSID and channel are cached in RTC memory for a faster reassociation,
and that cache is keyed to the SSID, so switching networks doesn't leave the device chasing the
old router.

**Reconfiguring.** Hold the **BOOT** button (GPIO9 on this board) for 3 seconds, either at boot
or while the device is running. That reopens the portal. To erase stored credentials entirely,
flash with **Tools > Erase All Flash Before Sketch Upload** set to *Enabled*.

**When the portal does *not* open.** Only three things open it: nothing is stored, the stored
values have never once produced a successful POST, or someone held BOOT. A network that worked
before and then failed is treated as a transient fault — the device keeps reading sensors,
prints them to serial, and retries the link every 20 seconds in the background. This is
deliberate. A patient monitor that parks itself on a config page because the ward router
rebooted has stopped being a monitor.

That "has it ever worked" flag is set by the first accepted POST, which means a wrong Supabase
key or device secret reopens the portal on the next boot rather than failing silently. If nobody
connects within 10 minutes and there is a stored network, the portal gives up and retries it.

### Power modes

| Mode | Behaviour |
|---|---|
| `POWER_MODE_USB_CONTINUOUS` | WiFi stays up, a reading every 2s, no monitoring gaps. **Use this for real monitoring.** |
| `POWER_MODE_BATTERY_SLEEP` | Wake → read → post → deep sleep for 30s. Longer runtime, but nothing is measured while asleep. |

Battery mode is for demos and portability. Two honest caveats: a desaturation shorter than the
sleep interval can be missed entirely, and the saving is smaller than you'd expect because the
MAX30100 needs ~5s to converge on every wake (expect ~15–20s awake per cycle). Deep sleep is
better on the SuperMini than on the old WROOM-32 dev board, which burned 10–20 mA keeping its
USB-UART bridge alive — the C6 has no bridge to keep alive. Measure your own board rather than
trusting a figure, since the LDO and RGB LED vary between clones.

The board also has no LiPo connector or charger, so battery use means adding an external
TP4056 (or equivalent) plus a cell. If you want a battery-level reading, put the divider on
GPIO0–3 — the C6 only has ADC1, so the classic ESP32 "ADC2 is unusable with WiFi up" trap
doesn't exist here, but keep clear of GPIO4/5, which are strapping pins.

## Security model

The firmware carries only the **anon** key plus its own device secret — never the
`service_role` key. All writes go through `device_push_vitals()`, a `security definer` function
that verifies the secret, maps `baby_code` to the internal uuid, and inserts the rows.

If the device is stolen or its flash is dumped, the attacker can insert fake vitals for that
one baby and nothing else — no read access to any patient data. Rotate a compromised secret
with the `update` statement near the bottom of the device-auth section of `RUN_ALL.sql`.

That blast radius is the whole reason the design is safe to store credentials on the device at
all. NVS is **not** encrypted by default, so the WiFi password, anon key and device secret are
all readable from a dumped flash image. Treat a physically accessible unit as one whose secret is
already known, and rotate it if a device goes missing.

**The setup AP ships open, with no password — a prototype default that must be changed before
deployment.** The setup page is plain HTTP on 192.168.4.1, and it can't be HTTPS: there's no
valid certificate for a private IP, and the browser warning would break the captive-portal sheet
that makes the page appear on its own. So WPA2 on the AP is the *only* encryption over that form
— not one layer of several. While it's open, anyone in radio range can read the WiFi password and
device secret as you submit them, and can reach the page themselves to point the device at another
server or take it offline. Set `PORTAL_AP_PASSWORD` to 8+ characters to close both. The window is
provisioning only, but that's 10 minutes per portal entry and unbounded on an unconfigured device.

The WiFi password and device secret are write-only fields — never rendered back into the page.

---

## Still manual / missing

1. **Push notifications** — Firebase Cloud Messaging is not wired up. A new critical alert in
   `alerts` should push to the doctor/nurse phone.
2. **"Add Baby" UI** — still done from the Supabase dashboard.
3. **Per-baby thresholds** — thresholds are static in the firmware; they should vary by
   gestational age and O2 support.
4. **Vitals history chart** — `BabyDetail` shows current values only.
5. **One device per baby** — each ESP32 serves a single cot; a multi-baby ward needs one
   module per cot.
6. **Offline buffering** — readings taken while WiFi is down are printed to serial and then
   dropped. They should queue in RTC memory and flush when the link returns.
