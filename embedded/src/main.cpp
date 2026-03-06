#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "SDLogger.h"
#include "WifiConnect.h"
#include "WifiBleService.h"
#include "OTAUpdate.h"
#include "OTABleService.h"
#include "version.h"
#include "secrets.h"
#include "AiNotifyController.h"

static const int           MQTT_PORT         = 8883;
static const char          MQTT_TOPIC[]      = "ai-notify/events";
static const unsigned long MQTT_RECONNECT_MS = 5000;

static WiFiClientSecure   secureClient;
static PubSubClient       mqttClient(secureClient);
static AiNotifyController controller;

static WifiConnect    wifi;
static WifiBleService wifiBleService;
static OTAUpdate      otaUpdate;
static OTABleService  otaBleService;

static BLEServer*  pServer           = nullptr;
static bool        deviceConnected   = false;
static bool        oldDeviceConnected = false;
static bool        sdCardReady       = false;
static unsigned long lastMqttReconnect        = 0;
static unsigned long lastWifiReconnectDisplay = 0;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*)    override { deviceConnected = true; }
    void onDisconnect(BLEServer*) override { deviceConnected = false; }
};

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        LOG_E("MQTT JSON parse error");
        return;
    }

    const char* event = doc["event"] | "";
    if (strcmp(event, "question") == 0) {
        controller.onQuestion(String(doc["text"] | ""));
    } else if (strcmp(event, "ai_start") == 0) {
        controller.onAiStart();
    } else if (strcmp(event, "tool_start") == 0) {
        controller.onToolStart(String(doc["tool"] | ""));
    } else if (strcmp(event, "tool_end") == 0) {
        controller.onToolEnd(String(doc["tool"] | ""));
    } else if (strcmp(event, "done") == 0) {
        controller.onDone();
    }
}

static bool mqttConnect() {
    secureClient.setCACert(AWS_CERT_CA);
    secureClient.setCertificate(AWS_CERT_CRT);
    secureClient.setPrivateKey(AWS_CERT_PRIVATE);
    mqttClient.setServer(AWS_IOT_ENDPOINT, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(4096);   // question text can be up to 2000 chars

    LOG_IF("Connecting MQTT to %s ...", AWS_IOT_ENDPOINT);
    if (mqttClient.connect(AWS_IOT_THING_NAME)) {
        mqttClient.subscribe(MQTT_TOPIC);
        LOG_IF("MQTT subscribed: %s", MQTT_TOPIC);
        return true;
    }
    LOG_IF("MQTT connect failed, rc=%d", mqttClient.state());
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("AI Notify starting");

    sdCardReady = SDLogger::getInstance().init("/logs");
    if (sdCardReady) {
        LOG_I("=== AI Notify ===");
        LOG_IF("Version: %s", FIRMWARE_VERSION);
        LOG_IF("Build: %s %s", __DATE__, __TIME__);
    }

    controller.begin();

    xTaskCreate(ledRainbowTask, "ledRainbow", 4096, &controller, 1, nullptr);
    xTaskCreate(lcdMarqueeTask, "lcdMarquee", 4096, &controller, 1, nullptr);

    BLEDevice::init("AiNotify");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    wifiBleService.setWifi(&wifi);
    wifiBleService.init(pServer);

    otaBleService.setOTAUpdate(&otaUpdate);
    otaBleService.setWifi(&wifi);
    otaBleService.setVersionString(FIRMWARE_VERSION);
    otaBleService.init(pServer);

    BLEDevice::getAdvertising()->start();
    LOG_I("BLE advertising started");

    wifi.begin();
    if (wifi.hasCredentials()) {
        LOG_IF("Connecting to WiFi '%s'...", wifi.getSSID().c_str());
        if (wifi.connect() == 0) {
            LOG_IF("WiFi connected: %s", WiFi.localIP().toString().c_str());
            mqttConnect();
        } else {
            LOG_W("WiFi connection failed — configure via BLE");
        }
    } else {
        LOG_I("No WiFi credentials — configure via BLE");
    }

    LOG_I("Setup complete");
}

void loop() {
    wifiBleService.handle();
    otaBleService.handle();

    // BLE reconnect advertising
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        BLEDevice::getAdvertising()->start();
        oldDeviceConnected = deviceConnected;
    }

    // MQTT keepalive and reconnect
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    if (wifiOk) {
        if (!mqttClient.connected()) {
            unsigned long now = millis();
            if (now - lastMqttReconnect > MQTT_RECONNECT_MS) {
                lastMqttReconnect = now;
                if (!mqttConnect()) {
                    controller.onWifiReconnecting();
                }
            }
        } else {
            mqttClient.loop();
        }
    } else {
        unsigned long now = millis();
        if (now - lastWifiReconnectDisplay >= 5000) {
            lastWifiReconnectDisplay = now;
            controller.onWifiReconnecting();
        }
    }

    delay(10);
}
