/*
 * @Author: 星年 jixingnian@gmail.com
 * @Date: 2025-11-22 13:43:50
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-23 17:24:44
 * @FilePath: \xn_ota_manger\main\main.c
 * @Description: esp32 OTA管理组件 By.星年
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "xn_ota_manage.h"

/* 应用日志 TAG */
static const char *TAG = "app_main";

/**
 * @brief OTA 管理事件回调（仅作日志输出示例）
 *
 * - 每当 ota_manage 状态发生变化时被调用；
 * - 当前实现仅打印日志，实际升级策略可在此基础上扩展。
 */
static void ota_manage_event_log_cb(ota_manage_state_t              state,
			       const http_ota_cloud_version_t *cloud_version,
			       void                           *user_data)
{
	(void)user_data;

	switch (state) {
	case OTA_MANAGE_STATE_IDLE:
		ESP_LOGI(TAG, "ota state: IDLE");
		break;
	case OTA_MANAGE_STATE_CHECKING:
		ESP_LOGI(TAG, "ota state: CHECKING");
		break;
	case OTA_MANAGE_STATE_NO_UPDATE:
		ESP_LOGI(TAG, "ota state: NO_UPDATE");
		break;
	case OTA_MANAGE_STATE_HAS_UPDATE:
		if (cloud_version) {
			ESP_LOGI(TAG,
				 "ota state: HAS_UPDATE, version=%s, url=%s",
				 cloud_version->version,
				 cloud_version->download_url);
		} else {
			ESP_LOGI(TAG, "ota state: HAS_UPDATE");
		}
		break;
	case OTA_MANAGE_STATE_UPDATING:
		ESP_LOGI(TAG, "ota state: UPDATING");
		break;
	case OTA_MANAGE_STATE_DONE:
		ESP_LOGI(TAG, "ota state: DONE");
		break;
	case OTA_MANAGE_STATE_FAILED:
		ESP_LOGW(TAG, "ota state: FAILED");
		break;
	default:
		break;
	}
}

/**
 * @brief 应用入口：初始化 OTA 管理并触发一次版本检查
 *
 * 当前示例流程：
 *  - 打印组件信息；
 *  - 使用默认配置填充 ota_manage_config_t；
 *  - 设置版本检查 URL 与回调；
 *  - 初始化 ota_manage 并主动请求一次版本检查。
 *
 * 注意：version_url 需要根据你的实际服务端 OTA 版本接口地址进行修改。
 */
void app_main(void)
{
	/* 简单启动提示 */
	printf("esp32 OTA管理组件 By.星年\n");

	/* 基于默认宏初始化配置，再按需覆盖部分字段 */
	ota_manage_config_t cfg = OTA_MANAGE_DEFAULT_CONFIG();

	/* TODO: 将此处的版本检查 URL 替换为你自己的云端版本 JSON 地址 */
	snprintf(cfg.version_url,
		 sizeof(cfg.version_url),
		 "http://192.168.1.100:8080/ota/version.json");

	/* 启动时检查一次，不做周期性自动检查，避免开发阶段频繁访问服务器 */
	cfg.check_on_boot     = true;
	cfg.check_interval_ms = -1;

	/* 仅检查并输出“有无新版本”，是否升级由上层策略决定 */
	cfg.auto_update       = false;

	/* 注册简单日志回调，方便观察状态机变化 */
	cfg.event_cb          = ota_manage_event_log_cb;
	cfg.progress_cb       = NULL;
	cfg.user_data         = NULL;

	/* 初始化 OTA 管理模块（内部会创建任务并驱动状态机） */
	esp_err_t ret = ota_manage_init(&cfg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "ota_manage_init failed: %s", esp_err_to_name(ret));
		return;
	}

	/* 主动触发一次版本检查；后续也可以在业务逻辑中再次调用 */
	( void )ota_manage_request_check();
}
