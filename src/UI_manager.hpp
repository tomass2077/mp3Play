#pragma once
#include "FS.h"
#include "SD_MMC.h"
#include "pin_config.h"
#include <TFT_eSPI.h>
#include "NotoSansBold15.h"
#include "NotoSansMonoSCB20.h"
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
enum BUTTONS
{
    BUTTON_UP = 1,
    BUTTON_DOWN = 2,
    BUTTON_LEFT = 3,
    BUTTON_RIGHT = 4,
    BUTTON_CENTER = 5,
};

class UIManager
{
public:
    UIManager();
    void bufferPrint(const char *message);
    BUTTONS readButton_press()
    {
        BUTTONS out = static_cast<BUTTONS>(prev_read_button_value);
        prev_read_button_value = 0;
        return out;
    }

    void update_song_state(const char *title, const char *artist, uint32_t duration_ms, uint32_t elapsed_ms)
    {
        xSemaphoreTake(current_song_Mutex, portMAX_DELAY);
        memcpy(current_song.title, title, MIN(strlen(title), 64));
        current_song.title[64] = 0;
        memcpy(current_song.artist, artist, MIN(strlen(artist), 64));
        current_song.artist[64] = 0;

        current_song.duration_ms = duration_ms;
        current_song.elapsed_ms = elapsed_ms;

        xSemaphoreGive(current_song_Mutex);
    }

private:
    static void
    live_loop(void *pvParameters);

    TFT_eSPI tft;
    TFT_eSprite sprite;
    char buffer[13][100];
    int buffer_index;
    int prev_button_value;
    int current_button_value;
    int prev_read_button_value;
    uint32_t last_frame_time = 0;

    SemaphoreHandle_t current_song_Mutex;
    struct
    {
        char title[65];
        char artist[100];
        uint32_t duration_ms;
        uint32_t elapsed_ms;
    } current_song;
};

extern UIManager uiManager;