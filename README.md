# QuotaPuter

LLM Subscription Quota Viewer for M5Stack Cardputer

A pixel-art style firmware to view subscription quotas and API usage from various LLM platforms.

![Version](https://img.shields.io/badge/version-v1.0.0-blue)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-green)
![Framework](https://img.shields.io/badge/framework-ESP--IDF-orange)

## Features

- 📊 View quota/usage for multiple LLM providers
- 🎨 Pixel-art UI design
- 🔐 Secure credential storage
- 📡 Wi-Fi connectivity
- 💾 Offline caching
- ⚡ Auto-refresh

## Supported Providers

| Provider | Region | Auth Mode | Notes |
|----------|--------|-----------|-------|
| MiniMax | CN | Direct | Token Plan subscription |
| MiniMax | Global | Direct | Token Plan subscription |
| OpenAI | - | Relay (recommended) | API usage, NOT ChatGPT plan |
| Anthropic | - | Relay (required) | Org usage, NOT Claude Pro |
| Gemini | - | Relay (required) | Cloud project, NOT AI Pro |
| Kimi | - | Direct | API balance |

## What QuotaPuter CAN Do

✅ View MiniMax Token Plan remaining quota
✅ View Kimi API balance
✅ View OpenAI API usage (via relay)
✅ View Anthropic organization usage (via relay)
✅ View Google Cloud/Gemini quotas (via relay)
✅ Cache results for offline viewing
✅ Auto-refresh every 5 minutes (configurable)

## What QuotaPuter CANNOT Do

❌ Call LLM APIs or generate responses
❌ Access ChatGPT Plus subscription
❌ Access Claude Pro/Max personal plans
❌ Access Google AI Pro subscription
❌ Use unofficial or reverse-engineered APIs
❌ Store admin keys directly (use relay mode)

## Hardware

- **Device**: M5Stack Cardputer (ESP32-S3)
- **Display**: ILI9342C 320x240
- **Keyboard**: Built-in matrix keyboard
- **Storage**: MicroSD slot (optional)

## Quick Start

### Build

```bash
# Activate ESP-IDF
. $IDF_PATH/export.sh

# Set target
idf.py set-target esp32s3

# Build
idf.py build
```

### Flash

```bash
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

### Configure Wi-Fi

1. On first boot, device enters Wi-Fi config mode
2. Type SSID and password
3. Press ENTER to connect

### Add Providers

```bash
# Using USB tool
python tools/quota_config.py --port /dev/tty.usbmodemXXXX add-provider minimax_cn
python tools/quota_config.py --port /dev/tty.usbmodemXXXX add-provider kimi --key sk-xxxx
```

## User Guide

### Controls

| Key | Action |
|-----|--------|
| ← → | Navigate providers |
| ENTER | Open provider detail |
| ESC | Back/Cancel |
| R | Refresh current |
| S | Settings |
| W | Wi-Fi config |

### Provider Cards

Each card shows:
- Provider logo (pixel art)
- Provider name and region
- Main metric (percentage/balance)
- Progress bar
- Last updated time

### Status Colors

| Color | Meaning |
|-------|---------|
| 🟢 Green | OK (<80% used) |
| 🟡 Yellow | Warning (80-95% used) |
| 🔴 Red | Critical (>95% used) |
| ⚪ Gray | Stale (cached) / Setup required |

## Security

### Authorization Modes

**Direct Mode** (credentials on device):
- MiniMax CN/Global
- Kimi

**Relay Mode** (recommended for admin keys):
- OpenAI
- Anthropic
- Gemini

### Relay Server

For high-privilege credentials, deploy your own relay server:

```bash
# Your relay returns standardized JSON
{
  "provider": "openai",
  "used": 12.30,
  "unit": "USD",
  "percentage": 45.5,
  "status": "ok"
}
```

## Development

### Project Structure

```
QuotaPuter/
├── main/                    # Main application
│   └── main.c
├── components/
│   ├── display/            # Pixel UI
│   ├── keyboard/           # Input handling
│   ├── wifi_manager/       # Wi-Fi
│   ├── http_client/        # HTTPS requests
│   ├── secret_store/       # Encrypted NVS
│   ├── cache/              # Offline cache
│   ├── provider_core/      # Provider interface
│   ├── providers/          # Provider implementations
│   └── provisioner/        # USB config protocol
├── tools/
│   └── quota_config.py     # PC configuration tool
├── config/
│   └── providers.example.json
└── docs/
    ├── PROVISIONING.md
    └── SECURITY.md
```

### Build with ESP-IDF

```bash
# Full rebuild
rm -rf build
idf.py build

# Flash and monitor
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

## Documentation

- [Provisioning Guide](docs/PROVISIONING.md)
- [Security Guide](docs/SECURITY.md)
- [PRD](docs/PRD.md)

## License

MIT License - see LICENSE file

## Trademarks

All brand names, logos, and trademarks belong to their respective owners:
- MiniMax™ - MiniMax Co., Ltd.
- OpenAI™ - OpenAI, Inc.
- Anthropic™ - Anthropic PBC
- Google Gemini™ - Google LLC
- Kimi™ - Moonshot AI

QuotaPuter is not affiliated with any of these companies.

## Disclaimer

Users are responsible for:
- Securing their own API keys
- Monitoring API usage and costs
- Ensuring compliance with provider terms of service
- Device physical security

This firmware only reads publicly documented API endpoints. No warranty is provided.
