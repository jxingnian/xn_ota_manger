<?php
session_start();

if (empty($_SESSION['logged_in'])) {
    header('Location: login.php');
    exit;
}

require_once __DIR__ . '/auth_config.php';

$creds = auth_load_credentials();
$current_username = $creds['username'];
$current_hash = $creds['password_hash'];

$error = '';
$message = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $old_pass = $_POST['current_password'] ?? '';
    $new_user = trim($_POST['new_username'] ?? '');
    $new_pass = $_POST['new_password'] ?? '';
    $confirm = $_POST['confirm_password'] ?? '';

    if ($new_user === '') {
        $error = '新用户名不能为空';
    } elseif (strlen($new_user) > 32) {
        $error = '新用户名过长（最多32个字符）';
    } elseif ($new_pass === '') {
        $error = '新密码不能为空';
    } elseif (strlen($new_pass) < 4) {
        $error = '新密码长度至少为 4 位';
    } elseif ($new_pass !== $confirm) {
        $error = '两次输入的新密码不一致';
    } elseif (!password_verify($old_pass, $current_hash)) {
        $error = '当前密码错误';
    } else {
        if (auth_save_credentials($new_user, $new_pass)) {
            $message = '账号信息已更新，请下次使用新用户名和密码登录。';
            $current_username = $new_user;
        } else {
            $error = '保存失败，请检查服务器写权限。';
        }
    }
}
?>
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>账号设置 - OTA管理</title>
    <style>
        *{margin:0;padding:0;box-sizing:border-box}
        body{
            font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
            background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);
            min-height:100vh;
            display:flex;
            align-items:center;
            justify-content:center;
            padding:20px;
        }
        .card{
            width:100%;
            max-width:480px;
            background:#fff;
            border-radius:16px;
            box-shadow:0 20px 60px rgba(0,0,0,.3);
            padding:26px 22px 22px;
        }
        h1{
            font-size:1.5em;
            margin-bottom:6px;
            color:#111827;
            text-align:center;
        }
        p.subtitle{
            text-align:center;
            color:#6b7280;
            font-size:.9em;
            margin-bottom:18px;
        }
        .form-group{margin-bottom:14px}
        label{
            display:block;
            margin-bottom:6px;
            font-size:.9em;
            color:#374151;
        }
        input[type=text],input[type=password]{
            width:100%;
            padding:9px 11px;
            border:2px solid #e5e7eb;
            border-radius:8px;
            font-size:.95em;
        }
        input:focus{
            outline:none;
            border-color:#667eea;
        }
        .btn-primary{
            width:100%;
            padding:10px 16px;
            border:none;
            border-radius:999px;
            background:#10b981;
            color:#fff;
            font-size:1em;
            font-weight:500;
            cursor:pointer;
            transition:background .2s;
            margin-top:4px;
        }
        .btn-primary:hover{background:#059669}
        .link-row{
            display:flex;
            justify-content:space-between;
            align-items:center;
            margin-bottom:10px;
            font-size:.85em;
        }
        .link-row a{
            color:#4f46e5;
            text-decoration:none;
        }
        .link-row a:hover{text-decoration:underline}
        .error{
            background:#fee2e2;
            border:1px solid #fca5a5;
            color:#b91c1c;
            padding:8px 10px;
            border-radius:8px;
            font-size:.85em;
            margin-bottom:10px;
        }
        .success{
            background:#dcfce7;
            border:1px solid #86efac;
            color:#166534;
            padding:8px 10px;
            border-radius:8px;
            font-size:.85em;
            margin-bottom:10px;
        }
        .readonly{
            background:#f9fafb;
        }
    </style>
</head>
<body>
<div class="card">
    <h1>账号设置</h1>
    <p class="subtitle">修改 OTA 管理后台的登录用户名与密码</p>
    <div class="link-row">
        <span>当前登录中</span>
        <span>
            <a href="index.php">返回管理首页</a> ·
            <a href="login.php?logout=1">退出登录</a>
        </span>
    </div>
    <?php if ($error !== ''): ?>
        <div class="error"><?php echo htmlspecialchars($error, ENT_QUOTES, 'UTF-8'); ?></div>
    <?php elseif ($message !== ''): ?>
        <div class="success"><?php echo htmlspecialchars($message, ENT_QUOTES, 'UTF-8'); ?></div>
    <?php endif; ?>
    <form method="post" action="">
        <div class="form-group">
            <label>当前用户名</label>
            <input type="text" value="<?php echo htmlspecialchars($current_username, ENT_QUOTES, 'UTF-8'); ?>" class="readonly" readonly>
        </div>
        <div class="form-group">
            <label for="current_password">当前密码 *</label>
            <input type="password" id="current_password" name="current_password" required>
        </div>
        <div class="form-group">
            <label for="new_username">新用户名 *</label>
            <input type="text" id="new_username" name="new_username" required>
        </div>
        <div class="form-group">
            <label for="new_password">新密码 *</label>
            <input type="password" id="new_password" name="new_password" required>
        </div>
        <div class="form-group">
            <label for="confirm_password">确认新密码 *</label>
            <input type="password" id="confirm_password" name="confirm_password" required>
        </div>
        <button type="submit" class="btn-primary">保存修改</button>
    </form>
</div>
</body>
</html>
