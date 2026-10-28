#!/usr/bin/env python3
"""Upstream sync procedure for the YBX fork of CrossPoint Reader.

Default mode simulates the merge without touching the working tree, so it is safe
to run on a dirty checkout or before deciding anything. See --merge for the mode
that creates a sync branch and starts the real merge. This script never pushes,
never forces and never resets fork work.

Exit codes: 0 nothing to integrate and no findings, 1 usage or git error,
2 open conflicts or failed checks (a human must decide).
"""

from __future__ import annotations

import argparse
import datetime as dt
import fnmatch
import re
import subprocess
import sys
import tempfile
from pathlib import Path

UPSTREAM_URL_HINT = "crosspoint-reader/crosspoint-reader"
FORK_URL_HINT = "jhdvp/ybx-crosspoint"
DEFAULT_INVENTORY = "docs/ybx-fork-inventory.md"
DEFAULT_REPORT_DIR = ".ybx-sync.local"
MARKER_BEGIN = "<!-- ybx-sync-inventory:begin -->"
MARKER_END = "<!-- ybx-sync-inventory:end -->"
MAX_HUNKS = 3
MAX_HUNK_LINES = 24

# Fork behaviour that a clean-looking merge can still destroy. `contains` strings
# must survive, `absent` strings must stay out, `max_count` limits one line count.
INVARIANTS = [
    {
        "name": "OTA release feed",
        "path": "src/network/OtaReleasePolicy.cpp",
        "contains": ["jhdvp/ybx-crosspoint", "ybx-"],
        "absent": ["crosspoint-reader/crosspoint-reader"],
        "why": "a fork device must never install an upstream image",
    },
    {
        "name": "OTA updater uses fork policy",
        "path": "src/network/OtaUpdater.cpp",
        "contains": ["ota_policy::kReleaseFeedUrl", "ota_policy::releaseFeedAllowed", "ota_policy::assetDownloadAllowed"],
        "absent": ["crosspoint-reader/crosspoint-reader"],
        "why": "the updater must use the validated fork feed and asset policy",
    },
    {
        "name": "catalog auth capability",
        "path": "src/util/FirmwareIdentity.h",
        "contains": ["catalog.lists.auth"],
        "absent": [],
        "why": "the YBX catalog plugin does not work unless it is advertised",
    },
    {
        "name": "YBX SD plugin present",
        "path": "sd-card/plugins/ybx/manifest.json",
        "contains": ["catalog.lists.auth"],
        "absent": [],
        "why": "fork-owned catalog plugin package",
    },
    {
        "name": "plugin API name",
        "path": "src/network/html/shared/PluginHost.js.inc",
        "contains": ["window.CrossPoint"],
        "absent": [],
        "why": "protocol identifier, not branding",
    },
    {
        "name": "boot/sleep wordmark",
        "path": "src/activities/boot_sleep/BootActivity.cpp",
        "contains": ["STR_YBX"],
        "absent": [],
        "why": "YBX branding on boot",
    },
    {
        "name": "release matrix stays X3/X4",
        "path": ".github/workflows/release.yml",
        "contains": ["device: x3-x4"],
        "absent": [],
        "max_count": (r"^\s+-?\s*device:", 1),
        "why": "the fork publishes one board; extra rows mean upstream's matrix returned",
    },
    {
        "name": "translation keys",
        "path": "lib/I18n/translations/english.yaml",
        "contains": ["STR_YBX", "STR_YBX_HOME_CATALOGS"],
        "absent": [],
        "why": "home strings referenced by YbxHomeUi",
    },
]

# Paths that keep YBX data locations; checked with git grep across a tree.
TREE_INVARIANTS = [
    {
        "name": "SD config directory stays /.crosspoint/",
        "fixed": True,
        "pattern": "/.crosspoint/",
        "scope": ["src"],
        "min_hits": 1,
        "why": "moving it would orphan settings, progress and tokens on existing cards",
    },
]


class StepError(RuntimeError):
    pass


def git(repo: Path, *args: str, check: bool = True) -> str:
    proc = subprocess.run(
        ["git", *args], cwd=repo, text=True, capture_output=True, encoding="utf-8"
    )
    if check and proc.returncode != 0:
        raise StepError(f"git {' '.join(args)}: {proc.stderr.strip() or proc.stdout.strip()}")
    return proc.stdout.rstrip("\n")


def git_ok(repo: Path, *args: str) -> bool:
    return subprocess.run(["git", *args], cwd=repo, capture_output=True).returncode == 0


def rev_exists(repo: Path, rev: str) -> bool:
    proc = subprocess.run(
        ["git", "rev-parse", "--verify", "--quiet", f"{rev}^{{commit}}"],
        cwd=repo,
        text=True,
        capture_output=True,
    )
    return proc.returncode == 0


def blob(repo: Path, rev: str, path: str) -> str | None:
    proc = subprocess.run(
        ["git", "show", f"{rev}:{path}"], cwd=repo, text=True, capture_output=True
    )
    return proc.stdout if proc.returncode == 0 else None


# ---------------------------------------------------------------- inventory


class Area:
    def __init__(self, area, risk, handling, patterns, why):
        self.area = area
        self.risk = risk
        self.handling = handling
        self.patterns = patterns
        self.why = why

    def matches(self, path: str) -> bool:
        return any(fnmatch.fnmatch(path, p) for p in self.patterns)


def load_inventory(repo: Path, rel: str) -> list[Area]:
    text = (repo / rel).read_text(encoding="utf-8")
    try:
        begin = text.index(MARKER_BEGIN)
        end = text.index(MARKER_END)
    except ValueError:
        raise StepError(f"{rel}: markers {MARKER_BEGIN} / {MARKER_END} not found")
    areas = []
    for line in text[begin:end].splitlines():
        line = line.strip()
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) < 5 or cells[0] in ("Area", "") or set(cells[0]) <= {"-"}:
            continue
        patterns = [p.strip().strip("`") for p in cells[3].split(",") if p.strip()]
        areas.append(Area(cells[0], cells[1].lower(), cells[2].lower(), patterns, cells[4]))
    if not areas:
        raise StepError(f"{rel}: no inventory rows parsed")
    return areas


def area_of(areas: list[Area], path: str) -> Area | None:
    for area in areas:
        if area.matches(path):
            return area
    return None


def changed_paths(repo: Path, base: str, rev: str) -> list[str]:
    out = git(repo, "diff", "--name-only", f"{base}..{rev}")
    return [p for p in out.splitlines() if p]


def inventory_drift(repo: Path, areas: list[Area], fork_paths: list[str]) -> tuple[list[str], list[str]]:
    unclassified = [p for p in fork_paths if area_of(areas, p) is None]
    stale = [area.area for area in areas if not any(area.matches(p) for p in fork_paths)]
    return unclassified, stale


# ---------------------------------------------------------------- merge simulation


CONFLICT_IN_RE = re.compile(r"Merge conflict in (.+?)\s*$")
CONFLICT_OTHER_RE = re.compile(r"^CONFLICT \([^)]+\): (\S+)")


def merge_conflicts(repo: Path, ours: str, theirs: str) -> tuple[str | None, list[str], int]:
    """Run git merge-tree. Returns (result tree, conflicted paths, conflict count).

    The CONFLICT summary lines are authoritative; the per-path list from
    --name-only is not enough because rename/delete conflicts only appear in the
    messages.
    """
    proc = subprocess.run(
        ["git", "merge-tree", "--write-tree", ours, theirs],
        cwd=repo,
        text=True,
        capture_output=True,
    )
    if proc.returncode not in (0, 1):
        detail = (proc.stderr or proc.stdout).strip()
        if "no merge base" in detail or "not something we can merge" in detail:
            return None, [], 0
        raise StepError(f"git merge-tree: {detail}")
    lines = proc.stdout.splitlines()
    tree = lines[0].strip() if lines and re.fullmatch(r"[0-9a-f]{40}", lines[0].strip()) else None
    paths = []
    for line in lines:
        if not line.startswith("CONFLICT"):
            continue
        match = CONFLICT_IN_RE.search(line) or CONFLICT_OTHER_RE.match(line)
        if match:
            paths.append(match.group(1).strip())
    return tree, sorted(set(paths)), len(paths)


def extract_hunks(text: str) -> list[str]:
    """Conflict regions, marker to marker."""
    hunks, current = [], None
    for line in text.splitlines():
        if line.startswith("<<<<<<<"):
            current = [line]
        elif line.startswith(">>>>>>>"):
            if current is not None:
                current.append(line)
                hunks.append("\n".join(current))
                current = None
        elif current is not None:
            current.append(line)
    return hunks


def conflict_hunks(repo: Path, base: str, ours: str, theirs: str, path: str) -> list[str]:
    """Reproduce the conflicted region for one path without touching the worktree."""
    sides = {}
    for label, rev in (("ours", ours), ("base", base), ("theirs", theirs)):
        content = blob(repo, rev, path)
        sides[label] = "" if content is None else content
    with tempfile.TemporaryDirectory() as tmp:
        tmppath = Path(tmp) / Path(path).name
        tmppath.write_text(sides["ours"], encoding="utf-8")
        basepath = Path(tmp) / "base"
        theirpath = Path(tmp) / "theirs"
        basepath.write_text(sides["base"], encoding="utf-8")
        theirpath.write_text(sides["theirs"], encoding="utf-8")
        proc = subprocess.run(
            [
                "git",
                "merge-file",
                "-p",
                "-L",
                f"YBX ({ours})",
                "-L",
                f"upstream base ({base[:9]})",
                "-L",
                f"upstream ({theirs})",
                str(tmppath),
                str(basepath),
                str(theirpath),
            ],
            cwd=repo,
            text=True,
            capture_output=True,
        )
        if proc.returncode < 0:
            raise StepError(f"git merge-file failed for {path}: {proc.stderr.strip()}")
        return extract_hunks(proc.stdout)


def worktree_conflicts(repo: Path) -> list[str]:
    """Unmerged paths of an in-progress merge."""
    out = git(repo, "diff", "--name-only", "--diff-filter=U")
    return [p for p in out.splitlines() if p]


def worktree_conflict_markers(repo: Path, path: str) -> list[str]:
    target = repo / path
    if not target.is_file():
        return []
    text = target.read_text(encoding="utf-8", errors="replace")
    return extract_hunks(text)


# ---------------------------------------------------------------- checks


def resolve_pio(explicit: str | None) -> list[str]:
    if explicit:
        return [explicit]
    candidate = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if candidate.exists():
        return [str(candidate)]
    return ["pio"]


def summarize(text: str, limit: int = 12) -> list[str]:
    """Verdict lines from a build or ctest log, oldest first, tail preserved."""
    keep = []
    pattern = re.compile(
        r"(\[SUCCESS\]|\[FAILED\]|error:|Error \d+|Traceback|\d+/\d+ tests? (passed|failed)|"
        r"tests? passed out of|OUT-OF-DATE|FAILED TEST|RAM:\s+\[|Flash:\s+\[)"
    )
    for line in text.splitlines():
        if pattern.search(line) and line.strip() not in keep:
            keep.append(line.strip())
    if not keep:
        keep = [ln.strip() for ln in text.strip().splitlines()[-limit:] if ln.strip()]
    return keep[-limit:]


def run_checks(repo: Path, args: argparse.Namespace, report_dir: Path) -> list[dict]:
    pio = resolve_pio(args.pio)
    report_dir.mkdir(parents=True, exist_ok=True)
    results = []
    jobs = [("build default", [*pio, "run", "-e", "default"])]
    if not args.skip_unit_tests:
        jobs.append(("unit tests", [*pio, "run", "-t", "unit-tests"]))
    for name, cmd in jobs:
        stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%S")
        logfile = report_dir / f"{name.replace(' ', '-')}-{stamp}.log"
        print(f"* {name}: {' '.join(cmd)} (log: {logfile})", file=sys.stderr)
        with logfile.open("w", encoding="utf-8") as handle:
            proc = subprocess.run(cmd, cwd=repo, stdout=handle, stderr=subprocess.STDOUT, text=True)
        text = logfile.read_text(encoding="utf-8", errors="replace")
        results.append(
            {
                "name": name,
                "command": " ".join(cmd),
                "returncode": proc.returncode,
                "log": str(logfile),
                "summary": summarize(text),
            }
        )
    return results


def check_invariants(repo: Path, rev: str, worktree: bool = False) -> list[dict]:
    rows = []
    for spec in INVARIANTS:
        path = spec["path"]
        if worktree:
            content = (
                (repo / path).read_text(encoding="utf-8", errors="replace")
                if (repo / path).is_file()
                else None
            )
        else:
            content = blob(repo, rev, path)
        detail = []
        if content is None:
            rows.append({"name": spec["name"], "ok": False, "detail": [f"{path}: missing"]})
            continue
        for needle in spec["contains"]:
            if needle not in content:
                detail.append(f"missing {needle!r} in {path}")
        for needle in spec["absent"]:
            if needle in content:
                detail.append(f"unexpected {needle!r} in {path}")
        if spec.get("max_count"):
            regex, cap = spec["max_count"]
            hits = len([ln for ln in content.splitlines() if re.match(regex, ln)])
            if hits > cap:
                detail.append(f"{path}: {hits} lines match {regex!r}, expected at most {cap}")
        rows.append({"name": spec["name"], "ok": not detail, "detail": detail or ["ok"]})
    for spec in TREE_INVARIANTS:
        cmd = ["git", "grep", "-l"]
        if spec.get("fixed"):
            cmd.append("-F")
        cmd += ["-e", spec["pattern"]]
        cmd += ([] if worktree else [rev])
        cmd += ["--", *spec["scope"]]
        proc = subprocess.run(cmd, cwd=repo, text=True, capture_output=True)
        hits = [ln for ln in proc.stdout.splitlines() if ln.strip()]
        ok = len(hits) >= spec["min_hits"]
        rows.append(
            {
                "name": spec["name"],
                "ok": ok,
                "detail": [f"{len(hits)} files match"] if ok else [f"no match for {spec['pattern']!r}"],
            }
        )
    return rows


# ---------------------------------------------------------------- report


class Report:
    def __init__(self, title: str):
        self.title = title
        self.sections: list[tuple[str, list[str]]] = []

    def add(self, heading: str, lines) -> "Report":
        body = [lines] if isinstance(lines, str) else list(lines)
        self.sections.append((heading, body))
        return self

    def render(self) -> str:
        out = [f"# {self.title}", ""]
        for heading, body in self.sections:
            out.append(f"## {heading}")
            out.extend(body if body else ["(none)"])
            out.append("")
        return "\n".join(out)


def fmt_commit(repo: Path, rev: str) -> str:
    return git(repo, "log", "-1", "--format=%h %s", rev)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--remote", default="upstream", help="upstream remote name (default: upstream)")
    parser.add_argument("--branch", default="develop", help="upstream branch (default: develop)")
    parser.add_argument(
        "--target",
        help="revision to integrate instead of <remote>/<branch> (use with --no-fetch for tests)",
    )
    parser.add_argument("--no-fetch", action="store_true", help="skip git fetch (offline or simulated runs)")
    parser.add_argument("--merge", action="store_true", help="create the sync branch and really merge")
    parser.add_argument("--sync-branch", help="branch name for --merge (default sync/upstream-<sha>-<date>)")
    parser.add_argument("--abort", action="store_true", help="abort an in-progress merge on --sync-branch and return")
    parser.add_argument("--return-to", help="with --abort, branch to check out when sitting on the sync branch")
    parser.add_argument("--delete-sync-branch", action="store_true", help="with --abort, delete the sync branch")
    parser.add_argument("--inventory", default=DEFAULT_INVENTORY, help="inventory markdown path")
    parser.add_argument("--check-inventory", action="store_true", help="only validate the inventory and exit")
    parser.add_argument("--build", action="store_true", help="run the firmware build and host unit tests")
    parser.add_argument("--skip-unit-tests", action="store_true", help="with --build, only build")
    parser.add_argument("--pio", help="path to the PlatformIO executable")
    parser.add_argument("--report-dir", default=DEFAULT_REPORT_DIR, help="where reports and build logs go")
    parser.add_argument("--record", help="append a short sync entry to this tracked markdown log")
    args = parser.parse_args()

    repo = Path(git(Path.cwd(), "rev-parse", "--show-toplevel")).resolve()
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    report_dir = repo / args.report_dir
    areas = load_inventory(repo, args.inventory)
    start_branch = git(repo, "rev-parse", "--abbrev-ref", "HEAD")
    start_head = git(repo, "rev-parse", "HEAD")

    # Remotes: verify roles from URLs, not names.
    urls = {}
    for name in git(repo, "remote").splitlines():
        urls[name] = git(repo, "remote", "get-url", name)
    notes = []
    if args.remote not in urls:
        raise StepError(f"remote {args.remote!r} not configured; remotes: {', '.join(sorted(urls)) or 'none'}")
    if UPSTREAM_URL_HINT not in urls[args.remote]:
        notes.append(f"WARNING: {args.remote} URL is {urls[args.remote]}, expected {UPSTREAM_URL_HINT}")
    fork_remote = next((n for n, u in urls.items() if FORK_URL_HINT in u), None)
    if fork_remote is None:
        notes.append(f"WARNING: no remote points at {FORK_URL_HINT}; publish target unknown")

    target = args.target or f"{args.remote}/{args.branch}"
    if not args.no_fetch and not args.target:
        print(f"* fetching {args.remote} (no tags, no push)", file=sys.stderr)
        git(repo, "fetch", "--no-tags", args.remote, args.branch)
    if not rev_exists(repo, target):
        raise StepError(f"{target} does not resolve to a commit")

    base = git(repo, "merge-base", "HEAD", target)
    upstream_new = changed_paths(repo, base, target)
    fork_paths = changed_paths(repo, base, "HEAD")
    upstream_commits = int(git(repo, "rev-list", "--count", f"{base}..{target}"))
    fork_commits = int(git(repo, "rev-list", "--count", f"{target}..HEAD"))
    already_merged = git_ok(repo, "merge-base", "--is-ancestor", target, "HEAD")
    overlap = sorted(set(upstream_new) & set(fork_paths))

    report = Report(f"YBX upstream sync {stamp}")
    report.add(
        "Range",
        [
            f"- Start: `{start_branch}` @ `{start_head[:9]}`",
            f"- Integrate: `{target}` @ `{fmt_commit(repo, target)}`",
            f"- Merge base (integrated upstream SHA): `{base[:9]}`",
            f"- Upstream commits ahead of fork: {upstream_commits}",
            f"- Fork commits ahead of upstream: {fork_commits}",
            f"- Upstream touched paths: {len(upstream_new)}; YBX-owned paths: {len(fork_paths)}; overlap: {len(overlap)}",
        ]
        + notes,
    )

    unclassified, stale = inventory_drift(repo, areas, fork_paths)
    report.add(
        "Inventory health",
        [f"- Inventory: `{args.inventory}` ({len(areas)} areas covering {len(fork_paths)} paths)"]
        + [f"- UNCLASSIFIED (add to inventory): `{p}`" for p in unclassified]
        + [f"- STALE area (no longer changed by the fork): {a}" for a in stale],
    )
    if args.check_inventory:
        print(report.render())
        return 1 if unclassified or stale else 0

    # Predicted churn per YBX area, before any merge decision.
    churn = []
    for area in areas:
        mine = [p for p in fork_paths if area_of(areas, p) is area]
        theirs = [p for p in upstream_new if area_of(areas, p) is area]
        both = sorted(set(mine) & set(theirs))
        if both or (mine and theirs):
            churn.append(
                f"- {area.area} (risk {area.risk}, handling {area.handling}): "
                f"{len(both)} path(s) changed on both sides -> {', '.join(f'`{p}`' for p in both)}"
            )
    report.add(
        "Both-side churn",
        churn + [f"- Fork-only paths: {len(set(fork_paths) - set(overlap))}", f"- Upstream-only paths: {len(set(upstream_new) - set(overlap))}"],
    )

    conflicted: list[str] = []
    tree_oid: str | None = None
    merge_in_progress = git_ok(repo, "rev-parse", "-q", "--verify", "MERGE_HEAD")
    if merge_in_progress:
        conflicted = worktree_conflicts(repo)
        report.add(
            "Merge result",
            [
                f"- A merge is already in progress on `{start_branch}`: {len(conflicted)} unmerged path(s).",
                "- Nothing is simulated here; the report describes the working tree.",
            ],
        )
    elif already_merged:
        report.add("Merge result", [f"- `{target}` is already an ancestor of HEAD: nothing to integrate."])
    else:
        tree_oid, conflicted, count = merge_conflicts(repo, "HEAD", target)
        report.add(
            "Merge result (simulated)",
            [
                f"- git merge-tree: {count} conflicted path(s)" + (f", result tree `{tree_oid[:9]}`" if tree_oid else ""),
            ],
        )

    if args.abort:
        name = args.sync_branch
        if not name:
            raise StepError("--abort needs --sync-branch")
        if merge_in_progress:
            print(f"* aborting the merge in {name}", file=sys.stderr)
            git(repo, "merge", "--abort", check=False)
        current = git(repo, "rev-parse", "--abbrev-ref", "HEAD")
        return_to = args.return_to or (None if current == name else start_branch)
        if current == name:
            if not return_to:
                raise StepError(f"you are on the sync branch {name}; pass --return-to <branch>")
            git(repo, "checkout", return_to)
        if args.delete_sync_branch and rev_exists(repo, name):
            print(git(repo, "branch", "-d", name, check=False) or f"* deleted {name}", file=sys.stderr)
        print(report.render())
        return 0

    if args.merge and not already_merged and not merge_in_progress:
        name = args.sync_branch or f"sync/upstream-{target.split('/')[-1][:8]}-{dt.datetime.now(dt.timezone.utc):%Y%m%d}"
        if rev_exists(repo, name):
            raise StepError(f"branch {name} already exists; pass --sync-branch or delete it first")
        if git(repo, "status", "--porcelain", "--untracked-files=no"):
            raise StepError(
                "tracked changes present; commit or stash them before --merge "
                "(untracked files are fine)"
            )
        print(f"* creating sync branch {name} from {start_branch}", file=sys.stderr)
        git(repo, "checkout", "-b", name, "HEAD")
        print(f"* merging {target} (no force, no reset, no commit)", file=sys.stderr)
        git(repo, "merge", "--no-ff", "--no-commit", target, check=False)
        conflicted = worktree_conflicts(repo)
        if conflicted:
            report.add(
                "Sync branch",
                [
                    f"- Branch `{name}` now holds the merge in progress (nothing is committed).",
                    "- Resolve the paths below, keep every YBX behaviour, then `git commit` the merge yourself.",
                    f"- Back out with: --abort --sync-branch {name}",
                ],
            )
        else:
            report.add(
                "Sync branch",
                [
                    f"- Branch `{name}` holds a clean staged merge (nothing is committed).",
                    "- Read the YBX invariants below, run --build checks, then `git commit` the merge yourself.",
                    f"- Back out with: --abort --sync-branch {name}",
                ],
            )
    elif conflicted and not merge_in_progress:
        report.add(
            "How to resolve",
            [
                "- Re-run with --merge to start this merge on a throwaway sync branch, or resolve by area:",
                "- handling `reapply` = keep upstream, re-apply the YBX rename; "
                "`merge` = judge each hunk; `regenerate` = take one side then run the generator.",
            ],
        )

    # Conflicts grouped by YBX area, with the hunk each side wrote.
    by_area: dict[str, list[str]] = {}
    for path in conflicted:
        area = area_of(areas, path)
        key = f"{area.area} [{area.risk}/{area.handling}]" if area else "UNCLASSIFIED"
        hunks = (
            worktree_conflict_markers(repo, path)
            if merge_in_progress or args.merge
            else conflict_hunks(repo, base, "HEAD", target, path)
        )
        block = [f"- `{path}` ({len(hunks)} conflicted region(s))"]
        if area:
            block.append(f"  why YBX owns it: {area.why}")
        for hunk in hunks[:MAX_HUNKS]:
            shown = hunk.splitlines()[:MAX_HUNK_LINES]
            block.append("  ```")
            block.extend("  " + ln for ln in shown)
            block.append("  ```")
        if len(hunks) > MAX_HUNKS:
            block.append(f"  ... {len(hunks) - MAX_HUNKS} more region(s) in the file")
        by_area.setdefault(key, []).extend(block)
    if by_area:
        sections = []
        for key in sorted(by_area, key=lambda k: (k == "UNCLASSIFIED", k)):
            sections.append(f"### {key}")
            sections.extend(by_area[key])
            sections.append("")
        report.add("Conflicts by YBX area", sections)
    else:
        report.add("Conflicts by YBX area", ["- none"])

    # Protected behaviour: for a simulation read the merged tree, otherwise the worktree.
    if merge_in_progress or args.merge:
        check_rev, worktree_mode = "HEAD", True
    elif tree_oid:
        check_rev, worktree_mode = tree_oid, False
    else:
        check_rev, worktree_mode = "HEAD", False
    rows = check_invariants(repo, check_rev, worktree=worktree_mode)
    failed = [r for r in rows if not r["ok"]]
    report.add(
        "YBX invariants",
        [f"- checked against `{check_rev[:9]}`"]
        + [f"- {'OK  ' if r['ok'] else 'FAIL'} {r['name']}: {'; '.join(r['detail'])}" for r in rows],
    )

    if args.build:
        results = run_checks(repo, args, report_dir)
        lines = []
        for res in results:
            mark = "OK  " if res["returncode"] == 0 else "FAIL"
            lines.append(f"- {mark} {res['name']} (`{res['command']}`, rc={res['returncode']}, log `{res['log']}`)")
            lines.extend(f"    {ln}" for ln in res["summary"])
        report.add("Build and host tests", lines)
        failed = failed + [r for r in results if r["returncode"] != 0]
    else:
        report.add("Build and host tests", ["- skipped (pass --build to run pio run -e default and unit tests)"])

    version = None
    ini = blob(repo, check_rev, "platformio.ini") or ""
    match = re.search(r"^version = (\S+)", ini, re.MULTILINE)
    if match:
        version = match.group(1)
    submodule = git(repo, "submodule", "status", check=False).splitlines()
    report.add(
        "Record",
        [
            f"- Integrated upstream SHA: `{base[:9]}`"
            + ("" if already_merged else " (still to integrate)"),
            f"- Candidate upstream SHA: `{git(repo, 'rev-parse', target)[:9]}`",
            f"- Fork version string: {version or 'unknown'}",
            f"- SDK pin: {submodule[0] if submodule else 'none'}",
            f"- Open conflicts: {len(conflicted)} path(s)",
            "- Update the base commit line in YBX_FORK.md and append to docs/ybx-sync-log.md after the merge is committed.",
        ],
    )

    rendered = report.render()
    report_dir.mkdir(parents=True, exist_ok=True)
    logfile = report_dir / f"sync-{dt.datetime.now(dt.timezone.utc):%Y%m%d-%H%M%S}.md"
    logfile.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    print(f"\n* report: {logfile}", file=sys.stderr)

    if args.record:
        entry = [
            f"## {dt.datetime.now(dt.timezone.utc):%Y-%m-%d} — upstream `{git(repo, 'rev-parse', target)[:9]}`",
            "",
            f"- Integrated upstream SHA before this run: `{base[:9]}`",
            f"- Merge: {'already integrated' if already_merged else ('started on sync branch' if args.merge else 'simulated')} "
            f"— {len(conflicted)} conflicted path(s)",
            f"- Conflicts by area: {', '.join(sorted(by_area)) if by_area else 'none'}",
            f"- Invariants: {'all OK' if not [r for r in rows if not r['ok']] else 'FAILURES present'}",
            f"- Full report: `{logfile.relative_to(repo)}`",
            "",
        ]
        record_path = repo / args.record
        existing = record_path.read_text(encoding="utf-8") if record_path.exists() else ""
        record_path.write_text(existing.rstrip("\n") + "\n\n" + "\n".join(entry), encoding="utf-8")
        print(f"* appended entry to {args.record}", file=sys.stderr)

    if conflicted or failed or unclassified:
        return 2
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except StepError as err:
        print(f"error: {err}", file=sys.stderr)
        sys.exit(1)
