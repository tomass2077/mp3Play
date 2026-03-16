#include "UI_manager.hpp"
#include "menus/FileListMenu.hpp"
#include "menus/SettingsMenu.hpp"

#define ADC_PIN 1

UIManager uiManager;
MenuManager menuManager;

// ---------------------------------------------------------------------------
// Static menu instances (avoids heap fragmentation on embedded target)
// ---------------------------------------------------------------------------
static FileListMenu s_fileListMenu;
static SettingsMenu s_settingsMenu;

// ---------------------------------------------------------------------------
// Button ADC detection
// ---------------------------------------------------------------------------
const int ButtonDelta = 6;
const int buttonAnalogs[5] = {520, 1240, 0, 1950, 2920};

// ---------------------------------------------------------------------------

UIManager::UIManager() : tft(TFT_eSPI()), sprite(TFT_eSprite(&tft)), buffer_index(0)
{
    for (int i = 0; i < 13; i++)
        snprintf(buffer[i], sizeof(buffer[i]), "");

    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);

    tft.init();
    tft.setRotation(3);
    sprite.createSprite(320, 170);
    sprite.setSwapBytes(1);

    ledcSetup(0, 10000, 8);
    ledcAttachPin(38, 0);
    ledcWrite(0, 160);

    sprite.fillSprite(TFT_BLACK);
    sprite.pushSprite(0, 0);

    pinMode(ADC_PIN, INPUT);
    prev_button_value      = 0;
    current_button_value   = 0;
    prev_read_button_value = 0;

    xTaskCreatePinnedToCore(
        live_loop,
        "uiTask",
        12288,
        this,
        1,
        NULL,
        0
    );
}

// ---------------------------------------------------------------------------
// UI task — runs on Core 0
// ---------------------------------------------------------------------------
void UIManager::live_loop(void *pvParameters)
{
    UIManager *ui = static_cast<UIManager *>(pvParameters);
    ui->sprite.loadFont(NotoSansBold15);

    while (!musicManager.started)
        vTaskDelay(25);

    // Push the root menu (folder browser).
    // BUTTON_RIGHT from FileListMenu opens Settings.
    menuManager.push(&s_fileListMenu);

    ui->bufferPrint("Menu system ready");

    uint32_t last_time = micros();

    while (true)
    {
        // ---- Button reading ----
        int adcValue = analogRead(ADC_PIN);
        int last_button_value = ui->current_button_value;
        ui->current_button_value = 0;
        for (int i = 0; i < 5; i++)
            if (abs(adcValue - buttonAnalogs[i]) < ButtonDelta)
            {
                ui->current_button_value = i + 1;
                break;
            }
        // Detect rising edge
        if (ui->current_button_value != ui->prev_button_value)
        {
            ui->prev_button_value = ui->current_button_value;
            ui->current_button_value = 0;
        }
        if (ui->current_button_value != last_button_value)
            ui->prev_read_button_value = ui->current_button_value;

        // ---- Timing ----
        uint32_t now = micros();
        uint32_t dtUs = now - last_time;
        last_time = now;

        // ---- Hot-swap: rescan when needed ----
        // (MenuManager menus call scanLibrary which handles this internally)

        // ---- Clear frame ----
        ui->sprite.fillSprite(TFT_BLACK);
        ui->sprite.setTextDatum(0);
        ui->sprite.setTextColor(TFT_WHITE, 0);

        // ---- Dispatch button ----
        int cur_button = ui->readButton_press();
        if (cur_button != 0)
        {
            ui->bufferPrint(("Button: " + std::to_string(cur_button)).c_str());

            // BUTTON_RIGHT at root level opens/closes Settings
            if (cur_button == BUTTON_RIGHT && menuManager.depth() == 1)
                menuManager.push(&s_settingsMenu);
            else
                menuManager.onButton(static_cast<BUTTONS>(cur_button));
        }

        // ---- Update & draw active menu ----
        menuManager.update(dtUs);
        menuManager.draw(ui->sprite);

        // ---- Debug overlay ----
#ifdef DEBUG
        float fps = dtUs > 0 ? 1000000.0f / dtUs : 0;
        ui->sprite.fillRect(0, 0, 320, 15, TFT_BLACK);
        ui->sprite.setTextColor(TFT_GREEN, 0);
        ui->sprite.drawString(String(fps, 1).c_str(), 0, 0);
        ui->sprite.setTextColor(TFT_RED, 0);
        if (ui->prev_read_button_value > 0)
            ui->sprite.drawString(String(ui->prev_read_button_value).c_str(), 50, 0);

        // Debug log lines
        ui->sprite.setTextColor(sprite.color565(150,150,150), 0);
        for (int i = 0; i < 13; i++)
            ui->sprite.drawString(ui->buffer[(ui->buffer_index - i - 1 + 13) % 13], 0, 170 - 15 - i * 15);
#endif

        ui->sprite.pushSprite(0, 0);
        vTaskDelay(1);
    }
}

void UIManager::bufferPrint(const char *message)
{
    snprintf(buffer[buffer_index], sizeof(buffer[buffer_index]), "%s", message);
    buffer_index = (buffer_index + 1) % 13;
    Serial.println(message);
}
