# AI Notify — Design Document

**Date:** 2026-03-06
**Project:** `ai-notify` — Physical LED + LCD notification display for the nakom.is AI chat

---

## Overview

A physical ambient display that reacts in real time to AI activity in the nakom.is social app chat window. A strip of 5 NeoPixel LEDs lights up in the colour of whichever AI data source is being queried; a 16×2 LCD shows a scrolling marquee of the latest user question and a status line.

---

## Repository Structure

```
ai-notify/
├── embedded/          # PlatformIO project (bootstrapped via esp32-bootstrap)
├── infra/             # AWS CDK stack (IoT Thing + certs + IAM policy)
├── 3d/                # 3D-printed enclosure (future)
├── pcb/               # PCB design (future)
├── CLAUDE.md
└── README.md
```

---

## Architecture

Three components communicate in a simple chain:

```
nakom.is Lambda  →  AWS IoT Core  →  ESP32-S3-CAM
(stream-handler)    (MQTT broker)    (NeoPixel + LCD)
```

### 1. AWS IoT Core Infrastructure (`infra/`)

CDK stack deployed to the **main account** (`nakom.is` / `nakom.is-admin`).

**Creates:**
- IoT Thing: `AiNotifyThing`
- Device certificate (stored in SSM at deploy time via `cdk-iot-core-certificates-v3`)
  - `/AiNotify/AiNotifyThing/certPem`
  - `/AiNotify/AiNotifyThing/privKey`
- IoT Policy: grants `iot:Connect`, `iot:Subscribe`, `iot:Receive` on topic `ai-notify/events`
- IAM Managed Policy: grants `iot:Publish` on `arn:aws:iot:*:*:topic/ai-notify/events` — for Lambda to attach
- SSM Parameter: `/AiNotify/IotEndpoint` — IoT data endpoint URL
- SSM Parameter: `/AiNotify/IotPublishPolicyArn` — ARN of the IAM policy, for reference

**AWS profiles:**
- Standard CDK operations: `nakom.is` (PowerUser)
- IAM policy creation: `nakom.is-admin`

### 2. Social App Lambda Modifications (`nakom.is` repo)

File: `lambda/chat/stream-handler.ts`

**New behaviour:** Publishes MQTT events to `ai-notify/events` alongside existing SSE events.

**Events published:**

| Trigger | JSON payload |
|---|---|
| Question received (stream start) | `{"event":"question","text":"<user question>"}` |
| AI begins processing | `{"event":"ai_start"}` |
| Tool call begins | `{"event":"tool_start","tool":"<tool_name>"}` |
| Tool call ends | `{"event":"tool_end","tool":"<tool_name>"}` |
| Streaming complete | `{"event":"done"}` |

**Implementation:**
- Uses `@aws-sdk/client-iot-data-plane` (available in Lambda Node runtime, no extra dependencies)
- IoT endpoint read from SSM at cold start (cached in module scope)
- IoT publish is fire-and-forget (`await` but errors are caught and logged, never thrown — display failure must never affect the chat)
- Lambda execution role gets the IAM Managed Policy from the CDK stack attached

### 3. ESP32 Firmware (`embedded/`)

Bootstrapped from `esp32-bootstrap`. Board: ESP32-S3-CAM (16MB flash).

**Hardware:**

| Component | Connection |
|---|---|
| NeoPixel strip (5 LEDs) | Data → GPIO48 |
| LCD1602 (I2C, PCF8574 backpack) | SDA → GPIO8, SCL → GPIO9, addr 0x27 |

**Libraries:**
- Core: `SDLogger`, `WifiConnect`, `WifiBleService`, `OTAUpdate`, `OTABleService`
- External (via platformio.ini): `Adafruit NeoPixel`, `marcoschwartz/LiquidCrystal_I2C`
- MQTT: `PubSubClient` (direct, without full MqttService — simple subscribe-only use case)

**Firmware tasks:**
- Main loop: WiFi keepalive, MQTT loop, reconnection
- FreeRTOS task: LCD marquee scroller (scrolls row 1 when text > 16 chars)
- FreeRTOS task: LED4 colour cycler (steps through rainbow while AI is active)

---

## LED Behaviour

| LED | Source | Colour | NeoPixel RGB |
|---|---|---|---|
| LED0 | CV / Resume | `#ff8c00` dark orange | (255, 140, 0) |
| LED1 | LinkedIn | `#0077b5` blue | (0, 119, 181) |
| LED2 | GitHub | `#f0f0f0` near-white | (240, 240, 240) |
| LED3 | Blog | `#38983f` green | (56, 152, 63) |
| LED4 | AI activity | Rainbow cycle | Red→Orange→Yellow→Green→Blue→Purple |

**Global brightness:** 50% (via `Adafruit_NeoPixel::setBrightness(128)`) — prevents eye-searing white on LED2 and reduces power draw.

**Tool → LED mapping:**

| Tool name(s) | LED |
|---|---|
| `get_cv` | LED0 |
| `get_linkedin_profile` | LED1 |
| `get_github_readme`, `list_repo_files`, `read_repo_file` | LED2 |
| `get_blog_posts`, `get_interests` | LED3 |

---

## LCD Behaviour

**Row 1 (top):** Scrolling marquee of the most recent user question. Scrolls if > 16 characters; static if ≤ 16. Updated on `question` event. Persists until next question.

**Row 2 (bottom):** Status text, updated per event:

| Event | Row 2 text |
|---|---|
| `question` / `ai_start` | `Thinking...` |
| `tool_start: get_cv` | `Searching CV...` |
| `tool_start: get_linkedin_profile` | `Searching LinkedIn` |
| `tool_start: get_github_readme` etc. | `Searching GitHub` |
| `tool_start: get_blog_posts` etc. | `Searching Blog...` |
| `done` | `Ready` |

**Idle (boot / no conversation):** Row 1 shows last question (or `nakom.is` on first boot), row 2 shows `Ready`.

---

## State Machine (ESP32)

```
IDLE
  ↓ question event
ACTIVE (LED4 cycling)
  ↓ tool_start
  TOOL_ACTIVE (tool LED on, LED4 still cycling)
  ↓ tool_end
  ACTIVE
  ↓ done
IDLE (all LEDs off)
```

Multiple tool LEDs can technically be on simultaneously if future parallel tool calls are added; the firmware handles each independently.

---

## Error Handling

- **WiFi drops:** PubSubClient reconnect loop in main task; LEDs stay in last known state, row 2 shows `Reconnecting...`
- **MQTT disconnect:** Reconnect with exponential backoff; device cert re-used
- **IoT publish failure (Lambda side):** Caught, logged to CloudWatch, SSE stream unaffected
- **LCD I2C not found:** Logged via SDLogger; firmware continues without LCD
- **NeoPixel fault:** No fault detection possible at software level; firmware continues

---

## Out of Scope (for now)

- OTA firmware updates (cross that bridge later — note: CDK is in main account, not sandbox, so sandboxsite OTA path won't work directly)
- PCB / 3D enclosure design
- Multiple simultaneous display instances
- BLE configuration of display behaviour

---

## AWS Account & CDK Notes

- **Profile for CDK deploy:** `nakom.is` for standard resources, `nakom.is-admin` for IAM policy creation
- **Region:** Same as nakom.is social app (verify before deploy)
- **IoT endpoint:** One per account/region — retrieved via `aws iot describe-endpoint --endpoint-type iot:Data-ATS`
- **Naming convention:** `AiNotify` (PascalCase), `ai-notify` (kebab for topics/S3)
