#include <zephyr/kernel.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/logging/log_ctrl.h>
#include <string.h>

#include "ble_log_service.h"

/* IMPORTANT: Do NOT use LOG_INF/LOG_ERR inside a log backend */

static uint8_t out_buf[256];

static int output_func(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);
	(void)ble_log_send_as(data, length);
	return (int)length;
}

LOG_OUTPUT_DEFINE(ble_log_output, output_func, out_buf, sizeof(out_buf));

static void backend_process(const struct log_backend *const backend,
			    union log_msg_generic *msg)
{
	ARG_UNUSED(backend);

	struct log_msg *m = (struct log_msg *)&msg->log;

	/* Format & emit message */
	log_output_msg_process(&ble_log_output, m, 0U);

	/* newline */
	static const uint8_t nl[2] = {'\r', '\n'};
	(void)ble_log_send_as(nl, sizeof(nl));
}

static void backend_dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);

	char s[64];
	int n = snprintk(s, sizeof(s), "[DROPPED=%u]\r\n", (unsigned)cnt);
	if (n > 0) {
		(void)ble_log_send_as((const uint8_t *)s, (size_t)n);
	}
}

static void backend_panic(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
}

static const struct log_backend_api backend_api = {
	.process = backend_process,
	.dropped = backend_dropped,
	.panic   = backend_panic,
};

LOG_BACKEND_DEFINE(ble_backend, backend_api, true);
