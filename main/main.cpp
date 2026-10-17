#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include "button_fsm.h"
#include "load_driver.h"
#include "driver/gpio.h"
#include "pot_sensor.h"
#include "panic_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "arming_console";

static QueueHandle_t s_chirp_queue;

static void request_chirp(uint32_t ms)
{
    xQueueSend(s_chirp_queue, &ms, 0);
}

static void buzzer_task(void *arg)
{
    uint32_t ms;
    while (1)
    {
        if (xQueueReceive(s_chirp_queue, &ms, portMAX_DELAY))
        {
            load_buzzer_chirp(ms);
        }
    }
}

typedef enum
{
    SYS_DISARMED = 0,
    SYS_ARMING,
    SYS_ARMED,
    SYS_ALARM,
} system_state_t;

typedef struct
{
    const char *name;
    bool bypassed;
    bool tripped;
} zone_t;

#define N_ZONES 2
static zone_t s_zones[N_ZONES] = {
    {.name = "ZONE1", .bypassed = false, .tripped = false},
    {.name = "ZONE2", .bypassed = false, .tripped = false},
};

static_assert(N_ZONES <= 8, "bypass bitmask is a single u8");
#define NVS_NAMESPACE "arming"
#define NVS_KEY_BYPASS "bypass"
static TaskHandle_t s_nvs_task = NULL;

static uint8_t zones_bypass_mask(void)
{
    uint8_t mask = 0;
    for (int i = 0; i < N_ZONES; i++)
        if (s_zones[i].bypassed)
            mask |= (uint8_t)(1u << i);
    return mask;
}

static void request_bypass_save(void)
{
    if (s_nvs_task)
        xTaskNotifyGive(s_nvs_task);
}

static void nvs_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void bypass_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "NVS: no saved bypass (first boot) -- all zones active");
        return;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS: open failed (%s) -- all zones active", esp_err_to_name(err));
        return;
    }

    uint8_t mask = 0;
    err = nvs_get_u8(h, NVS_KEY_BYPASS, &mask);
    nvs_close(h);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "NVS: bypass not stored (%s) -- all zones active", esp_err_to_name(err));
        return;
    }

    for (int i = 0; i < N_ZONES; i++)
    {
        s_zones[i].bypassed = (mask >> i) & 1u;
        ESP_LOGI(TAG, "NVS: %s bypass=%d (restored)", s_zones[i].name, s_zones[i].bypassed);
    }
}

static void nvs_save_task(void *arg)
{
    uint8_t saved_mask = zones_bypass_mask();
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint8_t mask = zones_bypass_mask();
        if (mask == saved_mask)
            continue;

        nvs_handle_t h;
        esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
        if (err == ESP_OK)
        {
            err = nvs_set_u8(h, NVS_KEY_BYPASS, mask);
            if (err == ESP_OK)
                err = nvs_commit(h);
            nvs_close(h);
        }
        if (err == ESP_OK)
        {
            saved_mask = mask;
            ESP_LOGI(TAG, "NVS: bypass mask 0x%02x saved", mask);
        }
        else
        {
            ESP_LOGE(TAG, "NVS: bypass save failed (%s)", esp_err_to_name(err));
        }
    }
}

static volatile bool s_panic_latched = false;
static system_state_t s_sys_state = SYS_DISARMED;
static uint32_t s_state_enter_ms = 0;
static uint32_t s_arming_total_s = 0;

static volatile bool s_duress = false;

#define DURESS_PULSE_PERIOD_MS 5000u
#define DURESS_PULSE_MS 40u

#define N_FSM_BUTTONS 3
enum
{
    IDX_ARM = 0,
    IDX_ZONE1,
    IDX_ZONE2
};
static button_fsm_t s_buttons[N_FSM_BUTTONS];

#define ARM_DELAY_MIN_S 3u
#define ARM_DELAY_MAX_S 15u

static uint32_t arming_delay_seconds(void)
{
    uint16_t raw = pot_sensor_get_raw();
    return ARM_DELAY_MIN_S + ((uint32_t)raw * (ARM_DELAY_MAX_S - ARM_DELAY_MIN_S)) / 4095u;
};

static void sys_transition(system_state_t new_state, uint32_t now_ms)
{
    system_state_t old_state = s_sys_state;
    ESP_LOGW(TAG, "SYSTEM: %d -> %d", old_state, new_state);
    s_sys_state = new_state;
    s_state_enter_ms = now_ms;

    if (new_state == SYS_ALARM)
    {
        load_relay_set(true);
        load_buzzer_set(true);
    }
    else if (old_state == SYS_ALARM)
    {
        load_relay_set(false);
        load_buzzer_set(false);
    }
}

static void handle_arm_button(button_event_t ev, uint32_t now_ms)
{
    if (ev == BTN_EVENT_SHORT_PRESS)
    {
        switch (s_sys_state)
        {
        case SYS_DISARMED:
            s_arming_total_s = arming_delay_seconds();
            sys_transition(SYS_ARMING, now_ms);
            request_chirp(60);
            ESP_LOGI(TAG, "Installation of security: %lu s to ARMED", (unsigned long)s_arming_total_s);
            break;

        case SYS_ARMING:
            sys_transition(SYS_DISARMED, now_ms);
            request_chirp(200);
            break;

        case SYS_ARMED:
            sys_transition(SYS_DISARMED, now_ms);
            request_chirp(300);
            ESP_LOGI(TAG, "Disarmed.");
            break;

        case SYS_ALARM:
            sys_transition(SYS_DISARMED, now_ms);
            s_panic_latched = false;
            ESP_LOGW(TAG, "The alarm has been cleared, the system disarmed.");
            break;
        }
    }
    else if (ev == BTN_EVENT_LONG_PRESS && s_sys_state == SYS_ARMED)
    {
        sys_transition(SYS_DISARMED, now_ms);
        load_relay_set(false);
        load_buzzer_set(false);
        s_duress = true;
        ESP_LOGI(TAG, "Disarmed.");
    }
    else if (ev == BTN_EVENT_LONG_PRESS)
    {
        for (int i = 0; i < N_ZONES; i++)
        {
            s_zones[i].bypassed = false;
            s_zones[i].tripped = false;
        }
        s_panic_latched = 0;
        sys_transition(SYS_DISARMED, now_ms);
        load_relay_set(false);
        load_buzzer_set(false);
        request_bypass_save();
        ESP_LOGW(TAG, "FULL RESET OFF PANEL (LONG PRESS ARM).");
    }
}

static void handle_zone_button(int zone_idx, button_event_t ev, uint32_t now_ms)
{
    zone_t *z = &s_zones[zone_idx];
    if (ev == BTN_EVENT_PRESS)
    {
        z->tripped = true;
        ESP_LOGI(TAG, "%s: simulation working of sensor (bypass=%d)", z->name, z->bypassed);
        if (s_sys_state == SYS_ARMED && !z->bypassed)
        {
            sys_transition(SYS_ALARM, now_ms);
            ESP_LOGE(TAG, "!!! ALARM: %s !!!", z->name);
        }
    }
    else if (ev == BTN_EVENT_LONG_PRESS)
    {
        if (s_sys_state != SYS_DISARMED)
        {
            ESP_LOGW(TAG, "%s: bypass change ignored (system not DISARMED)", z->name);
            return;
        }
        z->bypassed = !z->bypassed;
        request_bypass_save();
        request_chirp(z->bypassed ? 150 : 50);
        ESP_LOGW(TAG, "%s: bypass %s", z->name, z->bypassed ? "TURN ON" : "TURN OFF");
    }
}

static void app_fsm_tick_all(uint32_t now_ms)
{
    button_event_t ev;

    ev = button_fsm_tick(&s_buttons[IDX_ARM], now_ms);
    if (ev != BTN_EVENT_NONE)
        handle_arm_button(ev, now_ms);

    ev = button_fsm_tick(&s_buttons[IDX_ZONE1], now_ms);
    if (ev != BTN_EVENT_NONE)
        handle_zone_button(0, ev, now_ms);

    ev = button_fsm_tick(&s_buttons[IDX_ZONE2], now_ms);
    if (ev != BTN_EVENT_NONE)
        handle_zone_button(1, ev, now_ms);

    if (s_sys_state == SYS_ARMING)
    {
        uint32_t elapsed_s = (now_ms - s_state_enter_ms) / 1000u;
        if (elapsed_s >= s_arming_total_s)
        {
            sys_transition(SYS_ARMED, now_ms);
            request_chirp(500);
            ESP_LOGW(TAG, "The system is armed.");
        }
    }

    if (s_panic_latched && s_sys_state != SYS_ALARM)
    {
        sys_transition(SYS_ALARM, now_ms);
    }

    if (s_sys_state == SYS_ALARM)
    {
        load_relay_set(true);
        load_buzzer_set(true);
    }
}

static void on_panic_deferred(void)
{
    s_panic_latched = true;
    ESP_LOGE(TAG, "PANIC detected; an alarm will be raised on the next FSM tick.");
}

#define STATUS_BEEP_MS 30u
#define ARMED_HEARTBEAT_PERIOD_MS 5000u
#define STATUS_POLL_MS 100u

static void status_blip(void)
{
    load_buzzer_set(true);
    vTaskDelay(pdMS_TO_TICKS(STATUS_BEEP_MS));
    if (s_sys_state != SYS_ALARM)
        load_buzzer_set(false);
}

static void status_beep_task(void *arg)
{
    system_state_t prev_state = SYS_DISARMED;
    uint32_t last_heartbeat_ms = 0;
    while (1)
    {
        system_state_t state = s_sys_state;
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

        if (state == SYS_ARMING)
        {
            uint32_t elapsed_s = (now_ms - s_state_enter_ms) / 1000u;
            uint32_t remaining_s = (elapsed_s < s_arming_total_s) ? (s_arming_total_s - elapsed_s) : 0;
            uint32_t beep_period_ms = 80 + (remaining_s * 420) / (ARM_DELAY_MAX_S);
            status_blip();
            vTaskDelay(pdMS_TO_TICKS(beep_period_ms));
        }
        else if (state == SYS_ARMED)
        {
            if (prev_state != SYS_ARMED)
                last_heartbeat_ms = s_state_enter_ms;
            if (now_ms - last_heartbeat_ms >= ARMED_HEARTBEAT_PERIOD_MS)
            {
                last_heartbeat_ms = now_ms;
                if (s_sys_state == SYS_ARMED)
                    status_blip();
            }
            vTaskDelay(pdMS_TO_TICKS(STATUS_POLL_MS));
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(STATUS_POLL_MS));
        }
        prev_state = state;
    }
}

static void duress_task(void *arg)
{
#if LOAD_BUZZER_MODE == LOAD_BUZZER_MODE_RELAY
    ESP_LOGI(TAG, "duress relay channel disabled in LOAD_BUZZER_MODE_RELAY");
    vTaskDelete(NULL);
#endif
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(DURESS_PULSE_PERIOD_MS));
        if (s_duress && s_sys_state != SYS_ALARM)
        {
            load_relay_set(true);
            vTaskDelay(pdMS_TO_TICKS(DURESS_PULSE_MS));
            if (s_sys_state != SYS_ALARM)
                load_relay_set(false);
        }
    }
}

extern "C" void app_main(void)
{
    nvs_storage_init();
    bypass_load();
    xTaskCreate(nvs_save_task, "nvs_save", 3072, NULL, 3, &s_nvs_task);

    load_driver_init();
    button_fsm_init(&s_buttons[IDX_ARM], GPIO_NUM_4);
    button_fsm_init(&s_buttons[IDX_ZONE1], GPIO_NUM_5);
    button_fsm_init(&s_buttons[IDX_ZONE2], GPIO_NUM_6);

    panic_sensor_init(on_panic_deferred);
    pot_sensor_init();

    s_chirp_queue = xQueueCreate(4, sizeof(uint32_t));
    xTaskCreate(buzzer_task, "buzzer_task", 3072, NULL, 4, NULL);

    button_fsm_start_timer(app_fsm_tick_all);

    xTaskCreate(status_beep_task, "status_beep", 3072, NULL, 4, NULL);
    xTaskCreate(duress_task, "duress", 3072, NULL, 4, NULL);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "STATE=%d\tZONE1(bypass=%d, trip=%d)\tZONE2(bypass=%d, trip=%d)\t[POT=%u]",
                 s_sys_state,
                 s_zones[0].bypassed, s_zones[0].tripped,
                 s_zones[1].bypassed, s_zones[1].tripped,
                 pot_sensor_get_raw());
    }
}
