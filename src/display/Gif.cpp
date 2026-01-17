#include "display/Gif.h"
#include "display/DisplayManager.h"
#include <Arduino_GFX_Library.h>
#include <array>
static constexpr uint32_t GIF_MAX_MS_PER_FILE = 20000U;
static constexpr uint8_t GIF_TARGET_FPS = 30U;
static constexpr uint32_t GIF_FRAME_MS = 1000U / GIF_TARGET_FPS;

Gif* Gif::s_instance = nullptr;

/**
 * @brief Construct a new Gif:: Gif object
 */
Gif::Gif() : m_gif(nullptr), m_playRequested(false), m_playing(false), m_loopEnabled(false), m_stopRequested(false) {
    s_instance = this;
}

/**
 * @brief Destroy the Gif:: Gif object
 */
Gif::~Gif() {
    stop();

    if (m_gif != nullptr) {
        delete m_gif;
        m_gif = nullptr;
    }
}

/**
 * @brief Initialize the Gif object
 *
 * @return true if initialization was successful false otherwise
 */
auto Gif::begin() -> bool {
    if (m_gif == nullptr) {
        m_gif = new AnimatedGIF();

        if (m_gif == nullptr) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Open a GIF file from LittleFS
 *
 * @param fname The filename to open
 * @param pSize Pointer to store the size of the file
 *
 * @return void* Handle to the opened file
 */
auto Gif::gifOpenFile(const char* fname, int32_t* pSize) -> void* {
    String path(fname);

    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    auto* filePtr = new File();

    if (filePtr == nullptr) {
        return nullptr;
    }

    *filePtr = LittleFS.open(path, "r");

    if (!(*filePtr)) {
        delete filePtr;

        return nullptr;
    }

    *pSize = static_cast<int32_t>(filePtr->size());

    return reinterpret_cast<void*>(filePtr);
}

/**
 * @brief Close the GIF file
 *
 * @param pHandle Handle to the file to close
 */
auto Gif::gifCloseFile(void* pHandle) -> void {
    auto* filePtr = reinterpret_cast<File*>(pHandle);

    if (filePtr == nullptr) {
        return;
    }

    if (*filePtr) {
        filePtr->close();
    }

    delete filePtr;
}

/**
 * @brief Read from the GIF file
 *
 * @param pFile Pointer to the GIFFILE structure
 * @param pBuf Buffer to read data into
 * @param iLen Number of bytes to read
 * @return int32_t Number of bytes read
 */
auto Gif::gifReadFile(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) -> int32_t {
    auto* filePtr = reinterpret_cast<File*>(pFile->fHandle);

    if (filePtr == nullptr || !(*filePtr)) {
        return 0;
    }

    int32_t bytesToRead = iLen;
    const int32_t remaining = pFile->iSize - pFile->iPos;

    if (remaining < bytesToRead) {
        bytesToRead = remaining;
    }

    if (bytesToRead <= 0) {
        return 0;
    }

    const auto bytesRead = static_cast<int32_t>(filePtr->read(pBuf, static_cast<size_t>(bytesToRead)));

    if (bytesRead > 0) {
        pFile->iPos += bytesRead;
    }

    return bytesRead;
}

/**
 * @brief Seek to a position in the GIF file
 *
 * @param pFile Pointer to the GIFFILE structure
 * @param iPosition Position to seek to
 * @return int32_t New position after seeking
 */
auto Gif::gifSeekFile(GIFFILE* pFile, int32_t iPosition) -> int32_t {
    auto* filePtr = reinterpret_cast<File*>(pFile->fHandle);

    if (filePtr == nullptr || !(*filePtr)) {
        return 0;
    }

    if (iPosition < 0) {
        iPosition = 0;
    }
    if (iPosition >= pFile->iSize) {
        iPosition = pFile->iSize - 1;
    }

    pFile->iPos = iPosition;
    (void)filePtr->seek(static_cast<uint32_t>(iPosition), SeekSet);

    return iPosition;
}

/**
 * @brief Draw a frame of the GIF
 *
 * @note need to be refactored
 *
 * @param pDraw Pointer to the GIFDRAW structure
 */
auto Gif::gifDraw(GIFDRAW* pDraw) -> void  // NOLINT(readability-function-cognitive-complexity)
{
    if (!DisplayManager::isReady()) {
        return;
    }

    auto* gfx = DisplayManager::getGfx();
    if (gfx == nullptr) {
        return;
    }

    auto* tft = reinterpret_cast<Arduino_TFT*>(gfx);
    if (pDraw->y == 0) {
        tft->startWrite();

        if (s_instance != nullptr) {
            s_instance->m_inFrameWrite = true;
        }
    }

    const auto* palette565 = reinterpret_cast<const uint16_t*>(pDraw->pPalette);
    const auto* src = pDraw->pPixels;

    const auto rawY = static_cast<int>(pDraw->iY + pDraw->y);
    const auto rawX = static_cast<int>(pDraw->iX);
    const auto width = static_cast<int>(pDraw->iWidth);

    if (pDraw->y == 0 && s_instance != nullptr) {
        if (!s_instance->m_centered) {
            const auto screenW = static_cast<int>(gfx->width());
            const auto screenH = static_cast<int>(gfx->height());
            const auto gifW = static_cast<int>(pDraw->iWidth);
            const auto gifH = static_cast<int>(pDraw->iHeight);

            const auto centerX = static_cast<int>((screenW - gifW) / 2);
            const auto centerY = static_cast<int>((screenH - gifH) / 2);

            s_instance->m_offsetX = static_cast<int16_t>(centerX - static_cast<int>(pDraw->iX));
            s_instance->m_offsetY = static_cast<int16_t>(centerY - static_cast<int>(pDraw->iY));
            s_instance->m_centered = true;
        }

        s_instance->m_curDisposal = pDraw->ucDisposalMethod;
        s_instance->m_curHadTransparency = (pDraw->ucHasTransparency != 0);
        s_instance->m_curX = static_cast<int16_t>(pDraw->iX + s_instance->m_offsetX);
        s_instance->m_curY = static_cast<int16_t>(pDraw->iY + s_instance->m_offsetY);
        s_instance->m_curW = static_cast<int16_t>(pDraw->iWidth);
        s_instance->m_curH = static_cast<int16_t>(pDraw->iHeight);
        s_instance->m_curBg = LCD_BLACK;
    }

    const auto xPos = static_cast<int>(rawX + (s_instance != nullptr ? s_instance->m_offsetX : 0));
    const auto yPos = static_cast<int>(rawY + (s_instance != nullptr ? s_instance->m_offsetY : 0));

    if (yPos < 0 || yPos >= static_cast<int>(gfx->height())) {
        return;
    }

    std::array<uint16_t, LINEBUF_MAX> localLineBuf{};
    std::array<uint16_t, LINEBUF_MAX>* pLineBuf = (s_instance != nullptr) ? &s_instance->m_lineBuf : &localLineBuf;
    auto& lineBuf = *pLineBuf;
    const auto maxW = static_cast<int>(lineBuf.size());
    const auto drawW = (width > maxW) ? maxW : width;

    int visStart = 0;
    int visEnd = drawW;

    if (xPos < 0) {
        visStart = -xPos;
    }

    if (xPos + visEnd > static_cast<int>(gfx->width())) {
        visEnd = static_cast<int>(gfx->width() - xPos);
    }

    if (visEnd <= visStart) {
        return;
    }

    const auto curStart = static_cast<int>(xPos + visStart);
    const auto curEnd = static_cast<int>(xPos + visEnd);

    const auto screenW = static_cast<int>(gfx->width());
    bool skipDraw = false;

    if (width <= 0) {
        skipDraw = true;
    }
    if (xPos >= screenW || (xPos + drawW) <= 0) {
        skipDraw = true;
    }

    const bool endOfFrame = (pDraw->y == static_cast<int>(pDraw->iHeight - 1));

    bool needClearLine = false;
    int clearStart = 0;
    int clearEnd = 0;

    if (s_instance != nullptr && s_instance->m_havePrev &&
        (s_instance->m_prevDisposal == 2 || s_instance->m_prevHadTransparency)) {
        const auto prevTop = s_instance->m_prevY;
        const auto prevBot =
            static_cast<int>(static_cast<int32_t>(s_instance->m_prevY) + static_cast<int32_t>(s_instance->m_prevH) - 1);

        if (yPos >= prevTop && yPos <= prevBot) {
            needClearLine = true;
            clearStart = s_instance->m_prevX;
            clearEnd =
                static_cast<int>(static_cast<int32_t>(s_instance->m_prevX) + static_cast<int32_t>(s_instance->m_prevW));
        }
    }

    const bool curValid = (!skipDraw);

    if (needClearLine || curValid) {
        const auto screenW2 = static_cast<int>(gfx->width());
        int uStart = curValid ? curStart : clearStart;
        int uEnd = curValid ? curEnd : clearEnd;

        if (needClearLine) {
            if (clearStart < uStart) {
                uStart = clearStart;
            }
            if (clearEnd > uEnd) {
                uEnd = clearEnd;
            }
        }

        if (uStart < 0) {
            uStart = 0;
        }
        if (uEnd > screenW2) {
            uEnd = screenW2;
        }
        const auto uLen = static_cast<int>(uEnd - uStart);
        if (uLen > 0) {
            const auto fillBg = (s_instance != nullptr) ? s_instance->m_prevBg : static_cast<uint16_t>(0);

            if (!curValid) {
                for (int i = 0; i < uLen; i++) {
                    lineBuf[static_cast<size_t>(i)] = fillBg;
                }

                tft->writeAddrWindow(static_cast<int16_t>(uStart), static_cast<int16_t>(yPos),
                                     static_cast<uint16_t>(uLen), 1);
                tft->writePixels(reinterpret_cast<uint16_t*>(lineBuf.data()), static_cast<uint32_t>(uLen));

            } else {
                if (pDraw->ucHasTransparency == 0) {
                    for (int i = 0; i < uLen; i++) {
                        lineBuf[static_cast<size_t>(i)] = fillBg;
                    }

                    const auto* const sPtr = src;
                    const auto baseX = xPos - uStart;

                    for (int i = 0; i < drawW; ++i) {
                        const auto dstIndex = baseX + i;

                        if (dstIndex >= 0 && dstIndex < uLen) {
                            lineBuf[static_cast<size_t>(dstIndex)] = palette565[static_cast<uint8_t>(sPtr[i])];
                        }
                    }

                    tft->writeAddrWindow(static_cast<int16_t>(uStart), static_cast<int16_t>(yPos),
                                         static_cast<uint16_t>(uLen), 1);
                    tft->writePixels(reinterpret_cast<uint16_t*>(lineBuf.data()), static_cast<uint32_t>(uLen));
                } else {
                    if (needClearLine) {
                        for (int i = 0; i < uLen; i++) {
                            lineBuf[static_cast<size_t>(i)] = fillBg;
                        }

                        const auto transparentIndex = static_cast<uint8_t>(pDraw->ucTransparent);
                        const auto* const sPtr = src;
                        const auto baseX = xPos - uStart;

                        for (int i = 0; i < drawW; ++i) {
                            const auto idx = static_cast<uint8_t>(sPtr[i]);

                            if (idx != transparentIndex) {
                                const auto dstIndex = baseX + i;

                                if (dstIndex >= 0 && dstIndex < uLen) {
                                    lineBuf[static_cast<size_t>(dstIndex)] = palette565[idx];
                                }
                            }
                        }

                        tft->writeAddrWindow(static_cast<int16_t>(uStart), static_cast<int16_t>(yPos),
                                             static_cast<uint16_t>(uLen), 1);
                        tft->writePixels(reinterpret_cast<uint16_t*>(lineBuf.data()), static_cast<uint32_t>(uLen));
                    } else {
                        const auto transparentIndex = static_cast<uint8_t>(pDraw->ucTransparent);
                        const auto* const sPtr = src + visStart;
                        int idx = 0;

                        while (idx < (visEnd - visStart)) {
                            while (idx < (visEnd - visStart) && sPtr[idx] == transparentIndex) {
                                idx++;
                            }

                            if (idx >= (visEnd - visStart)) {
                                break;
                            }

                            int runLen = 0;

                            while (idx < (visEnd - visStart) && sPtr[idx] != transparentIndex) {
                                lineBuf[static_cast<size_t>(runLen)] = palette565[static_cast<uint8_t>(sPtr[idx])];
                                ++runLen;
                                ++idx;
                            }

                            const auto dstX = static_cast<int>(xPos + visStart + (idx - runLen));

                            tft->writeAddrWindow(static_cast<int16_t>(dstX), static_cast<int16_t>(yPos),
                                                 static_cast<uint16_t>(runLen), 1);
                            tft->writePixels(reinterpret_cast<uint16_t*>(lineBuf.data()),
                                             static_cast<uint32_t>(runLen));
                        }
                    }
                }
            }
        }
    }

    if (endOfFrame) {
        if (s_instance != nullptr && s_instance->m_inFrameWrite) {
            tft->endWrite();
            s_instance->m_inFrameWrite = false;
        }

        if (s_instance != nullptr) {
            s_instance->m_havePrev = true;
            s_instance->m_prevDisposal = s_instance->m_curDisposal;
            s_instance->m_prevHadTransparency = s_instance->m_curHadTransparency;
            s_instance->m_prevX = s_instance->m_curX;
            s_instance->m_prevY = s_instance->m_curY;
            s_instance->m_prevW = s_instance->m_curW;
            s_instance->m_prevH = s_instance->m_curH;
            s_instance->m_prevBg = s_instance->m_curBg;
        }
    }
}

/**
 * @brief Play a single GIF file
 *
 * @param path The path to the GIF file
 *
 * @return true if playback started successfully false otherwise
 */
auto Gif::playOne(const String& path) -> bool {
    if (!DisplayManager::isReady()) {
        return false;
    }

    if (m_gif == nullptr) {
        if (!begin()) {
            return false;
        }
    }

    m_offsetX = 0;
    m_offsetY = 0;
    m_centered = false;

    m_gif->begin(GIF_PALETTE_RGB565_LE);

    if (m_gif->open(path.c_str(), gifOpenFile, gifCloseFile, gifReadFile, gifSeekFile, gifDraw) <= 0) {
        return false;
    }

    m_currentPath = path;

    m_stopRequested = false;
    m_playRequested = true;
    m_playing = true;
    m_delayMsFromGif = 0;
    m_targetMs = 0;
    m_lastFrameMs = millis();
    m_startMs = millis();
    m_frameCount = 0;

    return true;
}

/**
 * @brief Update the GIF playback, should be called regularly
 *
 * @return void
 */
auto Gif::update() -> void {
    if (!m_playing || m_gif == nullptr) {
        return;
    }

    if (m_stopRequested) {
        m_gif->close();
        m_playing = false;
        m_playRequested = false;
        m_stopRequested = false;
        return;
    }

    const uint32_t now = millis();
    if (m_targetMs > 0) {
        if ((now - m_lastFrameMs) < m_targetMs) {
            return;
        }
    }

    int delayMsFromGif = 0;
    const int result = m_gif->playFrame(false, &delayMsFromGif, nullptr);
    m_frameCount++;
    m_lastFrameMs = now;

    if (result <= 0) {
        if (m_loopEnabled && !m_stopRequested && !m_currentPath.isEmpty()) {
            m_gif->close();
            if (m_gif->open(m_currentPath.c_str(), gifOpenFile, gifCloseFile, gifReadFile, gifSeekFile, gifDraw) <= 0) {
                m_playing = false;
                m_playRequested = false;

                return;
            }

            m_delayMsFromGif = 0;
            m_targetMs = 0;
            m_lastFrameMs = millis();
            m_startMs = millis();
            m_frameCount = 0;

            return;
        }

        m_gif->close();
        m_playing = false;
        m_playRequested = false;

        return;
    }

    uint32_t targetMs = GIF_FRAME_MS;
    if (delayMsFromGif > 0 && static_cast<uint32_t>(delayMsFromGif) > targetMs) {
        targetMs = static_cast<uint32_t>(delayMsFromGif);
    }

    m_targetMs = targetMs;

    if ((millis() - m_startMs) > GIF_MAX_MS_PER_FILE) {
        m_gif->close();
        m_playing = false;
        m_playRequested = false;

        return;
    }
}

/**
 * @brief Play all GIF files from the /gifs directory in LittleFS
 *
 * @return true if playback was successful false otherwise
 */
auto Gif::playAllFromLittleFS() -> bool {
    if (m_playing) {
        return false;
    }

    m_playing = true;
    m_stopRequested = false;

    if (m_gif == nullptr) {
        begin();
    }

    if (!DisplayManager::isReady()) {
        return false;
    }

    if (!LittleFS.begin()) {
        return false;
    }

    m_gif->begin(LITTLE_ENDIAN_PIXELS);

    Dir dir = LittleFS.openDir("/gifs");

    while (dir.next()) {
        String name = dir.fileName();
        String lname = name;
        lname.toLowerCase();

        if (!lname.endsWith(".gif")) {
            continue;
        }

        if (m_stopRequested) {
            break;
        }

        playOne(String("/gifs/") + name);

        while (m_playing && !m_stopRequested) {
            update();
            yield();
        }

        if (m_stopRequested) {
            break;
        }

        if (!m_loopEnabled) {
            break;
        }
    }

    LittleFS.end();
    m_playing = false;
    m_stopRequested = false;
    m_loopEnabled = false;

    return true;
}

/**
 * @brief Stop GIF playback
 */
auto Gif::stop() -> void { m_stopRequested = true; }

/**
 * @brief Check if a GIF is currently playing
 *
 * @return true if a GIF is playing false otherwise
 */
auto Gif::isPlaying() const -> bool { return m_playing; }

/**
 * @brief Enable or disable looping of GIF playback
 *
 * @param enabled true to enable looping false to disable
 */
auto Gif::setLoopEnabled(bool enabled) -> void { m_loopEnabled = enabled; }
