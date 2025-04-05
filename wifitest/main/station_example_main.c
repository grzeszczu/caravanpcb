#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_server.h"
#include "driver/ledc.h"

// Wi-Fi konfiguracja
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

// Wi-Fi Event Group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";
static int s_retry_num = 0;

// PWM konfiguracja
#define MOTOR_IN1_GPIO 12
#define MOTOR_IN2_GPIO 13
#define PWM_FREQ_HZ 5000
#define PWM_DUTY 4095
#define PWM_MODE LEDC_LOW_SPEED_MODE
#define PWM_TIMER LEDC_TIMER_0
#define PWM_CHANNEL_IN1 LEDC_CHANNEL_0
#define PWM_CHANNEL_IN2 LEDC_CHANNEL_1

// HTML strona do sterowania
const char* html_page = "<!DOCTYPE html><html><body><h1>ESP32 Sterowanie Silnikiem</h1><button onclick=\"fetch('/activate')\">Uruchom Silnik</button></body></html>";

// Event handler dla Wi-Fi
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Funkcja inicjująca Wi-Fi
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

// Funkcja inicjująca PWM
void pwm_init(void) {
    ESP_LOGI(TAG, "Inicjalizacja PWM...");
    
    ledc_timer_config_t timer_conf = {
        .speed_mode = PWM_MODE,
        .duty_resolution = LEDC_TIMER_12_BIT,
        .timer_num = PWM_TIMER,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_in1 = {
        .gpio_num = MOTOR_IN1_GPIO,
        .speed_mode = PWM_MODE,
        .channel = PWM_CHANNEL_IN1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PWM_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_in1));

    ledc_channel_config_t channel_in2 = {
        .gpio_num = MOTOR_IN2_GPIO,
        .speed_mode = PWM_MODE,
        .channel = PWM_CHANNEL_IN2,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PWM_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_in2));

    ESP_LOGI(TAG, "PWM skonfigurowane pomyślnie");
}

// Funkcja obsługująca żądanie HTTP dla strony głównej
esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Funkcja obsługująca aktywację silnika przez HTTP
esp_err_t activate_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Uruchomienie silnika");
    
    ledc_set_duty(PWM_MODE, PWM_CHANNEL_IN1, PWM_DUTY);  // Włącz IN1 (silnik)
    ledc_set_duty(PWM_MODE, PWM_CHANNEL_IN2, 0);         // Wyłącz IN2
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_IN1);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_IN2);

    vTaskDelay(pdMS_TO_TICKS(3000)); // Silnik działa przez 3 sekundy

    // Cofanie
    ledc_set_duty(PWM_MODE, PWM_CHANNEL_IN1, 0);
    ledc_set_duty(PWM_MODE, PWM_CHANNEL_IN2, PWM_DUTY);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_IN1);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_IN2);

    vTaskDelay(pdMS_TO_TICKS(3000)); // Silnik działa przez 3 sekundy

    httpd_resp_send(req, "Silnik uruchomiony", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Funkcja uruchamiająca serwer HTTP
httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler
        };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t activate_uri = {
            .uri       = "/activate",
            .method    = HTTP_GET,
            .handler   = activate_get_handler
        };
        httpd_register_uri_handler(server, &activate_uri);
    }
    return server;
}

void app_main(void)
{
    // Inicjalizacja NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    // Inicjalizacja Wi-Fi
    wifi_init_sta();

    // Inicjalizacja PWM
    pwm_init();

    // Uruchomienie serwera HTTP
    start_webserver();
}

