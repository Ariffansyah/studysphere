# StudySphere: Smart Study Room (UTS/UAS IoT)

Node IoT berbasis ESP32 untuk ruang belajar pintar. Node membaca suhu, kelembapan, cahaya, kualitas udara dan kehadiran orang, menyalakan lampu belajar secara otomatis, menampilkan status di LCD, dan berkomunikasi dengan aplikasi desktop lewat **MQTT**. Aplikasi desktop (Tauri, Windows dan Linux) ada di `desktop/` dan sekaligus menjadi broker MQTT di laptop, jadi sistemnya berdiri sendiri tanpa internet. LLM lokal dikerjakan terpisah dan memakai kontrak MQTT di bawah.

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

Broker: aplikasi desktop di laptop, port 1883 (lihat [Aplikasi desktop](#aplikasi-desktop)). Semua topik diawali `studysphere/room-01/` (`TOPIC_ROOT` dan `DEVICE_ID` di `firmware/config.h`).

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

## Konfigurasi

Semua pengaturan jaringan ada di `firmware/config.h`: SSID dan password WiFi, host dan port broker, user dan password MQTT, `TOPIC_ROOT` dan `DEVICE_ID`. `MQTT_HOST` otomatis `127.0.0.1` di simulator lokal WireLab (makro `WIRELAB_SIM`). Untuk board asli, isi dengan IP laptop yang ditampilkan aplikasi desktop.

## Aplikasi desktop

Aplikasi di `desktop/` adalah *island*: pulau hitam kecil yang menggantung di tengah atas layar, dengan maskot burung hantu yang menjaga ruang belajar. Wajah dan kalimat burung hantu mengikuti keadaan ruangan: tidur saat ruangan kosong, senang saat ada yang belajar, pusing saat udara pengap, dan kaget saat node tidak mengirim data.

| Yang dilakukan | Yang terjadi |
|---|---|
| Arahkan mouse ke garis kecil di tengah atas layar | Island muncul satu baris: suhu, kelembapan, CO2 dan lampu |
| Klik island | Island terbuka: semua data, mode lampu (auto, on, off) dan pesan untuk LCD |
| `Esc`, atau mouse keluar | Island menutup lagi (8 detik setelah terbuka, 20 detik setelah satu baris) |
| Udara pengap atau node putus | Island terbuka sendiri |
| Tombol **Quit** di island yang terbuka | Menutup aplikasi |

Saat dibuka, aplikasi menjalankan broker MQTT sendiri di port 1883 pada semua interface laptop, jadi tidak perlu Mosquitto atau internet. Kalau port 1883 sudah dipakai broker lain (misalnya Mosquitto), aplikasi memakai broker itu. Aplikasi mengikuti `DEVICE_ID` apa pun yang mengirim telemetry.

Di Linux, island memakai layer-shell supaya tetap di atas layar (KDE, Hyprland, Sway, COSMIC). Pasang `gtk-layer-shell` (Arch) atau `libgtk-layer-shell0` (Debian/Ubuntu). Tanpa itu, atau di GNOME, island menjadi jendela biasa yang selalu di atas, dan posisinya ditentukan oleh desktop.

Syarat untuk build: [Rust](https://rustup.rs), Node.js, dan prasyarat Tauri untuk sistemnya ([daftar lengkap](https://v2.tauri.app/start/prerequisites/)): di Linux `webkit2gtk-4.1`, di Windows Microsoft C++ Build Tools (WebView2 sudah ada di Windows 10 dan 11).

```sh
cd desktop
npm install
npm run tauri dev     # jalankan
npm run tauri build   # installer: .msi dan .exe di Windows, .deb, .rpm dan .AppImage di Linux
```

Installer Windows harus di-build di Windows (Tauri tidak bisa cross-compile dari Linux). Hasilnya ada di `desktop/src-tauri/target/release/bundle/`.

Supaya ESP32 asli bisa tersambung ke laptop:

- ESP32 dan laptop harus di WiFi yang sama. WiFi kampus sering memblokir koneksi antar perangkat; kalau begitu, pakai hotspot HP atau hotspot laptop.
- Windows menanyakan izin firewall saat aplikasi pertama kali dibuka. Izinkan untuk jaringan **Private**, lalu pastikan WiFi-nya diset sebagai Private. Di Linux dengan ufw: `sudo ufw allow 1883/tcp`.
- Broker tidak memakai password, jadi siapa pun di WiFi yang sama bisa membaca dan mengirim command. Untuk presentasi di jaringan bersama, pakai hotspot sendiri.

## Menjalankan

### Di simulator WireLab (lokal, tanpa kuota)

Buka aplikasi desktop, lalu buka folder ini di WireLab dan tekan **Run**. Proyek ini memakai **simulator lokal** WireLab (`"engine": "local"` di `wirelab.json`):

- Firmware di-compile untuk PC dan berjalan di PC, jadi tidak memakai server atau kuota Wokwi.
- WiFi memakai jaringan PC, dan node tersambung ke broker aplikasi desktop di `127.0.0.1:1883`.
- Sensor diatur dengan mengklik part saat simulasi berjalan: slider cahaya (LDR), CO2 (MQ-135), suhu dan kelembapan (DHT11), serta tombol gerakan (PIR). LED dan LCD ikut menampilkan hasilnya.
- Syaratnya: compiler C++ (`g++` atau `clang++`) dan arduino-cli.

Simulator lokal mensimulasikan perilaku, bukan chip-nya: timing sinyal DHT11 dan I2C LCD tidak disimulasikan (library-nya diganti versi tiruan yang memakai nilai dari WireLab). Untuk menguji di mesin Wokwi, ganti simulator di **Settings** (memakai kuota CI: akun Free mendapat 50 menit per 30 hari), atau jalankan perintah di bawah. Mesin Wokwi berjalan di server Wokwi, jadi tidak bisa menjangkau broker di laptop; tes ini hanya memakai Serial.

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir firmware/build firmware
wokwi-cli --scenario test.scenario.yaml --timeout 60000 .
```

### Di ESP32 asli

1. Pasang library (sekali saja):
   ```sh
   arduino-cli lib install "DHT sensor library" "Adafruit Unified Sensor" "LiquidCrystal I2C" PubSubClient ArduinoJson
   ```
2. Isi WiFi kamu dan `MQTT_HOST` (IP laptop, ditampilkan aplikasi desktop) di `firmware/config.h`, dan ubah `volts()` untuk board asli (lihat bagian wiring).
3. Compile, upload, lalu lihat serial monitor (ganti `/dev/ttyUSB0` sesuai port ESP32):
   ```sh
   arduino-cli compile --fqbn esp32:esp32:esp32 firmware
   arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware
   arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
   ```
4. Buka aplikasi desktop. Serial monitor menampilkan `MQTT connected` setelah node tersambung.

## Mengetes MQTT tanpa aplikasi desktop

Selama aplikasi desktop terbuka, broker-nya bisa dipakai alat lain. Pakai [MQTT Explorer](https://mqtt-explorer.com/) (GUI), atau `mosquitto_sub` / `mosquitto_pub` dari paket mosquitto:

```sh
# lihat semua pesan dari node
mosquitto_sub -h localhost -t 'studysphere/room-01/#' -v

# minta data seperti yang akan dilakukan LLM
mosquitto_pub -h localhost -t studysphere/room-01/request \
  -m '{"task":"comfort_check","required_information":["temperature","humidity","co2"]}'

# atur lampu dan LCD
mosquitto_pub -h localhost -t studysphere/room-01/command -m '{"lamp":"on"}'
mosquitto_pub -h localhost -t studysphere/room-01/command -m '{"lcd":"Waktunya istirahat"}'
```

## Struktur folder

```text
firmware/firmware.ino   program ESP32
firmware/config.h       WiFi, broker MQTT, topik
desktop/                aplikasi desktop dan broker MQTT (Tauri)
diagram.json            rangkaian (WireLab / Wokwi)
wokwi.toml              lokasi firmware dan custom chip untuk simulator
chips/                  DHT11, MQ-135 dan HC-SR501 untuk simulator
test.scenario.yaml      tes otomatis lewat Serial di simulator
libraries.txt           daftar library Arduino
```
