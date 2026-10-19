from pathlib import PurePosixPath


def is_runtime_file(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or ':' in name or '\\' in name:
        raise ValueError(f'Invalid package path: {name}')
    suffix = path.suffix.lower()
    return ((name.startswith('Meshes/') and suffix == '.nif') or
            (name.startswith('Textures/') and suffix == '.dds') or
            (name.startswith('SKSE/Plugins/') and suffix in {'.dll', '.ini', '.toml'}) or
            (name.startswith('SKSE/CoreImpactFramework/') and suffix == '.json'))


def runtime_payload(payload):
    result = {name: data for name, data in payload.items() if is_runtime_file(name)}
    if len({name.casefold() for name in result}) != len(result):
        raise ValueError('Case-insensitive runtime path collision')
    return result
