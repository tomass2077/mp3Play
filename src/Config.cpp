#include "Config.hpp"

ConfigManager configManager;

// ---------------------------------------------------------------------------
// CRC32 (standard polynomial 0xEDB88320)
// ---------------------------------------------------------------------------
uint32_t ConfigManager::crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

void ConfigManager::setCrc()
{
    cfg.crc = 0;
    cfg.crc = crc32(reinterpret_cast<const uint8_t *>(&cfg), sizeof(cfg));
}

bool ConfigManager::checkCrc() const
{
    Config tmp = cfg;
    tmp.crc = 0;
    uint32_t computed = crc32(reinterpret_cast<const uint8_t *>(&tmp), sizeof(tmp));
    return computed == cfg.crc;
}

// ---------------------------------------------------------------------------
// Load / Save
// ---------------------------------------------------------------------------
bool ConfigManager::load()
{
    EEPROM.begin(CONFIG_EEPROM_SIZE);
    EEPROM.get(CONFIG_EEPROM_ADDR, cfg);
    EEPROM.end();

    if (!checkCrc())
    {
        // Reset to safe defaults so the rest of the code still works
        cfg = Config{};
        return false;
    }
    return true;
}

void ConfigManager::save()
{
    setCrc();
    EEPROM.begin(CONFIG_EEPROM_SIZE);
    EEPROM.put(CONFIG_EEPROM_ADDR, cfg);
    EEPROM.commit();
    EEPROM.end();
    Serial.println("[cfg] saved");
}

// ---------------------------------------------------------------------------
// Raw button read — returns BTN_* index or -1
// ---------------------------------------------------------------------------
int ConfigManager::readRawBtn() const
{
    int adc = analogRead(ADC_PIN);
    if (adc >= ADC_NO_BTN_MIN)
        return -1;
    for (int i = 0; i < 5; i++)
        if (abs(adc - (int)cfg.btnADC[i]) <= (int)cfg.btnDelta)
            return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Calibration wizard
// ---------------------------------------------------------------------------
void ConfigManager::runCalibration(TFT_eSPI &tft, TFT_eSprite &sprite)
{
    const char *names[5] = {"Up", "Down", "Left", "Right", "OK"};

    Serial.println("[cfg] Starting button calibration");

    for (int b = 0; b < 5; b++)
    {
        // --- prompt ---
        sprite.fillSprite(TFT_BLACK);
        sprite.setTextDatum(MC_DATUM);
        sprite.setTextColor(0xFD20, TFT_BLACK); // orange-ish

        char line1[40], line2[32];
        snprintf(line1, sizeof(line1), "Calibration: %d/5", b + 1);
        snprintf(line2, sizeof(line2), "Press  %s", names[b]);

        sprite.setTextColor(TFT_WHITE, TFT_BLACK);
        sprite.drawString(line1, 160, 60);
        sprite.setTextColor(0xFD20, TFT_BLACK);
        sprite.drawString(line2, 160, 90);
        sprite.setTextDatum(TL_DATUM);
        sprite.pushSprite(0, 0);

        // --- wait for press (release first if still held) ---
        while (analogRead(ADC_PIN) < ADC_NO_BTN_MIN)
            vTaskDelay(pdMS_TO_TICKS(10)); // wait for release

        vTaskDelay(pdMS_TO_TICKS(50)); // debounce

        // --- wait for a new press ---
        while (analogRead(ADC_PIN) >= ADC_NO_BTN_MIN)
            vTaskDelay(pdMS_TO_TICKS(10));

        // --- settle ---
        vTaskDelay(pdMS_TO_TICKS(CALIB_SETTLE_MS));

        int sample = analogRead(ADC_PIN);
        cfg.btnADC[b] = (uint16_t)sample;

        Serial.printf("[cfg] %s = ADC %d\n", names[b], sample);

        // brief confirm flash
        sprite.fillSprite(TFT_BLACK);
        sprite.setTextDatum(MC_DATUM);
        sprite.setTextColor(TFT_GREEN, TFT_BLACK);
        char conf[32];
        snprintf(conf, sizeof(conf), "%s OK  (ADC %d)", names[b], sample);
        sprite.drawString(conf, 160, 85);
        sprite.setTextDatum(TL_DATUM);
        sprite.pushSprite(0, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // --- done: wait for all buttons released ---
    while (analogRead(ADC_PIN) < ADC_NO_BTN_MIN)
        vTaskDelay(pdMS_TO_TICKS(10));

    sprite.fillSprite(TFT_BLACK);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_GREEN, TFT_BLACK);
    sprite.drawString("Calibration complete!", 160, 85);
    sprite.setTextDatum(TL_DATUM);
    sprite.pushSprite(0, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    save();
    Serial.println("[cfg] Calibration saved");
}
