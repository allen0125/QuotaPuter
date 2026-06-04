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
- `/openai` — org **cost in USD** via `GET /v1/organization/costs`. OpenAI has
  **no official credit-balance API** (the "% used" bar in the dashboard needs a
  login session, which this project won't scrape). So by default this reports
  *this month's* spend. Set `OPENAI_BUDGET_USD` to your total prepaid/budget and
  it instead reports **cumulative spend vs that budget as a percentage**, so the
  device shows a "% used" progress bar. `OPENAI_START` (e.g. `2026-01-01`) sets
  where cumulative summing begins.
- `/gemini` — **TODO**: Gemini/Cloud usage needs a Cloud Monitoring `timeSeries`
  query (metric `generativelanguage.googleapis.com/...`) with a service-account
  token. Returns `status:"error"` until you fill in `gemini()` in `src/index.js`.
- `/codex` — your **ChatGPT subscription plan** usage (the 5h + weekly "% used"
  windows), via the same undocumented endpoint the official Codex CLI polls
  (`GET chatgpt.com/backend-api/wham/usage`). This is **not** the developer API
  platform — it's the plan/quota you see in the app. See setup below.

## /codex (ChatGPT plan usage) setup

This route reads usage with your `codex login` OAuth tokens (kept server-side,
auto-refreshed). It is an **undocumented** surface OpenAI's own CLI uses, so it
may change — treat it as best-effort.

```bash
# 1) Log in Codex with your ChatGPT account if you haven't:
codex login

# 2) Grab the refresh token from ~/.codex/auth.json and set it as a secret:
python3 -c "import json,os;print(json.load(open(os.path.expanduser('~/.codex/auth.json')))['tokens']['refresh_token'])" \
  | wrangler secret put CODEX_REFRESH_TOKEN

# 3) Create the KV namespace and paste its id into wrangler.toml (kv_namespaces.id):
wrangler kv namespace create TOKENS

# 4) Deploy and verify the RAW response shape first:
wrangler deploy
curl "https://quotaputer-relay.<sub>.workers.dev/codex?debug=1" -H "Authorization: Bearer <DEVICE_TOKEN>"
# then without ?debug for the standardized record:
curl  "https://quotaputer-relay.<sub>.workers.dev/codex"        -H "Authorization: Bearer <DEVICE_TOKEN>"
```

Point a device provider at `…/codex` (it reports `percentage` + `reset_at`, so the
device shows a "% used" bar that counts down to the window reset). The `?debug=1`
dump lets you confirm the real field names; the parser already tolerates common
camelCase/snake_case variants.

## Deploy

```bash
cd examples/relay-cloudflare
npm i -g wrangler          # or: npx wrangler ...
wrangler login

# Set the admin keys as secrets (never committed):
wrangler secret put ANTHROPIC_ADMIN_KEY   # sk-ant-admin... (Console -> Admin keys)
wrangler secret put OPENAI_ADMIN_KEY      # sk-admin... org Admin key, Read-only perm is enough (optional)
wrangler secret put DEVICE_TOKEN          # optional shared secret (see below)

# Optional: make /openai show "% used" instead of $ spent (no OpenAI credit API):
echo "100" | wrangler secret put OPENAI_BUDGET_USD   # your total prepaid/budget in USD
# echo "2026-01-01" | wrangler secret put OPENAI_START  # optional: cumulative start date

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
