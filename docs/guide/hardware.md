# Hardware

SonosESP runs on GUITION ESP32-P4 + ESP32-C6 touchscreen boards. Both sizes
build from the same source, and the installer and updater pick the right image
for you.

|  | 4-inch (stable) | 7-inch (beta) |
|---|---|---|
| Board | GUITION JC4880P443C | GUITION JC1060P470C |
| Display | 800×480, ST7701 | 1024×600, JD9165 |
| Touch | GT911 capacitive | GT911 capacitive |
| Processor | ESP32-P4, 400 MHz dual-core | ESP32-P4, 400 MHz dual-core |
| Wi-Fi | ESP32-C6 | ESP32-C6 |
| Flash / PSRAM | 16 MB / 32 MB | 16 MB / 32 MB |
| Connection | USB-C | USB-C |

::: warning This firmware is for these specific boards
It will not run on other ESP32 boards without significant changes. The display,
touch controller and Wi-Fi arrangement are all specific to these.
:::

## Which one should I get?

The **4-inch** is the one to buy. It is the main target and gets the most
testing.

The **7-inch** works and people are using it daily, but it has had far less
testing. GUITION also ship two different LCD panels under the same product code,
so the firmware includes a first-boot wizard that works out which one you have —
see [7-inch screens](/MULTI_SCREEN_SUPPORT).

## What else you need

- A USB-C cable that carries **data**, not just power. Charge-only cables are a
  common cause of "the board does not show up".
- A 2.4 GHz Wi-Fi network. The Wi-Fi chip does not do 5 GHz.
- At least one Sonos speaker on the same network.
