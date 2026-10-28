"""
PlatformIO pre: script: apply the reviewed SecureNet deadline-DNS/TCP patch.

The FreeInk SDK is a pinned submodule consumed through PlatformIO's
`symlink://` dependencies, so the build compiles the submodule tree itself.
The patch in `scripts/securenet_patches/` replaces the blocking
`Network.hostByName()` connect with a deadline-bounded lookup (see the patch
header) and adds the matching host tests.

Each touched file must hash to the reviewed upstream or patched bytes; any
mismatch fails the build instead of compiling an unreviewed transport. The
script is idempotent (patched files are left untouched), and on an SDK update
the hashes below must be re-reviewed together with the patch.

Concurrent builds are serialised with an advisory flock on the submodule's
`.git` handle (checked out as a file; locking it changes no content). A tree
that is only partially patched - an apply interrupted by a crash - is restored
to the reviewed upstream bytes and re-applied, so one bad build cannot leave
every later build failing with a manual restore.

Restore the pristine SDK with:
  git -C freeink-sdk checkout -- .
  rm freeink-sdk/libs/network/SecureNet/include/DeadlineDns.h \
     freeink-sdk/libs/network/SecureNet/src/DeadlineDns.cpp \
     freeink-sdk/libs/network/SecureNet/test/host/test_deadline_dns.cpp
  rm -r freeink-sdk/libs/network/SecureNet/test/host/stubs/lwip
"""

import fcntl
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys

PATCH = "0001-deadline-dns-tcp.patch"

# (path relative to the SDK root, upstream sha256 or None for a new file,
# patched sha256). Upstream files are from freeink-sdk @ 44427f8.
FILES = (
    ("libs/network/SecureNet/include/DeadlineDns.h", None,
     "c8f5a33c11edaa1edca1147734c66c87489a113e014ba0887b8d5f2229481f2a"),
    ("libs/network/SecureNet/src/DeadlineDns.cpp", None,
     "8fe27642e5b95c6b59fdfdfc1d1fbbad43f87ad219f82a36c9320c6609650aff"),
    ("libs/network/SecureNet/include/SecureClient.h",
     "6d51e5d8908f30c4c99622e3d3b64c8c9c6d8daaf55a6868e3d01ca49e27a9f7",
     "c1bf74e8c909f87d44ab1a4e2019b772b70fa9b1e35c8cc3523a840dbd68a0c6"),
    ("libs/network/SecureNet/include/SecureHttpClient.h",
     "b9814ad852cceb6f56cf7fe6a9b4a219ac7848b860ab3c54314184ed658549d0",
     "de83a12410e5fc2864a969c937c6543ddc8b494834c7b1bf84d24c2493eaa558"),
    ("libs/network/SecureNet/src/SecureClient.cpp",
     "dced57c94310d1622d3315ba1a8bbb923586bbc9b7c01792bfcf5f810f6a0885",
     "e3b3b21a6a34211a35ea43ec14146419e4b54465332e7b6721d7c7039ed14cda"),
    ("libs/network/SecureNet/test/host/run.sh",
     "165668ab60715550785961a181d229b8c299c5be9bbb7b6cf495c66b1282cb54",
     "83d87a8b2774c5b46ba98bdecbd6dfb501754679084bf2ad2e69c191e730057c"),
    ("libs/network/SecureNet/test/host/stubs/Arduino.h",
     "d76b7604f0a5bff1c775e1ee980a612e716f820a8fb4868c12e97ca96f6b4622",
     "c8b97cf8c9f2321a6523e630c3faeda340adaf87b379a94228cff689a0244761"),
    ("libs/network/SecureNet/test/host/stubs/lwip/dns.h", None,
     "1bfa69dc9992a44b51fa37a01eb388b333cb74621d9b93de5b2b9fc057428327"),
    ("libs/network/SecureNet/test/host/stubs/lwip/ip6_addr.h", None,
     "5b0907772dac831a0ee02ea17e42f03e300f038b43cbf6ed878bbff599cd3692"),
    ("libs/network/SecureNet/test/host/stubs/lwip/ip_addr.h", None,
     "63c7a1755a9cdce6f9baa1cdc6ae8ccabdbaddf6387d64d9dc630a21f0aa783b"),
    ("libs/network/SecureNet/test/host/stubs/lwip/netif.h", None,
     "314bd0c3106472ba448f32dac2ae24c1fdf8272adf60e18826a5731481c4ed41"),
    ("libs/network/SecureNet/test/host/stubs/lwip/tcpip.h", None,
     "955c17613c70539be362ab2df981853f8efdf8f594def16cefd676667883f3d7"),
    ("libs/network/SecureNet/test/host/test_deadline_dns.cpp", None,
     "328da1906943d2b8d7bbeb7110a0b37d22651a2f04173c086ca919bddc56e94a"),
    ("libs/network/SecureNet/test/host/test_resumable_fetch.cpp",
     "467c309745ce35a53ea9698b21457a31830c9a1e2217bbc65cd21a82e04bd123",
     "2a05dd4ba1d29de9eb341b092ca0d5715dd876649e014578fc7627c7368e78b3"),
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pending_files(sdk):
    """Files whose bytes are neither the patched nor the upstream digest.

    Unknown content fails the build; a patched digest is only trusted when it
    still matches the reviewed patch (checked by the reverse apply below).
    """
    pending = []
    for relative, upstream, patched in FILES:
        target = sdk / relative
        if not target.is_file():
            if upstream is None:
                pending.append(relative)
                continue
            raise RuntimeError("Missing SecureNet patch target: " + relative)
        current = digest(target)
        if current == patched:
            continue
        if current != upstream:
            raise RuntimeError("Unrecognized SecureNet source: " + relative)
        pending.append(relative)
    return pending


def restore_pristine(sdk):
    """Undo a partial apply: tracked files back to the reviewed upstream
    bytes, added files removed."""
    subprocess.run(
        ["git", "checkout", "--"] + [relative for relative, upstream, _ in FILES if upstream is not None],
        cwd=sdk, check=True)
    for relative, upstream, _ in FILES:
        if upstream is None:
            target = sdk / relative
            if target.is_file():
                target.unlink()
    shutil.rmtree(sdk / "libs" / "network" / "SecureNet" / "test" / "host" / "stubs" / "lwip",
                  ignore_errors=True)


def check_and_apply(sdk, patch):
    pending = pending_files(sdk)
    if not pending:
        # Fully patched: the reverse check also catches a patch asset that
        # drifted from the reviewed files.
        result = subprocess.run(["git", "apply", "--check", "--reverse", str(patch)], cwd=sdk,
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("Patched SecureNet differs from " + patch.name + "\n" + result.stderr)
        return False
    if len(pending) != len(FILES):
        # An earlier apply was interrupted mid-tree. Restore and retry once
        # from the reviewed upstream bytes instead of wedging every build.
        restore_pristine(sdk)
        pending = pending_files(sdk)
        if pending and len(pending) != len(FILES):
            raise RuntimeError("Partially patched SecureNet after restore: " + ", ".join(pending))

    # Validate the whole patch before changing any file.
    result = subprocess.run(["git", "apply", "--check", str(patch)], cwd=sdk,
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("SecureNet patch does not apply: " + patch.name + "\n" + result.stderr)
    subprocess.run(["git", "apply", str(patch)], cwd=sdk, check=True)
    for relative, _, patched in FILES:
        if digest(sdk / relative) != patched:
            raise RuntimeError("Unexpected patched SecureNet source: " + relative)
    print("Applied SecureNet patch: " + patch.name)
    return True


def apply_patches(project_dir):
    project = Path(project_dir).resolve()
    sdk = project / "freeink-sdk"
    patch = project / "scripts" / "securenet_patches" / PATCH
    if not (sdk / ".git").exists() or not patch.is_file():
        raise RuntimeError("SecureNet patch targets a freeink-sdk submodule checkout")

    # Serialise check+apply against other builds of this worktree. In a
    # submodule checkout `.git` is a regular gitlink file; flocking it is
    # advisory only and changes no tracked content.
    with open(sdk / ".git", "r") as lock_handle:
        fcntl.flock(lock_handle, fcntl.LOCK_EX)
        try:
            return check_and_apply(sdk, patch)
        finally:
            fcntl.flock(lock_handle, fcntl.LOCK_UN)


if "Import" in globals():
    Import("env")  # noqa: F821 -- supplied by SCons
    apply_patches(env["PROJECT_DIR"])  # noqa: F821
else:
    apply_patches(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent)
