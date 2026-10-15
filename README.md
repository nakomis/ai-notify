# AI Notify

Physical ambient display for the [nakom.is](https://nakom.is) AI chat.

A strip of 5 NeoPixel LEDs lights up in the colour of whichever AI data source is being
queried, and a 16×2 LCD shows a scrolling marquee of the latest user question.

## Architecture

```
nakom.is Lambda  →  AWS IoT Core  →  ESP32-S3-CAM
(stream-handler)    (MQTT broker)    (NeoPixel + LCD)
```

## Repository Structure

- `embedded/` — PlatformIO firmware (ESP32-S3-CAM)
- `infra/` — AWS CDK stack (IoT Thing + certs + IAM policy)
- `3d/` — 3D-printed enclosure (future)
- `pcb/` — PCB design (future)

## Hardware

- ESP32-S3-CAM (Ai-Thinker, 16MB flash)
- NeoPixel strip × 5 LEDs → GPIO48
- LCD1602 with PCF8574 I2C backpack → SDA GPIO8, SCL GPIO9, addr 0x27
