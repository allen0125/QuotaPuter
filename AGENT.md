# QuotaPuter 开发指南

M5Stack Cardputer (ESP32-S3) 上的 LLM 额度查看固件，纯 **ESP-IDF** 开发，像素风 UI。
完整产品需求见 [docs/PRD.md](docs/PRD.md)。

## 开发原则

- **联网核实资料**：涉及 M5Stack 硬件或第三方 API 时先查官方文档，不得自行假设。
- **优先官方驱动**：UI、键盘、电池、传感器先用 M5Stack 官方库（M5Unified / M5GFX），不自行实现 GPIO/SPI/I2C 底层。
- **拒绝打桩**：空函数 / 固定返回值不算完成；目标是完整实现 PRD 功能并调用真实 API。
- **不硬编码密钥**：仓库、固件镜像、示例配置中不得出现任何真实凭证。

## 硬件参考 (Cardputer)

**键盘** — 74HC138 3-8 译码器扫描矩阵，**非 I2C**：
- 行选择 A0/A1/A2 → GPIO 11 / 9 / 8；列输入 Y0–Y6 → GPIO 13 / 15 / 3 / 4 / 5 / 6 / 7（下拉输入）
- 官方 `m5stack/m5cardputer` 的 `IOMatrixKeyboardReader` 已实现该 74HC138 矩阵扫描 —— 直接使用，**不自行重写**。

**显示** — ST7789V2，240×135 横屏，SPI2_HOST：MOSI/SCLK/CS/DC/RST/BL → GPIO 35 / 36 / 37 / 34 / 33 / 38。
由 M5GFX `autodetect()` 自动配置，`setRotation(1)` 得到 240×135 横屏。

**官方组件** — [M5Unified + M5GFX](https://github.com/m5stack/M5Unified)（Registry `m5stack/m5unified`，含 Cardputer 支持）。

## 架构决策 (Architecture Decisions)

1. **官方组件**：依赖 `m5stack/m5cardputer`（传递依赖 `m5stack/m5unified` → `m5stack/m5gfx`）。纯 ESP-IDF，**不含 Arduino**。显示、键盘、电池全部走官方驱动。
2. **语言分层**：数据/网络/存储/Provider 层用 **C**（`esp_http_client` / `cJSON` / `nvs` 友好，且 PRD §10.2 接口为 C）；UI/输入/应用层用 **C++**（M5Cardputer / M5GFX）。`main.cpp` 内 `extern "C" void app_main` 桥接。
3. **网络**：HTTPS 仅用 `esp_http_client` + `esp_crt_bundle`（Mozilla 根证书校验，禁止关闭 TLS）；超时 10s；失败重试 ≤1；并发 ≤2 Provider。
4. **加密 NVS**：应用层使用标准 NVS API；生产部署通过 **Flash Encryption** 透明加密整个 flash（含 NVS）。当 `CONFIG_NVS_ENCRYPTION` 开启时走 `nvs_flash_secure_init`，否则标准 init。细节写入 `docs/SECURITY.md`。
5. **配置写入**：经 **USB-Serial-JTAG** 控制台的行式 JSON 协议（`provisioner` 组件 ↔ `tools/quota_config.py`）。设备进入 `CONFIG MODE` 后按行收发命令。
6. **Provider 接口**：严格按 PRD §10.2 的 `quota_provider_t` / `quota_result_t`。注册表 `provider_registry` 持有全部 6 个 Provider 的静态实例，按 `secret_store` 中的 enabled 标记参与刷新。

## 已核实 API (Verified 2026-06-04，详见各 Provider 源码注释)

| Provider | 请求 | 授权 | 响应要点 |
| --- | --- | --- | --- |
| `minimax_cn` | `GET https://www.minimaxi.com/v1/token_plan/remains` | `Authorization: Bearer <Subscription Key>` | 官方仅公开请求；响应含 `base_resp{status_code,status_msg}` + `model_remains[]`（`usage_percent` 为**剩余**百分比，`start_time`/`end_time` 为窗口）。**防御式解析**。 |
| `minimax_global` | `GET https://www.minimax.io/v1/token_plan/remains` | 同上 | 同上（`.io` 镜像）。 |
| `kimi` | `GET https://api.moonshot.cn/v1/users/me/balance` | `Authorization: Bearer <API Key>` | 官方完整：`{code, scode, status, data:{available_balance, cash_balance, voucher_balance}}`（单位 ¥）。 |
| `openai` / `anthropic` / `gemini` | `GET <relay_url>`（Relay 模式） | `Authorization: Bearer <device token>` | PRD §6.2 标准化 JSON：`{provider, metric_type, title, used, limit, unit, percentage, reset_at, updated_at, status}`。 |

> MiniMax 与 Kimi 支持设备直连；OpenAI/Anthropic/Gemini 默认/强制 Relay（PRD §6.3）。详情页须打印对应免责角标（PRD §11）。

## 开发计划 (自底向上，分模块分 Commit)

> 每个 Phase 完成后 `idf.py build` 必须通过再 commit。Commit 用 `[Step X.X]`。验收对照 PRD §14。

**Phase 0 — 工程骨架**
- 0.1 顶层 `CMakeLists.txt`、`sdkconfig.defaults`(esp32s3/PSRAM/crt_bundle/USB-JTAG console)、`partitions.csv`(nvs+secret+factory)、`main/`(CMakeLists+idf_component.yml+main.cpp 最小启动画面)、`.gitignore`、`LICENSE`(MIT)。`idf.py set-target esp32s3 && idf.py build` 通过。

**Phase 1 — provider_core**（无外部依赖，最底层）
- 1.1 `quota_types.h`：`quota_result_t`、`quota_provider_t`、`quota_status_t`、`metric_type`、常量。
- 1.2 `provider_registry.{h,c}`：注册 6 Provider，按 enabled 过滤、按 id 查找、遍历刷新接口。

**Phase 2 — secret_store**（依赖 nvs）
- 2.1 每 Provider 配置读写：`enabled`、`auth_mode`、`secret`(direct) 或 `relay_url`+`token`(relay)；Wi-Fi SSID/密码；`secure init` 适配；全量擦除（factory reset）。屏蔽日志中的密钥。

**Phase 3 — cache**（依赖 nvs + quota_types）
- 3.1 每 Provider 最近一次成功 `quota_result_t` 存取（含 updated_at），启动加载；缓存不含完整密钥/响应头。

**Phase 4 — http_client**（依赖 esp_http_client + crt_bundle）
- 4.1 `https_get_json(url, bearer, out, out_cap, &http_status, &err)`：HTTPS GET、证书校验、10s 超时、1 次重试、上限缓冲、错误归类（OFFLINE/TIMEOUT/AUTH/SERVICE…）。

**Phase 5 — wifi_manager**（依赖 esp_wifi + secret_store）
- 5.1 STA 连接、自动重连、事件组（GOT_IP）、状态查询、断网检测、清除配置。

**Phase 6 — providers**（依赖 4/5/2/3 + provider_core）
- 6.1 `providers/minimax_common.{h,c}` + `minimax_cn` + `minimax_global`：token_plan 防御式解析，填 percentage/remaining/reset_at。
- 6.2 `providers/kimi`：balance 解析，available/cash/voucher。
- 6.3 `providers/relay_common` + `openai` + `anthropic` + `gemini`：解析 PRD §6.2 标准化 JSON；metric_type 与免责角标。

**Phase 7 — provisioner + PC 工具**（依赖 secret_store + 各 provider validate）
- 7.1 `provisioner`：USB-JTAG 行式 JSON 协议，命令 `hello/list/add-provider/remove-provider/set-wifi/factory-reset`；写入后即发只读测试查询，返回 `CONNECTED`/错误。
- 7.2 `tools/quota_config.py`：`add-provider/remove-provider/set-wifi/list/factory-reset`，pyserial 串口交互，密钥不回显不入库。

**Phase 8 — keyboard 输入层**（C++ 包装 M5Cardputer）
- 8.1 `keyboard`：把 `M5Cardputer.Keyboard.keysState()` 抽象为事件（LEFT/RIGHT/UP/DOWN/ENTER/BACK/REFRESH(R)/SETTINGS(S)/WIFI(W)/字符输入/Fn+Del），含长按 500ms 判定。

**Phase 9 — display UI 基元 + assets**（C++ 包装 M5GFX）
- 9.1 `assets`：6 个 Provider 的 16×16/24×24 像素 Logo（RGB565 数组，含 CN/GL 角标）+ 像素配色表。生成脚本不入密钥。
- 9.2 `display`：主题色（PRD §8.4 状态色）、进度条、状态角标（OK/ERR/STALE/SETUP）、卡片、标题栏、Wi-Fi 信号、像素字体封装。

**Phase 10 — 屏幕与应用状态机**（顶层，依赖全部）
- 10.1 启动画面 + 总览页（卡片轮播、←→ 切换、状态色、最近更新时间、WIFI 角标）。
- 10.2 详情页（主指标/进度条/重置窗口/刷新时间/错误信息/手动刷新/免责角标）。
- 10.3 Wi-Fi 配置页（键盘输入 SSID/密码、连接、清除）。
- 10.4 Provider 管理页（D 长按删除、Fn+Del 长按清空）+ 设置页（刷新间隔 1/5/15/30 分）+ 设备信息/安全清除页。
- 10.5 自动刷新调度（默认 5 分钟、可配置、并发≤2、失败退避、STALE 标记、手动 R 刷新）。

**Phase 11 — 文档/示例/资产合规**
- 11.1 `config/providers.example.json`、`config/wifi.example.json`、`docs/PROVISIONING.md`、`docs/SECURITY.md`、更新 `README.md`（修正显示参数为 ST7789V2 240×135、补 §13 全部条目）、`assets/logos/` 模板与 README 商标声明。

**Phase 12 — 集成与验收**
- 12.1 全量 `idf.py build` 通过；按 PRD §14 功能/安全/视觉验收清单逐项核对并在 README/commit 记录。

## Commit 规范

格式 `[Step X.X] <英文简述>`，例如：

```
[Step 0.1] Scaffold ESP-IDF project: CMake, partitions, M5Cardputer deps
[Step 1.1] provider_core: define quota_result_t and quota_provider_t
[Step 6.2] Implement kimi balance provider with real moonshot API
```

## 参考

- M5Stack 文档 — https://docs.m5stack.com ｜ M5Unified — https://github.com/m5stack/M5Unified
- M5Cardputer 键盘 — https://github.com/m5stack/M5Cardputer ｜ 社区键盘讨论 — https://community.m5stack.com/topic/5865/
- ESP Component Registry — https://components.espressif.com ｜ Cardputer 原理图 `M5Cardputer.pdf`（M5Stack 官网）
- MiniMax Token Plan FAQ — https://platform.minimaxi.com/docs/token-plan/faq ｜ Kimi 查询余额 — https://platform.kimi.com/docs/api/balance
