#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "driver/gpio.h"

#include "lvgl/lvgl.h"
#include "disp_spi.h"
#include "ili9341.h"
#include "tp_spi.h"
#include "xpt2046.h"

#include "calibration.h"
#include "main_menu.h"

static const char* TAG = "rv3";

static lv_color_t buf1[DISP_BUF_SIZE];
static lv_color_t buf2[DISP_BUF_SIZE];
static lv_disp_buf_t disp_buf;

static void IRAM_ATTR lv_tick_task(void)
{
	lv_tick_inc(portTICK_RATE_MS);
}

void app_main()
{
	lv_disp_drv_t disp_drv;
	lv_indev_drv_t indev_drv;
	void *main_menu_handle;
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
	ESP_ERROR_CHECK(ret);

	ESP_LOGI(TAG, "display init");
	/* display stack init */
	lv_init();
	disp_spi_init();
	ili9341_init();

	lv_disp_buf_init(&disp_buf, buf1, buf2, DISP_BUF_SIZE);
	lv_disp_drv_init(&disp_drv);
	disp_drv.flush_cb = ili9341_flush;
	disp_drv.buffer = &disp_buf;
	lv_disp_drv_register(&disp_drv);

	ESP_LOGI(TAG, "ts init");
	tp_spi_init();
	xpt2046_init();
	lv_indev_drv_init(&indev_drv);
	indev_drv.read_cb = xpt2046_read;
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	lv_indev_drv_register(&indev_drv);

	esp_register_freertos_tick_hook(lv_tick_task);

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
