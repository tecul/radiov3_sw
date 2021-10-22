#include "ota_update.h"

#include <string.h>

#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "rv3.ota_update";

#define FW_UPDATE_URL_DEV	"https://github.com/tecul/radiov3_sw/releases/download/latest/radiov3.bin"
#define FW_UPDATE_URL_RELEASE	"https://github.com/tecul/radiov3_sw/releases/latest/download/radiov3.bin"

#define FW_UPDATE_URL		FW_UPDATE_URL_DEV

#define MIN(a,b)	((a)<(b)?(a):(b))
#define OTA_HDR_LEN	((int)(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)))

const char* root_ca = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDxTCCAq2gAwIBAgIQAqxcJmoLQJuPC3nyrkYldzANBgkqhkiG9w0BAQUFADBs\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSswKQYDVQQDEyJEaWdpQ2VydCBIaWdoIEFzc3VyYW5j\n" \
"ZSBFViBSb290IENBMB4XDTA2MTExMDAwMDAwMFoXDTMxMTExMDAwMDAwMFowbDEL\n" \
"MAkGA1UEBhMCVVMxFTATBgNVBAoTDERpZ2lDZXJ0IEluYzEZMBcGA1UECxMQd3d3\n" \
"LmRpZ2ljZXJ0LmNvbTErMCkGA1UEAxMiRGlnaUNlcnQgSGlnaCBBc3N1cmFuY2Ug\n" \
"RVYgUm9vdCBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMbM5XPm\n" \
"+9S75S0tMqbf5YE/yc0lSbZxKsPVlDRnogocsF9ppkCxxLeyj9CYpKlBWTrT3JTW\n" \
"PNt0OKRKzE0lgvdKpVMSOO7zSW1xkX5jtqumX8OkhPhPYlG++MXs2ziS4wblCJEM\n" \
"xChBVfvLWokVfnHoNb9Ncgk9vjo4UFt3MRuNs8ckRZqnrG0AFFoEt7oT61EKmEFB\n" \
"Ik5lYYeBQVCmeVyJ3hlKV9Uu5l0cUyx+mM0aBhakaHPQNAQTXKFx01p8VdteZOE3\n" \
"hzBWBOURtCmAEvF5OYiiAhF8J2a3iLd48soKqDirCmTCv2ZdlYTBoSUeh10aUAsg\n" \
"EsxBu24LUTi4S8sCAwEAAaNjMGEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB/wQF\n" \
"MAMBAf8wHQYDVR0OBBYEFLE+w2kD+L9HAdSYJhoIAu9jZCvDMB8GA1UdIwQYMBaA\n" \
"FLE+w2kD+L9HAdSYJhoIAu9jZCvDMA0GCSqGSIb3DQEBBQUAA4IBAQAcGgaX3Nec\n" \
"nzyIZgYIVyHbIUf4KmeqvxgydkAQV8GK83rZEWWONfqe/EW1ntlMMUu4kehDLI6z\n" \
"eM7b41N5cdblIZQB2lWHmiRk9opmzN6cN82oNLFpmyPInngiK3BD41VHMWEZ71jF\n" \
"hS9OMPagMRYjyOfiZRYzy78aG6A9+MpeizGLYAiJLQwGXFK3xPkKmNEVX58Svnw2\n" \
"Yzi9RKR/5CYrCsSXaQ3pjOLAEFe4yHYSkVXySGnYvCoCWw9E1CAx2/S6cCZdkGCe\n" \
"vEsXCS+0yx5DaMkHJ8HSXPfqIbloEpw8nL+e/IBcm2PN7EeqJSdnoDfzAIJ9VNep\n" \
"+OkuE6N36B9K\n" \
"-----END CERTIFICATE-----\n";

struct fw_available_ctx {
	char hdr[OTA_HDR_LEN];
	int pos;
};

struct fw_update_ctx {
	esp_ota_handle_t update_handle;
	int binary_file_length;
	int has_error;
};

static esp_err_t fw_available_handle(esp_http_client_event_t *evt)
{
	struct fw_available_ctx *ctx = evt->user_data;
	int len;

	switch (evt->event_id) {
	case HTTP_EVENT_ERROR:
		ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
		break;
	case HTTP_EVENT_ON_CONNECTED:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
		break;
	case HTTP_EVENT_HEADERS_SENT:
		ESP_LOGD(TAG, "HTTP_EVENT_HEADERS_SENT");
		break;
	case HTTP_EVENT_ON_HEADER:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER %s: %s", evt->header_key,
			 evt->header_value);
		break;
	case HTTP_EVENT_ON_DATA:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA %d %p: %d", esp_http_client_get_status_code(evt->client), evt->data,
			 evt->data_len);
		if (esp_http_client_get_status_code(evt->client) != 200)
			break;
		len = MIN(evt->data_len, OTA_HDR_LEN - ctx->pos);
		if (!len)
			break;
		memcpy(&ctx->hdr[ctx->pos], evt->data, len);
		ctx->pos += len;
		if (ctx->pos == OTA_HDR_LEN)
			esp_http_client_close(evt->client);
		break;
	case HTTP_EVENT_ON_FINISH:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
		break;
	case HTTP_EVENT_DISCONNECTED:
		ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
		break;
	}

	return ESP_OK;
}

static bool is_new_firmware_available()
{
	const esp_partition_t *running_partition;
	esp_http_client_config_t config = {0};
	esp_http_client_handle_t client;
	esp_app_desc_t running_app_info;
	struct fw_available_ctx ctx;
	esp_app_desc_t new_app_info;
	int ret;

	ESP_LOGI(TAG, "Checking if new firmware available");

	running_partition = esp_ota_get_running_partition();
	if (!running_partition) {
		ESP_LOGE(TAG, "Unable to retrieve running partition");
		goto esp_ota_get_running_partition_error;
	}
	ret = esp_ota_get_partition_description(running_partition, &running_app_info);
	if (ret) {
		ESP_LOGE(TAG, "Unable to get running partition info");
		goto esp_ota_get_partition_description_error;
	}
	ESP_LOGI(TAG, "running version %s", running_app_info.version);

	config.url = FW_UPDATE_URL;
	config.cert_pem = root_ca;
	config.event_handler = fw_available_handle;
	config.buffer_size = 4096;
	config.buffer_size_tx = 4096;
	config.user_data = &ctx;
	ctx.pos = 0;
	client = esp_http_client_init(&config);
	if (!client) {
		ESP_LOGE(TAG, "unable to init http for %s", FW_UPDATE_URL);
		goto esp_http_client_init_error;
	}

	ret = esp_http_client_perform(client);
	if (ret) {
		ESP_LOGE(TAG, "unable to perform http for %s", FW_UPDATE_URL);
		goto esp_http_client_perform_error;
	}

	if (ctx.pos != OTA_HDR_LEN) {
		ESP_LOGE(TAG, "header too short %d / %d", ctx.pos, OTA_HDR_LEN);
		goto esp_http_client_perform_error;
	}

	memcpy(&new_app_info, &ctx.hdr[OTA_HDR_LEN - sizeof(esp_app_desc_t)], sizeof(esp_app_desc_t));
	ESP_LOGI(TAG, "new version %s", new_app_info.version);

	esp_http_client_cleanup(client);

	return memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version));

esp_http_client_perform_error:
	esp_http_client_cleanup(client);
esp_http_client_init_error:
esp_ota_get_partition_description_error:
esp_ota_get_running_partition_error:
	return false;
}

static esp_err_t fw_update_handle(esp_http_client_event_t *evt)
{
	struct fw_update_ctx *ctx = evt->user_data;
	int ret;

	switch (evt->event_id) {
	case HTTP_EVENT_ERROR:
		ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
		break;
	case HTTP_EVENT_ON_CONNECTED:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
		break;
	case HTTP_EVENT_HEADERS_SENT:
		ESP_LOGD(TAG, "HTTP_EVENT_HEADERS_SENT");
		break;
	case HTTP_EVENT_ON_HEADER:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER %s: %s", evt->header_key,
			 evt->header_value);
		break;
	case HTTP_EVENT_ON_DATA:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA %d %p: %d", esp_http_client_get_status_code(evt->client), evt->data,
			 evt->data_len);
		if (esp_http_client_get_status_code(evt->client) != 200)
			break;
		ctx->binary_file_length += evt->data_len;
		ret = esp_ota_write(ctx->update_handle, evt->data, evt->data_len);
		if (ret) {
			ctx->has_error = 1;
			esp_http_client_close(evt->client);
		}
		break;
	case HTTP_EVENT_ON_FINISH:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
		break;
	case HTTP_EVENT_DISCONNECTED:
		ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
		break;
	}

	return ESP_OK;
}

static void ota_apply_update()
{
	const esp_partition_t *update_partition;
	esp_http_client_config_t config = {0};
	esp_http_client_handle_t client;
	esp_ota_handle_t update_handle;
	struct fw_update_ctx ctx = {0};
	int ret;

	update_partition = esp_ota_get_next_update_partition(NULL);
	if (!update_partition) {
		ESP_LOGE(TAG, "Unable to retrieve update partition");
		goto esp_ota_get_next_update_partition_error;
	}

	ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
	if (ret) {
		ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(ret));
		goto esp_ota_begin_error;
	}

	config.url = FW_UPDATE_URL;
	config.cert_pem = root_ca;
	config.event_handler = fw_update_handle;
	config.buffer_size = 4096;
	config.buffer_size_tx = 4096;
	config.user_data = &ctx;
	ctx.update_handle = update_handle;
	client = esp_http_client_init(&config);
	if (!client) {
		ESP_LOGE(TAG, "unable to init http for %s", FW_UPDATE_URL);
		goto esp_http_client_init_error;
	}

	ret = esp_http_client_perform(client);
	if (ret) {
		ESP_LOGE(TAG, "unable to perform http for %s", FW_UPDATE_URL);
		goto esp_http_client_perform_error;
	}

	if (ctx.has_error) {
		ESP_LOGE(TAG, "unable to fetch all data for %s", FW_UPDATE_URL);
		goto has_error_error;
	}
	ESP_LOGI(TAG, "Total Write binary data length : %d", ctx.binary_file_length);

	ret = esp_ota_end(update_handle);
	if (ret) {
		ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(ret));
		goto esp_ota_end_error;
	}

	ESP_LOGI(TAG, "set new boot partition\n");
	ret = esp_ota_set_boot_partition(update_partition);
	if (ret) {
		ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(ret));
		goto esp_ota_set_boot_partition_error;
	}

	esp_http_client_close(client);
	esp_http_client_cleanup(client);

	ESP_LOGI(TAG, "Reboot to enjoy new version!");

	return ;

esp_ota_set_boot_partition_error:
esp_ota_end_error:
	esp_ota_abort(update_handle);
has_error_error:
esp_http_client_perform_error:
	esp_http_client_cleanup(client);
esp_http_client_init_error:
	esp_ota_abort(update_handle);
esp_ota_begin_error:
esp_ota_get_next_update_partition_error:
	return ;
}

static void ota_update_task(void *arg)
{
	lv_obj_t *mbox = arg;

	vTaskDelay(500 / portTICK_PERIOD_MS);
	if (is_new_firmware_available()) {
		ESP_LOGI(TAG, "start update");
		lv_msgbox_set_text(mbox, "Updating ...");
		ota_apply_update();
		lv_msgbox_set_text(mbox, "Will reboot in 3 seconds ...");
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		lv_msgbox_set_text(mbox, "Will reboot in 2 seconds ...");
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		lv_msgbox_set_text(mbox, "Will reboot in 1 seconds ...");
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		lv_msgbox_set_text(mbox, "Will reboot in 0 seconds ...");
		esp_restart();
	}
	lv_msgbox_set_text(mbox, "Fw is up to date");
	vTaskDelay(1000 / portTICK_PERIOD_MS);
	lv_msgbox_start_auto_close(mbox, 0);
	vTaskDelete(NULL);
}

int ota_update_start(lv_obj_t *mbox)
{
	BaseType_t res;

	res = xTaskCreatePinnedToCore(ota_update_task, "ota_update", 3 * 4096, mbox,
			tskIDLE_PRIORITY + 1, NULL, tskNO_AFFINITY);
	assert(res == pdPASS);

	return 0;
}
