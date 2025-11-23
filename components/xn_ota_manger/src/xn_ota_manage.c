/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-23 16:50:00
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-23 16:50:00
 * @FilePath: \xn_ota_manger\components\xn_ota_manger\src\xn_ota_manage.c
 * @Description: OTA 管理模块实现（封装 HTTP OTA / 版本检查 / 状态机）
 */

#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "http_ota.h"
#include "xn_ota_manage.h"

/* 日志 TAG */
static const char *TAG = "ota_manage";

/* 管理模块内部状态与配置 */
static ota_manage_config_t s_ota_cfg;                 /* 上层配置副本 */
static ota_manage_state_t  s_ota_state = OTA_MANAGE_STATE_IDLE;
static TaskHandle_t        s_ota_task  = NULL;
static bool                s_ota_inited = false;

/* 最近一次云端版本信息（可选） */
static http_ota_cloud_version_t s_cloud_version;      /* 最近一次获取到的云端版本信息 */
static bool                     s_has_cloud_version = false;

/* 由外部请求设置的“命令标志”，由管理任务消费 */
static bool s_need_check  = false;                    /* 是否需要执行一次版本检查 */
static bool s_need_update = false;                    /* 是否需要执行一次 OTA 升级 */

/* 最近一次成功检查版本的时间戳（Tick） */
static TickType_t s_last_check_ticks = 0;

/* -------------------- 内部辅助函数声明 -------------------- */
static void ota_manage_task(void *arg);
static void ota_manage_notify_state(ota_manage_state_t new_state);
static void ota_manage_on_version_checked(bool has_update,
                                          const http_ota_cloud_version_t *cloud_version,
                                          void *user_data);
static esp_err_t ota_manage_do_check(void);
static esp_err_t ota_manage_do_update(void);

/* -------------------- 状态变更回调通知 -------------------- */

static void ota_manage_notify_state(ota_manage_state_t new_state)
{
    s_ota_state = new_state;

    if (s_ota_cfg.event_cb) {
        const http_ota_cloud_version_t *ver = s_has_cloud_version ? &s_cloud_version : NULL;
        s_ota_cfg.event_cb(new_state, ver, s_ota_cfg.user_data);
    }
}

/* -------------------- 版本检查回调（供 http_ota 使用） -------------------- */

static void ota_manage_on_version_checked(bool has_update,
                                          const http_ota_cloud_version_t *cloud_version,
                                          void *user_data)
{
    (void)user_data;

    if (has_update && cloud_version != NULL) {
        /* 复制云端版本信息 */
        s_cloud_version = *cloud_version;
        s_has_cloud_version = true;

        ESP_LOGI(TAG, "found new version: %s", s_cloud_version.version);
        ota_manage_notify_state(OTA_MANAGE_STATE_HAS_UPDATE);
    } else {
        /* 无更新或查询失败时，不再保留旧版本信息 */
        memset(&s_cloud_version, 0, sizeof(s_cloud_version));
        s_has_cloud_version = false;

        ota_manage_notify_state(OTA_MANAGE_STATE_NO_UPDATE);
    }
}

/* -------------------- 执行一次版本检查 -------------------- */

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

static esp_err_t ota_manage_do_update(void)
{
    if (!s_has_cloud_version || s_cloud_version.download_url[0] == '\0') {
        ESP_LOGE(TAG, "no valid cloud version, cannot start ota");
        return ESP_ERR_INVALID_STATE;
    }

    http_ota_config_t cfg = {
        .url               = s_cloud_version.download_url,
        .timeout_ms        = 30000,
        .skip_version_check = true,
        .auto_reboot       = s_ota_cfg.auto_reboot,
    };

    ESP_LOGI(TAG, "start ota, url=%s", cfg.url);

    ota_manage_notify_state(OTA_MANAGE_STATE_UPDATING);

    /* 此处使用同步接口，由 OTA 管理任务阻塞等待结果。
     * http_ota_start 内部根据 cfg.auto_reboot 决定是否在成功后重启。 */
    esp_err_t ret = http_ota_start(&cfg, s_ota_cfg.progress_cb, s_ota_cfg.user_data);
    if (ret == ESP_OK) {
        ota_manage_notify_state(OTA_MANAGE_STATE_DONE);
    } else {
        ESP_LOGE(TAG, "ota start failed: %s", esp_err_to_name(ret));
        ota_manage_notify_state(OTA_MANAGE_STATE_FAILED);
    }

    return ret;
}

/* -------------------- 管理任务：驱动状态机 -------------------- */

static void ota_manage_task(void *arg)
{
    (void)arg;

    /* 启动后是否立刻进行一次检查 */
    if (s_ota_cfg.check_on_boot && s_ota_cfg.version_url[0] != '\0') {
        s_need_check = true;
    }

    for (;;) {
        /* 处理外部请求的检查命令 */
        if (s_need_check) {
            s_need_check = false;
            (void)ota_manage_do_check();
        }

        /* 若启用了周期性检查，根据时间间隔自动触发 */
        if (s_ota_cfg.check_interval_ms > 0 && s_ota_cfg.version_url[0] != '\0') {
            TickType_t now   = xTaskGetTickCount();
            TickType_t need  = pdMS_TO_TICKS(s_ota_cfg.check_interval_ms);

            if (s_last_check_ticks == 0 || now - s_last_check_ticks >= need) {
                s_need_check = true;
            }
        }

        /* 若应用或配置请求升级，则在已获取新版本信息的前提下启动 OTA */
        if (s_need_update || (s_ota_cfg.auto_update && s_has_cloud_version)) {
            s_need_update = false;
            (void)ota_manage_do_update();
        }

        vTaskDelay(pdMS_TO_TICKS(OTA_MANAGE_STEP_INTERVAL_MS));
    }
}

/* -------------------- 对外接口实现 -------------------- */

esp_err_t ota_manage_init(const ota_manage_config_t *config)
{
    if (s_ota_inited) {
        return ESP_OK;
    }

    /* 复制配置或使用默认配置 */
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

    s_ota_state         = OTA_MANAGE_STATE_IDLE;
    s_has_cloud_version = false;
    memset(&s_cloud_version, 0, sizeof(s_cloud_version));
    s_need_check        = false;
    s_need_update       = false;
    s_last_check_ticks  = 0;

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

ota_manage_state_t ota_manage_get_state(void)
{
    return s_ota_state;
}

esp_err_t ota_manage_request_check(void)
{
    if (!s_ota_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    s_need_check = true;
    return ESP_OK;
}

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
