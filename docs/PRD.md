# QuotaPuter PRD

## 1. 产品概述

**QuotaPuter** 是一款运行在 **M5Stack Cardputer** 上的开源固件，用于查看各家官方 LLM 平台的订阅额度、API 用量或账户余额。

产品采用全像素风格界面，每家供应商使用其品牌 Logo 的像素化版本进行识别。项目为公开仓库，仓库中不得包含任何用户密钥、账号信息、Cookie、Token 或其他敏感数据。

## 2. 产品目标

* 在 Cardputer 上快速查看多个官方 LLM 平台的额度或用量状态。
* 优先支持可通过官方公开接口查询的供应商。
* 区分同一供应商的不同区域版本，例如 MiniMax 中国版与国际版。
* 采用统一、复古、清晰的像素风 UI。
* 提供安全、明确的授权和写入设备流程。
* 仅使用 **ESP-IDF** 开发，不使用 Arduino 框架。

## 3. 非目标

* 不支持 OpenRouter、ZenMux、API 中转站、第三方代理或聚合平台。
* 不通过抓包、网页登录 Cookie、模拟浏览器、逆向私有接口查询额度。
* 不保证读取 ChatGPT Plus、Claude Pro/Max、Google AI Pro 等个人聊天订阅剩余额度，除非供应商提供官方公开接口。
* 不在仓库、固件镜像或示例配置中预置任何真实凭证。
* 不提供模型调用、聊天或代码生成能力。

## 4. 支持的平台

| 平台            | 区域/类型                                  | 展示内容                    | 授权方式                   | 支持状态                      |
| ------------- | -------------------------------------- | ----------------------- | ---------------------- | ------------------------- |
| MiniMax       | 中国版 Token Plan                         | 剩余额度、用量比例、窗口状态、更新时间     | 用户自己的 Subscription Key | 支持                        |
| MiniMax       | 国际版 Token Plan                         | 剩余额度、用量比例、窗口状态、更新时间     | 用户自己的 Subscription Key | 支持                        |
| OpenAI        | API Platform Organization              | Token 用量、费用、时间区间        | Admin API Key          | 支持，不等同于 ChatGPT 订阅        |
| Anthropic     | Console Organization / Claude Code Org | 用量、费用或 Claude Code 使用报告 | Admin API Key          | 支持，不等同于个人 Claude 订阅       |
| Google Gemini | Google Cloud / Gemini API 项目           | 项目级用量或配额监控结果            | 推荐通过用户自建 Relay         | 支持，不等同于个人 Gemini 订阅       |
| Kimi          | API 开放平台                               | 可用余额、现金余额、代金券余额         | API Key                | 支持，不等同于 Kimi Code Plan 额度 |

### 4.1 MiniMax 区域区分

MiniMax 必须作为两个独立 Provider 展示：

| Provider ID      | 显示名称           | API Host           |
| ---------------- | -------------- | ------------------ |
| `minimax_cn`     | MiniMax CN     | `www.minimaxi.com` |
| `minimax_global` | MiniMax Global | `www.minimax.io`   |

示例查询路径：

```text
MiniMax CN:     https://www.minimaxi.com/v1/token_plan/remains
MiniMax Global: https://www.minimax.io/v1/token_plan/remains
```

两者分别保存授权配置、分别显示 Logo 角标，不共享凭证。

## 5. 核心功能

### 5.1 Provider 首页

首页以像素卡片形式展示已配置平台：

```text
┌────────────────────────┐
│ QUOTAPUTER        WIFI▮ │
├────────────────────────┤
│ [MM CN]  72% LEFT       │
│ [MM GL]  41% LEFT       │
│ [OPENAI] $12.30 USED    │
│ [CLAUDE] 1.2M TOKENS    │
│ [KIMI]  ¥48.60          │
└────────────────────────┘
```

每张卡片展示：

* 像素化 Logo。
* Provider 名称或区域角标。
* 主要额度指标。
* 状态颜色：正常、接近上限、已耗尽、请求失败。
* 最近成功更新时间。

### 5.2 Provider 详情页

点击某个平台后展示：

* 平台名称与像素 Logo。
* 当前额度、已使用量或余额。
* 用量进度条。
* 重置时间或查询周期，如接口返回该字段。
* 最近刷新时间。
* 请求状态和简化错误信息。
* 手动刷新按钮。

不同类型 Provider 的详情内容应适配数据能力：

| 类型      | 主指标              |
| ------- | ---------------- |
| 订阅额度型   | 剩余百分比、已用/总额、重置窗口 |
| API 用量型 | 今日/本周期 Token、费用  |
| 余额型     | 可用余额、现金余额、代金券余额  |
| 项目监控型   | 调用量、Token 或配额消耗  |

### 5.3 自动刷新

* 默认每 5 分钟自动刷新一次。
* 用户可在设置中选择：1、5、15、30 分钟。
* 支持手动刷新。
* 请求失败时保留最近一次成功数据，并标记为 `STALE`。
* 连续失败时采用退避机制，避免频繁请求官方接口。

### 5.4 离线缓存

* 最近一次成功查询结果保存到 NVS。
* 无网络时仍显示缓存结果。
* 所有缓存结果必须显示最后更新时间。
* 缓存不得包含完整密钥或完整响应头。

### 5.5 Wi-Fi 配置

* 首次启动进入配置页面。
* 支持通过 Cardputer 键盘输入 Wi-Fi SSID 与密码。
* Wi-Fi 信息保存于设备本地 NVS。
* 提供清除 Wi-Fi 配置功能。

## 6. 授权与安全设计

### 6.1 基本原则

* QuotaPuter 只调用供应商官方公开接口。
* 用户必须自行在对应官方平台创建所需凭证。
* 仓库中仅提供配置模板和字段说明，不提供真实 Key。
* 日志中不得打印完整 Token、Cookie、Authorization Header 或 Wi-Fi 密码。
* 屏幕中不得显示完整密钥，只允许显示状态或末尾 4 位。

### 6.2 授权模式

固件支持两种授权模式。

#### 模式 A：设备直连模式

适用于个人使用且权限较低的 Key，例如 MiniMax Subscription Key、Kimi API Key。

流程：

1. 用户从官方平台创建或复制自己的 Key。
2. 使用 QuotaPuter 配置工具，通过 USB 串口写入 Cardputer。
3. 固件将凭证存入加密 NVS。
4. 设备通过 HTTPS 直接请求官方接口。

限制：

* 对组织管理员 Key 风险较高。
* 设备丢失后可能存在密钥泄露风险。
* 不推荐存储 OpenAI Admin Key、Anthropic Admin Key 或 Google Cloud 高权限凭证。

#### 模式 B：Relay 模式，推荐

适用于高权限凭证或复杂 OAuth 平台。

流程：

1. 用户自行部署一个轻量 Relay 服务。
2. OpenAI、Anthropic、Google Cloud 等高权限凭证只保存在 Relay 服务器。
3. Cardputer 中只保存 Relay 地址和只读设备令牌。
4. Cardputer 从 Relay 获取标准化用量结果。

Relay 返回统一格式：

```json
{
  "provider": "openai",
  "metric_type": "usage",
  "title": "OpenAI API",
  "used": 12.3,
  "limit": null,
  "unit": "USD",
  "percentage": null,
  "reset_at": null,
  "updated_at": "2026-06-01T12:00:00Z",
  "status": "ok"
}
```

### 6.3 Provider 授权要求

| Provider                | 推荐授权方式           | 设备直连 | Relay 推荐程度 |
| ----------------------- | ---------------- | ---- | ---------- |
| MiniMax CN              | Subscription Key | 支持   | 可选         |
| MiniMax Global          | Subscription Key | 支持   | 可选         |
| Kimi API                | API Key          | 支持   | 可选         |
| OpenAI API Organization | Admin API Key    | 不推荐  | 强烈推荐       |
| Anthropic Organization  | Admin API Key    | 不推荐  | 强烈推荐       |
| Gemini / Google Cloud   | OAuth 或服务账号      | 不支持  | 必须         |

### 6.4 公开仓库安全要求

仓库必须包含：

```text
config/providers.example.json
config/wifi.example.json
docs/PROVISIONING.md
docs/SECURITY.md
assets/logos/
```

仓库不得包含：

```text
config/providers.json
config/wifi.json
*.key
*.pem
.env
sdkconfig 中的真实账号或密钥
串口写入产生的本地 secrets 文件
任何真实 API 响应样本中的用户信息
```

`.gitignore` 至少包含：

```gitignore
.env
*.key
*.pem
secrets/
config/providers.json
config/wifi.json
build/
sdkconfig
```

## 7. 配置与写入 Cardputer

### 7.1 开发环境

* 开发框架：ESP-IDF。
* 芯片平台：ESP32-S3。
* 构建工具：`idf.py`。
* 依赖管理：ESP-IDF Component Manager 或仓库内组件。
* 不使用 Arduino、PlatformIO 或依赖 Arduino 运行时的库。

### 7.2 固件烧录

基础烧录流程：

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

### 7.3 凭证写入方式

凭证不得硬编码进源码或编译产物模板。

提供一个本地配置命令行工具：

```bash
python tools/quota_config.py \
  --port /dev/tty.usbmodemXXXX \
  add-provider minimax_cn
```

工具交互输入：

```text
Provider: MiniMax CN
Authorization Type: Subscription Key
Paste Key: ********
Save to device? [y/N]
```

写入流程：

1. Cardputer 通过 USB 串口进入 `CONFIG MODE`。
2. PC 端工具发送 Provider 配置。
3. 固件校验字段格式。
4. 凭证写入加密 NVS。
5. 固件立即发起一次只读查询测试。
6. 屏幕显示 `CONNECTED` 或错误原因。

也支持删除单一 Provider：

```bash
python tools/quota_config.py \
  --port /dev/tty.usbmodemXXXX \
  remove-provider minimax_cn
```

支持彻底清除设备配置：

```bash
python tools/quota_config.py \
  --port /dev/tty.usbmodemXXXX \
  factory-reset
```

### 7.4 示例配置文件

仓库仅提供无敏感信息的示例配置：

```json
{
  "providers": [
    {
      "id": "minimax_cn",
      "enabled": false,
      "auth_mode": "direct",
      "secret_source": "provisioned_on_device"
    },
    {
      "id": "openai",
      "enabled": false,
      "auth_mode": "relay",
      "relay_url": "https://your-relay.example.com/openai"
    }
  ]
}
```

## 8. UI 与视觉规范

### 8.1 设计风格

* 整体为复古掌机风、8-bit 像素风。
* 使用低分辨率像素图标、硬边框、网格化布局。
* 禁止使用模糊阴影、圆角渐变、高清矢量风格元素。
* 所有页面使用统一像素字体。

### 8.2 Logo 规范

每个 Provider 必须展示对应的像素化 Logo：

| Provider           | Logo 展示要求                     |
| ------------------ | ----------------------------- |
| MiniMax CN         | MiniMax 像素 Logo + `CN` 角标     |
| MiniMax Global     | MiniMax 像素 Logo + `GL` 角标     |
| OpenAI             | OpenAI 品牌识别图形的像素版             |
| Anthropic / Claude | Claude 或 Anthropic 品牌识别图形的像素版 |
| Gemini             | Gemini 品牌识别图形的像素版             |
| Kimi               | Kimi 品牌识别图形的像素版               |

资产要求：

* Logo 使用单色或少色像素重绘。
* 每个图标提供 `16x16` 与 `24x24` 两种尺寸。
* 资产文件使用明确命名，例如：

```text
assets/logos/minimax_cn_16.png
assets/logos/minimax_global_16.png
assets/logos/openai_16.png
assets/logos/claude_16.png
assets/logos/gemini_16.png
assets/logos/kimi_16.png
```

* README 中声明各品牌 Logo 与商标归原权利人所有，本项目仅用于平台识别。
* 发布前检查各品牌资产的公开仓库再分发要求；如存在限制，则仅保留由用户自行导入的资产模板。

### 8.3 页面结构

页面包括：

1. 启动画面：QuotaPuter Logo 与固件版本。
2. Provider 总览页。
3. Provider 详情页。
4. Wi-Fi 设置页。
5. Provider 管理页。
6. 刷新频率与显示设置页。
7. 安全清除与设备信息页。

### 8.4 状态颜色

| 状态             | 显示         |
| -------------- | ---------- |
| 正常             | 绿色像素条      |
| 用量超过 80%       | 黄色像素条      |
| 用量超过 95% 或余额不足 | 红色像素条      |
| 请求失败           | 红色 `ERR`   |
| 使用缓存           | 灰色 `STALE` |
| 未授权            | 灰色 `SETUP` |

## 9. 操作方式

| 操作              | 行为               |
| --------------- | ---------------- |
| 左右键             | 切换 Provider 卡片   |
| Enter           | 打开详情页            |
| Backspace / Esc | 返回               |
| `R`             | 手动刷新当前 Provider  |
| `S`             | 打开设置             |
| `W`             | 打开 Wi-Fi 配置      |
| `D` 长按          | 删除当前 Provider 配置 |
| `Fn + Del` 长按确认 | 清除全部敏感配置         |

## 10. 技术架构

### 10.1 固件组件

```text
main/
components/
  display/           屏幕驱动与像素 UI
  keyboard/          Cardputer 键盘输入
  wifi_manager/      Wi-Fi 配置与重连
  http_client/       HTTPS 请求封装
  secret_store/      NVS 凭证保存与读取
  provider_core/     Provider 通用接口
  providers/
    minimax_cn/
    minimax_global/
    openai/
    anthropic/
    gemini/
    kimi/
  cache/             最近查询结果缓存
  provisioner/       USB 串口配置协议
  assets/            字体、Logo、图标
tools/
  quota_config.py    本地配置工具
docs/
  PROVISIONING.md
  SECURITY.md
```

### 10.2 Provider 标准接口

每个 Provider 实现统一接口：

```c
typedef struct {
    const char *id;
    const char *display_name;
    const char *region;
    const char *metric_type;
    esp_err_t (*fetch_usage)(quota_result_t *result);
    esp_err_t (*validate_config)(void);
} quota_provider_t;
```

统一数据结构：

```c
typedef struct {
    char provider_id[32];
    char title[32];
    char unit[16];
    float used;
    float limit;
    float remaining;
    float percentage;
    int64_t reset_at;
    int64_t updated_at;
    bool has_limit;
    bool cached;
    int status;
} quota_result_t;
```

### 10.3 网络要求

* 所有请求必须使用 HTTPS。
* 校验证书，不允许关闭 TLS 校验。
* 请求超时默认 10 秒。
* 单次刷新不得并发请求超过 2 个 Provider。
* 失败重试最多 1 次。
* HTTP 响应只解析必要字段，不长期保存完整响应体。

## 11. Provider 行为说明

### 11.1 MiniMax CN / Global

* 使用用户提供的 Subscription Key。
* 查询 Token Plan 剩余额度。
* 展示剩余额度、使用比例、重置窗口和状态。
* 中国版与国际版必须完全独立配置。

### 11.2 OpenAI

* 仅对接官方 API Platform Organization Usage / Costs 数据。
* 使用 Admin API Key 时，默认要求 Relay 模式。
* 展示 Token 消耗、费用和统计周期。
* 页面中明确标记：

```text
API USAGE · NOT CHATGPT PLAN
```

### 11.3 Anthropic

* 仅支持组织账户的官方 Admin API 用量能力。
* Claude Code 组织报告可作为单独指标展示。
* 默认要求 Relay 模式。
* 页面中明确标记：

```text
ORG USAGE · NOT CLAUDE PRO/MAX
```

### 11.4 Gemini

* 仅展示 Google Cloud 项目可读取的 Gemini API 或相关配额监控数据。
* 必须采用 Relay 模式，不在 Cardputer 保存 Google Cloud 服务账号密钥。
* 页面中明确标记：

```text
CLOUD PROJECT · NOT GOOGLE AI PLAN
```

### 11.5 Kimi

* 使用官方 API Key 查询开放平台可用余额。
* 展示可用余额、现金余额与代金券余额。
* 页面中明确标记：

```text
API BALANCE
```

## 12. 错误处理

| 场景        | UI 提示            |
| --------- | ---------------- |
| 未配置凭证     | `SETUP REQUIRED` |
| Key 无效或过期 | `AUTH FAILED`    |
| 权限不足      | `NO PERMISSION`  |
| 官方接口不可用   | `SERVICE ERROR`  |
| 网络断开      | `OFFLINE`        |
| 接口超时      | `TIMEOUT`        |
| 本地缓存可用    | 显示旧数据并标记 `STALE` |
| 响应格式变化    | `API CHANGED`    |

错误日志要求：

* 只记录 Provider ID、HTTP 状态码、错误类型、时间。
* 不记录 Authorization Header。
* 不记录完整请求体或完整响应体。
* 不记录 Wi-Fi 密码。

## 13. README 使用说明必须包含的内容

README 必须说明：

1. QuotaPuter 能查询什么，不能查询什么。
2. 支持的官方 Provider 列表。
3. MiniMax CN 与 MiniMax Global 的区别。
4. 如何使用 ESP-IDF 构建和烧录。
5. 如何通过 USB 配置 Wi-Fi。
6. 如何通过 USB 写入用户自己的 Provider 凭证。
7. 哪些 Provider 推荐使用 Relay。
8. 如何删除凭证与恢复出厂设置。
9. 仓库不会收集、上传或内置任何用户密钥。
10. Logo 和商标归原品牌方所有。

## 14. 验收标准

### 功能验收

* 固件可在 Cardputer 上启动并正常操作。
* 仅使用 ESP-IDF 构建成功。
* 可配置 Wi-Fi 并自动联网。
* 可添加、删除至少 6 个 Provider 配置：

  * MiniMax CN
  * MiniMax Global
  * OpenAI
  * Anthropic
  * Gemini
  * Kimi
* MiniMax 中国版与国际版可分别查询和展示状态。
* 首页可展示所有已启用 Provider 的最新数据。
* 详情页可查看单个平台信息并手动刷新。
* 离线时可展示最近一次缓存数据。
* 错误场景有清晰状态提示。

### 安全验收

* 公开仓库内不存在任何真实 Key、Token、Cookie、账号或密码。
* 示例配置均为占位内容。
* 日志不输出完整敏感信息。
* 用户可通过设备或工具清除全部凭证。
* 高权限 Provider 明确推荐或强制使用 Relay 模式。
* 不使用非官方接口或网页登录态抓取。

### 视觉验收

* 全部页面采用统一像素风。
* 每个 Provider 卡片均展示像素化 Logo。
* MiniMax CN 与 MiniMax Global 具有清晰区域区分。
* 状态颜色、进度条、错误标记清晰可辨。
* 小屏显示无文字溢出，核心额度信息无需滚动即可查看。

## 15. 开源许可与声明

* 项目代码建议采用 MIT License。
* 第三方品牌名称与商标仅用于标识对应官方服务。
* 像素化 Logo 不代表品牌方赞助、认证或合作。
* 用户自行负责其凭证权限、额度消耗与设备安全。
* 项目不保存、不代理、不上传用户密钥，除非用户自行配置 Relay 服务。

