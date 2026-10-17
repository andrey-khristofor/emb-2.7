#include "panic_sensor.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "panic_isr";
static QueueHandle_t s_panic_queue = NULL;

static void IRAM_ATTR panic_isr_handler(void *arg)
{
    uint32_t dummy = 1;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_panic_queue, &dummy, &woken);
    if (woken)
        portYIELD_FROM_ISR();
}

static void panic_task(void *arg)
{
    void (*callback)(void) = (void (*)(void))arg;
    uint32_t dummy;
    while (1)
    {
        if (xQueueReceive(s_panic_queue, &dummy, portMAX_DELAY))
        {
            ESP_LOGE(TAG, "!!! PANIC - hardware of ISR GPIO%d !!!", PANIC_PIN);
            callback();
        }
    }
}

void panic_sensor_init(void (*on_panic_deferred)(void))
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PANIC_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));

    s_panic_queue = xQueueCreate(4, sizeof(uint32_t));
    configASSERT(s_panic_queue);

    BaseType_t ok = xTaskCreate(panic_task, "panic_task", 3072, (void *)on_panic_deferred, 15, NULL);
    configASSERT(ok == pdPASS);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PANIC_PIN, panic_isr_handler, NULL));
}
