#include "fsm.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"

#define FAN_ON 0
#define FAN_OFF 1
#define FAN_RELAY_GPIO GPIO_NUM_3

#define MINUTES(x) ((x) * 60 * 1000000LL)
#define LOGI(...) printf(__VA_ARGS__)
#define MAX_LOG_ENTRIES 50

static fsm_state_t current_state = IDLE;
static int64_t last_high_humidity_time = 0;
static int64_t fan_start_time = 0;
static int64_t last_transition_time = 0;
static bool fan_on = false;
static const uint8_t image_clock_quarters_bits[] = {0xe0, 0x03, 0x98, 0x0c, 0x84, 0x10, 0x02, 0x20, 0x82, 0x20, 0x81, 0x40, 0x81, 0x40, 0x87, 0x70, 0x01, 0x41, 0x01, 0x42, 0x02, 0x20, 0x02, 0x20, 0x84, 0x10, 0x98, 0x0c, 0xe0, 0x03, 0x00, 0x00};
static const uint8_t image_device_power_button_bits[] = {0x80, 0x00, 0x80, 0x00, 0x98, 0x0c, 0xa4, 0x12, 0x92, 0x24, 0x8a, 0x28, 0x85, 0x50, 0x05, 0x50, 0x05, 0x50, 0x05, 0x50, 0x05, 0x50, 0x0a, 0x28, 0x12, 0x24, 0xe4, 0x13, 0x18, 0x0c, 0xe0, 0x03};
static const uint8_t image_file_upload_bits[] = {0x00, 0x00, 0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0x90, 0x04, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x87, 0x70, 0x05, 0x50, 0xfd, 0x5f, 0x01, 0x40, 0x01, 0x40, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00};
static const uint8_t image_moon_white_bits[] = {0x20, 0x08, 0x38, 0x08, 0x14, 0x2a, 0x12, 0x1c, 0xaa, 0xb6, 0x39, 0x1c, 0x19, 0x2a, 0x09, 0x08, 0x11, 0x08, 0x11, 0x60, 0x62, 0x38, 0x82, 0x27, 0x04, 0x10, 0x18, 0x0c, 0xe0, 0x03};

// -------------------- FAN CONTROL --------------------

static void fsm_turn_fan_on()
{
    fan_on = true;
    fan_start_time = esp_timer_get_time();
    gpio_set_level(FAN_RELAY_GPIO, FAN_ON);
}

static void fsm_turn_fan_off()
{
    fan_on = false;
    gpio_set_level(FAN_RELAY_GPIO, FAN_OFF);
}

// -------------------- PUBLIC API --------------------

fsm_state_t fsm_get_state()
{
    return current_state;
}

bool fsm_is_fan_on()
{
    return fan_on;
}

static void log_fsm_transition_to_nvs(const char *transition_label, float humidity)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("fsm_log", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return;

    uint32_t index = 0;
    nvs_get_u32(handle, "log_index", &index);
    index = (index + 1) % MAX_LOG_ENTRIES;

    char key[16];
    snprintf(key, sizeof(key), "entry_%lu", (unsigned long)index);

    int64_t now = esp_timer_get_time();
    int seconds = now / 1000000;
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;

    char log_line[64];
    // Record: STATE + timestamp (in local ESP's time) converted to human time (since start)
    snprintf(log_line, sizeof(log_line), "%02d:%02d:%02d: %s [%.1f%%]", hours, minutes, secs, transition_label, humidity);

    nvs_set_str(handle, key, log_line);
    nvs_set_u32(handle, "log_index", index);
    nvs_commit(handle);
    nvs_close(handle);
}

void fsm_init()
{
    current_state = IDLE;
    fsm_turn_fan_off();
    last_high_humidity_time = esp_timer_get_time();
    fan_start_time = 0;
    last_transition_time = 0;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    LOGI("FSM initialized\n");
    log_fsm_transition_to_nvs("FSM initialized", 0.0f);

    // Print logs from the memory
    nvs_handle_t handle;
    nvs_open("fsm_log", NVS_READONLY, &handle);
    for (int i = 0; i < MAX_LOG_ENTRIES; ++i)
    {
        char key[16], value[64];
        size_t len = sizeof(value);
        snprintf(key, sizeof(key), "entry_%02d", i);
        if (nvs_get_str(handle, key, value, &len) == ESP_OK)
        {
            printf("%s: %s\n", key, value);
        }
    }
    nvs_close(handle);
}

void fsm_update(float humidity)
{
    int64_t now = esp_timer_get_time();

    switch (current_state)
    {
    case IDLE:
        if (humidity > 70.0)
        {
            current_state = COOLING;
            fsm_turn_fan_on();
            LOGI("Transition to COOLING\n");
            log_fsm_transition_to_nvs("COOLING", humidity);
        }
        else if ((now - last_high_humidity_time) > MINUTES(360))
        {
            current_state = FORCE;
            fsm_turn_fan_on();
            LOGI("Transition to FORCE\n");
            log_fsm_transition_to_nvs("FORCE", humidity);
        }
        if (humidity > 70.0)
        {
            last_high_humidity_time = now;
        }
        break;

    case COOLING:
        if ((now - fan_start_time) > MINUTES(30) || humidity < 70.0)
        {
            current_state = WAITING;
            fsm_turn_fan_off();
            last_transition_time = now;
            LOGI("Transition to WAITING\n");
            log_fsm_transition_to_nvs("WAITING", humidity);
        }
        break;

    case FORCE:
        if ((now - fan_start_time) > MINUTES(30))
        {
            current_state = IDLE;
            fsm_turn_fan_off();
            last_high_humidity_time = now;
            LOGI("Transition to IDLE (from FORCE)\n");
            log_fsm_transition_to_nvs("IDLE (from FORCE)", humidity);
        }
        break;

    case WAITING:
        if ((now - last_transition_time) > MINUTES(120))
        {
            current_state = IDLE;
            LOGI("Transition to IDLE (from WAITING)\n");
            log_fsm_transition_to_nvs("IDLE (from WAITING)", humidity);
        }
        break;
    }
}

void fsm_get_display_lines(char *fan_line, char *timer_line, char *state_line)
{
    int64_t now = esp_timer_get_time();

    strcpy(fan_line, fan_on ? "FAN ON " : "FAN OFF");

    int64_t remaining = 0;
    switch (current_state)
    {
    case COOLING:
    case FORCE:
        remaining = MINUTES(30) - (now - fan_start_time);
        break;
    case WAITING:
        remaining = MINUTES(120) - (now - last_transition_time);
        break;
    case IDLE:
        remaining = MINUTES(360) - (now - last_high_humidity_time);
        break;
    default:
        remaining = 0;
        break;
    }
    if (remaining < 0)
        remaining = 0;

    int mins = remaining / 1000000 / 60;
    int secs = (remaining / 1000000) % 60;
    sprintf(timer_line, "%02d:%02d", mins, secs);

    switch (current_state)
    {
    case IDLE:
        strcpy(state_line, "IDLE   ");
        break;
    case COOLING:
        strcpy(state_line, "COOLING");
        break;
    case FORCE:
        strcpy(state_line, "FORCE  ");
        break;
    case WAITING:
        strcpy(state_line, "WAIT   ");
        break;
    }
}

uint8_t *fsm_get_state_icon()
{
    switch (current_state)
    {
    case IDLE:
        return image_moon_white_bits;
    case COOLING:
        return image_device_power_button_bits;
    case FORCE:
        return image_file_upload_bits;
    case WAITING:
        return image_clock_quarters_bits;
    }
    return image_clock_quarters_bits;
}

void fsm_set_manual_override(bool new_state)
{
    int64_t now = esp_timer_get_time();

    if (new_state)
    {
        current_state = COOLING;
        fsm_turn_fan_on();
        LOGI("Manual override: FAN ON (COOLING)\n");
    }
    else
    {
        current_state = IDLE;
        fsm_turn_fan_off();
        last_high_humidity_time = now;
        LOGI("Manual override: FAN OFF (IDLE)\n");
    }
}
