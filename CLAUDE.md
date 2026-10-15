# AI Notify

Physical ambient display for the nakom.is AI chat.
Reacts in real time to AI activity via AWS IoT Core MQTT.

## Hardware

- **Board:** ESP32-S3-CAM (Ai-Thinker, 16MB flash, 8MB PSRAM)
- **NeoPixel strip:** 5 LEDs, data → GPIO48
- **LCD1602 I2C (PCF8574 backpack):** SDA → GPIO8, SCL → GPIO9, address 0x27

## Pin Mapping

| Component | GPIO |
|---|---|
| NeoPixel data | 48 |
| I2C SDA (LCD) | 8 |
| I2C SCL (LCD) | 9 |

## Build & Flash

```bash
cd embedded
AWS_PROFILE=nakom.is python scripts/generate_secrets.py   # first time only
pio run --target upload
pio device monitor
```

## CDK Infra

Deployed to the main account (`nakom.is`). IoT Thing: `AiNotifyThing`.

Deploy ai-notify CDK **before** deploying the nakom.is ChatStack.

```bash
cd infra
npm install
npx cdk deploy --profile nakom.is         # standard resources
npx cdk deploy --profile nakom.is-admin   # if CDK blocks on IAM policy creation
```

After deploying, store the IoT endpoint in SSM:
```bash
ENDPOINT=$(aws iot describe-endpoint --endpoint-type iot:Data-ATS \
    --profile nakom.is --query endpointAddress --output text)
aws ssm put-parameter --name /AiNotify/IotEndpoint --value "$ENDPOINT" \
    --type String --profile nakom.is
```

SSM parameters created by the CDK stack:
- `/AiNotify/AiNotifyThing/certPem` — device certificate PEM
- `/AiNotify/AiNotifyThing/privKey` — device private key PEM
- `/AiNotify/IotPublishPolicyArn` — IAM policy ARN for the nakom.is Lambda

## MQTT Topic

`ai-notify/events` — JSON payloads from the nakom.is Lambda.

| Event | Payload |
|---|---|
| `question` | `{"event":"question","text":"<user question>"}` |
| `ai_start` | `{"event":"ai_start"}` |
| `tool_start` | `{"event":"tool_start","tool":"<name>"}` |
| `tool_end` | `{"event":"tool_end","tool":"<name>"}` |
| `done` | `{"event":"done"}` |

## AWS Profiles

- `nakom.is` — standard CDK and CLI operations (PowerUser)
- `nakom.is-admin` — IAM policy/role creation when blocked

## LED Behaviour

| LED | Source | Colour (RGB) |
|---|---|---|
| LED0 | CV/Resume | Dark orange (255, 140, 0) |
| LED1 | LinkedIn | Blue (0, 119, 181) |
| LED2 | GitHub | Near-white (240, 240, 240) |
| LED3 | Blog | Green (56, 152, 63) |
| LED4 | AI activity | Rainbow cycle |

Global brightness: 50% (`setBrightness(128)`)

## LCD Behaviour

- **Row 1:** Scrolling marquee of most recent question (scrolls if > 16 chars)
- **Row 2:** Status (`Thinking...` / `Searching CV...` / `Ready` / `Reconnecting...`)

## OTA Updates

Out of scope for now — CDK is in the main account, not the sandbox.
