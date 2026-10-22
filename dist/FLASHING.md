# Flashing the Daedric Grimoire — M5Stack Cardputer ADV (ESP32-S3)

Two images are provided. Most people want the **full image**.

## Full image (clean install) — `daedric-grimoire-cardputer-adv.bin`
A merged factory image: bootloader + partition table + app. Flash it to a
blank or factory Cardputer ADV at offset **0x0**. Nothing else is required.

Using esptool (install with `pip install esptool`):

    esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
      write-flash 0x0 daedric-grimoire-cardputer-adv.bin

- Port is `/dev/ttyACM0` on Linux, `COMx` on Windows, `/dev/cu.usbmodem*` on macOS.
- If the port doesn't appear, hold **G0** while tapping **reset** to enter
  download mode, then run the command.
- You can also drag this file into https://espressif.github.io/esptool-js/
  (Web Serial, Chrome/Edge) and flash at offset 0.

## App-only image — `daedric-grimoire.bin`
Just the application partition, for installing over the Daedric Launcher's
`grimoi` OTA slot (leaves the bootloader/Launcher/NVS untouched):

    esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
      --no-stub write-flash 0x170000 daedric-grimoire.bin

## Verify
    sha256sum -c SHA256SUMS.txt

Board: ESP32-S3, 8 MB flash, USB-CDC on boot. Source and build instructions:
https://github.com/numbpill3d/cardputer-adv-diviner
