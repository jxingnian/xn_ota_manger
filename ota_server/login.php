<?php
session_start();

require_once __DIR__ . '/auth_config.php';

$creds = auth_load_credentials();
$USERNAME = $creds['username'];
$PASSWORD_HASH = $creds['password_hash'];

if (isset($_GET['logout'])) {
    $_SESSION = [];
    if (ini_get('session.use_cookies')) {
        $params = session_get_cookie_params();
        setcookie(session_name(), '', time() - 42000,
            $params['path'], $params['domain'],
            $params['secure'], $params['httponly']
        );
    }
    session_destroy();
    header('Location: login.php');
    exit;
}

if (!empty($_SESSION['logged_in'])) {
    header('Location: index.php');
    exit;
}

$error = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $user = $_POST['username'] ?? '';
    $pass = $_POST['password'] ?? '';

    if ($user === $USERNAME && password_verify($pass, $PASSWORD_HASH)) {
        $_SESSION['logged_in'] = true;
        header('Location: index.php');
        exit;
    } else {
        $error = '用户名或密码错误';
    }
}
?>
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>登录 - OTA管理</title>
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
            max-width:400px;
            background:#fff;
            border-radius:16px;
            box-shadow:0 20px 60px rgba(0,0,0,.3);
            padding:30px 24px 24px;
        }
        h1{
            font-size:1.6em;
            margin-bottom:10px;
            text-align:center;
            color:#111827;
        }
        p.subtitle{
            text-align:center;
            color:#6b7280;
            font-size:.9em;
            margin-bottom:24px;
        }
        .form-group{margin-bottom:18px}
        label{
            display:block;
            margin-bottom:6px;
            font-size:.9em;
            color:#374151;
        }
        input[type=text],input[type=password]{
            width:100%;
            padding:10px 12px;
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
            background:#667eea;
            color:#fff;
            font-size:1em;
            font-weight:500;
            cursor:pointer;
            transition:background .2s;
        }
        .btn-primary:hover{background:#5568d3}
        .error{
            background:#fee2e2;
            border:1px solid #fca5a5;
            color:#b91c1c;
            padding:8px 10px;
            border-radius:8px;
            font-size:.85em;
            margin-bottom:16px;
        }
        .hint{
            margin-top:14px;
            font-size:.8em;
            color:#6b7280;
            text-align:left;
        }
        .hint code{
            background:#f3f4f6;
            padding:1px 4px;
            border-radius:4px;
        }
    </style>
</head>
<body>
<div class="card">
    <h1>登录 OTA 管理</h1>
    <p class="subtitle">请输入管理员账号登录后管理固件</p>
    <?php if ($error !== ''): ?>
        <div class="error"><?php echo htmlspecialchars($error, ENT_QUOTES, 'UTF-8'); ?></div>
    <?php endif; ?>
    <form method="post" action="">
        <div class="form-group">
            <label for="username">用户名</label>
            <input type="text" id="username" name="username" required autocomplete="username">
        </div>
        <div class="form-group">
            <label for="password">密码</label>
            <input type="password" id="password" name="password" required autocomplete="current-password">
        </div>
        <button type="submit" class="btn-primary">登录</button>
    </form>
    <div class="hint">
        默认账号：<code>admin</code> / <code>admin123</code>，登录后可在“账号设置”页面修改用户名和密码。
    </div>
</div>
</body>
</html>
