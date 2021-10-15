#include "wifi.h"

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

static const char *TAG = "rv3.wifi";

static esp_ip4_addr_t ip;
static char ip_addr[32];
static void (*wifi_scan_done_cb)(void *);
static void *wifi_scan_done_cb_arg;
static int is_wifi_init_done;
static bool is_connected;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
				int32_t event_id, void* event_data)
{
	switch (event_id) {
	case WIFI_EVENT_STA_START:
		ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
		break;
	case WIFI_EVENT_SCAN_DONE:
		ESP_LOGI(TAG, "SYSTEM_EVENT_SCAN_DONE");
		wifi_scan_done_cb(wifi_scan_done_cb_arg);
		break;
	case WIFI_EVENT_STA_DISCONNECTED:
		ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
		is_connected = false;
		esp_wifi_connect();
		break;
	default:
		ESP_LOGI(TAG, "unsupported wifi event %d\n", event_id);
		break;
	}
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
			     int32_t event_id, void* event_data)
{
	ip_event_got_ip_t *got_ip = event_data;

	switch (event_id) {
	case IP_EVENT_STA_GOT_IP:
		ip = got_ip->ip_info.ip;
		esp_ip4addr_ntoa(&ip, ip_addr, sizeof(ip_addr));
		is_connected = true;
		ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP %s", ip_addr);
		break;
	default:
		ESP_LOGI(TAG, "unsupported ip event %d\n", event_id);
		break;
	}
}

static esp_err_t read_wifi_credentials(char *ssid, char *password)
{
	nvs_handle hdl;
	esp_err_t ret;
	size_t length;

	ret = nvs_open(TAG, NVS_READONLY, &hdl);
	if (ret) {
		ESP_LOGI(TAG, "Unable to open %s nvm space %s\n", TAG,
			 esp_err_to_name(ret));
		return ret;
	}

	length = 32;
	ret = nvs_get_str(hdl, "ssid", ssid, &length);
	if (ret) {
		ESP_LOGI(TAG, "Unable to read ssid %s\n", esp_err_to_name(ret));
		goto cleanup;
	}

	length = 64;
	ret = nvs_get_str(hdl, "password", password, &length);
	if (ret) {
		ESP_LOGI(TAG, "Unable to read password %s\n", esp_err_to_name(ret));
		goto cleanup;
	}

cleanup:
	nvs_close(hdl);

	return ret;
}

void wifi_init()
{
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

	if (is_wifi_init_done)
		return ;

	esp_log_level_set("wifi", ESP_LOG_INFO);

	esp_netif_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
							    wifi_event_handler, NULL, NULL));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
							    ip_event_handler, NULL, NULL));
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_start() );
	is_wifi_init_done = 1;
}

int wifi_connect_start()
{
	wifi_config_t wifi_config = { 0 };
	char password[64];
	char ssid[32];
	esp_err_t ret;

	ret = read_wifi_credentials(ssid, password);
	if (ret) {
		ESP_LOGW(TAG, "unable to get wifi credentials\n");
		return ret;
	}

	strncpy((char *) wifi_config.sta.ssid, ssid, 32);
	strncpy((char *) wifi_config.sta.password, password, 64);
	wifi_config.sta.ssid[31] = '\0';
	wifi_config.sta.password[63] = '\0';

	ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
	if (ret)
		return ret;
	ret = esp_wifi_connect();
	if (ret)
		return ret;
	ESP_LOGI(TAG, "wifi connect start");

	return ESP_OK;
}

void wifi_scan_start(void (*_wifi_scan_done_cb)(void *), void *arg)
{
	wifi_scan_config_t scan_config = { 0 };

	wifi_scan_done_cb = _wifi_scan_done_cb;
	wifi_scan_done_cb_arg = arg;
	//ESP_ERROR_CHECK( esp_wifi_disconnect() );
	ESP_ERROR_CHECK( esp_wifi_scan_start(&scan_config, false) );
}

int wifi_set_credentials(char *ssid, char *pwd)
{
	nvs_handle hdl;
	esp_err_t ret;

	ret = nvs_open(TAG, NVS_READWRITE, &hdl);
	if (ret) {
		ESP_LOGI(TAG, "Unable to open %s nvm space %s\n", TAG,
			 esp_err_to_name(ret));
		return ret;
	}

	ret = nvs_set_str(hdl, "ssid", ssid);
	if (ret) {
		ESP_LOGI(TAG, "Unable to write ssid %s\n", esp_err_to_name(ret));
		goto cleanup;
	}

	ret = nvs_set_str(hdl, "password", pwd);
	if (ret) {
		ESP_LOGI(TAG, "Unable to write password %s\n", esp_err_to_name(ret));
		goto cleanup;
	}

cleanup:
	nvs_commit(hdl);
	nvs_close(hdl);

	return ret;
}

int wifi_is_connected()
{
	return is_connected;
}

char *wifi_get_ip()
{
	if (!wifi_is_connected())
		return NULL;

	return ip_addr;
}
