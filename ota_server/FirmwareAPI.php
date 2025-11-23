<?php
/**
 * OTA 固件管理 API（简化版，供 xn_ota_manger/ota_server 使用）
 */

header('Content-Type: application/json; charset=utf-8');
 session_start();
 if (empty($_SESSION['logged_in'])) {
     echo json_encode(['success' => false, 'message' => '未登录或登录已过期']);
     exit;
 }

$action = $_GET['action'] ?? '';
$firmware_dir = __DIR__ . '/firmware';

// 确保 firmware 目录存在
if (!is_dir($firmware_dir)) {
    mkdir($firmware_dir, 0755, true);
}

switch ($action) {
    case 'save_config':
        saveConfig();
        break;

    case 'upload':
        uploadFirmware();
        break;

    case 'delete':
        deleteFirmware();
        break;

    default:
        echo json_encode(['success' => false, 'message' => '无效的操作']);
        break;
}

/**
 * 保存 OTA 配置，生成 firmware/version.json
 */
function saveConfig()
{
    global $firmware_dir;

    $input = file_get_contents('php://input');
    $config = json_decode($input, true);

    if (!$config) {
        echo json_encode(['success' => false, 'message' => '无效的JSON数据']);
        return;
    }

    if (empty($config['version']) || empty($config['url'])) {
        echo json_encode(['success' => false, 'message' => '版本号和URL不能为空']);
        return;
    }

    // 与 http_ota_module.h 中的说明保持兼容：
    // {"version":"1.0.1","url":"http://xxx/firmware.bin","description":"修复bug","force":false}
    $version_config = [
        'version' => $config['version'],
        'url' => $config['url'],
        'description' => $config['description'] ?? '',
        'force' => $config['force'] ?? false,
    ];

    $version_file = $firmware_dir . '/version.json';
    $json = json_encode(
        $version_config,
        JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES
    );

    if (file_put_contents($version_file, $json) !== false) {
        echo json_encode(['success' => true, 'message' => '配置保存成功']);
    } else {
        echo json_encode(['success' => false, 'message' => '文件写入失败']);
    }
}

/**
 * 上传固件（仅允许 .bin，默认最大 10MB）
 */
function uploadFirmware()
{
    global $firmware_dir;

    if (!isset($_FILES['firmware'])) {
        echo json_encode(['success' => false, 'message' => '没有上传文件']);
        return;
    }

    $file = $_FILES['firmware'];

    if ($file['error'] !== UPLOAD_ERR_OK) {
        echo json_encode(['success' => false, 'message' => '上传失败，错误代码: ' . $file['error']]);
        return;
    }

    $ext = strtolower(pathinfo($file['name'], PATHINFO_EXTENSION));
    if ($ext !== 'bin') {
        echo json_encode(['success' => false, 'message' => '只支持.bin格式的固件文件']);
        return;
    }

    // 最大 10MB，可根据需要自行调大
    $max_size = 10 * 1024 * 1024;
    if ($file['size'] > $max_size) {
        echo json_encode(['success' => false, 'message' => '文件太大，最大支持10MB']);
        return;
    }

    // 生成安全文件名
    $filename = basename($file['name']);
    $filename = preg_replace('/[^a-zA-Z0-9._-]/', '_', $filename);

    $target_path = $firmware_dir . '/' . $filename;
    if (file_exists($target_path)) {
        $name = pathinfo($filename, PATHINFO_FILENAME);
        $filename = $name . '_' . time() . '.bin';
        $target_path = $firmware_dir . '/' . $filename;
    }

    if (move_uploaded_file($file['tmp_name'], $target_path)) {
        @chmod($target_path, 0644);

        $scheme = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off') ? 'https' : 'http';
        $host = $_SERVER['HTTP_HOST'] ?? '';
        $url = $scheme . '://' . $host . '/firmware/' . $filename;

        echo json_encode([
            'success' => true,
            'message' => '上传成功',
            'filename' => $filename,
            'size' => filesize($target_path),
            'url' => $url,
        ]);
    } else {
        echo json_encode(['success' => false, 'message' => '文件移动失败']);
    }
}

/**
 * 删除固件文件（不允许删除 version.json）
 */
function deleteFirmware()
{
    global $firmware_dir;

    $filename = $_GET['filename'] ?? '';

    if ($filename === '') {
        echo json_encode(['success' => false, 'message' => '文件名不能为空']);
        return;
    }

    // 目录遍历防护
    $safe_name = basename($filename);
    if ($safe_name !== $filename) {
        echo json_encode(['success' => false, 'message' => '无效的文件名']);
        return;
    }

    if ($safe_name === 'version.json') {
        echo json_encode(['success' => false, 'message' => '不能删除配置文件']);
        return;
    }

    $filepath = $firmware_dir . '/' . $safe_name;

    if (!file_exists($filepath)) {
        echo json_encode(['success' => false, 'message' => '文件不存在']);
        return;
    }

    if (unlink($filepath)) {
        echo json_encode(['success' => true, 'message' => '删除成功']);
    } else {
        echo json_encode(['success' => false, 'message' => '删除失败']);
    }
}
