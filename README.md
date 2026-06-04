# QuotaPuter

LLM quota / usage viewer for the **M5Stack Cardputer** (ESP32-S3).

A pixel-art firmware that shows your subscription quota, API usage, or account
balance across several official LLM platforms — built with pure **ESP-IDF**
(no Arduino).

![Version](https://img.shields.io/badge/version-v0.1.0-blue)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-green)
![Framework](https://img.shields.io/badge/framework-ESP--IDF-orange)

## What QuotaPuter can and cannot do

**Can:**

- ✅ Show MiniMax Token Plan remaining quota (China **and** International, as two
  separate providers)
- ✅ Show Kimi (Moonshot) API balance — available / cash / voucher
- ✅ Show OpenAI / Anthropic / Google-Gemini **org/API usage** via a relay you run
- ✅ Cache the last good result and show it offline, marked `STALE`
- ✅ Auto-refresh (1 / 5 / 15 / 30 min) and manual refresh

**Cannot:**

- ❌ Call LLMs or generate anything
- ❌ Read a personal **ChatGPT Plus**, **Claude Pro/Max**, or **Google AI** plan —
  those have no official public quota API
- ❌ Use unofficial APIs, web-login cookies, or scraping
- ❌ Store admin keys on the device for OpenAI/Anthropic/Gemini (use relay mode)

QuotaPuter only calls official, publicly documented endpoints.

## Supported providers

| Provider | Region / type | Shows | Auth |
| --- | --- | --- | --- |
| MiniMax | CN (Token Plan) | interval & weekly remaining %, reset window | Subscription Key (direct) |
| MiniMax | Global (Token Plan) | interval & weekly remaining %, reset window | Subscription Key (direct) |
| Kimi | API balance | available / cash / voucher (CNY) | API Key (direct) |
| OpenAI | API org usage | tokens / cost | Admin key via **relay** |
| Anthropic | Console org usage | usage / cost | Admin key via **relay** |
| Gemini | Google Cloud project | project usage / quota | service account via **relay** |

The OpenAI/Anthropic/Gemini screens are labelled `API USAGE - NOT CHATGPT PLAN`,
`ORG USAGE - NOT CLAUDE PRO/MAX`, and `CLOUD PROJECT - NOT GOOGLE AI PLAN`.

### MiniMax CN vs MiniMax Global

MiniMax is shown as **two independent providers** with separate credentials and
region badges:

| Provider id | Badge | API host |
| --- | --- | --- |
| `minimax_cn` | `CN` | `www.minimaxi.com` |
| `minimax_global` | `GL` | `www.minimax.io` |

They do not share keys; configure each separately. The Token Plan is measured as
a **remaining percentage** per window (a short "interval" cap and a "weekly" cap),
not an absolute token count — the card headlines the smaller (binding) of the two,
and the detail page lists both plus the reset time.

## Hardware

- **Device:** M5Stack Cardputer (ESP32-S3, 8 MB flash, no PSRAM)
- **Display:** ST7789V2, 240×135 landscape
- **Keyboard:** built-in 74HC138 matrix (56 keys), driven directly in ESP-IDF
- **Connectivity:** Wi-Fi (2.4 GHz), USB-Serial-JTAG for provisioning

## Build & flash

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor   # macOS: /dev/cu.usbmodem*; Linux: /dev/ttyACM*
```

The first build downloads the official `m5stack/m5unified` component (which pulls
`m5stack/m5gfx`) from the ESP Component Registry.

> The Cardputer's USB-Serial-JTAG port name can change after a flash or reset
> (e.g. `usbmodem1101` ↔ `usbmodem<serial>`). Run `ls /dev/cu.usbmodem*` to find
> the current one. If `idf.py flash` reports *"No serial data received"*, hold the
> **G0/BOOT** button while plugging in (or while tapping reset) to force download
> mode, then flash.

## Configure Wi-Fi & credentials

Set Wi-Fi on the device (press **W**) or over USB; add provider credentials over
USB with the bundled tool. Full walkthrough: **[docs/PROVISIONING.md](docs/PROVISIONING.md)**.

The port is auto-detected when there's a single board attached, so `--port` can
usually be omitted (handy because the USB-Serial-JTAG name changes across resets):

```bash
python tools/quota_config.py set-wifi
python tools/quota_config.py add-provider minimax_cn
python tools/quota_config.py add-provider openai --relay-url https://your-relay.example.com/openai
python tools/quota_config.py list
# pass --port /dev/cu.usbmodemXXXX explicitly if auto-detect fails
```

Keys are read without echo, sent to the device only, and never written to disk.
After writing a key the device runs a read-only test query and reports
`CONNECTED` or the error.

### Which providers should use a relay?

| Provider | Recommendation |
| --- | --- |
| MiniMax CN / Global, Kimi | Direct is fine (low-privilege user keys) |
| OpenAI, Anthropic | **Relay strongly recommended** (admin keys) |
| Gemini / Google Cloud | **Relay required** (service-account credentials) |

A relay is a small service you host that holds the high-privilege credentials and
returns a standardized JSON record; the device stores only the relay URL and a
read-only token. See [docs/PROVISIONING.md](docs/PROVISIONING.md) for the format.

## Controls (PRD §9)

The Cardputer has no dedicated arrow keys, so punctuation keys act as arrows.

| Key | Action |
| --- | --- |
| `,` / `/` | Previous / next provider |
| `;` / `.` | Up / down (menus) |
| `Enter` | Open detail / select |
| `Backspace` | Back / cancel |
| `R` | Refresh current provider |
| `S` | Settings |
| `W` | Wi-Fi setup |
| `D` (hold) | Delete the current provider's credentials |
| `Fn`+`Del` (hold) | Wipe all credentials and Wi-Fi |

### Status colours

| Colour / tag | Meaning |
| --- | --- |
| 🟢 Green | OK (<80% used) |
| 🟡 Yellow | Warning (≥80% used / low balance) |
| 🔴 Red `ERR` | Critical (≥95% used) or request failure |
| ⚪ Gray `STALE` | Showing cached data |
| ⚪ Gray `SETUP` | Not configured |

## Delete credentials / factory reset

- One provider: `quota_config.py remove-provider <id>`, or hold **D** on the
  overview.
- Everything (all secrets + Wi-Fi): `quota_config.py factory-reset`, or hold
  **Fn+Del** on the device.

## Privacy & security

- This repository, the firmware image, and the example configs contain **no**
  real keys, tokens, cookies, accounts, or passwords.
- QuotaPuter does **not** collect, upload, or embed any user credentials. Keys
  live only on your device (and, for relay mode, on your own relay server).
- Secrets are never printed in full on screen or in logs.
- All requests are HTTPS with certificate validation.

Details: **[docs/SECURITY.md](docs/SECURITY.md)**.

## Project structure

```
QuotaPuter/
├── main/                      # app entry, UI state machine, refresh scheduler
│   ├── main.cpp app.cpp refresh.cpp
├── components/
│   ├── provider_core/         # quota_result_t / provider vtable / registry
│   ├── secret_store/          # credential + Wi-Fi NVS storage
│   ├── cache/                 # offline result cache
│   ├── http_client/           # HTTPS GET + cert bundle
│   ├── wifi_manager/          # STA connect + reconnect
│   ├── providers/             # minimax / kimi / relay (openai,anthropic,gemini)
│   ├── provisioner/           # USB-Serial-JTAG config protocol
│   ├── keyboard/              # 74HC138 matrix scan
│   ├── display/               # pixel UI primitives (M5GFX)
│   └── assets/                # provider glyph styles
├── tools/quota_config.py      # PC provisioning tool
├── config/                    # *.example.json templates (no secrets)
└── docs/                      # PRD, PROVISIONING, SECURITY
```

## License

MIT — see [LICENSE](LICENSE).

## Trademarks

All brand names and logos are the property of their respective owners (MiniMax,
OpenAI, Anthropic, Google Gemini, Moonshot/Kimi). The pixel glyphs in this
project are used only to identify the corresponding official service and do not
imply sponsorship, endorsement, or affiliation. QuotaPuter is not affiliated with
any of these companies.

## Disclaimer

You are responsible for your own API keys, their permissions, your usage/costs,
and the physical security of the device. This firmware reads only publicly
documented endpoints and is provided without warranty.
