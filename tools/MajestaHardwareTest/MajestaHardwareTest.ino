#include <Arduino.h>

// GPIO numbers printed on the board, NOT physical header positions.
// GPIO32 -> 1k resistor -> blue LED anode; cathode -> GND.
// GPIO33 -> 1k resistor -> red LED anode; cathode -> GND.
// GPIO25 -> normally-open NEXT switch -> GND.
// GPIO26 -> normally-open PREVIOUS switch -> GND.
const uint8_t BLUE_LED = 32;
const uint8_t RED_LED = 33;
const uint8_t NEXT_SWITCH = 25;
const uint8_t PREV_SWITCH = 26;

void setup() {
  Serial.begin(115200);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(RED_LED, LOW);
  pinMode(NEXT_SWITCH, INPUT_PULLUP);
  pinMode(PREV_SWITCH, INPUT_PULLUP);
  delay(500);
  Serial.println("MAJESTA HARDWARE TEST v1 - LEDs alternate every second");
  Serial.println("Buttons: released=1, pressed=0. GPIO25=NEXT, GPIO26=PREVIOUS");
}

void loop() {
  uint32_t now = millis();
  bool blueOn = ((now / 1000) % 2) == 0;
  digitalWrite(BLUE_LED, blueOn ? HIGH : LOW);
  digitalWrite(RED_LED, blueOn ? LOW : HIGH);
  static uint32_t lastPrint = 0;
  if (now - lastPrint >= 250) {
    lastPrint = now;
    Serial.printf("GPIO25 NEXT=%d  GPIO26 PREV=%d  BLUE=%s  RED=%s\n",
                  digitalRead(NEXT_SWITCH), digitalRead(PREV_SWITCH),
                  blueOn ? "ON" : "OFF", blueOn ? "OFF" : "ON");
  }
  delay(1);
}
