
#include <Arduino_GFX_Library.h>
#include <EEPROM.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>


#include <AnimatedGIF.h>
#define GIF_ENABLE 1

const char *DEFAULT_SSID = "";
const char *DEFAULT_PASSWORD = "";
const char *AP_SSID = "ESP_RESCUE";
const char *AP_PASSWORD = "12345678";

static constexpr bool LCD_ENABLE = true;
static constexpr int16_t LCD_W = 240;
static constexpr int16_t LCD_H = 240;
static constexpr uint8_t LCD_ROTATION = 4; // 4=portrait upside-down for the cube
static constexpr int8_t LCD_MOSI_GPIO = 13;
static constexpr int8_t LCD_SCK_GPIO = 14;
static constexpr int8_t LCD_CS_GPIO = 2;
static constexpr int8_t LCD_DC_GPIO = 0;
static constexpr int8_t LCD_RST_GPIO = 15;
static constexpr bool LCD_CS_ACTIVE_HIGH = true;
static constexpr bool LCD_DC_CMD_HIGH = false;
static constexpr uint8_t LCD_SPI_MODE = 0;
static constexpr bool LCD_KEEP_CS_ASSERTED = true;
static constexpr uint32_t LCD_SPI_HZ = 80000000;

static constexpr int8_t LCD_BACKLIGHT_GPIO = 5;
static constexpr bool LCD_BACKLIGHT_ACTIVE_LOW = true;

static constexpr uint16_t LCD_BLACK = 0x0000;
static constexpr uint16_t LCD_WHITE = 0xFFFF;
static constexpr uint16_t LCD_RED = 0xF800;
static constexpr uint16_t LCD_GREEN = 0x07E0;
static constexpr uint16_t LCD_BLUE = 0x001F;

static constexpr const char *GIF_DIR = "/gifs";
static constexpr uint32_t GIF_MAX_MS_PER_FILE = 20000;
static constexpr uint8_t GIF_TARGET_FPS = 30;
static constexpr uint32_t GIF_FRAME_MS = 1000 / GIF_TARGET_FPS;

static volatile bool g_gifPlayRequested = false;
static bool g_gifPlaying = false;
static volatile bool g_gifLoopEnabled = false;
static volatile bool g_gifStopRequested = false;

static File g_uploadFile;
static String g_uploadPath;
static bool g_uploadFsMounted = false;
static uint32_t g_uploadBytesThisFile = 0;
static String g_lastUploadStatus;

static inline void lcdBacklightOn() {
  if (LCD_BACKLIGHT_GPIO < 0)
    return;
  pinMode((uint8_t)LCD_BACKLIGHT_GPIO, OUTPUT);
  digitalWrite((uint8_t)LCD_BACKLIGHT_GPIO, LCD_BACKLIGHT_ACTIVE_LOW ? LOW : HIGH);
}

static Arduino_DataBus *g_lcdBus = nullptr;
static Arduino_GFX *g_lcd = nullptr;
static bool g_lcdReady = false;
static bool g_lcdInitializing = false;
static uint32_t g_lcdInitAttempts = 0;
static uint32_t g_lcdInitLastMs = 0;
static bool g_lcdInitOk = false;

static void lcdEnsureInit();
static void lcdShutdown();

static void gifPlayAllFromLittleFS();
static void ensureGifsDirMounted();
static String htmlEscape(const String &s);

extern ESP8266WebServer server;

static void lcdDrawTextWrapped(int16_t x, int16_t y, const String &text, uint8_t textSize, uint16_t fg,
                               uint16_t bg, bool clearBg) {
  if (!LCD_ENABLE)
    return;

  if (!g_lcdReady) {
    if (g_lcdInitializing)
      return;
    lcdEnsureInit();
  }
  if (!g_lcdReady || g_lcd == nullptr)
    return;

  const int16_t screenW = (int16_t)g_lcd->width();
  const int16_t screenH = (int16_t)g_lcd->height();

  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x >= screenW || y >= screenH)
    return;

  const int16_t charW = (int16_t)(6 * textSize);
  const int16_t charH = (int16_t)(8 * textSize);
  if (charW <= 0 || charH <= 0)
    return;

  const int16_t maxCharsPerLine = (int16_t)((screenW - x) / charW);
  const int16_t maxLines = (int16_t)((screenH - y) / charH);
  if (maxCharsPerLine <= 0 || maxLines <= 0)
    return;

  String lines[10];
  const int16_t maxLineSlots = (int16_t)(sizeof(lines) / sizeof(lines[0]));
  int16_t lineCount = 0;
  String line;
  String word;

  auto pushLine = [&]() {
    if (lineCount >= maxLines || lineCount >= maxLineSlots)
      return;
    lines[lineCount++] = line;
    line = "";
  };

  auto appendWord = [&]() {
    if (!word.length())
      return;

    if ((int16_t)word.length() > maxCharsPerLine) {
      if (line.length()) {
        pushLine();
        if (lineCount >= maxLines || lineCount >= maxLineSlots) {
          word = "";
          return;
        }
      }

      line = word;
      word = "";
      return;
    }

    if (!line.length()) {
      line = word;
      word = "";
      return;
    }

    if ((int16_t)(line.length() + 1 + word.length()) <= maxCharsPerLine) {
      line += ' ';
      line += word;
      word = "";
      return;
    }

    pushLine();
    if (lineCount >= maxLines || lineCount >= maxLineSlots) {
      word = "";
      return;
    }
    line = word;
    word = "";
  };

  for (uint32_t i = 0; i < text.length(); i++) {
    char c = text.charAt(i);
    if (c == '\r')
      continue;
    if (c == '\n') {
      appendWord();
      pushLine();
      continue;
    }
    if (c == ' ' || c == '\t') {
      appendWord();
      continue;
    }
    word += c;
  }
  appendWord();
  if (line.length() && lineCount < maxLines && lineCount < maxLineSlots) {
    lines[lineCount++] = line;
  }
  if (lineCount <= 0)
    lineCount = 1;

  if (clearBg) {
    g_lcd->fillRect(x, y, screenW - x, (int16_t)(lineCount * charH), bg);
  }

  g_lcd->setTextSize(textSize);
  g_lcd->setTextColor(fg, bg);
  for (int16_t li = 0; li < lineCount; li++) {
    g_lcd->setCursor(x, (int16_t)(y + li * charH));
    g_lcd->print(lines[li]);
  }
}

static inline void ST7789_WriteCommand(uint8_t cmd) {
  if (g_lcdBus == nullptr)
    return;
  g_lcdBus->writeCommand(cmd);
}

static inline void ST7789_WriteData(uint8_t data) {
  if (g_lcdBus == nullptr)
    return;
  g_lcdBus->write(data);
}

static void lcdRunVendorInit() {
  if (g_lcdBus == nullptr)
    return;

  g_lcdBus->beginWrite();

  ST7789_WriteCommand(0x11);
  delay(120);
  yield();

  ST7789_WriteCommand(0xB2);
  ST7789_WriteData(0x1F);
  ST7789_WriteData(0x1F);
  ST7789_WriteData(0x00);
  ST7789_WriteData(0x33);
  ST7789_WriteData(0x33);
  yield();

  ST7789_WriteCommand(0x35);
  ST7789_WriteData(0x00);
  yield();

  ST7789_WriteCommand(0x36);
  ST7789_WriteData(0x00);
  yield();

  ST7789_WriteCommand(0x3A);
  ST7789_WriteData(0x05);
  yield();

  ST7789_WriteCommand(0xB7);
  ST7789_WriteData(0x00);
  yield();

  ST7789_WriteCommand(0xBB);
  ST7789_WriteData(0x36);
  yield();

  ST7789_WriteCommand(0xC0);
  ST7789_WriteData(0x2C);
  yield();

  ST7789_WriteCommand(0xC2);
  ST7789_WriteData(0x01);
  yield();

  ST7789_WriteCommand(0xC3);
  ST7789_WriteData(0x13);
  yield();

  ST7789_WriteCommand(0xC4);
  ST7789_WriteData(0x20);
  yield();

  ST7789_WriteCommand(0xC6);
  ST7789_WriteData(0x13);
  yield();

  ST7789_WriteCommand(0xD6);
  ST7789_WriteData(0xA1);
  yield();

  ST7789_WriteCommand(0xD0);
  ST7789_WriteData(0xA4);
  ST7789_WriteData(0xA1);
  yield();

  ST7789_WriteCommand(0xD6);
  ST7789_WriteData(0xA1);
  yield();

  ST7789_WriteCommand(0xE0);
  ST7789_WriteData(0xF0);
  ST7789_WriteData(0x08);
  ST7789_WriteData(0x0E);
  ST7789_WriteData(0x09);
  ST7789_WriteData(0x08);
  ST7789_WriteData(0x04);
  ST7789_WriteData(0x2F);
  ST7789_WriteData(0x33);
  ST7789_WriteData(0x45);
  ST7789_WriteData(0x36);
  ST7789_WriteData(0x13);
  ST7789_WriteData(0x12);
  ST7789_WriteData(0x2A);
  ST7789_WriteData(0x2D);
  yield();

  ST7789_WriteCommand(0xE1);
  ST7789_WriteData(0xF0);
  ST7789_WriteData(0x0E);
  ST7789_WriteData(0x12);
  ST7789_WriteData(0x0C);
  ST7789_WriteData(0x0A);
  ST7789_WriteData(0x15);
  ST7789_WriteData(0x2E);
  ST7789_WriteData(0x32);
  ST7789_WriteData(0x44);
  ST7789_WriteData(0x39);
  ST7789_WriteData(0x17);
  ST7789_WriteData(0x18);
  ST7789_WriteData(0x2B);
  ST7789_WriteData(0x2F);
  yield();

  ST7789_WriteCommand(0xE4);
  ST7789_WriteData(0x1D);
  ST7789_WriteData(0x00);
  ST7789_WriteData(0x00);
  yield();

  ST7789_WriteCommand(0x21);
  yield();

  ST7789_WriteCommand(0x29);
  yield();

  ST7789_WriteCommand(0x2A);
  ST7789_WriteData(0x00);
  ST7789_WriteData(0x00);
  ST7789_WriteData(0x00);
  ST7789_WriteData(0xEF);
  yield();

  ST7789_WriteCommand(0x2B);
  ST7789_WriteData(0x00);
  ST7789_WriteData(0x00);
  ST7789_WriteData(0x00);
  ST7789_WriteData(0xEF);
  yield();

  ST7789_WriteCommand(0x2C);
  yield();

  g_lcdBus->endWrite();
}

class ESP8266SPIWithCustomCS : public Arduino_DataBus {
public:
  ESP8266SPIWithCustomCS(int8_t dc, int8_t cs, bool csActiveHigh, int32_t defaultSpeed, int8_t defaultDataMode)
      : _spi(dc, GFX_NOT_DEFINED, &SPI, true), _cs(cs), _csActiveHigh(csActiveHigh), _defaultSpeed(defaultSpeed),
        _defaultDataMode(defaultDataMode) {}

  bool begin(int32_t speed = GFX_NOT_DEFINED, int8_t dataMode = GFX_NOT_DEFINED) override {
    if (speed == GFX_NOT_DEFINED)
      speed = _defaultSpeed;
    if (dataMode == GFX_NOT_DEFINED)
      dataMode = _defaultDataMode;

    if (_cs != GFX_NOT_DEFINED) {
      pinMode((uint8_t)_cs, OUTPUT);
      digitalWrite((uint8_t)_cs, _csActiveHigh ? LOW : HIGH);
    }
    return _spi.begin(speed, dataMode);
  }

  void beginWrite() override {
    if (_cs != GFX_NOT_DEFINED) {
      digitalWrite((uint8_t)_cs, _csActiveHigh ? HIGH : LOW);
    }
    _spi.beginWrite();
  }

  void endWrite() override {
    _spi.endWrite();
    if (LCD_KEEP_CS_ASSERTED)
      return;
    if (_cs != GFX_NOT_DEFINED) {
      digitalWrite((uint8_t)_cs, _csActiveHigh ? LOW : HIGH);
    }
  }

  void writeCommand(uint8_t c) override { _spi.writeCommand(c); }
  void writeCommand16(uint16_t c) override { _spi.writeCommand16(c); }
  void writeCommandBytes(uint8_t *data, uint32_t len) override { _spi.writeCommandBytes(data, len); }
  void write(uint8_t d) override { _spi.write(d); }
  void write16(uint16_t d) override { _spi.write16(d); }
  void writeRepeat(uint16_t p, uint32_t len) override { _spi.writeRepeat(p, len); }
  void writeBytes(uint8_t *data, uint32_t len) override { _spi.writeBytes(data, len); }
  void writePixels(uint16_t *data, uint32_t len) override { _spi.writePixels(data, len); }

private:
  Arduino_HWSPI _spi;
  int8_t _cs;
  bool _csActiveHigh;
  int32_t _defaultSpeed;
  int8_t _defaultDataMode;
};

#if GIF_ENABLE
static AnimatedGIF *g_gif = nullptr;
static int16_t g_gifOffsetX = 0;
static int16_t g_gifOffsetY = 0;

static void *GIFOpenFile(const char *fname, int32_t *pSize) {
  String path(fname);
  if (!path.startsWith("/"))
    path = "/" + path;

  File *f = new File();
  if (f == nullptr)
    return nullptr;
  *f = LittleFS.open(path, "r");
  if (!(*f)) {
    delete f;
    return nullptr;
  }
  *pSize = (int32_t)f->size();
  Serial.printf("GIF: File opened, handle=%p, size=%d\n", (void*)f, *pSize);
  return (void *)f;
}

static void GIFCloseFile(void *pHandle) {
  File *f = (File *)pHandle;
  if (f == nullptr)
    return;
  if (*f) {
    f->close();
    Serial.printf("GIF: File closed, handle=%p\n", pHandle);
  }
  delete f;
}

static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  File *f = (File *)pFile->fHandle;
  if (f == nullptr || !(*f)) {
    return 0;
  }

  int32_t bytesToRead = iLen;
  const int32_t remaining = pFile->iSize - pFile->iPos;
  if (remaining < bytesToRead)
    bytesToRead = remaining;
  if (bytesToRead <= 0)
    return 0;

  const int32_t bytesRead = (int32_t)f->read(pBuf, (size_t)bytesToRead);
  if (bytesRead > 0)
    pFile->iPos += bytesRead;
    
  return bytesRead;
}

static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  File *f = (File *)pFile->fHandle;
  if (f == nullptr || !(*f))
    return 0;

  if (iPosition < 0)
    iPosition = 0;
  else if (iPosition >= pFile->iSize)
    iPosition = pFile->iSize - 1;

  pFile->iPos = iPosition;
  (void)f->seek((uint32_t)iPosition, SeekSet);
  return iPosition;
}

static void GIFDraw(GIFDRAW *pDraw) {
  if (!LCD_ENABLE)
    return;
  if (!g_lcdReady || g_lcd == nullptr)
    return;

  Arduino_TFT *tft = (Arduino_TFT *)g_lcd;
  static bool s_inFrameWrite = false;
  if (pDraw->y == 0) {
    tft->startWrite();
    s_inFrameWrite = true;
  }

  const uint16_t *palette565 = (const uint16_t *)pDraw->pPalette;
  const uint8_t *src = pDraw->pPixels;

  const int16_t y = (int16_t)(pDraw->iY + pDraw->y + g_gifOffsetY);
  const int16_t x = (int16_t)(pDraw->iX + g_gifOffsetX);
  const int16_t w = (int16_t)pDraw->iWidth;

  bool skipDraw = false;
  if (w <= 0)
    skipDraw = true;
  if (y < 0 || y >= (int16_t)g_lcd->height())
    skipDraw = true;

  static uint16_t lineBuf[240];
  const int16_t maxW = (int16_t)(sizeof(lineBuf) / sizeof(lineBuf[0]));
  const int16_t drawW = (w > maxW) ? maxW : w;

  static bool s_havePrev = false;
  static uint8_t s_prevDisposal = 0;
  static bool s_prevHadTransparency = false;
  static int16_t s_prevX = 0, s_prevY = 0, s_prevW = 0, s_prevH = 0;
  static uint16_t s_prevBg = LCD_BLACK;

  static uint8_t s_curDisposal = 0;
  static bool s_curHadTransparency = false;
  static int16_t s_curX = 0, s_curY = 0, s_curW = 0, s_curH = 0;
  static uint16_t s_curBg = LCD_BLACK;
  if (pDraw->y == 0) {
    s_curDisposal = pDraw->ucDisposalMethod;
    s_curHadTransparency = (pDraw->ucHasTransparency != 0);
    s_curX = (int16_t)(pDraw->iX + g_gifOffsetX);
    s_curY = (int16_t)(pDraw->iY + g_gifOffsetY);
    s_curW = (int16_t)pDraw->iWidth;
    s_curH = (int16_t)pDraw->iHeight;
    s_curBg = LCD_BLACK;
  }

  const int16_t screenW = (int16_t)g_lcd->width();
  if (x >= screenW || (x + drawW) <= 0)
    skipDraw = true;
  int16_t visStart = 0;
  int16_t visEnd = drawW;
  if (x < 0)
    visStart = (int16_t)(-x);
  if ((int16_t)(x + visEnd) > screenW)
    visEnd = (int16_t)(screenW - x);
  if (visEnd <= visStart)
    skipDraw = true;

  const bool endOfFrame = (pDraw->y == (pDraw->iHeight - 1));

  bool needClearLine = false;
  int16_t clearStart = 0;
  int16_t clearEnd = 0;
  if (s_havePrev && (s_prevDisposal == 2 || s_prevHadTransparency)) {
    const int16_t prevTop = s_prevY;
    const int16_t prevBot = (int16_t)(s_prevY + s_prevH - 1);
    if (y >= prevTop && y <= prevBot) {
      needClearLine = true;
      clearStart = s_prevX;
      clearEnd = (int16_t)(s_prevX + s_prevW);
    }
  }

  const bool curValid = (!skipDraw);
  const int16_t curStart = (int16_t)(x + visStart);
  const int16_t curEnd = (int16_t)(x + visEnd);

  if (needClearLine || curValid) {
    const int16_t screenW2 = (int16_t)g_lcd->width();
    int16_t uStart = curValid ? curStart : clearStart;
    int16_t uEnd = curValid ? curEnd : clearEnd;
    if (needClearLine) {
      if (clearStart < uStart)
        uStart = clearStart;
      if (clearEnd > uEnd)
        uEnd = clearEnd;
    }

    if (uStart < 0)
      uStart = 0;
    if (uEnd > screenW2)
      uEnd = screenW2;
    const int16_t uLen = (int16_t)(uEnd - uStart);
    if (uLen > 0) {
      for (int16_t i = 0; i < uLen; i++) {
        lineBuf[i] = LCD_BLACK;
      }

      if (curValid) {
        if (!pDraw->ucHasTransparency) {
          for (int16_t i = visStart; i < visEnd; i++) {
            const int16_t dx = (int16_t)(x + i);
            const int16_t di = (int16_t)(dx - uStart);
            if ((uint16_t)di < (uint16_t)uLen)
              lineBuf[di] = palette565[src[i]];
          }
        } else {
          const uint8_t t = pDraw->ucTransparent;
          for (int16_t i = visStart; i < visEnd; i++) {
            const uint8_t idx = src[i];
            if (idx == t)
              continue;
            const int16_t dx = (int16_t)(x + i);
            const int16_t di = (int16_t)(dx - uStart);
            if ((uint16_t)di < (uint16_t)uLen)
              lineBuf[di] = palette565[idx];
          }
        }
      }

      tft->writeAddrWindow(uStart, y, (uint16_t)uLen, 1);
      tft->writePixels(lineBuf, (uint32_t)uLen);
    }
  }

  if (endOfFrame) {
    if (s_inFrameWrite) {
      tft->endWrite();
      s_inFrameWrite = false;
    }

    s_havePrev = true;
    s_prevDisposal = s_curDisposal;
    s_prevHadTransparency = s_curHadTransparency;
    s_prevX = s_curX;
    s_prevY = s_curY;
    s_prevW = s_curW;
    s_prevH = s_curH;
    s_prevBg = s_curBg;
  }
  return;
}

static bool gifPlayOne(const String &path) {
  if (!LCD_ENABLE)
    return false;

  if (g_gif == nullptr) {
    Serial.println("GIF: decoder not allocated");
    return false;
  }

  Serial.printf("Free heap before playing: %u bytes\n", ESP.getFreeHeap());

  lcdEnsureInit();
  if (!g_lcdReady || g_lcd == nullptr)
    return false;

  g_gifOffsetX = 0;
  g_gifOffsetY = 0;

  g_gif->begin(GIF_PALETTE_RGB565_LE);
  if (!g_gif->open(path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
    int err = g_gif->getLastError();
    Serial.printf("GIF: open failed: %s (err=%d)\n", path.c_str(), err);
    return false;
  }

  Serial.printf("Free heap after open: %u bytes\n", ESP.getFreeHeap());

  const int16_t gifW = (int16_t)g_gif->getCanvasWidth();
  const int16_t gifH = (int16_t)g_gif->getCanvasHeight();
  Serial.printf("GIF dimensions: %dx%d\n", gifW, gifH);
  g_gifOffsetX = (int16_t)((LCD_W - gifW) / 2);
  g_gifOffsetY = (int16_t)((LCD_H - gifH) / 2);

  const uint32_t startMs = millis();
  int delayMsFromGif = 0;
  int frameCount = 0;
  Serial.println("GIF: Starting frame loop");

  ESP.wdtDisable();
  
  while (true) {
    const uint32_t frameStart = millis();

    int rc = g_gif->playFrame(false /*bSync*/, &delayMsFromGif, nullptr);
    frameCount++;
    
    if (rc < 0) {
      break;
    }
    if (rc == 0) {
      break;
    }

    uint32_t targetMs = GIF_FRAME_MS;
    if (delayMsFromGif > 0 && (uint32_t)delayMsFromGif > targetMs)
      targetMs = (uint32_t)delayMsFromGif;

    const uint32_t elapsed = millis() - frameStart;
    if (elapsed < targetMs) {
      uint32_t remaining = targetMs - elapsed;
      uint32_t delayStart = millis();
      while ((millis() - delayStart) < remaining) {
        if (g_gifStopRequested)
          break;
      }
    }

    if (g_gifStopRequested) {
      break;
    }
    if ((millis() - startMs) > GIF_MAX_MS_PER_FILE) {
      break;
    }
  }

  ESP.wdtEnable(0);
  
  g_gif->close();
  
  Serial.printf("GIF: Playback complete, %d frames\n", frameCount);
  return true;
}
#endif

static void lcdShutdown() {
  g_lcdReady = false;
  g_lcdInitializing = false;
  if (g_lcd != nullptr) {
    delete g_lcd;
    g_lcd = nullptr;
  }
  if (g_lcdBus != nullptr) {
    delete g_lcdBus;
    g_lcdBus = nullptr;
  }
}

static void lcdHardReset() {
  if (LCD_RST_GPIO < 0)
    return;
  pinMode((uint8_t)LCD_RST_GPIO, OUTPUT);
  digitalWrite((uint8_t)LCD_RST_GPIO, HIGH);
  delay(10);
  digitalWrite((uint8_t)LCD_RST_GPIO, LOW);
  delay(20);
  digitalWrite((uint8_t)LCD_RST_GPIO, HIGH);
  delay(120);
}

static void lcdEnsureInit() {
  if (!LCD_ENABLE)
    return;
  if (g_lcdReady)
    return;
  if (g_lcdInitializing)
    return;

  g_lcdInitializing = true;
  g_lcdInitAttempts++;
  g_lcdInitLastMs = millis();
  g_lcdInitOk = false;

  Serial.printf("LCD: init start (SPI mode=%u, hz=%lu, CS=%s, DC cmd=%s)\n", (unsigned)LCD_SPI_MODE,
                (unsigned long)LCD_SPI_HZ, LCD_CS_ACTIVE_HIGH ? "AH" : "AL", LCD_DC_CMD_HIGH ? "H" : "L");

  lcdBacklightOn();

  lcdHardReset();

  if (g_lcd != nullptr) {
    delete g_lcd;
    g_lcd = nullptr;
  }
  if (g_lcdBus != nullptr) {
    delete g_lcdBus;
    g_lcdBus = nullptr;
  }

  g_lcdBus = new ESP8266SPIWithCustomCS(LCD_DC_GPIO, LCD_CS_GPIO, LCD_CS_ACTIVE_HIGH, (int32_t)LCD_SPI_HZ,
                                        (int8_t)LCD_SPI_MODE);
  g_lcd = new Arduino_ST7789(g_lcdBus, -1 /*RST handled manually*/, LCD_ROTATION /*rotation*/, true, LCD_W, LCD_H);

  g_lcdBus->begin((int32_t)LCD_SPI_HZ, (int8_t)LCD_SPI_MODE);

  g_lcd->begin();
  delay(10);

  lcdHardReset();
  g_lcdBus->begin((int32_t)LCD_SPI_HZ, (int8_t)LCD_SPI_MODE);

  lcdRunVendorInit();

  g_lcd->setRotation(LCD_ROTATION);

  g_lcdReady = true;
  g_lcdInitializing = false;
  g_lcdInitOk = true;

  Serial.println("LCD: init done");

  g_lcd->fillScreen(LCD_BLACK);
  lcdDrawTextWrapped(10, 10, "HelloCubic Lite Open rescue firmware", 2, LCD_WHITE, LCD_BLACK, true);

  String info = "CPU freq: " + String(ESP.getCpuFreqMHz()) + " MHz\n";
  info += "Flash chip ID: 0x" + String(ESP.getFlashChipId(), HEX);
  info.toUpperCase();
  lcdDrawTextWrapped(10, 100, info, 2, LCD_WHITE, LCD_BLACK, true);

  const int16_t box = 18;
  const int16_t gap = 8;
  const int16_t boxY = 150;
  g_lcd->fillRect(10, boxY, (int16_t)(box * 3 + gap * 2), box, LCD_BLACK);
  g_lcd->fillRect(10, boxY, box, box, LCD_RED);
  g_lcd->fillRect((int16_t)(10 + box + gap), boxY, box, box, LCD_GREEN);
  g_lcd->fillRect((int16_t)(10 + (box + gap) * 2), boxY, box, box, LCD_BLUE);
}

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

String dumpFlashSection(uint32_t addr, uint32_t length) {
  String out;
  uint32_t processed = 0;
  uint32_t buffer[64];

  while (processed < length) {
    uint32_t chunk = (length - processed) > sizeof(buffer)
                         ? sizeof(buffer)
                         : (length - processed);

    ESP.flashRead(addr + processed, buffer, chunk);
    uint8_t *bytes = (uint8_t *)buffer;

    for (uint32_t line = 0; line < chunk; line += 16) {
      uint32_t lineAddr = addr + processed + line;
      uint32_t lineBytes = min(16U, chunk - line);

      out += String(lineAddr, HEX);
      out += ": ";

      for (uint32_t i = 0; i < lineBytes; i++) {
        uint8_t b = bytes[line + i];
        if (b < 0x10)
          out += "0";
        out += String(b, HEX);
        out += " ";
      }

      out += " | ";
      for (uint32_t i = 0; i < lineBytes; i++) {
        uint8_t b = bytes[line + i];
        out += (b >= 0x20 && b <= 0x7E) ? (char)b : '.';
      }
      out += "\n";
    }

    processed += chunk;
  }

  return out;
}

String hexdumpBuffer(const uint8_t *data, uint32_t length, uint32_t baseAddr) {
  String out;
  for (uint32_t line = 0; line < length; line += 16) {
    uint32_t lineAddr = baseAddr + line;
    uint32_t lineBytes = min((uint32_t)16, length - line);

    out += String(lineAddr, HEX);
    out += ": ";

    for (uint32_t i = 0; i < lineBytes; i++) {
      uint8_t b = data[line + i];
      if (b < 0x10)
        out += "0";
      out += String(b, HEX);
      out += " ";
    }

    out += " | ";
    for (uint32_t i = 0; i < lineBytes; i++) {
      uint8_t b = data[line + i];
      out += (b >= 0x20 && b <= 0x7E) ? (char)b : '.';
    }
    out += "\n";
  }
  return out;
}
 
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\n=== HelloCubic Lite Open rescue firmware ===");

  WiFi.mode(WIFI_STA);
  WiFi.begin(DEFAULT_SSID, DEFAULT_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println("");

  bool apMode = false;
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    Serial.print("IP: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi Fail. Starting AP.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.print("AP IP: http://");
    Serial.println(WiFi.softAPIP());
    apMode = true;
  }

  httpUpdater.setup(&server, "/update");

  if (LCD_ENABLE) {
    lcdEnsureInit();
  }

  server.on("/", HTTP_GET, [apMode]() {
    IPAddress ip = apMode ? WiFi.softAPIP() : WiFi.localIP();

    String h = "<!DOCTYPE html><html><head><meta name='viewport' "
               "content='width=device-width, initial-scale=1'>"
               "<style>body{font-family:sans-serif;margin:20px;padding:0;"
               "background:#333;color:#fff}"
               ".btn{display:block;padding:15px;margin:10px 0;"
               "background:#6c757d;color:white;text-decoration:none;border:none;"
               "border-radius:5px;font-size:16px;cursor:pointer}"
               ".btn:hover{background:#5a6268}"
               ".card{background:#444;padding:20px;border-radius:10px;margin-bottom:20px}"
               "</style></head><body>";

    h += "<h1>HelloCubic Lite Open rescue firmware</h1>";
    h += "<div class='card'><h2>Status</h2>";
    h += "<p>Mode: <strong>" + String(apMode ? "AP" : "STA") + "</strong></p>";
    h += "<p>IP: <strong>http://" + ip.toString() + "/</strong></p>";
    h += "<p>LCD: <strong>" + String((g_lcdReady && g_lcd != nullptr && g_lcdInitOk) ? "ready" : "not ready") + "</strong></p>";
    h += "<p>LCD init attempts: <strong>" + String(g_lcdInitAttempts) + "</strong></p>";
    h += "<p>GIF: <strong>enabled</strong></p>";
    h += "</div>";

    h += "<div class='card'><h2>Tools</h2>";
    h += "<a href='/update' class='btn'>Firmware Update (OTA)</a>";
    h += "<a href='/nvs' class='btn'>Read NVS / System Params</a>";
    h += "<a href='/eeprom' class='btn'>Read EEPROM</a>";
    h += "<a href='/gifs' class='btn'>Manage GIFs (Upload/List)</a>";
    h += "<a href='/reboot' class='btn'>Reboot</a>";
    h += "</div>";

    h += "<div class='card'><h2>LCD</h2>";
    h += "<form action='/lcd' method='get'>";
    h += "<label style='display:block;margin-bottom:5px;font-weight:bold'>Text:</label>";
    h += "<input name='text' placeholder='Text to display' maxlength='64' style='width:100%;padding:12px;border-radius:6px;border:0;margin-bottom:10px'>";
    h += "<div style='display:flex;gap:10px;margin-bottom:10px'>";
    h += "<div style='flex:1'><label style='display:block;margin-bottom:5px;font-weight:bold'>X:</label>";
    h += "<input name='x' type='number' value='10' min='0' max='239' style='width:100%;padding:12px;border-radius:6px;border:0'></div>";
    h += "<div style='flex:1'><label style='display:block;margin-bottom:5px;font-weight:bold'>Y:</label>";
    h += "<input name='y' type='number' value='10' min='0' max='239' style='width:100%;padding:12px;border-radius:6px;border:0'></div>";
    h += "<div style='flex:1'><label style='display:block;margin-bottom:5px;font-weight:bold'>Size:</label>";
    h += "<input name='size' type='number' value='2' min='1' max='10' style='width:100%;padding:12px;border-radius:6px;border:0'></div>";
    h += "</div>";
    h += "<div style='text-align:right'>";
    h += "<button class='btn' type='submit' style='display:inline-block;width:auto;padding:12px 30px'>Draw Text</button>";
    h += "</div>";
    h += "</form>";
    h += "<a href='/lcd-clear' class='btn'>Clear Screen</a>";
    h += "<a href='/lcd-reinit' class='btn'>Re-init LCD</a>";

    h += "</div>";

    h += "</body></html>";
    server.send(200, "text/html", h);
  });

  server.on("/gifs", HTTP_GET, []() {
    String h = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
               "<style>body{font-family:sans-serif;margin:20px;background:#333;color:#fff}"
               ".card{background:#444;padding:20px;border-radius:10px;margin-bottom:20px}"
               ".btn{display:block;padding:15px;margin:10px 0;background:#6c757d;color:white;text-decoration:none;"
               "border:none;border-radius:5px;font-size:16px;cursor:pointer}"
               ".btn:hover{background:#5a6268}"
               "input[type=file]{width:100%;padding:12px;background:#222;color:#fff;border-radius:6px;border:0}"
               "</style></head><body>";
    h += "<h1>GIFs on LittleFS</h1>";

    h += "<div class='card'><h2>Upload</h2>";
    if (g_lastUploadStatus.length()) {
      h += "<p>Last upload: <strong>" + htmlEscape(g_lastUploadStatus) + "</strong></p>";
    }
    h += "<p>Upload one or more <strong>.gif</strong> files to <code>/gifs</code> in LittleFS.</p>";
    h += "<form method='POST' action='/upload-gif' enctype='multipart/form-data' onsubmit='return validateUpload()'>";
    h += "<input type='file' id='gifFile' name='gif' accept='.gif,image/gif' multiple>";
    h += "<button class='btn' type='submit'>Upload</button>";
    h += "</form>";
    h += "<script>function validateUpload(){var f=document.getElementById('gifFile');if(!f.files.length){alert('Please select at least one GIF file');return false;}return true;}</script>";
    h += "<a class='btn' href='/gif-loop-start'>Start GIF Loop</a>";
    h += "<a class='btn' href='/gif-loop-stop'>Stop GIF Loop</a>";
    h += "<a class='btn' href='/gifs-clear'>Delete ALL GIFs</a>";
    h += "</div>";

    h += "<div class='card'><h2>Filesystem</h2><pre>";
    if (!LittleFS.begin()) {
      h += "LittleFS mount failed\n";
    } else {
      uint32_t flashReal = ESP.getFlashChipRealSize();
      uint32_t flashCfg = ESP.getFlashChipSize();
      h += "Flash real:   " + String(flashReal) + " bytes\n";
      h += "Flash config: " + String(flashCfg) + " bytes\n";
      if (flashCfg < flashReal) {
        h += "WARNING: firmware built for smaller flash size.\n";
        h += "Rebuild with eesz=4M2M (or matching) and OTA update.\n\n";
      }
      FSInfo fs_info;
      if (LittleFS.info(fs_info)) {
        h += "Total: " + String(fs_info.totalBytes) + " bytes\n";
        h += "Used:  " + String(fs_info.usedBytes) + " bytes\n";
        h += "Free:  " + String(fs_info.totalBytes - fs_info.usedBytes) + " bytes\n";
        h += "Block: " + String(fs_info.blockSize) + " bytes\n";
        h += "Page:  " + String(fs_info.pageSize) + " bytes\n";
      } else {
        h += "LittleFS.info() failed\n";
      }
      LittleFS.end();
    }
    h += "</pre></div>";

    h += "<div class='card'><h2>Current files</h2><pre>";
    if (!LittleFS.begin()) {
      h += "LittleFS mount failed\n";
    } else {
      ensureGifsDirMounted();
      Dir dir = LittleFS.openDir(GIF_DIR);
      int count = 0;
      int totalFiles = 0;
      while (dir.next()) {
        totalFiles++;
        String name = dir.fileName();
        size_t sz = dir.fileSize();
        
        h += htmlEscape(name) + "\t" + String(sz) + " bytes";
        
        if (!name.endsWith(".gif") && !name.endsWith(".GIF")) {
          h += "  (not a GIF, skipped)";
        } else {
          count++;
          if (sz == 0)
            h += "  <== upload failed (0 bytes)";
        }
        h += "\n";
      }
      if (totalFiles == 0)
        h += "(no files in " + String(GIF_DIR) + " directory)\n";
      else if (count == 0)
        h += "(no GIF files found, " + String(totalFiles) + " other files)\n";
      else
        h += "\nTotal: " + String(count) + " GIF file(s)\n";
      LittleFS.end();
    }
    h += "</pre><a class='btn' href='/'>Back</a></div>";

    h += "</body></html>";
    server.send(200, "text/html", h);
  });

  server.on("/gifs-clear", HTTP_GET, []() {
    if (!LittleFS.begin()) {
      server.send(500, "text/plain", "LittleFS mount failed");
      return;
    }
    ensureGifsDirMounted();
    Dir dir = LittleFS.openDir(GIF_DIR);
    int deleted = 0;
    int attempted = 0;
    while (dir.next()) {
      String name = dir.fileName();
      if (!name.endsWith(".gif") && !name.endsWith(".GIF"))
        continue;
      
      attempted++;
      
      String fullPath = String(GIF_DIR) + "/" + name;
      Serial.printf("Attempting to delete: %s\n", fullPath.c_str());
      
      if (LittleFS.remove(fullPath)) {
        deleted++;
        Serial.printf("  SUCCESS\n");
      } else {
        Serial.printf("  FAILED\n");
      }
      yield();
    }
    LittleFS.end();

    Serial.printf("Delete complete: %d/%d files deleted\n", deleted, attempted);

    server.send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<meta http-equiv='refresh' content='2;url=/gifs'>"
                "<title>Delete Result</title></head><body><h2>Delete Result:</h2><p>Deleted " +
                    String(deleted) + " of " + String(attempted) +
                    " GIF file(s).</p><a href='/gifs'>Back</a></body></html>");
  });

  server.on(
      "/upload-gif", HTTP_POST,
      []() {
        if (g_uploadFile)
          g_uploadFile.close();
        if (g_uploadFsMounted) {
          LittleFS.end();
          g_uploadFsMounted = false;
        }
        if (!g_lastUploadStatus.length())
          g_lastUploadStatus = "No file was uploaded or status not set";
        
        String responseMsg = g_lastUploadStatus;
        Serial.printf("UPLOAD COMPLETE: %s\n", responseMsg.c_str());
        
        server.send(200, "text/html",
                    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                    "<meta http-equiv='refresh' content='2;url=/gifs'>"
                    "<title>Upload Result</title></head><body><h2>Upload Result:</h2><p>" +
                        htmlEscape(responseMsg) +
                        "</p><a href='/gifs'>Back to GIFs page</a></body></html>");
      },
      []() {
        HTTPUpload &upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          g_lastUploadStatus = "";
          if (!g_uploadFsMounted) {
            g_uploadFsMounted = LittleFS.begin();
            if (!g_uploadFsMounted) {
              Serial.println("UPLOAD: LittleFS mount failed");
              g_lastUploadStatus = "LittleFS mount failed";
              return;
            }
          }
          ensureGifsDirMounted();

          FSInfo fs_info;
          if (LittleFS.info(fs_info)) {
            uint32_t freeBytes = fs_info.totalBytes - fs_info.usedBytes;
            const uint32_t slack = 4096;
            if ((uint32_t)upload.totalSize > (freeBytes > slack ? (freeBytes - slack) : 0)) {
              Serial.printf("UPLOAD: out of space (need=%u free=%u)\n", (unsigned)upload.totalSize,
                            (unsigned)freeBytes);
              g_lastUploadStatus = "out of space (rebuild with larger LittleFS: eesz=4M2M)";
              g_uploadPath = "";
              if (g_uploadFile)
                g_uploadFile.close();
              return;
            }
          }

          String filename = upload.filename;
          filename.replace("\\", "/");
          int slash = filename.lastIndexOf('/');
          if (slash >= 0)
            filename = filename.substring(slash + 1);

          filename.replace("..", "");
          filename.replace(":", "_");
          filename.replace("*", "_");
          filename.replace("?", "_");
          filename.replace("\"", "_");
          filename.replace("<", "_");
          filename.replace(">", "_");
          filename.replace("|", "_");
          filename.replace(" ", "_");

          if (!filename.endsWith(".gif") && !filename.endsWith(".GIF")) {
            Serial.printf("UPLOAD: skipping non-gif: %s\n", filename.c_str());
            g_lastUploadStatus = "skipped non-gif";
            return;
          }

          g_uploadPath = String(GIF_DIR) + "/" + filename;
          if (!g_uploadPath.startsWith("/"))
            g_uploadPath = "/" + g_uploadPath;

          Serial.printf("UPLOAD: start %s (%u bytes)\n", g_uploadPath.c_str(), (unsigned)upload.totalSize);
          if (g_uploadFile)
            g_uploadFile.close();
          g_uploadFile = LittleFS.open(g_uploadPath, "w");
          g_uploadBytesThisFile = 0;
          if (!g_uploadFile) {
            Serial.printf("UPLOAD: open failed: %s\n", g_uploadPath.c_str());
            g_lastUploadStatus = "open failed (check FS size/layout)";
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (g_uploadFile) {
            size_t written = g_uploadFile.write(upload.buf, upload.currentSize);
            g_uploadBytesThisFile += (uint32_t)written;
            if (written != upload.currentSize) {
              Serial.printf("UPLOAD: write failed (%u/%u) -> likely out of space\n", (unsigned)written,
                            (unsigned)upload.currentSize);
              g_lastUploadStatus = "write failed (likely out of space)";
              g_uploadFile.close();
              if (g_uploadPath.length())
                LittleFS.remove(g_uploadPath);
              g_uploadPath = "";
            }
          }
          yield();
        } else if (upload.status == UPLOAD_FILE_END) {
          if (g_uploadFile) {
            g_uploadFile.close();
          }
          Serial.printf("UPLOAD: done %s (%u bytes)\n", g_uploadPath.c_str(), (unsigned)g_uploadBytesThisFile);
          if (!g_lastUploadStatus.length()) {
            if (g_uploadBytesThisFile == 0) {
              g_lastUploadStatus = "0 bytes written (check FS size/layout)";
              if (g_uploadPath.length())
                LittleFS.remove(g_uploadPath);
            } else {
              g_lastUploadStatus = "uploaded " + String(g_uploadBytesThisFile) + " bytes";
            }
          }
          g_uploadPath = "";
          yield();
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
          if (g_uploadFile)
            g_uploadFile.close();
          g_uploadPath = "";
          Serial.println("UPLOAD: aborted");
          g_lastUploadStatus = "aborted";
        }

      });

  server.on("/lcd-reinit", HTTP_GET, []() {
    if (!LCD_ENABLE) {
      server.send(503, "text/plain", "LCD disabled in firmware");
      return;
    }
    lcdShutdown();
    lcdEnsureInit();
    server.send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<meta http-equiv='refresh' content='0;url=/'>"
                "<title>OK</title></head><body>OK. <a href='/'>Back</a></body></html>");
  });

  server.on("/lcd", HTTP_GET, []() {
    if (!LCD_ENABLE) {
      server.send(503, "text/plain", "LCD disabled in firmware");
      return;
    }
    lcdEnsureInit();
    if (!g_lcdReady || g_lcd == nullptr) {
      server.send(500, "text/plain", "LCD init failed");
      return;
    }

    g_lcd->fillScreen(LCD_BLACK);

    String text = server.hasArg("text") ? server.arg("text") : "";
    if (text.length() > 64)
      text = text.substring(0, 64);

    int x = server.hasArg("x") ? server.arg("x").toInt() : 10;
    int y = server.hasArg("y") ? server.arg("y").toInt() : 10;
    int size = server.hasArg("size") ? server.arg("size").toInt() : 2;
    
    if (x < 0)
      x = 0;
    if (y < 0)
      y = 0;
    if (x > 239)
      x = 239;
    if (y > 239)
      y = 239;
    if (size < 1)
      size = 1;
    if (size > 10)
      size = 10;

    lcdDrawTextWrapped(x, y, text.length() ? text : String("(empty)"), size, LCD_WHITE, LCD_BLACK, true);

    server.send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<meta http-equiv='refresh' content='0;url=/'>"
                "<title>OK</title></head><body>OK. <a href='/'>Back</a></body></html>");
  });

  server.on("/gif-loop-start", HTTP_GET, []() {
    if (!LCD_ENABLE) {
      server.send(503, "text/plain", "LCD disabled in firmware");
      return;
    }
#if !GIF_ENABLE
    server.send(500, "text/plain",
                "GIF support not compiled. Install the AnimatedGIF library and rebuild.");
    return;
#else
    g_gifLoopEnabled = true;
    g_gifStopRequested = false;
    if (!g_gifPlaying)
      g_gifPlayRequested = true;
    server.send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<meta http-equiv='refresh' content='0;url=/gifs'>"
                "<title>OK</title></head><body>GIF loop started. <a href='/gifs'>Back</a></body></html>");
    return;
#endif
  });

  server.on("/gif-loop-stop", HTTP_GET, []() {
    g_gifLoopEnabled = false;
    g_gifStopRequested = true;
    server.send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<meta http-equiv='refresh' content='0;url=/gifs'>"
                "<title>OK</title></head><body>Stopping GIF loop... <a href='/gifs'>Back</a></body></html>");
  });

  server.on("/lcd-clear", HTTP_GET, []() {
    if (!LCD_ENABLE) {
      server.send(503, "text/plain", "LCD disabled in firmware");
      return;
    }
    lcdEnsureInit();
    if (!g_lcdReady || g_lcd == nullptr) {
      server.send(500, "text/plain", "LCD init failed");
      return;
    }
    g_lcd->fillScreen(LCD_BLACK);
    server.send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<meta http-equiv='refresh' content='0;url=/'>"
                "<title>OK</title></head><body>OK. <a href='/'>Back</a></body></html>");
  });

  server.on("/flash-read", HTTP_GET, []() {
    if (!server.hasArg("addr")) {
      server.send(400, "text/plain",
                  "Missing 'addr' parameter. Usage: "
                  "/flash-read?addr=0x282000&size=1024");
      return;
    }

    uint32_t addr = strtoul(server.arg("addr").c_str(), NULL, 0);
    uint32_t size = server.hasArg("size") ? server.arg("size").toInt() : 1024;

    if (size > 4096)
      size = 4096;

    String h = "<!DOCTYPE html><html><body><h1>Flash Read</h1><pre>";
    h += "Address: 0x" + String(addr, HEX) + "\n";
    h += "Size: " + String(size) + " bytes\n\n";

    uint8_t buffer[256];
    for (uint32_t offset = 0; offset < size; offset += 256) {
      uint32_t readSize = (size - offset) < 256 ? (size - offset) : 256;
      ESP.flashRead(addr + offset, (uint32_t *)buffer, readSize);

      for (uint32_t i = 0; i < readSize; i += 16) {
        h += String(offset + i, HEX) + ": ";

        for (int j = 0; j < 16 && (i + j) < readSize; j++) {
          uint8_t b = buffer[i + j];
          if (b < 0x10)
            h += "0";
          h += String(b, HEX) + " ";
        }

        h += " | ";

        for (int j = 0; j < 16 && (i + j) < readSize; j++) {
          uint8_t b = buffer[i + j];
          h += (b >= 0x20 && b <= 0x7E) ? (char)b : '.';
        }
        h += "\n";
      }

      yield();
    }

    h += "</pre><a href='/flash-scan'>Back to Scan</a></body></html>";
    server.send(200, "text/html", h);
  });

  server.on("/eeprom", HTTP_GET, []() {
    String h = "<!DOCTYPE html><html><body><h1>EEPROM & Persistent "
               "Storage</h1><pre>";

    EEPROM.begin(4096);

    h += "=== EEPROM Content (First 512 bytes) ===\n";
    h += "This is where app configs often persist across flashes.\n\n";

    for (int i = 0; i < 512; i += 16) {
      h += String(i, HEX).c_str();
      h += ": ";

      for (int j = 0; j < 16 && (i + j) < 512; j++) {
        uint8_t b = EEPROM.read(i + j);
        if (b < 0x10)
          h += "0";
        h += String(b, HEX) + " ";
      }

      h += " | ";

      for (int j = 0; j < 16 && (i + j) < 512; j++) {
        uint8_t b = EEPROM.read(i + j);
        if (b >= 0x20 && b <= 0x7E) {
          h += (char)b;
        } else {
          h += ".";
        }
      }
      h += "\n";
    }

    EEPROM.end();

    h += "\n=== Flash Memory Layout ===\n";
    h += "Flash Size: " + String(ESP.getFlashChipRealSize()) + " bytes\n";
    h += "Sketch Size: " + String(ESP.getSketchSize()) + " bytes\n";
    h += "Free Sketch: " + String(ESP.getFreeSketchSpace()) + " bytes\n";
    h += "Sketch MD5: " + ESP.getSketchMD5() + "\n\n";

    h += "Typical ESP8266 4MB Layout:\n";
    h += "0x000000 - Bootloader\n";
    h += "0x001000 - User App (Sketch)\n";
    h += "0x100000 - OTA Partition (if used)\n";
    h += "0x200000 - File System (SPIFFS/LittleFS)\n";
    h += "0x3FB000 - WiFi Config (RF_CAL)\n";
    h += "0x3FC000 - PHY Data\n";
    h += "0x3FD000 - System Params\n";
    h += "0x3FE000 - EEPROM Emulation Area\n\n";

    h += "EEPROM and System Params persist across normal flashes!\n";

    h += "\n=== PHY Data (0x3FC000) ===\n";
    h += dumpFlashSection(0x3FC000, 512);
    h += "\n=== System Params (0x3FD000) ===\n";
    h += dumpFlashSection(0x3FD000, 512);

    h += "</pre><a href='/'>Back</a></body></html>";
    server.send(200, "text/html", h);
  });

  server.on("/nvs", HTTP_GET, []() {
    String h = "<!DOCTYPE html><html><body><h1>NVS Storage Contents</h1><pre>";

    h += "=== ESP8266 System Settings ===\n\n";

    h += "Boot Version: " + String(ESP.getBootVersion()) + "\n";
    h += "Boot Mode: " + String(ESP.getBootMode()) + "\n";
    h += "CPU Frequency: " + String(ESP.getCpuFreqMHz()) + " MHz\n";
    h += "SDK Version: " + String(ESP.getSdkVersion()) + "\n";
    h += "Core Version: " + String(ESP.getCoreVersion()) + "\n";
    h += "Reset Reason: " + String(ESP.getResetReason()) + "\n";
    h += "Reset Info: " + String(ESP.getResetInfo()) + "\n\n";

    h += "Flash Chip ID: 0x" + String(ESP.getFlashChipId(), HEX) + "\n";
    h += "Flash Chip Vendor: 0x" + String(ESP.getFlashChipVendorId(), HEX) +
         "\n\n";

    h += "=== RTC Memory (Summary: first 64 bytes) ===\n";
    const int RTC_SUMMARY_WORDS = 16;
    const int RTC_FULL_WORDS = 128;
    uint32_t rtcData[RTC_FULL_WORDS];
    if (ESP.rtcUserMemoryRead(0, rtcData, sizeof(rtcData))) {
      for (int i = 0; i < RTC_SUMMARY_WORDS; i++) {
        h += "[" + String(i) + "]: 0x" + String(rtcData[i], HEX) + " (" +
             String(rtcData[i]) + ")\n";
      }
      h += "\n=== RTC Memory Hex Dump (512 bytes) ===\n";
      h += hexdumpBuffer((uint8_t *)rtcData, sizeof(rtcData), 0);
    } else {
      h += "Failed to read RTC memory\n";
    }

    uint32_t nvsSize = 4096;
    if (server.hasArg("size")) {
      int requested = server.arg("size").toInt();
      if (requested > 0)
        nvsSize = min((uint32_t)requested, (uint32_t)4096);
    }
    if (nvsSize % 16)
      nvsSize = ((nvsSize / 16) + 1) * 16;

    h += "\n=== System Params (0x3FD000, " + String(nvsSize) + " bytes) ===\n";
    h += "(Use /nvs?size=256 for a shorter window or omit to dump the full "
         "4KB)\n";
    h += dumpFlashSection(0x3FD000, nvsSize);

    h += "</pre><a href='/'>Back</a></body></html>";
    server.send(200, "text/html", h);
  });

  server.on("/reboot", HTTP_GET, []() {
    server.send(200, "text/plain", "Rebooting...");
    delay(500);
    ESP.restart();
  });

  server.begin();
}

void loop() {
  server.handleClient();

#if GIF_ENABLE
  if (g_gifPlayRequested && !g_gifPlaying) {
    g_gifPlayRequested = false;
    gifPlayAllFromLittleFS();
  }
#endif
}

static void ensureGifsDirMounted() {
  LittleFS.mkdir(GIF_DIR);
}

static String htmlEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (uint32_t i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c == '&')
      out += "&amp;";
    else if (c == '<')
      out += "&lt;";
    else if (c == '>')
      out += "&gt;";
    else if (c == '\"')
      out += "&quot;";
    else
      out += c;
  }
  return out;
}

static void gifPlayAllFromLittleFS() {
#if !GIF_ENABLE
  return;
#else
  if (g_gifPlaying)
    return;
  g_gifPlaying = true;
  g_gifStopRequested = false;

  Serial.println("GIF: Starting playback");
  yield();

  if (g_gif == nullptr) {
    Serial.println("GIF: Allocating decoder...");
    Serial.printf("Free heap before alloc: %u bytes\n", ESP.getFreeHeap());
    g_gif = new AnimatedGIF();
    yield();
    if (g_gif == nullptr) {
      Serial.println("GIF: failed to allocate decoder");
      lcdDrawTextWrapped(10, 200, "GIF alloc failed\nLow memory", 1, LCD_WHITE, LCD_BLACK, true);
      g_gifPlaying = false;
      return;
    }
    Serial.printf("Free heap after alloc: %u bytes\n", ESP.getFreeHeap());
    Serial.println("GIF: Decoder allocated successfully");
  }

  lcdEnsureInit();
  yield();
  if (!g_lcdReady || g_lcd == nullptr) {
    Serial.println("GIF: LCD not ready");
    g_gifPlaying = false;
    if (g_gif) {
      delete g_gif;
      g_gif = nullptr;
    }
    return;
  }

  if (!LittleFS.begin()) {
    Serial.println("GIF: LittleFS mount failed");
    lcdDrawTextWrapped(10, 200, "LittleFS mount failed", 1, LCD_WHITE, LCD_BLACK, true);
    g_gifPlaying = false;
    if (g_gif) {
      delete g_gif;
      g_gif = nullptr;
    }
    return;
  }

  Serial.println("GIF: LittleFS mounted");
  yield();

  g_gif->begin(LITTLE_ENDIAN_PIXELS);

  int played = 0;
  int loopCount = 0;
  do {
    loopCount++;
    Serial.printf("GIF: Loop iteration %d\n", loopCount);
    yield();
    
    Dir dir = LittleFS.openDir(GIF_DIR);
    String gifFiles[20];
    int filesFound = 0;
    
    while (dir.next() && filesFound < 20) {
      server.handleClient();
      yield();
      if (g_gifStopRequested) {
        Serial.println("GIF: Stop requested");
        break;
      }

      String name = dir.fileName();
      if (!name.endsWith(".gif") && !name.endsWith(".GIF"))
        continue;

      String fullPath;
      if (name.startsWith(String(GIF_DIR) + "/")) {
        fullPath = name;
      } else if (name.startsWith("/")) {
        fullPath = name;
      } else {
        fullPath = String(GIF_DIR) + "/" + name;
      }
      if (!fullPath.startsWith("/"))
        fullPath = "/" + fullPath;

      gifFiles[filesFound] = fullPath;
      filesFound++;
    }

    for (int i = 0; i < filesFound; i++) {
      server.handleClient();
      yield();
      
      if (g_gifStopRequested) {
        Serial.println("GIF: Stop requested");
        break;
      }
      
      String fullPath = gifFiles[i];
      Serial.printf("GIF: playing %s\n", fullPath.c_str());
      
      g_lcd->fillScreen(LCD_BLACK);
      yield();
      
      if (!g_gifLoopEnabled) {
        lcdDrawTextWrapped(10, 10, "Playing GIF:\n" + fullPath, 1, LCD_WHITE, LCD_BLACK, true);
      }

      bool success = gifPlayOne(fullPath);

      ESP.wdtEnable(0);
      yield();
      server.handleClient();
      yield();
      
      if (success) {
        played++;
      } else {
        Serial.printf("GIF: Failed to play %s\n", fullPath.c_str());
      }

      delay(100);
      yield();

        LittleFS.end();
        delay(10);
        LittleFS.begin();
        yield();
      
      if (g_gifStopRequested) {
        Serial.println("GIF: Stop requested after playback");
        break;
      }
    }
    
    if (filesFound == 0) {
      Serial.println("GIF: No GIF files found in directory");
      g_lcd->fillScreen(LCD_BLACK);
      lcdDrawTextWrapped(10, 10, "No GIF files found\nin " + String(GIF_DIR), 2, LCD_WHITE, LCD_BLACK, true);
      delay(2000);
      break;
    }
    
    if (g_gifStopRequested)
      break;
      
    yield();
  } while (g_gifLoopEnabled);

  LittleFS.end();

  g_lcd->fillScreen(LCD_BLACK);
  if (g_gifLoopEnabled) {
    lcdDrawTextWrapped(10, 10, "GIF loop stopped\nFiles played: " + String(played), 2, LCD_WHITE, LCD_BLACK, true);
  } else {
    lcdDrawTextWrapped(10, 10, "GIF playlist done\nFiles: " + String(played), 2, LCD_WHITE, LCD_BLACK, true);
  }
  
  Serial.printf("GIF: Playback complete, played %d files\n", played);
  
  g_gifPlaying = false;
  g_gifStopRequested = false;
  g_gifLoopEnabled = false;

  delete g_gif;
  g_gif = nullptr;
#endif
}
