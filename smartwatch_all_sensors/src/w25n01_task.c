#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(w25n01_mem, LOG_LEVEL_INF);

/* GPIO */
#define GPIO0_NODE DT_NODELABEL(gpio0)
#define GPIO1_NODE DT_NODELABEL(gpio1)

#define CS_PIN     17   /* P0.17 */
#define WP_PIN     29   /* P0.29 */
#define HOLD_PIN   8    /* P1.08 */

/* SPI (IMPORTANT): use spi2 so i2c1 can keep HW instance 1 */
#define SPI_NODE DT_NODELABEL(spi2)

static struct spi_config spi_cfg = {
	.frequency = 1000000, /* safe */
	.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
	.slave = 0,
};

static const struct device *gpio0;
static const struct device *gpio1;
static const struct device *spi_dev;

static inline void cs_low(void)  { gpio_pin_set(gpio0, CS_PIN, 0); }
static inline void cs_high(void) { gpio_pin_set(gpio0, CS_PIN, 1); }

static int spi_tx(const uint8_t *tx, size_t len)
{
	struct spi_buf b = { .buf = (void *)tx, .len = len };
	struct spi_buf_set s = { .buffers = &b, .count = 1 };
	return spi_write(spi_dev, &spi_cfg, &s);
}

static int spi_rx(uint8_t *rx, size_t len)
{
	struct spi_buf b = { .buf = rx, .len = len };
	struct spi_buf_set s = { .buffers = &b, .count = 1 };
	return spi_read(spi_dev, &spi_cfg, &s);
}

/* ===== NAND commands ===== */
#define CMD_RESET        0xFF
#define CMD_WREN         0x06
#define CMD_GET_FEATURE  0x0F
#define CMD_SET_FEATURE  0x1F
#define CMD_BLOCK_ERASE  0xD8
#define CMD_PROG_LOAD    0x02
#define CMD_PROG_EXEC    0x10
#define CMD_PAGE_READ    0x13
#define CMD_READ_CACHE   0x03

#define REG_STATUS       0xC0
#define REG_PROTECTION   0xA0

#define SR_OIP   (1 << 0)
#define SR_EFAIL (1 << 2)
#define SR_PFAIL (1 << 3)

#define PAGES_PER_BLOCK 64
#define DEMO_BLOCK      1
#define DEMO_PAGE       (DEMO_BLOCK * PAGES_PER_BLOCK)
#define COL_ADDR        0x0000

static uint8_t get_status(void)
{
	uint8_t tx[3] = { CMD_GET_FEATURE, REG_STATUS, 0x00 };
	uint8_t rx[3] = { 0 };

	cs_low();
	spi_tx(tx, sizeof(tx));
	spi_rx(rx, sizeof(rx));
	cs_high();

	return rx[2];
}

static void wait_ready(const char *tag, int timeout_ms)
{
	int elapsed = 0;
	while (1) {
		uint8_t sr = get_status();
		if ((sr & SR_OIP) == 0) {
			LOG_INF("%s: READY (STATUS=0x%02X)", tag, sr);
			return;
		}
		k_msleep(5);
		elapsed += 5;
		if (elapsed >= timeout_ms) {
			LOG_ERR("%s: TIMEOUT (STATUS=0x%02X)", tag, sr);
			return;
		}
	}
}

static void set_protection_off(void)
{
	uint8_t tx[3] = { CMD_SET_FEATURE, REG_PROTECTION, 0x00 };
	cs_low();
	spi_tx(tx, sizeof(tx));
	cs_high();
	k_msleep(2);
	LOG_INF("Protection cleared (A0=0x00)");
}

static void nand_reset(void)
{
	uint8_t cmd = CMD_RESET;
	cs_low();
	spi_tx(&cmd, 1);
	cs_high();
	k_msleep(5);
	LOG_INF("NAND reset");
}

static void nand_wren(void)
{
	uint8_t cmd = CMD_WREN;
	cs_low();
	spi_tx(&cmd, 1);
	cs_high();
}

static void nand_block_erase(uint32_t page_addr)
{
	uint8_t tx[4] = {
		CMD_BLOCK_ERASE,
		(uint8_t)((page_addr >> 16) & 0xFF),
		(uint8_t)((page_addr >> 8) & 0xFF),
		(uint8_t)(page_addr & 0xFF),
	};

	nand_wren();
	cs_low();
	spi_tx(tx, sizeof(tx));
	cs_high();

	LOG_INF("Erase issued for block=%d (page=%d)", DEMO_BLOCK, (int)page_addr);
	wait_ready("ERASE", 3000);

	uint8_t sr = get_status();
	if (sr & SR_EFAIL) {
		LOG_ERR("ERASE FAILED (STATUS=0x%02X)", sr);
	} else {
		LOG_INF("Erase OK");
	}
}

static void nand_program_page(uint32_t page_addr, const uint8_t *data, size_t len)
{
	uint8_t hdr[3] = { CMD_PROG_LOAD, 0x00, 0x00 };

	nand_wren();
	cs_low();
	spi_tx(hdr, sizeof(hdr));
	spi_tx(data, len);
	cs_high();

	uint8_t exec[4] = {
		CMD_PROG_EXEC,
		(uint8_t)((page_addr >> 16) & 0xFF),
		(uint8_t)((page_addr >> 8) & 0xFF),
		(uint8_t)(page_addr & 0xFF),
	};

	cs_low();
	spi_tx(exec, sizeof(exec));
	cs_high();

	LOG_INF("Program execute issued (page=%d)", (int)page_addr);
	wait_ready("PROGRAM", 3000);

	uint8_t sr = get_status();
	if (sr & SR_PFAIL) {
		LOG_ERR("PROGRAM FAILED (STATUS=0x%02X)", sr);
	} else {
		LOG_INF("Program OK");
	}
}

static void nand_page_read_to_cache(uint32_t page_addr)
{
	uint8_t tx[4] = {
		CMD_PAGE_READ,
		(uint8_t)((page_addr >> 16) & 0xFF),
		(uint8_t)((page_addr >> 8) & 0xFF),
		(uint8_t)(page_addr & 0xFF),
	};

	cs_low();
	spi_tx(tx, sizeof(tx));
	cs_high();

	wait_ready("PAGE_READ", 3000);
}

static void nand_read_cache(uint16_t col, uint8_t *out, size_t len)
{
	uint8_t tx[4] = {
		CMD_READ_CACHE,
		(uint8_t)((col >> 8) & 0xFF),
		(uint8_t)(col & 0xFF),
		0x00
	};

	cs_low();
	spi_tx(tx, sizeof(tx));
	spi_rx(out, len);
	cs_high();
}

static void dump_10_bytes_one_line(const uint8_t *rb)
{
	char ascii[11];
	for (int i = 0; i < 10; i++) {
		ascii[i] = (rb[i] >= 32 && rb[i] <= 126) ? (char)rb[i] : '.';
	}
	ascii[10] = '\0';

	LOG_INF("DUMP: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X | %s",
		rb[0], rb[1], rb[2], rb[3], rb[4],
		rb[5], rb[6], rb[7], rb[8], rb[9],
		ascii);
}

/* ---------- thread wrapper ---------- */
static void w25n01_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	LOG_INF("W25N01 task started. Connect BLE now; demo will repeat every 30s.");
	k_sleep(K_SECONDS(8));

	gpio0 = DEVICE_DT_GET(GPIO0_NODE);
	gpio1 = DEVICE_DT_GET(GPIO1_NODE);
	spi_dev = DEVICE_DT_GET(SPI_NODE);

	if (!device_is_ready(gpio0) || !device_is_ready(gpio1) || !device_is_ready(spi_dev)) {
		LOG_ERR("Devices not ready");
		return;
	}

	gpio_pin_configure(gpio0, CS_PIN, GPIO_OUTPUT_HIGH);
	gpio_pin_configure(gpio0, WP_PIN, GPIO_OUTPUT_HIGH);
	gpio_pin_configure(gpio1, HOLD_PIN, GPIO_OUTPUT_HIGH);
	k_msleep(10);

	nand_reset();
	set_protection_off();

	const char msg[] = "HELLO NAND";

	while (1) {
		LOG_INF("=== W25N01 NAND DEMO START ===");

		nand_block_erase(DEMO_PAGE);
		nand_program_page(DEMO_PAGE, (const uint8_t *)msg, strlen(msg));

		nand_page_read_to_cache(DEMO_PAGE);

		uint8_t rb[16] = {0};
		nand_read_cache(COL_ADDR, rb, sizeof(rb));

		LOG_INF("SUMMARY STRING: '%c%c%c%c%c%c%c%c%c%c'",
			rb[0], rb[1], rb[2], rb[3], rb[4],
			rb[5], rb[6], rb[7], rb[8], rb[9]);

		dump_10_bytes_one_line(rb);

		bool pass = (memcmp(rb, msg, strlen(msg)) == 0);
		LOG_INF("VERIFY: %s", pass ? "PASS" : "FAIL");

		LOG_INF("=== W25N01 NAND DEMO END | next run in 30s ===");
		k_sleep(K_SECONDS(30));
	}
}

/* thread objects */
#define W25N01_STACK_SIZE 4096
#define W25N01_PRIORITY   5

K_THREAD_STACK_DEFINE(w25n01_stack, W25N01_STACK_SIZE);
static struct k_thread w25n01_tcb;
static bool started;

void w25n01_task_start(void)
{
	if (started) {
		return;
	}
	started = true;

	k_thread_create(&w25n01_tcb, w25n01_stack, K_THREAD_STACK_SIZEOF(w25n01_stack),
			w25n01_thread, NULL, NULL, NULL,
			W25N01_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(&w25n01_tcb, "w25n01_task");
}
