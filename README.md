# LaMarzocco Display

A sophisticated ESP32-based display system for monitoring and controlling La Marzocco espresso machines with real-time WebSocket communication, boiler status visualization, and smart WiFi management.

![Hardware](https://img.shields.io/badge/Hardware-ESP32--S3-blue)
![Platform](https://img.shields.io/badge/Platform-PlatformIO-orange)
![UI](https://img.shields.io/badge/UI-LVGL%208.4-green)

## 📋 Table of Contents

- [Overview](#overview)
- [Hardware Requirements](#hardware-requirements)
- [Features](#features)
- [System Architecture](#system-architecture)
- [Installation](#installation)
- [Configuration](#configuration)
- [How It Works](#how-it-works)
- [Tunable Parameters](#tunable-parameters)
- [Troubleshooting](#troubleshooting)
- [Development](#development)

---

## 🎯 Overview

This project provides a real-time monitoring and control interface for La Marzocco espresso machines using an ESP32-S3 with an AMOLED display. It connects to La Marzocco's cloud API and WebSocket service to display machine status, boiler temperatures, countdown timers, and allow remote power control.

### Key Capabilities

- ☕ **Real-time Machine Monitoring** via WebSocket
- 🔥 **Boiler Status Display** with countdown timers and progress arcs
- ⚡ **Remote Power Control** - Turn machine on/off from the display
- 📡 **Smart WiFi Management** - Automatic reconnection with 5 retry attempts
- 🔐 **Secure Authentication** - ECDSA key generation and signing
- 🎨 **Beautiful AMOLED UI** - Built with LVGL 8.4

---

## 🔧 Hardware Requirements

### Tested Hardware

- **Board**: LilyGo T-Display AMOLED (ESP32-S3)
- **Display**: 1.91" AMOLED QSPI (536x240)
- **RAM**: 8MB PSRAM
- **Flash**: 16MB
- **Touch**: CST816 touch controller

### Pin Configuration

The pin configuration is automatically detected by the LilyGo AMOLED library:

- **Display Interface**: QSPI
- **Touch Interface**: I2C (SDA: 3, SCL: 2)
- **Battery Monitoring**: Available through ADC

---

## ✨ Features

### 1. Machine Control

- **Power On/Off**: Toggle machine power with a button press
- **Status Display**: Real-time machine status (Off, StandBy, PoweredOn, BrewingMode)
- **Mode Information**: Current operating mode display

### 2. Boiler Monitoring

- **Coffee Boiler**: Real-time temperature and ready status
- **Steam Boiler**: Real-time level and ready status
- **Countdown Timers**: 
  - Shows remaining time in minutes (when > 60 seconds)
  - Shows remaining time in seconds (when ≤ 60 seconds)
  - Shows "READY" when at temperature
  - Shows "OFF" when machine is off/standby
- **Progress Arcs**: Visual countdown indicator that empties from 100% → 0% as time runs out, then fills to 100% when ready
- **Smart Display Logic**:
  - Machine OFF/StandBy → "OFF", arc at 0%
  - Machine ON, no heating needed → "READY", arc at 100%
  - Machine ON, heating up → Countdown timer with arc emptying as time runs out
- **Timezone-aware**: Automatically converts GMT timestamps to local time

### 3. WiFi Management

- **Web-based Setup**: Captive portal for initial WiFi configuration
- **Automatic Reconnection**: 
  - 5 retry attempts with 30-second intervals
  - 15-second timeout per attempt
  - Total retry time: ~3 minutes before showing error
- **Status Indicators**: WiFi signal strength icon with 4 levels

### 4. User Interface

- **Welcome Screen**: Initial boot screen
- **Setup Screen**: WiFi configuration interface
- **Main Screen**: 
  - Machine status
  - Boiler countdown timers with arcs
  - Power and steam control buttons
  - Current time display
  - Battery and WiFi status icons
- **Error Screen**: Connection failure information

### 5. Security

- **ECDSA Cryptography**: SECP256R1 key generation
- **Request Signing**: Y5.e signature algorithm
- **Secure Storage**: NVS (Non-Volatile Storage) for credentials
- **Installation Keys**: Unique device identification

### 6. Memory & Performance Optimizations

- **Heap Protection**: 
  - Increased signature buffers (128 bytes) to prevent corruption
  - Pre-allocated strings with `reserve()` to avoid fragmentation
  - Memory monitoring before/after crypto operations
- **Task Stack Management**: 
  - LVGL task stack increased to 16KB for crypto operations
  - Proper yield() calls after heavy cryptographic work
- **Non-blocking Operations**: 
  - WiFi reconnection with state machine (no blocking delays)
  - Smooth UI updates with LVGL timers
  - WebSocket handling in callback context with cached tokens
- **Robust Error Handling**:
  - UUID generation validation
  - Signature generation error checking
  - Heap corruption prevention with buffer clearing

### 7. Recent Bug Fixes

- ✅ Fixed arc behavior to show countdown correctly (empties 100% → 0%, then fills to 100% when ready)
- ✅ Fixed temperature display to show "°C" instead of just "°"
- ✅ Fixed heap corruption in ECDSA signature generation
- ✅ Fixed LoadProhibited crashes with proper memory allocation
- ✅ Fixed `readyStartTime` interpretation (target time vs start time)
- ✅ Fixed timezone conversion for accurate countdown timers
- ✅ Fixed NVS key length limits (shortened to 15 chars)
- ✅ Fixed WebSocket disconnection issues with proper SSL setup
- ✅ Fixed JSON parsing with increased buffer sizes

---

## 🏗️ System Architecture

### Software Components

```
┌─────────────────────────────────────────────────────────────┐
│                         Main Loop                           │
│  ┌────────────┐  ┌──────────────┐  ┌─────────────────┐   │
│  │   LVGL     │  │   WiFi       │  │   WebSocket     │   │
│  │   UI Task  │  │   Manager    │  │   Handler       │   │
│  └────────────┘  └──────────────┘  └─────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                   La Marzocco Client                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │     Auth     │  │   REST API   │  │   WebSocket  │    │
│  │   Handler    │  │    Client    │  │    Client    │    │
│  └──────────────┘  └──────────────┘  └──────────────┘    │
└─────────────────────────────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────┐
│               Boiler Display Manager                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │   Coffee     │  │    Steam     │  │    Timer     │    │
│  │   Boiler     │  │   Boiler     │  │   Handler    │    │
│  └──────────────┘  └──────────────┘  └──────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### File Structure

```
LaMarzocco-Display/
├── include/
│   ├── boiler_display.h        # Boiler status display logic
│   ├── config.h                # Configuration constants
│   ├── lamarzocco_auth.h       # Authentication & key generation
│   ├── lamarzocco_client.h     # REST API client
│   ├── lamarzocco_machine.h    # Machine control interface
│   ├── lamarzocco_websocket.h  # WebSocket with STOMP protocol
│   ├── update_screen.h         # Screen update utilities
│   ├── web.h                   # Web server setup
│   └── web_handle.h            # Web request handlers
├── src/
│   ├── boiler_display.cpp      # Boiler countdown & arc logic
│   ├── custom_ui_event.cpp     # UI event handlers
│   ├── lamarzocco_auth.cpp     # ECDSA key generation & signing
│   ├── lamarzocco_client.cpp   # REST API implementation
│   ├── lamarzocco_machine.cpp  # Machine control & JSON parsing
│   ├── lamarzocco_websocket.cpp # WebSocket & STOMP implementation
│   ├── main.cpp                # Main program entry
│   ├── update_screen.cpp       # Screen refresh & WiFi monitoring
│   ├── web.cpp                 # Web server initialization
│   └── web_handler.cpp         # HTTP request handlers
├── data/                       # Web server HTML files
├── images/                     # UI assets (PNG images)
├── lib/                        # LilyGo AMOLED library
└── platformio.ini              # Build configuration
```

---

## 📦 Installation

### Prerequisites

- [PlatformIO](https://platformio.org/) installed
- USB-C cable
- La Marzocco machine with cloud connectivity
- La Marzocco account credentials

### Steps

1. **Clone the repository**
   ```bash
   git clone https://github.com/yourusername/LaMarzocco-Display.git
   cd LaMarzocco-Display
   ```

2. **Configure PlatformIO**
   ```bash
   # The project uses platformio.ini - no additional configuration needed
   ```

3. **Build the project**
   ```bash
   platformio run
   ```

4. **Upload to ESP32**
   ```bash
   platformio run --target upload
   ```

5. **Monitor Serial Output** (optional)
   ```bash
   platformio device monitor
   ```

### First-Time Setup

1. **Power on the device** - You'll see the Welcome Screen
2. **WiFi Setup** - If no WiFi is configured, it shows "No Connection" screen
3. **Connect to WiFi** - Press "WiFi Setup" to start the captive portal
4. **Access Web Interface** - Connect to the ESP32's WiFi hotspot and navigate to the IP shown
5. **Enter Credentials**:
   - WiFi SSID and Password
   - La Marzocco email and password
   - Machine serial number
6. **Save & Restart** - Device will connect and show the main screen

---

## ⚙️ Configuration

### config.h Constants

Located in `include/config.h`:

```cpp
// Debug output
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)

// Time configuration
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 0              // Adjust for your timezone
#define DAYLIGHT_OFFSET_SEC 0         // Adjust for DST

// Update intervals
#define TIME_UPDATE 30000             // 30 seconds between time updates

// Battery monitoring
#define BATTERY_VOLTAGE_PIN 4         // ADC pin for battery voltage
```

---

## 🔧 How It Works

### 1. Authentication Flow

```
┌──────────────┐
│ Device Boots │
└──────┬───────┘
       │
       ▼
┌─────────────────────┐
│ Check Stored        │
│ Installation Key    │
└──────┬──────────────┘
       │
       ├─[Not Found]──────┐
       │                  ▼
       │        ┌──────────────────┐
       │        │ Generate UUID    │
       │        │ Generate ECDSA   │
       │        │ Key Pair         │
       │        │ (SECP256R1)      │
       │        └──────┬───────────┘
       │               │
       │               ▼
       │        ┌──────────────────┐
       │        │ Save to NVS      │
       │        └──────┬───────────┘
       │               │
       ├───────────────┘
       │
       ▼
┌─────────────────────┐
│ Register Client     │
│ with La Marzocco    │
│ Cloud API           │
└──────┬──────────────┘
       │
       ▼
┌─────────────────────┐
│ Get Access Token    │
│ (OAuth 2.0)         │
└──────┬──────────────┘
       │
       ▼
┌─────────────────────┐
│ Connect WebSocket   │
│ with STOMP Protocol │
└─────────────────────┘
```

### 2. WebSocket Communication

**Connection Process**:
1. Resolve `lion.lamarzocco.io` DNS
2. Establish SSL connection on port 443
3. Send HTTP WebSocket upgrade request with custom headers:
   - `X-App-Installation-Id`: Installation UUID
   - `X-Timestamp`: Current timestamp in milliseconds
   - `X-Nonce`: Random UUID
   - `X-Request-Signature`: ECDSA signature
4. Send STOMP `CONNECT` frame with Authorization token
5. Receive STOMP `CONNECTED` confirmation
6. Subscribe to `/ws/sn/{SERIAL}/dashboard` topic
7. Receive continuous `MESSAGE` frames with machine data

**STOMP Frame Format**:
```
COMMAND
header1:value1
header2:value2

{JSON body}␀
```

### 3. Boiler Display Logic

**State Machine**:
```
Machine OFF/StandBy
    ↓
┌──────────┐
│   OFF    │ Label: "OFF", Arc: 0%
└──────────┘
    
Machine ON + readyStartTime = null
    ↓
┌──────────┐
│  READY   │ Label: "READY", Arc: 100%
└──────────┘

Machine ON + readyStartTime = timestamp
    ↓
┌──────────┐
│ HEATING  │ Label: "X min" or "X sec", Arc: 100% → 0% (empties)
└────┬─────┘
     │
     ├─[remaining > 60s]──→ Update every 30 seconds
     │
     └─[remaining ≤ 60s]──→ Update every 1 second
```

**Calculation**:
```cpp
remaining_seconds = (readyStartTime - current_time) / 1000  // Both in GMT milliseconds
arc_value = (remaining_seconds * 100) / WARMUP_DURATION_SEC  // Decreases as time passes
// Example: 300s remaining → 100%, 150s remaining → 50%, 0s remaining → 0%
```

### 4. WiFi Reconnection

**State Diagram**:
```
WiFi Connected
      ↓
  [Disconnect detected]
      ↓
┌─────────────────┐
│  Attempt 1      │ ──[Timeout 15s]──┐
└─────────────────┘                  │
      ↓                               │
  [Wait 30s]                          │
      ↓                               │
┌─────────────────┐                  │
│  Attempt 2      │ ──[Timeout 15s]──┤
└─────────────────┘                  │
      ↓                               │
  [Wait 30s]                          │
      ↓                               │
      ...                             │
      ↓                               │
┌─────────────────┐                  │
│  Attempt 5      │ ──[Timeout 15s]──┤
└─────────────────┘                  │
      ↓                               │
  [All Failed]                        │
      ↓                               │
┌─────────────────┐                  │
│ Show Error      │ ←────────────────┘
│ Screen          │
└─────────────────┘

[Success at any point] → Resume normal operation
```

### 5. JSON Message Parsing

**Incoming WebSocket Message Structure**:
```json
{
  "connected": true,
  "widgets": [
    {
      "code": "CMMachineStatus",
      "output": {
        "status": "PoweredOn",
        "mode": "BrewingMode"
      }
    },
    {
      "code": "CMCoffeeBoiler",
      "output": {
        "status": "HeatingUp",
        "readyStartTime": 1762859634966
      }
    },
    {
      "code": "CMSteamBoilerLevel",
      "output": {
        "status": "Ready",
        "readyStartTime": null
      }
    }
  ]
}
```

**Parsing Logic**:
1. Deserialize JSON using ArduinoJson
2. Extract `CMMachineStatus` → machine power state
3. Extract `CMCoffeeBoiler` → coffee boiler status & readyStartTime
4. Extract `CMSteamBoilerLevel` → steam boiler status & readyStartTime
5. Update boiler displays based on extracted data

---

## 🎛️ Tunable Parameters

### Boiler Display (`src/boiler_display.cpp`)

```cpp
// Warm-up duration (default: 5 minutes)
#define WARMUP_DURATION_SEC 300

// Timer update intervals
static uint32_t period_ms;
if (needs_fast_update) {
    period_ms = 1000;   // 1 second when < 60 seconds remaining
} else {
    period_ms = 30000;  // 30 seconds otherwise
}
```

**To adjust warm-up time**:
- Edit line 11 in `include/boiler_display.h`
- Change `#define WARMUP_DURATION_SEC 300` to desired seconds
- Example: `#define WARMUP_DURATION_SEC 420` for 7 minutes

### WiFi Management (`src/update_screen.cpp`)

```cpp
// WiFi monitoring intervals
static const unsigned long WIFI_CHECK_INTERVAL = 5000;       // Check every 5 seconds
static const unsigned long WIFI_RECONNECT_DELAY = 30000;     // 30 seconds between retries
static const unsigned long WIFI_CONNECT_TIMEOUT = 15000;     // 15 seconds per attempt
static const int MAX_RECONNECT_ATTEMPTS = 5;                 // Maximum 5 retries
```

**To adjust reconnection behavior**:
- **Change retry count**: Edit `MAX_RECONNECT_ATTEMPTS` (line 188)
- **Change retry interval**: Edit `WIFI_RECONNECT_DELAY` (line 186)
- **Change connection timeout**: Edit `WIFI_CONNECT_TIMEOUT` (line 187)
- **Change check frequency**: Edit `WIFI_CHECK_INTERVAL` (line 185)

### Initial WiFi Connection (`src/main.cpp`)

```cpp
// WiFi connection variables
const int MAX_WIFI_RETRIES = 10;        // Initial connection retries
const int WIFI_TIMEOUT_MS = 15000;      // Total timeout for initial connection
```

**To adjust initial connection**:
- Edit lines 26-27 in `src/main.cpp`
- Increase `MAX_WIFI_RETRIES` for slower networks
- Increase `WIFI_TIMEOUT_MS` for weak signals

### Time & Date Configuration (`include/config.h`)

```cpp
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 0              // Seconds offset from GMT
#define DAYLIGHT_OFFSET_SEC 0         // Daylight saving offset
#define TIME_UPDATE 30000             // Update time every 30 seconds
```

**To adjust for your timezone**:
- **GMT+1 (CET)**: `GMT_OFFSET_SEC 3600`
- **GMT-5 (EST)**: `GMT_OFFSET_SEC -18000`
- **GMT+5:30 (IST)**: `GMT_OFFSET_SEC 19800`
- **Add DST**: `DAYLIGHT_OFFSET_SEC 3600` (1 hour)

### Battery Monitoring (`src/update_screen.cpp`)

```cpp
// Battery voltage thresholds
if (voltage >= 4.0) return 3;      // Full
else if (voltage >= 3.8) return 2; // Medium
else if (voltage >= 3.5) return 1; // Low
else return 0;                     // Empty

// Voltage divider ratio
float voltage = (rawValue / 4095.0) * 3.3 * 2.0;  // 2:1 divider
```

**To adjust battery levels**:
- Edit thresholds in lines 52-55 of `src/update_screen.cpp`
- Adjust voltage divider ratio if your hardware differs (line 48)

### WiFi Signal Strength (`src/update_screen.cpp`)

```cpp
// RSSI (signal strength) thresholds
if (rssi >= -50) return 3;        // Strong
else if (rssi >= -66) return 2;   // Medium
else if (rssi >= -80) return 1;   // Weak
else return 0;                    // Very weak
```

**To adjust signal levels**:
- Edit thresholds in lines 72-75 of `src/update_screen.cpp`

### La Marzocco API Configuration

**REST API Base URL** (`src/lamarzocco_client.cpp`):
```cpp
const char* API_BASE_URL = "https://cms.lamarzocco.io/api/v1";
```

**WebSocket URL** (`src/lamarzocco_websocket.cpp`):
```cpp
const char* WS_BASE_URL = "lion.lamarzocco.io";
const uint16_t WS_PORT = 443;
```

**Note**: These should not be changed unless La Marzocco updates their API endpoints.

### Memory & Performance Configuration

**Task Stack Sizes** (`src/main.cpp`):
```cpp
// LVGL task stack (default: 16KB)
xTaskCreatePinnedToCore(Task_LVGL, "Task_LVGL", 
                        1024 * 16,  // 16KB stack for crypto operations
                        NULL, 3, NULL, 0);
```

**Cryptographic Buffers** (`src/lamarzocco_auth.cpp`):
```cpp
// Signature buffer size (default: 128 bytes)
uint8_t sig[128];  // ECDSA DER signature buffer

// Hash buffer
uint8_t hash[32];  // SHA256 hash (always 32 bytes)
```

**String Pre-allocation** (throughout codebase):
```cpp
String example;
example.reserve(400);  // Pre-allocate to avoid fragmentation
```

**Platform Configuration** (`platformio.ini`):
```ini
-D CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC
-D CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM=8
-D CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM=16
```

**To adjust memory settings**:
- **Increase LVGL stack**: Change `1024 * 16` to higher value if experiencing crashes
- **Reduce WiFi buffers**: Decrease `STATIC_RX_BUFFER_NUM` to free memory
- **Signature buffer**: Do not decrease below 128 bytes (prevents heap corruption)

**Memory Statistics**:
- **RAM Usage**: ~15.9% (52KB / 327KB)
- **Flash Usage**: ~24.6% (1.6MB / 6.5MB)
- **LVGL Task Stack**: 16KB
- **Signature Buffer**: 128 bytes
- **Free Heap (typical)**: 180-190KB after initialization

### Debug Output

**Enable/Disable Debug**:
```cpp
// In boiler_display.cpp (line 9)
#define DEBUG_BOILER 1  // Set to 0 to disable boiler debug

// In config.h
#define debug(x) Serial.print(x)      // Comment out to disable
#define debugln(x) Serial.println(x)  // Comment out to disable
```

### Memory Optimization

**JSON Document Size** (`src/lamarzocco_machine.cpp`):
```cpp
JsonDocument doc;  // Uses dynamic allocation
```

**If you encounter memory issues**:
- The ArduinoJson library auto-sizes based on content
- For large messages (>3KB), the ESP32's PSRAM handles this automatically
- No manual adjustment needed unless you modify message structure

### Task Stack Sizes (`src/main.cpp`)

```cpp
xTaskCreatePinnedToCore(Task_LVGL,
                        "Task_LVGL",
                        1024 * 10,     // 10KB stack size
                        NULL,
                        3,             // Priority level 3
                        NULL,
                        0);            // Core 0
```

**To adjust LVGL task**:
- Edit line 86 in `src/main.cpp`
- Increase if you encounter stack overflow
- Decrease to save memory (minimum 8KB recommended)

---

## 🐛 Troubleshooting

### Common Issues

#### 1. **WiFi Won't Connect**

**Symptoms**: Device shows "No Connection" immediately

**Solutions**:
- Increase `MAX_WIFI_RETRIES` in `main.cpp`
- Check WiFi credentials are correct
- Ensure 2.4GHz WiFi is enabled (ESP32 doesn't support 5GHz)
- Move device closer to router

#### 2. **WebSocket Keeps Disconnecting**

**Symptoms**: Serial shows "WebSocket disconnected" repeatedly

**Solutions**:
- Check La Marzocco credentials
- Verify machine serial number is correct
- Check if machine is online in La Marzocco app
- Look for "LoadProhibited" errors - may indicate memory issue

#### 3. **Boiler Display Stuck at "OFF"**

**Symptoms**: Machine is on but display shows "OFF"

**Solutions**:
- Check WebSocket connection status in serial monitor
- Verify JSON parsing is successful (look for "✓ JSON parsed successfully")
- Check if `readyStartTime` is being received correctly (should be 13-digit number)

#### 4. **Countdown Timer Incorrect**

**Symptoms**: Timer shows wrong values or doesn't count down

**Solutions**:
- Verify NTP time is syncing (check serial monitor for time)
- Check `GMT_OFFSET_SEC` is set correctly for your timezone
- Verify `readyStartTime` is being parsed as `long long` (not truncated)

#### 5. **Display Freezes**

**Symptoms**: UI stops responding, no touch input

**Solutions**:
- Check for stack overflow errors in serial monitor
- Increase `Task_LVGL` stack size in `main.cpp`
- Disable debug output to reduce serial overhead

#### 6. **Compilation Errors**

**Symptoms**: Build fails with errors

**Common fixes**:
- Clean build: `platformio run --target clean`
- Update PlatformIO: `pio upgrade`
- Check all libraries are installed correctly
- Ensure `platformio.ini` matches your board

### Debug Commands

**Enable verbose output**:
```bash
platformio run -v
```

**Monitor with timestamp**:
```bash
platformio device monitor --echo --filter time
```

**Check memory usage**:
```bash
platformio run --target size
```

---

## 👨‍💻 Development

### Adding New Features

#### 1. **Adding a New Widget to Parse**

Edit `src/lamarzocco_machine.cpp`:

```cpp
// In _websocket_message_handler(), add:
else if (strcmp(code, "YourNewWidget") == 0) {
    Serial.println("📊 Found YourNewWidget");
    JsonObject output = widget["output"].as<JsonObject>();
    
    // Extract your data
    const char* yourData = output["fieldName"];
    
    // Process it
    Serial.print("Data: ");
    Serial.println(yourData);
}
```

#### 2. **Adding a New Control Button**

1. **Add UI element** in SquareLine Studio
2. **Add event handler** in `src/custom_ui_event.cpp`:
```cpp
void yourButtonHandler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        // Your action here
        Serial.println("Button clicked!");
    }
}
```

3. **Register handler** in `src/ui/ui.c`:
```cpp
lv_obj_add_event_cb(ui_yourButton, yourButtonHandler, LV_EVENT_ALL, NULL);
```

#### 3. **Adding a New Screen**

1. **Create screen** in SquareLine Studio
2. **Export** to `src/ui/` folder
3. **Navigate** using:
```cpp
lv_disp_load_scr(ui_YourNewScreen);
```

### Code Style

- **Indentation**: 4 spaces (not tabs)
- **Naming**:
  - Functions: `camelCase()` or `snake_case()`
  - Classes: `PascalCase`
  - Constants: `UPPER_SNAKE_CASE`
  - Globals: `g_variableName`
- **Comments**: Use `//` for single-line, `/* */` for multi-line
- **Debug output**: Use emojis for easy visual parsing (✅ ❌ ⚠ 🔄 📊 ☕ ♨️)

### Testing

**Local testing without hardware**:
1. Comment out hardware-specific code in `main.cpp`
2. Use ESP32 simulator (not fully supported for LVGL)
3. Test WebSocket/REST API independently

**Serial Monitor Commands**:
- None currently implemented
- Can add custom commands by parsing `Serial.read()` in `loop()`

### Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

---

## 📄 License

This project is provided as-is for personal use. Please respect La Marzocco's terms of service when using their API.

---

## 🙏 Acknowledgments

- **LilyGo** - For the excellent AMOLED ESP32 board
- **LVGL** - For the graphics library
- **La Marzocco** - For creating amazing espresso machines
- **PlatformIO** - For the build system

---

## 📞 Support

For issues, questions, or contributions:
- Open an issue on GitHub
- Check the troubleshooting section
- Review serial monitor output for detailed error messages

---

**Last Updated**: November 2024  
**Version**: 1.0.0  
**Tested with**: La Marzocco Linea Mini, Linea Micra, GS3
