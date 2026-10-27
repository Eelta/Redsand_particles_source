
from pathlib import Path
import argparse
import hashlib
import json
import zipfile
from runtime_payload import runtime_payload


def sha(data):
    return hashlib.sha256(data).hexdigest()


def source_files(root):
    excluded_dirs = {'.git', '.build', '.vs', 'cache', 'output', '__pycache__', 'recovery', 'backups'}
    private_names = {'AGENTS.md', 'LOCAL_AI_NOTES.md', 'MAINTENANCE_EXPERIENCE.zh-CN.md', 'PUBLICATION_AUDIT.zh-CN.md', 'PLAYER_REQUIREMENTS.zh-CN.md', 'README-PUBLIC.md'}
    excluded_suffixes = {'.dll', '.pdb', '.obj', '.lib', '.exe', '.pyc', '.ilk', '.exp'}
    for p in sorted(root.rglob('*')):
        relative = p.relative_to(root)
        if relative.parts[0].casefold() == 'hellbladeplayertime':
            continue
        folded = tuple(part.casefold() for part in relative.parts)
        if folded[:2] == ('compat', 'maxsu') or folded[:3] in {
                ('compat', 'vendor', 'maxsublockspark-ng'), ('compat', 'vendor', 'dkutil-maxsu')}:
            continue
        if p.name in private_names or p.name.startswith('.redsand-') or any(part in excluded_dirs for part in relative.parts):
            continue
        if p.is_symlink():
            raise ValueError(f'Linked source input: {relative}')
        if p.is_file() and p.suffix.lower() not in excluded_suffixes:
            yield p, relative.as_posix()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.archive.resolve().parent
    source_output = root / 'cache/BuildData/Sources'
    source_output.mkdir(parents=True, exist_ok=True)
    source_zip = source_output / 'Redsand Particles - Corresponding Source.zip'
    manifest = json.loads(args.manifest.read_text('utf-8-sig'))
    with zipfile.ZipFile(args.archive) as runtime:
        assert len(runtime.namelist()) == len(set(runtime.namelist()))
        precision = runtime.read('SKSE/Plugins/Precision.dll')
    source_hashes = {p.relative_to(root / 'source').as_posix(): sha(p.read_bytes())
                     for p in sorted((root / 'source').rglob('*')) if p.is_file()}
    if 'sourceSHA256' in manifest:
        assert {k: v.lower() for k, v in manifest['sourceSHA256'].items()} == source_hashes
    public_readme = root / 'README-PUBLIC.md'
    if not public_readme.is_file():
        public_readme = root / 'README.md'
    provenance_path = root / 'SOURCE-PROVENANCE.json'
    provenance = json.loads(provenance_path.read_text('utf-8')) if provenance_path.is_file() else {
        'precision_revision': manifest.get('precisionCommit'),
        'commonlib_revision': manifest.get('commonLibCommit'),
    }
    provenance.pop('current_maxsu_dll_sha256', None)
    provenance.update(current_precision_dll_sha256=sha(precision),
                      current_native_source_sha256=source_hashes,
                      source_delivery='Separate corresponding-source archive; runtime ZIP contains only game files',
                      dp_enabled=False)
    (root / 'SOURCE-PROVENANCE.json').write_text(json.dumps(provenance, indent=2) + '\n', 'utf-8')
    with zipfile.ZipFile(source_zip, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p, rel in source_files(root):
            z.write(public_readme if rel == 'README.md' else p, rel)
    temporary = output / (args.archive.stem + '.runtime.tmp.zip')
    with zipfile.ZipFile(args.archive) as old, zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as new:
        payload = runtime_payload({name: old.read(name) for name in old.namelist()})
        for name, content in payload.items():
            new.writestr(name, content)
    temporary.replace(args.archive)
    with zipfile.ZipFile(args.archive) as z:
        payload = {name: sha(z.read(name)) for name in sorted(z.namelist())}
    manifest.update(runtimePayloadSHA256=payload, packageSHA256=sha(args.archive.read_bytes()),
                    correspondingSourceSHA256=sha(source_zip.read_bytes()),
                    correspondingSourceFiles=sum(1 for _ in source_files(root)),
                    publicationPackage=True, donationPointsEnabled=False, runtimeOnly=True,
                    correspondingSourceArchive=str(source_zip.relative_to(root)),
                    requiresChaosTrailReplacer=False, includesPrecisionRibbonOverride=False,
                    precisionRibbonColorIndependent=True, particlesUseOwnEnchantmentPalette=True,
                    particleEnchantmentCompatibilityColorFallback=True, particleCopiesRibbonSettings=False)
    args.manifest.write_text(json.dumps(manifest, indent=2) + '\n', 'utf-8')
    print(json.dumps({'runtime': str(args.archive), 'source': str(source_zip),
                      'runtime_files': len(payload), 'source_files': manifest['correspondingSourceFiles'],
                      'runtime_sha256': manifest['packageSHA256'], 'source_sha256': manifest['correspondingSourceSHA256']}))


if __name__ == '__main__':
    main()
