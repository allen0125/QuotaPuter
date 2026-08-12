// QuotaPuter relay — Cloudflare Worker reference implementation.
//
// Holds your high-privilege provider keys as Worker *secrets*, calls each
// vendor's official org usage/cost API, and returns the standardized record the
// Cardputer expects (PRD §6.2). The device stores only this Worker's URL and an
// optional read-only device token — never the admin keys.
//
// Routes (one provider per path):
//   GET /anthropic   -> Anthropic org cost (month-to-date, USD)   [implemented]
//   GET /openai      -> OpenAI org cost (month-to-date, USD)       [implemented*]
//   GET /gemini      -> Google Cloud / Gemini usage               [TODO, see README]
//   GET /codex       -> ChatGPT/Codex weekly usage + reset count   [best-effort]
//
// Secrets (wrangler secret put ...):
//   ANTHROPIC_ADMIN_KEY   sk-ant-admin...   (required for /anthropic)
//   OPENAI_ADMIN_KEY      sk-admin...       (required for /openai)
//   CODEX_REFRESH_TOKEN   Codex OAuth token (required for /codex)
//   DEVICE_TOKEN          required for /codex, optional for other routes; send
//                         it as `Authorization: Bearer <DEVICE_TOKEN>`

export default {
  async fetch(request, env) {
    const path = new URL(request.url).pathname.replace(/\/+$/, "");
    const provider = path.split("/").filter(Boolean).pop() || "";

    if (provider === "codex" && !env.DEVICE_TOKEN) {
      return json({ provider, status: "error", error: "DEVICE_TOKEN required for /codex" }, 503);
    }
    // Optional shared-secret gate (the device's relay_token).
    if (env.DEVICE_TOKEN) {
      const token = (request.headers.get("authorization") || "").replace(/^Bearer\s+/i, "");
      if (token !== env.DEVICE_TOKEN) {
        return json({ provider, status: "auth", error: "invalid device token" }, 401);
      }
    }

    try {
      if (provider === "codex") {
        return await codexUsage(env, new URL(request.url).searchParams.has("debug"));
      }
      if (provider === "anthropic") return await anthropic(env);
      if (provider === "openai") return await openai(env);
      if (provider === "gemini") return await gemini(env);
      return json({ provider, status: "error", error: "unknown provider path" }, 404);
    } catch (e) {
      return json({ provider, status: "error", error: String(e) }, 502);
    }
  },
};

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { "content-type": "application/json" },
  });
}

// Surface the upstream vendor error so "wrong key" vs "missing permission" is
// distinguishable on the device/curl. 401/403 -> auth, everything else -> error.
async function upstreamError(provider, r) {
  let detail = "";
  try {
    detail = (await r.text()).slice(0, 200).replace(/\s+/g, " ").trim();
  } catch (_) {}
  const status = r.status === 401 || r.status === 403 ? "auth" : "error";
  return json({ provider, status, error: `http ${r.status} ${detail}`.trim() });
}

// First instant of the current UTC month, as an RFC-3339 timestamp.
function monthStartISO(now) {
  return new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), 1)).toISOString();
}

// ---- Anthropic: month-to-date org cost in USD --------------------------------
// GET /v1/organizations/cost_report -> data[].results[].amount (decimal string,
// in CENTS) with currency "USD". Sum all buckets/results, divide by 100.
async function anthropic(env) {
  const key = env.ANTHROPIC_ADMIN_KEY;
  if (!key) return json({ provider: "anthropic", status: "error", error: "ANTHROPIC_ADMIN_KEY not set" }, 500);

  const now = new Date();
  const startingAt = monthStartISO(now);
  let cents = 0;
  let page = null;

  for (let i = 0; i < 32; i++) {
    const u = new URL("https://api.anthropic.com/v1/organizations/cost_report");
    u.searchParams.set("starting_at", startingAt);
    u.searchParams.set("ending_at", now.toISOString());
    u.searchParams.set("limit", "31");
    if (page) u.searchParams.set("page", page);

    const r = await fetch(u, {
      headers: {
        "x-api-key": key,
        "anthropic-version": "2023-06-01",
        "user-agent": "QuotaPuter-Relay/1.0",
      },
    });
    if (!r.ok) return await upstreamError("anthropic", r);
    const body = await r.json();
    for (const bucket of body.data || []) {
      for (const res of bucket.results || []) {
        cents += parseFloat(res.amount || "0") || 0;
      }
    }
    if (body.has_more && body.next_page) page = body.next_page;
    else break;
  }

  return json({
    provider: "anthropic",
    metric_type: "usage",
    title: "Anthropic",
    used: Math.round((cents / 100) * 100) / 100,
    limit: null,
    unit: "USD",
    percentage: null,
    reset_at: null,
    updated_at: now.toISOString(),
    status: "ok",
  });
}

// ---- OpenAI: month-to-date org cost in USD -----------------------------------
// GET /v1/organization/costs (singular!) -> data[].results[].amount.{value,currency}
// where value is USD DOLLARS as a number. Needs an org Admin key (sk-admin...).
async function openai(env) {
  const key = env.OPENAI_ADMIN_KEY;
  if (!key) return json({ provider: "openai", status: "error", error: "OPENAI_ADMIN_KEY not set" }, 500);

  const now = new Date();
  // OpenAI has NO official credit-balance API. If you set OPENAI_BUDGET_USD to
  // your total prepaid/budget, the relay reports cumulative spend vs that budget
  // as a percentage (so the device can show a "% used" bar). Without it, it just
  // reports this month's spend in USD. OPENAI_START (an RFC date like
  // "2026-01-01") overrides where cumulative summing begins (default ~2 years).
  const budget = env.OPENAI_BUDGET_USD ? parseFloat(env.OPENAI_BUDGET_USD) : null;
  const startUnix = env.OPENAI_START
    ? Math.floor(Date.parse(env.OPENAI_START) / 1000)
    : Math.floor(Date.UTC(now.getUTCFullYear() - (budget ? 2 : 0), now.getUTCMonth(), 1) / 1000);
  let total = 0;
  let page = null;

  for (let i = 0; i < 32; i++) {
    // NOTE: OpenAI uses the SINGULAR "organization" (Anthropic uses plural).
    const u = new URL("https://api.openai.com/v1/organization/costs");
    u.searchParams.set("start_time", String(startUnix));
    u.searchParams.set("bucket_width", "1d");
    u.searchParams.set("limit", "31");
    if (page) u.searchParams.set("page", page);

    const r = await fetch(u, { headers: { authorization: `Bearer ${key}` } });
    if (!r.ok) return await upstreamError("openai", r);
    const body = await r.json();
    for (const bucket of body.data || []) {
      for (const res of bucket.results || []) {
        const v = res.amount && typeof res.amount.value === "number" ? res.amount.value : 0;
        total += v;
      }
    }
    if (body.has_more && body.next_page) page = body.next_page;
    else break;
  }

  const resp = {
    provider: "openai",
    metric_type: "usage",
    title: "OpenAI API",
    used: Math.round(total * 100) / 100,
    limit: null,
    unit: "USD",
    percentage: null,
    reset_at: null,
    updated_at: now.toISOString(),
    status: "ok",
  };
  if (budget && budget > 0) {
    resp.limit = Math.round(budget * 100) / 100;
    resp.percentage = Math.round((total / budget) * 10000) / 100;  // % used; device shows a bar
  }
  return json(resp);
}

// ---- Gemini: TODO ------------------------------------------------------------
// Gemini/Google Cloud usage isn't a single REST call. Implement a query against
// the Cloud Monitoring API (timeSeries for the generativelanguage.googleapis.com
// metric) or Cloud Billing, authenticated with a service-account access token
// scoped to your project, then map the result below. See README for pointers.
async function gemini(env) {
  return json({
    provider: "gemini",
    metric_type: "project",
    title: "Gemini",
    used: null,
    limit: null,
    unit: null,
    percentage: null,
    reset_at: null,
    updated_at: new Date().toISOString(),
    status: "error", // not implemented — fill in the Cloud Monitoring query
  });
}

// ---- Codex / ChatGPT plan usage ----------------------------------------------
// Reads your ChatGPT subscription plan usage (5h/weekly windows) and banked
// reset-credit count via the same backend client paths used by Codex:
//   GET https://chatgpt.com/backend-api/wham/usage
//   GET https://chatgpt.com/backend-api/wham/rate-limit-reset-credits
// The supported desktop integration is `codex app-server`'s
// account/rateLimits/read method. A Worker cannot spawn that local process, so
// this hosted relay uses the equivalent internal HTTPS paths. Keep it isolated
// behind DEVICE_TOKEN because those paths are not a public OpenAI API contract.
// Tokens come from `codex login`. Set CODEX_REFRESH_TOKEN (from ~/.codex/auth.json
// -> tokens.refresh_token) as a secret; the Worker refreshes the short-lived
// access token via OAuth and persists the rotated tokens in the TOKENS KV
// namespace. NOTE: undocumented surface — fields may shift; use ?debug=1 to dump
// the raw response, then tighten the parser below.
const CODEX_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann";
const CODEX_TOKEN_URL = "https://auth.openai.com/oauth/token";
const CODEX_USAGE_URL = "https://chatgpt.com/backend-api/wham/usage";
const CODEX_RESET_CREDITS_URL =
  "https://chatgpt.com/backend-api/wham/rate-limit-reset-credits";

function b64urlToJson(seg) {
  const b64 = seg.replace(/-/g, "+").replace(/_/g, "/");
  const pad = b64.padEnd(Math.ceil(b64.length / 4) * 4, "=");
  return JSON.parse(decodeURIComponent(escape(atob(pad))));
}
function jwtClaim(token, path) {
  try {
    const p = b64urlToJson(token.split(".")[1]);
    const auth = p["https://api.openai.com/auth"] || {};
    return auth[path] ?? p[path] ?? null;
  } catch (_) {
    return null;
  }
}
function jwtExpMs(token) {
  try {
    return (b64urlToJson(token.split(".")[1]).exp || 0) * 1000;
  } catch (_) {
    return 0;
  }
}

async function refreshCodexToken(refresh) {
  const body = new URLSearchParams({
    grant_type: "refresh_token",
    refresh_token: refresh,
    client_id: CODEX_CLIENT_ID,
  });
  const r = await fetch(CODEX_TOKEN_URL, {
    method: "POST",
    headers: { "content-type": "application/x-www-form-urlencoded" },
    body,
  });
  if (!r.ok) throw new Error(`token refresh ${r.status} ${(await r.text()).slice(0, 150)}`);
  return await r.json(); // { access_token, refresh_token, id_token, expires_in }
}

async function codexAccessToken(env) {
  if (!env.TOKENS) throw new Error("bind a KV namespace as TOKENS (see README)");
  let stored = null;
  const cached = await env.TOKENS.get("codex");
  if (cached) stored = JSON.parse(cached);

  const now = Date.now();
  if (stored && stored.access && stored.exp - now > 60000) return stored;

  const refresh = (stored && stored.refresh) || env.CODEX_REFRESH_TOKEN;
  if (!refresh) throw new Error("no refresh token (set CODEX_REFRESH_TOKEN)");

  const tok = await refreshCodexToken(refresh);
  const access = tok.access_token;
  const next = {
    access,
    refresh: tok.refresh_token || refresh,
    exp: jwtExpMs(access) || now + (tok.expires_in || 3600) * 1000,
    accountId: jwtClaim(access, "chatgpt_account_id"),
  };
  await env.TOKENS.put("codex", JSON.stringify(next));
  return next;
}

function finiteNumber(value) {
  if (typeof value === "number" && Number.isFinite(value)) return value;
  if (typeof value === "string" && value.trim() !== "") {
    const parsed = Number(value);
    if (Number.isFinite(parsed)) return parsed;
  }
  return null;
}

// Normalize both the private wham JSON (snake_case, *_window) and Codex
// app-server JSON (camelCase, primary/secondary). Window duration, rather than
// position, is authoritative: current plans can return the weekly bucket as
// `primary` with no secondary bucket.
function normalizeWindow(w) {
  if (!w || typeof w !== "object") return null;
  const used = finiteNumber(
    w.usedPercent ?? w.used_percent ?? w.percent_used ?? w.percent,
  );
  let mins = finiteNumber(
    w.windowDurationMins ?? w.window_duration_mins ?? w.window_minutes,
  );
  if (mins == null) {
    const seconds = finiteNumber(
      w.limitWindowSeconds ?? w.limit_window_seconds ?? w.window_seconds,
    );
    if (seconds != null) mins = seconds / 60;
  }

  let reset = w.resetsAt ?? w.resets_at ?? w.reset_at ?? null;
  if (typeof reset === "string" && finiteNumber(reset) == null) {
    const parsed = Date.parse(reset);
    reset = Number.isFinite(parsed) ? Math.floor(parsed / 1000) : null;
  } else {
    reset = finiteNumber(reset);
  }
  if (reset != null && reset > 1e12) reset = Math.floor(reset / 1000);
  if (reset == null) {
    const inS = finiteNumber(
      w.resetAfterSeconds ?? w.reset_after_seconds ??
        w.resetsInSeconds ?? w.resets_in_seconds ?? w.resets_in,
    );
    if (inS != null) reset = Math.floor(Date.now() / 1000) + inS;
  }
  if (used == null && mins == null && reset == null) return null;
  return { used, minutes: mins, reset };
}

function classifyCodexWindows(body) {
  const root = body?.result || body || {};
  const byId = root.rateLimitsByLimitId || root.rate_limits_by_limit_id || {};
  const rl = byId.codex || root.rateLimits || root.rate_limits || root.rate_limit || root;
  const primary = normalizeWindow(rl.primary_window || rl.primaryWindow || rl.primary);
  const secondary = normalizeWindow(
    rl.secondary_window || rl.secondaryWindow || rl.secondary,
  );

  let fiveHour = null;
  let weekly = null;
  for (const w of [primary, secondary]) {
    if (!w || w.minutes == null) continue;
    if (Math.abs(w.minutes - 300) < 0.01) fiveHour = w;
    if (Math.abs(w.minutes - 10080) < 0.01) weekly = w;
  }
  // Backward-compatible positional fallback for older payloads with no duration.
  if (!fiveHour && primary && primary !== weekly && secondary) fiveHour = primary;
  if (!weekly && secondary) weekly = secondary;
  // Some payload variants expose one unlabeled window as `primary`. Keep it as
  // the current headline without claiming it is weekly; duration-aware current
  // plans (including weekly-only plans) were classified above.
  return { fiveHour, weekly, current: primary || secondary };
}

function resetCreditCount(usageBody, creditsBody) {
  const candidates = [];
  for (const body of [creditsBody, usageBody]) {
    const root = body?.result || body;
    if (!root || typeof root !== "object") continue;
    candidates.push(
      root.rateLimitResetCredits,
      root.rate_limit_reset_credits,
      root.resetCredits,
      root,
    );
  }
  for (const c of candidates) {
    if (!c || typeof c !== "object") continue;
    const value = finiteNumber(c.availableCount ?? c.available_count);
    if (value != null) return Math.max(0, Math.floor(value));
  }
  return null;
}

async function codexUsage(env, debug) {
  const t = await codexAccessToken(env);
  const headers = {
    authorization: `Bearer ${t.access}`,
    "user-agent": "codex_cli_rs/0.0.0 (QuotaPuter relay)",
    originator: "codex_cli_rs",
    "openai-beta": "codex-1",
    accept: "application/json",
  };
  if (t.accountId) headers["chatgpt-account-id"] = t.accountId;

  const [usageResponse, creditsResponse] = await Promise.all([
    fetch(CODEX_USAGE_URL, { headers }),
    fetch(CODEX_RESET_CREDITS_URL, { headers }),
  ]);
  if (!usageResponse.ok) return await upstreamError("codex", usageResponse);
  const usageBody = await usageResponse.json();
  let creditsBody = null;
  if (creditsResponse.ok) {
    creditsBody = await creditsResponse.json();
  }
  if (debug) {
    return json({
      usage: usageBody,
      reset_credits: creditsBody,
      reset_credits_http_status: creditsResponse.status,
    });
  }

  const { fiveHour, weekly, current } = classifyCodexWindows(usageBody);
  const headline = weekly || fiveHour || current;
  if (!headline || headline.used == null) {
    return json({
      provider: "codex", metric_type: "subscription", title: "Codex Weekly",
      used: null, limit: null, unit: "%", percentage: null, reset_at: null,
      updated_at: new Date().toISOString(),
      status: "error", error: "could not parse usage windows — try ?debug=1",
    });
  }
  const used = Math.max(0, Math.min(100, headline.used));
  const resetCount = resetCreditCount(usageBody, creditsBody);
  return json({
    provider: "codex",
    metric_type: "subscription",
    title: weekly ? "Codex Weekly" : "Codex Current",
    used: Math.round(used * 100) / 100,
    // Keep `has_limit` false in the compact firmware data model: `percentage`
    // already drives the bar, while the freed detail row fits 5h, resets, and
    // the weekly reset countdown on the 135px display.
    limit: null,
    unit: "%",
    percentage: Math.round(used * 100) / 100,
    reset_at: headline.reset ? new Date(headline.reset * 1000).toISOString() : null,
    aux1: fiveHour && fiveHour !== headline && fiveHour.used != null
      ? Math.max(0, Math.min(100, fiveHour.used))
      : null,
    aux1_label: fiveHour && fiveHour !== headline ? "5H USED%" : null,
    aux2: resetCount,
    aux2_label: resetCount != null ? "RESETS" : null,
    updated_at: new Date().toISOString(),
    status: "ok",
  });
}
