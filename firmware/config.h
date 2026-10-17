// Network settings. The defaults work in the Wokwi simulator (and so in WireLab).
// On the real board: put your WiFi here, and a broker the desktop app can reach too, for example
// Mosquitto running on the laptop (MQTT_HOST = the laptop's IP on the same WiFi).
#pragma once

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASS ""

// Public test broker: anyone can read and publish here, so don't send anything private.
#define MQTT_HOST "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASS ""

// Topics are TOPIC_ROOT/DEVICE_ID/...; on a public broker make DEVICE_ID unique (your student ID).
#define TOPIC_ROOT "studysphere"
#define DEVICE_ID "room-01"
