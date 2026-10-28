"""Install the matching SD plugin without replacing reader credentials/config."""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile

SOURCE = Path(__file__).resolve().parents[1] / 'sd-card/plugins/ybx'


def install(sd_root: Path, source: Path = SOURCE) -> dict:
    root = sd_root.expanduser().resolve(strict=True)
    if not root.is_dir():
        raise ValueError('The SD root must be an existing directory')
    candidates = [root / path for path in ('plugins/ybx', '.crosspoint/plugins/ybx', '.plugins/ybx')]
    existing = [path for path in candidates if path.exists()]
    if len(existing) > 1:
        raise ValueError('Multiple YBX installations found; resolve the target first')
    target = existing[0] if existing else candidates[0]
    if not target.resolve().is_relative_to(root):
        raise ValueError('Plugin destination escapes the selected SD root')
    manifest = json.loads((source / 'manifest.json').read_text())
    names = manifest['files']
    for name in names:
        if not isinstance(name, str) or Path(name).name != name or name in ('.', '..'):
            raise ValueError('Invalid plugin filename')
        if name == 'token.json':
            raise ValueError('A plugin package must never supply a reader token')
        if not (source / name).is_file():
            raise ValueError(f'Missing plugin file: {name}')
    target.mkdir(parents=True, exist_ok=True)
    result = {'installed': [], 'preserved': []}
    # Publish version metadata last; incomplete copies keep the previous version.
    order = [name for name in names if name not in ('plugin.json', 'manifest.json')]
    order += [name for name in ('plugin.json', 'manifest.json') if name in names]
    for name in order:
        destination = target / name
        if name == 'config.json' and destination.exists():
            result['preserved'].append(name)
            continue
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=target, prefix='.ybx-install-', delete=False) as output:
                temporary = Path(output.name)
                with (source / name).open('rb') as input_file:
                    shutil.copyfileobj(input_file, output)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, destination)
            result['installed'].append(name)
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()
    if (target / 'token.json').exists():
        result['preserved'].append('token.json')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path, help='Mounted SD card root; no automatic device selection')
    print(json.dumps(install(parser.parse_args().sd_root), indent=2))
