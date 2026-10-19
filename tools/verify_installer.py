
from pathlib import Path, PurePosixPath
import argparse
import configparser
import hashlib
import itertools
import json
import subprocess
import tempfile
import tomllib
import xml.etree.ElementTree as ET
import zipfile


def project(archive, root, flags):
    result = {}
    priorities = {}
    def apply(files):
        for entry in files:
            source = entry.attrib['source'].replace('\\', '/').rstrip('/')
            destination = entry.attrib.get('destination', '').replace('\\', '/').rstrip('/')
            priority = int(entry.attrib.get('priority', '0'))
            names = [source] if entry.tag == 'file' else [n for n in archive.namelist() if n.startswith(source + '/')]
            assert names, source
            for name in names:
                suffix = '' if entry.tag == 'file' else name[len(source) + 1:]
                target = '/'.join(p for p in (destination, suffix) if p)
                path = PurePosixPath(target)
                assert target and not path.is_absolute() and '..' not in path.parts and ':' not in target
                if target in result and priority == priorities[target]:
                    assert result[target] == name, ('ambiguous equal-priority output', target)
                if priority >= priorities.get(target, -1):
                    result[target] = name
                    priorities[target] = priority
    apply(root.find('requiredInstallFiles'))
    for pattern in root.findall('./conditionalFileInstalls/patterns/pattern'):
        deps = pattern.find('dependencies')
        assert deps.attrib['operator'] == 'And'
        assert all(d.tag == 'flagDependency' for d in deps)
        if all(flags[d.attrib['flag']] == d.attrib['value'] for d in deps):
            apply(pattern.find('files'))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--decoder', type=Path)
    parser.add_argument('--extract-default', type=Path)
    parser.add_argument('--manifest', type=Path)
    args = parser.parse_args()
    ini_name = 'SKSE/Plugins/RedsandParticles.ini'
    route_name = 'SKSE/Plugins/RedsandImpactVFX.toml'
    groups = []
    decoded = set()
    default_flags = {}
    default_payload = None
    with zipfile.ZipFile(args.runtime) as r:
        runtime = {name: r.read(name) for name in r.namelist()}
    base_routes = tomllib.loads(runtime[route_name].decode('utf-8-sig'))['Route']
    with zipfile.ZipFile(args.archive) as z:
        assert len(z.namelist()) == len(set(z.namelist()))
        root = ET.fromstring(z.read('fomod/ModuleConfig.xml'))
        assert root.tag == 'config'
        for group in root.findall('./installSteps/installStep/optionalFileGroups/group'):
            assert group.attrib['type'] == 'SelectExactlyOne'
            choices = []
            recommended = []
            for plugin in group.findall('./plugins/plugin'):
                flags = plugin.findall('./conditionFlags/flag')
                assert len(flags) == 1 and plugin.find('files') is None
                choice = (flags[0].attrib['name'], flags[0].text)
                choices.append(choice)
                if plugin.find('./typeDescriptor/type').attrib['name'] == 'Recommended':
                    recommended.append(choice)
            assert len(recommended) == 1
            default_flags.update([recommended[0]])
            groups.append(choices)
        assert set(default_flags) == {'hit', 'slash', 'block'}


        for name in z.namelist():
            if name.startswith(('Core/', 'Compatibility/')):
                relative = name.split('/', 1)[1] if name.startswith('Core/') else name.split('/', 2)[2]
                assert z.read(name) == runtime[relative], relative
        count = 0
        for values in itertools.product(*groups):
            flags = dict(values)
            payload = project(z, root, flags)
            assert ini_name in payload and route_name in payload
            assert not any(PurePosixPath(n).suffix.lower() in {'.esp', '.esm', '.esl', '.wav', '.xwm', '.fuz'} for n in payload)
            assert not any(n.startswith(('Core/', 'Compatibility/', 'Options/', 'fomod/')) for n in payload)
            assert 'SKSE/Plugins/Precision.dll' in payload and 'Docs/RedsandParticles/SOURCE.zip' in payload
            cfg = configparser.ConfigParser()
            cfg.read_string(z.read(payload[ini_name]).decode('utf-8-sig'))
            original = configparser.ConfigParser()
            original.read_string(runtime[ini_name].decode('utf-8-sig'))
            for section in ('Menu', 'SwordParticles', 'BowParticles'):
                assert dict(cfg[section]) == dict(original[section]), 'Installer changed a menu-controlled parameter'
            assert cfg.getboolean('ImpactVFX', 'Enabled') == any(flags[k] == 'on' for k in ('hit', 'slash', 'block'))
            for key in ('hit', 'slash', 'block'):
                assert cfg.getboolean('ImpactVFX', key) == (flags[key] == 'on')
            for section in cfg.sections():
                if section.startswith('Color.'):
                    original = configparser.ConfigParser()
                    original.read_string(runtime[ini_name].decode('utf-8-sig'))
                    assert dict(cfg[section]) == dict(original[section]), 'Installer changed RGB defaults'
            assert ('SKSE/Plugins/MaxsuBlockSpark.dll' in payload) == (flags['block'] == 'on')
            assert ('Meshes/ValhallaCombat/SparksFlareRings.nif' in payload) == (flags['block'] == 'on')
            for name in runtime:
                if name.startswith('SKSE/Plugins/MuImpactFramework/'):
                    key = 'block' if PurePosixPath(name).name.startswith('blocking_') else 'hit'
                    assert (name in payload) == (flags[key] == 'on')
            content = z.read(payload[route_name])
            routes = tomllib.loads(content.decode('utf-8-sig'))
            assert len(routes['Native']) == 5
            expected = [r for r in base_routes if flags[r['category'].lower()] == 'on']
            assert routes['Route'] == expected
            if args.decoder and payload[route_name] not in decoded:
                with tempfile.TemporaryDirectory(prefix='redsand-installer-') as temporary:
                    p = Path(temporary) / 'routes.toml'
                    p.write_bytes(content)
                    subprocess.run([str(args.decoder), str(p)], check=True, capture_output=True)
                decoded.add(payload[route_name])
            if flags == default_flags:
                default_payload = {n: z.read(source) for n, source in payload.items()}
            count += 1
        assert count == 8 and default_payload is not None
        assert set(default_payload) == set(runtime)
        defaults = configparser.ConfigParser()
        defaults.read_string(default_payload[ini_name].decode('utf-8-sig'))
        for section, keys in {'SwordParticles': ('Brightness', 'Density', 'Spread', 'Size'),
                              'BowParticles': ('Brightness', 'Density'),
                              'ImpactVFX': ('Brightness', 'Density', 'Size', 'CutLength', 'SlashDistance', 'SlashSpread', 'SlashDensity')}.items():
            assert all(defaults.getfloat(section, key) == 1 for key in keys)
        if args.extract_default:
            destination = args.extract_default.resolve()
            assert not destination.exists(), 'Use a new extraction directory'
            destination.mkdir(parents=True)
            for name, content in default_payload.items():
                target = destination / name
                assert target.resolve().is_relative_to(destination)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(content)
    result = {'passed': True, 'variants': count, 'production_decoded_manifest_variants': len(decoded),
              'default_files': len(default_payload), 'esp_esl_required': False, 'game_or_ui_tested': False,
              'defaultPayloadSHA256': {n: hashlib.sha256(b).hexdigest() for n, b in sorted(default_payload.items())}}
    if args.manifest:
        manifest = json.loads(args.manifest.read_text('utf-8-sig'))
        manifest['installerVerification'] = result
        args.manifest.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'defaultPayloadSHA256'}))


if __name__ == '__main__':
    main()
