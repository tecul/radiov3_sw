#include <stdio.h>
#include <assert.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "soc/io_mux_reg.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "lvgl.h"
#include "lvgl_helpers.h"

#include "freertos/FreeRTOS.h"
#include "esp_freertos_hooks.h"
#include "freertos/task.h"

#include "sdcard.h"
#include "wifi.h"
#include "bluetooth.h"
#include "audio.h"
#include "main_menu.h"
#include "calibration.h"
#include "theme.h"
#include "system.h"

static const char* TAG = "rv3";

static lv_color_t buf1[DISP_BUF_SIZE];
static lv_color_t buf2[DISP_BUF_SIZE];
static lv_disp_buf_t disp_buf;

static void IRAM_ATTR lv_tick_task(void)
{
	lv_tick_inc(portTICK_RATE_MS);
}

void app_main(void)
{
	lv_indev_drv_t indev_drv;
	lv_disp_drv_t disp_drv;
	ui_hdl main_menu_handle;
	int ret;

	ESP_LOGI(TAG, "Starting");
	/* select gpio instead of jtag default config */
	PIN_FUNC_SELECT(IO_MUX_GPIO12_REG, PIN_FUNC_GPIO);
	PIN_FUNC_SELECT(IO_MUX_GPIO13_REG, PIN_FUNC_GPIO);

	ESP_LOGI(TAG, "nvs init");
	/* init nvs */
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}

	ESP_LOGI(TAG, "setup sdcard");
	sdcard_init();

	ESP_LOGI(TAG, "setup wifi");
	wifi_init();
	wifi_connect_start();

	ESP_LOGI(TAG, "setup bluetooth");
	ret = bluetooth_init(system_get_name());
	if (ret)
		ESP_LOGE(TAG, "unable to init bluetooth");

	/* setup audio stack */
	ESP_LOGI(TAG, "setup audio");
	audio_init();

	ESP_LOGI(TAG, "display init");
	/* display stack init */
	lv_init();
	lvgl_driver_init();

	lv_disp_buf_init(&disp_buf, buf1, buf2, DISP_BUF_SIZE);
	lv_disp_drv_init(&disp_drv);
	disp_drv.flush_cb = ili9341_flush;
	disp_drv.buffer = &disp_buf;
	lv_disp_drv_register(&disp_drv);

	ESP_LOGI(TAG, "ts init");
	lv_indev_drv_init(&indev_drv);
	indev_drv.read_cb = xpt2046_read;
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	lv_indev_drv_register(&indev_drv);

	esp_register_freertos_tick_hook(lv_tick_task);

	ESP_LOGI(TAG, "theme init");
	theme_init();

	ESP_LOGI(TAG, "screen init");
	/* setup init screen */
	main_menu_handle = main_menu_create();
	assert(main_menu_handle);

	/* calibrate ts if needed */
	ESP_LOGI(TAG, "ts calibration setup");
	calibration_setup(&indev_drv);

	ESP_LOGI(TAG, "main loop");
	/* main loop */
	while(1) {
		vTaskDelay(1);
		lv_task_handler();
	}
}
