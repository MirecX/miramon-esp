# MyraMon

ESP32-S3 WiFi Configurator with Captive Portal

## Overview

MyraMon is a standalone ESP-IDF application for ESP32-S3 that provides a self-configuring WiFi setup experience. The device automatically connects to a previously configured WiFi network on boot, or falls back to Access Point mode with a captive portal for initial configuration.

## Features

- **Auto-connect**: Automatically connects to configured WiFi on boot
- **Captive Portal**: Falls back to AP mode if connection fails (30s timeout)
- **WiFi Scanner**: Lists available networks with signal strength
- **Temperature Monitoring**: Displays internal temperature with live AJAX updates
- **Auto-reconnect**: Handles transient WiFi disconnections
- **Cross-platform**: Works with Android, iOS, Windows captive portal detection

## Requirements

- ESP32-S3 development board (4MB flash minimum)
- ESP-IDF v5.3
- USB-C connection for power and debugging

## Build Instructions

```bash
# Set up ESP-IDF environment
source /path/to/esp-idf/export.sh

# Navigate to project
cd myramon

# Set target to ESP32-S3
idf.py set-target esp32s3

# Build
idf.py build

# Flash
idf.py flash

# Monitor
idf.py monitor
```

## Usage

### First-Time Setup

1. Power on the device
2. Connect to WiFi network "MyraMon_XX" (XX = last 2 bytes of MAC)
3. Your device should automatically redirect to the configuration page
4. If not, open http://192.168.4.1 in your browser
5. Select your WiFi network from the dropdown
6. Enter your WiFi password
7. Click "Save & Connect"
8. The device will connect and you'll see the status page

### Normal Operation

After initial setup, the device will:
1. Attempt to connect to your WiFi network on boot
2. Display connection status and temperature on the welcome page
3. Auto-reconnect if the connection drops

### Accessing the Device

- **In AP mode**: http://192.168.4.1
- **In STA mode**: http://[device-local-ip] (check your router)

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Welcome page |
| `/api/temp` | GET | Temperature reading `{"temp": 25.3}` |
| `/api/scan` | GET | WiFi scan `{"ssids": [{"ssid": "name", "rssi": -45}]}` |
| `/api/config` | POST | Save credentials `{"ssid": "...", "password": "..."}` |
| `/api/status` | GET | Connection status `{"state": "...", "ssid": "...", "ip": "..."}` |

## Configuration

Edit `sdkconfig.defaults` or run `idf.py menuconfig`:

- `MYRAMON_AP_SSID_PREFIX`: AP SSID prefix (default: "MyraMon_")
- `MYRAMON_AP_IP_ADDR`: AP IP address (default: "192.168.4.1")
- `MYRAMON_STA_TIMEOUT_SEC`: STA connection timeout (default: 30)
- `MYRAMON_TEMP_POLL_INTERVAL_MS`: Temperature polling interval (default: 10000)

## Troubleshooting

### Device not appearing in WiFi list

- Check power connection
- Verify ESP32-S3 board is functional
- Check logs via `idf.py monitor`

### Captive portal not auto-redirecting

- Some devices don't support captive portal detection
- Manually open http://192.168.4.1 in your browser
- Try a different device (iOS/Android/Windows)

### Connection fails after saving credentials

- Verify WiFi password is correct
- Check signal strength in WiFi scan
- Device will fall back to AP mode after 30 seconds
- Try again with correct credentials

### Temperature shows N/A

- Sensor may not be initialized
- Check logs for temperature sensor errors
- Temperature readings are internal die temperature (5-10°C higher than ambient)

### Build errors

- Ensure ESP-IDF v5.3 is installed
- Run `idf.py set-target esp32s3` before building
- Run `idf.py fullclean` and rebuild

## Project Structure

```
myramon/
├── CMakeLists.txt          # Project CMake
├── sdkconfig.defaults      # Default configuration
├── partitions.csv          # Partition table
├── README.md               # This file
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   └── main.c              # Application entry point
└── components/
    ├── wifi_manager/       # WiFi state machine & NVS storage
    ├── dns_server/         # DNS responder for captive portal
    └── http_server/        # HTTP server & web interface
```

## License

MIT License

## Version

v1.0.0
