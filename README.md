# ESP32 SD Card NAS Web Server (AP Mode)

This project turns an ESP32 + SD card into a small **offline NAS-style file server** running in **Wi-Fi Access Point (AP) mode**.

You can:

- Connect to the ESP32’s Wi-Fi network
- Login with user accounts (Admin/User/Viewer)
- Upload / download / delete files on the SD card (role-based)
- View SD storage usage
- Monitor system info (heap, uptime, flash, clients)
- See live server logs and connected clients (Admin console)
- Rate-limit and manually ban IPs for security

---

## Features

- **Wi-Fi AP Mode**
  - ESP32 creates its own Wi-Fi network
  - Default:
    - SSID: `wireless server`
    - Password: `password123`

- **User Accounts & Roles**
  - Users stored in a `std::map<String, User>`:
    - `admin` / `adminpass` → full access
    - `user` / `userpass` → upload + delete + download
    - `viewer` / `viewerpass` → read-only download
  - Role types:
    - `ADMIN`
    - `USER`
    - `VIEWER`

- **Authentication**
  - Login page at `/login`
  - Redirects to login if not authenticated
  - Simple in-memory session via `currentUsername`

- **Brute-Force Protection & IP Ban**
  - Rate limiting based on failed login attempts
  - IP temporarily blocked after too many failed attempts
  - Manual IP ban/unban from admin console
  - Keeps track of:
    - `loginAttempts` (temporary)
    - `bannedIPs` (manual bans)

- **File Management**
  - List files on SD root
  - Upload files (non-Viewer roles)
  - Delete files (non-Viewer roles)
  - Download files
  - Filename validation to block `..` and path traversal

- **Storage Info**
  - Shows:
    - Total capacity
    - Used space
    - Free space
    - Usage progress bar with percentage

- **System Info (Admin)**
  - `/system` page includes:
    - Chip model, revision, cores
    - CPU frequency
    - SDK version
    - Uptime
    - Heap info
    - Flash size & speed
    - Sketch size & free space
    - AP SSID, IP, MAC, connected clients

- **Admin Console**
  - `/console`:
    - Live **server activity log**
    - List of **connected clients** (MAC addresses)
    - List and manage **banned IPs**
  - Uses `/console/data` (JSON) for AJAX polling

- **UI / UX**
  - Clean, responsive CSS (pure HTML + CSS + minimal JS)
  - Navigation bar with active page highlighting
  - Mobile-friendly layout
  - Upload progress message on home page

---

## Hardware Requirements

- ESP32 development board
- SD card module (SPI)
- microSD card (formatted as FAT/FAT32)
- Required SPI wiring:
  - `CS_PIN` is set to **GPIO 5** in code:
    ```cpp
    #define CS_PIN 5
    ```
  - Connect other SPI pins (MOSI, MISO, SCK) as per your ESP32 board pinout

---

## Software Requirements

- Arduino IDE (with ESP32 board support) or PlatformIO
- Libraries:
  - `WiFi.h`
  - `WebServer.h`
  - `SD.h`
  - `SPI.h`
  - `esp_wifi.h`
  - `esp_netif.h`
  - Standard C++ STL (`<map>`, `<list>`, `<set>`)

Make sure **ESP32 board package** is installed in Arduino IDE.

---

## Setup & Upload

1. **Clone / copy** this sketch into a new Arduino project.
2. Select your board in Arduino IDE:
   - `Tools` → `Board` → Your ESP32 board
3. Check `CS_PIN` and wiring for your SD module.
4. Insert a **formatted SD card** into the SD slot.
5. Connect the ESP32 via USB.
6. Click **Upload** in Arduino IDE.
7. Open **Serial Monitor** at **115200 baud** to see:
   - Boot messages
   - AP IP address
   - SD card status
   - Server logs

---

## Accessing the Web Interface

1. Power the ESP32.
2. On your phone/PC, connect to Wi-Fi:
   - SSID: `wireless server`
   - Password: `password123`
3. In a browser, go to the IP shown in Serial Monitor  
   (usually `192.168.4.1` for AP mode
