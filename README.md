# ESP32 SD Card Server Project

## Project Description
The ESP32 SD Card Server is a standalone wireless file storage system that enables users to upload, browse, and download files through a web interface. This project leverages the ESP32's WiFi capabilities to create an Access Point (AP) mode server, allowing direct file management via a web browser without requiring an external internet connection.

## Objectives
- Create a portable file storage and sharing system using an ESP32.
- Implement a user-friendly web interface for file management.
- Enable seamless file uploads, downloads, and storage monitoring.
- Support standalone operation in Access Point mode without an external network.

## Key Features
- **Web-Based File Manager:** Upload, browse, and download files via a simple web interface.
- **WiFi AP Mode:** The ESP32 functions as a wireless access point, allowing devices to connect directly.
- **SD Card Storage:** Utilizes an SD card for file storage with automatic initialization.
- **Real-Time File Uploads:** Monitor upload progress dynamically.
- **Storage Information:** Displays total, used, and available storage space.

## Applications
- Personal network-attached storage (NAS)
- IoT file logging and remote access
- Secure offline file sharing
- Embedded system data management

## Technical Specifications
- **Microcontroller:** ESP32
- **Storage:** MicroSD card (formatted as FAT32)
- **Communication:** WiFi (AP mode)
- **Web Server:** ESP32 WebServer library

## Future Enhancements
- Implement user authentication for secure access
- Add file deletion and renaming capabilities
- Enable multi-client file access
- Expand support for additional file formats

This project showcases the capabilities of the ESP32 for building compact, wireless storage solutions that can be accessed without the need for internet connectivity.

