#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ble_log_service.h"
#include "as6221_task.h"
#include "lsm6dso_task.h"
#include "max30101_task.h"
#include "ads1113_task.h"   /* <-- add this */
#include "w25n01_task.h"

LOG_MODULE_REGISTER(main_all, LOG_LEVEL_INF);

int main(void)
{
	int err = ble_log_service_init();
	if (err) {
		LOG_ERR("ble_log_service_init failed (%d)", err);
	}

	k_msleep(500);

	as6221_task_start();
	lsm6dso_task_start();
	max30101_task_start();
	ads1113_task_start();    /* <-- add this */
    w25n01_task_start();

	LOG_INF("All sensor tasks started.");

	while (1) {
		k_sleep(K_SECONDS(1));
	}
}
