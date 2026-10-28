# YBX SDK source patches

The firmware compiles the pinned FreeInk SDK submodule directly: `platformio.ini`
declares every SDK library as `symlink://freeink-sdk/libs/...`, so a change that
must ship before the next SDK release has to be carried as a reviewed patch in
this repository, not as an edit inside the submodule.

Each patch is two files:

- `scripts/<topic>_patches/<n>-<name>.patch` — the diff, reviewed like source;
- `scripts/patch_<topic>.py` — a PlatformIO `pre:` script that applies it.

The script verifies every touched file against reviewed SHA-256 digests before
touching anything, fails the build on unknown SDK content, and is idempotent so
incremental builds do not rewrite sources. It modifies the submodule working
tree, so `git status` reports `freeink-sdk` as modified while a patch is
applied; do not commit inside the submodule.

## Applied patches

| Patch | Script | SDK area |
| --- | --- | --- |
| `scripts/securenet_patches/0001-deadline-dns-tcp.patch` | `scripts/patch_securenet_deadline.py` | `libs/network/SecureNet` |

### SecureNet: deadline-bounded DNS and TCP

Pinned SDK revision: `44427f8d51866848b26bea01197f499f9fee5c4b`.

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

Host tests (in the SDK test harness; run after a build or after
`python3 scripts/patch_securenet_deadline.py` has applied the patch):

```sh
sh freeink-sdk/libs/network/SecureNet/test/host/run.sh
```

They cover deadline arithmetic across the `millis()` wraparound, literal-address
parsing, a resolver cache hit, NXDOMAIN, a blackholed resolver stopping at the
deadline, abort, a late answer after both timeout and abort, and slot
exhaustion followed by recycling. They do not measure device latency.

## Removing or restoring

Restore the pristine submodule at any time; the next build re-applies the patch:

```sh
git -C freeink-sdk checkout -- .
rm freeink-sdk/libs/network/SecureNet/include/DeadlineDns.h \
   freeink-sdk/libs/network/SecureNet/src/DeadlineDns.cpp \
   freeink-sdk/libs/network/SecureNet/test/host/test_deadline_dns.cpp
rm -r freeink-sdk/libs/network/SecureNet/test/host/stubs/lwip
```

To drop a patch permanently, remove its script from `base.extra_scripts` in
`platformio.ini`, delete the two files, and rebuild from a pristine SDK.

## Updating the SDK pin

1. Bump the submodule and try `python3 scripts/patch_securenet_deadline.py`.
   A hash mismatch or a failing `git apply` means the reviewed SDK content
   changed.
2. Port the change onto the new sources, rerun the host tests, and rebuild the
   firmware profiles that ship (`default`, `x4pro-*`).
3. Refresh the upstream and patched digests in `FILES` in the script, and the
   pinned revision named in this document.
4. Submit the change upstream to the FreeInk SDK. When it is released, delete
   the patch asset and script and bump the pin instead of carrying them.
