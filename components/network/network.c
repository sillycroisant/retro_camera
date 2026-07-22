#include "network.h"

#include "string.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "nvs_flash.h"
#include "esp_wifi.h"

static const char *TAG = "[Network]";

static esp_err_t network_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }

    return ret;
}

static esp_err_t network_init_stack(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    return ESP_OK;
}

static esp_err_t network_init_wifi_driver(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    return ESP_OK;
}

static esp_err_t network_configure_ap(
    const network_config_t *cfg) {
    
    wifi_config_t wifi_config = {0};

    strncpy(
        (char *)wifi_config.ap.ssid,
        cfg->ssid,
        sizeof(wifi_config.ap.ssid));

    wifi_config.ap.ssid_len = strlen(cfg->ssid);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.channel = 1;

    if(strlen(cfg->password) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    return ESP_OK;
}

static esp_err_t network_start_wifi(void)
{
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

esp_err_t network_init(const network_config_t *cfg)
{
    if (cfg == NULL){
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing network...");

    ESP_ERROR_CHECK(network_init_nvs());
    ESP_ERROR_CHECK(network_init_stack());
    ESP_ERROR_CHECK(network_init_wifi_driver());
    ESP_ERROR_CHECK(network_configure_ap(cfg));
    ESP_ERROR_CHECK(network_start_wifi());


    ESP_LOGI(TAG, "==================================");
    ESP_LOGI(TAG, " WiFi Access Point Started");
    ESP_LOGI(TAG, " SSID     : %s", cfg->ssid);
    ESP_LOGI(TAG, " Password : %s", cfg->password);
    ESP_LOGI(TAG, " IP       : 192.168.4.1");
    ESP_LOGI(TAG, "==================================");

    return ESP_OK;
}

