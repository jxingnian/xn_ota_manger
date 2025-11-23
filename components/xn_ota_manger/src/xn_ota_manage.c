/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-23 16:50:00
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-23 17:31:10
 * @FilePath: \xn_ota_manger\components\xn_ota_manger\src\xn_ota_manage.c
 *
 * @brief OTA 管理模块实现
 *
 * - 统一封装版本检查与 HTTP OTA 升级；
 * - 内部以任务 + 状态机形式周期运行；
 * - 对外仅暴露少量接口与事件回调。
 */

#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "http_ota_module.h"
#include "xn_ota_manage.h"

/* 日志 TAG */
static const char *TAG = "ota_manage";

/* -------------------- 内部状态与配置 -------------------- */

/* 上层配置副本，仅在初始化时赋值 */
static ota_manage_config_t s_ota_cfg;

/* 当前 OTA 管理状态 */
static ota_manage_state_t s_ota_state = OTA_MANAGE_STATE_IDLE;

/* 管理任务句柄与初始化标志 */
static TaskHandle_t s_ota_task   = NULL;
static bool         s_ota_inited = false;

/* 最近一次从云端获取到的版本信息及其有效性标志 */
static http_ota_cloud_version_t s_cloud_version;
static bool                     s_has_cloud_version = false;

/* 外部请求触发的命令标志，由管理任务在自身上下文中消费 */
static bool s_need_check  = false;   /* 触发版本检查 */
static bool s_need_update = false;   /* 触发 OTA 升级 */

/* 最近一次成功执行版本检查时的系统 Tick */
static TickType_t s_last_check_ticks = 0;

/* -------------------- 内部函数声明 -------------------- */

static void     ota_manage_task(void *arg);
static void     ota_manage_notify_state(ota_manage_state_t new_state);
static void     ota_manage_on_version_checked(bool has_update,
					      const http_ota_cloud_version_t *cloud_version,
					      void *user_data);
static esp_err_t ota_manage_do_check(void);
static esp_err_t ota_manage_do_update(void);

/* -------------------- 状态变更回调封装 -------------------- */

/**
 * @brief 更新内部状态并调用上层事件回调
 *
 * - 始终先更新 @ref s_ota_state；
 * - 若上层配置了 event_cb，则会携带当前缓存的云端版本信息一起回调。
 */
static void ota_manage_notify_state(ota_manage_state_t new_state)
{
	s_ota_state = new_state;

	if (s_ota_cfg.event_cb) {
		const http_ota_cloud_version_t *ver =
			s_has_cloud_version ? &s_cloud_version : NULL;
		s_ota_cfg.event_cb(new_state, ver, s_ota_cfg.user_data);
	}
}

/* -------------------- 版本检查回调（供 http_ota 模块调用） -------------------- */

/**
 * @brief http_ota_check_version 的回调实现
 *
 * @param has_update   是否检测到有新版本
 * @param cloud_version 云端返回的版本信息（在 has_update=true 时有效）
 * @param user_data    用户数据指针（当前未使用）
 *
 * - 若有更新，则缓存云端版本并切换到 HAS_UPDATE 状态；
 * - 若无更新或失败，则清除缓存并切换到 NO_UPDATE 状态。
 */
static void ota_manage_on_version_checked(bool has_update,
					  const http_ota_cloud_version_t *cloud_version,
					  void *user_data)
{
	(void)user_data;

	if (has_update && cloud_version) {
		/* 记录云端最新版本信息 */
		s_cloud_version      = *cloud_version;
		s_has_cloud_version  = true;

		ESP_LOGI(TAG, "found new version: %s", s_cloud_version.version);
		ota_manage_notify_state(OTA_MANAGE_STATE_HAS_UPDATE);
	} else {
		/* 无可用更新或查询失败时，清除旧的版本缓存 */
		memset(&s_cloud_version, 0, sizeof(s_cloud_version));
		s_has_cloud_version = false;

		ota_manage_notify_state(OTA_MANAGE_STATE_NO_UPDATE);
	}
}

/* -------------------- 执行一次版本检查 -------------------- */

/**
 * @brief 触发一次云端版本检查
 *
 * @return
 *  - ESP_OK              : 已成功调用底层版本检查接口
 *  - ESP_ERR_INVALID_ARG : 配置中 version_url 为空
 *  - 其它 esp_err_t      : 底层 http_ota_check_version 失败
 */
static esp_err_t ota_manage_do_check(void)
{
	if (s_ota_cfg.version_url[0] == '\0') {
		ESP_LOGW(TAG, "version_url is empty, skip check");
		return ESP_ERR_INVALID_ARG;
	}

	ota_manage_notify_state(OTA_MANAGE_STATE_CHECKING);

	esp_err_t ret = http_ota_check_version(s_ota_cfg.version_url,
					       ota_manage_on_version_checked,
					       NULL);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "check version failed: %s", esp_err_to_name(ret));
		ota_manage_notify_state(OTA_MANAGE_STATE_FAILED);
		return ret;
	}

	s_last_check_ticks = xTaskGetTickCount();
	return ESP_OK;
}

/* -------------------- 执行一次 OTA 升级 -------------------- */

/**
 * @brief 在已有云端版本信息的前提下执行一次 HTTP OTA 升级
 *
 * @return
 *  - ESP_OK                : OTA 升级成功（可能在内部触发重启）
 *  - ESP_ERR_INVALID_STATE : 当前没有有效的云端版本信息
 *  - 其它 esp_err_t        : http_ota_start 执行失败
 *
 * 注意：本函数为同步调用，会在当前任务中等待升级完成。
 */
static esp_err_t ota_manage_do_update(void)
{
	if (!s_has_cloud_version || s_cloud_version.download_url[0] == '\0') {
		ESP_LOGE(TAG, "no valid cloud version, cannot start ota");
		return ESP_ERR_INVALID_STATE;
	}

	http_ota_config_t cfg = {
		.url                = s_cloud_version.download_url,
		.timeout_ms         = 30000,
		.skip_version_check = true,
		.auto_reboot        = s_ota_cfg.auto_reboot,
	};

	ESP_LOGI(TAG, "start ota, url=%s", cfg.url);

	ota_manage_notify_state(OTA_MANAGE_STATE_UPDATING);

	/* 使用同步接口，在当前管理任务中等待升级完成。
	 * 是否在成功后自动重启由 cfg.auto_reboot 决定。 */
	esp_err_t ret = http_ota_start(&cfg,
				       s_ota_cfg.progress_cb,
				       s_ota_cfg.user_data);
	if (ret == ESP_OK) {
		ota_manage_notify_state(OTA_MANAGE_STATE_DONE);
	} else {
		ESP_LOGE(TAG, "ota start failed: %s", esp_err_to_name(ret));
		ota_manage_notify_state(OTA_MANAGE_STATE_FAILED);
	}

	return ret;
}

/* -------------------- 管理任务：驱动状态机循环运行 -------------------- */

/**
 * @brief OTA 管理主任务函数
 *
 * - 根据外部请求标志和配置周期性检查云端版本；
 * - 在检测到新版本后根据 auto_update 或外部请求执行升级；
 * - 间隔为 @ref OTA_MANAGE_STEP_INTERVAL_MS。
 */
static void ota_manage_task(void *arg)
{
	(void)arg;

	/* 可选：启动后立即检查一次更新 */
	if (s_ota_cfg.check_on_boot && s_ota_cfg.version_url[0] != '\0') {
		s_need_check = true;
	}

	for (;;) {
		/* 处理外部发起的版本检查请求 */
		if (s_need_check) {
			s_need_check = false;
			(void)ota_manage_do_check();
		}

		/* 若启用周期性检查，则按配置的时间间隔自动发起一次检查 */
		if (s_ota_cfg.check_interval_ms > 0 &&
		    s_ota_cfg.version_url[0] != '\0') {
			TickType_t now  = xTaskGetTickCount();
			TickType_t need = pdMS_TO_TICKS(s_ota_cfg.check_interval_ms);

			if (s_last_check_ticks == 0 ||
			    now - s_last_check_ticks >= need) {
				s_need_check = true;
			}
		}

		/* 在已获取云端新版本信息的前提下，根据请求或自动策略发起升级 */
		if (s_need_update ||
		    (s_ota_cfg.auto_update && s_has_cloud_version)) {
			s_need_update = false;
			(void)ota_manage_do_update();
		}

		vTaskDelay(pdMS_TO_TICKS(OTA_MANAGE_STEP_INTERVAL_MS));
	}
}

/* -------------------- 对外接口实现 -------------------- */

/**
 * @brief 初始化 OTA 管理模块
 *
 * - 复制上层配置，或在 config==NULL 时使用 @ref OTA_MANAGE_DEFAULT_CONFIG；
 * - 初始化底层 HTTP OTA 模块；
 * - 创建独立的 ota_manage_task 任务驱动状态机。
 *
 * @param config OTA 管理配置指针，可为 NULL
 *
 * @return
 *  - ESP_OK         : 初始化成功（若已初始化则直接返回 ESP_OK）
 *  - ESP_ERR_NO_MEM : 创建任务失败
 *  - 其它 esp_err_t : http_ota_init 失败
 */
esp_err_t ota_manage_init(const ota_manage_config_t *config)
{
	if (s_ota_inited) {
		return ESP_OK;
	}

	/* 复制上层配置，或使用默认配置 */
	if (config == NULL) {
		s_ota_cfg = OTA_MANAGE_DEFAULT_CONFIG();
	} else {
		s_ota_cfg = *config;
	}

	/* 初始化底层 HTTP OTA 模块 */
	esp_err_t ret = http_ota_init();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "http_ota_init failed: %s", esp_err_to_name(ret));
		return ret;
	}

	/* 重置内部状态 */
	s_ota_state         = OTA_MANAGE_STATE_IDLE;
	s_has_cloud_version = false;
	memset(&s_cloud_version, 0, sizeof(s_cloud_version));
	s_need_check        = false;
	s_need_update       = false;
	s_last_check_ticks  = 0;

	/* 创建 OTA 管理任务（若尚未创建） */
	if (s_ota_task == NULL) {
		BaseType_t ret_task = xTaskCreate(
			ota_manage_task,
			"ota_manage",
			4096,
			NULL,
			tskIDLE_PRIORITY + 1,
			&s_ota_task);

		if (ret_task != pdPASS) {
			ESP_LOGE(TAG, "create ota_manage task failed");
			s_ota_task = NULL;
			return ESP_ERR_NO_MEM;
		}
	}

	s_ota_inited = true;
	ESP_LOGI(TAG, "ota manage init done");
	return ESP_OK;
}

/**
 * @brief 获取当前 OTA 管理状态
 *
 * @return 当前内部状态机状态 @ref ota_manage_state_t
 */
ota_manage_state_t ota_manage_get_state(void)
{
	return s_ota_state;
}

/**
 * @brief 请求在后台任务中执行一次版本检查
 *
 * - 仅设置标志位，由 ota_manage_task 在下一个循环中真正发起检查；
 * - 若模块尚未初始化，则返回 ESP_ERR_INVALID_STATE。
 *
 * @return
 *  - ESP_OK                : 已成功提交检查请求
 *  - ESP_ERR_INVALID_STATE : 模块未初始化
 */
esp_err_t ota_manage_request_check(void)
{
	if (!s_ota_inited) {
		return ESP_ERR_INVALID_STATE;
	}

	s_need_check = true;
	return ESP_OK;
}

/**
 * @brief 在已发现新版本的前提下，请求开始 OTA 升级
 *
 * - 本函数不会直接执行升级，只是设置标志位；
 * - 真正的升级流程由 ota_manage_task 调用 ota_manage_do_update 完成；
 * - 若当前没有有效的云端版本信息，则返回 ESP_ERR_INVALID_STATE。
 *
 * @return
 *  - ESP_OK                : 已成功提交升级请求
 *  - ESP_ERR_INVALID_STATE : 模块未初始化或尚未检测到新版本
 */
esp_err_t ota_manage_request_update(void)
{
	if (!s_ota_inited) {
		return ESP_ERR_INVALID_STATE;
	}

	if (!s_has_cloud_version) {
		return ESP_ERR_INVALID_STATE;
	}

	s_need_update = true;
	return ESP_OK;
}
