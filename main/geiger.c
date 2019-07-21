#include "geiger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// TODO: Remove globals.
static xQueueHandle geiger_event_queue = NULL;
static void (*geiger_callback)();

static void IRAM_ATTR geiger_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(geiger_event_queue, &gpio_num, NULL);
}

static void geiger_event_task(void *arg) {
    uint32_t io_num;
    while (true) {
        if (xQueueReceive(geiger_event_queue, &io_num, portMAX_DELAY)) {
            geiger_callback();
        }
    }
}

void geiger_init(gpio_num_t int_pin, void (*callback)()) {
    geiger_callback = callback;
    geiger_event_queue = xQueueCreate(10, sizeof(uint32_t));

    gpio_config_t io_conf = {
        .pin_bit_mask = 1<<int_pin,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = 0,
        .pull_down_en = 0,
        .intr_type    = GPIO_INTR_POSEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(int_pin, geiger_isr_handler, (void*)int_pin));

    xTaskCreate(geiger_event_task, "geiger_event_task", 2048, NULL, 10, NULL);
}
