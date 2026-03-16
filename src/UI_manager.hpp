#pragma once
#include "MusicManager.hpp"
#include "pin_config.h"
#include <TFT_eSPI.h>
#include "NotoSansBold15.h"
#include "NotoSansMonoSCB20.h"
#include "menus/Menu.hpp"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

// Fixed-width underlying type so the forward declaration in Menu.hpp matches.
enum BUTTONS : int
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

private:
    static void live_loop(void *pvParameters);

    TFT_eSPI tft;
    TFT_eSprite sprite;
    char buffer[13][100];
    int buffer_index;
    int prev_button_value;
    int current_button_value;
    int prev_read_button_value;
    uint32_t last_frame_time = 0;
};

extern UIManager uiManager;
