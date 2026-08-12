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
  "reset_at": null, "updated_at": "2026-06-04T12:00:00Z", "status": "ok",
  "aux1": null, "aux1_label": null, "aux2": null, "aux2_label": null }
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
- `/codex` — your **ChatGPT subscription plan** weekly used percentage, weekly
  reset time, optional 5-hour window, and available banked reset count. This is
  **not** the developer API platform. See the compatibility note and setup below.

## /codex (ChatGPT plan usage) setup

For desktop/local integrations, prefer the supported `codex app-server` method
`account/rateLimits/read`; it owns login/refresh and returns rate-limit windows
plus `rateLimitResetCredits.availableCount`. A Cloudflare Worker cannot spawn
that local process, so this hosted route uses the corresponding internal
`/wham/usage` and `/wham/rate-limit-reset-credits` paths. It reads your `codex
login` OAuth tokens, keeps them server-side, and refreshes them automatically.
Those HTTPS paths are undocumented and can change, so treat this route as
best-effort.

```bash
# 1) Log in Codex with your ChatGPT account if you haven't:
codex login

# 2) Grab the refresh token from ~/.codex/auth.json and set it as a secret:
python3 -c "import json,os;print(json.load(open(os.path.expanduser('~/.codex/auth.json')))['tokens']['refresh_token'])" \
  | wrangler secret put CODEX_REFRESH_TOKEN

# 3) Create the KV namespace and paste its id into wrangler.toml (kv_namespaces.id):
wrangler kv namespace create TOKENS

# 4) Set DEVICE_TOKEN (strongly recommended), deploy, and verify the raw shape:
wrangler secret put DEVICE_TOKEN
wrangler deploy
curl "https://quotaputer-relay.<sub>.workers.dev/codex?debug=1" -H "Authorization: Bearer <DEVICE_TOKEN>"
# then without ?debug for the standardized record:
curl  "https://quotaputer-relay.<sub>.workers.dev/codex"        -H "Authorization: Bearer <DEVICE_TOKEN>"
```

Point a device provider at `…/codex`. The card headline and progress bar show
the weekly used percentage; detail shows its reset countdown, the 5-hour used
percentage when the backend returns it, and `RESETS: N`. The `?debug=1` dump
includes account quota metadata, so do not share it publicly.

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
error. Repeat with `add-provider openai --relay-url .../openai`, or add Codex:

```bash
python tools/quota_config.py add-provider codex \
  --relay-url https://quotaputer-relay.<your-subdomain>.workers.dev/codex
```

## Region / geo-blocking (important for HK & mainland China)

OpenAI/ChatGPT reject requests from unsupported regions (Hong Kong, mainland
China) with `unsupported_country_region_territory`. A Worker's outbound `fetch()`
egresses from the **data center where the Worker runs**, which by default is the
one nearest your *device* — so a device in HK makes the Worker egress from HK and
the call is blocked.

Fix (already in `wrangler.toml`): a placement hint pins the Worker near Tokyo so
egress is a Japan IP:

```toml
[placement]
region = "aws:ap-northeast-1"   # or gcp:asia-northeast1 / azure:japaneast
```

Cloudflare does **not** let you pick an arbitrary egress country directly; the
placement hint targets a cloud region and runs the Worker in the nearest CF data
center (Tokyo for `ap-northeast-1`). If you need a hard guarantee of Japan
egress, run this relay on a Tokyo-region host instead (Google Cloud Run
`asia-northeast1` or Fly.io `nrt`) — same code, managed HTTPS, fixed JP egress.

## Security notes

- The Anthropic **Admin API key** (`sk-ant-admin...`) only exists in the Worker
  secret store — not in this repo, not on the device.
- `DEVICE_TOKEN` is required for `/codex` and optional for the other routes. It
  is checked against the `Authorization: Bearer` header.
- The relay only ever performs read-only usage/cost queries.
- The Codex refresh token is an account credential. It stays in Worker secrets;
  rotated tokens stay in the private `TOKENS` KV namespace and never reach the
  device. `/codex` reports reset credits but never consumes one.
- The Worker refuses `/codex` when `DEVICE_TOKEN` is not configured, preventing
  accidental public exposure of your quota state.
- Consider restricting the Worker further (Cloudflare Access, mTLS, or an
  allow-list) if the URL might leak.
