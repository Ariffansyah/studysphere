// Reads every sensor once a second and prints one JSON line on Serial.
// The WireLab app (src-tauri) parses these lines.
#include <Arduino.h>

const int PIR_PIN = 27;
const int LDR_AO = 34, LDR_DO = 25;
const int MQ_AO = 35, MQ_DO = 26;
const int RS485_RX = 16, RS485_TX = 17, RS485_DE = 4;  // DE and RE tied together
const uint8_t MD02_ADDR = 1;

// Calibration knobs. Defaults match the sim; tune them against a reference on real hardware.
const float LDR_VCC = 5.0, LDR_GAMMA = 0.7, LDR_RL10 = 50;  // RL10: kOhm at 10 lux
const float MQ_VCC = 5.0, MQ_RLOAD = 10.0, MQ_RZERO = 76.63;  // kOhm; RZERO from clean-air calibration

uint16_t crc16(const uint8_t *buf, int len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

// Modbus RTU: function 0x04, read 2 input registers from 0x0001 (temp x10, RH x10).
bool readMd02(float &temp, float &hum) {
  uint8_t req[8] = {MD02_ADDR, 0x04, 0x00, 0x01, 0x00, 0x02};
  uint16_t crc = crc16(req, 6);
  req[6] = crc & 0xFF;
  req[7] = crc >> 8;

  while (Serial2.available()) Serial2.read();
  digitalWrite(RS485_DE, HIGH);
  Serial2.write(req, sizeof req);
  Serial2.flush();
  digitalWrite(RS485_DE, LOW);

  uint8_t res[9];
  if (Serial2.readBytes(res, sizeof res) != sizeof res) return false;
  if (res[0] != MD02_ADDR || res[1] != 0x04 || crc16(res, sizeof res) != 0) return false;
  temp = (int16_t)(res[3] << 8 | res[4]) / 10.0;
  hum = (res[5] << 8 | res[6]) / 10.0;
  return true;
}

// Wokwi's ESP32 ADC maps 0-5 V to 0-4095. On a real board use analogReadMilliVolts(pin) / 1000.0,
// and put a divider on 5 V module outputs: the ESP32 pin tops out around 3.1 V.
float volts(int pin) { return analogRead(pin) * 5.0 / 4095; }

float readLux() {
  float v = volts(LDR_AO);
  float r = 10000 * v / (LDR_VCC - v);  // LDR is the low side of a 10k divider
  return pow(LDR_RL10 * 1e3 * pow(10, LDR_GAMMA) / r, 1 / LDR_GAMMA);
}

float readCo2() {
  float v = volts(MQ_AO);
  float rs = MQ_RLOAD * (MQ_VCC - v) / v;
  return 116.6020682 * pow(rs / MQ_RZERO, -2.769034857);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
  Serial2.setTimeout(200);
  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_DE, LOW);
  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_DO, INPUT);
  pinMode(MQ_DO, INPUT);
}

void loop() {
  float temp, hum;
  char th[40] = "\"temp\":null,\"hum\":null";
  if (readMd02(temp, hum)) snprintf(th, sizeof th, "\"temp\":%.1f,\"hum\":%.1f", temp, hum);

  Serial.printf("{%s,\"lux\":%.0f,\"ldr_do\":%d,\"co2\":%.0f,\"mq_do\":%d,\"pir\":%d}\n",
                th, readLux(), digitalRead(LDR_DO), readCo2(), digitalRead(MQ_DO), digitalRead(PIR_PIN));
  delay(1000);
}
