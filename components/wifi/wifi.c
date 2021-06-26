#include "wifi.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "rv3.wifi";

const int WIFI_CONNECTED_BIT = BIT0;
static EventGroupHandle_t wifi_event_group;
static ip4_addr_t ip;
static void (*wifi_scan_done_cb)(void *);
static void *wifi_scan_done_cb_arg;
static int is_wifi_init_done;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	switch(event->event_id) {
	case SYSTEM_EVENT_STA_START:
		ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
		break;
	case SYSTEM_EVENT_SCAN_DONE:
		ESP_LOGI(TAG, "SYSTEM_EVENT_SCAN_DONE");
		wifi_scan_done_cb(wifi_scan_done_cb_arg);
		break;
	case SYSTEM_EVENT_STA_GOT_IP:
		ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP: %s",
			 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
		ip = event->event_info.got_ip.ip_info.ip;
		xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
		xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
		esp_wifi_connect();
		break;
	default:
		ESP_LOGI(TAG, "unsupported event_handler %d\n", event->event_id);
		break;
	}

	return ESP_OK;
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

	wifi_event_group = xEventGroupCreate();
	tcpip_adapter_init();
	ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
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
	ESP_ERROR_CHECK( esp_wifi_disconnect() );
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
	return xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT;
}

char *wifi_get_ip()
{
	if (!wifi_is_connected())
		return NULL;

	return ip4addr_ntoa(&ip);
}
