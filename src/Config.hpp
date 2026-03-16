#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include <TFT_eSPI.h>

// ---------------------------------------------------------------------------
//  Persistent config — stored in EEPROM with CRC32 checksum.
//
//  Button names (calibration order):
//    0 = Up     1 = Down    2 = Left    3 = Right    4 = OK
//
//  ADC pin is always pin 1 (resistor ladder).
//  "no button" reads close to 4095 (float / high-impedance).
// ---------------------------------------------------------------------------

#define CONFIG_EEPROM_SIZE   64
#define CONFIG_EEPROM_ADDR    0
#define ADC_PIN               1
#define ADC_NO_BTN_MIN     3000   // anything above this = no button pressed
#define CALIB_SETTLE_MS     100   // wait after detecting a press before sampling

struct Config
{
    // Button ADC centre values (raw 12-bit, 0-4095)
    uint16_t btnADC[5] = {0, 0, 0, 0, 0};
    uint16_t btnDelta   = 60;    // ± tolerance
    uint8_t  volume     = 12;    // 0-21
    uint8_t  _pad       = 0;
    uint32_t crc        = 0;
};

// Button index constants (matches calibration order)
#define BTN_UP    0
#define BTN_DOWN  1
#define BTN_LEFT  2
#define BTN_RIGHT 3
#define BTN_OK    4

// ---------------------------------------------------------------------------

class ConfigManager
{
public:
    Config cfg;

    // Load from EEPROM; returns true if valid checksum, false = needs calibration
    bool load();

    // Save current cfg to EEPROM
    void save();

    // Interactive calibration wizard.
    // Draws prompts on the given sprite/tft and blocks until all 5 buttons done.
    void runCalibration(TFT_eSPI &tft, TFT_eSprite &sprite);

    // Read current raw button index (0-4 matching BTN_* defines), or -1 if none
    int readRawBtn() const;

private:
    static uint32_t crc32(const uint8_t *data, size_t len);
    void setCrc();
    bool checkCrc() const;
};

extern ConfigManager configManager;
