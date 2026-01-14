# ESP32-P4 Firmware Flashing Instructions

## Important Note
ESP32-P4 is not yet supported by ESP Web Tools. Please use one of the methods below to flash the firmware.

## Method 1: Using esptool.py (Recommended)

### Install esptool
```bash
pip install esptool
```

### Flash the firmware
For GUITION JC4880P433C (ESP32-P4 + ESP32-C6), the USB connection goes through the C6:

```bash
# Try ESP32-C6 first (handles USB serial on this board)
esptool.py --chip esp32c6 --port COM_PORT write_flash \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin

# If that fails, try auto-detect
esptool.py --port COM_PORT write_flash \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin
```

Replace `COM_PORT` with your serial port:
- Windows: `COM3`, `COM4`, etc.
- Linux/Mac: `/dev/ttyUSB0`, `/dev/cu.usbserial-*`, etc.

### Find your COM port
**Windows:**
```bash
# PowerShell
Get-WmiObject Win32_SerialPort | Select-Object Name, DeviceID
```

**Linux:**
```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

**Mac:**
```bash
ls /dev/cu.*
```

## Method 2: Using PlatformIO

1. Clone this repository
2. Open in VS Code with PlatformIO extension
3. Connect your ESP32-P4 via USB
4. Click "Upload" button or run:
```bash
pio run --target upload
```

## Method 3: Using Arduino IDE

1. Install Arduino IDE
2. Add ESP32 board support: https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html
3. Select Board: "ESP32P4 Dev Module"
4. Download the firmware.bin file
5. Use Tools > Partition Scheme > Default 16MB with spiffs
6. Upload

## Download Firmware Files

Get the latest firmware files from:
- GitHub Releases: [View Releases](../../releases)
- GitHub Actions Artifacts: [View Builds](../../actions)

## Troubleshooting

### Port not found
- Make sure the device is connected via USB
- Try a different USB cable (data cable, not charge-only)
- Install USB-to-Serial drivers if needed

### Permission denied (Linux/Mac)
```bash
sudo usermod -a -G dialout $USER
# Then log out and log back in
```

### Erase flash before flashing
If you encounter issues, erase the flash first:
```bash
esptool.py --chip esp32p4 --port COM_PORT erase_flash
```

## Support

For issues and questions, please visit: https://github.com/OpenSurface/SonosESP/issues
