# YBX Reader #56: opt-in context-anchor sync v1 over verified KoSync HTTPS

Stacked on draft PR #21 (local Native Notes context storage). Server
dependency: jhdvp/xteink-yandex-books draft PR #40, additive PostgreSQL V22.
This is a draft integration, not a production deployment.

## Compatibility contract

Original clipping sync remains exactly /api/v1/clippings/{document}
sync_version=2. After it has completed all uploads/tombstones and pulled
all clipping pages, and ONLY for managed Native Notes
(note-<16 lowercase hex>.md), the client tries:

GET {base}/api/v1/clippings/{document}/anchors?cursor=0

If an old server answers a completely received HTTP 404, the client
treats the extension as unsupported and reports the v2 sync successful.
Any other error/invalid version fails the OPTIONAL context step and
surfaces a sync error rather than silently overwriting local anchors.
No extension request is made for ordinary EPUB and third-party MD.

A server offering anchor_version=1 accepts:
PUT {base}/api/v1/clippings/{document}/anchors
{"anchor_version":1,"id":"<32 lowercase hex>",
 "text":"exact original quote", "prefix":"...","suffix":"..."}

Local anchors are uploaded idempotently (the server enforces exact
account/document/quote equality), without changing the clipping's
server revision/Yandex outbox. The same local context can safely be
PUT again on retry; no saved upload-state field or permanent
revision churn is added to the clipping format.

After upload, GET pages carry at most one anchor:
{"anchor_version":1,"cursor":N,"more":false,"items":[
 {"id":"...", "text":"...", "prefix":"...", "suffix":"...",
  "revision":N}
]}
Only anchors whose clipping ID and EXACT UTF-8 quote already exist
locally are saved. An unknown remote ID is ignored, never creates a
new clipping. Bad pagination, missing/invalid fields, text mismatches,
or conflicting immutable anchors fail closed and never mutate saved
quotes. The local sidecar controls capture-time context.

Network calls use the SAME pre-verified HTTPS client as the existing
clipping sync: CA+hostname validation, trusted time and no insecure
fallback before sending x-auth-user/x-auth-key/Bearer token. C3 uses
its existing 32 KiB frame-buffer loan; S3 uses bounded allocation.

## Stale quote changes

A remote V2 edit can change a clipping's text without changing its
stable id. The old context sidecar then has a quote fingerprint
mismatch. This is distinguished from actual storage corruption:
Status::Stale makes the old anchor unusable for reanchoring and
skips uploading that anchor but does NOT block other quotes from
synchronizing. Actual damage/invalid checksum remains Status::Invalid
and fails closed. Existing unanchored/legacy clippings still use
PR #20's single-exact-quote fallback. No source offsets/revisions,
tombstones, or Yandex quotes are rewritten during context transfer.

## Safety and rollout

Server V22 migration and server PR #40 MUST be staged/verified before
using the extension in production. Old firmware remains on v2 and
is unaffected. New firmware against an old server is backward
compatible through 404 optional-feature detection. There is no
automatic old binary-key collection aliasing or cross-EPUB CFI
migration; these remain separate user/audit-gated operations.

A successfully delivered context from another device can be used
by the existing immutable in-memory Native Note reanchor code,
but it is not a proof of semantic equivalence; identical moved
quotes with identical context remain ambiguous or potentially wrong.

Open acceptance:
- physical X3/X4/X4 Pro: HTTPS CA/SNTP, real SD, actual contiguous heap,
  cold-open/write latency and tap highlighting, power loss;
- local PostgreSQL V21->V22 migration and schema drift tests using
  opt-in YBX_TEST_POSTGRES_DSN (not a production DSN);
- staging two-device round-trip, 404 old server, invalid revision,
  conflicting immutable anchors, offline deletes and retries;
- account-scoped canonical document aliases with dry-run/auditable
  migration and no implicit merge of unrelated book hashes.

No main merge, production server schema change, firmware device flash,
OTA, release or production deployment.
