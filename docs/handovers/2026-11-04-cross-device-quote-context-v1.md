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

## Final validated evidence · 2026-11-04

- **1237/1237 full host CMake/CTest PASS** (36.44 s) after the
  stale context status change, with existing rendering, clipping,
  identity, YAML, Markdown and runtime regressions.
- **C3 + S3 ClippingSync standalone host PASS** (both exact architecture
  compile targets): old-server 404 fallback; verified TLS only;
  local anchor upload and idempotent repeat; remote anchor pull;
  edited/mismatched quote rejection; stale local context skipping
  without blocking other clipping sync; invalid version, cursor
  and revision; maximum legacy v2 quote and C3 framebuffer loan.
- **Server PR #40** SQLite/auth/API/schema suite: 92 passed,
  90 skipped (opt-in PostgreSQL local staging). Staging PG V22
  upgrade and audit have NOT been exercised.
- **ESP32-C3 default/X3-X4 PlatformIO PASS**, final re-run:
  flash 5,728,515 / 6,553,600 B (**87.4%**);
  static RAM 66,904 / 327,680 B (**20.4%**);
  firmware.bin **5,743,040 B**;
  SHA256 **a203aff9c324c4425a4c978db693b212cd676aea9a7e63c88c2e7d30d54cea66**.
- **ESP32-S3 x4pro/X4 Pro PlatformIO PASS**, final re-run:
  flash 5,567,570 / 6,553,600 B (**85.0%**);
  static RAM 111,956 / 327,680 B (**34.2%**);
  firmware.bin **5,572,832 B**;
  SHA256 **69dbc4b595c871e53545c465428762430b5529c5482fe8f77266f6b5e52944ee**.
- Relative to parent PR #21 linked flash grows +2,322 B C3 and
  +2,548 B S3; static RAM unchanged. These are linked metrics,
  not measured live heap/battery or cold-open latency.
- The first C3 build emitted a different SHA for a same-size image
  on a later clean/incremental toolchain pass. No deterministic
  reproducibility claim: final SHA above identifies the exact
  retained file after the LAST successful PlatformIO run.
- Artifacts/logs remain on the Mac at
  ~/Projects/Ybx/artifacts/ybx-note-anchor-sync-20261104.
  PlatformIO's freeink-sdk local patch marked the submodule dirty;
  those SDK modifications were NOT staged or committed.
- Main, OTA, physical device flash, production deployments and
  any DB migrations remain untouched.
