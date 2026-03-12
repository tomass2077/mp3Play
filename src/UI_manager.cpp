#include "UI_manager.hpp"
#define ADC_PIN 1

UIManager uiManager;

const int ButtonDelta = 6; // Tolerance for button press detection
const int buttonAnalogs[5] = {520, 1240, 0, 1950, 2920};

UIManager::UIManager() : tft(TFT_eSPI()), sprite(TFT_eSprite(&tft)), buffer_index(0)
{
    for (int i = 0; i < 10; i++)
    {
        snprintf(buffer[i], sizeof(buffer[i]), "");
    }

    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);

    tft.init(); // amoled lcd initialization
    tft.setRotation(3);
    sprite.createSprite(320, 170);
    sprite.setSwapBytes(1);

    ledcSetup(0, 10000, 8);
    ledcAttachPin(38, 0);
    ledcWrite(0, 160);
    // Load font once - not every frame

    // Clear the sprite, and push to the display
    sprite.fillSprite(TFT_BLACK);
    sprite.pushSprite(0, 0);

    // Setup ADC
    pinMode(ADC_PIN, INPUT);
    prev_button_value = 0;

    xTaskCreatePinnedToCore(
        live_loop,   // Task function
        "core0Task", // Task name
        4096,        // Stack size (bytes)
        this,        // Task parameter
        1,           // Priority
        NULL,        // Task handle
        0            // Core 0 (second core)
    );
}

void UIManager::live_loop(void *pvParameters)
{
    UIManager *ui = static_cast<UIManager *>(pvParameters);
    ui->sprite.loadFont(NotoSansBold15);

    while (true)
    {
        // Log analog value to buffer
        int adcValue = analogRead(ADC_PIN);
        int last_button_value = ui->current_button_value;
        ui->current_button_value = 0;
        for (int i = 0; i < 5; i++)
            if (abs(adcValue - buttonAnalogs[i]) < ButtonDelta)
            {
                ui->current_button_value = i + 1;
                break;
            }
        if (ui->current_button_value != ui->prev_button_value)
        {
            ui->prev_button_value = ui->current_button_value;
            ui->current_button_value = 0;
        }
        if (ui->current_button_value != last_button_value)
        {
            ui->prev_read_button_value = ui->current_button_value;
        }

        // Redraw the sprite with the current buffer contents
        ui->sprite.fillSprite(TFT_BLACK);
        ui->sprite.setTextDatum(0);
        ui->sprite.setTextColor(TFT_WHITE, 0);
        // Font loaded once in constructor

        // ui->sprite.loadFont(NotoSansMonoSCB20);
        for (int i = 0; i < 13; i++)
        {
            ui->sprite.drawString(ui->buffer[(ui->buffer_index - i - 1 + 13) % 13], 0, 170 - 15 - i * 15);
        }

        // To15px is debug
        // Fill 0.5 alpha black
        ui->sprite.fillRect(0, 0, 320, 15, TFT_BLACK);
        ui->sprite.setTextColor(TFT_RED, 0);
        if (ui->current_button_value > 0)
            ui->sprite.drawString(String(ui->current_button_value).c_str(), 0, 0);
        if (ui->prev_read_button_value > 0)
            ui->sprite.drawString(String(ui->prev_read_button_value).c_str(), 10, 0);

        // Calc fps
        uint32_t current_time = micros();
        uint32_t delta_time = current_time - ui->last_frame_time;
        ui->last_frame_time = current_time;
        float fps = 1000000.0f / delta_time;
        ui->sprite.setTextColor(TFT_GREEN, 0);
        ui->sprite.drawString(String(fps, 2).c_str(), 20, 0);

        ui->sprite.pushSprite(0, 0);
        ui->readButton_press();
        vTaskDelay(1); // Yield to scheduler without sleeping
    }
}

void UIManager::bufferPrint(const char *message)
{
    snprintf(buffer[buffer_index], sizeof(buffer[buffer_index]), "%s", message);
    buffer_index = (buffer_index + 1) % 13;
}