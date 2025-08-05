# 🌬️ ESP32 Smart Fan Controller with OLED, FSM, Logging & Time Sync

This project is a compact yet feature-rich fan controller built on the ESP32 platform, with an OLED display, a humidity/temperature sensor, time-synchronized logging, and custom finite state machine (FSM) logic.

Designed for reliability, offline functionality, and full transparency, it’s perfect for DIY air quality, ventilation, or climate control projects.

![Image](https://github.com/user-attachments/assets/f3608a15-1333-4bac-b3aa-3f61265ff9aa)

---

## 🧰 Features

### 🧱 Hardware
- **ESP32** microcontroller
- **AHT10** humidity + temperature sensor
- **5V Relay module** for fan control
- **OLED I2C display** (e.g. SSD1306)
- **Push button** for manual override
- Optional: onboard LED indicator

### ⚙️ Software / Logic
- ✅ **Finite State Machine (FSM)** with 4 smart modes:
  - `IDLE`, `COOLING`, `WAITING`, `FORCE`
- ✅ **Humidity-based automation**
- ✅ **Manual override** (fan on/off via button)
- ✅ **OLED UI with two pages**:
  - Live state (fan, humidity, timer)
  - Log view with pagination
- ✅ **Logs stored in flash** (via NVS)
  - Tracks state transitions and humidity
  - Shows real-world time if synced, or `+HH:MM:SS` since boot
- ✅ **Time sync over Wi-Fi**
  - Automatically connects to known hotspot (e.g. your phone)
  - Syncs time via NTP (SNTP)
  - Applies local timezone (e.g. GMT+2 with DST)

---

## 🕹 UI Behavior

- First screen: live fan state, humidity, timer
- Second screen: logs (4 per page) + pagination `[1 / N]`
- Screens auto-rotate (3s first pages, 1s for others)
- Clean formatting for readability on small screen

---

## 🧠 FSM Logic (Simplified)

| State     | Trigger                    | Action           |
|-----------|----------------------------|------------------|
| `IDLE`    | Humidity > 70%             | → `COOLING`      |
|           | No high humidity for 6 hrs | → `FORCE`        |
| `COOLING` | 30 min passed OR dry air   | → `WAITING`      |
| `WAITING` | 2 hours passed             | → `IDLE`         |
| `FORCE`   | 30 min passed              | → `IDLE`         |

---

## 🕓 Time Management

- Uses SNTP via Wi-Fi
- Automatically connects to phone hotspot if available
- Sets timezone to Central European Time (CEST / GMT+2)
- Falls back to uptime-based logs when offline
- Clean switching between real time and fallback mode

---

## 🧪 Development Notes

- Logs are stored using NVS (non-volatile flash)
- Old logs are automatically rotated (FIFO)
- First 20s after boot are ignored for noise filtering
- Time sync is only attempted when Wi-Fi connects
- Safe to run offline indefinitely

---

## 📦 Future Ideas

- Long-press to clear logs
- LED for Wi-Fi or fan state
- Web interface for live display
- More sensors (CO₂, motion, etc.)

---

## 🛠 Build & Flash

ESP-IDF required (v5.1 recommended)

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

