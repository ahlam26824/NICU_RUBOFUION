/*
  =========================================================
  NICU Monitor — ESP32-C6 SuperMini standalone (single module)
  =========================================================
  One ESP32-C6 does everything itself:

      read sensors -> check thresholds -> POST straight to Supabase

  There is no BLE gateway and no second board. The C6 has WiFi on the
  same chip that reads the sensors, so a relay would only add an extra
  board, an extra power outlet and an extra pairing failure mode.

  ---------------------------------------------------------
  HARDWARE
  ---------------------------------------------------------
    ESP32-C6 SuperMini dev board
    MAX30100   HR / SpO2      I2C 0x57
    MPU-6050   motion         I2C 0x68   (GY-521 board)
    DS18B20    temperature    1-Wire     (waterproof probe)

  Wiring:
    SDA  -> GPIO20    shared by MAX30100 + MPU-6050
    SCL  -> GPIO19    shared
    DS18B20 data -> GPIO18

    GPIO20/19 are the pins silkscreened SDA/SCL on this board, and GPIO18
    is the plain GPIO next to them on the same header edge, so all three
    sensor wires land together.

    This build needs no resistors of its own. The GY-521 ships with 2.2k
    pull-ups already tied to 3.3V and they serve the whole I2C bus, and
    the DS18B20 line runs on the C6's internal pull-up (see
    initSensors()). Full reasoning and the limits of that: WIRING.md.

    Power all three sensors from the board's 3V3 pin, not 5V. GND must be
    common. Note the SuperMini's 3V3 pin is an OUTPUT from its regulator -
    do not feed it from a bench supply while USB is also plugged in.

  >>> BOARD / IDE SETTINGS - THE ONE THAT CATCHES EVERYONE <<<
    Tools > Board            : "MakerGO ESP32 C6 SuperMini"
                               (or "ESP32C6 Dev Module" - same chip)
    Tools > USB CDC On Boot  : *Enabled*
    Tools > Partition Scheme : "Huge APP (3MB No OTA/1MB SPIFFS)"

    USB CDC On Boot is the one that wastes an evening. This board has no
    USB-UART bridge; its USB port is the C6's native USB Serial/JTAG.
    With CDC On Boot disabled - the factory default for this board -
    Serial goes to UART0 on GPIO16/17, which is not connected to the USB
    port, and Serial Monitor stays blank forever while the firmware runs
    perfectly. There is a #warning below the includes if you forget.

    Partition Scheme matters because this sketch is big: TLS, a web
    server, JSON and three sensor libraries land at ~1.21MB. The default
    scheme allows 1.25MB, so it fits with 8% to spare and the next thing
    you add will not. "Huge APP" moves the ceiling to 3MB (measured: 92%
    -> 38%). Nothing here uses OTA or SPIFFS, so that space is free.

  >>> ONE LIBRARY NEEDS A PATCH FOR THE C6 <<<
    OneWire 2.3.8 does not compile for the ESP32-C6 as shipped. Its
    direct-GPIO header special-cases the C3 but not the C6, so the C6
    falls into the "plain ESP32" branch and assigns a uint32_t straight
    into a register that is a struct on this chip. Five one-line edits in
    libraries/OneWire/util/OneWire_direct_gpio.h fix it - see WIRING.md
    section 5c for the exact change. It is already applied on this
    machine, but Library Manager will silently revert it on the next
    OneWire update.

  >>> PINS YOU MUST NOT USE ON THIS BOARD <<<
    GPIO4, 5, 8, 9, 15 are strapping pins. 8 and 9 decide boot mode, so
    a sensor holding either one at the wrong level at reset stops the
    board booting. (GPIO4 was the 1-Wire pin on the old WROOM-32 build -
    on the C6 it is a JTAG-select strap, which is why 1-Wire moved.)
    GPIO12, GPIO13 are the native USB D-/D+ lines. Using them costs you
    the USB serial port you are reading the logs on.
    GPIO24-30 are the SPI flash. Touching them bricks the boot.
    GPIO8 also drives the on-board RGB LED, GPIO15 the blue one.
    GPIO16/17 are UART0 TX/RX - free if you use USB CDC, but that is
    where serial goes when CDC On Boot is off, so leave them alone.

  >>> READ THIS IF THE MAX30100 DOES NOT RESPOND <<<
    Most purple GY-MAX30100 breakouts wire their SDA/SCL pull-ups to the
    board's internal 1.8V rail instead of 3.3V, so the ESP32 never sees
    the sensor. Remove the two pull-ups on the module, or cut their
    traces, and add nothing back - the point of the rework is getting
    them off the 1.8V rail, and the GY-521's already hold both lines.
    Run i2c_scanner.ino first and confirm 0x57 and 0x68 both appear
    before you flash this sketch.

  ---------------------------------------------------------
  REQUIRED LIBRARIES  (Arduino IDE > Library Manager)
  ---------------------------------------------------------
    MAX30100lib          by OXullo Intersecans   <-- for MAX30100
    Adafruit MPU6050
    Adafruit Unified Sensor
    OneWire
    DallasTemperature
    ArduinoJson

    NOTE: this is NOT the SparkFun MAX3010x library. That one only
    supports the MAX30102/MAX30105 — different silicon and a different
    register map. It will not drive a MAX30100.

  Also patch OneWire for the C6 before compiling - 2.3.8 does not build
  for this chip as shipped. Five one-line edits, written out in
  WIRING.md section 5c. Symptom if you skip it:
    error: no match for 'operator=' ... GPIO.out_w1ts

  Board: Tools > Board > ESP32 Arduino > "MakerGO ESP32 C6 SuperMini"
                                        (or "ESP32C6 Dev Module")
    USB CDC On Boot  : ENABLED        <- see below, this one matters
    Partition Scheme : Huge APP (3MB No OTA/1MB SPIFFS)
    Port             : the board's native USB port

  USB CDC On Boot must be Enabled. The SuperMini has no USB-UART bridge -
  its USB port is the C6's own USB Serial/JTAG peripheral. With CDC off
  (the factory default for this board) Serial goes to UART0 on GPIO16/17,
  which is not wired to the USB socket, so Serial Monitor stays blank
  while the firmware runs perfectly. This file emits a compiler warning
  if you forget.

  Partition Scheme must be Huge APP. This firmware is ~1.21MB and the
  default scheme allows 1.25MB - it fits with 8% spare, and the next
  thing you add will not. Huge APP moves the ceiling to 3MB. Nothing
  here uses OTA or SPIFFS, so that space is free.

  If upload fails with "Failed to connect": hold BOOT, tap RST, release
  BOOT, then upload again.

  ---------------------------------------------------------
  SETUP — nothing to edit in this file
  ---------------------------------------------------------
  Flash as-is. On first boot the device has no stored config, so it hosts
  its own access point:

    1. Join WiFi  "NICU-Setup-XXXX"   password: 12345678
    2. A sign-in page opens by itself. If it doesn't, browse to
       http://192.168.4.1
    3. Pick your WiFi from the list, type its password, tap Connect.
    4. The device reboots, joins your network and starts posting.

  WiFi is the only thing you type. The Supabase URL, anon key, baby code
  and device secret are compiled in below, and the phone form keeps them
  folded away under "Advanced" - you only open that to point a board at a
  different project or a different baby.

  Credentials live in NVS, so they survive power loss and reflashing. Every
  later boot reconnects to that network on its own — the portal stays shut.

  To reconfigure: hold BOOT for 3 seconds, at boot or while it's running.

  The setup AP uses WPA2 with the password 12345678 - fine for a bench,
  guessed first anywhere else. Change PORTAL_AP_PASSWORD before deploying:
  the setup form is plain HTTP, so WPA2 on that AP is the only thing
  encrypting the WiFi password you type into it. NVS is not encrypted
  either, and the device secret is now compiled into this file as well, so
  treat a physically accessible device - or a shared copy of this sketch -
  as one whose secret is readable. That secret only permits inserting
  vitals for its own baby, never reading.
  =========================================================
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

#include "MAX30100_PulseOximeter.h"
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// =========================================================
// Build-time board checks
// =========================================================
// Fail loudly rather than at 2am with a blank Serial Monitor.
#if !defined(CONFIG_IDF_TARGET_ESP32C6)
#warning "This sketch is pinned for the ESP32-C6 SuperMini (SDA=20, SCL=19, 1-Wire=18, BOOT=9). Select an ESP32-C6 board, or change the pin defines to match yours."
#endif

// This board has no USB-UART bridge. With CDC On Boot disabled, Serial is
// UART0 on GPIO16/17 - pins that go nowhere near the USB socket - so the
// firmware runs correctly and prints to an audience of nobody.
#if defined(CONFIG_IDF_TARGET_ESP32C6) && defined(ARDUINO_USB_CDC_ON_BOOT) && !ARDUINO_USB_CDC_ON_BOOT
#warning "USB CDC On Boot is Disabled - Serial Monitor will stay blank on a SuperMini. Set Tools > USB CDC On Boot > Enabled."
#endif

// =========================================================
// POWER MODE  — pick one
// =========================================================
//   USB_CONTINUOUS : WiFi stays up, a reading every SEND_INTERVAL_MS.
//                    No gaps in monitoring. Use this on the ward.
//
//   BATTERY_SLEEP  : wake -> settle -> read -> post -> deep sleep.
//                    Longer runtime, but nothing is measured while
//                    asleep. A desaturation shorter than SLEEP_SECONDS
//                    can be missed entirely, so this is for demos and
//                    portability, not for real monitoring.
//
//                    The MAX30100 still needs ~5s of continuous sampling
//                    to converge on every wake, and READINGS_PER_WAKE
//                    stacks on top, so expect ~15-20s awake per cycle.
//                    That awake time is the floor on the saving and it
//                    does not shrink on a C6.
//
//                    The C6 SuperMini does sleep much better than the old
//                    WROOM-32 dev board did: no USB-UART bridge to keep
//                    powered, so deep sleep is in the tens-of-uA range
//                    rather than the 10-20mA a DevKitC burns. Measure your
//                    own board before trusting any number - the LDO and
//                    the RGB LED both vary by clone.
// =========================================================
#define POWER_MODE_USB_CONTINUOUS  1
#define POWER_MODE_BATTERY_SLEEP   2

#define POWER_MODE  POWER_MODE_USB_CONTINUOUS

// =========================================================
// CONFIG — defaults only
// =========================================================
// You do NOT have to edit these. On first boot the device hosts its own
// WiFi access point and serves a setup page that asks for your WiFi and
// nothing else - the four values below are prefilled into it. Whatever the
// page submits is stored in NVS flash and survives both power loss and
// reflashing this sketch.
//
// Values here are used only as the initial contents of that form, so
// filling them in is optional convenience for flashing a batch of
// identical units. Anything entered through the portal wins.
// =========================================================
// There are deliberately no WiFi defines here. WiFi is the one thing that
// differs per site and the portal scans for it anyway, so configLoad()
// falls back to an empty SSID - which is exactly what makes cfg.usable()
// false on a virgin device and opens the setup portal.

// Project URL and anon key are prefilled so the phone form only needs the
// WiFi details. The anon key is meant to be public - it carries no
// privileges of its own, and every device write still goes through
// device_push_vitals() with the device secret. Never put a service_role
// key here; that one would grant full read access to patient data.
#define DEFAULT_SUPABASE_URL   "https://carvrdhygmjoxtlghxrx.supabase.co"
#define DEFAULT_SUPABASE_KEY   "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImNhcnZyZGh5Z21qb3h0bGdoeHJ4Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODYxMTcyMDQsImV4cCI6MjEwMTY5MzIwNH0.xmJ6eChinC_UHC1O60hMnjlC7uqnE2sK7Zc2uyfMwRA"

// Must match a row in public.devices. seed_demo_accounts.sql registers
// exactly this pair, so a freshly seeded project works with no typing.
// Rotate the secret before this leaves a bench - see RUN_ALL.sql.
#define DEFAULT_BABY_CODE      "baby_01"
#define DEFAULT_DEVICE_SECRET  "demo-device-secret-change-me"

// =========================================================
// SETUP PORTAL
// =========================================================
// Setup AP password. Empty string = open network, no password to type.
//
// Open is fine on a bench. It is not fine on a ward: the setup form sends
// your WiFi password over plain HTTP, so WPA2 on this AP is the only thing
// encrypting it. Put a password back before this goes near a patient.
// Must be either "" or 8+ characters - WPA2 has no middle ground.
#define PORTAL_AP_PASSWORD    "12345678"
// BOOT button. GPIO9 on the C6, not GPIO0 as on the WROOM-32 - the C6's
// boot strap moved, and the SuperMini wires its button to 9 accordingly.
#define PORTAL_BUTTON_PIN     9
#define PORTAL_BUTTON_HOLD_MS 3000          // hold this long to force setup mode
#define PORTAL_TIMEOUT_MS     600000UL      // 10 min unattended -> retry stored network

#define NVS_NAMESPACE         "nicu"
static const byte DNS_PORT = 53;

// ---------- Pins (ESP32-C6 SuperMini) ----------
// SDA/SCL match the board's own silkscreen, so Wire.begin() below is only
// restating the variant default. GPIO18 is a plain GPIO with no strapping
// or USB role, sitting next to them on the header.
#define I2C_SDA        20
#define I2C_SCL        19
#define ONE_WIRE_BUS   18

// ---------- Timing ----------
const unsigned long SEND_INTERVAL_MS  = 2000;   // USB mode: gap between posts
const unsigned long SPO2_SETTLE_MS    = 5000;   // MAX30100 convergence time
const unsigned long BEAT_TIMEOUT_MS   = 5000;   // no beat this long => probe off
const unsigned long WIFI_TIMEOUT_MS   = 15000;
const unsigned long RECONNECT_INTERVAL_MS = 20000;  // backoff between WiFi retries
                                                    // once we're already running

// ---------- Battery mode ----------
const uint32_t SLEEP_SECONDS      = 30;
const int      READINGS_PER_WAKE  = 3;   // take the debounce readings inside one
                                          // wake, so an alert still fires in seconds
                                          // rather than 3 * SLEEP_SECONDS later

// =========================================================
// NICU THRESHOLDS
// =========================================================
// Room-air, late-preterm baseline. In a real deployment these should be
// per-baby (gestational age, O2 support), not one static set.
struct Thresholds {
  int   hr_min        = 100;    // bpm
  int   hr_max        = 190;
  int   spo2_min      = 90;     // %
  int   spo2_critical = 85;     // below this is a cyanosis risk
  float temp_min      = 36.5;   // C
  float temp_max      = 37.5;
};
Thresholds th;

const int ALERT_CONFIRM_COUNT  = 3;  // consecutive abnormal readings before alerting
const int SENSOR_FAULT_CONFIRM = 3;  // consecutive bad readings before a sensor alert

// =========================================================
// State that must survive deep sleep
// =========================================================
// In battery mode the chip restarts from setup() on every wake, so these
// counters would reset every cycle and the debounce would never trigger.
// RTC memory keeps them across sleeps.
RTC_DATA_ATTR int      rtcOutOfRangeCount  = 0;
RTC_DATA_ATTR int      rtcSensorFaultCount = 0;
RTC_DATA_ATTR uint32_t rtcBootCount        = 0;

// Cached AP details — lets WiFi skip the scan and reassociate in a few
// hundred ms instead of 1-2s, which matters when you do it every wake.
RTC_DATA_ATTR uint8_t  rtcBssid[6];
RTC_DATA_ATTR uint8_t  rtcChannel   = 0;
RTC_DATA_ATTR bool     rtcWifiValid = false;

// Hash of the SSID those cached details belong to. Without this, changing
// networks through the portal would leave us fast-reconnecting to the old
// AP's BSSID forever.
RTC_DATA_ATTR uint32_t rtcSsidHash  = 0;

// =========================================================
// Types
// =========================================================
// These must appear before the first function definition in the sketch.
// The Arduino preprocessor generates prototypes for every function and
// inserts them all immediately above the first one it finds — so a struct
// declared later than that point is unknown to the prototype of any
// function returning or taking it ("'Reading' does not name a type").
struct Reading {
  int    hr;         // -1 when invalid
  int    spo2;       // -1 when invalid
  float  tempC;      // -127 when invalid
  float  motion;
  bool   sensorOk;
  String faultReason;
};

struct Evaluation {
  bool        isAbnormal;
  bool        raiseAlert;
  String      reason;
  const char* severity;   // "critical" | "warning"
};

// =========================================================
// Stored configuration
// =========================================================
struct DeviceConfig {
  String wifiSsid;
  String wifiPass;
  String supabaseUrl;
  String supabaseKey;
  String babyCode;
  String deviceSecret;

  // True once these values have actually reached Supabase. Until then the
  // portal may reopen on boot; afterwards a failure is treated as a
  // transient network fault instead of a bad configuration.
  bool   proven = false;

  bool hasWifi() const { return wifiSsid.length() > 0; }

  bool usable() const {
    return wifiSsid.length()     > 0 &&
           supabaseUrl.length()  > 0 && supabaseUrl.indexOf("your-project-ref") < 0 &&
           supabaseKey.length()  > 0 &&
           babyCode.length()     > 0 &&
           deviceSecret.length() > 0;
  }
};

DeviceConfig cfg;

// djb2 — only needs to detect "the SSID changed", not resist collisions.
uint32_t ssidHash(const String& s) {
  uint32_t h = 5381;
  for (unsigned i = 0; i < s.length(); i++) h = ((h << 5) + h) + (uint8_t)s[i];
  return h;
}

void configLoad(DeviceConfig& c) {
  Preferences p;
  p.begin(NVS_NAMESPACE, true);              // read-only
  c.wifiSsid     = p.getString("ssid",   "");
  c.wifiPass     = p.getString("pass",   "");
  c.supabaseUrl  = p.getString("sburl",  DEFAULT_SUPABASE_URL);
  c.supabaseKey  = p.getString("sbkey",  DEFAULT_SUPABASE_KEY);
  c.babyCode     = p.getString("baby",   DEFAULT_BABY_CODE);
  c.deviceSecret = p.getString("secret", DEFAULT_DEVICE_SECRET);
  c.proven       = p.getBool  ("proven", false);
  p.end();
}

void configSave(const DeviceConfig& c) {
  Preferences p;
  p.begin(NVS_NAMESPACE, false);             // read-write
  p.putString("ssid",   c.wifiSsid);
  p.putString("pass",   c.wifiPass);
  p.putString("sburl",  c.supabaseUrl);
  p.putString("sbkey",  c.supabaseKey);
  p.putString("baby",   c.babyCode);
  p.putString("secret", c.deviceSecret);
  p.putBool  ("proven", c.proven);
  p.end();
}

// Called after the first successful POST, so a slow AP on the next boot
// does not drop a working device back into setup mode.
void configMarkProven() {
  if (cfg.proven) return;
  cfg.proven = true;
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putBool("proven", true);
  p.end();
  Serial.println("Config confirmed working, stored.");
}

// =========================================================
// Objects
// =========================================================
PulseOximeter      pox;
Adafruit_MPU6050   mpu;
OneWire            oneWire(ONE_WIRE_BUS);
DallasTemperature  tempSensor(&oneWire);

bool mpuReady  = false;
bool poxReady  = false;

// How many DS18B20s the 1-Wire search found. Zero means every temperature
// read would return -127, so takeReading() retries the search instead of
// reporting a fault forever - a probe seated after boot, or one that
// dropped off the weak internal pull-up, gets picked back up.
int  tempProbeCount = 0;

// Acceleration magnitude with the probe at rest, i.e. gravity. Subtracted
// from the measured magnitude so "motion" reads ~0 on a still baby instead
// of a constant 9.81.
const float GRAVITY_MS2 = 9.80665f;

unsigned long lastBeatMs      = 0;
unsigned long lastSendMs      = 0;
unsigned long lastReconnectMs = 0;

// Consecutive rejected posts while cfg.proven is still false. A config that
// has never once worked and keeps being refused is a wrong baby code or
// device secret, which only the portal can fix. Counted only in the
// unproven state: once proven, failures are network faults and must never
// pull a working monitor off the ward.
int unprovenPostFailures = 0;
const int UNPROVEN_POST_FAILURE_LIMIT = 3;

// =========================================================
// MAX30100 beat callback
// =========================================================
// Called from inside pox.update(), not from an ISR. A recent beat is our
// best signal that the probe is actually on skin.
void onBeatDetected() {
  lastBeatMs = millis();
}

// =========================================================
// Sensors
// =========================================================
void initSensors() {
  Wire.begin(I2C_SDA, I2C_SCL);

  // ---- MAX30100 ----
  if (!pox.begin()) {
    Serial.println("MAX30100 not found. Run i2c_scanner.ino - most likely");
    Serial.println("the 1.8V pull-up problem described at the top of this file.");
    poxReady = false;
  } else {
    pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
    pox.setOnBeatDetectedCallback(onBeatDetected);
    poxReady = true;
    Serial.println("MAX30100 ready.");
  }

  // ---- MPU-6050 ----
  if (!mpu.begin()) {
    Serial.println("MPU-6050 not found. Check wiring.");
    mpuReady = false;
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    mpu.setGyroRange(MPU6050_RANGE_250_DEG);
    mpuReady = true;
    Serial.println("MPU-6050 ready.");
  }

  // ---- DS18B20 ----
  // Enable the ESP32's internal pull-up on the 1-Wire line. This is a
  // fallback for a bench build with no 4.7k to hand: the internal one is
  // ~45k, which is far too weak by spec, but on a short lead with the
  // probe externally powered (red to 3V3, NOT parasite mode) the rise time
  // usually still clears the 15us read window. A real 4.7k is the correct
  // build - see WIRING.md section 5.
  //
  // Harmless when a proper pull-up IS fitted: 45k across 4.7k is ~4.3k.
  // Safe to set here because OneWire's ESP32 path releases the line via
  // the output-enable register and leaves the IO_MUX pull-up bit alone,
  // so this survives every subsequent bus transaction.
  pinMode(ONE_WIRE_BUS, INPUT_PULLUP);

  // Async conversion: requestTemperatures() returns immediately instead of
  // blocking ~750ms. That matters because any blocking call starves
  // pox.update() and the SpO2 reading falls apart.
  tempSensor.begin();
  tempSensor.setWaitForConversion(false);
  tempProbeCount = tempSensor.getDeviceCount();

  if (tempProbeCount > 0) {
    tempSensor.requestTemperatures();
    Serial.printf("DS18B20 ready (%d probe%s).\n",
                  tempProbeCount, tempProbeCount == 1 ? "" : "s");
  } else {
    Serial.printf("DS18B20 not found on GPIO%d. Check the data wire, and that\n",
                  ONE_WIRE_BUS);
    Serial.println("red goes to 3V3 - parasite power cannot work on the weak");
    Serial.println("internal pull-up. takeReading() will keep retrying.");
  }

  // Put the I2C bus back to 100kHz, LAST, after both sensor libraries have
  // had their turn. MAX30100::begin() calls Wire.setClock(400000) itself,
  // so without this the bus ends up in fast mode no matter what we asked
  // for. 400kHz allows only 300ns of rise time; the GY-521's 2.2k pull-ups
  // driving 20cm of jumper wire are close enough to that to produce the
  // "addresses appear then vanish" fault in WIRING.md section 9. Nothing
  // here needs the bandwidth - the MAX30100 samples at 100Hz, a few bytes
  // at a time - so standard mode is free robustness.
  Wire.setClock(100000);
}

// Spin pox.update() for a fixed window, doing nothing else.
// The MAX30100 library is poll-driven and needs continuous, uninterrupted
// calls before heart rate and SpO2 converge.
void settlePulseOximeter(unsigned long durationMs) {
  unsigned long start = millis();
  while (millis() - start < durationMs) {
    pox.update();
  }
}

Reading takeReading() {
  Reading r;

  // ---- HR / SpO2 ----
  float   hrFloat = poxReady ? pox.getHeartRate() : 0.0f;
  uint8_t spo2Raw = poxReady ? pox.getSpO2()      : 0;

  bool beatRecent    = poxReady && lastBeatMs != 0 &&
                       (millis() - lastBeatMs) < BEAT_TIMEOUT_MS;
  bool hrPlausible   = hrFloat >= 30.0f && hrFloat <= 250.0f;
  bool spo2Plausible = spo2Raw >= 70 && spo2Raw <= 100;

  r.hr   = hrPlausible   ? (int)(hrFloat + 0.5f) : -1;
  r.spo2 = spo2Plausible ? (int)spo2Raw          : -1;

  // ---- Temperature ----
  // The conversion was kicked off at least one cycle ago, so it is ready.
  //
  // The window rejects both failure values the DS18B20 produces: -127 when
  // it does not answer at all, and exactly 85.0 - the power-on register
  // contents, returned when a conversion was requested but never completed.
  // 85 is above the 60C ceiling, so it is caught here rather than being
  // posted as a plausible-looking fever.
  float t = tempProbeCount > 0 ? tempSensor.getTempCByIndex(0) : -127.0f;
  bool tempValid = (t > -50.0f && t < 60.0f);
  r.tempC = tempValid ? t : -127.0f;

  if (tempValid) {
    tempSensor.requestTemperatures();   // start the next one now
  } else {
    // Re-run the 1-Wire search before giving up on the probe. begin() is
    // cheap and this is the only path back for a probe seated after boot.
    tempSensor.begin();
    tempProbeCount = tempSensor.getDeviceCount();
    if (tempProbeCount > 0) tempSensor.requestTemperatures();
  }

  // ---- Motion ----
  // The accelerometer measures gravity too, so a perfectly still foot reads
  // 9.81 m/s2, not 0. Reporting that raw put a constant 9.8 in the
  // dashboard's "Motion" field and buried the thing it is meant to show.
  // Subtracting 1g leaves the deviation from rest: ~0 when still, rising
  // with movement in either direction.
  if (mpuReady) {
    sensors_event_t a, g, tmp;
    mpu.getEvent(&a, &g, &tmp);
    float mag = sqrtf(a.acceleration.x * a.acceleration.x +
                      a.acceleration.y * a.acceleration.y +
                      a.acceleration.z * a.acceleration.z);
    r.motion = fabsf(mag - GRAVITY_MS2);
  } else {
    r.motion = 0.0f;
  }

  // ---- Is this reading trustworthy? ----
  // MAX30100lib has no validHeartRate / validSPO2 flag, so we infer it:
  // a pulse was detected recently AND both numbers are physiologically
  // plausible.
  r.sensorOk = beatRecent && hrPlausible && spo2Plausible && tempValid;

  if (!beatRecent)            r.faultReason = "no pulse signal detected";
  else if (!hrPlausible ||
           !spo2Plausible)    r.faultReason = "implausible HR/SpO2 reading";
  else if (!tempValid)        r.faultReason = "temperature sensor not responding";
  else                        r.faultReason = "";

  return r;
}

// =========================================================
// Threshold logic
// =========================================================
Evaluation evaluateReading(const Reading& r) {
  Evaluation e;
  e.isAbnormal = false;
  e.raiseAlert = false;
  e.reason     = "";
  e.severity   = "critical";

  if (!r.sensorOk) {
    // A bad reading is NOT treated as "skip". If it were - counter reset,
    // isAbnormal false - a detached or dead probe would never alert; it
    // would look exactly like a healthy baby. A sustained sensor fault
    // raises its own alert instead.
    rtcSensorFaultCount++;
    rtcOutOfRangeCount = 0;

    if (rtcSensorFaultCount >= SENSOR_FAULT_CONFIRM) {
      e.raiseAlert = true;
      e.severity   = "warning";
      e.reason     = "Sensor signal lost - probe may be detached (" + r.faultReason + ")";
      rtcSensorFaultCount = 0;
    }
    return e;
  }

  rtcSensorFaultCount = 0;

  // ---- Normal threshold checks ----
  if (r.hr < th.hr_min || r.hr > th.hr_max) {
    e.isAbnormal = true;
    e.reason += "Heart rate abnormal (" + String(r.hr) + " bpm); ";
  }
  if (r.spo2 < th.spo2_min) {
    e.isAbnormal = true;
    e.reason += "SpO2 low (" + String(r.spo2) + "%); ";
  }
  if (r.tempC < th.temp_min || r.tempC > th.temp_max) {
    e.isAbnormal = true;
    e.reason += "Temperature abnormal (" + String(r.tempC, 1) + "C); ";
  }

  // ---- Debounce: N consecutive abnormal readings before alerting ----
  // Movement artefact on a neonate's foot is constant; without this the
  // ward would drown in false alarms.
  if (e.isAbnormal) {
    rtcOutOfRangeCount++;
  } else {
    rtcOutOfRangeCount = 0;
  }

  if (rtcOutOfRangeCount >= ALERT_CONFIRM_COUNT) {
    e.raiseAlert = true;
    e.severity   = "critical";
    if (r.spo2 < th.spo2_critical) {
      e.reason = "CRITICAL: SpO2 severely low (" + String(r.spo2) + "%)! " + e.reason;
    }
    // Reset so we don't re-fire every single reading. The 5-minute dedupe
    // in device_push_vitals() catches the rest, and lets a still-unresolved
    // condition re-alert once the window lapses.
    rtcOutOfRangeCount = 0;
  }

  return e;
}

// =========================================================
// SETUP PORTAL
// =========================================================
// On first boot the device hosts its own access point and serves a
// config page. Join it from a phone, pick your WiFi, save, done.
//
//   1. device brings up AP  "NICU-Setup-A1B2"   (open by default)
//   2. phone joins -> captive portal pops, or browse to 192.168.4.1
//   3. pick SSID from the scan, enter password (+ Supabase fields)
//   4. Save -> written to NVS -> reboot -> connects to your WiFi
//
// WHY THIS DOES NOT OPEN ON EVERY WIFI DROP:
// This is a patient monitor. If the ward AP reboots, the right behaviour
// is to keep reading sensors and retry in the background, NOT to sit in
// a config page waiting for someone to notice. The portal opens only
// when there is nothing stored, when stored values have never once
// worked, or when someone holds the BOOT button. A network that worked
// before and stopped is a transient fault, not a reconfiguration request.
//
// SECURITY: PORTAL_AP_PASSWORD is "", so this AP is open. The page is
// plain HTTP, so nothing encrypts the form - anyone in radio range can
// read the WiFi password and device secret as they are submitted, and can
// reach the page to repoint the device. Set PORTAL_AP_PASSWORD to 8+ chars
// before deploying. The WiFi password and device secret are write-only:
// never rendered back into the page. NVS is not encrypted, which is why
// the device secret only ever authorises inserting vitals for one baby
// and grants no reads.
// =========================================================

WebServer     portalServer(80);
DNSServer     portalDns;
bool          portalSaved       = false;
bool          portalRoutesReady = false;

void portalButtonBegin() {
  pinMode(PORTAL_BUTTON_PIN, INPUT_PULLUP);
}

// True if BOOT is held for PORTAL_BUTTON_HOLD_MS.
//
// Returns immediately when the button is up, which is the case on all but
// one loop() iteration in a million, so polling it from loop() costs a
// digitalRead. It only blocks while someone is actually holding it down,
// and at that point they have already decided to stop monitoring.
bool portalButtonHeld() {
  if (digitalRead(PORTAL_BUTTON_PIN) != LOW) return false;

  Serial.print("BOOT held, keep holding for setup mode");
  unsigned long start = millis();
  while (digitalRead(PORTAL_BUTTON_PIN) == LOW) {
    if (millis() - start >= PORTAL_BUTTON_HOLD_MS) {
      Serial.println("  -> setup mode");
      return true;
    }
    delay(50);
    if ((millis() - start) % 500 < 50) Serial.print(".");
  }
  Serial.println("  released too early, continuing");
  return false;
}

// AP name unique per board, so two units on a bench are distinguishable.
String portalApName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[24];
  snprintf(buf, sizeof(buf), "NICU-Setup-%02X%02X", mac[4], mac[5]);
  return String(buf);
}

String htmlEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); i++) {
    char ch = s[i];
    if      (ch == '&')  o += "&amp;";
    else if (ch == '<')  o += "&lt;";
    else if (ch == '>')  o += "&gt;";
    else if (ch == '"')  o += "&quot;";
    else if (ch == '\'') o += "&#39;";
    else                 o += ch;
  }
  return o;
}

// Phone keyboards habitually append a space after a paste, and copying the
// project URL out of the Supabase dashboard usually brings a trailing slash
// with it. Neither is visible in the form, and each one fails in a way that
// points somewhere else entirely: a stray space in the anon key reads as a
// 401, in the device secret as a 403, and in the baby code as "no active baby
// for code". All three look like a wrong value rather than a whitespace
// problem, so they cost an evening each. Strip it here instead.
String cleanArg(const String& raw) {
  String s = raw;
  s.trim();
  return s;
}

// Same, plus the trailing slash. cfg.supabaseUrl has "/rest/v1/rpc/..."
// appended to it, so one trailing slash yields a double slash in the path,
// which the API gateway does not route.
String cleanUrlArg(const String& raw) {
  String s = cleanArg(raw);
  while (s.endsWith("/")) s.remove(s.length() - 1);
  return s;
}

// Scan results are cached because the root page is not requested once.
// A phone joining the AP fires several captive-portal probes, every one of
// which onNotFound() redirects to "/", and scanNetworks() blocks for
// seconds while forcing the radio off the AP channel. Rescanning on each
// probe is what makes a portal look hung and drop the client mid-load.
String        portalScanCache;
unsigned long portalScanMs    = 0;
bool          portalScanned   = false;
const unsigned long PORTAL_SCAN_TTL_MS = 45000;

// force is spelled out at every call site on purpose. A default argument
// here would be repeated in the prototype the Arduino builder generates
// above, and C++ rejects the same default being specified twice.
String portalScanOptions(bool force) {
  if (portalScanned && !force && millis() - portalScanMs < PORTAL_SCAN_TTL_MS) {
    return portalScanCache;
  }

  int n = WiFi.scanNetworks();
  portalScanned = true;
  portalScanMs  = millis();

  if (n <= 0) {
    portalScanCache = "";
    return portalScanCache;
  }

  String out;
  for (int i = 0; i < n && i < 25; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;              // hidden network

    int rssi = WiFi.RSSI(i);
    const char* bars = rssi > -60 ? "excellent" : rssi > -70 ? "good"
                     : rssi > -80 ? "fair"      : "weak";
    bool lock = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

    out += "<option value=\"" + htmlEscape(ssid) + "\">" + htmlEscape(ssid)
         + " (" + bars + (lock ? ", locked" : ", open") + ")</option>";
  }
  WiFi.scanDelete();
  portalScanCache = out;
  return portalScanCache;
}

void portalHandleRoot() {
  String page =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>NICU Monitor setup</title><style>"
    "body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:20px;"
    "background:#0f172a;color:#e2e8f0;line-height:1.5}"
    ".w{max-width:420px;margin:0 auto}h1{font-size:22px;margin:0 0 6px}"
    "p.sub{margin:0 0 22px;color:#94a3b8;font-size:14px}"
    "label{display:block;margin:16px 0 5px;font-size:14px;color:#cbd5e1}"
    "input,select{width:100%;box-sizing:border-box;padding:12px;border-radius:10px;"
    "border:1px solid #334155;background:#1e293b;color:#e2e8f0;font-size:16px}"
    "button{width:100%;margin-top:24px;padding:15px;border:0;border-radius:10px;"
    "background:#2563eb;color:#fff;font-size:17px;font-weight:600}"
    "details{margin-top:28px;border-top:1px solid #1e293b;padding-top:12px}"
    "summary{font-size:13px;color:#64748b}"
    ".hint{font-size:12px;color:#64748b;margin-top:6px}"
    "</style></head><body><div class=w>"
    "<h1>Connect to WiFi</h1>"
    "<p class=sub>Pick your network and the monitor will do the rest.</p>"
    "<form method=POST action=/save>"
    "<label>WiFi network</label>"
    "<select name=ssid onchange=\"document.getElementById('oth').style.display="
    "this.value?'none':'block'\">"
    "<option value=''>-- Other / hidden --</option>";

  page += portalScanOptions(false);

  page +=
    "</select>"
    "<div id=oth style=display:none>"
    "<label>Network name</label><input name=ssid_other placeholder=SSID>"
    "</div>"
    "<div class=hint>2.4 GHz only - a 5 GHz network will not appear here. "
    "<a href=/rescan style=color:#60a5fa>Rescan</a></div>"
    "<label>WiFi password</label>"
    "<input name=pass type=password placeholder='leave blank if open'>"
    "<button type=submit>Connect</button>"

    // Everything below is already compiled in and correct for this project,
    // so it stays folded away. It only needs opening to point a board at a
    // different Supabase project or a different baby - which is not what
    // anyone is doing the first time they see this page.
    "<details><summary>Advanced - Supabase settings</summary>"
    "<label>Project URL</label>"
    "<input name=sburl value='" + htmlEscape(cfg.supabaseUrl) + "'>"
    "<label>Anon key</label>"
    "<input name=sbkey value='" + htmlEscape(cfg.supabaseKey) + "'>"
    "<div class=hint>The anon key, never service_role.</div>"
    "<label>Baby code</label>"
    "<input name=baby value='" + htmlEscape(cfg.babyCode) + "'>"
    "<label>Device secret</label>"
    "<input name=secret type=password placeholder='" +
    String(cfg.deviceSecret.length() ? "stored - blank keeps it"
                                     : "the secret you registered") + "'>"
    "</details>"
    "</form></div></body></html>";

  portalServer.send(200, "text/html", page);
}

void portalHandleSave() {
  String ssid = portalServer.arg("ssid");
  if (ssid.length() == 0) ssid = portalServer.arg("ssid_other");

  if (ssid.length() == 0) {
    portalServer.send(200, "text/html",
      "<meta name=viewport content='width=device-width,initial-scale=1'>"
      "<body style='font-family:system-ui;padding:24px'>"
      "<h3>Pick a network first</h3><a href=/>Back</a></body>");
    return;
  }

  cfg.wifiSsid = ssid;
  cfg.wifiPass = portalServer.arg("pass");

  // Note wifiPass above is deliberately NOT trimmed - a space is legal in a
  // WiFi password, and a wrong one fails loudly anyway (the device never
  // joins, the portal reopens, serial says so). The four below fail silently,
  // which is why they get cleaned.
  if (cleanUrlArg(portalServer.arg("sburl")).length())
    cfg.supabaseUrl = cleanUrlArg(portalServer.arg("sburl"));
  if (cleanArg(portalServer.arg("sbkey")).length())
    cfg.supabaseKey = cleanArg(portalServer.arg("sbkey"));
  if (cleanArg(portalServer.arg("baby")).length())
    cfg.babyCode    = cleanArg(portalServer.arg("baby"));

  // Blank secret means "keep the stored one", so changing WiFi does not
  // force retyping it.
  if (cleanArg(portalServer.arg("secret")).length())
    cfg.deviceSecret = cleanArg(portalServer.arg("secret"));

  cfg.proven = false;              // unproven until a POST actually succeeds
  configSave(cfg);

  portalServer.send(200, "text/html",
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<body style='font-family:system-ui,sans-serif;background:#0f172a;color:#e2e8f0;"
    "padding:28px;line-height:1.6'><h2 style='margin:0 0 8px'>Saved</h2>"
    "<p>Connecting to <b>" + htmlEscape(cfg.wifiSsid) + "</b> and restarting.</p>"
    "<p style='color:#94a3b8;font-size:14px'>This setup network will disappear - that "
    "is expected. Reconnect your phone to your normal WiFi.</p>"
    "<p style='color:#94a3b8;font-size:14px'>If the device cannot reach that network, "
    "it will reopen this page.</p></body>");

  portalSaved = true;
}

// Forces a fresh scan, then bounces back to the form. Separate route so a
// captive-portal probe redirect can never trigger a rescan by accident.
void portalHandleRescan() {
  portalScanOptions(true);
  portalServer.sendHeader("Location", "/", true);
  portalServer.send(302, "text/plain", "");
}

// Phones probe a known URL and expect an exact reply; returning a redirect
// instead is what makes the "sign in to network" sheet appear by itself.
void portalHandleNotFound() {
  portalServer.sendHeader("Location", "http://192.168.4.1/", true);
  portalServer.send(302, "text/plain", "");
}

// Blocks until saved (then reboots). Returns only on timeout, in which
// case the caller should retry whatever is stored.
void runPortal() {
  portalSaved = false;
  String apName = portalApName();

  // AP_STA rather than plain AP: the station side has to stay alive for
  // scanNetworks() to return real results while the AP is up.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName.c_str(), PORTAL_AP_PASSWORD);
  delay(200);

  IPAddress ip = WiFi.softAPIP();
  portalDns.setErrorReplyCode(DNSReplyCode::NoError);
  portalDns.start(DNS_PORT, "*", ip);          // resolve every lookup to us

  // Scan now, while nobody is connected yet. The user needs a few seconds
  // to find the AP and join it, and spending that window on the scan means
  // the first page load is already warm instead of blocking on the radio.
  Serial.println("Scanning for networks...");
  portalScanOptions(true);

  // Routes are registered once per boot. WebServer::on() appends to a list
  // rather than replacing, so re-registering after a portal timeout would
  // pile up duplicate handlers.
  if (!portalRoutesReady) {
    portalServer.on("/",       HTTP_GET,  portalHandleRoot);
    portalServer.on("/rescan", HTTP_GET,  portalHandleRescan);
    portalServer.on("/save",   HTTP_POST, portalHandleSave);
    portalServer.onNotFound(portalHandleNotFound);
    portalRoutesReady = true;
  }
  portalServer.begin();

  Serial.println();
  Serial.println("=========================================");
  Serial.println("  SETUP MODE - NOT MONITORING");
  Serial.println("=========================================");
  Serial.printf ("  join WiFi : %s\n", apName.c_str());
  if (PORTAL_AP_PASSWORD[0] == '\0') {
    Serial.println("  password  : none - open network");
  } else {
    Serial.printf ("  password  : %s\n", PORTAL_AP_PASSWORD);
  }
  Serial.printf ("  then open : http://%s\n", ip.toString().c_str());
  Serial.println("=========================================");
  Serial.println();

  unsigned long start = millis();

  while (!portalSaved) {
    portalDns.processNextRequest();
    portalServer.handleClient();

    // Nobody came, but we do have stored credentials. Stop squatting in
    // setup mode - an unattended device should end up monitoring rather
    // than sitting on a config page.
    if (cfg.hasWifi() && millis() - start > PORTAL_TIMEOUT_MS) {
      Serial.println("Portal timed out. Retrying stored network.");
      portalServer.stop();
      portalDns.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      return;
    }
    delay(2);
  }

  delay(1200);                    // let the browser receive the "Saved" page
  portalServer.stop();
  portalDns.stop();
  WiFi.softAPdisconnect(true);
  Serial.println("Restarting...");
  ESP.restart();
}

// =========================================================
// WiFi
// =========================================================
bool connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (!cfg.hasWifi()) return false;

  WiFi.mode(WIFI_STA);

  // Only trust the cached BSSID if it belongs to the SSID we now want —
  // otherwise a network change would keep chasing the old AP.
  bool cacheUsable = rtcWifiValid && rtcSsidHash == ssidHash(cfg.wifiSsid);

  if (cacheUsable) {
    // Fast path: channel and BSSID already known from a previous boot.
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str(), rtcChannel, rtcBssid);
  } else {
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  }

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(100);
  }

  // Cached details go stale (AP reboot, roaming). Retry once from scratch.
  if (WiFi.status() != WL_CONNECTED && cacheUsable) {
    Serial.println("Fast reconnect failed, falling back to a full scan.");
    rtcWifiValid = false;
    WiFi.disconnect();
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
      delay(100);
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connect failed.");
    Serial.println("  -> the C6 is 2.4GHz only; a 5GHz-only SSID will never appear");
    return false;
  }

  memcpy(rtcBssid, WiFi.BSSID(), 6);
  rtcChannel   = WiFi.channel();
  rtcSsidHash  = ssidHash(cfg.wifiSsid);
  rtcWifiValid = true;

  Serial.println("WiFi connected. IP: " + WiFi.localIP().toString());
  return true;
}

// =========================================================
// Push to Supabase
// =========================================================
// One RPC call carries the vitals and, when needed, the alert. The
// function resolves the baby_code to the babies.id uuid server-side, so
// the firmware never needs to know it.
bool pushToSupabase(const Reading& r, const Evaluation& e) {
  if (!cfg.usable()) return false;
  if (!connectWifi()) return false;

  WiFiClientSecure client;

  // Certificate validation is off so this works without pinning a CA.
  // Fine on a trusted ward network; for production put the Supabase root
  // CA in SUPABASE_ROOT_CA below and swap the two lines.
  client.setInsecure();
  // static const char* SUPABASE_ROOT_CA = "-----BEGIN CERTIFICATE-----\n...";
  // client.setCACert(SUPABASE_ROOT_CA);

  // WiFiClientSecure defaults to a 120 SECOND TLS handshake timeout. This
  // call runs from loop(), so a half-open AP or a hijacking captive portal
  // would otherwise stall the whole monitor for two minutes with no sensor
  // reads at all. Ten seconds is generous for a handshake and bounds the
  // worst case to well under one send interval's worth of missed cycles.
  client.setHandshakeTimeout(10);          // seconds

  String url = cfg.supabaseUrl + "/rest/v1/rpc/device_push_vitals";

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("http.begin() failed");
    return false;
  }

  http.setConnectTimeout(8000);            // ms
  http.setTimeout(8000);                   // ms, waiting on the response

  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", cfg.supabaseKey);
  http.addHeader("Authorization", String("Bearer ") + cfg.supabaseKey);

  // ArduinoJson 7 merged StaticJsonDocument into a self-sizing JsonDocument
  // and deprecated the old name; Library Manager installs 7.x by default, so
  // hard-coding either spelling breaks for half the people who flash this.
  // Version 6 needs a capacity: the alert reason can reach ~130 chars and
  // strings are copied into the document, so 768 leaves real headroom.
  // Undersizing it there fails silently - serializeJson() emits truncated
  // JSON and Supabase answers 400.
#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  StaticJsonDocument<768> doc;
#endif
  doc["p_baby_code"]     = cfg.babyCode;
  doc["p_device_secret"] = cfg.deviceSecret;
  doc["p_hr"]            = r.hr;
  doc["p_spo2"]          = r.spo2;
  doc["p_temp"]          = r.tempC;
  doc["p_motion"]        = r.motion;
  doc["p_sensor_ok"]     = r.sensorOk;
  doc["p_is_abnormal"]   = e.isAbnormal;

  // Omitted entirely when there's no alert — the SQL function defaults
  // both of these to null.
  if (e.raiseAlert) {
    doc["p_alert_reason"]   = e.reason;
    doc["p_alert_severity"] = e.severity;
  }

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);

  if (code == 200) {
    Serial.printf("POST ok  hr=%d spo2=%d temp=%.1f motion=%.2f sensor_ok=%d%s\n",
                  r.hr, r.spo2, r.tempC, r.motion, r.sensorOk,
                  e.raiseAlert ? "  [ALERT SENT]" : "");
    // First accepted POST is what promotes the config from "just typed in"
    // to "known good", which is what keeps the portal shut from now on.
    configMarkProven();
  } else {
    Serial.printf("POST failed (%d): %s\n", code, http.getString().c_str());
    // A bare code is not much to debug from, so name the likely cause. These
    // four cover essentially every failure seen during bring-up.
    if (code == 401 || code == 403) {
      Serial.println("  -> 401/403 is the DATABASE rejecting this device.");
      Serial.println("     Either the anon key is wrong, or baby code/device");
      Serial.println("     secret do not match the devices row. Check for a");
      Serial.println("     stray space if you pasted them from a phone.");
      Serial.printf("     baby_code currently: \"%s\"\n", cfg.babyCode.c_str());
    } else if (code == 404) {
      Serial.println("  -> 404 means the URL resolved but the function is not");
      Serial.println("     there. Either RUN_ALL.sql was never run, or the");
      Serial.printf("     project URL is wrong: %s\n", cfg.supabaseUrl.c_str());
    } else if (code < 0) {
      Serial.println("  -> negative code = never reached Supabase at all.");
      Serial.println("     DNS, TLS or no route. WiFi can be associated and");
      Serial.println("     still have no internet - check the router.");
    }
  }

  http.end();
  return code == 200;
}

// =========================================================
// Deep sleep (battery mode only)
// =========================================================
void goToSleep() {
  Serial.printf("Sleeping for %lus...\n\n", (unsigned long)SLEEP_SECONDS);
  Serial.flush();

  if (poxReady) pox.shutdown();
  if (mpuReady) mpu.enableSleep(true);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
}

// =========================================================
// Battery voltage
// =========================================================
// Deliberately not implemented. The SuperMini has no battery connector,
// no charger and no battery-sense divider, so there is nothing to read.
// If you add a LiPo + TP4056 and want a fuel gauge, divide BAT+ with
// 2x 100k into one of GPIO0-GPIO6, which is where the C6's ADC1 lives.
//
// The classic-ESP32 trap does not apply here: that chip's ADC2 shared
// hardware with the WiFi driver and analogRead() failed whenever the
// radio was up. The C6 exposes only ADC1, so any ADC pin you pick keeps
// working with WiFi running. Avoid GPIO4/5 anyway - strapping pins.

// =========================================================
// setup()
// =========================================================
void setup() {
  Serial.begin(115200);

  // Native USB CDC needs a moment to enumerate with the host before it will
  // accept output. Without this the first few prints - including the setup
  // portal's AP name and IP - vanish into a port that isn't open yet.
  //
  // Deliberately a plain delay and NOT `while (!Serial)`. On this board
  // Serial is USB CDC, so that loop waits for a computer to open the port
  // and a monitor plugged into a phone charger would hang in setup() and
  // never read a sensor. Losing a log line is the right trade.
  delay(1000);

  rtcBootCount++;
  Serial.println();
  Serial.println("=== NICU Monitor - ESP32-C6 SuperMini standalone ===");

  configLoad(cfg);
  portalButtonBegin();

  Serial.printf("baby_code: %s   boot #%lu\n",
                cfg.babyCode.length() ? cfg.babyCode.c_str() : "(unset)",
                (unsigned long)rtcBootCount);

  // Decide whether to open the setup portal.
  //
  //   nothing usable stored -> nothing else it can do
  //   BOOT held 3s          -> deliberate reconfiguration
  //
  // Note what is deliberately NOT a reason: a config that has been entered
  // but never confirmed working. Those get tried first and only send us
  // back to the portal further down, once an attempt has actually failed.
  // Opening the portal here on !proven instead would deadlock the device -
  // the flag is set by a successful POST, a POST needs this code to be
  // past the portal, so it would re-host the setup AP on every boot and
  // never reach the network the user just gave it.
  //
  // A proven config that merely fails right now does NOT land here either.
  // That case is a router reboot or a dropped link, and a monitor that
  // parks itself in a config page instead of retrying is a monitor that
  // stopped monitoring.
  bool forced = portalButtonHeld();
  if (forced) Serial.println("BOOT held - entering setup portal.");

  if (!cfg.usable() || forced) {
    if (!forced) Serial.println("No usable config stored - opening setup portal.");
    runPortal();
    // Returns only on the unattended timeout, with a stored network to try.
  }

  initSensors();

  // An unproven config that cannot even associate is nearly always a
  // mistyped password or a 5GHz-only SSID. Ask again rather than retrying
  // a network that has never once worked. Proven configs skip this and
  // fall through to the reconnect watchdog in loop().
  if (!connectWifi() && !cfg.proven) {
    Serial.println("Could not join that network, and this config has never worked.");
    Serial.println("Reopening setup portal so the details can be corrected.");
    runPortal();
    connectWifi();          // only reached if the portal timed out
  }

  Serial.printf("Settling MAX30100 for %lums...\n", SPO2_SETTLE_MS);
  settlePulseOximeter(SPO2_SETTLE_MS);

#if POWER_MODE == POWER_MODE_BATTERY_SLEEP
  // ---- Battery mode: whole cycle happens here, then we sleep ----
  //
  // The debounce readings are taken back-to-back inside this one wake.
  // If we took one per wake instead, a 30s sleep would mean 90s before a
  // critical desaturation raised an alert.
  Reading    lastReading;
  Evaluation worstEval;
  worstEval.raiseAlert = false;
  worstEval.isAbnormal = false;
  worstEval.reason     = "";
  worstEval.severity   = "critical";

  for (int i = 0; i < READINGS_PER_WAKE; i++) {
    if (i > 0) settlePulseOximeter(1000);   // keep the sensor fed between reads

    lastReading    = takeReading();
    Evaluation ev  = evaluateReading(lastReading);

    Serial.printf("  reading %d/%d: hr=%d spo2=%d temp=%.1f ok=%d\n",
                  i + 1, READINGS_PER_WAKE,
                  lastReading.hr, lastReading.spo2, lastReading.tempC,
                  lastReading.sensorOk);

    if (ev.isAbnormal) worstEval.isAbnormal = true;
    if (ev.raiseAlert) {
      worstEval.raiseAlert = true;
      worstEval.reason     = ev.reason;
      worstEval.severity   = ev.severity;
    }
  }

  // One post per wake: the latest numbers, plus any alert the burst raised.
  bool posted = pushToSupabase(lastReading, worstEval);

  // Deep sleep wipes the RAM counter used in USB mode, so the equivalent
  // check here is single-shot: a first-ever post that gets refused opens
  // the portal now rather than sleeping into an endless retry cycle no one
  // is watching.
  if (!posted && !cfg.proven) {
    Serial.println("First post refused. Opening setup portal instead of sleeping.");
    runPortal();
  }

  goToSleep();
#endif
}

// =========================================================
// loop()  — USB continuous mode only
// =========================================================
// In battery mode setup() ends in deep sleep, so this never runs.
void loop() {
#if POWER_MODE == POWER_MODE_USB_CONTINUOUS

  // Must run as often as possible. Everything else is gated behind the
  // timers below so we don't starve it.
  pox.update();

  // ---- Escape hatch ----
  // Hold BOOT for 3s to reopen the setup portal on a device that is already
  // running. Checked here rather than only in setup() so moving the cot to a
  // different ward doesn't mean finding a USB cable and a laptop.
  if (portalButtonHeld()) {
    Serial.println("\nBOOT held - reopening setup portal. Monitoring paused.");
    runPortal();          // restarts the chip once something is saved
  }

  // ---- Reconnect watchdog ----
  // WiFi.begin() blocks, so it must not run on the sensor cadence. Retry on
  // a slow backoff and keep reading in between: the thresholds are evaluated
  // on-device, so a dropped link costs us the upload, not the measurement.
  if (WiFi.status() != WL_CONNECTED &&
      millis() - lastReconnectMs >= RECONNECT_INTERVAL_MS) {
    Serial.println("WiFi down, retrying...");
    connectWifi();
    lastReconnectMs = millis();
  }

  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    Reading    r = takeReading();
    Evaluation e = evaluateReading(r);

    if (WiFi.status() == WL_CONNECTED) {
      // This blocks for a few hundred ms and the pulse oximeter loses some
      // samples, but it reconverges well before the next post.
      bool ok = pushToSupabase(r, e);

      // WiFi is up but Supabase keeps refusing us, and this config has
      // never worked. That is a data problem the network cannot fix -
      // usually a baby code with no matching devices row, or the wrong
      // secret. Send it back to setup instead of posting into a void.
      if (!cfg.proven) {
        if (ok) {
          unprovenPostFailures = 0;
        } else if (++unprovenPostFailures >= UNPROVEN_POST_FAILURE_LIMIT) {
          Serial.printf("\n%d posts rejected and never yet accepted.\n",
                        unprovenPostFailures);
          Serial.println("Check the baby code and device secret against public.devices.");
          unprovenPostFailures = 0;
          runPortal();
        }
      }
    } else {
      // Still worth printing: the serial console is the only readout left
      // while the link is down.
      Serial.printf("offline  hr=%d spo2=%d temp=%.1f motion=%.2f ok=%d%s\n",
                    r.hr, r.spo2, r.tempC, r.motion, r.sensorOk,
                    e.raiseAlert ? "  [ALERT PENDING]" : "");
    }

    lastSendMs = millis();
  }

#endif
}
