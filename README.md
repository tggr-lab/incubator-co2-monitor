# DIY cell culture CO₂ monitor

We built our own CO₂ monitor for our cell culture incubator. It was fun, and you can try too.

![The monitor reading 4.95 % CO₂, status NORMAL](docs/photos/reading.jpg)

**[Read the build guide](docs/BUILD_GUIDE.md)** for the parts list, wiring map, build steps, calibration and ten days of real data.
The same guide is an [interactive page](https://tggr-lab.github.io/incubator-co2-monitor/) with animated diagrams and hover charts.

It measures CO₂ from 0 to 10 % and temperature, shows both on a small touch
screen, keeps a history, logs to an SD card, and serves a web page over Wi-Fi.

## Why build one

- **The parts are cheap.** The whole build costs a small fraction of a commercial incubator CO₂ monitor.
- **Ready-made meters will not do.** Most stop at 5,000 ppm. An incubator runs at 50,000 ppm.
- **It is not tied to one board.** The sensor needs a single input pin, so any ESP32 will do, with or without a screen.
- **You can extend it.** Other sensors, such as humidity, can be added.

## Read this first

This is a home-built instrument, not a certified one. Out of the box our sensor
read about a third too high inside the incubator. **Calibrate it against a known
incubator or a trusted meter before you rely on it**, and check it again from
time to time. Do not use it as the only safeguard for anything that matters.

## Build it

Three parts and six wires. The [build guide](docs/BUILD_GUIDE.md) covers each step with photos.

![Wiring map](docs/wiring-map.svg)

| Part | Role |
|---|---|
| ESP32-2432S028R display board | Controller, screen, SD slot, Wi-Fi |
| Winsen MH-Z16, 0–10 % vol | CO₂ sensor, read over its PWM output |
| DS18B20 waterproof probe | Temperature |

## Flash the firmware

You need [arduino-cli](https://arduino.github.io/arduino-cli/) and the ESP32 core, version 3.3.11 or later.

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install "LovyanGFX" "OneWire" "DallasTemperature"
cd firmware/CO2_CYD
./build.sh upload
```

The serial port defaults to `/dev/ttyUSB0`. Set `CO2_CYD_PORT` to use another.

## Make it yours

Everything you are likely to change is in `firmware/CO2_CYD/Config.h`.

| Setting | What it does |
|---|---|
| `CO2_CAL_GAIN` | Your calibration factor. Ships as 1.0, meaning uncorrected |
| `CO2_LOW_PCT`, `CO2_HIGH_PCT` | The band that counts as normal |
| `TEMP_LOW_C`, `TEMP_HIGH_C` | The temperature band |
| `LAB_NAME` | The name in the header of every page |
| `NET_AP_PASS` | Password of the monitor's own Wi-Fi network. **Change it** |

To use your own logo:

```bash
tools/png2logo.py yourlogo.png -o firmware/CO2_CYD/Logo.h -W 26 -H 26 --dark
tools/png2logo.py yourlogo.png -o firmware/CO2_CYD/LogoBoot.h -W 100 -H 100 --crop auto --dark --name LOGO_BOOT
```

## What is in here

| Path | Contents |
|---|---|
| `firmware/CO2_CYD/` | The Arduino sketch |
| `docs/` | Build guide, photos, charts and the data behind them |
| `tools/` | Logo converter, screenshot grabber, Wi-Fi setup, log summariser, guide generator |
| `assets/` | Logo artwork |

## Using a different board

The CO₂ reader is in `Co2Pwm.h` and `Co2Pwm.cpp` and depends on nothing else in
the project. Give it any input pin. The display code is specific to the
ESP32-2432S028R and can be left out.

## Licence

Everything here is open source.

| What | Licence |
|---|---|
| Firmware and tools | [MIT](LICENSE) |
| Guide, photos, diagrams, charts and data | [CC BY 4.0](docs/LICENSE) |

Use it, change it, share it. Credit the TGGR Lab when you reuse the guide or photos.
The firmware shows our lab name and logo. Please swap in your own for your build.

## Credits

Built at the TGGR Lab, the Translational Genetics and Genomics Research Laboratory.
Firmware and guide by YAMIR.
