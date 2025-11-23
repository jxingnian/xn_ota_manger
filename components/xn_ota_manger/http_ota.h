/*
 * @Author: xingnian
 * @Date: 2025-10-21
 * @Description: HTTP OTA 升级模块头文件
 */

#ifndef HTTP_OTA_H
#define HTTP_OTA_H

#include "esp_err.h"
#include "esp_http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP OTA 配置结构体
 */
typedef struct {
    const char *url;                    /*!< OTA 固件下载 URL (HTTP) */
    int timeout_ms;                      /*!< HTTP 超时时间 (ms) */
    bool skip_version_check;             /*!< 是否跳过版本检查 */
    bool auto_reboot;                    /*!< OTA 成功后是否自动重启 */
} http_ota_config_t;

/**
 * @brief HTTP OTA 事件类型
 */
typedef enum {
    HTTP_OTA_EVENT_START = 0,           /*!< OTA 开始 */
    HTTP_OTA_EVENT_CONNECTED,           /*!< 已连接到服务器 */
    HTTP_OTA_EVENT_DOWNLOADING,         /*!< 正在下载 */
    HTTP_OTA_EVENT_FINISH,              /*!< OTA 完成 */
    HTTP_OTA_EVENT_FAILED,              /*!< OTA 失败 */
} http_ota_event_t;

/**
 * @brief HTTP OTA 进度回调函数类型
 *
 * @param event OTA 事件类型
 * @param current_bytes 当前已下载字节数
 * @param total_bytes 总字节数
 * @param user_data 用户数据指针
 */
typedef void (*http_ota_progress_cb_t)(http_ota_event_t event,
                                       size_t current_bytes,
                                       size_t total_bytes,
                                       void *user_data);

/**
 * @brief 初始化 HTTP OTA 模块
 *
 * @return
 *    - ESP_OK: 初始化成功
 *    - ESP_FAIL: 初始化失败
 */
esp_err_t http_ota_init(void);

/**
 * @brief 启动 HTTP OTA 升级
 *
 * @param config OTA 配置
 * @param progress_cb 进度回调函数 (可选，可为 NULL)
 * @param user_data 用户数据指针 (传递给回调函数)
 *
 * @return
 *    - ESP_OK: OTA 升级成功
 *    - ESP_FAIL: OTA 升级失败
 */
esp_err_t http_ota_start(const http_ota_config_t *config,
                         http_ota_progress_cb_t progress_cb,
                         void *user_data);

/**
 * @brief 启动 HTTP OTA 升级任务 (异步，在新任务中执行)
 *
 * @param config OTA 配置
 * @param progress_cb 进度回调函数 (可选，可为 NULL)
 * @param user_data 用户数据指针 (传递给回调函数)
 *
 * @return
 *    - ESP_OK: 任务创建成功
 *    - ESP_FAIL: 任务创建失败
 */
esp_err_t http_ota_start_async(const http_ota_config_t *config,
                               http_ota_progress_cb_t progress_cb,
                               void *user_data);

/**
 * @brief 获取当前运行的固件版本
 *
 * @return 固件版本字符串
 */
const char *http_ota_get_version(void);

/**
 * @brief 检查是否支持 OTA 回滚
 *
 * @return
 *    - true: 支持回滚
 *    - false: 不支持回滚
 */
bool http_ota_rollback_is_possible(void);

/**
 * @brief 标记当前固件为有效 (防止回滚)
 *
 * @return
 *    - ESP_OK: 标记成功
 *    - ESP_FAIL: 标记失败
 */
esp_err_t http_ota_mark_app_valid(void);

/**
 * @brief 云端版本信息结构体
 */
typedef struct {
    char version[32];           /*!< 云端固件版本 */
    char download_url[256];     /*!< 固件下载地址 */
    char description[128];      /*!< 更新说明 */
    bool force_update;          /*!< 是否强制更新 */
} http_ota_cloud_version_t;

/**
 * @brief 版本检查回调函数类型
 *
 * @param has_update 是否有新版本
 * @param cloud_version 云端版本信息 (如果has_update为true)
 * @param user_data 用户数据指针
 */
typedef void (*http_ota_version_check_cb_t)(bool has_update,
                                            const http_ota_cloud_version_t *cloud_version,
                                            void *user_data);

/**
 * @brief 检查云端是否有新版本
 *
 * @param version_url 版本信息API地址 (返回JSON格式)
 * @param callback 检查结果回调函数
 * @param user_data 用户数据指针
 *
 * @return
 *    - ESP_OK: 检查成功
 *    - ESP_FAIL: 检查失败
 *
 * @note version_url 应返回如下格式的JSON:
 *       {"version":"1.0.1","url":"http://xxx/firmware.bin","description":"修复bug","force":false}
 */
esp_err_t http_ota_check_version(const char *version_url,
                                 http_ota_version_check_cb_t callback,
                                 void *user_data);

/**
 * @brief 比较两个版本号 (格式: x.y.z)
 *
 * @param v1 版本1
 * @param v2 版本2
 *
 * @return
 *    - >0: v1 > v2
 *    - =0: v1 == v2
 *    - <0: v1 < v2
 */
int http_ota_compare_version(const char *v1, const char *v2);

#ifdef __cplusplus
}
#endif

#endif // HTTP_OTA_H

