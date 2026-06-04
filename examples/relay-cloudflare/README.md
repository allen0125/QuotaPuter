# QuotaPuter relay (Cloudflare Worker)

A ready-to-run relay so the Cardputer can show **OpenAI / Anthropic / Gemini**
org usage without ever holding the high-privilege admin keys. The keys live as
Worker **secrets**; the device stores only this Worker's URL and an optional
read-only token. Cloudflare gives you a publicly-trusted HTTPS URL out of the
box — which the firmware requires (it validates certs against the Mozilla root
bundle, so plain-http or self-signed relays won't connect).

## What it returns

Each `GET /<provider>` returns the standardized record the firmware parses:

```json
{ "provider": "anthropic", "metric_type": "usage", "title": "Anthropic",
  "used": 12.34, "limit": null, "unit": "USD", "percentage": null,
  "reset_at": null, "updated_at": "2026-06-04T12:00:00Z", "status": "ok" }
```

- `/anthropic` — month-to-date org **cost in USD** via
  `GET /v1/organizations/cost_report` (sums `data[].results[].amount`, which are
  decimal strings in **cents**). **Implemented & schema-verified.**
- `/openai` — month-to-date org cost via `GET /v1/organizations/costs`.
  Implemented, but verify the amount units against your account.
- `/gemini` — **TODO**: Gemini/Cloud usage needs a Cloud Monitoring `timeSeries`
  query (metric `generativelanguage.googleapis.com/...`) with a service-account
  token. Returns `status:"error"` until you fill in `gemini()` in `src/index.js`.

## Deploy

```bash
cd examples/relay-cloudflare
npm i -g wrangler          # or: npx wrangler ...
wrangler login

# Set the admin keys as secrets (never committed):
wrangler secret put ANTHROPIC_ADMIN_KEY   # sk-ant-admin... (Console -> Admin keys)
wrangler secret put OPENAI_ADMIN_KEY      # sk-admin...     (optional)
wrangler secret put DEVICE_TOKEN          # optional shared secret (see below)

wrangler deploy
# -> https://quotaputer-relay.<your-subdomain>.workers.dev
```

Quick check:

```bash
curl https://quotaputer-relay.<your-subdomain>.workers.dev/anthropic \
  -H "Authorization: Bearer <DEVICE_TOKEN if you set one>"
```

## Point the device at it

```bash
python tools/quota_config.py add-provider anthropic \
  --relay-url https://quotaputer-relay.<your-subdomain>.workers.dev/anthropic
# when prompted for the relay device token, enter the same DEVICE_TOKEN
# (or leave blank if you didn't set one)
```

The device immediately runs a read-only test query and shows `CONNECTED` or the
error. Repeat with `add-provider openai --relay-url .../openai`.

## Security notes

- The Anthropic **Admin API key** (`sk-ant-admin...`) only exists in the Worker
  secret store — not in this repo, not on the device.
- `DEVICE_TOKEN` is an optional shared secret so only your device can read the
  relay. It is checked against the `Authorization: Bearer` header.
- The relay only ever performs read-only usage/cost queries.
- Consider restricting the Worker further (Cloudflare Access, mTLS, or an
  allow-list) if the URL might leak.
