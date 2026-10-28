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
# freeink-sdk @ 44427f8.
STAGES = (
    ('0001-deadline-dns-tcp.patch', (
        ('libs/network/SecureNet/include/DeadlineDns.h', None, 'c8f5a33c11edaa1edca1147734c66c87489a113e014ba0887b8d5f2229481f2a'),
        ('libs/network/SecureNet/include/SecureClient.h', '6d51e5d8908f30c4c99622e3d3b64c8c9c6d8daaf55a6868e3d01ca49e27a9f7', 'c1bf74e8c909f87d44ab1a4e2019b772b70fa9b1e35c8cc3523a840dbd68a0c6'),
        ('libs/network/SecureNet/include/SecureHttpClient.h', 'b9814ad852cceb6f56cf7fe6a9b4a219ac7848b860ab3c54314184ed658549d0', 'de83a12410e5fc2864a969c937c6543ddc8b494834c7b1bf84d24c2493eaa558'),
        ('libs/network/SecureNet/src/DeadlineDns.cpp', None, '8fe27642e5b95c6b59fdfdfc1d1fbbad43f87ad219f82a36c9320c6609650aff'),
        ('libs/network/SecureNet/src/SecureClient.cpp', 'dced57c94310d1622d3315ba1a8bbb923586bbc9b7c01792bfcf5f810f6a0885', 'e3b3b21a6a34211a35ea43ec14146419e4b54465332e7b6721d7c7039ed14cda'),
        ('libs/network/SecureNet/test/host/run.sh', '165668ab60715550785961a181d229b8c299c5be9bbb7b6cf495c66b1282cb54', '83d87a8b2774c5b46ba98bdecbd6dfb501754679084bf2ad2e69c191e730057c'),
        ('libs/network/SecureNet/test/host/stubs/Arduino.h', 'd76b7604f0a5bff1c775e1ee980a612e716f820a8fb4868c12e97ca96f6b4622', 'c8b97cf8c9f2321a6523e630c3faeda340adaf87b379a94228cff689a0244761'),
        ('libs/network/SecureNet/test/host/stubs/lwip/dns.h', None, '1bfa69dc9992a44b51fa37a01eb388b333cb74621d9b93de5b2b9fc057428327'),
        ('libs/network/SecureNet/test/host/stubs/lwip/ip6_addr.h', None, '5b0907772dac831a0ee02ea17e42f03e300f038b43cbf6ed878bbff599cd3692'),
        ('libs/network/SecureNet/test/host/stubs/lwip/ip_addr.h', None, '63c7a1755a9cdce6f9baa1cdc6ae8ccabdbaddf6387d64d9dc630a21f0aa783b'),
        ('libs/network/SecureNet/test/host/stubs/lwip/netif.h', None, '314bd0c3106472ba448f32dac2ae24c1fdf8272adf60e18826a5731481c4ed41'),
        ('libs/network/SecureNet/test/host/stubs/lwip/tcpip.h', None, '955c17613c70539be362ab2df981853f8efdf8f594def16cefd676667883f3d7'),
        ('libs/network/SecureNet/test/host/test_deadline_dns.cpp', None, '328da1906943d2b8d7bbeb7110a0b37d22651a2f04173c086ca919bddc56e94a'),
        ('libs/network/SecureNet/test/host/test_resumable_fetch.cpp', '467c309745ce35a53ea9698b21457a31830c9a1e2217bbc65cd21a82e04bd123', '2a05dd4ba1d29de9eb341b092ca0d5715dd876649e014578fc7627c7368e78b3'),
    )),
    ('0002-tls-verify.patch', (
        ('libs/network/SecureNet/include/ResumableFetch.h', '5f2619d0df5316ae83362612a07b54890e3e1d7f83520b0354923de6493af1c1', 'f3d46e3d7d93d29d9d7628ca4ce308644f0e90903704fbdf9b80e3720f13683a'),
        ('libs/network/SecureNet/include/SecureClient.h', 'c1bf74e8c909f87d44ab1a4e2019b772b70fa9b1e35c8cc3523a840dbd68a0c6', '2dccb26f3692c7a04a6e73a695d8e0b40ed6a5170ed1585f2e716ba3e889a8e2'),
        ('libs/network/SecureNet/include/SecureHttpClient.h', 'de83a12410e5fc2864a969c937c6543ddc8b494834c7b1bf84d24c2493eaa558', 'd6b5197c7d8a645b49b0ebf5c4f130ce4a142b416c8fc5136e0cf8181d68a8c0'),
        ('libs/network/SecureNet/src/SecureClient.cpp', 'e3b3b21a6a34211a35ea43ec14146419e4b54465332e7b6721d7c7039ed14cda', 'd3f98ff7535ce9adfd622f4ed48dedf1f28536c54b1b83a184b10b2c7b7818a5'),
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
