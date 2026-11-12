"""
PlatformIO pre: script: apply CrossPoint's SecureNet (FreeInk SDK) patch series.

The FreeInk SDK is a pinned submodule consumed through PlatformIO's
`symlink://` dependencies, so the build compiles the submodule tree itself.
The patches in `scripts/securenet_patches/` are applied in name order:

  0001-deadline-dns-tcp.patch  replaces the blocking Network.hostByName()
                               connect with a deadline-bounded lookup;
  0002-tls-verify.patch        enables peer and host-name verification when a
                               CA bundle is set, classifies connect failures,
                               scopes credentials to their origin across
                               redirects, and refuses an https -> http
                               downgrade in ResumableFetch.

Both also add the matching host tests. They are a temporary deviation from the
pinned SDK and are meant to move upstream; see YBX_FORK.md. The submodule
pointer stays at its pin, so `git status` reports `freeink-sdk` as modified
after a build — do not commit inside the submodule.

Each stage declares the SHA-256 of every file before and after it. A file that
matches neither state fails the build instead of compiling an unreviewed
transport, and a stage is applied only when all its files are in the previous
state. A failure in a later stage rolls back the stages applied by this run.

Restore the pristine SDK with:
  git -C freeink-sdk checkout -- .
  rm freeink-sdk/libs/network/SecureNet/include/DeadlineDns.h \
     freeink-sdk/libs/network/SecureNet/src/DeadlineDns.cpp \
     freeink-sdk/libs/network/SecureNet/test/host/test_deadline_dns.cpp
  rm -r freeink-sdk/libs/network/SecureNet/test/host/stubs/lwip
"""

import hashlib
from pathlib import Path
import subprocess
import sys

# (patch asset, ((path relative to the SDK root, digest before, digest after), ...)).
# None means the file does not exist in that state. Upstream files are from
# freeink-sdk @ 6a208c0.
STAGES = (
    ('0001-deadline-dns-tcp.patch', (
        ('libs/network/SecureNet/include/DeadlineDns.h', None, 'c8f5a33c11edaa1edca1147734c66c87489a113e014ba0887b8d5f2229481f2a'),
        ('libs/network/SecureNet/include/SecureClient.h', '55389390b984cdb46e96b0f49e9b31c36f66fae10f46072637ac11a7e14fa498', '46a89cc555bb81422817e9c17782b376e72929a966a9de70132933d596005dc9'),
        ('libs/network/SecureNet/include/SecureHttpClient.h', '6e377a82269aa6d71a6ce1ae16166ec4d68c4dedf117a6fa4ca8b10e4f3ab977', 'ecaf17c21d4d95ff4885cd59742ba213e2435d3e877ccbe85bc0f431d4fe73d2'),
        ('libs/network/SecureNet/src/DeadlineDns.cpp', None, '8fe27642e5b95c6b59fdfdfc1d1fbbad43f87ad219f82a36c9320c6609650aff'),
        ('libs/network/SecureNet/src/SecureClient.cpp', 'a3c8fb6cb2914cb2d6d550f2d2430622ea06841403a156a47c25ab59e17ef3e2', '1182d828ef1da27f7612b2ed67493559252792d7a7c5e41862dfe8e4a172f9df'),
        ('libs/network/SecureNet/test/host/run.sh', 'bcff2d31dfee5e3ca635249eff303661a80c7d5ba51ebc9853e87f610009cb59', 'b2fafdb8ba75c2ec0f7c155aeb113295dea5820a8fe3327a441e21403f7479bc'),
        ('libs/network/SecureNet/test/host/stubs/Arduino.h', 'd76b7604f0a5bff1c775e1ee980a612e716f820a8fb4868c12e97ca96f6b4622', 'c8b97cf8c9f2321a6523e630c3faeda340adaf87b379a94228cff689a0244761'),
        ('libs/network/SecureNet/test/host/stubs/lwip/dns.h', None, '1bfa69dc9992a44b51fa37a01eb388b333cb74621d9b93de5b2b9fc057428327'),
        ('libs/network/SecureNet/test/host/stubs/lwip/ip6_addr.h', None, '5b0907772dac831a0ee02ea17e42f03e300f038b43cbf6ed878bbff599cd3692'),
        ('libs/network/SecureNet/test/host/stubs/lwip/ip_addr.h', None, '63c7a1755a9cdce6f9baa1cdc6ae8ccabdbaddf6387d64d9dc630a21f0aa783b'),
        ('libs/network/SecureNet/test/host/stubs/lwip/netif.h', None, '314bd0c3106472ba448f32dac2ae24c1fdf8272adf60e18826a5731481c4ed41'),
        ('libs/network/SecureNet/test/host/stubs/lwip/tcpip.h', None, '955c17613c70539be362ab2df981853f8efdf8f594def16cefd676667883f3d7'),
        ('libs/network/SecureNet/test/host/test_bulk_request.cpp', 'f159b8197afc08f2c34e6749a862721860f6def64c0a59e65b5e31f3a7a5ec19', '62b7c20e78dcee89bd188b021287dc2d6acdad305e2ff19a857cbc6b20c25386'),
        ('libs/network/SecureNet/test/host/test_deadline_dns.cpp', None, '328da1906943d2b8d7bbeb7110a0b37d22651a2f04173c086ca919bddc56e94a'),
        ('libs/network/SecureNet/test/host/test_resumable_fetch.cpp', '10c036eaa828df081f8fb4ccb7280b68929854829e96bf833f6e6f2cbc9345fd', 'ed61e04d488443af83d0bef088676a4254162b41b279aab1c5f4b60a156e2cad'),
    )),
    ('0002-tls-verify.patch', (
        ('libs/network/SecureNet/include/ResumableFetch.h', '5f2619d0df5316ae83362612a07b54890e3e1d7f83520b0354923de6493af1c1', 'f3d46e3d7d93d29d9d7628ca4ce308644f0e90903704fbdf9b80e3720f13683a'),
        ('libs/network/SecureNet/include/SecureClient.h', '46a89cc555bb81422817e9c17782b376e72929a966a9de70132933d596005dc9', 'c3a2fe1413c3dd3dba39e73652f0073421fe5e8603fe9c4c0cd641bd9f96d923'),
        ('libs/network/SecureNet/include/SecureHttpClient.h', 'ecaf17c21d4d95ff4885cd59742ba213e2435d3e877ccbe85bc0f431d4fe73d2', 'da85920de16a17739f94a18fda4bace024ec66e811283a899b006fd1b271dc90'),
        ('libs/network/SecureNet/src/SecureClient.cpp', '1182d828ef1da27f7612b2ed67493559252792d7a7c5e41862dfe8e4a172f9df', 'ecf01ddb68af7f9ed984f09456019d481573d450cabe5efa27ad7771a45ce94a'),
    )),
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def applied_stages(sdk):
    """Number of leading stages already applied; raises unless the tree matches
    exactly one stage boundary (pristine, or patched through stage N-1)."""
    for boundary in range(len(STAGES) + 1):
        # A stage's `before` is the state left by the previous stage, so walk
        # the stages in order and keep the last digest at or before `boundary`.
        expected = {}
        for index, stage in enumerate(STAGES):
            for relative, before, after in stage[1]:
                expected.setdefault(relative, before)
                if index < boundary:
                    expected[relative] = after
        matches = True
        for relative, digest_expected in expected.items():
            target = sdk / relative
            current = digest(target) if target.is_file() else None
            if current != digest_expected:
                matches = False
                break
        if matches:
            return boundary
    raise RuntimeError("Unrecognized or partially patched SecureNet sources "
                       "(submodule revision or patches changed? re-review the series)")


def apply_patches(project_dir):
    project = Path(project_dir).resolve()
    sdk = project / "freeink-sdk"
    if not (sdk / ".git").exists():
        raise RuntimeError("SecureNet patches target a freeink-sdk submodule checkout")

    pending = STAGES[applied_stages(sdk):]

    if not pending:
        # Fully patched: also catch a patch asset that drifted from the
        # reviewed files, since the digests above only describe the result.
        patch = project / "scripts" / "securenet_patches" / STAGES[-1][0]
        result = subprocess.run(["git", "apply", "--check", "--reverse", str(patch)], cwd=sdk,
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("Patched SecureNet differs from " + patch.name + "\n" + result.stderr)
        return False

    changed = []
    try:
        for name, files in pending:
            patch = project / "scripts" / "securenet_patches" / name
            if not patch.is_file():
                raise RuntimeError("Missing SecureNet patch asset: " + name)
            check = subprocess.run(["git", "apply", "--check", str(patch)], cwd=sdk,
                                   capture_output=True, text=True)
            if check.returncode:
                raise RuntimeError("SecureNet patch does not apply: " + name + "\n" + check.stderr)
            subprocess.run(["git", "apply", str(patch)], cwd=sdk, check=True)
            changed.append(patch)
            for relative, _, expected in files:
                if digest(sdk / relative) != expected:
                    raise RuntimeError("Unexpected patched SecureNet source: " + relative)
            print("Applied SecureNet patch: " + name)
    except BaseException:
        for patch in reversed(changed):
            subprocess.run(["git", "apply", "--reverse", str(patch)], cwd=sdk, check=False)
        raise
    return True


if "Import" in globals():
    Import("env")  # noqa: F821 -- supplied by SCons
    apply_patches(env["PROJECT_DIR"])  # noqa: F821
else:
    apply_patches(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent)
