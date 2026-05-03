# Changelog

All notable changes to MyraMon will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-05-03

### Added

- Initial release
- ESP32-S3 WiFi configurator with captive portal
- Automatic STA connection on boot with 30s timeout fallback to AP
- NVS credential storage with validation
- WiFi state machine with 5 states
- DNS responder for captive portal (responds to all queries with 192.168.4.1)
- HTTP server with embedded HTML welcome page
- WiFi scan API with signal strength display
- WiFi config form with password toggle
- Temperature sensor with 10-second AJAX polling
- Status API with real-time connection feedback
- Captive portal detection URLs for Android, Windows, Firefox
- Auto-reconnect on WiFi disconnect
- USB-CDC debug output
- Partition table with OTA support (OTA implementation deferred to v2)

### Components

- `wifi_manager`: WiFi state machine, NVS credential storage, event handlers
- `dns_server`: Minimal DNS responder (lwIP UDP socket)
- `http_server`: HTTP server with all endpoints and embedded HTML

### API Endpoints

- `GET /` - Welcome page
- `GET /api/temp` - Temperature reading
- `GET /api/scan` - WiFi scan
- `POST /api/config` - Save credentials
- `GET /api/status` - Connection status
- Captive portal detection URLs (iOS, Android, Windows)

### Known Limitations

- On-chip temperature reads die temp, not ambient (5-10°C higher when WiFi active)
- Single STA connection attempt may fail in weak signal areas
- HTML changes require firmware rebuild
- AP is open (no password)
- Credentials stored unencrypted in NVS

### Out of Scope (Planned for v2)

- OTA firmware upload via web interface
- NVS encryption for credential storage
- External sensor support (I2C: BME280, etc.)
- AP password option (WPA2)
- Temperature calibration
- WiFi signal strength display on welcome page
- Connection history / last error display
- Internationalization support

## [Unreleased]

### Planned

- OTA implementation
- NVS encryption
- External sensor support
