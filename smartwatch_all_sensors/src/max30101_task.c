#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>

LOG_MODULE_REGISTER(max30101_demo, LOG_LEVEL_INF);

#define MAX30101_I2C_ADDR 0x57

/* Registers */
#define REG_INTR_STATUS_1     0x00
#define REG_INTR_STATUS_2     0x01
#define REG_INTR_ENABLE_1     0x02
#define REG_INTR_ENABLE_2     0x03
#define REG_FIFO_WR_PTR       0x04
#define REG_FIFO_OVF_CNT      0x05
#define REG_FIFO_RD_PTR       0x06
#define REG_FIFO_DATA         0x07
#define REG_FIFO_CONFIG       0x08
#define REG_MODE_CONFIG       0x09
#define REG_SPO2_CONFIG       0x0A
#define REG_LED1_PA           0x0C   /* LED1 = RED */
#define REG_LED2_PA           0x0D   /* LED2 = IR  */
#define REG_LED3_PA           0x0E   /* LED3 = GREEN */
#define REG_MULTI_LED_CTRL1   0x11
#define REG_MULTI_LED_CTRL2   0x12
#define REG_REV_ID            0xFE
#define REG_PART_ID           0xFF

static const struct device *i2c_dev;

static int wr(uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(i2c_dev, MAX30101_I2C_ADDR, reg, val);
}

static int rd(uint8_t reg, uint8_t *val)
{
	return i2c_reg_read_byte(i2c_dev, MAX30101_I2C_ADDR, reg, val);
}

static uint32_t parse_sample18(const uint8_t b[3])
{
	uint32_t v = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
	return v & 0x3FFFF; /* 18-bit */
}

static void dump_regs(void)
{
	uint8_t v;

	rd(REG_MODE_CONFIG, &v);      LOG_INF("MODE_CONFIG      (0x09) = 0x%02X", v);
	rd(REG_FIFO_CONFIG, &v);      LOG_INF("FIFO_CONFIG      (0x08) = 0x%02X", v);
	rd(REG_SPO2_CONFIG, &v);      LOG_INF("SPO2_CONFIG      (0x0A) = 0x%02X", v);
	rd(REG_MULTI_LED_CTRL1, &v);  LOG_INF("MULTI_LED_CTRL1  (0x11) = 0x%02X", v);
	rd(REG_MULTI_LED_CTRL2, &v);  LOG_INF("MULTI_LED_CTRL2  (0x12) = 0x%02X", v);
	rd(REG_LED1_PA, &v);          LOG_INF("LED1_PA (RED)    (0x0C) = 0x%02X", v);
	rd(REG_LED2_PA, &v);          LOG_INF("LED2_PA (IR)     (0x0D) = 0x%02X", v);
	rd(REG_LED3_PA, &v);          LOG_INF("LED3_PA (GREEN)  (0x0E) = 0x%02X", v);
}

static bool max30101_reset_wait(void)
{
	/* Reset bit is MODE_CONFIG bit6. We write it then poll until it clears. */
	int err = wr(REG_MODE_CONFIG, 0x40);
	if (err) {
		LOG_ERR("Reset write failed err=%d", err);
		return false;
	}

	for (int i = 0; i < 50; i++) { /* up to ~500ms */
		uint8_t mc = 0;
		if (rd(REG_MODE_CONFIG, &mc) == 0) {
			if ((mc & 0x40) == 0) {
				return true; /* reset done */
			}
		}
		k_msleep(10);
	}

	LOG_ERR("Reset did not clear!");
	return false;
}

static void max30101_manual_init(void)
{
	uint8_t part = 0, rev = 0;

	if (rd(REG_PART_ID, &part) == 0 && rd(REG_REV_ID, &rev) == 0) {
		LOG_INF("PART_ID (0xFF)=0x%02X | REV_ID (0xFE)=0x%02X", part, rev);
	} else {
		LOG_ERR("Failed to read PART/REV ID");
	}

	if (!max30101_reset_wait()) {
		return;
	}

	/* Disable interrupts for bring-up (simple) */
	wr(REG_INTR_ENABLE_1, 0x00);
	wr(REG_INTR_ENABLE_2, 0x00);

	/* FIFO:
	   SMP_AVE=1 (000), FIFO_ROLLOVER_EN=1, FIFO_A_FULL=15 => 0x1F
	*/
	wr(REG_FIFO_CONFIG, 0x1F);

	/* Multi-LED mode */
	wr(REG_MODE_CONFIG, 0x07);

	/* SPO2 config (keep your working value) */
	wr(REG_SPO2_CONFIG, 0x27);

	/* LED currents (0x24 ~ moderate) */
	wr(REG_LED1_PA, 0x24);  /* RED */
	wr(REG_LED2_PA, 0x24);  /* IR  */
	wr(REG_LED3_PA, 0x24);  /* GREEN */

	/* Slots: S1=LED1(RED), S2=LED2(IR), S3=LED3(GREEN), S4=NONE */
	wr(REG_MULTI_LED_CTRL1, 0x21);
	wr(REG_MULTI_LED_CTRL2, 0x03);

	/* Clear FIFO pointers */
	wr(REG_FIFO_WR_PTR,  0x00);
	wr(REG_FIFO_OVF_CNT, 0x00);
	wr(REG_FIFO_RD_PTR,  0x00);

	/* Clear latched status */
	uint8_t tmp;
	rd(REG_INTR_STATUS_1, &tmp);
	rd(REG_INTR_STATUS_2, &tmp);

	LOG_INF("Manual Multi-LED (RED/IR/GREEN) configuration applied.");
	dump_regs();
}

static void max30101_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	LOG_INF("=== MAX30101 REGISTER-LEVEL FIFO READ (NO ZEPHYR DRIVER) ===");

	i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C0 NOT READY");
		return;
	}

	max30101_manual_init();

	uint32_t tick = 0;

	while (1) {
		uint8_t wrp = 0, rdp = 0, ovf = 0;

		if (rd(REG_FIFO_WR_PTR, &wrp) != 0 ||
		    rd(REG_FIFO_RD_PTR, &rdp) != 0 ||
		    rd(REG_FIFO_OVF_CNT, &ovf) != 0) {
			LOG_ERR("Failed to read FIFO pointers");
			k_msleep(100);
			continue;
		}

		uint8_t available = (wrp - rdp) & 0x1F;

		/* Always show progress even if no samples */
		if ((++tick % 50) == 0) { /* ~1 second (50 * 20ms) */
			uint8_t s1 = 0, s2 = 0, mc = 0;
			rd(REG_INTR_STATUS_1, &s1);
			rd(REG_INTR_STATUS_2, &s2);
			rd(REG_MODE_CONFIG, &mc);

			LOG_INF("FIFO DBG | WR=%u RD=%u OVF=%u avail=%u | INT1=0x%02X INT2=0x%02X | MODE=0x%02X",
				wrp, rdp, ovf, available, s1, s2, mc);
		}

		if (available == 0) {
			k_msleep(20);
			continue;
		}

		/* Read 1 frame: 3 samples (RED, IR, GREEN) => 9 bytes */
		uint8_t raw[9];
		int err = i2c_burst_read(i2c_dev, MAX30101_I2C_ADDR, REG_FIFO_DATA, raw, sizeof(raw));
		if (err) {
			LOG_ERR("FIFO read err=%d", err);
			k_msleep(50);
			continue;
		}

		uint32_t red   = parse_sample18(&raw[0]);
		uint32_t ir    = parse_sample18(&raw[3]);
		uint32_t green = parse_sample18(&raw[6]);

		LOG_INF("PPG FIFO | RED=%u | IR=%u | GREEN=%u | avail=%u", red, ir, green, available);

		k_msleep(50);
	}
}

/* thread objects */
#define MAX30101_STACK_SIZE 3072
#define MAX30101_PRIORITY   5

K_THREAD_STACK_DEFINE(max30101_stack, MAX30101_STACK_SIZE);
static struct k_thread max30101_tcb;
static bool started;

void max30101_task_start(void)
{
	if (started) {
		return;
	}
	started = true;

	k_thread_create(&max30101_tcb, max30101_stack, K_THREAD_STACK_SIZEOF(max30101_stack),
			max30101_thread, NULL, NULL, NULL,
			MAX30101_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(&max30101_tcb, "max30101_task");
}
