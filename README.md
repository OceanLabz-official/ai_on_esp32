# **ESP32 AI Voice Assistant**

An ESP32-based AI Voice Assistant that can listen, understand, think, and speak — fully running on microcontroller hardware with cloud AI integration.
This project is designed for education, experimentation, and real-world embedded AI demos.

## **Hardware Requirements**

| Component | Recommended Part |
|---------|------------------|
| **Microcontroller** | ESP32-S3 / ESP32 / ESP32-C3 |
| **Microphone** | INMP441 (I2S Digital Microphone) |
| **Audio Output** | MAX98357A / PCM5102 (I2S DAC) |
| **Storage** | MicroSD Card (**FAT32**) |
| **Speaker** | 4Ω / 8Ω Speaker |
| **Connectivity** | WiFi (2.4GHz) |


## **Pin Configuration For ESP32-S3**

## **INMP441 Microphone**
| Signal | ESP32 Pin |
|------|-----------|
| **SCK (BCLK)** | GPIO 40 |
| **WS (LRCLK)** | GPIO 39 |
| **SD** | GPIO 41 |

## **I2S DAC**
| Signal | ESP32 Pin |
|------|-----------|
| **BCLK** | GPIO 48 |
| **LRCLK** | GPIO 21 |
| **DIN** | GPIO 47 |

## **SD Card (SPI)**
| Signal | ESP32 Pin |
|------|-----------|
| **CS** | GPIO 42 |
| **MISO** | GPIO 46 |
| **MOSI** | GPIO 3 |
| **SCK** | GPIO 2 |

## **Development Environment**
- **Arduino IDE** (latest version)
- **ESP32 Board Package 3.0.0** by Espressif Systems

## **Required Libraries**
- **WiFi** (built-in)
- **HTTPClient** (built-in)
- **ArduinoJson**
- **ArduinoHttpClient**
- **ESP8266Audio**

  ## **Library Installation Notes**
  > **ESP8266Audio:**  
  > Install via **Arduino IDE → Library Manager**.






