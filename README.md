# Depot Indicator System

## Overview

This repository contains all the code and configuration needed to run a low-cost, Wi-Fi-connected safety indicator panel for train depots. The system uses two ESP32-DevKit-S3 boards, a local Mosquitto broker and a Node-RED dashboard on a private 2.4 GHz LAN with no internet access. It displays live status lamps and provides a remote emergency-stop function.

## Key Components

### Main Display Board (`E-connected.ino`)
- Drives eleven status LEDs, one dedicated red E-Stop lamp and a piezo buzzer  
- Subscribes to  
train/<TRAIN_ID>/status
train/<TRAIN_ID>/control/status
- Parses JSON payloads with ArduinoJson and maps fields to GPIOs  
- Automatically reconnects to Wi-Fi and MQTT after any drop  

### Emergency-Stop Button Board (`Wifi_connected.ino`)
- Reads a latching mushroom switch on GPIO 4 (INPUT_PULLUP)  
- Publishes retained JSON  
```json
{ "ignoreCommands": true }
{ "ignoreCommands": false }
```
to train/<TRAIN_ID>/control/status
- Ensures the main board locks out normal commands until the hazard is cleared

### Node-RED Flow (flow.json)
- Provides a web UI for selecting train IDs and toggling status flags
- Injects well-formed JSON messages with the retained flag
- Routes messages back to the ESP32 boards on the private LAN

Getting Started
1. Set up the LAN and Download the Node.js
- Configure a router as a private 2.4 GHz access point (air-gapped).
- Install Mosquitto on your host PC and open TCP port 1883 in the firewall.
- Go website https://nodejs.org/ to download the LTS version (recommended for most users).
- Node-RED is available as an npm package, install it globally in cmd with command : npm install -g --unsafe-perm node-red.
- after downloading, activate it in cmd by typing "node-red".

2. Flash the ESP32 boards
- Edit each sketch’s top section to set your TRAIN_ID, Wi-Fi SSID/PASSWORD and broker IP.
- Upload E-connected.ino to the main display board.
- Upload Wifi_connected.ino to the button board.

3. Import the Node-RED flow
- Activate node-red in cmd, go to the default link http://127.0.0.1:1880/
- Open Node-RED in your browser, import Flow.json, and deploy.
- Open the MQTT nodes and set the Server to the IPv4 address of the connected WLAN.
- Once the texts under the nodes show connected, the connection is established.
- Visit http://<host-ip>:1880/ui to control the panel.
- Switch on/off the switches to check the IoT update on the LED.

With these steps complete, you’ll have a fully functional, locally-isolated depot indicator system that is easy to extend and maintain.
