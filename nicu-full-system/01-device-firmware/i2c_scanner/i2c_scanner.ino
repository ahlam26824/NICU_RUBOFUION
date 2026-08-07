/*
  =========================================================
  I2C Scanner — ESP32-C6 SuperMini (flash this one FIRST)
  =========================================================
  Purpose:
    Before flashing the application firmware, confirm that both I2C
    sensors actually respond:

      0x57  ->  MAX30100  (HR / SpO2)
      0x68  ->  MPU-6050  (motion)

  Why this is a separate sketch:
    A MAX30100 breakout (the purple GY-MAX30100 board) not showing up is
    by far the most common reason this build stalls. Debugging that from
    inside the application firmware wastes time; here you know in
    30 seconds.

  Wiring (same pins as the application firmware):
    SDA -> GPIO20
    SCL -> GPIO19
    Sensor VCC -> 3V3 pin (not 5V), GND common

  Reading the output:
    Each pass first measures the two lines electrically, then scans. A line
    that is floating or stuck low means no address can answer, so check that
    before reading anything into the address results.

    Which sensors are missing tells you where to look, and the sketch says
    so per case. The one worth knowing up front: BOTH silent is not the
    common MAX30100 pull-up fault. That fault leaves 0x68 visible, because
    the GY-521 works as shipped. Both silent means the whole bus is down -
    often an un-reworked MAX30100 clamping it, since its pull-ups sit on a
    1.8V rail the ESP32 cannot read as HIGH.

    You can rewire between passes; each scan re-measures.

  Board: Arduino IDE > Tools > Board > ESP32 Arduino > "MakerGO ESP32 C6 SuperMini"
         (any ESP32-C6 board works; "ESP32C6 Dev Module" is the safe fallback)
  Also set: Tools > USB CDC On Boot > Enabled, or this prints to UART0 on
         GPIO16/17 and Serial Monitor stays empty - this board has no
         USB-UART bridge.
  Serial Monitor: 115200 baud
  =========================================================
*/

#include <Wire.h>

// ESP32-C6 SuperMini default I2C pins — identical to the application
// firmware, and the ones silkscreened SDA/SCL on the board.
// Do not move these to GPIO24-30: those are wired to the chip's internal
// SPI flash on this board.
#define I2C_SDA  20
#define I2C_SCL  19

#define ADDR_MAX30100  0x57
#define ADDR_MPU6050   0x68

// Electrical state of one bus line, measured before Wire takes the pins.
enum LineState {
  LINE_PULLED_UP,   // a real external pull-up is holding it high - correct
  LINE_FLOATING,    // no external pull-up anywhere on the bus
  LINE_STUCK_LOW    // shorted to GND, or a device is clamping it
};

// An I2C line idles high. Which of the three ways it can fail tells you
// which fault you have, and that is worth knowing before interpreting a
// scan that found nothing.
//
//   INPUT_PULLDOWN : the internal pulldown is ~45k. Any real external
//                    pull-up (2.2k-10k) beats it, so HIGH here proves one
//                    is fitted and powered.
//   INPUT_PULLUP   : ~45k up. If the line still reads LOW, something is
//                    actively holding it down.
LineState readLineState(int pin) {
  pinMode(pin, INPUT_PULLDOWN);
  delayMicroseconds(500);
  bool highAgainstPulldown = digitalRead(pin);

  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(500);
  bool highAgainstPullup = digitalRead(pin);

  pinMode(pin, INPUT);   // leave the pin neutral for Wire.begin()

  if (highAgainstPulldown) return LINE_PULLED_UP;
  if (highAgainstPullup)   return LINE_FLOATING;
  return LINE_STUCK_LOW;
}

const char* lineStateName(LineState s) {
  switch (s) {
    case LINE_PULLED_UP: return "pulled up (OK)";
    case LINE_FLOATING:  return "FLOATING - no external pull-up";
    default:             return "STUCK LOW";
  }
}

LineState sdaState = LINE_PULLED_UP;
LineState sclState = LINE_PULLED_UP;

void setup() {
  Serial.begin(115200);
  delay(300);   // let the USB-UART bridge settle

  Serial.println();
  Serial.println("=== I2C Scanner - ESP32-C6 SuperMini ===");
  Serial.printf("SDA=GPIO%d, SCL=GPIO%d\n", I2C_SDA, I2C_SCL);
  Serial.println("Rewire freely - every scan re-measures. No reset needed.");
  Serial.println();
}

void loop() {
  int found = 0;
  bool foundMax = false;
  bool foundMpu = false;

  // Measure the lines with the I2C peripheral released, so this reflects
  // the wiring as it is right now. That matters while you are unplugging
  // sensors to isolate a fault.
  Wire.end();
  sdaState = readLineState(I2C_SDA);
  sclState = readLineState(I2C_SCL);

  Serial.println("--- Bus health (measured with I2C released) ---");
  Serial.printf("  SDA (GPIO%d): %s\n", I2C_SDA, lineStateName(sdaState));
  Serial.printf("  SCL (GPIO%d): %s\n", I2C_SCL, lineStateName(sclState));
  Serial.println("-----------------------------------------------");
  Serial.println();

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);   // 100kHz for the scan — more forgiving of marginal wiring

  Serial.println("Scanning 0x01 .. 0x7F ...");

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();

    if (err == 0) {
      found++;
      Serial.printf("  found 0x%02X", addr);

      if (addr == ADDR_MAX30100) { Serial.print("  <- MAX30100 (HR/SpO2)"); foundMax = true; }
      if (addr == ADDR_MPU6050)  { Serial.print("  <- MPU-6050 (motion)");  foundMpu = true; }
      // MPU-6050 moves to this address when its AD0 pin is pulled HIGH
      if (addr == 0x69)          { Serial.print("  <- MPU-6050 (AD0 = HIGH)"); foundMpu = true; }

      Serial.println();
    }
  }

  Serial.println();
  if (found == 0) {
    Serial.println("No devices responded - most likely a wiring or power problem.");
  }

  // ---------- Result summary ----------
  Serial.println("---------------- RESULT ----------------");
  Serial.printf("MAX30100 (0x57): %s\n", foundMax ? "OK" : "NOT FOUND  <- must be fixed");
  Serial.printf("MPU-6050 (0x68): %s\n", foundMpu ? "OK" : "NOT FOUND  <- must be fixed");
  Serial.println("----------------------------------------");

  if (foundMax && foundMpu) {
    Serial.println("Both sensors OK - you can now flash esp32_wroom_standalone.ino");
  }

  // ---------- Diagnosis ----------
  // Which advice is right depends on WHICH sensors are missing, so branch
  // on that rather than always printing the pull-up story.

  bool lineFault = (sdaState != LINE_PULLED_UP) || (sclState != LINE_PULLED_UP);

  if (lineFault) {
    Serial.println();
    Serial.println("The bus is electrically broken - no address can respond.");
    Serial.println();
    if (sdaState == LINE_FLOATING || sclState == LINE_FLOATING) {
      Serial.println("  A line is FLOATING: nothing is pulling it high.");
      Serial.println("   - Is sensor power actually connected? An unpowered");
      Serial.println("     GY-521 cannot supply its own pull-ups.");
      Serial.println("   - Fit 4.7k from SDA -> 3V3 and SCL -> 3V3.");
    }
    if (sdaState == LINE_STUCK_LOW || sclState == LINE_STUCK_LOW) {
      Serial.println("  A line is STUCK LOW: something is holding it down.");
      Serial.println("   - Check for a solder bridge or a wire shorted to GND.");
      Serial.println("   - Unplug one sensor at a time to find which clamps it.");
    }
  }
  else if (!foundMax && !foundMpu) {
    // Both silent on a healthy-looking bus. The GY-521 works as shipped, so
    // it should have answered - which points at something common to both.
    Serial.println();
    Serial.println("BOTH sensors are silent. Note this is NOT the usual");
    Serial.println("MAX30100 pull-up case - that one still leaves 0x68 visible.");
    Serial.println();
    Serial.println("Most likely: the un-reworked MAX30100 is taking the whole");
    Serial.println("bus down. Its pull-ups sit on the module's internal 1.8V");
    Serial.println("rail, and 1.8V is below the ESP32's logic-high threshold");
    Serial.println("(~2.5V), so no device on the shared bus can be read.");
    Serial.println();
    Serial.println("  TEST: unplug the MAX30100 completely - all four wires -");
    Serial.println("        and rescan.");
    Serial.println("   - 0x68 now appears  -> confirmed. Do the rework (WIRING.md S4).");
    Serial.println("   - 0x68 still absent -> the fault is shared wiring:");
    Serial.println("       * GND common between board and sensors?");
    Serial.println("       * sensor VCC on the 3V3 pin (not 5V)?");
    Serial.printf ("       * SDA on GPIO%d and SCL on GPIO%d, not swapped?\n",
                   I2C_SDA, I2C_SCL);
  }
  else if (!foundMax && foundMpu) {
    // The textbook case: bus is fine, one sensor is deaf.
    Serial.println();
    Serial.println("0x68 answers but 0x57 does not, so the bus works and the");
    Serial.println("MAX30100 alone is the problem. This is the common case:");
    Serial.println();
    Serial.println("  On most purple GY-MAX30100 boards the SDA/SCL pull-up");
    Serial.println("  resistors are wired to the board's internal 1.8V rail");
    Serial.println("  instead of 3.3V, so the ESP32 never sees the sensor.");
    Serial.println();
    Serial.println("  Fix (one-time rework):");
    Serial.println("   1. Remove the two 4.7k pull-up resistors on the board");
    Serial.println("      (or cut their traces)");
    Serial.println("   2. Add your own 4.7k pull-ups:  SDA -> 3.3V,  SCL -> 3.3V");
    Serial.println();
    Serial.println("  If it stays missing after the rework, check VIN is on 3V3");
    Serial.println("  and that its GND is shared.");
  }
  else if (foundMax && !foundMpu) {
    Serial.println();
    Serial.println("0x57 answers but 0x68 does not - the bus is fine.");
    Serial.println("   - Is the GY-521 powered? Its onboard LED should be lit.");
    Serial.println("   - AD0 tied HIGH moves it to 0x69; this scan would have");
    Serial.println("     reported that, so it is not the cause here.");
    Serial.println("   - Check its VCC and GND jumpers.");
  }

  Serial.println();
  Serial.println("Re-scanning in 5 seconds...");
  Serial.println();
  delay(5000);
}
