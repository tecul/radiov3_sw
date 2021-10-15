#include "ota_update.h"

#include <string.h>

#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "rv3.ota_update";

#define FW_UPDATE_URL		"http://192.168.1.8/radio/radiov3.bin"

static bool is_new_firmware_available()
{
	const int header_size = sizeof(esp_image_header_t) +
				sizeof(esp_image_segment_header_t) +
				sizeof(esp_app_desc_t);
	const esp_partition_t *running_partition;
	esp_http_client_config_t config = {0};
	esp_http_client_handle_t client;
	esp_app_desc_t running_app_info;
	esp_app_desc_t new_app_info;
	int data_read;
	char *buffer;
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
	client = esp_http_client_init(&config);
	if (!client) {
		ESP_LOGE(TAG, "unable to init http for %s", FW_UPDATE_URL);
		goto esp_http_client_init_error;
	}
	ret = esp_http_client_open(client, 0);
	if (ret) {
		ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(ret));
		goto esp_http_client_open_error;
	}
	esp_http_client_fetch_headers(client);
	buffer = malloc(header_size);
	if (!buffer) {
		ESP_LOGE(TAG, "unable to allocate memory for data buffer");
		goto malloc_error;
	}
	data_read = esp_http_client_read(client, buffer, header_size);
	if (data_read != header_size) {
		ESP_LOGE(TAG, "read %d bytes, need %d bytes", data_read, header_size);
		goto esp_http_client_read_error;
	}
	memcpy(&new_app_info, &buffer[header_size - sizeof(esp_app_desc_t)], sizeof(esp_app_desc_t));
	ESP_LOGI(TAG, "new version %s", new_app_info.version);

	free(buffer);
	esp_http_client_close(client);
	esp_http_client_cleanup(client);

	return memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version));

esp_http_client_read_error:
	free(buffer);
malloc_error:
	esp_http_client_close(client);
esp_http_client_open_error:
	esp_http_client_cleanup(client);
esp_http_client_init_error:
esp_ota_get_partition_description_error:
esp_ota_get_running_partition_error:

	return false;
}

static void ota_apply_update()
{
	const int buffer_size = 1024;
	const esp_partition_t *update_partition;
	esp_http_client_config_t config = {0};
	esp_ota_handle_t update_handle;
	esp_http_client_handle_t client;
	int binary_file_length = 0;
	int data_read;
	char *buffer;
	int ret;

	update_partition = esp_ota_get_next_update_partition(NULL);
	if (!update_partition) {
		ESP_LOGE(TAG, "Unable to retrieve update partition");
		goto esp_ota_get_next_update_partition_error;
	}

	config.url = FW_UPDATE_URL;
	client = esp_http_client_init(&config);
	if (!client) {
		ESP_LOGE(TAG, "unable to init http for %s", FW_UPDATE_URL);
		goto esp_http_client_init_error;
	}
	ret = esp_http_client_open(client, 0);
	if (ret) {
		ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(ret));
		goto esp_http_client_open_error;
	}
	esp_http_client_fetch_headers(client);
	buffer = malloc(buffer_size);
	if (!buffer) {
		ESP_LOGE(TAG, "unable to allocate memory for data buffer");
		goto malloc_error;
	}

	ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
	if (ret) {
		ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(ret));
		goto esp_ota_begin_error;
	}

	while (1) {
		data_read = esp_http_client_read(client, buffer, buffer_size);
		if (data_read < 0) {
			ESP_LOGE(TAG, "failed to get data");
			goto data_read_error;
		}
		if (data_read == 0)
			break;
		ret = esp_ota_write(update_handle, (const void *) buffer, data_read);
		if (ret) {
			ESP_LOGE(TAG, "esp_ota_write failed");
			goto esp_ota_write_error;
		}
		binary_file_length += data_read;
	}
	ESP_LOGI(TAG, "Total Write binary data length : %d", binary_file_length);

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

	free(buffer);
	esp_http_client_close(client);
	esp_http_client_cleanup(client);

	ESP_LOGI(TAG, "Reboot to enjoy new version!");

	return ;

esp_ota_set_boot_partition_error:
esp_ota_end_error:
esp_ota_write_error:
data_read_error:
esp_ota_begin_error:
	free(buffer);
malloc_error:
	esp_http_client_close(client);
esp_http_client_open_error:
	esp_http_client_cleanup(client);
esp_http_client_init_error:
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
