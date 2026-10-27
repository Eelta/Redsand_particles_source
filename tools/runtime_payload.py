from pathlib import PurePosixPath


def is_runtime_file(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or ':' in name or '\\' in name:
        raise ValueError(f'Invalid package path: {name}')
    suffix = path.suffix.lower()
    return (any(name.startswith(f'Meshes/{folder}/') for folder in ('RedsandParticles', 'RedsandBlockLightning', 'StellarImpact', 'RedsandSwingTrail')) and suffix == '.nif' or
            any(name.startswith(f'Textures/{folder}/') for folder in ('RedsandParticles', 'RedsandBlockLightning', 'StellarImpact', 'RedsandSwingTrail')) and suffix == '.dds' or
            name in {'SKSE/Plugins/Precision.dll', 'SKSE/Plugins/RedsandParticles.ini',
                     'SKSE/Plugins/RedsandImpactVFX.toml', 'SKSE/Plugins/Precision/Precision_ElementalDesert.toml'})


def runtime_payload(payload):
    result = {name: data for name, data in payload.items() if is_runtime_file(name)}
    if len({name.casefold() for name in result}) != len(result):
        raise ValueError('Case-insensitive runtime path collision')
    return result
