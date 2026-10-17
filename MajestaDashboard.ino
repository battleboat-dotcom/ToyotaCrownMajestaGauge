#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/twai.h"

// NodeMCU-32S + SN65HVD230, existing wiring unchanged.
// OBD pin 6 = CAN-H, pin 14 = CAN-L. Module termination removed.
// Sends ONLY service 01 current-data requests. No code clearing or actuation.
#define CAN_TX GPIO_NUM_17
#define CAN_RX GPIO_NUM_16
#define CONNECTED_LED 32 // Blue LED through 1k resistor to GND
#define RX_LED 33 // Red LED through 1k resistor to GND
#define NEXT_BUTTON 25 // Normally-open momentary switch to GND
#define PREV_BUTTON 26 // Normally-open momentary switch to GND
Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool oledOK = false, canOK = false, recovering = false;
enum PageId : uint8_t {
  HOME, COOLANT, INTAKE, RPM, SPEED, LOAD, THROTTLE, FUEL, RUNTIME,
  TEMPERATURES, RPM_SPEED, LOAD_THROTTLE, RUNTIME_FUEL, FAULT_STATUS,
  CAN_DIAGNOSTICS, PAGE_COUNT
};
uint8_t page = 0;
uint32_t frames = 0, lastFrame = 0, replies = 0, txFailures = 0;
uint32_t lastId = 0, lastDraw = 0, lastSend = 0, sentAt = 0;
uint32_t discoveryAt = 0;
bool discoveryStarted = false, pending = false, supportedKnown = false;
uint8_t pendingPID = 0, nextMetric = 0;
uint32_t ecu = 0, supported = 0;
uint32_t supported20 = 0, extraDiscoveryAt = 0;
bool support20Known = false, extraDiscoveryStarted = false;
const uint32_t STALE_MS = 5000;
const uint32_t REQUEST_INTERVAL_MS = 40;
const uint32_t RESPONSE_TIMEOUT_MS = 250;
const uint32_t DISPLAY_INTERVAL_MS = 50;

struct Reading {
  uint8_t pid;
  const char *label;
  const char *unit;
  float value;
  bool valid;
  uint32_t updated;
};
Reading readings[] = {
  {0x05, "Coolant", "C", 0, false, 0},
  {0x0F, "Intake", "C", 0, false, 0},
  {0x0C, "RPM", "rpm", 0, false, 0},
  {0x0D, "Speed", "km/h", 0, false, 0},
  {0x04, "Load", "%", 0, false, 0},
  {0x11, "Throttle", "%", 0, false, 0},
  {0x01, "MIL", "", 0, false, 0},
  {0x2F, "Fuel", "%", 0, false, 0}
};
const uint8_t NUM_READINGS = sizeof(readings) / sizeof(readings[0]);
float minimumSeen[NUM_READINGS] = {}, maximumSeen[NUM_READINGS] = {};
bool extremaKnown[NUM_READINGS] = {};
bool engineRunning = false, runSeen = false, completedRunSeen = false;
bool runtimeWasFresh = false;
uint64_t runMillis = 0, shortestRun = 0, longestRun = 0;
uint32_t runtimeTickAt = 0;
uint32_t lastRPMRequest = 0;
uint8_t dtcCount = 0;
uint32_t lastOBDReply = 0;
uint32_t rxPulseStarted = 0;
bool rxPulseActive = false;
bool rxPulseEver = false;

bool supports(uint8_t pid) {
  if (!supportedKnown || pid == 0) return false;
  if (pid <= 32) return supported & (uint32_t(1) << (32 - pid));
  if (pid <= 64 && support20Known)
    return supported20 & (uint32_t(1) << (64 - pid));
  return false;
}

void printFrame(const twai_message_t &m) {
  Serial.printf("OBD RX 0x%03lX:", (unsigned long)m.identifier);
  for (uint8_t i = 0; i < m.data_length_code && i < 8; ++i)
    Serial.printf(" %02X", m.data[i]);
  Serial.println();
}

void receiveCAN() {
  twai_message_t m;
  // Bound each batch so display and button remain responsive on a busy bus.
  for (int n = 0; n < 100 && twai_receive(&m, 0) == ESP_OK; ++n) {
    ++frames;
    lastFrame = millis();
    // Limit flashes to five per second so busy traffic remains visibly pulsed.
    if (!rxPulseEver || (!rxPulseActive && millis() - rxPulseStarted >= 200)) {
      rxPulseStarted = millis();
      rxPulseActive = true;
      rxPulseEver = true;
      digitalWrite(RX_LED, HIGH);
    }
    lastId = m.identifier;
    if (m.extd || m.rtr || m.identifier < 0x7E8 || m.identifier > 0x7EF) continue;
    printFrame(m);
    // Only single-frame ISO-TP responses to our outstanding service 01 request.
    if (!pending || millis() - sentAt >= ((pendingPID == 0x00 || pendingPID == 0x20) ? 700UL : RESPONSE_TIMEOUT_MS) || m.data_length_code < 4) continue;
    uint8_t len = m.data[0];
    if ((len & 0xF0) || len < 3 || len > 7 || len + 1 > m.data_length_code) continue;
    if (m.data[1] != 0x41 || m.data[2] != pendingPID) continue;
    if (ecu && m.identifier != ecu) continue;
    uint8_t pid = m.data[2];
    if (pid == 0x00 || pid == 0x20) {
      if (len < 6) continue;
      // Lock onto one ECU; never combine data from different responders.
      uint32_t mask = (uint32_t(m.data[3]) << 24) | (uint32_t(m.data[4]) << 16) |
                      (uint32_t(m.data[5]) << 8) | m.data[6];
      if (pid == 0x00) {
        ecu = m.identifier;
        supported = mask;
        supportedKnown = true;
        // No continuation bit means PIDs 21..40 are not advertised.
        if (!(supported & 1)) support20Known = true;
      } else {
        supported20 = mask;
        support20Known = true;
      }
      Serial.printf("Selected ECU 0x%03lX; support mask 0x%08lX\n",
                    (unsigned long)ecu, (unsigned long)mask);
      for (uint8_t i = 0; i < NUM_READINGS; ++i)
        Serial.printf("  PID %02X %s: %s\n", readings[i].pid, readings[i].label,
                      supports(readings[i].pid) ? "supported" : "not advertised");
    } else {
      uint8_t required = (pid == 0x01) ? 6 : (pid == 0x0C ? 4 : 3);
      if (len < required) continue;
      for (uint8_t i = 0; i < NUM_READINGS; ++i) {
        Reading &r = readings[i];
        if (r.pid != pid) continue;
        uint8_t a = m.data[3];
        if (pid == 0x05 || pid == 0x0F) r.value = int(a) - 40;
        else if (pid == 0x0C) r.value = (uint16_t(a) * 256 + m.data[4]) / 4.0f;
        else if (pid == 0x04 || pid == 0x11 || pid == 0x2F) r.value = a * 100.0f / 255.0f;
        else if (pid == 0x01) { r.value = (a & 0x80) != 0; dtcCount = a & 0x7F; }
        else r.value = a;
        r.valid = true;
        r.updated = millis();
        if (!extremaKnown[i]) {
          minimumSeen[i] = maximumSeen[i] = r.value;
          extremaKnown[i] = true;
        } else {
          if (r.value < minimumSeen[i]) minimumSeen[i] = r.value;
          if (r.value > maximumSeen[i]) maximumSeen[i] = r.value;
        }
      }
    }
    ++replies;
    lastOBDReply = millis();
    pending = false;
  }
}

void requestPID(uint8_t pid) {
  twai_message_t m = {};
  m.identifier = ecu ? ecu - 8 : 0x7DF;
  m.data_length_code = 8;
  m.ss = 1; // Single-shot transmission; no automatic retry loop.
  m.data[0] = 0x02;
  m.data[1] = 0x01;
  m.data[2] = pid;
  lastSend = millis();
  if (pid == 0x0C) lastRPMRequest = lastSend;
  if (twai_transmit(&m, 0) == ESP_OK) {
    pending = true;
    pendingPID = pid;
    sentAt = millis();
    Serial.printf("OBD TX 0x%03lX: 02 01 %02X 00 00 00 00 00\n",
                  (unsigned long)m.identifier, pid);
  } else {
    ++txFailures;
    Serial.println("OBD TX queue failed");
  }
}

void pollOBD() {
  if (!canOK) return;
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) return;
  if (status.state == TWAI_STATE_BUS_OFF) {
    pending = false;
    if (!recovering && twai_initiate_recovery() == ESP_OK) {
      recovering = true;
      Serial.println("CAN bus-off; recovery started");
    }
    return;
  }
  if (recovering && status.state == TWAI_STATE_STOPPED) {
    if (twai_start() == ESP_OK) {
      recovering = false;
      Serial.println("CAN restarted after recovery");
    }
    return;
  }
  if (status.state != TWAI_STATE_RUNNING) return;
  uint32_t now = millis();
  if (pending) {
    if (now - sentAt < ((pendingPID == 0x00 || pendingPID == 0x20) ? 700UL : RESPONSE_TIMEOUT_MS)) return;
    Serial.printf("No reply to PID %02X\n", pendingPID);
    pending = false;
  }
  if (now - lastSend < REQUEST_INTERVAL_MS) return;
  if (!supportedKnown) {
    if (!discoveryStarted || now - discoveryAt >= 5000) {
      discoveryStarted = true;
      discoveryAt = now;
      requestPID(0x00);
    }
    return;
  }
  if ((supported & 1) && !support20Known &&
      (!extraDiscoveryStarted || now - extraDiscoveryAt >= 5000)) {
    extraDiscoveryStarted = true;
    extraDiscoveryAt = now;
    requestPID(0x20);
    return;
  }
  // Prioritise an RPM request every 250 ms, without starving other PIDs on timeout.
  if (supports(0x0C) && now - lastRPMRequest >= 250) {
    requestPID(0x0C);
    return;
  }
  // Three priority requests, then one background request.
  // Priority follows the visible gauges; otherwise RPM/load/throttle rotate.
  // Only one diagnostic request is outstanding at a time.
  static uint8_t priorityTurn = 0;
  static uint8_t fastIndex = 0;
  const uint8_t fastMetrics[3] = {2, 4, 5};
  uint8_t chosen = NUM_READINGS;
  if (priorityTurn != 3) {
    if (page >= TEMPERATURES && page <= LOAD_THROTTLE) {
      const uint8_t pageMetrics[3][2] = {{0, 1}, {2, 3}, {4, 5}};
      for (uint8_t n = 0; n < 2; ++n) {
        uint8_t i = pageMetrics[page - TEMPERATURES][fastIndex % 2];
        fastIndex = (fastIndex + 1) % 2;
        if (supports(readings[i].pid)) { chosen = i; break; }
      }
    } else if (page == RUNTIME_FUEL) {
      uint8_t i = (fastIndex++ % 2) ? 7 : 2;
      if (supports(readings[i].pid)) chosen = i;
    } else if (page >= COOLANT && page <= RUNTIME) {
      const uint8_t singles[8] = {0, 1, 2, 3, 4, 5, 7, 2};
      uint8_t i = singles[page - COOLANT];
      if (supports(readings[i].pid)) chosen = i;
    } else {
      for (uint8_t n = 0; n < 3; ++n) {
        uint8_t i = fastMetrics[fastIndex % 3];
        fastIndex = (fastIndex + 1) % 3;
        if (supports(readings[i].pid)) { chosen = i; break; }
      }
    }
  }
  if (chosen == NUM_READINGS) {
    for (uint8_t n = 0; n < NUM_READINGS; ++n) {
      uint8_t i = nextMetric;
      nextMetric = (nextMetric + 1) % NUM_READINGS;
      if (supports(readings[i].pid)) { chosen = i; break; }
    }
  }
  if (chosen != NUM_READINGS) {
    requestPID(readings[chosen].pid);
    priorityTurn = (priorityTurn + 1) % 4;
  }
}
void updateIndicators() {
  uint32_t now = millis();
  twai_status_info_t status;
  bool running = canOK && twai_get_status_info(&status) == ESP_OK &&
                 status.state == TWAI_STATE_RUNNING;
  // Connected means a valid diagnostic reply was received within five seconds.
  digitalWrite(CONNECTED_LED,
               running && replies > 0 && now - lastOBDReply <= STALE_MS ? HIGH : LOW);
  if (rxPulseActive && now - rxPulseStarted >= 40) {
    digitalWrite(RX_LED, LOW);
    rxPulseActive = false;
  }
}
void buttonTick() {
  const uint8_t pins[2] = {NEXT_BUTTON, PREV_BUTTON};
  static bool previousRaw[2] = {HIGH, HIGH};
  static bool stable[2] = {HIGH, HIGH};
  static uint32_t changed[2] = {0, 0};
  bool pressed[2] = {false, false};
  uint32_t now = millis();
  for (uint8_t i = 0; i < 2; ++i) {
    bool raw = digitalRead(pins[i]);
    if (raw != previousRaw[i]) {
      previousRaw[i] = raw;
      changed[i] = now;
    }
    if (now - changed[i] >= 30 && raw != stable[i]) {
      stable[i] = raw;
      pressed[i] = (stable[i] == LOW);
    }
  }
  // One page per press, no repeat when held. Ignore presses while both are down.
  if (stable[0] == LOW && stable[1] == LOW) return;
  if (pressed[0]) page = (page + 1) % PAGE_COUNT;
  else if (pressed[1]) page = (page + PAGE_COUNT - 1) % PAGE_COUNT;
}

// Two 63-pixel-wide panels; size-2 values fit up to five characters.
void centeredText(const char *text, int16_t left, int16_t width,
                  int16_t y, uint8_t size) {
  display.setTextSize(size);
  int16_t textWidth = strlen(text) * 6 * size;
  display.setCursor(left + (width - textWidth) / 2, y);
  display.print(text);
}

bool freshReading(uint8_t index) {
  return supportedKnown && supports(readings[index].pid) &&
         readings[index].valid && millis() - readings[index].updated <= STALE_MS;
}

void updateRuntime() {
  uint32_t now = millis();
  uint32_t elapsed = now - runtimeTickAt;
  runtimeTickAt = now;
  bool fresh = freshReading(2);
  if (engineRunning && runtimeWasFresh && fresh) runMillis += elapsed;
  runtimeWasFresh = fresh;
  // Missing data is not evidence that the engine stopped. Pause the timer.
  if (!fresh) return;
  bool running = readings[2].value > 0;
  if (running && !engineRunning) {
    runMillis = 0;
    runSeen = true;
  } else if (!running && engineRunning) {
    if (!completedRunSeen) shortestRun = longestRun = runMillis;
    else {
      if (runMillis < shortestRun) shortestRun = runMillis;
      if (runMillis > longestRun) longestRun = runMillis;
    }
    completedRunSeen = true;
  }
  engineRunning = running;
}

void formatRuntime(uint64_t milliseconds, char *buffer, size_t length) {
  uint64_t seconds = milliseconds / 1000;
  snprintf(buffer, length, "%lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
           (unsigned long)((seconds / 60) % 60), (unsigned long)(seconds % 60));
}

const char *readingStatus(uint8_t index) {
  if (!supportedKnown || (readings[index].pid > 32 && !support20Known)) return "WAIT";
  if (!supports(readings[index].pid)) return "N/A";
  if (!readings[index].valid) return "---";
  if (!freshReading(index)) return "STALE";
  return nullptr;
}

void drawGauge(uint8_t index, int16_t left, const char *label,
               float minimum, float maximum) {
  Reading &r = readings[index];
  centeredText(label, left, 63, 16, 1);
  const char *status = readingStatus(index);
  char value[12];
  if (status) snprintf(value, sizeof(value), "%s", status);
  else snprintf(value, sizeof(value), "%.0f", double(r.value));
  centeredText(value, left, 63, 28, 2);
  centeredText(r.unit, left, 63, 46, 1);
  display.drawRect(left + 3, 56, 57, 8, SSD1306_WHITE);
  if (!status) {
    // Graph bounds only: these are NOT warning thresholds or factory limits.
    float fraction = (r.value - minimum) / (maximum - minimum);
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;
    int16_t fill = int16_t(fraction * 55 + 0.5f);
    if (fill > 0) display.fillRect(left + 4, 57, fill, 6, SSD1306_WHITE);
  }
}

void drawFaultStatus() {
  centeredText("ENG LIGHT", 0, 63, 16, 1);
  centeredText("DTC COUNT", 65, 63, 16, 1);
  const char *status = nullptr;
  if (!supportedKnown) status = "WAIT";
  else if (!supports(0x01)) status = "N/A";
  else if (!readings[6].valid) status = "---";
  else if (!freshReading(6)) status = "STALE";
  char count[8];
  snprintf(count, sizeof(count), "%u", unsigned(dtcCount));
  centeredText(status ? status : (readings[6].value > 0 ? "ON" : "OFF"), 0, 63, 30, 2);
  centeredText(status ? status : count, 65, 63, 30, 2);
  centeredText("MIL", 0, 63, 52, 1);
  centeredText("stored", 65, 63, 52, 1);
}

void drawSingleGauge(uint8_t index) {
  Reading &r = readings[index];
  char label[28], value[16], extrema[28];
  snprintf(label, sizeof(label), "%s (%s)", r.label, r.unit);
  centeredText(label, 0, 128, 16, 1);
  const char *status = readingStatus(index);
  if (status) snprintf(value, sizeof(value), "%s", status);
  else snprintf(value, sizeof(value), "%.0f", double(r.value));
  centeredText(value, 0, 128, 27, 3);
  if (extremaKnown[index])
    snprintf(extrema, sizeof(extrema), "MIN %.0f  MAX %.0f",
             double(minimumSeen[index]), double(maximumSeen[index]));
  else snprintf(extrema, sizeof(extrema), "MIN ---  MAX ---");
  // Up to five-digit RPM minima/maxima fit at the standard font size.
  centeredText(extrema, 0, 128, 56, 1);
}

void drawRuntime(bool fullScreen) {
  char value[24];
  const char *status = readingStatus(2);
  if (status) snprintf(value, sizeof(value), "%s", status);
  else formatRuntime(runMillis, value, sizeof(value));
  if (fullScreen) {
    centeredText("ENGINE RUN TIME", 0, 128, 16, 1);
    centeredText(value, 0, 128, 27, strlen(value) <= 10 ? 2 : 1);
    char limits[24], shortest[20], longest[20];
    formatRuntime(shortestRun, shortest, sizeof(shortest));
    formatRuntime(longestRun, longest, sizeof(longest));
    snprintf(limits, sizeof(limits), "MIN %s", completedRunSeen ? shortest : "---");
    centeredText(limits, 0, 128, 47, 1);
    snprintf(limits, sizeof(limits), "MAX %s", completedRunSeen ? longest : "---");
    centeredText(limits, 0, 128, 56, 1);
  } else {
    centeredText("RUN TIME", 0, 63, 16, 1);
    // h:mm:ss fits the half-width panel at size 1.
    centeredText(value, 0, 63, 31, 1);
    centeredText(status ? "NO RPM" : (engineRunning ? "RUNNING" : "STOPPED"),
                 0, 63, 48, 1);
  }
}

void drawScreen() {
  if (!oledOK || millis() - lastDraw < DISPLAY_INTERVAL_MS) return;
  lastDraw = millis();
  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 11, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 2);
  display.print("Majesta Gauge ");
  display.print(page + 1);
  display.print('/');
  display.print(PAGE_COUNT);
  display.setTextColor(SSD1306_WHITE);
  if (page >= TEMPERATURES && page <= FAULT_STATUS)
    display.drawFastVLine(63, 16, 48, SSD1306_WHITE);
  if (page == HOME) {
    centeredText("Toyota", 0, 128, 16, 2);
    centeredText("Crown", 0, 128, 32, 2);
    centeredText("Majesta", 0, 128, 48, 2);
  } else if (page >= COOLANT && page <= FUEL) {
    const uint8_t singles[7] = {0, 1, 2, 3, 4, 5, 7};
    drawSingleGauge(singles[page - COOLANT]);
  } else if (page == RUNTIME) {
    drawRuntime(true);
  } else if (page == TEMPERATURES) {
    drawGauge(0, 0, "COOLANT", -40, 130);
    drawGauge(1, 65, "INTAKE", -40, 100);
  } else if (page == RPM_SPEED) {
    drawGauge(2, 0, "ENGINE", 0, 8000);
    drawGauge(3, 65, "SPEED", 0, 180);
  } else if (page == LOAD_THROTTLE) {
    drawGauge(4, 0, "LOAD", 0, 100);
    drawGauge(5, 65, "THROTTLE", 0, 100);
  } else if (page == FAULT_STATUS) {
    drawFaultStatus();
  } else if (page == RUNTIME_FUEL) {
    drawRuntime(false);
    drawGauge(7, 65, "FUEL", 0, 100);
  } else {
    display.setTextSize(1);
    display.setCursor(0, 16);
    if (!canOK) display.print("Driver FAILED");
    else if (recovering) display.print("CAN recovering");
    else if (!frames || millis() - lastFrame > 2000) display.print("CAN: no recent RX");
    else display.print("CAN: traffic");
    display.setCursor(0, 28); display.print("Frames: "); display.print(frames);
    display.setCursor(0, 40); display.print("OBD replies: "); display.print(replies);
    display.setCursor(0, 52);
    if (ecu) { display.print("ECU: 0x"); display.print(ecu, HEX); }
    else display.print("OBD: searching");
  }
  display.display();
}
void setup() {
  Serial.begin(115200);
  pinMode(CONNECTED_LED, OUTPUT);
  pinMode(RX_LED, OUTPUT);
  digitalWrite(CONNECTED_LED, LOW);
  digitalWrite(RX_LED, LOW);
  pinMode(NEXT_BUTTON, INPUT_PULLUP);
  pinMode(PREV_BUTTON, INPUT_PULLUP);
  Wire.begin(21, 22);
  Wire.setClock(400000);
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledOK) Serial.println("OLED failed; serial diagnostics still available");
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX, CAN_RX, TWAI_MODE_NORMAL);
  g.rx_queue_len = 100;
  g.tx_queue_len = 1;
  // One transmission attempt per queued request; avoids endless retries on a disconnected bus.
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t result = twai_driver_install(&g, &t, &f);
  if (result == ESP_OK) result = twai_start();
  canOK = result == ESP_OK;
  Serial.println("Majesta dashboard: 500 kbps, NORMAL mode, service 01 reads");
  Serial.println("External buttons: GPIO25 = next; GPIO26 = previous; switches to GND.");
  if (!canOK) Serial.printf("TWAI init failed: %d\n", int(result));
}

void loop() {
  buttonTick();
  if (canOK) receiveCAN();
  updateRuntime();
  pollOBD();
  updateIndicators();
  drawScreen();
  delay(1);
}






