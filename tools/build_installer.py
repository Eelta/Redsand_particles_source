
from pathlib import Path
import argparse
import hashlib
import itertools
import json
import re
import xml.etree.ElementTree as ET
import zipfile
from runtime_payload import runtime_payload


AXES = {name: ('on', 'off') for name in ('hit', 'slash', 'block')}
INI = 'SKSE/Plugins/RedsandParticles.ini'
MANIFEST = 'SKSE/Plugins/RedsandImpactVFX.toml'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def set_ini(text, section, key, value):
    expression = r'(?ms)(^\[' + re.escape(section) + r'\]\s*\n)(.*?)(?=^\[|\Z)'
    def edit(match):
        body, count = re.subn(r'(?m)^' + re.escape(key) + r'=[^\r\n]*', key + '=' + value, match[2])
        assert count == 1, (section, key)
        return match[1] + body
    result, count = re.subn(expression, edit, text)
    assert count == 1, section
    return result


def variant_ini(original, flags):
    text = original.decode('utf-8-sig').replace('\r\n', '\n')
    for name in ('hit', 'slash', 'block'):
        text = set_ini(text, 'ImpactVFX', name.title(), str(flags[name] == 'on').lower())
    text = set_ini(text, 'ImpactVFX', 'Enabled', str(any(flags[n] == 'on' for n in ('hit', 'slash', 'block'))).lower())
    return text.encode('utf-8')


def variant_manifest(original, flags):
    text = original.decode('utf-8-sig').replace('\r\n', '\n')
    sections = re.split(r'(?m)^\[\[Route\]\]\s*\n', text)
    selected = []
    for section in sections[1:]:
        category = re.search(r'(?m)^category\s*=\s*"(Hit|Slash|Block)"', section)[1].lower()
        if flags[category] == 'on':
            selected.append('[[Route]]\n' + section)
    header = sections[0]
    if not selected:

        header = header.replace('schemaVersion = 1', 'schemaVersion = 1\nRoute = []')
    return (header + ''.join(selected)).encode('utf-8')


def part_for(name):
    if name.startswith('SKSE/Plugins/MuImpactFramework/'):
        return 'Block' if Path(name).name.startswith('blocking_') else 'Hit'
    if name in {'SKSE/Plugins/MaxsuBlockSpark.dll', 'Meshes/ValhallaCombat/SparksFlareRings.nif',
                'Textures/effects/Scholar Keriak/SKK_HitStar.dds'}:
        return 'Block'
    return None


def sub(parent, tag, text=None, **attributes):
    element = ET.SubElement(parent, tag, attributes)
    if text is not None:
        element.text = text
    return element


def config_xml():
    ET.register_namespace('xsi', 'http://www.w3.org/2001/XMLSchema-instance')
    root = ET.Element('config', {'{http://www.w3.org/2001/XMLSchema-instance}noNamespaceSchemaLocation':
                               'http://qconsulting.ca/fo3/ModConfig5.0.xsd'})
    sub(root, 'moduleName', 'Redsand Particles')
    files = sub(root, 'requiredInstallFiles')
    sub(files, 'folder', source='Core', destination='', priority='0')
    steps = sub(root, 'installSteps', order='Explicit')
    def group(parent, name, flag, choices):
        groups = parent.find('optionalFileGroups')
        if groups is None:
            groups = sub(parent, 'optionalFileGroups', order='Explicit')
        plugins = sub(sub(groups, 'group', name=name, type='SelectExactlyOne'), 'plugins', order='Explicit')
        for index, (value, caption, description) in enumerate(choices):
            plugin = sub(plugins, 'plugin', name=caption)
            sub(plugin, 'description', description)
            sub(sub(plugin, 'conditionFlags'), 'flag', value, name=flag)
            sub(sub(plugin, 'typeDescriptor'), 'type', name='Recommended' if index == 0 else 'Optional')
    combat = sub(steps, 'installStep', name='Combat effects / 战斗特效组件')
    for flag, caption, description in [
        ('hit', 'Hit sparks and impact waves / 命中火花与冲击波', 'Impact sparks and power/critical-hit waves.\n命中火花，以及重击、暴击时的冲击效果。'),
        ('slash', 'Slash light and sparks / 斩光与火花', 'Slash light and sparks following the weapon swing.\n沿挥刀方向显示斩光与飞溅火花。'),
        ('block', 'Block flashes and sparks / 格挡闪光与火花', 'Block flashes and sparks; does not add timed-block gameplay.\n格挡闪光与火花，不添加限时格挡玩法。')]:
        group(combat, caption, flag, [
            ('on', 'Enable / 启用', description),
            ('off', 'Disable / 关闭', 'Start with this effect disabled.\n初始关闭此效果。')])
    patterns = sub(sub(root, 'conditionalFileInstalls'), 'patterns')
    def pattern(flags, source, destination, folder=False):
        item = sub(patterns, 'pattern')
        dependencies = sub(item, 'dependencies', operator='And')
        for name, value in flags.items():
            sub(dependencies, 'flagDependency', flag=name, value=value)
        sub(sub(item, 'files'), 'folder' if folder else 'file', source=source, destination=destination, priority='10')
    for component in ('Hit', 'Block'):
        pattern({component.lower(): 'on'}, 'Compatibility/' + component, '', folder=True)
    for bits in itertools.product(('on', 'off'), repeat=3):
        flags = dict(zip(('hit', 'slash', 'block'), bits))
        pattern(flags, 'Options/impacts-' + '-'.join(bits) + '.toml', MANIFEST)
    for values in itertools.product(*AXES.values()):
        flags = dict(zip(AXES, values))
        pattern(flags, 'Options/' + '-'.join(values) + '.ini', INI)
    ET.indent(root)
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text('utf-8-sig'))
    with zipfile.ZipFile(args.runtime) as source:
        runtime = {name: source.read(name) for name in source.namelist()}
    assert sha(args.runtime.read_bytes()) == manifest['packageSHA256']
    runtime = runtime_payload(runtime)
    assert not any(Path(name).suffix.lower() in {'.esp', '.esm', '.esl'} for name in runtime)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in runtime.items():
            if name in {INI, MANIFEST}:
                continue
            part = part_for(name)
            z.writestr(('Compatibility/' + part if part else 'Core') + '/' + name, data)
        z.writestr('fomod/ModuleConfig.xml', config_xml())
        info = ET.Element('fomod')
        for name, value in [('Name', 'Redsand Particles'), ('Author', 'Epsilona11'),
                            ('Version', manifest['packageVersion']), ('Website', 'https://www.nexusmods.com/skyrimspecialedition/mods/191545'),
                            ('Description', 'Weapon particles, directional slash sparks and impact effects.')]:
            sub(info, name, value)
        z.writestr('fomod/info.xml', ET.tostring(info, encoding='utf-8', xml_declaration=True))
        for values in itertools.product(*AXES.values()):
            flags = dict(zip(AXES, values))
            z.writestr('Options/' + '-'.join(values) + '.ini', variant_ini(runtime[INI], flags))
        for bits in itertools.product(('on', 'off'), repeat=3):
            flags = dict(zip(('hit', 'slash', 'block'), bits))
            z.writestr('Options/impacts-' + '-'.join(bits) + '.toml', variant_manifest(runtime[MANIFEST], flags))
    manifest.update(installerArchive=args.output.name, installerSHA256=sha(args.output.read_bytes()),
                    installerVariants=8, installerFormat='FOMOD 5', installerHandlesKnownOverlap=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'installer': str(args.output), 'variants': 8, 'sha256': manifest['installerSHA256']}))


if __name__ == '__main__':
    main()
