# Eutronic Air Quality Sensor

I got frustrated on the buzzer of Eurtonic Luftgütesensor which I bought quite long ago.
Later revision of the hardware has the functionality to turn off the buzzer, but my version has no functionality to turn off the buzzer.
As a result, I opened up the device and replaced the CO2 sensor to NLIR-based Technoline WL 1030, and kept this as a secondary sensor.
Current work in progress is reverse engineering hardware details and how to integrate it in the Home Assistant.

## Hardware

* Main microcontroller: [STM8L052C6T6](https://www.st.com/en/microcontrollers-microprocessors/stm8l052c6.html)
* Environment sensor: [Sensirion SGP30](https://sensirion.com/products/catalog/SGP30) (VOC and eCO2), [Sensirion SHTC3](https://sensirion.com/products/catalog/SHTC3) (temperature and humidity)
* Buzzer: [Keliking KLJ-8530-3627](https://www.keliking.com/KLJ-8530-3627-SMD-Magnetic-Buzzer-pd6696765.html)
* Power supply through microUSB

SHTC3 is 3.3 V part, while SGP30 is 1.8 V part. Both sensors communicate through I2C, and uses standard I2C address for those sensors (`0x58` for SGP30, `0x70` for SHTC3)
There are 5 LEDs indicating the eCO2 and VOC level, which is connected to the STM8 through individual GPIO.
On the back side (where button is mounted), there are total 8 test pads divided into 4 parallel to the USB connector/4 orthogonal to it.

Group 1 from left to right:

* VBUS (5 V when measured)
* SCK (connected to STM8 PC1, 3.3 V)
* SDA (connected to STM8 PC0, 3.3 V)
* VSGP30 (1.8 V when measured)

Group 2 from top to bottom:

* VSHTC3 (3.3 V when measured)
* SWIM (connected to STM8 PA0)
* RST (connected to STM8 PA1)
* GND

Other STM8 pinmap:

* PE3: Button
* PE7: Buzzer control
* PC5: OSC32_IN
* PC6: OSC32_OUT
* PE5: CO2 LED, Red
* PD0: CO2 LED, Orange
* PD1: CO2 LED, Yellow
* PD2: CO2 LED, Yellow-Green
* PD3: CO2 LED, Green
* PB4: VOC LED, Red
* PB3: VOC LED, Orange
* PB2: VOC LED, Yellow
* PB1: VOC LED, Yellow-Green
* PB0: VOC LED, Green

TODO: a complete circuit diagram

## I2C Sniffing

Connect SCK, SDA, GND to the logic analyzer. Although SGP30 and SHTC3 has different signal levels, the I2C signal will be on 3.3 V.
On the stock firmware, the following I2C transaction is conducted for each second.

| Address | Chipset | R/W | Payload | Description |
|---------|---------|-----|---------|-------------|
| `0x70`  | SHTC3   | W   | `35 17` | Wakeup |
| `0x70`  | SHTC3   | W   | `5C 24` | Get measurement |
| `0x70`  | SHTC3   | R   | `64 2A 0A 68 D9 6F` | Humidity 2b, CRC 1b, Temperature 2b, CRC 1b |
| `0x70`  | SHTC3   | W   | `B0 98` | Sleep |
| `0x58`  | SGP30   | W   | `20 61 64 2A 0A` | Write humidity compensation: Absolute humidity 2b, CRC 1b |
| `0x58`  | SGP30   | W   | `20 08` | Get measurement |
| `0x58`  | SGP30   | R   | `02 C1 2E 00 62 58` | eCO2 PPM 2b, CRC 1b, TVOC PPB 2b, CRC 1b |

In this table, SHT30 reported humidity RH 39.1%, temperature 26.7C.
SGP30 reported eCO2 705 ppm, TVOC 98 ppb.
According to Claude, the raw value coming from SHTC3 (relative humidity) is directly written to the SGP30's humidity compensation, which is not expected by the SGP30 (absolute humidity) and affects the CO2 level calculation of the stock device.
This causes absolute humidity reporting to be about 10 times larger than what it should be.
In this example, `64 2A` means RH 39.1% and absolute humidity is 9.9 g/m^3.
The raw bytes were written in the SGP30, and it is interpreted as 100.2 g/m^3, about 10 times larger than the real measurement.

Claude provided following two strategies to integrate it into the Home Assistant instance.

## I2C Sniffer with Raspberry Pi Pico W

The application under `i2c_sniffer` folder implements a passive I2C sniffer running on a Raspberry Pi Pico W.
It does not touch STM8's behavior, requires soldering on only 3 pads.
PIO engine is used to sniff the I2C connumication, keep the STM8 as I2C master and RPi Pico only listens on it.
Requires a Raspberry Pi Pico SDK installed, and the environment variable `PICO_SDK_PATH` needs to be set for building.
Follow the usual CMake build routine to build the `i2c_sniffer.uf2` file and install on the Raspberry Pi Pico W.

| Sensor Pad | Pico Pin |
|------------|----------|
| SCK        | GP5      |
| SDA        | GP4      |
| GND        | Any GND  |

In the next iteration, an optional sensor logging to SD will be implemented.

## STM8 Replacement through Raspberry Pi Pico W

Can customize the default behavior, but requires soldering in way more test points. None of the LEDs have corresponding test point, a fine soldering skill is required.
