static const unsigned long BAUD = 115200;
static const unsigned long TICK_MS = 1000;

unsigned long last_tick = 0;
unsigned long counter = 0;
bool led_on = false;

static void setLed(bool on) {
  led_on = on;
  digitalWrite(LED_BUILTIN, on ? LOW : HIGH);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  setLed(false);

  Serial.begin(BAUD);
  Serial1.begin(BAUD);

  delay(1200);
  Serial.println("VK_RA4M2 LED + USB Serial + UART test");
  Serial.println("USB Serial: Serial at 115200");
  Serial.println("UART pins: Serial1 TX=D1, RX=D0 at 115200");
  Serial.println("For UART echo test, short D1 to D0 and type in Serial Monitor.");

  Serial1.println("VK_RA4M2 UART on D1/TX and D0/RX at 115200");
}

void loop() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    Serial1.write((uint8_t)c);
  }

  while (Serial1.available() > 0) {
    int c = Serial1.read();
    Serial.write((uint8_t)c);
  }

  unsigned long now = millis();
  if (now - last_tick >= TICK_MS) {
    last_tick = now;
    counter++;
    setLed(!led_on);

    Serial.print("tick ");
    Serial.print(counter);
    Serial.print(" led=");
    Serial.println(led_on ? "on" : "off");

    Serial1.print("uart tick ");
    Serial1.println(counter);
  }
}
