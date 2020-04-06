#ifndef __STB350__
#define __STB350__ 1

#include "driver/i2c.h"
#include "driver/i2s.h"
#include "driver/gpio.h"

void *stb350_create(i2c_port_t i2c_num, i2s_port_t i2s_num, gpio_num_t reset_n_gpio);
void stb350_destroy(void *hdl);
int stb350_init(void *hdl);
int stb350_start(void *hdl);
int stb350_stop(void *hdl);
int stb350_set_volume(void *hdl, uint8_t volume);
int stb350_write(void *hdl, const void *src, size_t size, size_t *bytes_written, TickType_t ticks_to_wait);

#endif