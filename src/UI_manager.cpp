// =============================================================================
//  UI_manager.cpp — menu-based UI for mp3Play
//
//  Layout (320 × 170):
//    ┌──────────────────────┬───────────┐
//    │  LEFT PANEL (220 px) │ RIGHT     │
//    │  Albums / Songs /    │ Now       │
//    │  Settings            │ Playing + │
//    │                      │ Controls  │
//    └──────────────────────┴───────────┘
//
//  Focus::LEFT
//    Up/Down  — move cursor in list
//    OK       — select / open
//    Right    — jump to right panel
//    Hold OK (600 ms) — toggle Settings
//
//  Focus::RIGHT
//    Left/Right — cycle between |<  []  >| buttons
//    OK         — activate selected button
//    Left from |< — return to left panel
// =============================================================================

#include "UI_manager.hpp"
#include <esp_system.h>

// Left panel list layout
static const int kHeaderH = 16;
static const int kListRows = 8;
static const int kRowH = 18;
static const int kListY = kHeaderH;

// Right panel geometry (100 px wide)
static const int kRightX = DIVIDER_X + 3;
static const int kRightW = RIGHT_W - 5;

// Title scroll: 1 px every 60 ms, pause 1.5 s at start/end
static const uint32_t kScrollTickMs = 60;
static const uint32_t kScrollPauseMs = 1500;

UIManager uiManager;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
UIManager::UIManager() : tft(TFT_eSPI()), sprite(TFT_eSprite(&tft)),
                         titleSprite(TFT_eSprite(&tft)),
                         windowSprite(TFT_eSprite(&tft))
{
    for (auto &row : logBuf)
        row[0] = '\0';
    titleCache[0] = '\0';

    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);

    tft.init();
    tft.setRotation(3);
    sprite.createSprite(SCREEN_W, SCREEN_H);
    sprite.setSwapBytes(true);

    // Backlight
    ledcSetup(0, 10000, 8);
    ledcAttachPin(PIN_LCD_BL, 0);
    ledcWrite(0, 160);

    sprite.fillSprite(TFT_BLACK);
    sprite.pushSprite(0, 0);

    pinMode(ADC_PIN, INPUT);

    xTaskCreatePinnedToCore(live_loop, "uiTask", 16384, this, 1, NULL, 0);
}

// ---------------------------------------------------------------------------
// Debug log
// ---------------------------------------------------------------------------
void UIManager::bufferPrint(const char *message)
{
    snprintf(logBuf[logIdx], sizeof(logBuf[logIdx]), "%s", message);
    logIdx = (logIdx + 1) % 8;
    Serial.println(message);
}

// ---------------------------------------------------------------------------
// Button reading
// Returns BTN_UP / BTN_DOWN / BTN_LEFT / BTN_RIGHT / BTN_OK on rising edge,
// -1 otherwise.
// ---------------------------------------------------------------------------
int UIManager::readBtn()
{
    int raw = configManager.readRawBtn(); // -1 or 0-4

    int edge = -1;
    if (raw != prevRaw)
    {
        if (raw >= 0 && prevRaw < 0) // rising edge
            edge = raw;
        prevRaw = raw;
    }
    return edge;
}

// ---------------------------------------------------------------------------
// Library helpers
// ---------------------------------------------------------------------------
void UIManager::loadLibrary()
{
    if (libLoaded)
        return;
    library = musicManager.scanLibrary();
    libLoaded = true;
    albumSel = 0;
    albumScroll = 0;
}

void UIManager::openAlbum(int idx)
{
    openAlbumIdx = idx;
    songSel = 0;
    songScroll = 0;
    leftMode = LeftMode::SONGS;
}

void UIManager::playSong(int sIdx)
{
    if (openAlbumIdx < 0 || openAlbumIdx >= (int)library.size())
        return;
    const auto &songs = library[openAlbumIdx].songs;
    if (sIdx < 0 || sIdx >= (int)songs.size())
        return;
    playAlbum = openAlbumIdx;
    playSongIdx = sIdx;
    musicManager.play(songs[sIdx].path.c_str());
}

void UIManager::playNext(int delta)
{
    if (playAlbum < 0 || playAlbum >= (int)library.size())
        return;
    const auto &songs = library[playAlbum].songs;
    if (songs.empty())
        return;
    playSongIdx = (playSongIdx + delta + (int)songs.size()) % (int)songs.size();
    musicManager.play(songs[playSongIdx].path.c_str());
}

// ---------------------------------------------------------------------------
// Scroll helpers (clamped scroll so selection stays visible)
// ---------------------------------------------------------------------------
static void scrollToShow(int sel, int &scroll, int rows)
{
    if (sel < scroll)
        scroll = sel;
    if (sel >= scroll + rows)
        scroll = sel - rows + 1;
}

// ---------------------------------------------------------------------------
// Truncate 'text' in-place so it fits within 'maxPx' pixels, appending "..."
// ---------------------------------------------------------------------------
static void truncateWithEllipsis(TFT_eSprite &spr, char *buf, size_t bufLen, int maxPx)
{
    if (spr.textWidth(buf) <= maxPx)
        return;
    // Reserve room for the ellipsis
    int ellipsisW = spr.textWidth("...");
    size_t len = strlen(buf);
    while (len > 0 && spr.textWidth(buf) + ellipsisW > maxPx)
        buf[--len] = '\0';
    strlcat(buf, "...", bufLen);
}

// ---------------------------------------------------------------------------
// Input — LEFT panel
// ---------------------------------------------------------------------------
void UIManager::handleLeft(int btn, uint32_t holdMs)
{
    // Hold OK → toggle Settings
    if (holdMs >= HOLD_SETTINGS_MS && !centerHeld)
    {
        centerHeld = true;
        if (leftMode == LeftMode::SETTINGS)
        {
            settingEditing = false;
            leftMode = (openAlbumIdx >= 0) ? LeftMode::SONGS : LeftMode::ALBUMS;
        }
        else
        {
            settingEditing = false;
            settingSel = SettingItem::VOLUME;
            leftMode = LeftMode::SETTINGS;
        }
        return;
    }
    if (holdMs > 0)
        return; // still holding — suppress taps

    // Right → jump to right panel
    if (btn == BTN_RIGHT)
    {
        focus = Focus::RIGHT;
        return;
    }

    if (leftMode == LeftMode::ALBUMS)
    {
        int n = (int)library.size();
        if (n == 0)
            return;
        if (btn == BTN_UP)
        {
            albumSel = (albumSel - 1 + n) % n;
            scrollToShow(albumSel, albumScroll, kListRows);
        }
        if (btn == BTN_DOWN)
        {
            albumSel = (albumSel + 1) % n;
            scrollToShow(albumSel, albumScroll, kListRows);
        }
        if (btn == BTN_OK)
        {
            openAlbum(albumSel);
        }
    }
    else if (leftMode == LeftMode::SONGS)
    {
        if (openAlbumIdx < 0 || openAlbumIdx >= (int)library.size())
            return;
        int n = (int)library[openAlbumIdx].songs.size() + 1; // +1 for Back
        if (btn == BTN_UP)
        {
            songSel = (songSel - 1 + n) % n;
            scrollToShow(songSel, songScroll, kListRows);
        }
        if (btn == BTN_DOWN)
        {
            songSel = (songSel + 1) % n;
            scrollToShow(songSel, songScroll, kListRows);
        }
        if (btn == BTN_OK)
        {
            if (songSel == 0)
                leftMode = LeftMode::ALBUMS;
            else
            {
                playSong(songSel - 1);
                focus = Focus::RIGHT;
                rightSel = 1; // land on play/pause
            }
        }
    }
    else if (leftMode == LeftMode::SETTINGS)
    {
        if (!settingEditing)
        {
            // Navigate between setting items
            if (btn == BTN_UP)
                settingSel = (settingSel == SettingItem::VOLUME) ? SettingItem::CLEAR_CONFIG : SettingItem::VOLUME;
            if (btn == BTN_DOWN)
                settingSel = (settingSel == SettingItem::VOLUME) ? SettingItem::CLEAR_CONFIG : SettingItem::VOLUME;
            if (btn == BTN_OK)
            {
                if (settingSel == SettingItem::CLEAR_CONFIG)
                {
                    // Wipe EEPROM and reboot — calibration will run on next boot
                    EEPROM.begin(CONFIG_EEPROM_SIZE);
                    for (int i = 0; i < CONFIG_EEPROM_SIZE; i++)
                        EEPROM.write(i, 0xFF);
                    EEPROM.commit();
                    EEPROM.end();
                    Serial.println("[cfg] Config cleared — rebooting");
                    vTaskDelay(pdMS_TO_TICKS(200));
                    esp_restart();
                }
                else
                {
                    settingEditing = true;
                }
            }
        }
        else
        {
            PlayStats ps = musicManager.getPlayStats();
            if (settingSel == SettingItem::VOLUME)
            {
                int v = ps.volume;
                if (btn == BTN_LEFT && v > 0)
                {
                    musicManager.setVolume(v - 1);
                    configManager.cfg.volume = v - 1;
                    configManager.save();
                }
                if (btn == BTN_RIGHT && v < 21)
                {
                    musicManager.setVolume(v + 1);
                    configManager.cfg.volume = v + 1;
                    configManager.save();
                }
                if (btn == BTN_OK)
                    settingEditing = false;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Input — RIGHT panel (playback controls)
// Left/Right cycle the selected button; OK activates it; Left from btn 0 → back
// ---------------------------------------------------------------------------
void UIManager::handleRight(int btn)
{
    if (btn == BTN_LEFT)
    {
        if (rightSel > 0)
            rightSel--;
        else
            focus = Focus::LEFT; // exit right panel
        return;
    }
    if (btn == BTN_RIGHT)
    {
        if (rightSel < 2)
            rightSel++;
        return;
    }
    if (btn == BTN_OK)
    {
        switch (rightSel)
        {
        case 0:
            playNext(-1);
            break; // prev
        case 1:
            musicManager.pause();
            break; // play/pause
        case 2:
            playNext(+1);
            break; // next
        }
    }
    // Up/Down do nothing on right panel (could be used for volume in future)
}

// ---------------------------------------------------------------------------
// Title sprite cache — rebuilds only when the title string changes
// ---------------------------------------------------------------------------
void UIManager::rebuildTitleSprite(const char *title)
{
    if (strcmp(title, titleCache) == 0)
        return; // same title, no rebuild needed

    strlcpy(titleCache, title, sizeof(titleCache));

    // Delete old sprite if it exists
    titleSprite.deleteSprite();

    // Measure text width using the main sprite (font already loaded there)
    int textW = sprite.textWidth(title);
    if (textW <= 0)
        textW = 1;

    titleSpriteW = textW;
    titleScrollOff = 0;
    titleScrollMs = 0; // pause at start

    // Create a 15px-tall sprite exactly as wide as the text
    titleSprite.createSprite(textW, 15);
    titleSprite.loadFont(NotoSansBold15);
    titleSprite.fillSprite(TFT_BLACK);
    titleSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    titleSprite.setTextDatum(TL_DATUM);
    titleSprite.drawString(title, 0, 0);
}

// ---------------------------------------------------------------------------
// Drawing — Albums
// ---------------------------------------------------------------------------
void UIManager::drawAlbums()
{
    sprite.setTextColor(COL_ORANGE, TFT_BLACK);
    sprite.drawString("Albums", 4, 1);

    int n = (int)library.size();
    for (int i = 0; i < kListRows && (albumScroll + i) < n; i++)
    {
        int idx = albumScroll + i;
        int y = kListY + i * kRowH;
        bool sel = (idx == albumSel) && (focus == Focus::LEFT);
        uint16_t bg = sel ? 0x3186 : TFT_BLACK;
        if (sel)
            sprite.fillRect(0, y, LEFT_W, kRowH, bg);
        sprite.setTextColor(sel ? TFT_WHITE : 0xBDF7, bg);
        char albumName[64];
        strlcpy(albumName, library[idx].name.c_str(), sizeof(albumName));
        truncateWithEllipsis(sprite, albumName, sizeof(albumName), LEFT_W - 8);
        sprite.drawString(albumName, 4, y + 2);
    }
}

// ---------------------------------------------------------------------------
// Drawing — Songs
// ---------------------------------------------------------------------------
void UIManager::drawSongs()
{
    if (openAlbumIdx < 0 || openAlbumIdx >= (int)library.size())
        return;
    const auto &songs = library[openAlbumIdx].songs;

    sprite.setTextColor(COL_ORANGE, TFT_BLACK);
    sprite.drawString(library[openAlbumIdx].name.c_str(), 4, 1);

    int total = (int)songs.size() + 1; // +1 for Back
    for (int i = 0; i < kListRows && (songScroll + i) < total; i++)
    {
        int idx = songScroll + i;
        int y = kListY + i * kRowH;
        bool sel = (idx == songSel) && (focus == Focus::LEFT);
        bool playing = (idx > 0) && (playAlbum == openAlbumIdx && playSongIdx == idx - 1);
        uint16_t bg = sel ? 0x3186 : TFT_BLACK;
        if (sel)
            sprite.fillRect(0, y, LEFT_W, kRowH, bg);
        uint16_t fg = sel ? TFT_WHITE : (playing ? COL_ORANGE : 0xBDF7);
        sprite.setTextColor(fg, bg);

        if (idx == 0)
            sprite.drawString("<- Back", 4, y + 2);
        else
        {
            const SongInfo &s = songs[idx - 1];
            char songName[64];
            strlcpy(songName, s.title.empty() ? s.filename.c_str() : s.title.c_str(), sizeof(songName));
            truncateWithEllipsis(sprite, songName, sizeof(songName), LEFT_W - 8);
            sprite.drawString(songName, 4, y + 2);
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing — Settings
// ---------------------------------------------------------------------------
void UIManager::drawSettings()
{
    sprite.setTextColor(COL_ORANGE, TFT_BLACK);
    sprite.drawString("Settings", 4, 1);

    PlayStats ps = musicManager.getPlayStats();
    bool panelSel = (focus == Focus::LEFT);

    // ---- Row 0: Volume ----
    {
        bool rowSel = panelSel && (settingSel == SettingItem::VOLUME);
        uint16_t bg = (settingEditing && rowSel) ? 0x2104 : (rowSel ? 0x3186 : TFT_BLACK);
        int y = kListY;
        sprite.fillRect(0, y, LEFT_W, kRowH, bg);

        char volStr[16];
        int vol = ps.volume;
        snprintf(volStr, sizeof(volStr), "Vol %2d ", vol);
        int labelW = sprite.textWidth(volStr);
        sprite.setTextColor((settingEditing && rowSel) ? COL_ORANGE : (rowSel ? TFT_WHITE : 0x8C71), bg);
        sprite.drawString(volStr, 4, y + 2);

        int bx = 4 + labelW, by = y + kRowH / 2 - 2;
        int bw = LEFT_W - bx - 8, bh = 4;
        sprite.fillRect(bx, by, bw, bh, 0x2104);
        sprite.fillRect(bx, by, bw * vol / 21, bh, (settingEditing && rowSel) ? COL_ORANGE : 0x8C71);

        if (settingEditing && rowSel)
        {
            sprite.setTextColor(0x8C71, TFT_BLACK);
            sprite.drawString("L/R adjust   OK done", 4, y + kRowH + 2);
        }
    }

    // ---- Row 1: Clear config ----
    {
        bool rowSel = panelSel && (settingSel == SettingItem::CLEAR_CONFIG);
        uint16_t bg = rowSel ? 0x3186 : TFT_BLACK;
        int y = kListY + kRowH;
        sprite.fillRect(0, y, LEFT_W, kRowH, bg);
        // Red tint for destructive action
        uint16_t fg = rowSel ? 0xF800 : 0x8C71; // bright red when selected, dim otherwise
        sprite.setTextColor(fg, bg);
        sprite.drawString("Clear config + reboot", 4, y + 2);
    }
}

// ---------------------------------------------------------------------------
// Drawing — Now Playing (right panel, top section)
// ---------------------------------------------------------------------------
void UIManager::drawNowPlaying(const PlayStats &ps)
{
    sprite.setTextColor(COL_ORANGE, TFT_BLACK);
    sprite.drawString("NOW", kRightX, 2);
    sprite.drawFastHLine(DIVIDER_X, 16, RIGHT_W, 0x3186);

    if (!ps.isPlaying && !ps.isPaused)
    {
        sprite.setTextColor(0x4A49, TFT_BLACK);
        sprite.drawString("---", kRightX, 20);
        return;
    }

    // Album art — 64x64, centred horizontally in the right panel
    static const int kArtY = 18;
    static const int kArtX = DIVIDER_X + (RIGHT_W - 64) / 2; // centred
    if (ps.albumArtReady)
    {
        // Fetch into the member buffer once per track (avoids 8 KB on stack)
        if (!cachedArtLoaded)
        {
            cachedArtLoaded = musicManager.getAlbumArt(cachedArt);
        }
        if (cachedArtLoaded && cachedArt.hasArt)
            sprite.pushImage(kArtX, kArtY, AlbumArt::W, AlbumArt::H, cachedArt.pixels);
    }
    else
    {
        cachedArtLoaded = false; // reset so we re-fetch when art becomes ready
        // Placeholder box while art is loading or absent
        sprite.fillRect(kArtX, kArtY, 64, 64, 0x1082);
        sprite.drawRect(kArtX, kArtY, 64, 64, 0x2104);
        sprite.setTextColor(0x2104, 0x1082);
        sprite.setTextDatum(MC_DATUM);
        sprite.drawString("?", kArtX + 32, kArtY + 32);
        sprite.setTextDatum(TL_DATUM);
    }

    int y = kArtY + 64 + 3; // text starts below art

    // Artist — truncate to fit
    if (ps.artist[0])
    {
        sprite.setTextColor(0x8C71, TFT_BLACK);
        char artist[64];
        strlcpy(artist, ps.artist, sizeof(artist));
        while (artist[0] && sprite.textWidth(artist) > kRightW)
            artist[strlen(artist) - 1] = '\0';
        sprite.drawString(artist, kRightX, y);
        y += 14;
    }

    // Title — scrolling marquee using pre-rendered sprite
    {
        const char *title = ps.title[0] ? ps.title : "Unknown";
        rebuildTitleSprite(title); // no-op if title unchanged

        if (titleSpriteW <= kRightW)
        {
            titleSprite.pushToSprite(&sprite, kRightX, y);
        }
        else
        {
            if (windowSprite.width() != kRightW || windowSprite.height() != 15)
            {
                windowSprite.deleteSprite();
                windowSprite.createSprite(kRightW, 15);
            }
            windowSprite.fillSprite(TFT_BLACK);
            titleSprite.pushToSprite(&windowSprite, -titleScrollOff, 0);
            windowSprite.pushToSprite(&sprite, kRightX, y);
        }
        y += 16;
    }

    // Progress bar
    if (ps.durationSec > 0)
    {
        int filled = (int)((float)ps.positionSec / ps.durationSec * kRightW);
        sprite.fillRect(kRightX, y, kRightW, 4, 0x2104);
        sprite.fillRect(kRightX, y, filled, 4, COL_ORANGE);
        y += 7;

        char timeStr[12];
        snprintf(timeStr, sizeof(timeStr), "%lu:%02lu/%lu:%02lu",
                 (unsigned long)(ps.positionSec / 60), (unsigned long)(ps.positionSec % 60),
                 (unsigned long)(ps.durationSec / 60), (unsigned long)(ps.durationSec % 60));
        sprite.setTextColor(0x4A49, TFT_BLACK);
        sprite.drawString(timeStr, kRightX, y);
    }
}

// ---------------------------------------------------------------------------
// Drawing — Playback buttons (right panel, bottom)
// ---------------------------------------------------------------------------
void UIManager::drawPlaybackBtns(const PlayStats &ps)
{
    static const int btnH = 22;
    static const int btnW = 30;
    static const int gap = 2;
    static const int totalW = 3 * btnW + 2 * gap; // 94
    static const int startX = DIVIDER_X + (RIGHT_W - totalW) / 2;
    static const int btnY = SCREEN_H - btnH - 2;

    bool hasFocus = (focus == Focus::RIGHT);
    const char *labels[3] = {"|<", ps.isPaused ? "|>" : "[]", ">|"};

    for (int i = 0; i < 3; i++)
    {
        int bx = startX + i * (btnW + gap);
        bool sel = hasFocus && (i == rightSel);

        uint16_t bg = sel ? 0x528A : (hasFocus ? 0x3186 : 0x2104);
        uint16_t fg = sel ? COL_ORANGE : (hasFocus ? TFT_WHITE : 0x4A49);
        uint16_t bdr = sel ? COL_ORANGE : (hasFocus ? 0x528A : 0x2104);

        sprite.fillRect(bx, btnY, btnW, btnH, bg);
        sprite.drawRect(bx, btnY, btnW, btnH, bdr);
        sprite.setTextColor(fg, bg);
        sprite.setTextDatum(MC_DATUM);
        sprite.drawString(labels[i], bx + btnW / 2, btnY + btnH / 2 - 1);
        sprite.setTextDatum(TL_DATUM);
    }
}

// ---------------------------------------------------------------------------
// drawLeft / drawRight
// ---------------------------------------------------------------------------
void UIManager::drawLeft()
{
    sprite.setTextDatum(TL_DATUM);
    if (leftMode == LeftMode::ALBUMS)
        drawAlbums();
    else if (leftMode == LeftMode::SONGS)
        drawSongs();
    else if (leftMode == LeftMode::SETTINGS)
        drawSettings();
}

void UIManager::drawRight()
{
    PlayStats ps = musicManager.getPlayStats();
    uint16_t divCol = (focus == Focus::RIGHT) ? COL_ORANGE : 0x2104;
    sprite.drawFastVLine(DIVIDER_X, 0, SCREEN_H, divCol);
    drawNowPlaying(ps);
    drawPlaybackBtns(ps);
}

// ---------------------------------------------------------------------------
// Main UI task — Core 0
// ---------------------------------------------------------------------------
void UIManager::live_loop(void *pvParameters)
{
    UIManager *ui = static_cast<UIManager *>(pvParameters);
    ui->sprite.loadFont(NotoSansBold15);

    // Wait for MusicManager
    while (!musicManager.started)
        vTaskDelay(pdMS_TO_TICKS(25));

    // Load config (EEPROM). If invalid → run calibration wizard.
    if (!configManager.load())
    {
        Serial.println("[ui] No valid config — running calibration");
        configManager.runCalibration(ui->tft, ui->sprite);
    }

    // Apply saved volume
    musicManager.setVolume(configManager.cfg.volume);

    // Scan SD library
    ui->loadLibrary();

    while (true)
    {
        uint32_t now = millis();

        // ---- button ----
        int btn = ui->readBtn();

        // Track OK hold
        bool okDown = (ui->prevRaw == BTN_OK);
        uint32_t holdMs = 0;
        if (okDown)
        {
            if (ui->btnDownMs == 0)
                ui->btnDownMs = now;
            holdMs = now - ui->btnDownMs;
        }
        else
        {
            ui->btnDownMs = 0;
            ui->centerHeld = false;
        }

        // SD hot-swap reset
        if (ui->libLoaded && !musicManager.isCardPresent())
        {
            ui->library.clear();
            ui->libLoaded = false;
            ui->openAlbumIdx = -1;
            ui->albumSel = 0;
            ui->albumScroll = 0;
            ui->leftMode = LeftMode::ALBUMS;
            ui->focus = Focus::LEFT;
            ui->cachedArtLoaded = false;
        }
        if (!ui->libLoaded && musicManager.isCardPresent())
            ui->loadLibrary();

        // ---- auto-advance to next song on EOF ----
        if (musicManager.consumeEof())
            ui->playNext(1);

        // ---- dispatch ----
        if (ui->focus == Focus::LEFT)
            ui->handleLeft(btn, holdMs);
        else
            ui->handleRight(btn);

        // ---- title scroll tick ----
        {
            PlayStats ps = musicManager.getPlayStats();
            if ((ps.isPlaying || ps.isPaused) && ui->titleSpriteW > kRightW)
            {
                // Pause at the start position
                if (ui->titleScrollOff == 0 && ui->titleScrollMs == 0)
                {
                    ui->titleScrollMs = now; // mark pause start
                }
                else if (now - ui->titleScrollMs >= (ui->titleScrollOff == 0 ? kScrollPauseMs : kScrollTickMs))
                {
                    ui->titleScrollMs = now;
                    ui->titleScrollOff++;
                    // Pause again once fully scrolled off (gap before reset)
                    if (ui->titleScrollOff > ui->titleSpriteW + 20)
                        ui->titleScrollOff = 0;
                }
            }
            else
            {
                ui->titleScrollOff = 0;
                ui->titleScrollMs = 0;
            }
        }

        // ---- draw ----
        ui->sprite.fillSprite(TFT_BLACK);
        ui->sprite.setTextDatum(TL_DATUM);

        ui->drawLeft();
        ui->drawRight();

        ui->sprite.pushSprite(0, 0);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
