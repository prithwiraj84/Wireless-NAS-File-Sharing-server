<div align="center">

# ⚡ ESP32 Wireless NAS

<img src="https://img.shields.io/badge/ESP32-Microcontroller-blue?style=for-the-badge&logo=espressif&logoColor=white"/>
<img src="https://img.shields.io/badge/FAT32-SD%20Card-green?style=for-the-badge&logo=files&logoColor=white"/>
<img src="https://img.shields.io/badge/Language-C%2B%2B-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++" />
<img src="https://img.shields.io/badge/Framework-Arduino_IDE-00979D?style=for-the-badge&logo=arduino&logoColor=white" alt="Arduino" />
<img src="https://img.shields.io/badge/WiFi-Access%20Point-orange?style=for-the-badge&logo=wifi&logoColor=white"/>


<br/>
<br/>

> **Transform an ESP32 microcontroller into a fully offline, standalone Network Attached Storage device — no internet required.**

</div>

---

## 📖 Overview

By creating its own **Wi-Fi Access Point** and interfacing with an SD card via **SPI**, this project lets you wirelessly store, retrieve, and manage files without requiring an active internet connection. It features a responsive web dashboard, role-based access control, and built-in security mechanisms to prevent unauthorized access.

---

## ✨ Key Features

| Feature | Description |
|---|---|
| 📶 **Standalone Access Point** | Creates a private local network — no external router needed |
| 💾 **SD Card Integration** | Supports mass storage via SPI with FAT32 MicroSD cards |
| 🔐 **Role-Based Access Control** | Three user tiers: Admin, User, and Viewer |
| 🛡️ **Intrusion Protection** | Auto IP-ban after 5 failed login attempts |
| 📊 **Real-Time Dashboard** | Monitor uptime, heap memory, CPU frequency & clients |
| 📁 **File Management** | Upload, download, delete & list files via browser |
| 📜 **Audit Logging** | Tracks last 100 system events and user actions |

---

## ⚙️ Hardware Requirements

- ✅ ESP32 Development Board *(e.g. ESP32-WROOM-32)*
- ✅ MicroSD Card Module *(SPI Interface)*
- ✅ MicroSD Card *(Formatted to FAT32)*
- ✅ Jumper Wires *(Female-to-Female)*
- ✅ Breadboard *(Optional)*

### 🔌 Wiring Guide — SPI Interface

| SD Card Pin | ESP32 Pin | Description |
|:-----------:|:---------:|:------------|
| **CS** | `GPIO 5` | Chip Select |
| **MOSI** | `GPIO 23` | Master Out Slave In |
| **MISO** | `GPIO 19` | Master In Slave Out |
| **SCK** | `GPIO 18` | Serial Clock |
| **VCC** | `5V / 3.3V` | Power *(check module spec)* |
| **GND** | `GND` | Ground |

---

## 🚀 Installation & Setup

**1. Clone the Repository**
```bash
git clone https://github.com/yourusername/esp32-nas.git
cd esp32-nas
```

**2. Format the SD Card**

Ensure your MicroSD card is formatted to **FAT32** before inserting it into the module. EXFAT is not supported.

**3. Open in IDE**

Open the project folder in **Arduino IDE** or **PlatformIO**.

**4. Install Required Libraries**

Ensure the following standard ESP32 libraries are available:
```
WiFi.h
SD.h
SPI.h
WebServer.h
```

**5. Compile & Upload**

Connect your ESP32 via USB and hit **Upload**.
Open Serial Monitor at `115200` baud to view startup logs.

---

## 💻 Usage

### 1️⃣ Connect to the Network

Search for Wi-Fi networks and connect to:
```
SSID     : wireless server
Password : password123
```

### 2️⃣ Access the Dashboard

Open a browser and navigate to:
```
http://192.168.4.1
```

### 3️⃣ Login Credentials

| Role | Username | Password | Access Level |
|:----:|:--------:|:--------:|:-------------|
| 👑 **Admin** | `admin` | `adminpass` | Full system access — files, settings, bans, console |
| 👤 **User** | `user` | `userpass` | File management — upload, download, delete |
| 👁️ **Viewer** | `viewer` | `viewerpass` | Read-only — browse and download only |

---

## 🔒 Security Notes

> [!WARNING]
> **Change Default Credentials** — It is strongly recommended to update the default `apSSID`, `apPassword`, and all user credentials in the source code before deploying in any real environment.

> [!NOTE]
> **Automatic IP Banning** — The system automatically blocks an IP for **5 minutes** after **5 consecutive** failed login attempts. Admins can manually manage bans via the Console.

---

## 📁 Project Structure
```
esp32-nas/
├── src/
│   └── esp32_nas.ino       # Main source file
├── docs/
│   └── wiring_diagram.png  # Hardware wiring reference
├── assets/
│   └── screenshots/        # Dashboard screenshots
└── README.md
```

---

<div align="center">

Made with ❤️ by **Prithwiraj Das**

*ESP32 Wireless NAS · Open Source Project*

</div>
