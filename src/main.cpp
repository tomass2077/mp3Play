#include "pin_config.h"
#include "UI_manager.hpp"
#include "MusicManager.hpp"

void bufferPrint(const char *message)
{
    uiManager.bufferPrint(message);
}

void setup()
{
    Serial.begin(115200);

    // MusicManager::begin() handles SD_MMC pin config, initial mount attempt,
    // and I2S initialisation.  Hot-swap polling is driven by musicManager.loop().
    musicManager.begin();
}

void loop()
{
    musicManager.loop();
}