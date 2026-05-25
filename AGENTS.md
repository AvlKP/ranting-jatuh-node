# AGENTS.md - Project Context & Guidelines

## 0. IMPORTANT
The host OS is Windows. Use PowerShell, NOT Bash.
Source 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1' to enable ESP-IDF environment.

## 1. Project Overview
- **Description:** 
  - A tree branch monitoring system that notifies users when events that signal risk or branch failure occured. 
  - System analyze dynamic behavior of branch to determine risks and send warnings. 
  - System is made up of IoT nodes for monitoring and a server for analysis.
- **Goal:**
  - Nodes send periodic data of parameters that indicate failure to server.
  - Server calculates the probability of failure and detect anomalies; send warning if risk detected.
  - Send notification when branch failure occurrs.
- **Target Hardware:**  ESP32-S3FH4R2 on a custom PCB, pinout defined in 'main/pins.hpp'.
- **Success Criteria:** Give a notification to app upon failure or warnings based on prediction, optimized power usage (without complex deep-sleep states to favor rapid iteration), send regular monitoring data to server via MQTT.

## 2. System Architecture
- **Framework:** ESP-IDF v5.5.4 for ESP32-S3
- **Communication Bus:** I2C 400kHz for LSM6DS3 and SDIO 1-bit for microSD card storage.
- **Key Components:**
  - `lsm6ds3`: LSM6DS3TR driver to communicate with IMU.
  - `logger`: Receives processed data and warnings from monitor. Write logs and warnings into MicroSD. Transmits logs and warnings to server.
  - `monitor`: Process the input data from IMU and AE sensor to parameters. Send the results to logger.
- **Hardware:**
  - ESP32-S3 MCU
  - LSM6DS3 IMU (I2C)
  - MicroSD Card (SDIO 1-bit)
  - Acoustic Emission analog sensor and processing pipeline. Assume output is high when emission is detected.

## 4. Engineering Standards
- **Naming:** PascalCase for classes, snake_case for files
- **Error Handling:** esp_err_t propagation, C++ exceptions disabled
- **Testing:** Unity for unit tests, manual debugging via openocd
- **Documentation:** Doxygen style, README.md per component

## 5. Agent Instructions
- **Code Style:** Follow C++ for real time systems coding style.
- **Don'ts:** Combine everything into one source file.
- **Dos:** Always use '/realtime-cpp' and '/esp32s3-idf' skills when writing code, optimize battery usage, ask questions about the hardware required.

Respond terse like smart caveman. All technical substance stay. Only fluff die.

Rules:
- Drop: articles (a/an/the), filler (just/really/basically), pleasantries, hedging
- Fragments OK. Short synonyms. Technical terms exact. Code unchanged.
- Pattern: [thing] [action] [reason]. [next step].
- Not: "Sure! I'd be happy to help you with that."
- Yes: "Bug in auth middleware. Fix:"

Switch level: /caveman lite|full|ultra|wenyan
Stop: "stop caveman" or "normal mode"

Auto-Clarity: drop caveman for security warnings, irreversible actions, user confused. Resume after.

Boundaries: code/commits/PRs written normal.
