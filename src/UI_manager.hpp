#pragma once
#include "MusicManager.hpp"
#include "Config.hpp"
#include "pin_config.h"
#include <TFT_eSPI.h>
#include "NotoSansBold15.h"

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
#define SCREEN_W 320
#define SCREEN_H 170
#define RIGHT_W 100
#define LEFT_W (SCREEN_W - RIGHT_W) // 220 px — left panel
#define DIVIDER_X LEFT_W            // x = 220

// Accent colour (255, 92, 0) in RGB565
#define COL_ORANGE ((uint16_t)(((255 >> 3) << 11) | ((92 >> 2) << 5) | (0 >> 3)))

// How long (ms) OK must be held to open/close Settings
#define HOLD_SETTINGS_MS 600

// ---------------------------------------------------------------------------
// Focus / mode enums
// ---------------------------------------------------------------------------
enum class Focus
{
    LEFT,
    RIGHT
};
enum class LeftMode
{
    ALBUMS,
    SONGS,
    SETTINGS
};
enum class SettingItem
{
    VOLUME,
    CLEAR_CONFIG
};
static const int SETTINGS_COUNT = 2;

// ---------------------------------------------------------------------------

class UIManager
{
public:
    UIManager();
    void bufferPrint(const char *message);

private:
    static void live_loop(void *pvParameters);

    // ---- drawing ----
    void drawLeft();
    void drawRight();
    void drawAlbums();
    void drawSongs();
    void drawSettings();
    void drawNowPlaying(const PlayStats &ps);
    void drawPlaybackBtns(const PlayStats &ps);
    void rebuildTitleSprite(const char *title);

    // ---- input ----
    // Returns BTN_UP/DOWN/LEFT/RIGHT/OK on a new press, -1 otherwise
    int readBtn();
    void handleLeft(int btn, uint32_t holdMs);
    void handleRight(int btn);

    // ---- helpers ----
    void loadLibrary();
    void openAlbum(int idx);
    void playSong(int songIdx);
    void playNext(int delta); // delta = +1 or -1

    // ---- TFT ----
    TFT_eSPI tft;
    TFT_eSprite sprite;

    // ---- navigation state ----
    Focus focus = Focus::LEFT;
    LeftMode leftMode = LeftMode::ALBUMS;

    // Album list
    std::vector<FolderInfo> library;
    bool libLoaded = false;
    int albumSel = 0;
    int albumScroll = 0;

    // Song list
    int openAlbumIdx = -1;
    int songSel = 0;
    int songScroll = 0;

    // Settings
    SettingItem settingSel = SettingItem::VOLUME;
    bool settingEditing = false;

    // Currently playing song coords
    int playAlbum = -1;
    int playSongIdx = -1;

    // Right panel: which playback button is selected (0=prev, 1=play/pause, 2=next)
    int rightSel = 1;

    // ---- title scroll ----
    // We render the title into a fixed-size sprite once per title change,
    // then just blit it at a scrolling x-offset each frame — no per-frame
    // sprite create/delete = no flicker.
    TFT_eSprite titleSprite;  // wide enough to hold the full title text
    TFT_eSprite windowSprite; // fixed-width clip window for scrolling (persistent)
    char titleCache[128];     // last title we rendered into titleSprite
    int titleSpriteW = 0;     // pixel width of titleSprite content
    int titleScrollOff = 0;
    uint32_t titleScrollMs = 0;

    // ---- album art cache (avoids 8 KB on the UI task stack) ----
    AlbumArt cachedArt;
    bool cachedArtLoaded = false; // true once fetched for the current track

    // ---- button debounce ----
    int prevRaw = -1;       // last raw btn index (-1 = none)
    uint32_t btnDownMs = 0; // millis() when OK was first held
    bool centerHeld = false;

    // ---- debug log ----
    char logBuf[8][80];
    int logIdx = 0;
};

extern UIManager uiManager;
