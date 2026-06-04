# Security Model

QuotaPuter is a read-only quota viewer. It only calls official, publicly
documented provider endpoints and never performs web-login scraping, cookie
capture, or reverse-engineered API access.

## Where credentials live

- Wi-Fi and provider credentials are stored **only on the device**, in a
  dedicated `secret` NVS partition (separate from the general `nvs` partition so
  a factory reset can wipe it wholesale).
- The repository, the firmware image, and the example configs contain **no**
  real keys, tokens, cookies, or passwords. `config/providers.json`,
  `config/wifi.json`, `*.key`, `*.pem`, `.env`, and `secrets/` are gitignored.

## Encryption at rest

The firmware uses standard NVS APIs. For confidentiality at rest in production:

1. **Flash Encryption** (recommended): enabling ESP-IDF Flash Encryption
   transparently encrypts the entire flash, including the `secret` NVS
   partition. No application change is required.
2. **NVS Encryption**: if you build with `CONFIG_NVS_ENCRYPTION=y` and add an
   `nvs_keys` partition (type `data`, subtype `nvs_keys`), `secret_store` opens
   the `secret` partition with `nvs_flash_secure_init_partition`. Otherwise it
   falls back to plaintext NVS (rely on Flash Encryption above).

## What is never exposed

- Secrets are **never logged in full**. Diagnostics use `secret_store_redact`,
  which shows only the last 4 characters (e.g. `****1a2b`).
- The screen never shows a full key.
- Logs record only provider id, HTTP status, error class, and time — never the
  Authorization header, request/response bodies, or the Wi-Fi password
  (PRD §6.1, §12).
- In-memory copies of keys are scrubbed (`memset` to zero) after use in the
  providers and provisioner.

## Transport security

- All requests are HTTPS. Server certificates are validated against the bundled
  Mozilla root CA store (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`). TLS verification
  is never disabled.
- 10-second timeout, at most one retry, and no more than one provider request in
  flight at a time (PRD §10.3).

## Authorization modes

| Mode | Use for | Notes |
| --- | --- | --- |
| Direct | MiniMax CN/Global, Kimi | Low-privilege user keys stored on device. |
| Relay (recommended/required) | OpenAI, Anthropic, Gemini | High-privilege org/admin credentials stay on **your** relay server; the device holds only a relay URL and a read-only device token. |

Do **not** store OpenAI/Anthropic Admin keys or Google Cloud service-account
credentials directly on the device — use relay mode.

## Erasing credentials

- Remove one provider: `quota_config.py remove-provider <id>`, or hold **D** on
  the overview.
- Erase everything (all provider secrets + Wi-Fi): `quota_config.py
  factory-reset`, or hold **Fn+Del** on the device.

## Caching

The offline cache stores only the parsed `quota_result_t` (numbers, units,
timestamps, status) — never full keys or response headers (PRD §5.4).
