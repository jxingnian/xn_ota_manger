<?php
/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-23 18:51:41
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-23 18:52:17
 * @FilePath: \xn_ota_manger\ota_server\auth_config.php
 * @Description: 
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
/**
 * 简单账号存储：使用本地 auth.json 保存用户名与密码哈希
 */

function auth_get_config_file()
{
    return __DIR__ . '/auth.json';
}

function auth_load_credentials()
{
    $file = auth_get_config_file();

    if (is_file($file)) {
        $json = @file_get_contents($file);
        if ($json !== false) {
            $data = json_decode($json, true);
            if (is_array($data) && isset($data['username'], $data['password_hash'])) {
                return [
                    'username' => (string)$data['username'],
                    'password_hash' => (string)$data['password_hash'],
                ];
            }
        }
    }

    // 文件不存在或格式不正确时，使用默认账号
    $defaultUsername = 'admin';
    $defaultPassword = 'admin123';

    return [
        'username' => $defaultUsername,
        'password_hash' => password_hash($defaultPassword, PASSWORD_DEFAULT),
    ];
}

function auth_save_credentials($username, $password)
{
    $file = auth_get_config_file();

    $data = [
        'username' => (string)$username,
        'password_hash' => password_hash((string)$password, PASSWORD_DEFAULT),
    ];

    $json = json_encode($data, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE);

    return @file_put_contents($file, $json) !== false;
}
