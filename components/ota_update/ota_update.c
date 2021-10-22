#include "ota_update.h"

#include <string.h>

#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "downloader.h"

static const char* TAG = "rv3.ota_update";

#define FW_UPDATE_URL_DEV	"https://github.com/tecul/radiov3_sw/releases/download/latest/radiov3.bin"
#define FW_UPDATE_URL_RELEASE	"https://github.com/tecul/radiov3_sw/releases/latest/download/radiov3.bin"

#define FW_UPDATE_URL		FW_UPDATE_URL_DEV

#define MIN(a,b)	((a)<(b)?(a):(b))
#define OTA_HDR_LEN	((int)(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)))

struct fw_available_ctx {
	char hdr[OTA_HDR_LEN];
	int pos;
};

struct fw_update_ctx {
	esp_ota_handle_t update_handle;
	int binary_file_length;
	int has_error;
};

static int fw_available_cb(void *data, int data_len, void *cb_ctx)
{
	struct fw_available_ctx *ctx = cb_ctx;
	int len;

	len = MIN(data_len, OTA_HDR_LEN - ctx->pos);
	if (!len)
		return 0;

	memcpy(&ctx->hdr[ctx->pos], data, len);
	ctx->pos += len;

	return ctx->pos == OTA_HDR_LEN ? 1 : 0;
}

static bool is_new_firmware_available()
{
	const esp_partition_t *running_partition;
	esp_app_desc_t running_app_info;
	struct fw_available_ctx ctx;
	esp_app_desc_t new_app_info;
	int ret;

	ESP_LOGI(TAG, "Checking if new firmware available");

	running_partition = esp_ota_get_running_partition();
	if (!running_partition) {
		ESP_LOGE(TAG, "Unable to retrieve running partition");
		goto error;
	}
	ret = esp_ota_get_partition_description(running_partition, &running_app_info);
	if (ret) {
		ESP_LOGE(TAG, "Unable to get running partition info");
		goto error;
	}
	ESP_LOGI(TAG, "running version %s", running_app_info.version);

	ctx.pos = 0;
	ret = downloader_generic(FW_UPDATE_URL, fw_available_cb, &ctx);
	if (ret) {
		ESP_LOGE(TAG, "unable to download header for %s", FW_UPDATE_URL);
		goto error;
	}

	if (ctx.pos != OTA_HDR_LEN) {
		ESP_LOGE(TAG, "header too short %d / %d", ctx.pos, OTA_HDR_LEN);
		goto error;
	}

	memcpy(&new_app_info, &ctx.hdr[OTA_HDR_LEN - sizeof(esp_app_desc_t)], sizeof(esp_app_desc_t));
	ESP_LOGI(TAG, "new version %s", new_app_info.version);

	return memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version));

error:
	return false;

}

static int fw_update_cb(void *data, int data_len, void *cb_ctx)
{
	struct fw_update_ctx *ctx = cb_ctx;
	int ret;

	ctx->binary_file_length += data_len;
	ret = esp_ota_write(ctx->update_handle, data, data_len);
	if (!ret)
		return 0;

	ctx->has_error = 1;
	return ret;
}

static void ota_apply_update()
{
	const esp_partition_t *update_partition;
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

	ctx.update_handle = update_handle;
	ret = downloader_generic(FW_UPDATE_URL, fw_update_cb, &ctx);
	if (ret) {
		ESP_LOGE(TAG, "unable to download header for %s", FW_UPDATE_URL);
		goto downloader_generic_error;
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

	ESP_LOGI(TAG, "Reboot to enjoy new version!");

	return ;

esp_ota_end_error:
has_error_error:
downloader_generic_error:
	esp_ota_abort(update_handle);
esp_ota_begin_error:
esp_ota_get_next_update_partition_error:
	return ;

esp_ota_set_boot_partition_error:
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
