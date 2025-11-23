/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-23 16:50:00
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-23 16:50:00
 * @FilePath: \xn_ota_manger\components\xn_ota_manger\include\xn_ota_manage.h
 * @Description: OTA 管理模块对外接口（封装 HTTP OTA / 版本检查 / 状态机）
 *
 * - 提供统一的 OTA 状态机与事件回调；
 * - 支持周期性检查云端版本；
 * - 可选自动升级 / 自动重启策略；
 *
 * 上层应用只需调用 ota_manage_init() 并根据回调决策是否升级，
 * 具体下载与烧写流程由底层 http_ota 模块完成。
 */

#ifndef XN_OTA_MANAGE_H
#define XN_OTA_MANAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "http_ota.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA 管理状态机单步运行周期（单位：ms）
 *
 * - 间隔过小：状态更新更及时，但占用更多 CPU；
 * - 间隔过大：定时检查版本的精度变差。
 */
#define OTA_MANAGE_STEP_INTERVAL_MS 1000

/**
 * @brief OTA 管理抽象状态
 */
typedef enum {
    OTA_MANAGE_STATE_IDLE = 0,      ///< 空闲，未在检查或升级
    OTA_MANAGE_STATE_CHECKING,      ///< 正在检查云端版本
    OTA_MANAGE_STATE_NO_UPDATE,     ///< 已检查且无可用更新
    OTA_MANAGE_STATE_HAS_UPDATE,    ///< 已发现新版本，等待决策是否升级
    OTA_MANAGE_STATE_UPDATING,      ///< 正在执行 OTA 升级
    OTA_MANAGE_STATE_DONE,          ///< 升级完成（通常随后将重启）
    OTA_MANAGE_STATE_FAILED,        ///< 检查或升级失败
} ota_manage_state_t;

/**
 * @brief OTA 管理事件回调
 *
 * - 每当管理状态变化时被调用；
 * - 如当前已获取到云端版本信息，则 cloud_version 非 NULL。
 */
typedef void (*ota_manage_event_cb_t)(ota_manage_state_t                 state,
                                      const http_ota_cloud_version_t    *cloud_version,
                                      void                              *user_data);

/**
 * @brief OTA 管理模块配置
 *
 * 建议流程：
 * 1. 使用 OTA_MANAGE_DEFAULT_CONFIG() 获取默认配置；
 * 2. 填写 version_url 等必要字段；
 * 3. 调用 ota_manage_init() 完成初始化。
 */
typedef struct {
    char                    version_url[256];   ///< 云端版本信息 API 地址（返回 JSON）
    int                     check_interval_ms;  ///< 自动检查间隔（ms），<0 表示不自动检查
    bool                    check_on_boot;      ///< 是否在启动后自动检查一次
    bool                    auto_update;        ///< 发现新版本后是否自动开始 OTA 升级
    bool                    auto_reboot;        ///< OTA 成功后是否自动重启（传递给 http_ota）
    ota_manage_event_cb_t   event_cb;           ///< 状态机事件回调，可为 NULL
    http_ota_progress_cb_t  progress_cb;        ///< 进度回调（底层 http_ota 透传），可为 NULL
    void                   *user_data;          ///< 传递给回调的用户指针
} ota_manage_config_t;

/**
 * @brief OTA 管理模块默认配置宏
 */
#define OTA_MANAGE_DEFAULT_CONFIG()                        \
    (ota_manage_config_t){                                 \
        .version_url       = "",                          \
        .check_interval_ms = -1,                           \
        .check_on_boot     = true,                         \
        .auto_update       = false,                        \
        .auto_reboot       = true,                         \
        .event_cb          = NULL,                         \
        .progress_cb       = NULL,                         \
        .user_data         = NULL,                         \
    }

/**
 * @brief 初始化 OTA 管理模块
 *
 * - 会调用底层 http_ota_init() 完成基础 OTA 能力初始化；
 * - 创建独立任务按固定周期驱动状态机运行；
 * - 若配置了 check_on_boot 且 version_url 非空，则启动即检查一次更新。
 *
 * @param config 若为 NULL，则使用 @ref OTA_MANAGE_DEFAULT_CONFIG
 *
 * @return
 *      - ESP_OK           : 初始化成功
 *      - 其它 esp_err_t   : 初始化失败
 */
esp_err_t ota_manage_init(const ota_manage_config_t *config);

/**
 * @brief 获取当前 OTA 管理状态
 */
ota_manage_state_t ota_manage_get_state(void);

/**
 * @brief 主动请求一次版本检查
 *
 * - 会在内部任务上下文中异步执行 http_ota_check_version；
 * - 若当前已经在检查或升级中，则简单标记稍后重试，不立即返回错误。
 */
esp_err_t ota_manage_request_check(void);

/**
 * @brief 在已发现新版本的前提下，请求开始 OTA 升级
 *
 * - 需要先通过一次版本检查拿到云端版本信息；
 * - 若 auto_update=true，则无需显式调用本接口。
 */
esp_err_t ota_manage_request_update(void);

#ifdef __cplusplus
}
#endif

#endif /* XN_OTA_MANAGE_H */
