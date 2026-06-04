# Provisioning Guide

QuotaPuter stores Wi-Fi and provider credentials **only on the device**, in a
dedicated encrypted-capable NVS partition. Nothing sensitive lives in the repo
or the firmware image. You write credentials over USB with
`tools/quota_config.py`, or set Wi-Fi directly on the device keyboard.

## 1. Build & flash

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

The Cardputer enumerates as a USB-Serial-JTAG port (e.g. `/dev/tty.usbmodem1101`
on macOS, `/dev/ttyACM0` on Linux, `COMx` on Windows).

## 2. Configure Wi-Fi

On the device: press **W** on the overview, type the SSID, press **Enter**, type
the password, press **Enter** to save and connect.

Or over USB:

```bash
python tools/quota_config.py --port /dev/tty.usbmodemXXXX set-wifi
# prompts for SSID and password (password is not echoed)
```

## 3. Add providers

The PC tool reads keys without echoing them and never stores them on disk; the
device runs an immediate read-only test query and reports `CONNECTED` or the
error.

### Direct-mode providers (MiniMax CN/Global, Kimi)

These use your own low-privilege key written to the device:

```bash
python tools/quota_config.py --port /dev/tty.usbmodemXXXX add-provider minimax_cn
python tools/quota_config.py --port /dev/tty.usbmodemXXXX add-provider minimax_global
python tools/quota_config.py --port /dev/tty.usbmodemXXXX add-provider kimi
# or pass non-interactively:
python tools/quota_config.py --port /dev/tty.usbmodemXXXX add-provider kimi --key sk-...
```

| Provider | Key to use | Endpoint queried |
| --- | --- | --- |
| `minimax_cn` | MiniMax **Subscription Key** (Token Plan) | `https://www.minimaxi.com/v1/token_plan/remains` |
| `minimax_global` | MiniMax **Subscription Key** (Token Plan) | `https://www.minimax.io/v1/token_plan/remains` |
| `kimi` | Moonshot **API Key** | `https://api.moonshot.cn/v1/users/me/balance` |

> Use the **Subscription Key** for MiniMax (from Token Plan / 订阅管理), not the
> pay-as-you-go API key — they are different keys.

### Relay-mode providers (OpenAI, Anthropic, Gemini)

High-privilege org/admin credentials should **never** be stored on the device.
Run your own relay that holds those credentials, talks to the official org/usage
APIs, and returns the standardized record below. The device stores only the
relay URL and a read-only device token.

```bash
python tools/quota_config.py --port /dev/tty.usbmodemXXXX add-provider openai \
    --relay-url https://your-relay.example.com/openai
# you'll be prompted for an optional read-only relay device token
```

Your relay must answer each `GET <relay_url>` with:

```json
{
  "provider": "openai",
  "metric_type": "usage",
  "title": "OpenAI API",
  "used": 12.30,
  "limit": null,
  "unit": "USD",
  "percentage": null,
  "reset_at": null,
  "updated_at": "2026-06-01T12:00:00Z",
  "status": "ok"
}
```

`status` may be `ok`, `auth`, `no_permission`, `stale`, `offline`, or `error`.
`metric_type` is `usage` (OpenAI/Anthropic) or `project` (Gemini).

## 4. Inspect, remove, reset

```bash
python tools/quota_config.py --port /dev/tty.usbmodemXXXX list
python tools/quota_config.py --port /dev/tty.usbmodemXXXX remove-provider minimax_cn
python tools/quota_config.py --port /dev/tty.usbmodemXXXX factory-reset
```

On the device you can also remove the selected provider by **holding D** on the
overview, and wipe **all** credentials + Wi-Fi by **holding Fn+Del** (overview or
device-info screen).

## Protocol reference

The device speaks a newline-delimited JSON protocol over USB-Serial-JTAG; each
reply line is prefixed with `#QP ` so it is distinguishable from log output.
See `components/provisioner` for the command set
(`hello`/`list`/`set_wifi`/`add_provider`/`remove_provider`/`factory_reset`).
