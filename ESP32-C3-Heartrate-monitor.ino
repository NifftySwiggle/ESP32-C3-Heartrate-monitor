#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include "MAX30105.h"
#include "heartRate.h"

// ================= Hardware Configuration =================
#define SCREEN_WIDTH      72
#define SCREEN_HEIGHT     40
#define OLED_ADDR         0x3C
#define OLED_COL_OFFSET   28
#define MAX30102_INT_PIN  10
#define BOOT_BUTTON_PIN   9

#define BEAT_LED_PIN      2
#define BUZZER_PIN        3

#define WAVE_TOP          9
#define WAVE_BOTTOM       30
#define FOOTER_Y          32

// Buzzer Output Modes
enum BuzzerType {
  BUZZ_PASSIVE_PIEZO = 0, // Frequency modulated by SpO2 via tone()
  BUZZ_ACTIVE = 1,        // Fixed DC pulse via digitalWrite()
  BUZZ_OFF = 2            // Silent
};

// Application Modes
enum AppMode {
  MODE_LIVE = 0,
  MODE_HISTORY = 1,
  MODE_SETTINGS = 2
};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MAX30105 sensor;
Preferences prefs;

// ================= State Variables =================
AppMode currentMode = MODE_LIVE;
BuzzerType buzzerSetting = BUZZ_PASSIVE_PIEZO;

// Sensor Interrupt
volatile bool dataAvailable = false;
void IRAM_ATTR sensorISR() {
  dataAvailable = true;
}

// BPM Tracking
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
byte validRateCount = 0;
uint32_t lastBeat = 0;
uint32_t lastAcceptedBeat = 0;
int beatAvg = 0;
const uint32_t MIN_BEAT_INTERVAL_MS = 270; // Reject double triggers above 222 BPM.
const uint32_t HEART_RATE_TIMEOUT_MS = 3000;

// High-Accuracy Optical Beat Cycle Trackers
long cycleIrMax = 0, cycleIrMin = 999999;
long cycleRedMax = 0, cycleRedMin = 999999;
float liveIrDC = 0, liveRedDC = 0;
float spo2 = 0;
float perfusionIndex = 0;

// PPG Waveform Display
int waveBuffer[SCREEN_WIDTH];
float irAC_smooth = 0;
float acPeak = 300;
float acTrough = -300;

// Timers & Flags
bool beatOccurred = false;
bool beatOutputActive = false;
uint32_t beatBlinkTimer = 0;
uint32_t beatOutputTimer = 0;
const uint16_t BEAT_DURATION_MS = 30;
uint32_t lastDisplayUpdate = 0;

// Button Handling (Short vs Long Press)
bool buttonLastPhysical = HIGH;
uint32_t buttonPressStartTime = 0;
bool buttonActive = false;

// History Database
struct HeartRecord {
  uint8_t bpm;
  uint8_t spo2;
  uint16_t timestampSec;
};

const byte MAX_RECORDS = 8;
HeartRecord logDatabase[MAX_RECORDS];
byte recordCount = 0;
uint32_t lastAutoLogTime = 0;
const uint32_t LOG_INTERVAL_MS = 10000;

// ================= OLED Hardware Routines =================
void sendOLEDCommand(uint8_t cmd) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(cmd);
  Wire.endTransmission();
}

void updateDisplay() {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(0x21);
  Wire.write(OLED_COL_OFFSET);
  Wire.write(OLED_COL_OFFSET + SCREEN_WIDTH - 1);
  Wire.write(0x22);
  Wire.write(0);
  Wire.write(4);
  Wire.endTransmission();

  uint8_t *buf = display.getBuffer();
  for (int i = 0; i < 360; i += 16) {
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x40);
    Wire.write(&buf[i], 16);
    Wire.endTransmission();
  }
}

void wipeControllerRAM() {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(0x21); Wire.write(0); Wire.write(127);
  Wire.write(0x22); Wire.write(0); Wire.write(7);
  Wire.endTransmission();

  for (int i = 0; i < 64; i++) {
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x40);
    for (int j = 0; j < 16; j++) Wire.write(0x00);
    Wire.endTransmission();
  }
}

void drawHeartIcon(int x, int y, bool filled) {
  if (filled) {
    display.fillRect(x, y + 1, 2, 2, SSD1306_WHITE);
    display.fillRect(x + 3, y + 1, 2, 2, SSD1306_WHITE);
    display.fillRect(x + 1, y + 3, 3, 1, SSD1306_WHITE);
    display.drawPixel(x + 2, y + 4, SSD1306_WHITE);
  } else {
    display.drawPixel(x, y + 1, SSD1306_WHITE);
    display.drawPixel(x + 1, y, SSD1306_WHITE);
    display.drawPixel(x + 2, y + 1, SSD1306_WHITE);
    display.drawPixel(x + 3, y, SSD1306_WHITE);
    display.drawPixel(x + 4, y + 1, SSD1306_WHITE);
    display.drawPixel(x, y + 2, SSD1306_WHITE);
    display.drawPixel(x + 4, y + 2, SSD1306_WHITE);
    display.drawPixel(x + 1, y + 3, SSD1306_WHITE);
    display.drawPixel(x + 3, y + 3, SSD1306_WHITE);
    display.drawPixel(x + 2, y + 4, SSD1306_WHITE);
  }
}

// ================= History Database =================
void storeRecord(uint8_t bpmVal, uint8_t spo2Val) {
  for (int i = MAX_RECORDS - 1; i > 0; i--) {
    logDatabase[i] = logDatabase[i - 1];
  }
  logDatabase[0].bpm = bpmVal;
  logDatabase[0].spo2 = spo2Val;
  logDatabase[0].timestampSec = (uint16_t)(millis() / 1000);

  if (recordCount < MAX_RECORDS) {
    recordCount++;
  }
}

void resetHeartRateTracking() {
  memset(rates, 0, sizeof(rates));
  rateSpot = 0;
  validRateCount = 0;
  lastBeat = 0;
  lastAcceptedBeat = 0;
  beatAvg = 0;
}

void triggerBeatAlert(uint32_t now) {
  beatOccurred = true;
  beatBlinkTimer = now;
  beatOutputTimer = now;
  beatOutputActive = true;
  digitalWrite(BEAT_LED_PIN, HIGH);

  if (buzzerSetting == BUZZ_PASSIVE_PIEZO) {
    int pitch = 880;
    if (spo2 >= 88.0 && spo2 <= 100.0) {
      pitch = map((int)spo2, 88, 100, 520, 880);
    }
    tone(BUZZER_PIN, pitch);
  } else if (buzzerSetting == BUZZ_ACTIVE) {
    digitalWrite(BUZZER_PIN, HIGH);
  }
}

// ================= Button & Menu Logic =================
void handleButton() {
  bool currentReading = digitalRead(BOOT_BUTTON_PIN);

  // Button pressed down
  if (buttonLastPhysical == HIGH && currentReading == LOW) {
    buttonPressStartTime = millis();
    buttonActive = true;
  }

  // Button released
  if (buttonLastPhysical == LOW && currentReading == HIGH && buttonActive) {
    uint32_t pressDuration = millis() - buttonPressStartTime;
    buttonActive = false;

    if (pressDuration >= 600) {
      // --- LONG PRESS ACTION ---
      if (currentMode == MODE_SETTINGS) {
        // Cycle Buzzer: PIEZO -> ACTIVE -> OFF
        buzzerSetting = (BuzzerType)((buzzerSetting + 1) % 3);
        prefs.putUChar("buz", (uint8_t)buzzerSetting);
      } else if (currentMode == MODE_HISTORY) {
        recordCount = 0; // Wipe history
      } else if (currentMode == MODE_LIVE) {
        if (beatAvg >= 40 && spo2 >= 85) {
          storeRecord((uint8_t)beatAvg, (uint8_t)spo2);
        }
      }
    } else if (pressDuration >= 40) {
      // --- SHORT PRESS ACTION ---
      currentMode = (AppMode)((currentMode + 1) % 3);
    }
  }

  buttonLastPhysical = currentReading;
}

// ================= Render Views =================
void renderLiveView() {
  display.setCursor(0, 0);
  if (liveIrDC == 0) {
    display.print("HR:--");
    display.setCursor(44, 0);
    display.print("--%");
  } else {
    display.print("HR:");
    if (beatAvg > 0) display.print(beatAvg);
    else display.print("--");

    display.setCursor(44, 0);
    if (spo2 > 0) {
      display.print((int)spo2);
      display.print("%");
    } else {
      display.print("--%");
    }
  }

  display.drawFastHLine(0, 8, SCREEN_WIDTH, SSD1306_WHITE);

  if (liveIrDC == 0) {
    display.setCursor(12, 15);
    display.print("LEAD OFF");
    for (int x = 6; x < 66; x += 4) {
      display.drawFastHLine(x, 24, 2, SSD1306_WHITE);
    }
  } else {
    for (int x = 0; x < SCREEN_WIDTH - 1; x++) {
      display.drawLine(x, waveBuffer[x], x + 1, waveBuffer[x + 1], SSD1306_WHITE);
    }
  }

  drawHeartIcon(1, FOOTER_Y + 1, (liveIrDC != 0) && beatOccurred);

  display.setCursor(9, FOOTER_Y);
  display.print("REC:");
  if (recordCount > 0) {
    display.print(logDatabase[0].bpm);
    display.print("bpm");
  } else {
    display.print("--");
  }
}

void renderHistoryView() {
  display.setCursor(0, 0);
  display.print("HISTORY");
  if (recordCount > 0) {
    int sumBpm = 0;
    for (byte i = 0; i < recordCount; i++) sumBpm += logDatabase[i].bpm;
    display.setCursor(44, 0);
    display.print(sumBpm / recordCount);
    display.print("A");
  }
  display.drawFastHLine(0, 8, SCREEN_WIDTH, SSD1306_WHITE);

  if (recordCount == 0) {
    display.setCursor(12, 16);
    display.print("NO DATA");
    display.setCursor(2, 28);
    display.print("HOLD TO CLR");
    return;
  }

  display.setCursor(0, 11);
  display.print("1: ");
  display.print(logDatabase[0].bpm);
  display.print("b ");
  display.print(logDatabase[0].spo2);
  display.print("%");

  if (recordCount > 1) {
    display.setCursor(0, 20);
    display.print("2: ");
    display.print(logDatabase[1].bpm);
    display.print("b ");
    display.print(logDatabase[1].spo2);
    display.print("%");
  }

  display.setCursor(0, 31);
  display.print("[HOLD: WIPE]");
}

void renderSettingsView() {
  display.setCursor(0, 0);
  display.print("SETTINGS");
  display.drawFastHLine(0, 8, SCREEN_WIDTH, SSD1306_WHITE);

  display.setCursor(0, 12);
  display.print("BUZ:");
  if (buzzerSetting == BUZZ_PASSIVE_PIEZO) {
    display.print("PIEZO");
  } else if (buzzerSetting == BUZZ_ACTIVE) {
    display.print("ACTIVE");
  } else {
    display.print("OFF");
  }

  display.setCursor(0, 22);
  display.print("PI:");
  if (perfusionIndex > 0) {
    display.print(perfusionIndex, 1);
    display.print("%");
  } else {
    display.print("--");
  }

  display.setCursor(0, 32);
  display.print("HOLD:TOGGLE");
}

// ================= Hardware Setup =================
void setup() {
  Serial.begin(115200);

  Wire.begin(5, 6); // ESP32-C3: SDA=5, SCL=6

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MAX30102_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(MAX30102_INT_PIN), sensorISR, FALLING);

  pinMode(BEAT_LED_PIN, OUTPUT);
  digitalWrite(BEAT_LED_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Load Saved Preferences from Flash
  prefs.begin("pulseox", false);
  buzzerSetting = (BuzzerType)prefs.getUChar("buz", BUZZ_PASSIVE_PIEZO);
  if (buzzerSetting > BUZZ_OFF) {
    buzzerSetting = BUZZ_PASSIVE_PIEZO;
    prefs.putUChar("buz", (uint8_t)buzzerSetting);
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (1);
  }

  sendOLEDCommand(0xDA);
  sendOLEDCommand(0x12);

  wipeControllerRAM();
  display.setRotation(2);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(12, 10);
  display.print("HOSPITAL");
  display.setCursor(15, 20);
  display.print("MONITOR");
  updateDisplay();

  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    display.clearDisplay();
    display.setCursor(6, 16);
    display.print("SENSOR ERR");
    updateDisplay();
    while (1);
  }

  // Clinical Optical Configuration:
  // powerLevel = 0x24 (~7.2mA) for penetration without saturated photodiode
  // sampleAverage = 4, ledMode = 2 (Red + IR), sampleRate = 100Hz, pulseWidth = 411us (18-bit max SNR)
  sensor.setup(0x24, 4, 2, 100, 411, 4096);
  sensor.setPulseAmplitudeRed(0x24);
  sensor.setPulseAmplitudeIR(0x24);
  sensor.setPulseAmplitudeGreen(0);
  sensor.enableDATARDY();

  for (int i = 0; i < SCREEN_WIDTH; i++) {
    waveBuffer[i] = (WAVE_TOP + WAVE_BOTTOM) / 2;
  }

  delay(1000);
}

// ================= Loop =================
void loop() {
  handleButton();

  // Ingest MAX30102 FIFO
  if (dataAvailable || digitalRead(MAX30102_INT_PIN) == LOW) {
    dataAvailable = false;
    sensor.check();

    while (sensor.available()) {
      long irValue = sensor.getFIFOIR();
      long redValue = sensor.getFIFORed();
      sensor.nextSample();

      // Lead-Off Threshold
      if (irValue < 45000) {
        resetHeartRateTracking();
        spo2 = 0;
        liveIrDC = 0;
        liveRedDC = 0;
        irAC_smooth = 0;
        perfusionIndex = 0;
        cycleIrMax = 0; cycleIrMin = 999999;
        cycleRedMax = 0; cycleRedMin = 999999;
        continue;
      }

      // Track Cycle Extrema for AC/DC derivation
      if (irValue > cycleIrMax)   cycleIrMax = irValue;
      if (irValue < cycleIrMin)   cycleIrMin = irValue;
      if (redValue > cycleRedMax) cycleRedMax = redValue;
      if (redValue < cycleRedMin) cycleRedMin = redValue;

      // Baseline DC Low-Pass Filter
      if (liveIrDC == 0) {
        liveIrDC = irValue;
        liveRedDC = redValue;
      } else {
        liveIrDC = (liveIrDC * 0.98) + (irValue * 0.02);
        liveRedDC = (liveRedDC * 0.98) + (redValue * 0.02);
      }

      // Heartbeat Detection Cycle Closure
      bool acceptedBeat = false;
      if (checkForBeat(irValue)) {
        uint32_t now = millis();
        uint32_t delta = now - lastBeat;

        // The detector can occasionally fire twice on a sharp/noisy edge.
        if (lastBeat == 0) {
          lastBeat = now;
        } else if (delta >= MIN_BEAT_INTERVAL_MS) {
          float bpmInstant = 60000.0 / delta;

          // Ignore intervals outside the usable heart-rate range.
          bool plausible = bpmInstant >= 35 && bpmInstant <= 220;

          if (plausible) {
            rates[rateSpot++] = (byte)bpmInstant;
            rateSpot %= RATE_SIZE;
            if (validRateCount < RATE_SIZE) validRateCount++;

            beatAvg = 0;
            for (byte x = 0; x < validRateCount; x++) beatAvg += rates[x];
            beatAvg /= validRateCount;
            acceptedBeat = true;
            lastAcceptedBeat = now;
          }

          // Keep timing anchored to the detector even when a noisy interval
          // is rejected, so one false trigger cannot suppress later beats.
          lastBeat = now;
        }
      }

      if (acceptedBeat) {
        // Calculate True AC/DC Ratio across the finished cardiac cycle
        float irAC = (float)(cycleIrMax - cycleIrMin);
        float redAC = (float)(cycleRedMax - cycleRedMin);

        if (liveIrDC > 0 && liveRedDC > 0 && irAC > 30 && redAC > 30) {
          // Perfusion Index (PI%)
          perfusionIndex = (irAC / liveIrDC) * 100.0;

          // Only compute SpO2 if pulse strength passes clinical threshold (PI > 0.15%)
          if (perfusionIndex >= 0.15) {
            float rVal = (redAC / liveRedDC) / (irAC / liveIrDC);

            // Maxim Integrated / Clinical Empirical Polynomial Calibration
            float calcSpO2 = -45.060 * rVal * rVal + 30.354 * rVal + 94.845;
            calcSpO2 = constrain(calcSpO2, 85.0, 100.0);

            spo2 = (spo2 == 0) ? calcSpO2 : ((spo2 * 0.75) + (calcSpO2 * 0.25));
          }
        }

        // Reset Cycle Trackers for next beat
        cycleIrMax = irValue; cycleIrMin = irValue;
        cycleRedMax = redValue; cycleRedMin = redValue;

        // Every accepted pulse gets an alert. SpO2 only changes passive-piezo pitch.
        triggerBeatAlert(millis());
      }

      // Live Waveform Plotting
      float rawAC = (float)(liveIrDC - irValue);
      irAC_smooth = (irAC_smooth * 0.65) + (rawAC * 0.35);

      if (irAC_smooth > acPeak)   acPeak = irAC_smooth;
      if (irAC_smooth < acTrough) acTrough = irAC_smooth;

      acPeak   = (acPeak * 0.985) + (80 * 0.015);
      acTrough = (acTrough * 0.985) - (80 * 0.015);

      float span = acPeak - acTrough;
      if (span < 250) span = 250;

      int yPlot = WAVE_BOTTOM - (int)(((irAC_smooth - acTrough) / span) * (WAVE_BOTTOM - WAVE_TOP));
      yPlot = constrain(yPlot, WAVE_TOP, WAVE_BOTTOM);

      for (int i = 0; i < SCREEN_WIDTH - 1; i++) {
        waveBuffer[i] = waveBuffer[i + 1];
      }
      waveBuffer[SCREEN_WIDTH - 1] = yPlot;
    }
  }

  if (lastAcceptedBeat > 0 && millis() - lastAcceptedBeat >= HEART_RATE_TIMEOUT_MS) {
    resetHeartRateTracking();
    spo2 = 0;
    perfusionIndex = 0;
  }

  // Non-blocking Buzzer and LED Shutoff
  if (beatOutputActive && (millis() - beatOutputTimer >= BEAT_DURATION_MS)) {
    beatOutputActive = false;
    digitalWrite(BEAT_LED_PIN, LOW);

    if (buzzerSetting == BUZZ_PASSIVE_PIEZO) {
      noTone(BUZZER_PIN);
    } else if (buzzerSetting == BUZZ_ACTIVE) {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }

  // Periodic Auto-Logger
  if (beatAvg >= 40 && spo2 >= 85) {
    if (millis() - lastAutoLogTime >= LOG_INTERVAL_MS) {
      lastAutoLogTime = millis();
      storeRecord((uint8_t)beatAvg, (uint8_t)spo2);
    }
  }

  // Reset Display Heart Icon State
  if (beatOccurred && (millis() - beatBlinkTimer > 120)) {
    beatOccurred = false;
  }

  // Display Refresh (~30 FPS)
  if (millis() - lastDisplayUpdate >= 33) {
    lastDisplayUpdate = millis();
    display.clearDisplay();

    switch (currentMode) {
      case MODE_LIVE:     renderLiveView();     break;
      case MODE_HISTORY:  renderHistoryView();  break;
      case MODE_SETTINGS: renderSettingsView(); break;
    }

    updateDisplay();
  }
}
