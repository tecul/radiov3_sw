#include "stb350.h"

#define I2C_ADDR					0x38
#define ACK_CHECK_EN				0x1
#define NACK_VAL					0x1

#define DEVICE_CONFF				0x05
#define CONFF_EAPD					(1 << 7)
#define DEVICE_MVOL					0x07
#define DEVICE_STATUS				0x2d

struct stb350 {
	i2c_port_t i2c_num;
	i2s_port_t i2s_num;
	gpio_num_t reset_n_gpio;
};

static struct stb350 *instance;

static int reg_read8(struct stb350 *i, uint8_t addr, uint8_t *data)
{
	i2c_cmd_handle_t cmd;
	int ret;

	cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, I2C_ADDR | I2C_MASTER_WRITE, ACK_CHECK_EN);
	i2c_master_write_byte(cmd, addr, ACK_CHECK_EN);
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, I2C_ADDR | I2C_MASTER_READ, ACK_CHECK_EN);
	i2c_master_read_byte(cmd, data, NACK_VAL);
	i2c_master_stop(cmd);
	ret = i2c_master_cmd_begin(i->i2c_num, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);

	return ret;
}

static int reg_write8(struct stb350 *i, uint8_t addr, uint8_t data)
{
	i2c_cmd_handle_t cmd;
	int ret;

	cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, I2C_ADDR | I2C_MASTER_WRITE, ACK_CHECK_EN);
	i2c_master_write_byte(cmd, addr, ACK_CHECK_EN);
	i2c_master_write_byte(cmd, data, ACK_CHECK_EN);
	i2c_master_stop(cmd);
	ret = i2c_master_cmd_begin(i->i2c_num, cmd, 100 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);

	return ret;
}

static int stb350_enable(struct stb350 *i, bool is_enabled)
{
	uint8_t reg;
	int res;

	res = reg_read8(instance, DEVICE_CONFF, &reg);
	if (res)
		return res;

	if (is_enabled)
		reg |= CONFF_EAPD;
	else
		reg &= ~CONFF_EAPD;

	return reg_write8(instance, DEVICE_CONFF, reg);
}

/* public api */
void *stb350_create(i2c_port_t i2c_num, i2s_port_t i2s_num, gpio_num_t reset_n_gpio)
{
	if (instance)
		return NULL;

	instance = malloc(sizeof(struct stb350));
	assert(instance);

	instance->i2c_num = i2c_num;
	instance->i2s_num = i2s_num;
	instance->reset_n_gpio = reset_n_gpio;

	assert(gpio_reset_pin(reset_n_gpio) == 0);
	assert(gpio_set_direction(reset_n_gpio, GPIO_MODE_OUTPUT) == 0);
	assert(gpio_set_level(reset_n_gpio, 0) == 0);

	return instance;
}

void stb350_destroy(void *hdl)
{
	assert(hdl);
	assert (instance == hdl);

	gpio_reset_pin(instance->reset_n_gpio);

	free(hdl);
	instance = NULL;
}

int stb350_init(void *hdl)
{
	assert(hdl);
	assert (instance == hdl);

	i2s_stop(instance->i2s_num);
	i2s_zero_dma_buffer(instance->i2s_num);

	assert(gpio_set_level(instance->reset_n_gpio, 0) == 0);
	vTaskDelay(50 / portTICK_PERIOD_MS);
	assert(gpio_set_level(instance->reset_n_gpio, 1) == 0);
	vTaskDelay(50 / portTICK_PERIOD_MS);

	return 0;
}

int stb350_start(void *hdl)
{
	i2s_start(instance->i2s_num);
	return stb350_enable(instance, true);
}

int stb350_stop(void *hdl)
{
	int res;

	res = stb350_enable(instance, false);
	i2s_stop(instance->i2s_num);
	i2s_zero_dma_buffer(instance->i2s_num);

	return res;
}

int stb350_set_volume(void *hdl, uint8_t volume)
{
	assert(hdl);
	assert (instance == hdl);

	return reg_write8(instance, DEVICE_MVOL, volume);
}

int stb350_write(void *hdl, const void *src, size_t size, size_t *bytes_written, TickType_t ticks_to_wait)
{
	assert(hdl);
	assert (instance == hdl);

	return i2s_write(instance->i2s_num, src, size, bytes_written, ticks_to_wait);
}

