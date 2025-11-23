/*
 * @Author: xingnian
 * @Date: 2025-10-29
 * @Description: HTTP客户端（基于esp_http_client）
 * 
 * 统一网络架构：WiFi和4G（通过USB RNDIS）都使用此实现
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP客户端句柄（不透明指针）
 */
typedef void* http_client_handle_t;

/**
 * @brief HTTP客户端配置
 */
typedef struct {
    const char *url;              ///< URL地址
    int timeout_ms;               ///< 超时时间（毫秒）
} http_client_config_t;

/**
 * @brief 创建HTTP客户端
 * 
 * @param config 配置参数
 * @return http_client_handle_t 客户端句柄，失败返回NULL
 */
http_client_handle_t http_client_create(const http_client_config_t *config);

/**
 * @brief 销毁HTTP客户端
 * 
 * @param handle 客户端句柄
 */
void http_client_destroy(http_client_handle_t handle);

/**
 * @brief 设置HTTP请求头
 * 
 * @param handle 客户端句柄
 * @param key 头字段名
 * @param value 头字段值
 * @return esp_err_t ESP_OK成功
 */
esp_err_t http_client_set_header(http_client_handle_t handle, const char *key, const char *value);

/**
 * @brief 打开HTTP连接
 * 
 * @param handle 客户端句柄
 * @param method HTTP方法（"GET", "POST"等）
 * @return esp_err_t ESP_OK成功
 */
esp_err_t http_client_open(http_client_handle_t handle, const char *method);

/**
 * @brief 获取HTTP状态码
 * 
 * @param handle 客户端句柄
 * @return int HTTP状态码（如200, 404等）
 */
int http_client_get_status_code(http_client_handle_t handle);

/**
 * @brief 获取响应内容长度
 * 
 * @param handle 客户端句柄
 * @return int 内容长度（字节），-1表示未知
 */
int http_client_get_content_length(http_client_handle_t handle);

/**
 * @brief 读取响应数据
 * 
 * @param handle 客户端句柄
 * @param buffer 接收缓冲区
 * @param len 缓冲区大小
 * @return int 实际读取的字节数，0表示EOF，<0表示错误
 */
int http_client_read(http_client_handle_t handle, void *buffer, size_t len);

/**
 * @brief 关闭HTTP连接
 * 
 * @param handle 客户端句柄
 * @return esp_err_t ESP_OK成功
 */
esp_err_t http_client_close(http_client_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // HTTP_CLIENT_H

