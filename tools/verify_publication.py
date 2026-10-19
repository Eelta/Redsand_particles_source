
from pathlib import Path
import argparse
import hashlib
import json
import tomllib
import zipfile
from nif_schema import CandidateNif
from bsa_index import names
from runtime_payload import is_runtime_file


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', required=True, type=Path)
    ap.add_argument('--game-data', required=True, type=Path)
    ap.add_argument('--precision-data', required=True, type=Path)
    ap.add_argument('--archive', type=Path)
    args = ap.parse_args()
    data = args.data.resolve()
    files = {p.relative_to(data).as_posix(): p for p in data.rglob('*') if p.is_file()}
    assert not any(Path(p).suffix.lower() in {'.esp', '.esm', '.esl', '.wav', '.xwm', '.fuz', '.pex'} for p in files)
    assert not any('/chaos/' in p.lower() or 'PrecisionTintSync' in p or p.endswith('AttackTrailMagic.nif') for p in files)
    assert all(is_runtime_file(name) for name in files)
    assert {name for name in files if name.endswith('.dll')} == {'SKSE/Plugins/Precision.dll'}
    vanilla = set()
    for p in args.game_data.glob('Skyrim - Textures*.bsa'):
        vanilla.update(names(p))
    assert vanilla, 'No vanilla texture BSA index found'
    local = {p.lower() for p in files}
    external = {}
    nifs = 0
    for rel, p in files.items():
        if p.suffix.lower() != '.nif':
            continue
        doc = CandidateNif.read(p)
        doc.validate()
        nifs += 1
        for t in doc.texture_paths():
            t = t.replace('\\', '/').lower()
            if not t.startswith('textures/'):
                t = 'textures/' + t
            if t in local:
                continue
            if t in vanilla:
                external[t] = 'Vanilla Skyrim texture BSA'
            else:
                assert (args.precision_data / t).is_file(), f'Missing minimal-install texture: {t} ({rel})'
                external[t] = 'Required original Precision installation'
    document = tomllib.loads((data / 'SKSE/Plugins/RedsandImpactVFX.toml').read_text('utf-8-sig'))
    assert len(document['Native']) == 5 and len(document['Route']) == 26
    assert all(route['plugin'] == 'Skyrim.esm' for route in document['Route'])
    for route in document['Route']:
        for model in route['models'] + [route['baseModel']] + ([route['offModel']] if 'offModel' in route else []):
            assert 'Meshes/' + model.replace('\\', '/') in files, model
    for route in document['Native']:
        assert 'Meshes/' + route['model'].replace('\\', '/') in files
    result = {'passed': True, 'files': len(files), 'nifs': nifs,
              'dds': sum(p.suffix.lower() == '.dds' for p in files.values()),
              'native_layers': 5, 'vanilla_routes': len(document['Route']),
              'external_textures': external, 'chaos_required': False,
              'precision_ribbon_resources_overridden': False, 'game_tested': False}
    if args.archive:
        with zipfile.ZipFile(args.archive) as z:
            assert len(z.namelist()) == len(set(z.namelist()))
            assert set(z.namelist()) == set(files)
            assert all(z.read(rel) == p.read_bytes() for rel, p in files.items())
            assert all(is_runtime_file(name) for name in z.namelist())
        result['archive_sha256'] = hashlib.sha256(args.archive.read_bytes()).hexdigest()
    print(json.dumps(result))


if __name__ == '__main__':
    main()
