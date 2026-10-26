# Wiring Guide

## Full pin table

| Device | Device pin | ESP32 pin | Notes |
|---|---|---|---|
| DHT11 | VCC | 3.3V | |
| DHT11 | GND | GND | |
| DHT11 | DATA | GPIO 4 | 10kΩ pull-up to 3.3V if the module has none |
| MQ sensor | VCC | 5V (VIN) | Needs 2 to 3 minutes warm-up |
| MQ sensor | GND | GND | |
| MQ sensor | AO | GPIO 34 | **Through a voltage divider** (see below) |
| SW-420 | VCC | 3.3V | |
| SW-420 | GND | GND | |
| SW-420 | DO | GPIO 27 | Adjust sensitivity with the potentiometer |
| Buzzer | + | GPIO 26 | Active buzzer |
| Buzzer | - | GND | |
| Traffic LED | GND | GND | |
| Traffic LED | R | GPIO 25 | 220Ω resistor per LED if the module has none |
| Traffic LED | Y | GPIO 33 | |
| Traffic LED | G | GPIO 32 | |
| SIM800L | TXD | GPIO 16 | |
| SIM800L | RXD | GPIO 17 | Optional 1kΩ/2kΩ divider |
| SIM800L | RST | GPIO 5 | |
| SIM800L | GND | GND | **Common ground with ESP32** |
| SIM800L | VCC | Separate 3.7 to 4.2V supply | **Not from the ESP32** |

## MQ sensor voltage divider

The MQ sensor runs at 5V, so its analog output can reach 5V, but ESP32 ADC pins accept a maximum of 3.3V.

```
MQ AO ──[ 10kΩ ]──┬── GPIO 34
                  │
               [ 20kΩ ]
                  │
                 GND
```

## SIM800L power

- Needs 3.7 to 4.2V and up to 2A during transmission
- Use a single 18650 Li-ion cell or an LM2596 buck converter set to about 4.0V
- Place a 1000µF capacitor across SIM800L VCC and GND
- Connect an antenna, otherwise the module will not register on the network
- SIM800L is **2G only**: the SIM must be from a carrier that still runs 2G in your area, with the PIN lock disabled
- Network LED: fast blink = searching, one blink every 3 seconds = registered

## ADC and WiFi note

The MQ sensor is on GPIO 34 (ADC1), which keeps working while WiFi is active. ADC2 pins cannot be read when WiFi is on, so do not move the analog sensor to an ADC2 pin.

## Suggested photo

Add a photo of your finished circuit as `docs/circuit.jpg` and link it in the README.
