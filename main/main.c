#include <driver/i2c.h>
#include <esp_event_loop.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_wifi.h>
#include <lwip/apps/sntp.h>
#include <nvs_flash.h>
#include <string.h>

#include "prometheus.h"
#include "prometheus_esp32.h"
//=========================================================================================================================
//Begin hier
#define WIFI_SSID "fancomwifi"
#define WIFI_PASSWORD "fancom password"

prom_counter_t metric_errors; //Een counter is een waarde die alleen omhoog gaat
prom_gauge_t metric_tvoc; //Een gauge is een waarde die alleen maar toe neemt
//=========================================================================================================================


void init_metrics() {
    init_metrics_esp32(prom_default_registry());

    prom_strings_t errors_strings = {
        .namespace = NULL,
        .subsystem = "sensors",
        .name      = "errors",
        .help      = "The total number of errors that occurred while performing measurements per sensor",
    };
    const char *errors_labels[] = { "sensor", "code" };
    prom_counter_init_vec(&metric_errors, errors_strings, errors_labels, 2);
    prom_register_counter(prom_default_registry(), &metric_errors);

    prom_strings_t tvoc_strings = {
        .namespace = NULL,
        .subsystem = "sensors",
        .name      = "tvoc_ppb",
        .help      = "Bla bla help tekst",
    };
    prom_gauge_init(&metric_tvoc, tvoc_strings);
    prom_register_gauge(prom_default_registry(), &metric_tvoc);
}

void record_sensor_error(const char *sensor, esp_err_t code) {
    char code_str[12];
    snprintf(code_str, sizeof(code_str), "0x%x", code);
    prom_counter_inc(&metric_errors, sensor, code_str);
    ESP_LOGE("main", "%s err: 0x%x", sensor, code);
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event) {
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            ESP_LOGI("main", "SYSTEM_EVENT_STA_START");
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI("main", "SYSTEM_EVENT_STA_GOT_IP");
            ESP_LOGI("main", "Got IP: '%s'",
                    ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGI("main", "SYSTEM_EVENT_STA_DISCONNECTED");
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void init_wifi() {
    tcpip_adapter_init();

    char hostname[32];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(hostname, sizeof(hostname), "sensornode-%x%x", mac[4], mac[5]);
    tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, hostname);
    ESP_LOGI("main", "Hostname: %s", hostname);

    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_LOGI("main", "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = ESP_OK;
    }
    ESP_ERROR_CHECK(err);

    init_metrics();
    init_wifi();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    httpd_handle_t http_server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(httpd_start(&http_server, &config));
    httpd_uri_t prom_export_uri = prom_http_uri(prom_default_registry());
    ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &prom_export_uri));
    ESP_LOGI("main", "Started server on port %d", config.server_port);

    const i2c_config_t i2c_conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = GPIO_NUM_21,
        .scl_io_num       = GPIO_NUM_22,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    while (true) {

        uint16_t tvoc = 666;
        prom_gauge_set(&metric_tvoc, tvoc);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
