#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(as6221_demo, LOG_LEVEL_INF);

#define AS6221_ADDR     0x48
#define REG_TEMP_MSB    0x00

static const struct device *i2c_dev;

/* ---------- your existing logic unchanged ---------- */
static float as6221_read_temp(void)
{
	uint8_t data[2];
	int ret = i2c_burst_read(i2c_dev, AS6221_ADDR, REG_TEMP_MSB, data, 2);

	if (ret < 0) {
		LOG_ERR("I2C read failed (%d)", ret);
		return -1000.0f;
	}

	uint16_t raw = (data[0] << 8) | data[1];
	float temperature = raw / 100.0f;

	LOG_INF("[AS6221] t=%.2f C | raw=%u | uptime=%lld ms",
		temperature, raw, k_uptime_get());

	return temperature;
}

/* ---------- thread wrapper ---------- */
static void as6221_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	LOG_INF("=== AS6221 CUSTOM I2C DEMO START ===");

	i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C0 not ready!");
		return;
	}

	LOG_INF("I2C0 ready, addr=0x48");

	while (1) {
		as6221_read_temp();
		k_msleep(1000);
	}
}

/* thread objects */
#define AS6221_STACK_SIZE 2048
#define AS6221_PRIORITY   5

K_THREAD_STACK_DEFINE(as6221_stack, AS6221_STACK_SIZE);
static struct k_thread as6221_tcb;
static bool started;

void as6221_task_start(void)
{
	if (started) {
		return;
	}
	started = true;

	k_thread_create(&as6221_tcb, as6221_stack, K_THREAD_STACK_SIZEOF(as6221_stack),
			as6221_thread, NULL, NULL, NULL,
			AS6221_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(&as6221_tcb, "as6221_task");
}
