# PicoMesh (MeshCore Companion Pager)
- PicoMesh is a Portable  MeshCore companion radio with Raspberry Pi Pico and the Waveshare SX1262 LoRa 32 Radio.
- A standalone MeshCore companion pager with it's own custom terminal style tiny UI for affordable off-grid mesh comms.
- Read about [MeshCore](https://github.com/meshcore-dev/MeshCore/blob/main/README.md) and its capabilities.
## Hardware specifications:
- Raspberry Pi - Pico 1 RP2040 microprocessor.
- Waveshare SX1262 LoRa 32 Radio Module.
- Compatible with ST7789 (240 X 240) and ST7896S (480 X 320) displays.
- M5 stack Card KB V.1 keyboard.
- DS3231 RTC module to keep UI time.
- MicroSD card module for added flash and storage.(Many ST7896S displays come with an embedded SD card reader)

## Wiring and Connection chart:

| Waveshare<br>SX1262 | RaspberryPi<br>Pico 1 |
| :-----------------: | :-------------------: |
|         3V3         |        3V3 OUT        |
|         GND         |          GND          |
|        BUSY         |        GPIO 02        |
|         CS          |        GPIO 03        |
|         SCK         |        GPIO 10        |
|        MOSI         |        GPIO 11        |
|        MISO         |        GPIO 12        |
|        RESET        |        GPIO 15        |
|         DIO         |        GPIO 20        |

| ST7796S /<br>ST7789 | RaspberryPi<br>Pico 1 |
|:-------------------:|:---------------------:|
|         VCC         |       3V3 (OUT)       |
|         GND         |          GND          |
|        RESET        |        GPIO 13        |
|         CS          |        GPIO 17        |
|      SCK / SCL      |        GPIO 18        |
|         SDA         |        GPIO 19        |
|         DC          |        GPIO 21        |
|         BL          |        GPIO 27        |

| M5 Stack<br>CardKB V1 | RaspberryPi<br>Pico 1 |
|:---------------------:|:---------------------:|
|VCC                    |3V3 (OUT)              |
|GND                    |GND                    |
|SDA                    |GPIO 4                 |
|SCL                    |GPIO 5                 |

| SD Card Module | RaspberryPi<br>Pico 1 |
|:--------------:|:---------------------:|
|SCK             |                       |
|MOSI            |                       |
|MISO            |                       |
|CS              |                       |

| RTC DS3231 Module | RaspberryPi<br>Pico 1 |
|:-----------------:|:---------------------:|
|VCC (+)            |                       |
|GND (-)            |                       |
|SDA (D)            |                       |
|SCL (C)            |                       |

## UI Specifications:
- The UI is a fork of the original MeshCore firmware.
- Tiny terminal style UI targeted at the Raspberry Pi Pico 1 and it's limited flash size availability.
- Approximately 20KB of memory usage in total after compile on the RPPico; No heap usage after startup.
- USB port is enabled, so is compatible with MeshCore App, MeshCore Web client and Meshcore-CLI.
- Conversations written to flash every 5 mins, after a change or a reboot. so the busy channels do not fill up the flash memory.
- Contact lists are indexed once and then cached. 
- Display orientation, brightness control and screen timeout settings.
- basic terminal color, Fonts and accent themes built in the UI.
- `@` key for manetions and replies in group chats.
- 5 slots for presaved messages / quick replies
- Radio presets available for quick switching frequencies to desired location.

## Build and Flash
- clone the repository in you system.

 `git clone https://github.com/Picoda/PicoMesh.git`

- Build the necessary firmware depending on your hardware.
 `pio run -e Pico_companion_term` 
> {***Firmware for ST7789 TFT-display}

 `pio run -e Pico_companion_term_7796`
> {***Firmware for ST7789 TFT-display}

- All build files are compiled and saved in `.pio` folder
- Press and hold the `BOOTSEL` while plugging in the RPPico to USB, It will mount as `RPI-RP2` volume drive.
- Copy the built `firmware.uf2` files to the `RPI-RP2` drive.
- The Picomesh UI is independent from the  MeshCore dependencies, keeping your  MeshCore firmware dependencies up-to-date.

- All UI files are in an independent folder

>==🔴/examples/companion_radio/ui-term==

- All target and pico board codes are in 

>==🔴/variant/rpi_pico==


## PicoMesh TO-DO
-[] Add Default Region-Scope settings.
-[] Add Discover Nodes and Sensors settings.
-[] Contacts Auto-add settings.
-[] Radio system monitoring and logging tools.
-[] Add GPS Module and support.
-[] Emoji support for chats.
-[] Extend board targets to RP Pico 1W, RP Pico 2, RP Pico 2W.