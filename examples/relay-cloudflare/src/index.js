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
//
// Secrets (wrangler secret put ...):
//   ANTHROPIC_ADMIN_KEY   sk-ant-admin...   (required for /anthropic)
//   OPENAI_ADMIN_KEY      sk-admin...       (required for /openai)
//   DEVICE_TOKEN          optional; if set, requests must send
//                         `Authorization: Bearer <DEVICE_TOKEN>`

export default {
  async fetch(request, env) {
    const path = new URL(request.url).pathname.replace(/\/+$/, "");
    const provider = path.split("/").filter(Boolean).pop() || "";

    // Optional shared-secret gate (the device's relay_token).
    if (env.DEVICE_TOKEN) {
      const token = (request.headers.get("authorization") || "").replace(/^Bearer\s+/i, "");
      if (token !== env.DEVICE_TOKEN) {
        return json({ provider, status: "auth", error: "invalid device token" }, 401);
      }
    }

    try {
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
    if (r.status === 401 || r.status === 403) {
      return json({ provider: "anthropic", status: "auth", error: "admin key rejected" });
    }
    if (!r.ok) {
      return json({ provider: "anthropic", status: "error", error: `anthropic http ${r.status}` });
    }
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
// GET /v1/organizations/costs -> data[].results[].amount.{value,currency}.
// NOTE: verify against your account — the amount units (USD dollars vs cents)
// and field names have changed across OpenAI API revisions.
async function openai(env) {
  const key = env.OPENAI_ADMIN_KEY;
  if (!key) return json({ provider: "openai", status: "error", error: "OPENAI_ADMIN_KEY not set" }, 500);

  const now = new Date();
  const startUnix = Math.floor(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), 1) / 1000);
  let total = 0;
  let page = null;

  for (let i = 0; i < 32; i++) {
    const u = new URL("https://api.openai.com/v1/organizations/costs");
    u.searchParams.set("start_time", String(startUnix));
    u.searchParams.set("bucket_width", "1d");
    u.searchParams.set("limit", "31");
    if (page) u.searchParams.set("page", page);

    const r = await fetch(u, { headers: { authorization: `Bearer ${key}` } });
    if (r.status === 401 || r.status === 403) {
      return json({ provider: "openai", status: "auth", error: "admin key rejected" });
    }
    if (!r.ok) {
      return json({ provider: "openai", status: "error", error: `openai http ${r.status}` });
    }
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

  return json({
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
  });
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
