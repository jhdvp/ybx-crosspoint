# YBX SDK source patches

The firmware compiles the pinned FreeInk SDK submodule directly: `platformio.ini`
declares every SDK library as `symlink://freeink-sdk/libs/...`, so a change that
must ship before the next SDK release has to be carried as a reviewed patch in
this repository, not as an edit inside the submodule.

Each patch topic is two files:

- `scripts/<topic>_patches/<n>-<name>.patch` — the diffs, reviewed like source,
  applied in file-name order;
- `scripts/patch_<topic>.py` — a PlatformIO `pre:` script that applies the
  series.

The script verifies every touched file against reviewed SHA-256 digests before
touching anything, fails the build on unknown or partially applied SDK content,
applies each stage only after `git apply --check` accepts it, and rolls the
series back if a later stage fails. It is idempotent, so incremental builds do
not rewrite sources. It modifies the submodule working tree, so `git status`
reports `freeink-sdk` as modified while the patches are applied; do not commit
inside the submodule.

## Applied patches

| Patch | Script | SDK area |
| --- | --- | --- |
| `scripts/securenet_patches/0001-deadline-dns-tcp.patch` | `scripts/patch_securenet.py` | `libs/network/SecureNet` |
| `scripts/securenet_patches/0002-tls-verify.patch` | `scripts/patch_securenet.py` | `libs/network/SecureNet` |

Pinned SDK revision: `6a208c0c46cce99bafa334bc4f171f5e558c5195` (the revision
`upstream/develop` pins; upstream `2.0.0` pins `5e94fe74`).

### 0001: deadline-bounded DNS and TCP

Why: `WiFiClient`/`NetworkClient::connect(host, port)` resolves the host with
`lwip_getaddrinfo()`, a blocking call that neither the socket timeout nor
`SecureClient`'s abort callback can interrupt. The YBX sleep path delivers
plugin events under a cooperative 15 s drain budget; a stalled lookup could add
the resolver's own retry schedule (seconds per DNS server) after that budget ran
out, and a cancelled request could not stop it at all.

What the patch does:

- adds `DeadlineDns` — lwIP's asynchronous IPv4 resolver behind a fixed pool of
  four static slots. lwIP may answer long after the waiter left; the slot is
  then recycled by the late callback itself, so no callback points at caller
  state, no task is created, and retried lookups cannot grow memory;
- resolves once per connect in `SecureClient` and connects the socket to the
  returned `IPAddress`; the hostname still drives SNI, the Host header and
  logging;
- shares one absolute `millis()` deadline across DNS, the TCP connect, the TLS
  handshake and the TLS 1.2 retry, and polls the abort callback while DNS is
  pending;
- applies the same deadline to plain-http connects in `SecureHttpClient`.

Limitations:

- resolution is IPv4-only (the previous blocking path could prefer IPv6 when
  the stack enabled it);
- the device's steady-state socket timeouts are restored after connect, so a
  slow lookup does not shorten later reads;
- DNS, TCP and TLS now share the request timeout, but the sleep drain budget
  around them remains cooperative: WiFi shutdown and SD writes are outside it.

### 0002: peer and host-name verification

Why: the pinned transport loaded a CA bundle but never turned verification on,
never checked that the certificate covers the requested host, and reported every
connect failure as a generic error. Callers could not tell a rejected
certificate from a dead network, an https→http redirect silently dropped
transport security in `ResumableFetch`, and a followed redirect kept the
caller's `Authorization`, Basic credentials and cookies for the new host.

What the patch does:

- `SecureClient` refuses to connect without a usable trust anchor: no CA bundle
  or a bundle wolfSSL cannot parse fails as `Configuration` instead of
  continuing into a handshake that cannot succeed;
- enables `WOLFSSL_VERIFY_PEER` and `wolfSSL_check_domain_name()`, so the chain
  and the host name are both checked (`setInsecure()` still opts out for the
  documented exceptions);
- adds `ConnectFailure` (`None`, `Aborted`, `Transport`, `Timeout`, `Memory`,
  `Certificate`, `Clock`, `Hostname`, `Configuration`), classified from the
  wolfSSL error at each failure point, and skips the TLS 1.2 retry when the
  failure was not an I/O or timeout problem — verification already decided it;
- `SecureHttpClient` exposes `connectFailure()` and drops the caller's
  `Authorization`, `Proxy-Authorization` and `Cookie` headers and Basic
  credentials when its own redirect follower crosses to another origin;
- `ResumableFetch` reports `insecureRedirect` and refuses an https→http hop
  unless the caller sets `redirectToHttp` (the URL token would otherwise cross
  in clear text), and exposes the last connect `failure` in `FetchResult`.

Limitations: the host tests exercise the policy with stub sockets; wolfSSL's
actual handshake behavior and its RAM cost on X3/X4 are firmware/hardware
checks, not host checks.

## Host tests

Run the SDK harness after a build has applied the series (or apply it with
`python3 scripts/patch_securenet.py`):

```sh
sh freeink-sdk/libs/network/SecureNet/test/host/run.sh
```

They cover deadline arithmetic across the `millis()` wraparound, literal-address
parsing, a resolver cache hit, NXDOMAIN, a blackholed resolver stopping at the
deadline, abort, a late answer after both timeout and abort, slot exhaustion
followed by recycling, redirect credential scoping, and the resumable-fetch
downgrade refusal. They do not measure device latency or real TLS.

## Removing or restoring

Restore the pristine submodule at any time; the next build re-applies the
series:

```sh
git -C freeink-sdk checkout -- .
rm freeink-sdk/libs/network/SecureNet/include/DeadlineDns.h \
   freeink-sdk/libs/network/SecureNet/src/DeadlineDns.cpp \
   freeink-sdk/libs/network/SecureNet/test/host/test_deadline_dns.cpp
rm -r freeink-sdk/libs/network/SecureNet/test/host/stubs/lwip
```

To drop a patch permanently, remove the script from `extra_scripts` in
`platformio.ini`, delete the patch assets and script, and rebuild from a
pristine SDK.

## Updating the SDK pin

1. Bump the submodule and run `python3 scripts/patch_securenet.py`. A hash
   mismatch or a failing `git apply` means the reviewed SDK content changed.
2. Port the change onto the new sources, rerun the host tests, and rebuild the
   firmware profiles that ship (`default`, `x4pro-*`).
3. Refresh the upstream, intermediate and patched digests in `STAGES` in the
   script, and the pinned revision named in this document.
4. Submit both changes upstream to the FreeInk SDK. When they are released,
   delete the patch assets and script and bump the pin instead of carrying them.

## Combined dependency update — 29 October 2026

The deadline stage includes `c49fd5c4`: native `IPAddress` answers, conditional
AAAA fallback on an up global-IPv6 interface, and deadline-bounded waits when
lookup slots are occupied. The TLS stage was rebased over those sources without
removing chain or hostname verification. Exhausted busy slots map to a timeout;
IPv6 Timeout/Aborted outcomes propagate without being replaced by IPv4 failure.
Both stages have regenerated exact source digests. At that update the SDK commit
was still `44427f8`; the patch assets explained its local source modifications.
(Superseded by the pin advance below — the current pin is `6a208c0c`.)

The firmware clock gate now accepts a request budget and cancellation callback.
OPDS and plugin requests forward it to the existing SNTP wait, checked every
100 ms. This does not weaken the plausible-clock or certificate requirement.
Host tests and reproducible application checks passed; IPv6, DNS-slot pressure,
SNTP cancellation and wolfSSL behavior still need device acceptance.

## Pin advance — 12 November 2026

The pin moved from `44427f8d` to `5e94fe74` (the revision upstream `2.0.0` pins)
and then to `6a208c0c` (the revision `upstream/develop` pins). Both stages were
re-ported onto the new sources at each step and every digest in `STAGES` was
regenerated. Three upstream commits touch SecureNet between `44427f8d` and
`5e94fe74`: `c8a13c6` (bounded catalog requests), `e49d6b8` (connection-owned
buffers) and `7ca28e5` (fail fast on an unresolvable host name). One more,
`6a208c0` (skip oversized GitHub CSP response headers), sits between `5e94fe74`
and `6a208c0c`; it re-ported with no conflicts.

The second step is required by the firmware, not by the transport: with the pin
at `5e94fe74` the merged `upstream/develop` tree fails to build in
`lib/hal/HalStorage.cpp`, because `121d644 feat: report SD usage scan failures`
adds the `bool SDCardManager::sdUsedBytes(uint64_t&)` overload that develop's
`sdUsedBytes()` wrapper calls. Pinning the revision develop itself pins keeps
the firmware and the SDK at the pairing upstream tested, instead of dropping
upstream's SD-usage failure reporting to suit an older SDK.

What the re-port decided:

- **Record buffers: upstream wins.** `e49d6b8` replaced the allocator-hook buffer
  loan (`g_recordBuf`, `lendMalloc`, `SecureClient::_recordBuf`) with a
  connection-owned `GrowInputBuffer()` reservation gated on
  `FREEINK_WOLFSSL_RETAIN_INPUT_BUFFER`. That design was upstream's at `44427f8d`
  too, so the fork only ever reformatted it; the port keeps upstream's new
  `reserveFullRecord()` and `stop()` unchanged. `test/host/test_record_reservation.py`
  extracts those two functions by signature and still parses them.
- **`_tcpConnected` is kept.** Upstream's "no TLS 1.2 retry after a failed TCP
  connect" guard now sits alongside the fork's deadline guard and its
  verification-failure guard, so all three reasons to skip the retry apply.
- **`hostResolves()` stays, but the fork no longer calls it.** `7ca28e5` added it
  as a blocking pre-check; `DeadlineDns::resolve()` supersedes it on both the
  https and the plain-http path because it honours the deadline and the abort
  callback. The helper remains an exported SDK utility, unchanged.
- **Host tests follow the resolver.** `test_resumable_fetch.cpp` drops the
  out-of-line `reserveFullRecord()` stub (the header now defines it inline when
  wolfSSL is off) and its `hostResolves()` stub, and scripts `DeadlineDns::resolve`
  from the existing `dnsWorks` flag, so upstream's "a host that does not resolve
  fails without a connect attempt" case still tests the same behaviour through
  the fork's resolver. `test_bulk_request.cpp` gains the same resolver stub
  because it links its own `SecureClient` stubs.
- Formatting follows upstream wherever the fork had only reformatted a line, so
  the series stays reviewable as a behaviour change.

Verified: `python3 scripts/patch_securenet.py` applies both stages to a pristine
`6a208c0c` and is idempotent on a second run; the patch chain reproduces the
reviewed tree byte for byte; `sh freeink-sdk/libs/network/SecureNet/test/host/run.sh`
passes all three binaries (resumable fetch, bulk request, deadline DNS).
`WOLFSSL_VERIFY_PEER`, `wolfSSL_check_domain_name()`, the fail-closed CA bundle
check, `ConnectFailure` classification, redirect credential scoping and the
https→http downgrade refusal are all present in the ported transport, alongside
upstream's CSP-header skip.
Real wolfSSL handshakes and their RAM cost on X3/X4 remain device acceptance.
