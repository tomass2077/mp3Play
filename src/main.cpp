#include "FS.h"
#include "SD_MMC.h"
#include "pin_config.h"
#include "UI_manager.hpp"
char buffer[10][100];
int buffer_index = 0;

void bufferPrint(const char *message)
{
    uiManager.bufferPrint(message);
}

void setup()
{

    for (int i = 0; i < 10; i++)
    {
        sprintf(buffer[i], "...");
    }

    // Turn on display power

    Serial.begin(115200);
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    if (!SD_MMC.begin("/sdcard", true, true))
    {
        bufferPrint("Card Mount Failed");
        return;
    }
    uint8_t cardType = SD_MMC.cardType();

    if (cardType == CARD_NONE)
    {
        bufferPrint("No SD_MMC card attached");
        return;
    }

    bufferPrint("SD_MMC Card Type: ");
    if (cardType == CARD_MMC)
    {
        bufferPrint("MMC");
    }
    else if (cardType == CARD_SD)
    {
        bufferPrint("SDSC");
    }
    else if (cardType == CARD_SDHC)
    {
        bufferPrint("SDHC");
    }
    else
    {
        bufferPrint("UNKNOWN");
    }

    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    bufferPrint(("SD_MMC Card Size: " + String(cardSize) + "MB").c_str());
    bufferPrint(("Total space: " + String(SD_MMC.totalBytes() / (1024 * 1024)) + "MB").c_str());
    bufferPrint(("Used space: " + String(SD_MMC.usedBytes() / (1024 * 1024)) + "MB").c_str());
}

void core0Task(void *pvParameters)
{
    while (true)
    {
        // Code here runs on Core 0 (second core)
        // Add your second-core logic here

        // Handle UI

        vTaskDelay(pdMS_TO_TICKS(10)); // Yield to avoid watchdog trigger
    }
}

void loop()
{
    // Main loop runs on Core 1 (default Arduino core)
}