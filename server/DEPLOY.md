# 账号服务器部署（ly.xzcqgzs.cn，宝塔面板）

这套代码是 0.4.2 登录功能的服务端：注册、登录、每次启动校验、登出，外加一个网页后台。
**还没有在真实服务器上测试**（开发环境没有 PHP 和 MySQL），第一次部署后请按第 6 节做一遍检查。

## 1. 宝塔里要有的环境

- PHP **8.0 或更高**，启用扩展 `pdo_mysql`（默认一般已开）。
- MySQL **8.0**。
- 站点 `ly.xzcqgzs.cn`，已申请 SSL 证书，强制 HTTPS。

## 2. 建数据库

1. 宝塔 → 数据库 → 添加数据库：名称 `totk_accounts`，用户 `totk_accounts`，设一个强密码（记下来）。字符集 `utf8mb4`。
2. 打开 phpMyAdmin，选中这个库，导入 `server/schema.sql`。

## 3. 上传代码

把仓库里的 `server/` 目录放到服务器上，**网站根目录指向 `server/public/`**（不要指向 `server/`，否则配置文件会被下载）。宝塔里：网站 → 设置 → 网站目录 → 运行目录，选 `/public`，或直接把网站根目录设成 `/www/wwwroot/ly.xzcqgzs.cn/public`。之后访问地址是 `https://ly.xzcqgzs.cn/admin.php`，不是 `/public/admin.php`。

```
server/
  src/common.php        （不要让外部访问，放在 public 外面）
  schema.sql            （只导入一次）
  config.example.php    （复制成 config.php 再填）
  config.php            （只在服务器上，不进 Git）
  public/api.php        （App 访问：/api.php?action=...）
  public/admin.php      （网页后台：/admin.php）
```

`src/` 和 `config.php` 都在 `public/` 的**上一级**，只有 `public/` 对外可见。`public/api.php` 通过 `../src/common.php` 读取代码，`common.php` 通过 `../config.php` 读取配置，所以 `config.php` 必须正好放在 `server/config.php`。

## 4. 写配置

`config.php` 必须是下面这种格式：第一行是 `<?php`，中间用 `return [ ... ];` 返回配置数组。**少了 `return`，或者前面有空行、文字，都会报错。**（报错 `cfg(): Return value must be of type array, int returned` 就是这个原因。）


复制 `config.example.php` 为 `config.php`，只需要改数据库密码：

- `db.pass`：第 2 步的数据库密码。

`max_devices`（每账号设备数，默认 2）和 `token_days`（登录有效天数，默认 30）可以按需改。后台的管理员不在配置文件里设置。

## 5. 首次设置管理员（尽快做）

打开 `https://ly.xzcqgzs.cn/admin.php`。**第一次打开时，页面会让你输入一个账户和密码，输入的这个账户就是管理员。** 之后只能用这个账户和密码登录后台。

- 账户 3–20 位（字母、数字、下划线），密码至少 8 位。
- 这一步没有任何人拦截：**部署好以后马上打开一次完成设置**，避免别人先于你设置。
- 设置完成后，后台的其他账户登录不了，只有管理员账户能进。

进后台后，在“注册口令”里设置一个至少 8 位的口令。**没设置前注册是关闭的。** 口令只以哈希保存，之后随时可以在后台改。

后台还可以：禁用或启用账号、清空某账号的设备绑定（用户换手机时用）。

## 6. 上线前检查（curl 测试）

把 `USER`、`PASS`、`PHRASE` 换成真实值，设备 id 随便一个 16 位以上的字符串：

```
# 注册（PHRASE 是后台设的注册口令）
curl -s -X POST 'https://ly.xzcqgzs.cn/api.php?action=register' \
  -d '{"username":"test01","password":"test123","passphrase":"PHRASE"}'
# 期望：{"ok":true}

# 登录（得到 token）
curl -s -X POST 'https://ly.xzcqgzs.cn/api.php?action=login' \
  -d '{"username":"test01","password":"test123","device_id":"devicetest000000001"}'
# 期望：{"ok":true,"token":"64位十六进制","username":"test01"}

# 校验（把上一步的 token 填进去）
curl -s -X POST 'https://ly.xzcqgzs.cn/api.php?action=check' \
  -d '{"device_id":"devicetest000000001","token":"TOKEN"}'
# 期望：{"ok":true}
```

再用两个不同的设备 id 登录第 3 次，应得到 `{"ok":false,"error":"device limit"}`（HTTP 403）。

确认 `https://ly.xzcqgzs.cn/src/common.php` 和 `https://ly.xzcqgzs.cn/config.php` 访问不到（应为 404）。

测完在后台删掉测试账号。

## 7. 接口说明（给客户端）

所有接口都是 `POST`，请求体是 JSON。

| action | 请求字段 | 成功返回 | 常见错误 |
|---|---|---|---|
| `register` | `username`, `password`, `passphrase` | `{"ok":true}` | 503 注册关闭，401 口令错，409 用户名已占用 |
| `login` | `username`, `password`, `device_id` | `{"ok":true,"token","username"}` | 401 账号或密码错，403 设备数已满或账号被禁用 |
| `check` | `device_id`, `token` | `{"ok":true}` | 401 未登录或已过期，403 被禁用 |
| `logout` | `device_id`, `token` | `{"ok":true}` | — |

用户名 3–20 位（字母、数字、下划线）；密码 6–72 字节；设备 id 16–64 位（字母、数字、`_`、`-`）。

## 8. 安全说明

- 密码和 token 都只存哈希；数据库里没有明文。
- 登录失败会停一秒，减缓暴力尝试。这只是基础防护，不是完整的限流。
- 后台用 Cookie（HttpOnly、Secure、SameSite=Strict）和 CSRF 令牌保护；管理员密码只以哈希保存。
- 首次设置管理员没有额外的口令保护，部署后请立即完成设置。
- 客户端代码是公开仓库的一部分，任何人都能读到校验流程。服务器校验能挡住“只改客户端就进去”，挡不住“有决心的人直接抓包/改代码绕过”。
