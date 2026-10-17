// Reads every sensor once a second and prints one JSON line on Serial.
// WireLab shows the latest line next to its serial monitor.
#include <Arduino.h>
#include <DHT.h>  // "DHT sensor library" by Adafruit

const int PIR_PIN = 27;
const int LDR_AO = 34, LDR_DO = 25;
const int MQ_AO = 35, MQ_DO = 26;
const int DHT_PIN = 4;  // DHT11 DATA

// Calibration knobs. Defaults match the sim; tune them against a reference on real hardware.
const float LDR_VCC = 5.0, LDR_GAMMA = 0.7, LDR_RL10 = 50;  // RL10: kOhm at 10 lux
const float MQ_VCC = 5.0, MQ_RLOAD = 10.0, MQ_RZERO = 76.63;  // kOhm; RZERO from clean-air calibration

DHT dht(DHT_PIN, DHT11);

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
  dht.begin();
  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_DO, INPUT);
  pinMode(MQ_DO, INPUT);
}

void loop() {
  float temp = dht.readTemperature();  // the library re-reads the sensor at most every 2 s
  float hum = dht.readHumidity();
  char th[40] = "\"temp\":null,\"hum\":null";
  if (!isnan(temp) && !isnan(hum)) snprintf(th, sizeof th, "\"temp\":%.1f,\"hum\":%.1f", temp, hum);

  Serial.printf("{%s,\"lux\":%.0f,\"ldr_do\":%d,\"co2\":%.0f,\"mq_do\":%d,\"pir\":%d}\n",
                th, readLux(), digitalRead(LDR_DO), readCo2(), digitalRead(MQ_DO), digitalRead(PIR_PIN));
  delay(1000);
}
