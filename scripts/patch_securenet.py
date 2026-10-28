"""
PlatformIO pre: script: apply CrossPoint's SecureNet (FreeInk SDK) patches.

The pinned FreeInk SDK revision loads a CA bundle but never checks that the
peer certificate covers the requested host name, does not report why a
connection failed, keeps caller authorization headers across a followed
redirect to another host, and lets a resumable fetch follow an https -> http
redirect by default. The patches in `scripts/securenet_patches/` close those
gaps in the transport.

This is a temporary deviation from the pinned SDK: it patches the checked-out
submodule working tree and is meant to move upstream. See YBX_FORK.md.
Because the submodule stays dirty, committing `freeink-sdk` must not happen
until the upstream change lands and the pin is bumped.

Each target file must hash to the reviewed upstream bytes at the pinned
revision or to the reviewed patched bytes; anything else fails the build
instead of compiling an unreviewed transport. Already-patched files are left
untouched so incremental builds do not rewrite the SDK. Every patch is
validated before any file is changed.
"""

import hashlib
import os
from pathlib import Path
import subprocess


# Reviewed upstream (freeink-sdk 44427f8) and patched bytes.
PATCHES = (
    ("0001-secure-client-peer-hostname-verification.patch",
     (("include/SecureClient.h",
       "6d51e5d8908f30c4c99622e3d3b64c8c9c6d8daaf55a6868e3d01ca49e27a9f7",
       "8f618cf5662df303373d73a170a896493a42f68ea98b02713aab44d23ebf13d0"),
      ("src/SecureClient.cpp",
       "dced57c94310d1622d3315ba1a8bbb923586bbc9b7c01792bfcf5f810f6a0885",
       "2dc6af025972f1bde0048ed8ca45f81e02f92c53dd02c33bb18f0c02fcf88b22"))),
    ("0002-secure-http-client-failure-and-credentials.patch",
     (("include/SecureHttpClient.h",
       "b9814ad852cceb6f56cf7fe6a9b4a219ac7848b860ab3c54314184ed658549d0",
       "cce03852d94d87a7bbeb3faa0115f687f6660057ec5abcb5b3b1f2b8b4b227bd"),)),
    ("0003-resumable-fetch-failure-and-downgrade.patch",
     (("include/ResumableFetch.h",
       "5f2619d0df5316ae83362612a07b54890e3e1d7f83520b0354923de6493af1c1",
       "f3d46e3d7d93d29d9d7628ca4ce308644f0e90903704fbdf9b80e3720f13683a"),)),
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def is_within(path, root):
    # Path.is_relative_to() needs Python 3.9; the documented minimum is 3.8.
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def apply_patches(project_dir, securenet_dir):
    project = Path(project_dir).resolve()
    securenet = Path(securenet_dir).resolve()
    if not is_within(securenet, project):
        raise RuntimeError("SecureNet must be inside this project")
    if not securenet.is_dir():
        raise RuntimeError("SecureNet not found at " + str(securenet) +
                           "; run `git submodule update --init freeink-sdk`")

    process_env = {key: os.environ[key] for key in ("HOME", "PATH", "TMPDIR", "LANG") if key in os.environ}
    process_env.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
    # SecureNet lives inside the SDK git repository. Without this stop, git
    # resolves patch paths against the repository root and silently skips them
    # as "outside" the current directory; the ceiling makes it run as a plain
    # directory and apply relative to SecureNet.
    process_env["GIT_CEILING_DIRECTORIES"] = str(securenet.parent)
    pending = []
    for name, files in PATCHES:
        states = []
        for relative, upstream, patched in files:
            target = securenet / relative
            if not is_within(target.resolve(), securenet) or not target.is_file():
                raise RuntimeError("Missing or escaping SecureNet patch target: " + relative)
            current = digest(target)
            if current not in (upstream, patched):
                raise RuntimeError("Unrecognized SecureNet source: " + relative +
                                   " at " + str(target) +
                                   " (submodule revision changed? re-review the patches and hashes)")
            states.append(current == patched)
        if all(states):
            continue
        if any(states):
            raise RuntimeError("Partially patched SecureNet: " + name)
        patch = project / "scripts" / "securenet_patches" / name
        # Validate the entire set before changing any SDK files.
        result = subprocess.run(["git", "apply", "--check", str(patch)], cwd=securenet,
                                env=process_env, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("SecureNet patch does not apply: " + name + "\n" + result.stderr)
        pending.append((patch, files))

    for patch, files in pending:
        subprocess.run(["git", "apply", str(patch)], cwd=securenet, env=process_env, check=True)
        for relative, _, expected in files:
            if digest(securenet / relative) != expected:
                raise RuntimeError("Unexpected patched SecureNet source: " + relative)
        print("Applied SecureNet patch: " + patch.name)


if "Import" in globals():
    Import("env")  # noqa: F821 -- supplied by SCons
    project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
    apply_patches(project_dir, Path(project_dir) / "freeink-sdk" / "libs" / "network" / "SecureNet")
