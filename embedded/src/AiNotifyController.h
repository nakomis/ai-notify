#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <LiquidCrystal_I2C.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define NEOPIXEL_PIN        48
#define NEOPIXEL_COUNT      5
#define NEOPIXEL_BRIGHTNESS 128   // 50% — prevents eye-searing white on LED2

#define LCD_ADDR  0x27
#define LCD_COLS  16
#define LCD_ROWS  2

// NeoPixel indices
#define LED_CV       0
#define LED_LINKEDIN 1
#define LED_GITHUB   2
#define LED_BLOG     3
#define LED_AI       4

enum class AppState {
    IDLE,
    ACTIVE,       // AI processing, LED4 rainbow cycling
    TOOL_ACTIVE,  // Tool LED on + LED4 still cycling
};

// Returns LED index (0–3) for a tool name, or -1 if not mapped
int toolToLed(const String& tool);

// Returns LCD row-2 status text for a tool_start event
String toolToStatus(const String& tool);

class AiNotifyController {
public:
    AiNotifyController();
    void begin();

    // Called from the MQTT callback (Arduino task context)
    void onQuestion(const String& text);
    void onAiStart();
    void onToolStart(const String& tool);
    void onToolEnd(const String& tool);
    void onDone();
    void onWifiReconnecting();

    // Called from FreeRTOS tasks
    void tickRainbow();   // call every ~50 ms
    void tickMarquee();   // call every ~400 ms

    AppState getState() const { return _state; }

    // Shared mutex — must be held before touching strip or lcd
    SemaphoreHandle_t displayMutex;

private:
    Adafruit_NeoPixel _strip;
    LiquidCrystal_I2C _lcd;

    AppState _state;
    bool     _toolLedOn[4];
    uint8_t  _rainbowIndex;
    String   _currentQuestion;
    String   _statusText;
    int      _marqueeOffset;
    bool     _lcdAvailable;

    void setToolLed(int led, bool on);
    void updateLcdRow2();
    uint32_t rainbowColor(uint8_t pos);
};

// FreeRTOS task entry points — pass AiNotifyController* as param
void ledRainbowTask(void* param);
void lcdMarqueeTask(void* param);
