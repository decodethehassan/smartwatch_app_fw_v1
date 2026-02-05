#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include "ble_log_service.h"

/* 128-bit UUIDs */
#define BT_UUID_LOG_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x9f7b0000, 0x6c35, 0x4d2c, 0x9c85, 0x4a8c1a2b3c4d)

#define BT_UUID_LOG_STREAM_VAL \
	BT_UUID_128_ENCODE(0x9f7b0001, 0x6c35, 0x4d2c, 0x9c85, 0x4a8c1a2b3c4d)

static struct bt_uuid_128 log_svc_uuid = BT_UUID_INIT_128(BT_UUID_LOG_SERVICE_VAL);
static struct bt_uuid_128 log_chr_uuid = BT_UUID_INIT_128(BT_UUID_LOG_STREAM_VAL);

static struct bt_conn *g_conn;
static volatile bool g_notify_enabled;

static uint8_t g_last[200];
static size_t  g_last_len;

static ssize_t log_read(struct bt_conn *conn,
			const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	(void)attr;
	return bt_gatt_attr_read(conn, attr, buf, len, offset, g_last, g_last_len);
}

static void ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	g_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

/* attrs index:
 * 0 = primary service
 * 1 = chr declaration
 * 2 = chr value (notify this)
 * 3 = ccc
 */
BT_GATT_SERVICE_DEFINE(log_svc,
	BT_GATT_PRIMARY_SERVICE(&log_svc_uuid),
	BT_GATT_CHARACTERISTIC(&log_chr_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       log_read, NULL, NULL),
	BT_GATT_CCC(ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		return;
	}

	if (g_conn) {
		bt_conn_unref(g_conn);
		g_conn = NULL;
	}
	g_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(reason);

	if (g_conn) {
		bt_conn_unref(g_conn);
		g_conn = NULL;
	}
	g_notify_enabled = false;
}

BT_CONN_CB_DEFINE(conn_cb) = {
	.connected = connected,
	.disconnected = disconnected,
};

int ble_log_service_init(void)
{
	int err = bt_enable(NULL);
	if (err) {
		return err;
	}

	/* connectable advertising with device name */
	return bt_le_adv_start(BT_LE_ADV_CONN_NAME, NULL, 0, NULL, 0);
}

int ble_log_send_as(const uint8_t *data, size_t len)
{
	if (!data || len == 0) {
		return 0;
	}

	struct bt_conn *conn = g_conn;
	if (!conn || !g_notify_enabled) {
		/* not connected or notify not enabled yet */
		return 0;
	}

	uint16_t mtu = bt_gatt_get_mtu(conn);
	uint16_t max_payload = (mtu > 3) ? (mtu - 3) : 20;

	g_last_len = MIN(len, sizeof(g_last));
	memcpy(g_last, data, g_last_len);

	size_t off = 0;
	while (off < len) {
		uint16_t chunk = (uint16_t)MIN((size_t)max_payload, len - off);

		int tries = 0;
		int err;
		do {
			err = bt_gatt_notify(conn, &log_svc.attrs[2], data + off, chunk);
			if (err == -ENOMEM) {
				k_msleep(5);
			}
			tries++;
		} while (err == -ENOMEM && tries < 10);

		if (err) {
			return err;
		}

		off += chunk;
		k_yield();
	}

	return (int)len;
}
