/*
 * @Author: 星年 jixingnian@gmail.com
 * @Date: 2025-11-22 13:43:50
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-23 19:08:47
 * @FilePath: \xn_ota_manger\main\main.c
 * @Description: esp32 OTA管理组件 By.星年
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "xn_wifi_manage.h"
#include "xn_ota_manage.h"

static const char *TAG = "app_main";

/* 仅在首次拿到 IP 后初始化一次 OTA 管理 */
static bool s_ota_inited = false;

/*
 * @brief OTA 初始化任务
 *
 * 在独立任务栈中调用 ota_manage_init，避免在 sys_evt 任务中发生栈溢出。
 */
static void ota_init_task(void *arg)
{
	(void)arg;

	

	vTaskDelete(NULL);
}

/*
 * @brief WiFi 管理状态回调
 *
 * 当 WiFi 管理状态变为 CONNECTED（已拿到 IP）时，创建任务初始化 OTA 管理模块。
 */
static void wifi_manage_event_cb(wifi_manage_state_t state)
{
	if (state != WIFI_MANAGE_STATE_CONNECTED || s_ota_inited) {
		return;
	}

	BaseType_t ret = xTaskCreate(ota_init_task,
				    "ota_init",
				    4096,
				    NULL,
				    tskIDLE_PRIORITY + 2,
				    NULL);
	if (ret != pdPASS) {
		ESP_LOGE(TAG, "create ota_init task failed");
	}
}

/*
 * @brief 应用入口：初始化 WiFi 管理，WiFi 连上后再初始化 OTA 管理。
 */
void app_main(void)
{
	printf("esp32 OTA管理组件 By.星年\n");

	wifi_manage_config_t wifi_cfg = WIFI_MANAGE_DEFAULT_CONFIG();
	wifi_cfg.wifi_event_cb        = wifi_manage_event_cb;

	esp_err_t ret = wifi_manage_init(&wifi_cfg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "wifi_manage_init failed: %s", esp_err_to_name(ret));
	}
}
