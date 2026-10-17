# StudySphere: Smart Study Room (UTS/UAS IoT)

Node IoT berbasis ESP32 untuk ruang belajar pintar. Node membaca suhu, kelembapan, cahaya, kualitas udara dan kehadiran orang, menyalakan lampu belajar secara otomatis, menampilkan status di LCD, dan berkomunikasi dengan aplikasi desktop lewat **MQTT**. Aplikasi desktop (dengan LLM lokal) dikerjakan terpisah; README ini menjelaskan sisi IoT dan kontrak MQTT yang akan dipakai aplikasi itu.

## Komponen dan wiring

| Komponen | Pin modul | ESP32 | Daya |
|---|---|---|---|
| DHT11 (suhu & kelembapan) | DATA | GPIO4 | 3V3 |
| Modul LDR (LM393) | AO | GPIO34 | 5V |
| MQ-135 (kualitas udara / CO2) | AO | GPIO35 | 5V |
| HC-SR501 PIR (kehadiran) | OUT | GPIO27 | 5V |
| LCD 16x2 I2C (alamat 0x27) | SDA, SCL | GPIO21, GPIO22 | 5V |
| LED lampu belajar + resistor 220 Ω | anoda lewat resistor | GPIO13 | |

Semua GND disambung ke GND ESP32. Rangkaiannya ada di `diagram.json` dan bisa dibuka di WireLab atau Wokwi.

Untuk perangkat asli:

- Output analog modul 5 V bisa sampai 5 V, sedangkan pin ESP32 maksimal sekitar 3,1 V. Pakai pembagi tegangan (misalnya 10 kΩ dan 20 kΩ) atau beri daya modul dari 3V3.
- Di simulator, ADC ESP32 membaca 0 sampai 5 V sebagai 0 sampai 4095, jadi `volts()` di firmware memakai `analogRead(pin) * 5.0 / 4095`. Di board asli, ganti dengan `analogReadMilliVolts(pin) / 1000.0` dan sesuaikan dengan pembagi tegangan.
- Konstanta kalibrasi LDR dan MQ-135 (`LDR_RL10`, `MQ_RZERO` dan seterusnya) ada di atas `firmware.ino`. `MQ_RZERO` sebaiknya dikalibrasi di udara bersih.
- Modul DHT11 tiga pin sudah punya resistor pull-up. Untuk DHT11 empat pin tanpa modul, tambahkan 10 kΩ dari DATA ke 3V3.

## Cara kerja

- Sensor dibaca setiap 2 detik. Telemetry dikirim setiap 5 detik, dan langsung saat kehadiran atau lampu berubah.
- **Kehadiran:** PIR mendeteksi gerakan. Sesi belajar dimulai saat ada gerakan dan berakhir setelah 60 detik tanpa gerakan (`IDLE_MS`).
- **Lampu belajar:** mode `auto` menyalakan lampu saat ada orang dan cahaya di bawah 150 lux (`DARK_LUX`). Mode `on` atau `off` bisa dipilih dari aplikasi desktop.
- **Kualitas udara:** CO2 di atas 1000 ppm (`CO2_STALE`) dianggap `stale`, artinya ruangan perlu dibuka atau diberi ventilasi.
- **LCD:** baris 1 berisi suhu, kelembapan dan cahaya (`25.0C 50%  500lx`). Baris 2 berisi CO2 dan status koneksi (`CO2  400ppm MQTT`; `WIFI` berarti WiFi tersambung tapi broker belum, `----` berarti offline). Pesan dari aplikasi desktop tampil di LCD selama 30 detik, maksimal 32 karakter untuk dua baris.

## MQTT

Broker bawaan: `broker.hivemq.com:1883` (broker publik untuk uji coba). Semua topik diawali `studysphere/room-01/` (`TOPIC_ROOT` dan `DEVICE_ID` di `firmware/config.h`).

| Topik | Arah | Isi |
|---|---|---|
| `studysphere/room-01/telemetry` | node ke desktop | Semua data, setiap 5 detik dan saat ada perubahan |
| `studysphere/room-01/request` | desktop ke node | Semantic selector: data apa yang diminta |
| `studysphere/room-01/response` | node ke desktop | Jawaban untuk request |
| `studysphere/room-01/command` | desktop ke node | Mengatur lampu dan LCD |
| `studysphere/room-01/status` | node ke desktop | `online` atau `offline` (retained; `offline` dikirim broker sebagai last will saat node putus) |

**Telemetry**

```json
{"temperature":25,"humidity":50,"lighting":500,"co2":400,"air_quality":"good","occupancy":false,"session_duration":0,"lamp":"off","lamp_mode":"auto"}
```

**Request dan response.** Formatnya sama dengan proyek semanticiot, jadi LLM yang sudah dilatih dengan format itu bisa langsung dipakai:

```json
{"task":"comfort_check","required_information":["temperature","humidity","noise"]}
```

```json
{"task":"comfort_check","data":{"temperature":25,"humidity":50},"unknown":["noise"]}
```

**Command:**

```json
{"lamp":"on"}
{"lamp":"auto"}
{"lcd":"Waktunya istirahat 5 menit"}
```

Setelah command lampu, node langsung mengirim telemetry baru sebagai konfirmasi.

**Nama informasi yang dikenal node:**

| Nama | Satuan / nilai |
|---|---|
| `temperature` | °C |
| `humidity` | % RH |
| `lighting` | lux |
| `co2` | ppm (perkiraan dari MQ-135) |
| `air_quality` | `good` atau `stale` |
| `occupancy` | `true` / `false` |
| `session_duration` | detik sejak sesi belajar dimulai |
| `lamp` | `on` / `off` |
| `lamp_mode` | `auto`, `on` atau `off` |

Nama lain dikembalikan di `unknown`. Request dan command yang sama juga bisa dikirim lewat Serial, satu baris JSON per perintah.

Broker publik bisa dibaca dan ditulisi siapa saja. Untuk presentasi, ganti `DEVICE_ID` dengan sesuatu yang unik (misalnya NIM), atau pakai broker sendiri seperti di bawah.

## Konfigurasi

Semua pengaturan jaringan ada di `firmware/config.h`: SSID dan password WiFi, host dan port broker, user dan password MQTT, `TOPIC_ROOT` dan `DEVICE_ID`. Nilai bawaannya untuk simulator (WiFi `Wokwi-GUEST`).

## Menjalankan

### Di simulator WireLab (lokal, tanpa kuota)

Buka folder ini di WireLab lalu tekan **Run**. Proyek ini memakai **simulator lokal** WireLab (`"engine": "local"` di `wirelab.json`):

- Firmware di-compile untuk PC dan berjalan di PC, jadi tidak memakai server atau kuota Wokwi.
- WiFi memakai koneksi internet PC, jadi MQTT tersambung ke broker sungguhan dan aplikasi desktop bisa langsung menerima datanya.
- Sensor diatur dengan mengklik part saat simulasi berjalan: slider cahaya (LDR), CO2 (MQ-135), suhu dan kelembapan (DHT11), serta tombol gerakan (PIR). LED dan LCD ikut menampilkan hasilnya.
- Syaratnya: compiler C++ (`g++` atau `clang++`) dan arduino-cli.

Simulator lokal mensimulasikan perilaku, bukan chip-nya: timing sinyal DHT11 dan I2C LCD tidak disimulasikan (library-nya diganti versi tiruan yang memakai nilai dari WireLab). Untuk menguji di mesin Wokwi, ganti simulator di **Settings** (memakai kuota CI: akun Free mendapat 50 menit per 30 hari), atau jalankan:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir firmware/build firmware
wokwi-cli --scenario test.scenario.yaml --timeout 60000 .
```

### Di ESP32 asli

1. Pasang library (sekali saja):
   ```sh
   arduino-cli lib install "DHT sensor library" "Adafruit Unified Sensor" "LiquidCrystal I2C" PubSubClient ArduinoJson
   ```
2. Isi WiFi kamu di `firmware/config.h`, dan ubah `volts()` untuk board asli (lihat bagian wiring).
3. Compile, upload, lalu lihat serial monitor (ganti `/dev/ttyUSB0` sesuai port ESP32):
   ```sh
   arduino-cli compile --fqbn esp32:esp32:esp32 firmware
   arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware
   arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
   ```

**Broker sendiri di laptop (Mosquitto).** Lebih aman dan tidak bergantung internet.

1. Pasang Mosquitto (`sudo pacman -S mosquitto`, atau `sudo apt install mosquitto mosquitto-clients`).
2. Tambahkan ke `/etc/mosquitto/mosquitto.conf`:
   ```text
   listener 1883 0.0.0.0
   allow_anonymous true
   ```
3. Jalankan `sudo systemctl restart mosquitto`.
4. Isi `MQTT_HOST` dengan IP laptop di jaringan WiFi yang sama.

## Mengetes MQTT tanpa aplikasi desktop

Pakai [MQTT Explorer](https://mqtt-explorer.com/) (GUI), atau `mosquitto_sub` / `mosquitto_pub` dari paket mosquitto:

```sh
# lihat semua pesan dari node
mosquitto_sub -h broker.hivemq.com -t 'studysphere/room-01/#' -v

# minta data seperti yang akan dilakukan LLM
mosquitto_pub -h broker.hivemq.com -t studysphere/room-01/request \
  -m '{"task":"comfort_check","required_information":["temperature","humidity","co2"]}'

# atur lampu dan LCD
mosquitto_pub -h broker.hivemq.com -t studysphere/room-01/command -m '{"lamp":"on"}'
mosquitto_pub -h broker.hivemq.com -t studysphere/room-01/command -m '{"lcd":"Waktunya istirahat"}'
```

## Struktur folder

```text
firmware/firmware.ino   program ESP32
firmware/config.h       WiFi, broker MQTT, topik
diagram.json            rangkaian (WireLab / Wokwi)
wokwi.toml              lokasi firmware dan custom chip untuk simulator
chips/                  DHT11, MQ-135 dan HC-SR501 untuk simulator
test.scenario.yaml      tes otomatis lewat Serial di simulator
libraries.txt           daftar library Arduino
```
