This is my Dual Screen port of M5Porkchop!  I've added some cool stuff since the video, so enjoy!


M5PORKCHOP: Embedded RF Protocol Analysis & Visualization Suite
An advanced, open-source firmware suite developed for the ESP32-S3 architecture (specifically optimized for the M5Stack Cardputer platform). This project serves as a technical demonstration of real-time 802.11 protocol auditing, Bluetooth Low Energy (BLE) beacon telemetry analysis, and high-performance embedded UI rendering.

Designed strictly for Educational, Security, Defense, and Awareness (ESDA) framework compliance, network administrators, and embedded systems developers.

🔬 Core Architecture & Features
1. Passive 802.11 Protocol Auditing
Frame Density Mapping: Real-time passive observation of 802.11 management and data frame distribution across the 2.4GHz spectrum.

RSSI Propagation Tracking: Diagnostic logging of Received Signal Strength Indicator (RSSI) metrics to analyze RF attenuation and signal boundary degradation.

Network Topology Mapping: Passive parsing of publicly broadcast beacon frames to inventory authorized infrastructure and audit baseline security posture.

2. BLE Telemetry & Advertising Analysis
Beacon Inventory: Passive discovery and classification of Bluetooth Low Energy (BLE) advertising channels.

Protocol Frame Parsing: Real-time decoding of localized GATT services and peripheral broadcast payloads for diagnostic optimization.

Transmission Stress-Testing: Utilities for generating localized broadcast packets to evaluate receiver-side queue handling and buffer allocation under dense signal environments.

3. High-Performance Embedded UI Engine
Waterfall Signal Display: A mathematically optimized, real-time visual representation of spectrum density utilizing custom sinc-function rendering pipelines.

Advanced Heap Management: Custom memory allocation paradigms utilizing Knuth's 50% rule optimization to maximize stability on restricted hardware layouts.

Hardware Interfacing: Full integration with onboard IMU sensors, power-management ICs (PMIC), and physical keyboard matrix polling routines.

🛠️ Hardware Requirements
This firmware is engineered to interface directly with the following hardware specifications:

Core Development Host: M5Stack Cardputer (ESP32-S3 Dual-Core Xtensa LX7 processor)

Storage / Logs: MicroSD Card slot (FAT32 formatted for passive text/log extraction)

Optional Diagnostics: Compatible external antenna arrays for directional RF propagation mapping.

🚀 Installation & Flashing
Option A: VS Code & PlatformIO (Recommended)
Clone this repository to your local workspace.

Open the project folder inside Visual Studio Code with the PlatformIO extension installed.

Connect your hardware via a verified USB-C data cable.

Execute the PlatformIO: Upload task to compile and flash the binary.

Option B: Arduino IDE
Ensure the esp32 board manager URL is added to your preferences.

Install the required dependencies: M5Cardputer, M5GFX, and standard WiFi / BLE libraries.

Configure target board settings: ESP32S3 Dev Module with enabled USB CDC on boot.

Compile and upload.

⚠️ Regulatory Compliance & Disclaimer
Important Notice: This software is an educational framework designed exclusively for passive RF diagnostic analysis, protocol auditing, and defensive infrastructure validation. Users are entirely responsible for ensuring that any deployment complies with local, national, and international telecommunications regulations (including FCC, ETSI, and local data privacy laws). The authors assume no liability for misuse, unintended cross-talk, or unauthorized utilization of packet generation utilities.