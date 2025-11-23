/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-10-21
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-23 17:20:00
 * @FilePath: \xn_ota_manger\components\xn_ota_manger\src\http_ota_module.c
 * @Description: HTTP OTA 升级模块实现
 *
 * 在 http_client_module 的基础上，按块下载远程固件并写入 OTA 分区，
 * 封装了断点重试、进度回调、版本比较与云端版本检查等功能。
 */

#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "http_ota_module.h"
#include "http_client_module.h"

static const char *TAG = "HTTP_OTA";

/**
 * @brief HTTP OTA 升级任务参数
 */
typedef struct {
    http_ota_config_t config;
    http_ota_progress_cb_t progress_cb;
    void *user_data;
} http_ota_task_params_t;

/**
 * @brief HTTP OTA 升级核心逻辑
 */
static esp_err_t http_ota_perform(const http_ota_config_t *config,
                                  http_ota_progress_cb_t progress_cb,
                                  void *user_data)
{
    ESP_LOGI(TAG, "开始 HTTP OTA 升级");
    ESP_LOGI(TAG, "固件 URL: %s", config->url);

    esp_err_t err = ESP_OK;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = NULL;
    http_client_handle_t http_client = NULL;
    uint8_t *buffer = NULL;
    size_t total_read = 0;
    size_t last_report = 0;  // 上次报告的字节数
    const size_t report_interval = 100 * 1024;  // 每100KB报告一次

    // 调用进度回调 - OTA 开始
    if (progress_cb) {
        progress_cb(HTTP_OTA_EVENT_START, 0, 0, user_data);
    }

    // 1. 创建HTTP客户端
    http_client_config_t http_config = {
        .url = config->url,
        .timeout_ms = config->timeout_ms,
    };

    http_client = http_client_create(&http_config);
    if (!http_client) {
        ESP_LOGE(TAG, "创建HTTP客户端失败");
        if (progress_cb) {
            progress_cb(HTTP_OTA_EVENT_FAILED, 0, 0, user_data);
        }
        return ESP_FAIL;
    }

    // 2. 打开HTTP连接
    err = http_client_open(http_client, "GET");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开HTTP连接失败: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // 检查HTTP状态码
    int status_code = http_client_get_status_code(http_client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP状态码错误: %d", status_code);
        err = ESP_FAIL;
        goto cleanup;
    }

    int content_length = http_client_get_content_length(http_client);
    ESP_LOGI(TAG, "固件大小: %d 字节 (%.2f KB)", content_length, content_length / 1024.0);

    // 调用进度回调 - 已连接
    if (progress_cb) {
        progress_cb(HTTP_OTA_EVENT_CONNECTED, 0, content_length, user_data);
    }

    // 3. 获取更新分区
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "获取OTA分区失败");
        err = ESP_FAIL;
        goto cleanup;
    }

    ESP_LOGI(TAG, "写入分区: %s (偏移: 0x%08"PRIx32", 大小: 0x%08"PRIx32")",
             update_partition->label, update_partition->address, update_partition->size);

    // 4. 开始OTA
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin失败: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // 5. 分配读取缓冲区（分块下载，每块50KB）
    const size_t chunk_size = 50 * 1024;  // 50KB一块
    buffer = (uint8_t *)malloc(chunk_size);
    if (!buffer) {
        ESP_LOGE(TAG, "分配缓冲区失败");
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    // 6. 连续下载并写入固件（检测丢包自动重传）
    ESP_LOGI(TAG, "开始下载固件，分块大小: %.1f KB", chunk_size / 1024.0);
    
    size_t chunk_start = 0;
    const int max_chunk_retries = 3;
    
    while (total_read < (size_t)content_length) {
        // 计算当前块大小
        size_t chunk_end = chunk_start + chunk_size - 1;
        if (chunk_end >= (size_t)content_length) {
            chunk_end = content_length - 1;
        }
        size_t expected_chunk_size = chunk_end - chunk_start + 1;
        
        // 尝试读取当前块
        size_t chunk_read = 0;
        int consecutive_zero_reads = 0;
        const int max_zero_reads = 5;  // 减少等待次数
        
        while (chunk_read < expected_chunk_size) {
            size_t remaining = expected_chunk_size - chunk_read;
            size_t to_read = (remaining < 4096) ? remaining : 4096;
            
            int read_len = http_client_read(http_client, buffer + chunk_read, to_read);
            
            if (read_len < 0) {
                ESP_LOGE(TAG, "读取固件数据失败");
                break;
            } else if (read_len == 0) {
                consecutive_zero_reads++;
                if (consecutive_zero_reads >= max_zero_reads) {
                    ESP_LOGW(TAG, "检测到连接异常 (连续零读取 %d 次)", consecutive_zero_reads);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            } else {
                consecutive_zero_reads = 0;
                chunk_read += read_len;
            }
        }
        
        // 检查块是否完整
        if (chunk_read == expected_chunk_size) {
            // 块完整，写入
            err = esp_ota_write(ota_handle, buffer, chunk_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write失败: %s", esp_err_to_name(err));
                goto cleanup;
            }
            
            total_read += chunk_read;
            chunk_start = chunk_end + 1;
            
            // 每100KB报告一次
            if (progress_cb && (total_read - last_report >= report_interval)) {
                progress_cb(HTTP_OTA_EVENT_DOWNLOADING, total_read, content_length, user_data);
                last_report = total_read;
            }
        } else {
            // 块不完整，使用 Range 重试
            ESP_LOGW(TAG, "块 [%zu-%zu] 不完整: 期望 %zu，实际 %zu 字节", 
                     chunk_start, chunk_end, expected_chunk_size, chunk_read);
            
            bool retry_success = false;
            for (int retry = 0; retry < max_chunk_retries && !retry_success; retry++) {
                ESP_LOGW(TAG, "Range 重试块 [%zu-%zu] (%d/%d)", 
                         chunk_start, chunk_end, retry + 1, max_chunk_retries);
                
                // 关闭旧连接
                http_client_close(http_client);
                http_client_destroy(http_client);
                vTaskDelay(pdMS_TO_TICKS(500));
                
                // 重新创建连接（Range请求）
                http_client = http_client_create(&http_config);
                if (!http_client) {
                    ESP_LOGE(TAG, "重新创建HTTP客户端失败");
                    continue;
                }
                
                char range_header[64];
                snprintf(range_header, sizeof(range_header), "bytes=%zu-%zu", chunk_start, chunk_end);
                http_client_set_header(http_client, "Range", range_header);
                
                if (http_client_open(http_client, "GET") != ESP_OK) {
                    ESP_LOGE(TAG, "重新连接失败");
                    continue;
                }
                
                int status = http_client_get_status_code(http_client);
                if (status != 206 && status != 200) {
                    ESP_LOGE(TAG, "Range请求失败: %d", status);
                    continue;
                }
                
                // 重新读取该块
                size_t retry_read = 0;
                while (retry_read < expected_chunk_size) {
                    size_t retry_remaining = expected_chunk_size - retry_read;
                    size_t retry_to_read = (retry_remaining < 4096) ? retry_remaining : 4096;
                    
                    int retry_len = http_client_read(http_client, buffer + retry_read, retry_to_read);
                    if (retry_len <= 0) {
                        if (retry_len == 0 && retry_read == expected_chunk_size) {
                            break;  // Range请求正常结束
                        }
                        ESP_LOGW(TAG, "重试读取返回: %d", retry_len);
                        vTaskDelay(pdMS_TO_TICKS(100));
                        continue;
                    }
                    retry_read += retry_len;
                }
                
                if (retry_read == expected_chunk_size) {
                    // 重试成功，写入
                    err = esp_ota_write(ota_handle, buffer, retry_read);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_write失败: %s", esp_err_to_name(err));
                        goto cleanup;
                    }
                    
                    total_read += retry_read;
                    chunk_start = chunk_end + 1;
                    retry_success = true;
                    
                    ESP_LOGI(TAG, "Range 重试成功，块 [%zu-%zu] 已下载", chunk_start - expected_chunk_size, chunk_end);
                    
                    if (progress_cb) {
                        progress_cb(HTTP_OTA_EVENT_DOWNLOADING, total_read, content_length, user_data);
                    }
                    
                    // 关闭Range连接，准备继续普通下载
                    http_client_close(http_client);
                    http_client_destroy(http_client);
                    
                    // 重新建立普通连接，从下一块继续
                    http_client = http_client_create(&http_config);
                    if (http_client) {
                        char range_continue[64];
                        snprintf(range_continue, sizeof(range_continue), "bytes=%zu-", chunk_start);
                        http_client_set_header(http_client, "Range", range_continue);
                        
                        if (http_client_open(http_client, "GET") == ESP_OK) {
                            ESP_LOGI(TAG, "重新建立连接，从 %zu 字节继续下载", chunk_start);
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "Range 重试失败: 期望 %zu，实际 %zu", expected_chunk_size, retry_read);
                }
            }
            
            if (!retry_success) {
                ESP_LOGE(TAG, "块 [%zu-%zu] 重试失败，已达最大次数", chunk_start, chunk_end);
                err = ESP_FAIL;
                break;
            }
        }
    }

    if (err != ESP_OK) {
        goto cleanup;
    }

    // 7. 检查是否有数据下载（基本验证）
    if (total_read == 0) {
        ESP_LOGE(TAG, "未下载到任何数据");
        err = ESP_FAIL;
        goto cleanup;
    }

    // 记录下载信息（Content-Length可能不准确，不作为验证依据）
    ESP_LOGI(TAG, "下载完成：实际接收 %zu 字节, Content-Length 声明 %d 字节", 
             total_read, content_length);
    if (content_length > 0 && total_read != (size_t)content_length) {
        ESP_LOGW(TAG, "注意：下载大小与Content-Length不一致，将通过固件完整性验证来判断");
    }

    // 8. 完成OTA（esp_ota_end会进行严格的固件完整性验证）
    // 包括：魔术字节、段头验证、SHA256校验和等
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end失败: %s", esp_err_to_name(err));
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "镜像验证失败");
        }
        goto cleanup;
    }

    // 9. 设置启动分区
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置启动分区失败: %s", esp_err_to_name(err));
        goto cleanup;
    }

    ESP_LOGI(TAG, "OTA 升级成功，总大小: %.2f KB", total_read / 1024.0);

            // 调用进度回调 - 完成
            if (progress_cb) {
        progress_cb(HTTP_OTA_EVENT_FINISH, total_read, total_read, user_data);
            }

            // 自动重启
            if (config->auto_reboot) {
                ESP_LOGI(TAG, "3 秒后重启...");
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_restart();
            }

cleanup:
    // 清理资源
    if (buffer) {
        free(buffer);
    }
    if (http_client) {
        http_client_close(http_client);
        http_client_destroy(http_client);
    }
    if (ota_handle) {
        esp_ota_abort(ota_handle);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA 升级失败");
        if (progress_cb) {
            progress_cb(HTTP_OTA_EVENT_FAILED, 0, 0, user_data);
        }
        
        // OTA失败后重启
        ESP_LOGE(TAG, "OTA失败，3秒后重启...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    return err;
}

/**
 * @brief HTTP OTA 升级任务
 */
static void http_ota_task(void *pvParameter)
{
    http_ota_task_params_t *params = (http_ota_task_params_t *)pvParameter;

    // 执行 OTA
    esp_err_t ret = http_ota_perform(&params->config, params->progress_cb, params->user_data);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA 任务执行失败");
    }

    // 释放参数内存
    free(params);

    // 删除任务
    vTaskDelete(NULL);
}

/**
 * @brief 初始化 HTTP OTA 模块
 */
esp_err_t http_ota_init(void)
{
    ESP_LOGI(TAG, "HTTP OTA 模块初始化");

    // 打印当前分区信息
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "当前运行分区: %s (偏移: 0x%08"PRIx32", 大小: 0x%08"PRIx32")",
             running->label, running->address, running->size);

    const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
    ESP_LOGI(TAG, "启动分区: %s (偏移: 0x%08"PRIx32", 大小: 0x%08"PRIx32")",
             boot_partition->label, boot_partition->address, boot_partition->size);

    return ESP_OK;
}

/**
 * @brief 启动 HTTP OTA 升级 (同步)
 */
esp_err_t http_ota_start(const http_ota_config_t *config,
                         http_ota_progress_cb_t progress_cb,
                         void *user_data)
{
    if (config == NULL || config->url == NULL) {
        ESP_LOGE(TAG, "配置参数无效");
        return ESP_ERR_INVALID_ARG;
    }

    return http_ota_perform(config, progress_cb, user_data);
}

/**
 * @brief 启动 HTTP OTA 升级任务 (异步)
 */
esp_err_t http_ota_start_async(const http_ota_config_t *config,
                               http_ota_progress_cb_t progress_cb,
                               void *user_data)
{
    if (config == NULL || config->url == NULL) {
        ESP_LOGE(TAG, "配置参数无效");
        return ESP_ERR_INVALID_ARG;
    }

    // 分配任务参数
    http_ota_task_params_t *params = (http_ota_task_params_t *)malloc(sizeof(http_ota_task_params_t));
    if (params == NULL) {
        ESP_LOGE(TAG, "分配内存失败");
        return ESP_ERR_NO_MEM;
    }

    // 复制配置
    params->config = *config;
    params->progress_cb = progress_cb;
    params->user_data = user_data;

    // 创建任务（提高优先级确保OTA下载不被阻塞）
    BaseType_t ret = xTaskCreate(
                         &http_ota_task,
                         "http_ota_task",
                         8192,  // 栈大小
                         params,
                         10,    // 优先级（提高到10，高于大多数任务）
                         NULL
                     );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建 OTA 任务失败");
        free(params);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA 任务已创建");
    return ESP_OK;
}

/**
 * @brief 获取当前运行的固件版本
 */
const char *http_ota_get_version(void)
{
    static char version_buf[32] = {0};  // 使用静态缓冲区
    const esp_partition_t *running = esp_ota_get_running_partition();

    // 检查分区指针是否有效
    if (running == NULL) {
        return "未知版本";
    }

    esp_app_desc_t running_app_info;

    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        // 复制版本字符串到静态缓冲区
        strncpy(version_buf, running_app_info.version, sizeof(version_buf) - 1);
        version_buf[sizeof(version_buf) - 1] = '\0';  // 确保NULL结尾
        return version_buf;
    }

    return "未知版本";
}

/**
 * @brief 检查是否支持 OTA 回滚
 */
bool http_ota_rollback_is_possible(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        return (ota_state == ESP_OTA_IMG_PENDING_VERIFY);
    }

    return false;
}

/**
 * @brief 标记当前固件为有效 (防止回滚)
 */
esp_err_t http_ota_mark_app_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "固件已标记为有效，取消回滚");
                return ESP_OK;
            } else {
                ESP_LOGE(TAG, "标记固件有效失败");
                return err;
            }
        }
    }

    ESP_LOGI(TAG, "当前固件无需标记 (已经有效)");
    return ESP_OK;
}

/**
 * @brief 比较两个版本号 (格式: x.y.z)
 */
int http_ota_compare_version(const char *v1, const char *v2)
{
    if (v1 == NULL || v2 == NULL) {
        return 0;
    }

    int v1_major = 0, v1_minor = 0, v1_patch = 0;
    int v2_major = 0, v2_minor = 0, v2_patch = 0;

    // 解析版本号 v1
    sscanf(v1, "%d.%d.%d", &v1_major, &v1_minor, &v1_patch);

    // 解析版本号 v2
    sscanf(v2, "%d.%d.%d", &v2_major, &v2_minor, &v2_patch);

    // 比较主版本号
    if (v1_major != v2_major) {
        return v1_major - v2_major;
    }

    // 比较次版本号
    if (v1_minor != v2_minor) {
        return v1_minor - v2_minor;
    }

    // 比较补丁版本号
    return v1_patch - v2_patch;
}

/**
 * @brief 检查云端是否有新版本（使用适配器层，支持WiFi和4G）
 */
esp_err_t http_ota_check_version(const char *version_url,
                                 http_ota_version_check_cb_t callback,
                                 void *user_data)
{
    if (version_url == NULL || callback == NULL) {
        ESP_LOGE(TAG, "参数无效");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "正在检查云端固件版本...");
    ESP_LOGI(TAG, "版本API地址: %s", version_url);

    esp_err_t err = ESP_OK;
    http_client_handle_t http_client = NULL;
    char *response_buffer = NULL;

    // 准备接收缓冲区
    response_buffer = (char *)malloc(1024);
    if (response_buffer == NULL) {
        ESP_LOGE(TAG, "分配内存失败");
        return ESP_ERR_NO_MEM;
    }
    memset(response_buffer, 0, 1024);

    // 创建HTTP客户端
    http_client_config_t config = {
        .url = version_url,
        .timeout_ms = 15000,
    };

    http_client = http_client_create(&config);
    if (!http_client) {
        ESP_LOGE(TAG, "创建HTTP客户端失败");
        free(response_buffer);
        return ESP_FAIL;
    }

    // 打开连接
    err = http_client_open(http_client, "GET");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "连接失败: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // 检查HTTP状态码
    int status_code = http_client_get_status_code(http_client);
    int content_length = http_client_get_content_length(http_client);
    ESP_LOGI(TAG, "HTTP状态码: %d, 内容长度: %d", status_code, content_length);

    if (status_code == 200) {
        // 读取响应体
        int read_len = http_client_read(http_client, response_buffer, 1023);
        if (read_len > 0) {
            response_buffer[read_len] = '\0';
            ESP_LOGI(TAG, "接收到的JSON: %s", response_buffer);

            // 解析JSON
            cJSON *root = cJSON_Parse(response_buffer);
            if (root != NULL) {
                http_ota_cloud_version_t cloud_version = {0};

                // 提取版本号
                cJSON *version = cJSON_GetObjectItem(root, "version");
                if (version != NULL && cJSON_IsString(version)) {
                    strncpy(cloud_version.version, version->valuestring, sizeof(cloud_version.version) - 1);
                    cloud_version.version[sizeof(cloud_version.version) - 1] = '\0';
                }

                // 提取下载URL
                cJSON *url = cJSON_GetObjectItem(root, "url");
                if (url != NULL && cJSON_IsString(url)) {
                    strncpy(cloud_version.download_url, url->valuestring, sizeof(cloud_version.download_url) - 1);
                    cloud_version.download_url[sizeof(cloud_version.download_url) - 1] = '\0';
                }

                // 提取更新说明
                cJSON *description = cJSON_GetObjectItem(root, "description");
                if (description != NULL && cJSON_IsString(description)) {
                    strncpy(cloud_version.description, description->valuestring, sizeof(cloud_version.description) - 1);
                    cloud_version.description[sizeof(cloud_version.description) - 1] = '\0';
                }

                // 提取是否强制更新
                cJSON *force = cJSON_GetObjectItem(root, "force");
                if (force != NULL && cJSON_IsBool(force)) {
                    cloud_version.force_update = cJSON_IsTrue(force);
                }

                // 获取当前版本
                const char *current_version = http_ota_get_version();
                ESP_LOGI(TAG, "当前版本: %s", current_version);
                ESP_LOGI(TAG, "云端版本: %s", cloud_version.version);

                // 比较版本
                int cmp = http_ota_compare_version(cloud_version.version, current_version);
                bool has_update = (cmp > 0);

                if (has_update) {
                    ESP_LOGW(TAG, "🆕 发现新版本: %s -> %s", current_version, cloud_version.version);
                    if (cloud_version.description[0] != '\0') {
                        ESP_LOGI(TAG, "更新说明: %s", cloud_version.description);
                    }
                    if (cloud_version.force_update) {
                        ESP_LOGW(TAG, "⚠️ 这是强制更新");
                    }
                } else {
                    ESP_LOGI(TAG, "✅ 已是最新版本");
                }

                // 调用回调函数
                callback(has_update, &cloud_version, user_data);

                cJSON_Delete(root);
                err = ESP_OK;
            } else {
                ESP_LOGE(TAG, "解析JSON失败");
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "读取响应失败");
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP请求失败，状态码: %d", status_code);
        err = ESP_FAIL;
    }

cleanup:
    // 清理资源
    if (http_client) {
        http_client_close(http_client);
        http_client_destroy(http_client);
    }
    if (response_buffer) {
        free(response_buffer);
    }

    return err;
}

