# PERWER Solar Monitor ☀️

A real-time IoT solar energy monitoring system built on the ESP32-S3. Reads electrical, thermal and environmental parameters from a solar installation and transmits live data to a Supabase cloud database with a web dashboard deployed on Vercel.

---

## 🖥️ Live Dashboard

**[View Live Dashboard →](https://pewer-project.vercel.app)**

---

## 📡 System Overview

```
Solar Panel & Battery
        │
   [Sensors] ──────────────────────────────────────────┐
        │                                               │
   ESP32-S3                                        16x2 LCD
        │                                          LEDs + Buzzer
        ├── WiFi (primary) ──────────────────────────────────┐
        │                                                     │
        └── A9 GPRS/MTN (fallback) ──────────────────────────┤
                                                             │
                                                      Supabase DB
                                                             │
                                                     Vercel Dashboard
```

---

## ⚡ Features

- **Real-time monitoring** of PV panel, battery and AC inverter output
- **Dual connectivity** — WiFi primary with automatic GPRS/MTN fallback
- **Local display** — 16x2 LCD scrolling through 9 data pages
- **Alert system** — LEDs and buzzer for warning and critical conditions
- **Cloud database** — Supabase with realtime enabled
- **Live dashboard** — Deployed on Vercel, auto-updates every 30 seconds
- **Energy tracking** — Cumulative kWh calculation
- **Auto sensor calibration** — Current sensors zero themselves on every boot

---

## 🔧 Hardware

| Component | Quantity | Purpose |
|-----------|----------|---------|
| ESP32-S3 DevKitC-1 | 1 | Main microcontroller |
| A9 GPRS Module | 1 | GSM/GPRS network fallback |
| ZMPT101B | 1 | AC voltage measurement |
| ACS712 30A | 1 | AC current measurement |
| ACS758 50A | 2 | PV and battery DC current |
| Voltage Sensor 25V | 2 | PV and battery DC voltage |
| DS18B20 (Waterproof) | 2 | Panel and battery temperature |
| DHT22 | 1 | Ambient temperature and humidity |
| LDR Module (4-pin) | 1 | Solar irradiance |
| 16x2 LCD with I2C | 1 | Local display |
| Green LED | 1 | System OK indicator |
| Yellow LED | 1 | Warning indicator |
| Buzzer | 1 | Audio alert |

---

## 📌 Pin Assignment

| GPIO | Sensor | Signal |
|------|--------|--------|
| 1 | ZMPT101B | AC Voltage |
| 2 | ACS712 | AC Current |
| 3 | ACS758 #1 | PV Current |
| 4 | ACS758 #2 | Battery Current |
| 5 | Volt Sensor #1 | PV Voltage |
| 6 | Volt Sensor #2 | Battery Voltage |
| 7 | LDR AO | Irradiance |
| 8 | LCD SDA | I2C Data |
| 9 | LCD SCL | I2C Clock |
| 10 | DS18B20 Bus | Temperatures |
| 11 | DHT22 | Humidity + Ambient Temp |
| 16 | Green LED | System OK |
| 17 | Yellow LED | Warning |
| 18 | Buzzer | Alert |
| 19 | A9 TX → ESP32 RX | UART GPRS |
| 20 | A9 RX → ESP32 TX | UART GPRS |

---

## 📊 Data Points Sent to Supabase

| Field | Unit | Description |
|-------|------|-------------|
| pv_voltage | V | Solar panel voltage |
| pv_current | A | Solar panel current |
| pv_power | W | Solar panel power |
| pv_temperature | °C | Panel surface temperature |
| irradiance | W/m² | Solar irradiance |
| ac_voltage | V | Inverter AC output voltage |
| ac_current | A | Inverter AC output current |
| ac_power | W | Inverter AC apparent power |
| frequency | Hz | AC frequency |
| energy_kwh | kWh | Cumulative energy |
| batt_voltage | V | Battery voltage |
| batt_current | A | Battery current |
| batt_power | W | Battery power |
| batt_temperature | °C | Battery temperature |
| batt_state | — | charging / discharging / idle |
| amb_temperature | °C | Ambient temperature |
| humidity | % | Relative humidity |

---

## 🚀 Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) installed in VS Code
- ESP32-S3 board
- Supabase account
- MTN SIM card (for GPRS fallback)

### Installation

1. Clone the repository:
```bash
git clone https://github.com/Adetayo224/PEWER.git
cd PEWER
```

2. Create `src/secrets.h` with your credentials:
```cpp
#define WIFI_SSID       "your_wifi_name"
#define WIFI_PASSWORD   "your_wifi_password"
#define SUPABASE_KEY    "your_supabase_key"
```

3. Update `src/main.cpp` with your Supabase URL

4. Build and flash:
```bash
pio run --target upload
```

5. Open serial monitor:
```bash
pio device monitor
```

---

## 🗄️ Supabase Setup

1. Create a new Supabase project
2. Create the `device_telemetry` table with all columns listed above
3. Disable RLS on the table
4. Enable Realtime:
```sql
ALTER PUBLICATION supabase_realtime ADD TABLE device_telemetry;
```
5. Insert your device:
```sql
INSERT INTO devices (id) VALUES ('PERWER-001');
```

---

## ⚙️ Calibration

### Voltage Sensors
Adjust these values in `main.cpp` until readings match your multimeter:
```cpp
#define VOLT_PV_CORRECTION    0.80
#define VOLT_BATT_CORRECTION  0.80
```

### ZMPT101B AC Voltage
```cpp
#define ZMPT101B_SENSITIVITY  500.0f
```
Increase if reading too low, decrease if reading too high.

### Current Sensors
Auto-calibrated at every boot — just make sure no current is flowing through the sensors during the 2 second startup calibration window.

---

## 🔔 Alert Thresholds

| Condition | Warning | Critical |
|-----------|---------|----------|
| PV Temperature | 60°C | 75°C |
| Battery Temperature | 45°C | 50°C |
| Battery Voltage Low | 11.5V | 11.0V |
| AC Voltage | < 200V or > 240V | — |

---

## 🌐 Network Connectivity

The device uses a dual connectivity strategy:

1. **WiFi** — primary connection, used whenever available
2. **A9 GPRS (MTN Nigeria)** — automatic fallback when WiFi is unavailable

The LCD page 7 shows the currently active network at all times.

---

## 📁 Project Structure

```
PEWER/
├── src/
│   └── main.cpp          # Main firmware
├── platformio.ini         # PlatformIO config + libraries
├── .gitignore
└── README.md
```

---

## 🛠️ Built With

- [PlatformIO](https://platformio.org/) — Firmware development
- [ZMPT101B Library](https://github.com/Abdurraziq/ZMPT101B-arduino) — AC voltage
- [ACS712 Library](https://github.com/RobTillaart/ACS712) — Current sensing
- [Supabase](https://supabase.com/) — Cloud database
- [Vercel](https://vercel.com/) — Dashboard hosting

---

## 👤 Author

**Adetayo** — Samfred Robotics
## Built for 
**PEWER TEAM**

---

## 📄 License

This project is open source and available under the [MIT License](LICENSE).
