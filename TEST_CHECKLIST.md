# Integration Test Checklist

## Test Environment

- [ ] ESP32-S3 development board (4MB flash)
- [ ] USB-C cable for power and debugging
- [ ] WiFi network for STA testing
- [ ] Mobile device (Android) for captive portal testing
- [ ] Windows PC for captive portal testing
- [ ] Serial monitor (`idf.py monitor`)

## Test Scenarios

### 1. Boot with No Credentials → AP Starts

- [ ] Clear NVS: `idf.py erase_flash`
- [ ] Flash and boot device
- [ ] Verify "MyraMon_XX" AP appears within 5 seconds
- [ ] Verify DNS resolves any domain to 192.168.4.1: `nslookup test.com`
- [ ] Verify HTTP server responds: `curl http://192.168.4.1`
- [ ] Verify welcome page loads with temperature and config form

**Expected**: AP starts, captive portal accessible

### 2. Boot with Valid Credentials → STA Connects

- [ ] Save valid credentials via config form
- [ ] Reboot device
- [ ] Verify device connects to WiFi within 30 seconds
- [ ] Verify logs show "Connected to SSID" and "Got IP"
- [ ] Verify state is `sta_connected` via `/api/status`

**Expected**: STA connects, AP disabled

### 3. Boot with Invalid Credentials → AP After Timeout

- [ ] Save invalid credentials (wrong password)
- [ ] Reboot device
- [ ] Verify device attempts connection for 30 seconds
- [ ] Verify logs show "STA connection timeout"
- [ ] Verify "MyraMon_XX" AP appears after timeout
- [ ] Verify state is `ap_active`

**Expected**: Falls back to AP after 30s timeout

### 4. Config Form Submit → Credentials Saved

- [ ] Connect to AP, open config page
- [ ] Select network from dropdown
- [ ] Enter password
- [ ] Submit form
- [ ] Verify response: `{"status": "ok"}`
- [ ] Verify logs show "WiFi credentials saved"
- [ ] Reboot and verify auto-connect

**Expected**: Credentials persist across reboot

### 5. STA Disconnect → Auto-Reconnect

- [ ] Connect device to WiFi
- [ ] Restart WiFi router (simulate disconnect)
- [ ] Verify logs show "Disconnected, reconnecting"
- [ ] Verify device reconnects automatically
- [ ] Verify state remains `sta_connected`

**Expected**: Auto-reconnect succeeds

### 6. Temperature Endpoint

- [ ] Access `/api/temp` in browser
- [ ] Verify JSON response: `{"temp": XX.X}`
- [ ] Verify temperature updates every 10 seconds (watch page)
- [ ] Compare with known reference (should be within ±5°C of die temp)

**Expected**: Valid temperature, 10-second polling

### 7. DNS Query Response

- [ ] Connect to AP
- [ ] Run: `nslookup test.com 192.168.4.1`
- [ ] Verify response: 192.168.4.1
- [ ] Run: `nslookup google.com 192.168.4.1`
- [ ] Verify response: 192.168.4.1

**Expected**: All queries return 192.168.4.1

### 8. Captive Portal Detection

#### Android
- [ ] Connect to AP
- [ ] Verify notification: "Sign in to network"
- [ ] Tap notification, verify redirect to config page

#### Windows
- [ ] Connect to AP
- [ ] Open Network & Internet settings
- [ ] Verify "No internet, secured" with captive portal indicator
- [ ] Open browser, verify redirect to config page

#### iOS (optional)
- [ ] Connect to AP
- [ ] Verify auto-redirect to config page

**Expected**: All platforms detect captive portal

## User Stories Verification

- [ ] US1: Device auto-connects to WiFi on boot
- [ ] US2: AP named "MyraMon_XXXX" appears with no credentials
- [ ] US3: Auto-redirect to config page on AP connection
- [ ] US4: WiFi scan populates SSID dropdown
- [ ] US5: Password field hides characters
- [ ] US6: Confirmation shown after credentials saved
- [ ] US7: Temperature displays on welcome page
- [ ] US8: Temperature updates without page refresh
- [ ] US9: Can reconfigure after failed connection
- [ ] US10: Auto-reconnect on disconnect
- [ ] US11: (Deferred to v2) OTA firmware upload
- [ ] US12: Logs visible over USB-CDC

## Success Criteria

- [ ] Device connects to configured WiFi within 30 seconds
- [ ] Device falls back to AP mode if connection fails
- [ ] Mobile devices detect captive portal
- [ ] WiFi scan completes within 5 seconds
- [ ] Credentials persist across reboots
- [ ] Temperature updates every 10 seconds
- [ ] Auto-reconnect handles disconnections
- [ ] All build commands work: `build`, `flash`, `monitor`

## Notes

- Temperature readings are internal die temperature (5-10°C higher than ambient when WiFi active)
- AP is open (no password) for setup simplicity
- Credentials stored unencrypted in NVS (physical access risk)
