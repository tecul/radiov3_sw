#include "sdcard.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"

static const char *TAG = "rv3.sdcard";

static int is_sdcard_detected;

void sdcard_init()
{
	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = false,
		.max_files = 3,
	};
	sdmmc_card_t *card;
	esp_err_t ret;

	ESP_LOGI(TAG, "sdcard init");
	host.flags = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_DDR;
	slot_config.width = 1;
	/* we use internal pullups */
	gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);
	gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);

	ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config,
				      &mount_config, &card);
	if (ret) {
		ESP_LOGW(TAG, "No sdcard => %s", esp_err_to_name(ret));
		return ;
	}

	is_sdcard_detected = 1;
	ESP_LOGI(TAG, "sdcard is present");
}

int sdcard_is_present()
{
	return is_sdcard_detected;
}
