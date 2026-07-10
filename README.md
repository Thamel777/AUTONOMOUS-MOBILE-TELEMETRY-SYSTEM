# AMTS Direct-to-Cloud Setup

This project runs on an ESP32 Dev Module in PlatformIO and uses two FreeRTOS tasks:
automation on core 1 and networking on core 0. The firmware lives in [src/main.cpp](src/main.cpp) and the configurable wiring and cloud placeholders live in [include/amts_config.h](include/amts_config.h).

## Wiring

Use the pin map below exactly as defined in `amts_config.h` and the sketch.

| Module | Signal | ESP32 GPIO | Notes |
|---|---:|---:|---|
| L298N | IN1 | GPIO 16 | Left motor forward |
| L298N | IN2 | GPIO 17 | Left motor reverse |
| L298N | IN3 | GPIO 18 | Right motor forward |
| L298N | IN4 | GPIO 19 | Right motor reverse |
| HC-SR04 | TRIG | GPIO 21 | 3.3V output from ESP32 is fine |
| HC-SR04 | ECHO | GPIO 22 | Use a 5V to 3.3V voltage divider |
| Dual IR sensor | Left output | GPIO 34 | Input only pin |
| Dual IR sensor | Right output | GPIO 35 | Input only pin |
| DHT11 | Data | GPIO 23 | Add a pull-up if your module does not already include one |

### Power wiring

1. Connect the 2S battery pack positive to the L298N motor supply input.
2. Feed the same battery pack into a buck converter and set the output to 5V.
3. Connect the buck converter 5V output to the ESP32 VIN or 5V pin.
4. Connect all grounds together: battery negative, L298N GND, buck GND, ESP32 GND, and sensor grounds.
5. Keep the HC-SR04 echo line protected with a divider before it reaches GPIO 22.

### Important notes

1. Do not drive the ESP32 directly from 7.4V.
2. Do not connect the HC-SR04 echo pin directly to the ESP32.
3. Keep motor wiring physically separate from the sensor wiring where possible.

## Configuration

Open [include/amts_config.h](include/amts_config.h) and replace the placeholder values:

1. `WIFI_SSID`
2. `WIFI_PASSWORD`
3. `FIREBASE_DATABASE_URL`
4. `FIREBASE_AUTH_TOKEN`

If those values stay as `YOUR_...`, the firmware will compile but it will skip Wi-Fi connection attempts and telemetry publishing.

If the car drives backward when the firmware is telling it to go forward, set `INVERT_LEFT_MOTOR` or `INVERT_RIGHT_MOTOR` to `true` in [include/amts_config.h](include/amts_config.h). You can also swap the two motor wires on the affected L298N output channel.

If the car only follows the line in the wrong direction, your IR sensor polarity may also be inverted. In that case, keep the motor direction as-is and adjust the IR logic in [src/main.cpp](src/main.cpp) after confirming the sensor outputs with Serial prints.

## Upload Instructions

### In VS Code

1. Install the PlatformIO extension.
2. Open this folder as a PlatformIO project.
3. Disconnect the motor driver and sensors from the ESP32 while uploading if possible.
4. Connect the ESP32 over USB with a short data-capable cable.
5. In the PlatformIO sidebar, choose **Build** first to confirm the code compiles.
6. Then choose **Upload**.
7. Open the serial monitor at `115200` baud if you want to watch connection and telemetry logs.

### From the terminal

If PlatformIO is installed in the standard Windows location, run:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run --target upload
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor --baud 115200
```

If upload still fails with flash communication or packet transfer errors, try holding the ESP32 BOOT button during the start of upload, then releasing it once flashing begins.

## What the firmware does

1. Core 1 reads the ultrasonic sensor and IR sensors every 15 ms.
2. If an obstacle is within 15 cm, the motors stop immediately.
3. Core 0 handles Wi-Fi reconnects and telemetry publishing every 5 seconds.
4. Telemetry is sent as JSON to your Firebase RTDB endpoint.

The telemetry payload includes:

1. `temperature_c` from the DHT11
2. `humidity_pct` from the DHT11
3. `dht_ok` to show whether the sensor read succeeded
4. `distance_cm`, `left_ir`, `right_ir`, `obstacle_detected`, `drive_mode`, `wifi_connected`, `wifi_rssi_dbm`, and `uptime_ms`

The data is written to the `telemetry` node in Firebase RTDB.

The ultrasonic sensor is treated as the front-stop sensor. In the code, `FRONT_OBSTACLE_STOP_CM` sets the distance where the car must halt when something is directly in front of it.

## Quick build check

The project already builds successfully with:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```