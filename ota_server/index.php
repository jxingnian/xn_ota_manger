<?php
/**
 * ESP32 OTA 固件管理页面（独立版）
 *
 * 部署方式：
 *  - 将本目录作为网站根目录部署到宝塔站点；
 *  - 访问 http://your-domain/ 即可打开本页面；
 *  - 固件文件与 version.json 存放在 firmware/ 目录下。
 */
 session_start();
 if (empty($_SESSION['logged_in'])) {
     header('Location: login.php');
     exit;
 }

// 当前 OTA 配置
$version_file = __DIR__ . '/firmware/version.json';
$current_config = [
    'version' => '1.0.0',
    'url' => '',
    'description' => '',
    'force' => false,
];

if (file_exists($version_file)) {
    $json = file_get_contents($version_file);
    $data = json_decode($json, true);
    if ($data) {
        $current_config = array_merge($current_config, $data);
    }
}

// 获取固件列表
$firmware_dir = __DIR__ . '/firmware';
$firmware_files = [];
if (is_dir($firmware_dir)) {
    $files = scandir($firmware_dir);
    foreach ($files as $file) {
        if (pathinfo($file, PATHINFO_EXTENSION) === 'bin') {
            $filepath = $firmware_dir . '/' . $file;
            $firmware_files[] = [
                'name' => $file,
                'size' => filesize($filepath),
                'time' => filemtime($filepath),
            ];
        }
    }
    // 按时间倒序
    usort($firmware_files, function ($a, $b) {
        return $b['time'] - $a['time'];
    });
}
?>
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>OTA固件管理</title>
    <style>
        * {margin: 0; padding: 0; box-sizing: border-box;}
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            overflow: hidden;
        }
        header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 30px;
        }
        h1 {font-size: 2em; margin-bottom: 10px;}
        .back-btn {
            display: inline-block;
            color: white;
            text-decoration: none;
            padding: 8px 16px;
            background: rgba(255,255,255,0.2);
            border-radius: 6px;
            margin-top: 10px;
        }
        main {padding: 30px;}
        .section {
            background: #f9fafb;
            border-radius: 12px;
            padding: 25px;
            margin-bottom: 25px;
        }
        .section h2 {
            color: #667eea;
            margin-bottom: 20px;
            font-size: 1.5em;
        }
        .form-group {margin-bottom: 20px;}
        .form-group label {
            display: block;
            margin-bottom: 8px;
            font-weight: 600;
            color: #374151;
        }
        .form-group input[type="text"],
        .form-group input[type="number"],
        .form-group textarea {
            width: 100%;
            padding: 12px;
            border: 2px solid #e5e7eb;
            border-radius: 8px;
            font-size: 1em;
        }
        .form-group input:focus,
        .form-group textarea:focus {
            outline: none;
            border-color: #667eea;
        }
        .checkbox-group {
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .checkbox-group input[type="checkbox"] {
            width: 20px;
            height: 20px;
            cursor: pointer;
        }
        .btn {
            padding: 12px 24px;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            font-size: 1em;
            font-weight: 500;
            transition: all 0.3s;
        }
        .btn-primary {background: #667eea; color: white;}
        .btn-primary:hover {background: #5568d3;}
        .btn-success {background: #10b981; color: white;}
        .btn-success:hover {background: #059669;}
        .btn-danger {background: #ef4444; color: white;}
        .btn-danger:hover {background: #dc2626;}
        .btn-secondary {background: #6b7280; color: white;}
        .btn-secondary:hover {background: #4b5563;}
        .upload-area {
            border: 3px dashed #d1d5db;
            border-radius: 12px;
            padding: 40px;
            text-align: center;
            cursor: pointer;
            transition: all 0.3s;
        }
        .upload-area:hover {
            border-color: #667eea;
            background: #f3f4f6;
        }
        .upload-area.dragover {
            border-color: #667eea;
            background: #e0e7ff;
        }
        .file-input {display: none;}
        .firmware-list {margin-top: 20px;}
        .firmware-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 15px;
            background: white;
            border: 2px solid #e5e7eb;
            border-radius: 8px;
            margin-bottom: 10px;
        }
        .firmware-info {flex: 1;}
        .firmware-name {font-weight: 600; color: #374151; margin-bottom: 5px;}
        .firmware-meta {font-size: 0.9em; color: #6b7280;}
        .firmware-actions {display: flex; gap: 8px;}
        .btn-small {padding: 6px 12px; font-size: 0.9em;}
        .alert {
            padding: 15px;
            border-radius: 8px;
            margin-bottom: 20px;
        }
        .alert-success {background: #d1fae5; color: #065f46; border: 2px solid #10b981;}
        .alert-error {background: #fee2e2; color: #991b1b; border: 2px solid #ef4444;}
        .alert-info {background: #dbeafe; color: #1e40af; border: 2px solid #3b82f6;}
        .config-preview {
            background: #1f2937;
            color: #f3f4f6;
            padding: 20px;
            border-radius: 8px;
            font-family: 'Courier New', monospace;
            font-size: 0.9em;
            overflow-x: auto;
            margin-top: 15px;
        }
        .progress-bar {
            width: 100%;
            height: 30px;
            background: #e5e7eb;
            border-radius: 15px;
            overflow: hidden;
            margin: 15px 0;
            display: none;
        }
        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, #667eea, #764ba2);
            width: 0%;
            transition: width 0.3s;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-weight: 600;
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>📦 OTA固件管理</h1>
            <a href="account.php" class="back-btn">账号设置</a>
            <a href="login.php?logout=1" class="back-btn">退出登录</a>
        </header>

        <main>
            <!-- 当前配置 -->
            <div class="section">
                <h2>📝 当前OTA配置</h2>
                <div class="alert alert-info">
                    <strong>配置文件路径：</strong> /firmware/version.json<br>
                    <strong>设备访问地址：</strong>
                    <?php echo (isset($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off' ? 'https' : 'http') . '://' . $_SERVER['HTTP_HOST']; ?>/firmware/version.json
                </div>
                <p style="margin-top:8px; font-size:0.9em; color:#4b5563;">
                    将上述“设备访问地址”填写到设备侧的 <code>version_url</code> 即可，例如在 <code>ota_manage_config_t.version_url</code> 中使用。
                </p>

                <form id="configForm">
                    <div class="form-group">
                        <label>固件版本号 *</label>
                        <input type="text" name="version" value="<?php echo htmlspecialchars($current_config['version']); ?>" required placeholder="1.0.0">
                    </div>

                    <div class="form-group">
                        <label>固件下载URL *</label>
                        <input type="text" name="url" value="<?php echo htmlspecialchars($current_config['url']); ?>" required placeholder="http://your-server.com/firmware/firmware.bin">
                    </div>

                    <div class="form-group">
                        <label>更新说明</label>
                        <textarea name="description" rows="3" placeholder="修复了若干bug，优化了性能"><?php echo htmlspecialchars($current_config['description']); ?></textarea>
                    </div>

                    <div class="form-group">
                        <div class="checkbox-group">
                            <input type="checkbox" name="force" id="force" <?php echo $current_config['force'] ? 'checked' : ''; ?>>
                            <label for="force" style="margin: 0;">强制更新（设备将自动下载安装）</label>
                        </div>
                    </div>

                    <button type="submit" class="btn btn-primary">💾 保存配置</button>
                    <button type="button" class="btn btn-secondary" onclick="previewConfig()">👁️ 预览JSON</button>
                </form>

                <div id="configPreview" class="config-preview" style="display: none;"></div>
            </div>

            <!-- 上传固件 -->
            <div class="section">
                <h2>⬆️ 上传固件</h2>
                <div class="upload-area" id="uploadArea" onclick="document.getElementById('fileInput').click()">
                    <div style="font-size: 3em; margin-bottom: 10px;">📁</div>
                    <div style="font-size: 1.2em; color: #374151; margin-bottom: 5px;">点击或拖拽文件到此处上传</div>
                    <div style="color: #6b7280; font-size: 0.9em;">支持 .bin 格式的固件文件</div>
                </div>
                <input type="file" id="fileInput" class="file-input" accept=".bin" onchange="uploadFirmware(this.files[0])">

                <div class="progress-bar" id="progressBar">
                    <div class="progress-fill" id="progressFill">0%</div>
                </div>

                <div id="uploadMessage"></div>
            </div>

            <!-- 固件列表 -->
            <div class="section">
                <h2>📋 已上传的固件</h2>
                <?php if (empty($firmware_files)): ?>
                    <div class="alert alert-info">暂无固件文件</div>
                <?php else: ?>
                    <div class="firmware-list">
                        <?php foreach ($firmware_files as $file): ?>
                            <div class="firmware-item">
                                <div class="firmware-info">
                                    <div class="firmware-name">📦 <?php echo htmlspecialchars($file['name']); ?></div>
                                    <div class="firmware-meta">
                                        大小: <?php echo number_format($file['size'] / 1024, 2); ?> KB |
                                        上传时间: <?php echo date('Y-m-d H:i:s', $file['time']); ?>
                                    </div>
                                </div>
                                <div class="firmware-actions">
                                    <button class="btn btn-success btn-small" onclick="copyUrl('<?php echo htmlspecialchars($file['name']); ?>')">📋 复制URL</button>
                                    <button class="btn btn-danger btn-small" onclick="deleteFirmware('<?php echo htmlspecialchars($file['name']); ?>')">🗑️ 删除</button>
                                </div>
                            </div>
                        <?php endforeach; ?>
                    </div>
                <?php endif; ?>
            </div>
        </main>
    </div>

    <script>
        // 保存配置
        document.getElementById('configForm').addEventListener('submit', async (e) => {
            e.preventDefault();
            const formData = new FormData(e.target);
            const config = {
                version: formData.get('version'),
                url: formData.get('url'),
                description: formData.get('description'),
                force: formData.get('force') === 'on',
            };

            try {
                const res = await fetch('FirmwareAPI.php?action=save_config', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify(config),
                });
                const data = await res.json();

                if (data.success) {
                    showMessage('success', '✅ 配置保存成功！');
                } else {
                    showMessage('error', '❌ 保存失败: ' + data.message);
                }
            } catch (e) {
                showMessage('error', '❌ 保存失败: ' + e.message);
            }
        });

        // 预览配置
        function previewConfig() {
            const formData = new FormData(document.getElementById('configForm'));
            const config = {
                version: formData.get('version'),
                url: formData.get('url'),
                description: formData.get('description'),
                force: formData.get('force') === 'on',
            };

            const preview = document.getElementById('configPreview');
            preview.textContent = JSON.stringify(config, null, 4);
            preview.style.display = 'block';
        }

        // 上传固件
        async function uploadFirmware(file) {
            if (!file) return;

            if (!file.name.endsWith('.bin')) {
                showMessage('error', '❌ 只支持 .bin 格式的固件文件');
                return;
            }

            const formData = new FormData();
            formData.append('firmware', file);

            const progressBar = document.getElementById('progressBar');
            const progressFill = document.getElementById('progressFill');
            progressBar.style.display = 'block';

            try {
                const xhr = new XMLHttpRequest();

                xhr.upload.addEventListener('progress', (e) => {
                    if (e.lengthComputable) {
                        const percent = Math.round((e.loaded / e.total) * 100);
                        progressFill.style.width = percent + '%';
                        progressFill.textContent = percent + '%';
                    }
                });

                xhr.addEventListener('load', () => {
                    if (xhr.status === 200) {
                        const data = JSON.parse(xhr.responseText);
                        if (data.success) {
                            showMessage('success', '✅ 固件上传成功！');
                            setTimeout(() => location.reload(), 1500);
                        } else {
                            showMessage('error', '❌ 上传失败: ' + data.message);
                        }
                    } else {
                        showMessage('error', '❌ 上传失败');
                    }
                    progressBar.style.display = 'none';
                });

                xhr.addEventListener('error', () => {
                    showMessage('error', '❌ 上传失败');
                    progressBar.style.display = 'none';
                });

                xhr.open('POST', 'FirmwareAPI.php?action=upload');
                xhr.send(formData);

            } catch (e) {
                showMessage('error', '❌ 上传失败: ' + e.message);
                progressBar.style.display = 'none';
            }
        }

        // 删除固件
        async function deleteFirmware(filename) {
            if (!confirm('确定要删除固件 ' + filename + ' 吗？')) return;

            try {
                const res = await fetch('FirmwareAPI.php?action=delete&filename=' + encodeURIComponent(filename), {
                    method: 'DELETE',
                });
                const data = await res.json();

                if (data.success) {
                    showMessage('success', '✅ 删除成功！');
                    setTimeout(() => location.reload(), 1000);
                } else {
                    showMessage('error', ' 删除失败: ' + data.message);
                }
            } catch (e) {
                showMessage('error', ' 删除失败: ' + e.message);
            }
        }

        // 复制URL
        function copyUrl(filename) {
            const url = window.location.origin + '/firmware/' + filename;

            if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
                navigator.clipboard.writeText(url).then(() => {
                    showMessage('success', ' URL：' + url);
                }).catch(() => {
                    showMessage('error', ' 复制失败');
                });
                return;
            }

            const input = document.createElement('input');
            input.value = url;
            document.body.appendChild(input);
            input.select();
            input.setSelectionRange(0, input.value.length);

            try {
                const ok = document.execCommand('copy');
                if (ok) {
                    showMessage('success', ' URL：' + url);
                } else {
                    showMessage('error', ' 复制失败：' + url);
                }
            } catch (e) {
                showMessage('error', ' 复制失败：' + url);
            } finally {
                document.body.removeChild(input);
            }
        }

        // 显示消息
        function showMessage(type, message) {
            const msgDiv = document.getElementById('uploadMessage');
            msgDiv.className = 'alert alert-' + type;
            msgDiv.textContent = message;
            msgDiv.style.display = 'block';
            setTimeout(() => {
                msgDiv.style.display = 'none';
            }, 5000);
        }

        // 拖拽上传
        const uploadArea = document.getElementById('uploadArea');

        uploadArea.addEventListener('dragover', (e) => {
            e.preventDefault();
            uploadArea.classList.add('dragover');
        });

        uploadArea.addEventListener('dragleave', () => {
            uploadArea.classList.remove('dragover');
        });

        uploadArea.addEventListener('drop', (e) => {
            e.preventDefault();
            uploadArea.classList.remove('dragover');
            const file = e.dataTransfer.files[0];
            if (file) uploadFirmware(file);
        });
    </script>
</body>
</html>
