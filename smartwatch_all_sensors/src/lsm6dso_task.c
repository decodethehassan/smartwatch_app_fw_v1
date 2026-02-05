#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(lsm6dso_app, LOG_LEVEL_INF);

/* ========= Your Pins (per your mapping) ========= */
#define GPIO0_NODE DT_NODELABEL(gpio0)

#define LSM6DSO_CS_PIN    4    /* P0.04 -> LSM6DSO_CS (force HIGH for I2C mode) */
#define LSM6DSO_INT2_PIN  5    /* P0.05 -> LSM6DSO_INT2 (optional) */
#define LSM6DSO_INT1_PIN  28   /* P0.28 -> LSM6DSO_INT1 (optional) */

/* ========= LSM6DSO Registers ========= */
#define REG_WHO_AM_I      0x0F
#define WHO_AM_I_VAL      0x6C

#define REG_CTRL1_XL      0x10
#define REG_CTRL2_G       0x11
#define REG_CTRL3_C       0x12

#define REG_OUTX_L_G      0x22  /* burst from here reads gyro then accel (12 bytes) */

#define CTRL1_XL_104HZ_2G     0x40
#define CTRL2_G_104HZ_250DPS  0x40
#define CTRL3_C_BDU_IFINC     0x44

#define ACC_MG_PER_LSB_NUM      61
#define ACC_MG_PER_LSB_DEN      1000

#define GYRO_MDPS_PER_LSB_NUM   875
#define GYRO_MDPS_PER_LSB_DEN   100

static const struct device *i2c1;
static const struct device *gpio0;

/* ========= I2C helpers ========= */
static int reg_read_u8(uint8_t addr, uint8_t reg, uint8_t *val)
{
	return i2c_reg_read_byte(i2c1, addr, reg, val);
}

static int reg_write_u8(uint8_t addr, uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(i2c1, addr, reg, val);
}

static int burst_read(uint8_t addr, uint8_t start_reg, uint8_t *buf, size_t len)
{
	return i2c_burst_read(i2c1, addr, start_reg, buf, len);
}

/* ========= Address detect ========= */
static int detect_addr(uint8_t *found)
{
	uint8_t who = 0;

	if (reg_read_u8(0x6A, REG_WHO_AM_I, &who) == 0 && who == WHO_AM_I_VAL) {
		*found = 0x6A;
		LOG_INF("WHO_AM_I @0x6A = 0x%02X (OK)", who);
		return 0;
	}

	if (reg_read_u8(0x6B, REG_WHO_AM_I, &who) == 0 && who == WHO_AM_I_VAL) {
		*found = 0x6B;
		LOG_INF("WHO_AM_I @0x6B = 0x%02X (OK)", who);
		return 0;
	}

	LOG_ERR("LSM6DSO not found at 0x6A/0x6B");
	return -EIO;
}

/* ========= Convert helpers ========= */
static inline int16_t le16(const uint8_t *p)
{
	return (int16_t)((p[1] << 8) | p[0]);
}

static int32_t accel_raw_to_mg(int16_t raw)
{
	return ((int32_t)raw * ACC_MG_PER_LSB_NUM) / ACC_MG_PER_LSB_DEN;
}

static int32_t gyro_raw_to_mdps(int16_t raw)
{
	return ((int32_t)raw * GYRO_MDPS_PER_LSB_NUM) / GYRO_MDPS_PER_LSB_DEN;
}

/* ========= Thread ========= */
static void lsm6dso_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	uint8_t addr = 0;
	int ret;

	LOG_INF("=== LSM6DSO FULL I2C ACC+GYRO TEST ===");

	gpio0 = DEVICE_DT_GET(GPIO0_NODE);
	i2c1  = DEVICE_DT_GET(DT_NODELABEL(i2c1));

	if (!device_is_ready(gpio0)) {
		LOG_ERR("GPIO0 not ready");
		return;
	}
	if (!device_is_ready(i2c1)) {
		LOG_ERR("I2C1 not ready");
		return;
	}

	/* Force CS HIGH to ensure I2C mode */
	ret = gpio_pin_configure(gpio0, LSM6DSO_CS_PIN, GPIO_OUTPUT_HIGH);
	if (ret) {
		LOG_ERR("CS pin config failed (%d)", ret);
		return;
	}

	/* Optional INT pins */
	(void)gpio_pin_configure(gpio0, LSM6DSO_INT1_PIN, GPIO_INPUT);
	(void)gpio_pin_configure(gpio0, LSM6DSO_INT2_PIN, GPIO_INPUT);

	k_msleep(20);

	ret = detect_addr(&addr);
	if (ret) {
		LOG_ERR("Bring-up failed: cannot detect address");
		return;
	}

	LOG_INF("Using LSM6DSO I2C address = 0x%02X", addr);

	ret = reg_write_u8(addr, REG_CTRL3_C, CTRL3_C_BDU_IFINC);
	if (ret) {
		LOG_ERR("CTRL3_C write failed (%d)", ret);
		return;
	}

	ret = reg_write_u8(addr, REG_CTRL1_XL, CTRL1_XL_104HZ_2G);
	if (ret) {
		LOG_ERR("CTRL1_XL write failed (%d)", ret);
		return;
	}

	ret = reg_write_u8(addr, REG_CTRL2_G, CTRL2_G_104HZ_250DPS);
	if (ret) {
		LOG_ERR("CTRL2_G write failed (%d)", ret);
		return;
	}

	LOG_INF("Configured: XL=104Hz(2g), G=104Hz(250dps), IF_INC+BDU enabled");

	while (1) {
		uint8_t buf[12];

		ret = burst_read(addr, REG_OUTX_L_G, buf, sizeof(buf));
		if (ret) {
			LOG_ERR("Burst read failed (%d)", ret);
			k_sleep(K_MSEC(500));
			continue;
		}

		int16_t gx = le16(&buf[0]);
		int16_t gy = le16(&buf[2]);
		int16_t gz = le16(&buf[4]);

		int16_t ax = le16(&buf[6]);
		int16_t ay = le16(&buf[8]);
		int16_t az = le16(&buf[10]);

		int32_t gx_mdps = gyro_raw_to_mdps(gx);
		int32_t gy_mdps = gyro_raw_to_mdps(gy);
		int32_t gz_mdps = gyro_raw_to_mdps(gz);

		int32_t ax_mg = accel_raw_to_mg(ax);
		int32_t ay_mg = accel_raw_to_mg(ay);
		int32_t az_mg = accel_raw_to_mg(az);

		LOG_INF("[LSM6DSO] G RAW [%6d %6d %6d] mdps [%6ld %6ld %6ld]",
			gx, gy, gz, (long)gx_mdps, (long)gy_mdps, (long)gz_mdps);

		LOG_INF("[LSM6DSO] A RAW [%6d %6d %6d]  mg [%6ld %6ld %6ld]",
			ax, ay, az, (long)ax_mg, (long)ay_mg, (long)az_mg);

		k_sleep(K_MSEC(200));
	}
}

/* thread objects */
#define LSM6DSO_STACK_SIZE 3072
#define LSM6DSO_PRIORITY   5

K_THREAD_STACK_DEFINE(lsm6dso_stack, LSM6DSO_STACK_SIZE);
static struct k_thread lsm6dso_tcb;
static bool started;

void lsm6dso_task_start(void)
{
	if (started) {
		return;
	}
	started = true;

	k_thread_create(&lsm6dso_tcb, lsm6dso_stack, K_THREAD_STACK_SIZEOF(lsm6dso_stack),
			lsm6dso_thread, NULL, NULL, NULL,
			LSM6DSO_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(&lsm6dso_tcb, "lsm6dso_task");
}
