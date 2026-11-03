# YBX Reader P2-I #56 — stable quote IDs and TLS-verified clipping sync

Stacked on draft PR #17 (Markdown V1 to V2 saved-position migration).
This patch hardens the existing CrossPoint ClippingStore and ClippingSync,
rather than adding a second notes protocol or importing another fork.
The YBX Server already implements /api/v1/clippings/{document} with
sync_version=2, durable upserts and tombstones. Legacy KoSync annotations
routes are NOT this protocol.

## Behaviour

Stable local clipping identity: each new clipping is given a random
128-bit 32-lowercase-hex ID inside ClippingStore::addClipping BEFORE its
first successful SD write. Previously that happened only when the user
eventually enabled or performed sync. Deleting a clipping before its
first sync now queues a deletion for that same ID. An improbable ID
collision retries up to eight times and fails closed without corrupting
any existing record. Two identically-worded selections remain distinct.

Existing V4 records retain their IDs unchanged. Older anonymous records
still receive their IDs in prepareSync before upload. No new local file
format: the ClippingStore stays at VERSION=4, the server protocol stays
at sync_version=2. Existing server idempotent uploads and tombstones
continue to be the authority for retries and deletions.

Transport: ClippingSync previously called setInsecure() while sending
account x-auth-user and x-auth-key plus optional bearer credentials.
The client now calls the existing YBX
tlspolicy::prepareHttpsClient BEFORE the first request. It requires
HTTPS, trustworthy time (bounded SNTP attempt), installed CA roots and
hostname verification. When transport verification fails, no sync
request or authentication headers are sent and there is no insecure
fallback. Formerly working plain-HTTP local clipping servers will need
HTTPS configured. Normal KoSync progress is not modified here.

## Remaining identity gaps

Stable clipping IDs alone do NOT make the same book share a document
identity across two clients. The server keys data by the KoSync document
hash: binary hashes may change after a Markdown note edit; filename
hashes may differ after renaming. The local clipping store also includes
the device-local book path. An ID survives the clipping's ordinary
lifecycle, but cannot magically bridge unrelated document hashes.

Stored source offsets are the reader's parsed content coordinates,
not a cross-edition EPUB CFI or XPath. The server may search quotation
text in a mapped edition, but repeated text can be ambiguous. This
patch does not claim to migrate Markdown V1 quote offsets or implement
PocketBook stock notes protocol v2, client conflict resolution or an
all-devices portable export. Those are separately scoped changes.

## Tests and release gates

Unit tests cover ID generation before sync, persistence, two identical
passages receiving distinct IDs, pre-sync deletion journal ID and
forced random collisions with fail-closed behaviour. Host random stubs
now have a long period rather than repeating after 16 generated IDs.

Dual C3/S3 ClippingSync host tests enforce HTTPS and simulated CA/time
failure, no authentication headers before the gate, maximum-sized JSON
clippings, network/incomplete-response errors and heap/loan guards.

No server schema or API changed and no live clippings were uploaded
or deleted. Full host regression, both board builds and checksums are
recorded after final verification.

Acceptance remains: real SNTP and wolfSSL CA/hostname checks, full SD
and power-cut, certificate renewal, large batches and retries,
staging server with test account, memory/energy, real X3/X4/X4Pro
selection, export and clip deletion. No main merge, OTA or device flash.
