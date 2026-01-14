#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Colors definitions
static constexpr uint16_t LCD_BLACK = 0x0000;
static constexpr uint16_t LCD_WHITE = 0xFFFF;
static constexpr uint16_t LCD_RED = 0xF800;
static constexpr uint16_t LCD_GREEN = 0x07E0;
static constexpr uint16_t LCD_BLUE = 0x001F;

class DisplayManager {
   public:
    static void begin();
    static bool isReady();
    static void drawStartup();
    static void drawTextWrapped(int16_t xPos, int16_t yPos, const String& text, uint8_t textSize, uint16_t fgColor,
                                uint16_t bgColor, bool clearBg);
    static void drawLoadingBar(float progress, int yPos = 180, int barWidth = 200, int barHeight = 20,
                               uint16_t fgColor = 0x07E0, uint16_t bgColor = 0x39E7);
};
