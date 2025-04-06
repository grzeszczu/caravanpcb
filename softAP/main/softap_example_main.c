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
#include "esp_mac.h"

#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#define MOTOR_IN1_GPIO 12
#define MOTOR_IN2_GPIO 13
#define MOTOR_IN3_GPIO 14
#define MOTOR_IN4_GPIO 15
#define MOTOR_IN5_GPIO 16
#define MOTOR_IN6_GPIO 17
#define MOTOR_IN7_GPIO 18
#define MOTOR_IN8_GPIO 19

#define PWM_FREQ_HZ 5000
#define PWM_DUTY 4095
#define PWM_MODE LEDC_LOW_SPEED_MODE
#define PWM_TIMER LEDC_TIMER_0

#define PWM_CHANNEL_IN1 LEDC_CHANNEL_0
#define PWM_CHANNEL_IN2 LEDC_CHANNEL_1
#define PWM_CHANNEL_IN3 LEDC_CHANNEL_2
#define PWM_CHANNEL_IN4 LEDC_CHANNEL_3
#define PWM_CHANNEL_IN5 LEDC_CHANNEL_4
#define PWM_CHANNEL_IN6 LEDC_CHANNEL_5
#define PWM_CHANNEL_IN7 LEDC_CHANNEL_6
#define PWM_CHANNEL_IN8 LEDC_CHANNEL_7

const char* html_page = "<!DOCTYPE html><html><body><h1>ESP32 Sterowanie Silownikami</h1>"
                        "<h3>Silownik 1</h3>"
                        "<label><input type=\"radio\" name=\"actuator_1\" onclick=\"toggleActuator(1, 'extend')\" /> Wysuwaj</label><br>"
                        "<label><input type=\"radio\" name=\"actuator_1\" onclick=\"toggleActuator(1, 'retract')\" /> Chowaj</label><br>"
                        "<label><input type=\"radio\" name=\"actuator_1\" onclick=\"toggleActuator(1, 'stop')\" checked /> Stop</label><br>"
                        "<h3>Silownik 2</h3>"
                        "<label><input type=\"radio\" name=\"actuator_2\" onclick=\"toggleActuator(2, 'extend')\" /> Wysuwaj</label><br>"
                        "<label><input type=\"radio\" name=\"actuator_2\" onclick=\"toggleActuator(2, 'retract')\" /> Chowaj</label><br>"
                        "<label><input type=\"radio\" name=\"actuator_2\" onclick=\"toggleActuator(2, 'stop')\" checked /> Stop</label><br>"
                        "<h3>Silownik 3</h3>"
                        "<label><input type=\"radio\" name=\"actuator_3\" onclick=\"toggleActuator(3, 'extend')\" /> Wysuwaj</label><br>"
                        "<label><input type=\"radio\" name=\"actuator_3\" onclick=\"toggleActuator(3, 'retract')\" /> Chowaj</label><br>"
                        "<label><input type=\"radio\" name=\"actuator_3\" onclick=\"toggleActuator(3, 'stop')\" checked /> Stop</label><br>"
                        "<h3>Silownik 4</h3>"
                        "<label><input type=\"radio\" name=\"actuator_4\" onclick=\"toggleActuator(4, 'extend')\" /> Wysuwaj</label><br>"
                        "<label><input type=\"radio\" name=\"actuator_4\" onclick=\"toggleActuator(4, 'retract')\" /> Chowaj</label><br>"
                        "<label><input type=\"radio\" name=\"actuator_4\" onclick=\"toggleActuator(4, 'stop')\" checked /> Stop</label><br>"
                        "<script>"
                        "function toggleActuator(id, action) {"
                        "    fetch('/' + action + '_' + id);"
                        "}"
                        "</script></body></html>";


static const char *TAG = "wifi softAP";

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d", MAC2STR(event->mac), event->aid, event->reason);
    }
}

void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

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

    ledc_channel_config_t channels[] = {
        {.gpio_num = MOTOR_IN1_GPIO, .speed_mode = PWM_MODE, .channel = PWM_CHANNEL_IN1, .intr_type = LEDC_INTR_DISABLE, .timer_sel = PWM_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = MOTOR_IN2_GPIO, .speed_mode = PWM_MODE, .channel = PWM_CHANNEL_IN2, .intr_type = LEDC_INTR_DISABLE, .timer_sel = PWM_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = MOTOR_IN3_GPIO, .speed_mode = PWM_MODE, .channel = PWM_CHANNEL_IN3, .intr_type = LEDC_INTR_DISABLE, .timer_sel = PWM_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = MOTOR_IN4_GPIO, .speed_mode = PWM_MODE, .channel = PWM_CHANNEL_IN4, .intr_type = LEDC_INTR_DISABLE, .timer_sel = PWM_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = MOTOR_IN5_GPIO, .speed_mode = PWM_MODE, .channel = PWM_CHANNEL_IN5, .intr_type = LEDC_INTR_DISABLE, .timer_sel = PWM_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = MOTOR_IN6_GPIO, .speed_mode = PWM_MODE, .channel = PWM_CHANNEL_IN6, .intr_type = LEDC_INTR_DISABLE, .timer_sel = PWM_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = MOTOR_IN7_GPIO, .speed_mode = PWM_MODE, .channel = PWM_CHANNEL_IN7, .intr_type = LEDC_INTR_DISABLE, .timer_sel = PWM_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = MOTOR_IN8_GPIO, .speed_mode = PWM_MODE, .channel = PWM_CHANNEL_IN8, .intr_type = LEDC_INTR_DISABLE, .timer_sel = PWM_TIMER, .duty = 0, .hpoint = 0}
    };

    for (int i = 0; i < 8; i++) {
        ESP_ERROR_CHECK(ledc_channel_config(&channels[i]));
    }

    ESP_LOGI(TAG, "PWM skonfigurowane pomyślnie");
}

esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t extend_get_handler(httpd_req_t *req) {
    int actuator_id = req->uri[8] - '0';
    ESP_LOGI(TAG, "Wysuwanie Siłownika %d", actuator_id);

    ledc_set_duty(PWM_MODE, PWM_CHANNEL_IN1 + (actuator_id - 1) * 2, PWM_DUTY);
    ledc_set_duty(PWM_MODE, PWM_CHANNEL_IN2 + (actuator_id - 1) * 2, 0);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_IN1 + (actuator_id - 1) * 2);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_IN2 + (actuator_id - 1) * 2);

    httpd_resp_send(req, "Siłownik wysunięty", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t retract_get_handler(httpd_req_t *req) {
    int actuator_id = req->uri[strlen(req->uri) - 1] - '0';
    ESP_LOGI(TAG, "Chowanie Siłownika %d", actuator_id);

    ledc_set_duty(PWM_MODE, PWM_CHANNEL_IN1 + (actuator_id - 1) * 2, 0);
    ledc_set_duty(PWM_MODE, PWM_CHANNEL_IN2 + (actuator_id - 1) * 2, PWM_DUTY);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_IN1 + (actuator_id - 1) * 2);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_IN2 + (actuator_id - 1) * 2);

    httpd_resp_send(req, "Siłownik chowany", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t stop_get_handler(httpd_req_t *req) {
    int actuator_id = req->uri[strlen(req->uri) - 1] - '0';
    ESP_LOGI(TAG, "Zatrzymanie Siłownika %d", actuator_id);

    ledc_set_duty(PWM_MODE, PWM_CHANNEL_IN1 + (actuator_id - 1) * 2, 0);
    ledc_set_duty(PWM_MODE, PWM_CHANNEL_IN2 + (actuator_id - 1) * 2, 0);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_IN1 + (actuator_id - 1) * 2);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_IN2 + (actuator_id - 1) * 2);

    httpd_resp_send(req, "Siłownik zatrzymany", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler
        };
        httpd_register_uri_handler(server, &root_uri);

        for (int i = 1; i <= 4; i++) {
            char extend_uri[20], retract_uri[20], stop_uri[20];

            snprintf(extend_uri, sizeof(extend_uri), "/extend_%d", i);
            httpd_uri_t extend = {
                .uri       = extend_uri,
                .method    = HTTP_GET,
                .handler   = extend_get_handler
            };
            httpd_register_uri_handler(server, &extend);

            snprintf(retract_uri, sizeof(retract_uri), "/retract_%d", i);
            httpd_uri_t retract = {
                .uri       = retract_uri,
                .method    = HTTP_GET,
                .handler   = retract_get_handler
            };
            httpd_register_uri_handler(server, &retract);

            snprintf(stop_uri, sizeof(stop_uri), "/stop_%d", i);
            httpd_uri_t stop = {
                .uri       = stop_uri,
                .method    = HTTP_GET,
                .handler   = stop_get_handler
            };
            httpd_register_uri_handler(server, &stop);
        }
    }
    return server;
}


void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");

    wifi_init_softap();

    pwm_init();

    start_webserver();
}













