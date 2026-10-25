"""Export exactly the flash images identified by ESP-IDF, plus audit files."""
import hashlib
import json
import shutil
from pathlib import Path

build = Path('/build')
output = Path('/project/output')
output.mkdir(exist_ok=True)
(output / 'sha256.json').unlink(missing_ok=True)
args = json.loads((build / 'flasher_args.json').read_text())
files = set(args['flash_files'].values()) | {
    'flasher_args.json', 'flash_args', 'inventory_counter_label.elf', 'inventory_counter_label.map',
}
manifest = {}
for name in sorted(files):
    source = (build / name).resolve()
    try:
        relative = source.relative_to(build.resolve())
    except ValueError:
        raise ValueError(f'Build artifact outside build directory: {name}')
    destination = output / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    manifest[relative.as_posix()] = hashlib.sha256(destination.read_bytes()).hexdigest()
for name in ('sdkconfig', 'sdkconfig.defaults', 'partitions.csv'):
    shutil.copyfile(Path('/project') / name, output / name)
    manifest[name] = hashlib.sha256((output / name).read_bytes()).hexdigest()
temporary_manifest = output / 'sha256.json.tmp'
temporary_manifest.write_text(json.dumps(manifest, indent=2) + '\n')
temporary_manifest.replace(output / 'sha256.json')
print(f'Exported {len(manifest)} verified build artifacts to {output}')
