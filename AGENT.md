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
- 行选择 A0/A1/A2 → GPIO 11 / 9 / 8
- 列输入 Y0–Y6 → GPIO 13 / 15 / 3 / 4 / 5 / 6 / 7（下拉输入）
- 原理：A0–A2 选行 → 74HC138 输出低电平 → 列检测按键
- 参考：[社区实现](https://community.m5stack.com/topic/5865/)、原理图 `M5Cardputer.pdf`

**显示** — ST7789V2，240×135 横屏，SPI2_HOST：
- MOSI/SCLK/CS/DC/RST/BL → GPIO 35 / 36 / 37 / 34 / 33 / 38

**官方组件** — [M5Unified + M5GFX](https://github.com/m5stack/M5Unified)（Registry `m5stack/m5unified`，含 Cardputer 支持）

## Commit 规范

格式 `[Step X.X] <英文简述>`，例如：

```
[Step 0.1] Rewrite keyboard: implement 74HC138 GPIO matrix scan
[Step 1.2] Implement minimax_global provider
[Step 2.1] Refactor main.c: multi-provider home screen
```

## 参考

- M5Stack 文档 — https://docs.m5stack.com
- M5Unified — https://github.com/m5stack/M5Unified
- Cardputer 键盘讨论 — https://community.m5stack.com/topic/5865/
- Cardputer 原理图 `M5Cardputer.pdf` — M5Stack 官网下载
