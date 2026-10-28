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
    ("0001-deadline-dns-tcp.patch", (
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
    )),
    ("0002-tls-verify.patch", (
        ("libs/network/SecureNet/include/ResumableFetch.h",
         "5f2619d0df5316ae83362612a07b54890e3e1d7f83520b0354923de6493af1c1",
         "f3d46e3d7d93d29d9d7628ca4ce308644f0e90903704fbdf9b80e3720f13683a"),
        ("libs/network/SecureNet/include/SecureClient.h",
         "316c522684563669125ce3766f7b7cdb53f6bad36de62c93a449f70f1053e2f9",
         "c5c8d765b1c6521d2b4b8a969e88697974ab7307a9d8a87b723affe20efd5fde"),
        ("libs/network/SecureNet/include/SecureHttpClient.h",
         "c3390d6bc7b6cb245665c06f47d3923f91ae38ddcb05c3645cfcddd491bed067",
         "0c84257c07fe2de5caaa171d17588c5b5de39d04a461ce16ffce3fe81ba266d8"),
        ("libs/network/SecureNet/src/SecureClient.cpp",
         "276063c3f77397812e6b4c5f113e186d267dfdee62f2245f04e0db982cde7e42",
         "7eed5c046509dfb4e6c298685f58a3c1e5c97e28013b4c93af934ff79db329fe"),
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
