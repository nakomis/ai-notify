#include "AiNotifyController.h"
#include "SDLogger.h"

// Tool LED colours at 100% brightness; global setBrightness(128) halves them
static const uint32_t TOOL_COLOURS[4] = {
    Adafruit_NeoPixel::Color(255, 140,   0),   // CV: dark orange
    Adafruit_NeoPixel::Color(  0, 119, 181),   // LinkedIn: blue
    Adafruit_NeoPixel::Color(240, 240, 240),   // GitHub: near-white
    Adafruit_NeoPixel::Color( 56, 152,  63),   // Blog: green
};

int toolToLed(const String& tool) {
    if (tool == "get_cv")                                                  return LED_CV;
    if (tool == "get_linkedin_profile")                                    return LED_LINKEDIN;
    if (tool == "get_github_readme" ||
        tool == "list_repo_files"   ||
        tool == "read_repo_file")                                          return LED_GITHUB;
    if (tool == "get_blog_posts" || tool == "get_interests")               return LED_BLOG;
    return -1;
}

String toolToStatus(const String& tool) {
    if (tool == "get_cv")                                                  return "Searching CV...";
    if (tool == "get_linkedin_profile")                                    return "Searching LinkedIn";
    if (tool == "get_github_readme" ||
        tool == "list_repo_files"   ||
        tool == "read_repo_file")                                          return "Searching GitHub";
    if (tool == "get_blog_posts" || tool == "get_interests")               return "Searching Blog...";
    return "Working...";
}

AiNotifyController::AiNotifyController()
    : _strip(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800),
      _lcd(LCD_ADDR, LCD_COLS, LCD_ROWS),
      _state(AppState::IDLE),
      _toolLedOn{false, false, false, false},
      _rainbowIndex(0),
      _currentQuestion("nakom.is"),
      _statusText("Ready"),
      _marqueeOffset(0),
      _lcdAvailable(false),
      displayMutex(nullptr)
{}

void AiNotifyController::begin() {
    displayMutex = xSemaphoreCreateMutex();

    _strip.begin();
    _strip.setBrightness(NEOPIXEL_BRIGHTNESS);
    _strip.clear();
    _strip.show();

    Wire.begin(8, 9);   // SDA=GPIO8, SCL=GPIO9
    _lcd.init();
    _lcdAvailable = true;
    _lcd.backlight();
    _lcd.clear();
    _lcd.setCursor(0, 0);
    _lcd.print("nakom.is");
    _lcd.setCursor(0, 1);
    _lcd.print("Ready");

    LOG_I("AiNotifyController: ready");
}

void AiNotifyController::onQuestion(const String& text) {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    _currentQuestion = text;
    _marqueeOffset   = 0;
    _statusText      = "Thinking...";
    _state           = AppState::ACTIVE;
    updateLcdRow2();
    xSemaphoreGive(displayMutex);
}

void AiNotifyController::onAiStart() {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    _statusText = "Thinking...";
    if (_state == AppState::IDLE) _state = AppState::ACTIVE;
    updateLcdRow2();
    xSemaphoreGive(displayMutex);
}

void AiNotifyController::onToolStart(const String& tool) {
    int led = toolToLed(tool);
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if (led >= 0 && led < 4) setToolLed(led, true);
    _statusText = toolToStatus(tool);
    _state      = AppState::TOOL_ACTIVE;
    updateLcdRow2();
    xSemaphoreGive(displayMutex);
}

void AiNotifyController::onToolEnd(const String& tool) {
    int led = toolToLed(tool);
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if (led >= 0 && led < 4) setToolLed(led, false);
    bool anyOn = false;
    for (int i = 0; i < 4; i++) anyOn |= _toolLedOn[i];
    _state = anyOn ? AppState::TOOL_ACTIVE : AppState::ACTIVE;
    xSemaphoreGive(displayMutex);
}

void AiNotifyController::onDone() {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    for (int i = 0; i < 4; i++) {
        _toolLedOn[i] = false;
        _strip.setPixelColor(i, (uint32_t)0);
    }
    _strip.setPixelColor(LED_AI, (uint32_t)0);
    _strip.show();
    _state      = AppState::IDLE;
    _statusText = "Ready";
    updateLcdRow2();
    xSemaphoreGive(displayMutex);
}

void AiNotifyController::onWifiReconnecting() {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    _statusText = "Reconnecting...";
    updateLcdRow2();
    xSemaphoreGive(displayMutex);
}

void AiNotifyController::setToolLed(int led, bool on) {
    _toolLedOn[led] = on;
    _strip.setPixelColor(led, on ? TOOL_COLOURS[led] : (uint32_t)0);
    _strip.show();
}

void AiNotifyController::updateLcdRow2() {
    if (!_lcdAvailable) return;
    _lcd.setCursor(0, 1);
    String s = _statusText;
    while ((int)s.length() < LCD_COLS) s += ' ';   // pad to clear old text
    _lcd.print(s.substring(0, LCD_COLS));
}

void AiNotifyController::tickRainbow() {
    if (_state == AppState::IDLE) return;
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    _strip.setPixelColor(LED_AI, rainbowColor(_rainbowIndex));
    _strip.show();
    _rainbowIndex += 3;   // wraps naturally at uint8_t overflow (256)
    xSemaphoreGive(displayMutex);
}

void AiNotifyController::tickMarquee() {
    if (!_lcdAvailable) return;

    // Capture question atomically, then release mutex before slow LCD writes
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    String q = _currentQuestion;
    xSemaphoreGive(displayMutex);

    String display;
    if ((int)q.length() <= LCD_COLS) {
        // Short question: static display
        display = q;
        while ((int)display.length() < LCD_COLS) display += ' ';
    } else {
        // Long question: scroll a 16-char window
        String padded = q + "    ";   // 4-space gap before wrap-around
        int len = padded.length();
        display = "";
        for (int i = 0; i < LCD_COLS; i++) {
            display += padded.charAt((_marqueeOffset + i) % len);
        }
        _marqueeOffset = (_marqueeOffset + 1) % len;
    }

    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    if (_lcdAvailable) {
        _lcd.setCursor(0, 0);
        _lcd.print(display);
    }
    xSemaphoreGive(displayMutex);
}

uint32_t AiNotifyController::rainbowColor(uint8_t pos) {
    // Standard HSV wheel: Red(0) → Yellow → Green(85) → Cyan → Blue(170) → Purple → Red(255)
    if (pos < 85) {
        return Adafruit_NeoPixel::Color(255 - pos * 3,  pos * 3,        0);
    } else if (pos < 170) {
        pos -= 85;
        return Adafruit_NeoPixel::Color(0,              255 - pos * 3,  pos * 3);
    } else {
        pos -= 170;
        return Adafruit_NeoPixel::Color(pos * 3,        0,              255 - pos * 3);
    }
}

void ledRainbowTask(void* param) {
    AiNotifyController* ctrl = static_cast<AiNotifyController*>(param);
    for (;;) {
        ctrl->tickRainbow();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void lcdMarqueeTask(void* param) {
    AiNotifyController* ctrl = static_cast<AiNotifyController*>(param);
    for (;;) {
        ctrl->tickMarquee();
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}
