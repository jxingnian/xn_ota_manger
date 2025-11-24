 # xn_ota_manger：ESP32 OTA 管理示例工程

 本仓库是一个基于 **ESP-IDF** 的 ESP32S3 OTA 管理示例工程，包含：

 - **HTTP OTA 管理组件**：统一从云端 `version.json` 拉取版本信息并执行 OTA 升级；
 - **Web WiFi 管理组件**：封装 WiFi 自动重连 + AP + Web 配网；
 - **PHP OTA 固件管理小网站**：用于在服务器上管理固件与生成 `version.json`。

 > 目标是提供一个 "开箱即用" 的 OTA 方案示例：
 >
 > 设备只需要配置一个 `version_url`，其它升级逻辑（HTTP 拉取 JSON、版本比较、下载与刷写固件、可选自动重启）全部由组件内部完成。

 ---

 ## 1. 目录结构

 仓库主要目录如下：

 ```text
 xn_ota_manger/
 ├─ main/                         # 示例应用入口（app_main），演示如何使用组件
 │  └─ main.c                     # 初始化 WiFi 管理，联网后初始化 HTTP OTA 管理
 ├─ components/
 │  ├─ xn_ota_manger/             # HTTP OTA 管理组件
 │  │  ├─ include/
 │  │  │  └─ http_ota_manager.h   # 对外接口与使用说明
 │  │  └─ src/
 │  │     └─ http_ota_manager.c   # 组件实现
 │  └─ xn_web_wifi_manger/        # Web WiFi 管理组件
 │     ├─ include/
 │     │  └─ xn_wifi_manage.h     # WiFi 管理对外接口
 │     └─ src/                    # 内部实现（WiFi 状态机、配网 Web 等）
 ├─ ota_server/                   # PHP OTA 固件管理小网站（部署在服务器，用于生成 version.json）
 │  └─ README_bt_ota_server.md    # 详细部署与使用说明
 ├─ partitions.csv                # 自定义分区表（包含 OTA 分区）
 ├─ sdkconfig                     # 当前工程的 sdkconfig
 └─ sdkconfig.defaults            # 默认配置（目标芯片、Flash、OTA 相关配置）
 ```

 ---

 ## 2. 依赖环境

 - **芯片平台**：ESP32S3
 - **开发框架**：ESP-IDF（建议使用官方最新稳定版，已安装并配置好 `idf.py` 环境）
 - **构建方式**：CMake + `idf.py`

 `sdkconfig.defaults` 中已预先配置了一些关键选项，例如：

 ```text
 CONFIG_IDF_TARGET="esp32s3"
 CONFIG_PARTITION_TABLE_CUSTOM=y
 CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
 CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y
 CONFIG_APP_PROJECT_VER_FROM_CONFIG=y
 CONFIG_APP_PROJECT_VER="1.0.3"
 ```

 其中：

 - `CONFIG_APP_PROJECT_VER` 为本地固件版本号，HTTP OTA 组件会用它与云端版本号比较；
 - `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y` 允许通过 HTTP 执行 OTA，便于在内网 / 测试环境使用（线上环境仍建议使用 HTTPS）。

 ---

 ## 3. 功能概览

 ### 3.1 HTTP OTA 管理组件（`components/xn_ota_manger`）

 对外头文件：`components/xn_ota_manger/include/http_ota_manager.h`

 核心能力：

 - 从指定的 `version_url` 拉取 JSON 配置：

   ```json
   {"version":"1.0.4","url":"http://your-domain/firmware/xxx.bin","description":"第一次优化","force":true}
   ```

 - 使用 `CONFIG_APP_PROJECT_VER` 作为本地版本号，与 JSON 中的 `version` 比较；
 - 当远端版本较新时，按 `url` 下载固件并执行 OTA 升级；
 - 升级成功后可选自动重启（由配置中的 `auto_reboot` 控制）；
 - 若已是最新版本，仅记录远端信息并返回成功。

 典型使用（简化版）：

 ```c
 http_ota_manager_config_t cfg = HTTP_OTA_MANAGER_DEFAULT_CONFIG();
 snprintf(cfg.version_url,
          sizeof(cfg.version_url),
          "http://your-domain.com/firmware/version.json");
 // cfg.state_cb = my_ota_state_cb; // 如需状态回调可自行设置
 http_ota_manager_init(&cfg);
 http_ota_manager_check_now();
 ```

 > 详细字段与行为，请参考 `http_ota_manager.h` 中的中文注释。

 ### 3.2 Web WiFi 管理组件（`components/xn_web_wifi_manger`）

 对外头文件：`components/xn_web_wifi_manger/include/xn_wifi_manage.h`

 主要特性：

 - 封装 WiFi STA 连接与自动重连；
 - 内置 AP + Web 配网页面，可通过手机连接到 ESP32 AP 进行 WiFi 配置；
 - 通过 `wifi_event_cb_t` 回调向上层报告 WiFi 状态：
   - `WIFI_MANAGE_STATE_CONNECTED`：已连接并获取 IP；
   - `WIFI_MANAGE_STATE_DISCONNECTED`：已断开；
   - `WIFI_MANAGE_STATE_CONNECT_FAILED`：本轮候选 WiFi 全部失败。

 默认配置（摘自宏 `WIFI_MANAGE_DEFAULT_CONFIG()`）：

 ```c
 .ap_ssid     = "XN-ESP32-AP",
 .ap_password = "12345678",
 .ap_ip       = "192.168.4.1",
 .web_port    = 80,
 ```

 你可以在此基础上只改关心的字段：

 ```c
 wifi_manage_config_t wifi_cfg = WIFI_MANAGE_DEFAULT_CONFIG();
 wifi_cfg.wifi_event_cb = wifi_manage_event_cb;  // 用于接收 WiFi 状态
 esp_err_t ret = wifi_manage_init(&wifi_cfg);
 ```

 ### 3.3 PHP OTA 管理小网站（`ota_server/`）

 - 使用 PHP（无需数据库）；
 - 提供后台登录、固件上传、固件列表、删除、编辑版本配置等能力；
 - 生成与 HTTP OTA 组件完全兼容的 `firmware/version.json`；
 - 默认后台账号：`admin` / `admin123`（部署后建议立刻在“账号设置”中修改）。

 > 详细部署及使用方法请查看：`ota_server/README_bt_ota_server.md`。

 ---

 ## 4. 示例应用流程（`main/main.c`）

 `main/main.c` 展示了如何将 **WiFi 管理** 与 **HTTP OTA 管理** 组件串联起来：

 1. 初始化 WiFi 管理组件：

    ```c
    wifi_manage_config_t wifi_cfg = WIFI_MANAGE_DEFAULT_CONFIG();
    wifi_cfg.wifi_event_cb        = wifi_manage_event_cb;
    esp_err_t ret = wifi_manage_init(&wifi_cfg);
    ```

 2. 当 WiFi 管理回调状态为 `WIFI_MANAGE_STATE_CONNECTED`（已拿到 IP）时，在单独任务中初始化 OTA 管理：

    ```c
    static void wifi_manage_event_cb(wifi_manage_state_t state)
    {
        if (state != WIFI_MANAGE_STATE_CONNECTED || s_ota_inited) {
            return;
        }

        xTaskCreate(ota_init_task, "ota_init", 1024*8, NULL,
                    tskIDLE_PRIORITY + 2, NULL);
        s_ota_inited = true;
    }
    ```

 3. OTA 初始化任务中配置 `version_url` 并发起一次检查：

    ```c
    static void ota_init_task(void *arg)
    {
        http_ota_manager_config_t cfg = HTTP_OTA_MANAGER_DEFAULT_CONFIG();
        snprintf(cfg.version_url,
                 sizeof(cfg.version_url),
                 "http://your-domain.com/firmware/version.json");

        esp_err_t ret = http_ota_manager_init(&cfg);
        if (ret == ESP_OK) {
            http_ota_manager_check_now();
        }

        vTaskDelete(NULL);
    }
    ```

 整套流程为：**设备上电 → WiFi 管理连上路由器 → 初始化 OTA 管理 → 拉取 version.json 并决定是否升级**。

 ---

 ## 5. HTTP OTA 使用步骤（端到端）

 ### 步骤 1：部署 OTA 管理网站

 1. 在服务器（例如宝塔面板）上新建一个站点，PHP 环境即可；
 2. 将本仓库 `ota_server/` 目录下的全部文件上传到网站根目录；
 3. 首次访问会跳转到后台登录页，使用默认账号登录并尽快修改用户名/密码；
 4. 通过 Web 页面上传 `.bin` 固件，并在“当前 OTA 配置”中填写：
    - 版本号 `version`；
    - 固件下载地址 `url`（通常是该站点下的 `/firmware/xxxx.bin`）；
    - 更新说明 `description`；
    - 是否强制更新 `force`；
 5. 保存后会在服务器生成 `/firmware/version.json`。

 > 详细操作步骤、截图与常见问题请阅读：`ota_server/README_bt_ota_server.md`。

 ### 步骤 2：在设备端配置 `version_url`

 在 `main/main.c` 中，将 `cfg.version_url` 改为你在网页上看到的“设备访问地址”，例如：

 ```c
 snprintf(cfg.version_url,
          sizeof(cfg.version_url),
          "http://your-domain.com/firmware/version.json");
 ```

 重新编译并烧录固件后，设备联网时会访问该 URL：

 1. 获取 `version.json`；
 2. 比较本地版本（`CONFIG_APP_PROJECT_VER`）与远端版本；
 3. 若远端版本更高，则按 `url` 下载并执行 OTA 升级。

 ### 步骤 3：升级与调试

 - 可以通过串口日志观察 OTA 过程：
   - 远端版本信息、下载进度、是否需要升级等；
 - 若升级失败：
   - 检查 `version.json` 是否为合法 JSON，且字段类型正确；
   - 确认固件 URL 可在浏览器中直接下载；
   - 查看服务器是否允许通过 HTTP 访问（或改用 HTTPS 并配置证书）。

 ---

 ## 6. 常见问题（简要）

 - **Q: 本地版本号从哪里来？**
   - A: 由 `CONFIG_APP_PROJECT_VER` 提供，你可以在 `sdkconfig.defaults` 中修改默认值，或通过 `menuconfig` 更新。

 - **Q: 一定要使用 HTTP 吗？**
   - A: 组件支持 `esp_https_ota`，当前示例里开启了 `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP` 方便测试。若在公网使用，建议改为 HTTPS 并正确配置服务器证书校验策略。

 - **Q: WiFi 配网页面怎么进入？**
   - A: 默认会开启名为 `XN-ESP32-AP` 的 AP，密码 `12345678`，IP `192.168.4.1`，你可以连接该 AP 并在浏览器访问 `http://192.168.4.1/` 进入 Web 配网页面（具体以 `xn_web_wifi_manger` 组件实现为准）。

 ---

 后续如果你对某个组件有更细的接口文档需求（例如仅单独集成 `http_ota_manager` 到自己的工程），可以在 `components/xn_ota_manger/include/http_ota_manager.h` 的基础上扩展一份单独的 README，我也可以帮你整理。

