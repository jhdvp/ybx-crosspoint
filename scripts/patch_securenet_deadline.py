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

PATCH = "0001-deadline-dns-tcp.patch"

# (path relative to the SDK root, upstream sha256 or None for a new file,
# patched sha256). Upstream files are from freeink-sdk @ 44427f8.
FILES = (
    ("libs/network/SecureNet/include/DeadlineDns.h", None,
     "70142e1279dd079cec65f975968442480a6ef0ae2688dafc2eef330675d920ca"),
    ("libs/network/SecureNet/src/DeadlineDns.cpp", None,
     "1ff8d8493ab56f9396d310348af7f052215bf29abce8f3880f1b50887232188a"),
    ("libs/network/SecureNet/include/SecureClient.h",
     "6d51e5d8908f30c4c99622e3d3b64c8c9c6d8daaf55a6868e3d01ca49e27a9f7",
     "316c522684563669125ce3766f7b7cdb53f6bad36de62c93a449f70f1053e2f9"),
    ("libs/network/SecureNet/include/SecureHttpClient.h",
     "b9814ad852cceb6f56cf7fe6a9b4a219ac7848b860ab3c54314184ed658549d0",
     "c3390d6bc7b6cb245665c06f47d3923f91ae38ddcb05c3645cfcddd491bed067"),
    ("libs/network/SecureNet/src/SecureClient.cpp",
     "dced57c94310d1622d3315ba1a8bbb923586bbc9b7c01792bfcf5f810f6a0885",
     "276063c3f77397812e6b4c5f113e186d267dfdee62f2245f04e0db982cde7e42"),
    ("libs/network/SecureNet/test/host/run.sh",
     "165668ab60715550785961a181d229b8c299c5be9bbb7b6cf495c66b1282cb54",
     "da2efbc3bd7aecacbbd35f9351d81708b0185a08185d37a531dc000e736f9c57"),
    ("libs/network/SecureNet/test/host/stubs/Arduino.h",
     "d76b7604f0a5bff1c775e1ee980a612e716f820a8fb4868c12e97ca96f6b4622",
     "8ec6a76e0fd765025ef119920a0f62c15f23e2425dd9c56fe774e7cbd24e8b68"),
    ("libs/network/SecureNet/test/host/stubs/lwip/dns.h", None,
     "f225d8a7419466cf3bd40e3c0acf7ee75156bc972c6b419055645a41b6afb5a0"),
    ("libs/network/SecureNet/test/host/stubs/lwip/ip_addr.h", None,
     "e0df1e3cdc8a686ad2a13d746d468f3526411f4f3962b4b506a9a9be065f5205"),
    ("libs/network/SecureNet/test/host/stubs/lwip/tcpip.h", None,
     "955c17613c70539be362ab2df981853f8efdf8f594def16cefd676667883f3d7"),
    ("libs/network/SecureNet/test/host/test_deadline_dns.cpp", None,
     "19937142d60513d619e8f40117f1cf0177c76f2b87a41226be9c9354fd86e3ff"),
    ("libs/network/SecureNet/test/host/test_resumable_fetch.cpp",
     "467c309745ce35a53ea9698b21457a31830c9a1e2217bbc65cd21a82e04bd123",
     "f826f71e03e8259ded1f3e6054686e69640eb9d61a7adf5bae910b8320cf9a0a"),
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def apply_patches(project_dir):
    project = Path(project_dir).resolve()
    sdk = project / "freeink-sdk"
    patch = project / "scripts" / "securenet_patches" / PATCH
    if not (sdk / ".git").exists() or not patch.is_file():
        raise RuntimeError("SecureNet patch targets a freeink-sdk submodule checkout")

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
    if not pending:
        # Fully patched: the reverse check also catches a patch asset that
        # drifted from the reviewed files.
        result = subprocess.run(["git", "apply", "--check", "--reverse", str(patch)], cwd=sdk,
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("Patched SecureNet differs from " + PATCH + "\n" + result.stderr)
        return False
    if len(pending) != len(FILES):
        raise RuntimeError("Partially patched SecureNet: " + ", ".join(pending))

    # Validate the whole patch before changing any file.
    result = subprocess.run(["git", "apply", "--check", str(patch)], cwd=sdk,
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("SecureNet patch does not apply: " + PATCH + "\n" + result.stderr)
    subprocess.run(["git", "apply", str(patch)], cwd=sdk, check=True)
    for relative, _, patched in FILES:
        if digest(sdk / relative) != patched:
            raise RuntimeError("Unexpected patched SecureNet source: " + relative)
    print("Applied SecureNet patch: " + PATCH)
    return True


if "Import" in globals():
    Import("env")  # noqa: F821 -- supplied by SCons
    apply_patches(env["PROJECT_DIR"])  # noqa: F821
else:
    apply_patches(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent)
