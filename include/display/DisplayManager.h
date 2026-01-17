#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Colors definitions
static constexpr uint16_t LCD_BLACK = 0x0000;
static constexpr uint16_t LCD_WHITE = 0xFFFF;
static constexpr uint16_t LCD_RED = 0xF800;
static constexpr uint16_t LCD_GREEN = 0x07E0;
static constexpr uint16_t LCD_BLUE = 0x001F;

static constexpr int ONE_LINE_SPACE = 20;
static constexpr int TWO_LINES_SPACE = 40;
static constexpr int THREE_LINES_SPACE = 60;

class DisplayManager {
   public:
    static void begin();
    static bool isReady();
    static void ensureInit();
    static Arduino_GFX* getGfx();
    static void drawStartup(String currentIP);
    static void drawTextWrapped(int16_t xPos, int16_t yPos, const String& text, uint8_t textSize, uint16_t fgColor,
                                uint16_t bgColor, bool clearBg);
    static void drawLoadingBar(float progress, int yPos = 180, int barWidth = 200, int barHeight = 20,
                               uint16_t fgColor = 0x07E0, uint16_t bgColor = 0x39E7);
    static bool playGifFullScreen(const String& path, uint32_t timeMs = 0);
    static bool stopGif();
    static void update();
    static void clearScreen();
};
