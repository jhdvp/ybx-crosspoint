#!/usr/bin/env python3
"""Package validated YBX firmware release assets from a clean commit.

Produces, per board, the OTA application image and the USB factory image with
their checksums, the build provenance (commit, SDK pin, toolchain) and a release
notes template. It never creates a GitHub release, never pushes and never tags:
publishing stays with the owner (see docs/ybx-release-packaging.md).
"""
from __future__ import annotations

import argparse
import configparser
import datetime
import hashlib
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
BOARD_TAG_MAGIC = b'CROSSPOINT-BOARD-V1:'

# esp_image_header_t / esp_image_segment_header_t sizes and the appended hash.
IMAGE_HEADER_SIZE = 24
SEGMENT_HEADER_SIZE = 8
SHA_TRAILER_SIZE = 32
CHIP_IDS = {
    'esp32': 0,
    'esp32c2': 16,
    'esp32c3': 5,
    'esp32c6': 13,
    'esp32h2': 18,
    'esp32p4': 17,
    'esp32s3': 9,
}


@dataclass(frozen=True)
class Board:
    """One release target: the PlatformIO env, its hardware identity and how the
    published assets are named. `status` is 'ready' only once the owner has
    accepted the device matrix for that board."""

    env: str
    esptool_chip: str
    board_tag: str
    asset_suffix: str
    partitions: str
    flash_mode: str
    flash_size: str
    status: str

    @property
    def name(self) -> str:
        return self.asset_suffix


BOARDS = (
    Board('gh_release', 'esp32c3', 'x4', 'x3-x4', 'partitions.csv', 'dio', '16MB', 'ready'),
    Board('x4pro-gh_release', 'esp32s3', 'x4pro', 'x4pro', 'partitions.csv', 'dio', '16MB', 'pending'),
    Board('x4c-gh_release', 'esp32s3', 'x4c', 'x4c', 'partitions.csv', 'dio', '16MB', 'pending'),
    Board('sticky-gh_release', 'esp32s3', 'sticky', 'sticky', 'partitions.csv', 'dio', '16MB', 'pending'),
    Board('papermono-gh_release', 'esp32s3', 'papermono', 'papermono', 'partitions.csv', 'dio', '16MB', 'pending'),
    Board('eego_a4-gh_release', 'esp32s3', 'eego_a4', 'eego_a4', 'partitions.csv', 'dio', '16MB', 'pending'),
    Board('metalio_eink4-gh_release', 'esp32s3', 'metalio_eink4', 'metalio_eink4', 'partitions.csv', 'dio',
          '16MB', 'pending'),
    Board('picco-gh_release', 'esp32c3', 'picco', 'picco', 'partitions_picco.csv', 'dio', '16MB', 'pending'),
)


class PackagingError(RuntimeError):
    """A release asset is not publishable; the message states why."""


# --- naming rules (must agree with src/network/OtaReleasePolicy.cpp) ---------

def suffix_for_board(board_tag: str) -> str:
    """The published asset suffix for a compiled compatibility tag. The combined
    X3/X4 ESP32-C3 image is tagged 'x4' and publishes as 'x3-x4'."""
    if not board_tag:
        raise PackagingError('empty board tag')
    return 'x3-x4' if board_tag == 'x4' else board_tag


def app_asset_name(version: str, board_tag: str) -> str:
    """The only asset name the OTA path ever asks for."""
    if not version or not re.fullmatch(r'[0-9A-Za-z._+-]+', version):
        raise PackagingError(f'unusable version string: {version!r}')
    return f'ybx-{version}-{suffix_for_board(board_tag)}.bin'


def factory_asset_name(version: str, board_tag: str) -> str:
    """The USB factory image. Distinct from the application name by construction,
    so a factory blob can never be offered as an OTA application image."""
    return f'ybx-{version}-{suffix_for_board(board_tag)}-factory.bin'


def resolve_release_tag(tag: str, configured_version: str) -> tuple[str, str]:
    """Validate a release tag against platformio.ini's version and return
    (version, channel). Mirrors .github/workflows/release.yml: a published tag is
    the configured numeric version, optionally prefixed with 'v', plus a literal
    'rc' for preview builds. Nothing else is a YBX release."""
    if not tag:
        raise PackagingError('a release tag is required')
    version = tag[1:] if tag[:1] in ('v', 'V') else tag
    if version == f'{configured_version}rc':
        return version, 'preview'
    if version == configured_version:
        return version, 'stable'
    raise PackagingError(
        f'tag {tag!r} does not match platformio.ini version {configured_version!r}: expected '
        f'{configured_version} (stable) or {configured_version}rc (preview), with an optional leading v')


def configured_version(ini_path: Path) -> str:
    parser = configparser.ConfigParser()
    parser.read(ini_path, encoding='utf-8')
    if not parser.has_option('crosspoint', 'version'):
        raise PackagingError(f'{ini_path} has no [crosspoint] version')
    return parser.get('crosspoint', 'version').strip()


# --- ESP image inspection ---------------------------------------------------

def esp_image_info(data: bytes) -> dict:
    """Parse esp_image_header_t and the segment table enough to compute the exact
    length the image declares. Raises PackagingError on a non-image."""
    if len(data) < IMAGE_HEADER_SIZE:
        raise PackagingError(f'image shorter than an ESP header ({len(data)} bytes)')
    if data[0] != 0xE9:
        raise PackagingError(f'not an ESP image: first byte 0x{data[0]:02X}')
    segment_count = data[1]
    chip_id = int.from_bytes(data[12:14], 'little')
    hash_appended = data[23] != 0
    position = IMAGE_HEADER_SIZE
    for index in range(segment_count):
        if position + SEGMENT_HEADER_SIZE > len(data):
            raise PackagingError(f'segment {index} header overruns the file')
        length = int.from_bytes(data[position + 4:position + 8], 'little')
        position += SEGMENT_HEADER_SIZE + length
    # The image ends at 16-byte aligned padding holding the XOR checksum, plus the
    # SHA-256 trailer for hash_appended builds.
    pad_end = (position + 16) & ~15
    declared = pad_end + (SHA_TRAILER_SIZE if hash_appended else 0)
    return {
        'segments': segment_count,
        'chip_id': chip_id,
        'hash_appended': hash_appended,
        'declared_length': declared,
    }


def embedded_board_tags(data: bytes) -> list[str]:
    """Board compatibility tags found in the image, in order."""
    tags = []
    start = 0
    while True:
        index = data.find(BOARD_TAG_MAGIC, start)
        if index < 0:
            return tags
        end = data.find(b';', index + len(BOARD_TAG_MAGIC))
        if end < 0:
            return tags
        tags.append(data[index + len(BOARD_TAG_MAGIC):end].decode('ascii', 'replace'))
        start = end + 1


def app_partition_offset(csv_path: Path) -> int:
    """Flash offset of the ota_0 app partition, read from the board's CSV."""
    for line in csv_path.read_text(encoding='utf-8').splitlines():
        fields = [field.strip() for field in line.split(',') if field.strip()]
        if len(fields) >= 5 and fields[1] == 'app' and fields[2] == 'ota_0':
            return int(fields[3], 16)
    raise PackagingError(f'{csv_path.name} has no app ota_0 partition')


def classify_application_image(data: bytes, board_tag: str, esptool_chip: str) -> None:
    """Assert `data` is exactly one complete application image for this board.

    A truncated image declares more than it holds; a USB factory blob declares the
    leading bootloader image only, so its trailing content makes the lengths differ
    — both are refused here, and the device refuses them over OTA as well.
    """
    info = esp_image_info(data)
    if info['chip_id'] != CHIP_IDS[esptool_chip]:
        raise PackagingError(
            f"application image is for chip id {info['chip_id']}, expected "
            f"{CHIP_IDS[esptool_chip]} ({esptool_chip})")
    if info['declared_length'] != len(data):
        raise PackagingError(
            f"application image length mismatch: file {len(data)} bytes, image declares "
            f"{info['declared_length']} (truncated, padded wrong, or a factory blob)")
    tags = embedded_board_tags(data)
    if board_tag not in tags:
        raise PackagingError(f'application image carries no {board_tag!r} compatibility tag (found {tags})')
    if len(data) < 64 * 1024:
        raise PackagingError(f'application image is only {len(data)} bytes')


def classify_factory_image(data: bytes, application: bytes, board_tag: str, app_offset: int) -> None:
    """Assert `data` is the merged factory blob and that it carries this exact
    application image at the app partition offset."""
    if len(data) != app_offset + len(application):
        raise PackagingError(
            f'factory blob is {len(data)} bytes, expected {app_offset + len(application)}')
    if data[app_offset:app_offset + len(application)] != application:
        raise PackagingError('factory blob does not contain the application image at its partition offset')
    # A factory blob is not a valid application image, and must never be accepted
    # as one: the app check has to reject it.
    try:
        classify_application_image(data, board_tag, 'esp32c3' if data[12:14] else 'esp32c3')
    except PackagingError:
        return
    raise PackagingError('factory blob was accepted as an application image; asset names could be swapped')


# --- checksums and documents ------------------------------------------------

def sha256_hex(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def checksums_text(entries: list[tuple[str, int]]) -> str:
    """sha256sum-compatible lines: hash, two spaces, path relative to the folder."""
    return ''.join(f'{digest}  {name}\n' for name, digest in sorted(entries))


def provenance(repo_root: Path) -> dict:
    def git(*args: str) -> str:
        return subprocess.run(['git', *args], cwd=repo_root, capture_output=True, text=True,
                              check=True).stdout.strip()

    status = git('status', '--porcelain')
    if status:
        raise PackagingError('working tree is not clean; commit or stash before packaging:\n' + status)
    if git('rev-parse', '--verify', 'HEAD') == '':
        raise PackagingError('HEAD does not resolve to a commit')
    submodule = ''
    try:
        raw = git('submodule', 'status')
    except subprocess.CalledProcessError:
        raw = ''
    for line in raw.splitlines():
        if line.strip().endswith('freeink-sdk'):
            if line[0] in '-+U':
                raise PackagingError(f'FreeInk SDK is not at the pinned revision: {line.strip()}')
            submodule = line.split()[0]
    if not submodule:
        raise PackagingError('FreeInk SDK submodule state could not be read')
    ini = (repo_root / 'platformio.ini').read_text(encoding='utf-8')
    platform = re.search(r'^platform\s*=\s*(\S+)', ini, re.M)
    core = subprocess.run(['pio', '--version'], capture_output=True, text=True)
    return {
        'commit': git('rev-parse', 'HEAD'),
        'short_commit': git('rev-parse', '--short=7', 'HEAD'),
        'branch': git('rev-parse', '--abbrev-ref', 'HEAD'),
        'sdk_pin': submodule,
        'platform': platform.group(1) if platform else 'unknown',
        'core': core.stdout.strip() if core.returncode == 0 else 'PlatformIO Core not on PATH',
        'tags': git('tag', '--points-at', 'HEAD') or '(none)',
    }


def release_info_text(version: str, channel: str, board: Board, assets: list[tuple[str, int, str]],
                      build_state: dict, now: str) -> str:
    lines = [
        f'release version    : {version}',
        f'channel            : {channel}',
        f'packaged at (UTC)  : {now}',
        f'source commit      : {build_state["commit"]}',
        f'source branch      : {build_state["branch"]}',
        f'tags on commit     : {build_state["tags"]}',
        f'FreeInk SDK pin    : {build_state["sdk_pin"]}',
        f'SDK is the pinned submodule, unmodified: {"yes" if build_state["sdk_pin"] else "no"}',
        f'platform pin       : {build_state["platform"]}',
        f'PlatformIO Core    : {build_state["core"]}',
        f'board env          : {board.env}',
        f'target chip        : {board.esptool_chip}',
        f'compatibility tag  : {board.board_tag}',
        f'partition table    : {board.partitions}',
        f'board acceptance   : {board.status}',
        '',
        'assets:',
    ]
    for name, size, role in assets:
        lines.append(f'  {name}  {size} bytes  {role}')
    lines += [
        '',
        'OTA selects the application asset by exact name, so the factory asset is never',
        'offered to a device (src/network/OtaReleasePolicy.cpp). OTA installs stable',
        'builds only; preview builds are installed from the SD card.',
    ]
    return '\n'.join(lines) + '\n'


RELEASE_NOTES_TEMPLATE = """# YBX Reader {version} ({channel})

Draft produced by `scripts/package_release.py`. The fork owner writes and owns the
release notes; do not publish this template as-is.

## Assets

| File | Use with | Size |
| --- | --- | --- |
| `{app}` | OTA update on X3/X4, or `esptool write_flash 0x10000` | {app_size} bytes |
| `{factory}` | First USB flash / recovery: `esptool write_flash 0x0` | {factory_size} bytes |

SHA-256:

```
{checksums}```

## Installation

1. Back up the SD card and read the whole flash (`read_flash 0x0 0x1000000`).
2. Devices already on a stable YBX build: Settings -> Update firmware (OTA).
3. Devices on a preview build, or not running YBX: install `{app}` from the SD
   card, or flash it over USB at `0x10000`.
4. A device whose partition table or bootloader must be replaced: flash `{factory}`
   at `0x0` and erase stale otadata (`erase_region 0xe000 0x2000`).

{notes}

## Build provenance

- source commit `{commit}` (branch `{branch}`)
- FreeInk SDK submodule `{sdk}`
- toolchain: {platform}, {core}
- board environment `{env}`, chip {chip}, compatibility tag `{tag}`

## Limitations to restate before publishing

- HTTPS certificate verification is disabled in the inherited transport, so this
  build must not be described as public or hardened.
- Hardware acceptance is recorded by the owner; a successful build is not evidence
  that a device works. See docs/ybx-release-packaging.md for the checklist.
- OTA carries stable builds only. Preview-to-preview updates use the SD card.
- Devices still on stock upstream CrossPoint must be flashed over USB or with the
  SD installer; OTA cannot cross that fork boundary.
"""


def release_notes_text(version: str, channel: str, board: Board, app: tuple[str, int],
                       factory: tuple[str, int], checksums: str, build_state: dict) -> str:
    return RELEASE_NOTES_TEMPLATE.format(
        version=version,
        channel=channel,
        app=app[0],
        app_size=app[1],
        factory=factory[0],
        factory_size=factory[1],
        checksums=checksums,
        notes='<!-- Owner: what changed, known issues, who tested it, on which devices. -->',
        commit=build_state['commit'],
        branch=build_state['branch'],
        sdk=build_state['sdk_pin'],
        platform=build_state['platform'],
        core=build_state['core'],
        env=board.env,
        chip=board.esptool_chip,
        tag=board.board_tag,
    )


# --- builds and merges ------------------------------------------------------

def pio_command() -> list[str]:
    penv = Path.home() / '.platformio' / 'penv' / 'bin' / 'pio'
    return [str(penv)] if penv.exists() else ['pio']


def build_board(board: Board, preview: bool, commit: str, build_dir: Path) -> None:
    command = pio_command() + ['run', '-e', board.env, '-j1']
    environment = dict(os.environ)
    if preview:
        environment['CROSSPOINT_RC_HASH'] = commit
    print(f':: building {board.env} ({"preview" if preview else "stable"})')
    subprocess.run(command, cwd=build_dir, env=environment, check=True)


def merge_factory(esptool: list[str], board: Board, boot: Path, parts: Path, app: Path, app_offset: int,
                  destination: Path) -> None:
    command = esptool + ['--chip', board.esptool_chip, 'merge_bin', '-o', str(destination),
                         '--flash_mode', board.flash_mode, '--flash_size', board.flash_size,
                         '0x0', str(boot), '0x8000', str(parts), hex(app_offset), str(app)]
    print(f':: merging {destination.name}')
    subprocess.run(command, check=True)


def find_esptool(explicit: str | None) -> list[str]:
    if explicit:
        return [explicit]
    packaged = Path.home() / '.platformio' / 'packages' / 'tool-esptoolpy' / 'esptool.py'
    if packaged.exists():
        return [sys.executable, str(packaged)]
    if shutil.which('esptool'):
        return ['esptool']
    if shutil.which('esptool.py'):
        return ['esptool.py']
    raise PackagingError('esptool not found; install it (pip install esptool) or pass --esptool')


def artefact_dir(build_root: Path, env: str) -> Path:
    directory = build_root / env
    if not directory.is_dir():
        raise PackagingError(f'{directory} does not exist; build {env} first or drop --skip-build')
    return directory


# --- orchestration ----------------------------------------------------------

def package(board: Board, version: str, channel: str, build_root: Path, output: Path, esptool: list[str],
            build_state: dict, now: str) -> list[Path]:
    source = artefact_dir(build_root, board.env)
    application = source / 'firmware.bin'
    bootloader = source / 'bootloader.bin'
    table = source / 'partitions.bin'
    for path in (application, bootloader, table):
        if not path.is_file():
            raise PackagingError(f'{path} is missing')

    app_bytes = application.read_bytes()
    boot_bytes = bootloader.read_bytes()
    app_offset = app_partition_offset(board.parent / board.partitions)
    classify_application_image(app_bytes, board.board_tag, board.esptool_chip)
    # The bootloader and the application must be built for the same silicon.
    if esp_image_info(boot_bytes)['chip_id'] != esp_image_info(app_bytes)['chip_id']:
        raise PackagingError(f'{board.env}: bootloader and application chip ids differ')

    output.mkdir(parents=True, exist_ok=True)
    app_name = app_asset_name(version, board.board_tag)
    factory_name = factory_asset_name(version, board.board_tag)
    if app_name == factory_name:
        raise PackagingError('asset naming collision between application and factory images')
    app_path = output / app_name
    factory_path = output / factory_name
    shutil.copyfile(application, app_path)
    merge_factory(esptool, board, bootloader, table, application, app_offset, factory_path)

    factory_bytes = factory_path.read_bytes()
    classify_factory_image(factory_bytes, app_bytes, board.board_tag, app_offset)
    if factory_bytes == app_bytes:
        raise PackagingError('factory image is identical to the application image')

    entries = [(app_name, sha256_hex(app_path)), (factory_name, sha256_hex(factory_path))]
    sums = checksums_text(entries)
    (output / f'SHA256SUMS-{board.asset_suffix}.txt').write_text(sums, encoding='utf-8')
    assets = [(app_name, len(app_bytes), 'OTA application image'),
              (factory_name, len(factory_bytes), 'USB factory image (flash at 0x0)')]
    (output / f'release-info-{board.asset_suffix}.txt').write_text(
        release_info_text(version, channel, board, assets, build_state, now), encoding='utf-8')
    (output / f'RELEASE_NOTES-{board.asset_suffix}.md').write_text(
        release_notes_text(version, channel, board, (app_name, len(app_bytes)),
                           (factory_name, len(factory_bytes)), sums, build_state), encoding='utf-8')
    return [app_path, factory_path]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--tag', required=True, help='release tag, e.g. 2.0.2 or 2.0.2rc')
    parser.add_argument('--board', default='x3-x4', help='board asset suffix (see BOARDS)')
    parser.add_argument('--out', default=None, help='output folder (default dist/<tag>/)')
    parser.add_argument('--build-dir', default=None, help='repository root holding .pio/build (default: this repo)')
    parser.add_argument('--skip-build', action='store_true', help='package existing .pio artifacts only')
    parser.add_argument('--allow-pending', action='store_true',
                        help='package a board whose hardware acceptance is still pending')
    parser.add_argument('--esptool', default=None, help='path to esptool')
    args = parser.parse_args(argv)

    try:
        board = next((item for item in BOARDS if item.asset_suffix == args.board), None)
        if board is None:
            known = ', '.join(item.asset_suffix for item in BOARDS)
            raise PackagingError(f'unknown board {args.board!r}; known: {known}')
        if board.status != 'ready' and not args.allow_pending:
            raise PackagingError(
                f'board {board.asset_suffix} has not passed hardware acceptance; pass --allow-pending to '
                'package it as a test artifact (it must not be published as a release)')
        version, channel = resolve_release_tag(args.tag, configured_version(REPO_ROOT / 'platformio.ini'))
        build_state = provenance(REPO_ROOT)
        now = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%S')
        build_root = (Path(args.build_dir) if args.build_dir else REPO_ROOT) / '.pio' / 'build'
        esptool = find_esptool(args.esptool)
        if not args.skip_build:
            build_board(board, channel == 'preview', build_state['short_commit'], REPO_ROOT)
        output = Path(args.out) if args.out else REPO_ROOT / 'dist' / args.tag
        produced = package(board, version, channel, build_root, output, esptool, build_state, now)
    except PackagingError as error:
        print(f'error: {error}', file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as error:
        print(f'error: command failed: {" ".join(str(part) for part in error.cmd)}', file=sys.stderr)
        return 3

    for path in produced:
        print(f'ok: {path}')
    print(f'checksums: {output}')
    print('Publishing is the owner\'s step: review the notes, then create the release by hand.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
