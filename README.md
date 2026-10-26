# ESP32 Multi-Sensor Industrial Hazard Monitor

An IoT early-warning system built on the ESP32. It monitors temperature, humidity, gas and vibration, shows the hazard level on a traffic-light LED module, sounds a buzzer, sends **SMS alerts** through a SIM800L GSM module, and logs all readings to the **ThingSpeak** cloud.

> Problem statement: ECW260004 – Multi-Sensor Industrial Hazard Prediction

## Features

- Four-parameter monitoring: temperature, humidity, gas (MQ sensor) and vibration (SW-420)
- Three alert levels with LED, buzzer and SMS: **NORMAL / WARNING / DANGER**
- **Gas auto-calibration** at startup, so the limit adapts to each MQ sensor
- Gas confirmation (3 readings in a row) and hysteresis to avoid false alarms
- Interrupt-based vibration counting
- SMS alerts with the cause and sensor values, with a 5-minute cooldown per level
- ThingSpeak upload every 20 seconds, sending the **peak** value of each window so short spikes are not lost
- Non-blocking buzzer patterns

## Hardware

| Component | Purpose |
|---|---|
| ESP32 Dev Module | Main controller, WiFi |
| DHT11 | Temperature and humidity |
| MQ gas sensor (MQ-2 / MQ-5 / MQ-135) | Gas / smoke detection |
| SW-420 vibration sensor | Vibration detection |
| Traffic LED module | Visual hazard level |
| Active buzzer | Audible alarm |
| SIM800L GSM module | SMS alerts (2G only) |
| 3.7 to 4.2V supply (Li-ion cell or buck converter) + 1000µF capacitor | Power for SIM800L |

## Pin connections

| Device | Pin | ESP32 |
|---|---|---|
| DHT11 | DATA | GPIO 4 |
| MQ sensor | AO (through voltage divider) | GPIO 34 |
| SW-420 | DO | GPIO 27 |
| Buzzer | + | GPIO 26 |
| Traffic LED | R / Y / G | GPIO 25 / 33 / 32 |
| SIM800L | TXD / RXD | GPIO 16 / GPIO 17 |
| SIM800L | RST | GPIO 5 |

See [docs/wiring.md](docs/wiring.md) for details, power notes and the voltage divider.

## Alert logic

| Status | Condition | LED | Buzzer | SMS |
|---|---|---|---|---|
| NORMAL | No sensor abnormal | Green | Off | None |
| WARNING | One sensor abnormal | Yellow | Short beep every 2 s | "HAZARD WARNING" |
| DANGER | Two or more abnormal, or gas leak | Red | Fast beeping | "HAZARD DANGER" |

Default thresholds (edit in the `.ino`):

| Parameter | Limit |
|---|---|
| Temperature | > 40 °C |
| Humidity | > 85 % |
| Vibration | > 5 pulses per 2 s |
| Gas | clean-air baseline + 400 (auto-calibrated) |

## Setup

1. Install the **ESP32 board package** in the Arduino IDE.
2. Install libraries from the Library Manager: **DHT sensor library** and **Adafruit Unified Sensor**.
3. Copy `IndustrialHazardMonitor/secrets.h.example` to `IndustrialHazardMonitor/secrets.h` and fill in your WiFi name, WiFi password, ThingSpeak Write API key and phone number.
4. Create a ThingSpeak channel with 5 fields (see below).
5. Open `IndustrialHazardMonitor.ino`, select **ESP32 Dev Module**, and upload.
6. Open the Serial Monitor at **115200** baud. Keep the MQ sensor in clean air during the 120-second warm-up.

### ThingSpeak channel fields

| Field | Name |
|---|---|
| 1 | Temperature |
| 2 | Humidity |
| 3 | Gas |
| 4 | Vibration |
| 5 | Status (0 = Normal, 1 = Warning, 2 = Danger) |

## Troubleshooting

| Problem | Fix |
|---|---|
| `SIM800L not responding` | Check TX/RX wiring, common GND and the separate power supply |
| `GSM not registered` | Check antenna, SIM (2G coverage, no PIN lock) and power |
| ESP32 reboots when sending SMS | SIM800L supply cannot deliver about 2A; use a Li-ion cell or buck converter plus capacitor |
| `ThingSpeak FAILED` | Check the API key and WiFi (2.4 GHz only); do not upload faster than every 15 s |
| False gas alarms | Increase `GAS_MARGIN` or `WARMUP_SECONDS`; new MQ sensors need several hours of burn-in |
| No buzzer sound | The buzzer gives one beep at startup; if silent, check its wiring and polarity |

## Project structure

```
ESP32-Industrial-Hazard-Monitor/
├── IndustrialHazardMonitor/
│   ├── IndustrialHazardMonitor.ino
│   └── secrets.h.example
├── docs/
│   └── wiring.md
├── .gitignore
├── LICENSE
└── README.md
```

## Future work

- LoRa (SX1278) link between sensor nodes and a receiver node
- Weighted risk score instead of a simple count
- ThingSpeak React alerts and MATLAB trend prediction
- GPRS upload so the node works without WiFi

## License

MIT License. See [LICENSE](LICENSE).
