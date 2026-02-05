#pragma once
#include <stddef.h>
#include <stdint.h>

int ble_log_service_init(void);

/* Send log bytes to notify characteristic (UTF-8 text) */
int ble_log_send_as(const uint8_t *data, size_t len);
