// examples/project_tcp/esp32/main/wifi.c
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"
#include "nvs_flash.h"

#define WIFI_SSID "connecting....."
#define WIFI_PASS "your_pass"
#define STATIC_IP "192.168.1.10"
#define STATIC_GW "192.168.1.1"
#define STATIC_NETMASK "255.255.255.0"

static const char* TAG = "WIFI";
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                               void* event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "disconnected, reconnecting...");
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
    ESP_LOGI(TAG, "connected to AP, obtaining IP via DHCP...");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    uint32_t ip = event->ip_info.ip.addr;
    ESP_LOGI(TAG, "got ip:%u.%u.%u.%u", (unsigned int)(ip & 0xFF), (unsigned int)((ip >> 8) & 0xFF),
             (unsigned int)((ip >> 16) & 0xFF), (unsigned int)((ip >> 24) & 0xFF));
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

bool wifi_init(void) {
  esp_err_t err;

  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_netif_t* netif = esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START,
                                                      &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                      &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                                      &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                      &wifi_event_handler, NULL, NULL));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASS,
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  // Switched from static IP to DHCP for dynamic network compatibility
  /*
  ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
  esp_netif_ip_info_t ip_info;
  memset(&ip_info, 0, sizeof(ip_info));
  ip4addr_aton(STATIC_IP, (ip4_addr_t *)&ip_info.ip);
  ip4addr_aton(STATIC_GW, (ip4_addr_t *)&ip_info.gw);
  ip4addr_aton(STATIC_NETMASK, (ip4_addr_t *)&ip_info.netmask);
  ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
  */

  ESP_ERROR_CHECK(esp_wifi_start());

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                                         pdMS_TO_TICKS(15000));

  if (!(bits & WIFI_CONNECTED_BIT)) {
    ESP_LOGE(TAG, "Timed out waiting for Wi-Fi connection");
    return false;
  }
  return true;
}
