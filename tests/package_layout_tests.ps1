param(
    [Parameter(Mandatory = $true)][string]$StagePath,
    [Parameter(Mandatory = $true)][string]$AssetsPath,
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [string]$PythonPath = 'python'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
$stage = (Resolve-Path -LiteralPath $StagePath).Path.TrimEnd('\', '/')
$assets = (Resolve-Path -LiteralPath $AssetsPath).Path.TrimEnd('\', '/')
$runtimeAssets = (Resolve-Path -LiteralPath (Join-Path $assets 'Basic')).Path.TrimEnd('\', '/')
$manifestFile = (Resolve-Path -LiteralPath $ManifestPath).Path
Require (Test-Path -LiteralPath $manifestFile -PathType Leaf) 'ManifestPath must identify the local build manifest'
Require (-not $manifestFile.StartsWith($stage + '\', [StringComparison]::OrdinalIgnoreCase)) 'Build manifest must remain outside the runtime package'
function StageFile([string]$Relative) {
    Require (-not [IO.Path]::IsPathRooted($Relative)) "Absolute runtime path: $Relative"
    $path = [IO.Path]::GetFullPath((Join-Path $stage $Relative))
    Require ($path.StartsWith($stage + '\', [StringComparison]::OrdinalIgnoreCase)) "Source escapes stage: $Relative"
    Require (Test-Path -LiteralPath $path) "Missing package source: $Relative"
    return $path
}
function IsLegacyAsset([string]$Relative) {
    return $Relative -match '^PrecisionParticleMatch_(75|50|25|5|npc_only|player_only)\.esp$' -or
        $Relative -match '^Options[\\/]' -or
        $Relative -match '^Meshes[\\/]Effects[\\/]WeaponTrails[\\/]ElementalDesert[\\/]Density(75|50|25|5)[\\/]'
}
function IsRuntimeAsset([string]$Relative) {
    if (IsLegacyAsset $Relative) { return $false }
    if ([IO.Path]::GetFileName($Relative) -match '^(LICENSE|COPYING|NOTICE)([. -]|$)') { return $false }
    return $Relative -match '^Meshes[\\/].+\.nif$' -or
        $Relative -match '^Textures[\\/].+\.dds$' -or
        $Relative -match '^SKSE[\\/].+\.(ini|toml)$'
}
function ReadSettings([string]$Path) {
    $sections = @{}
    $section = ''
    foreach ($line in Get-Content -LiteralPath $Path) {
        $text = $line.Trim()
        if (-not $text -or $text.StartsWith(';') -or $text.StartsWith('#')) { continue }
        if ($text -match '^\[([^\]]+)\]$') {
            $section = $Matches[1]
            Require (-not $sections.ContainsKey($section)) "Duplicate INI section: $section"
            $sections[$section] = @{}
            continue
        }
        Require ($section -and $text -match '^([^=]+)=(.*)$') "Invalid INI setting: $text"
        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        Require (-not $sections[$section].ContainsKey($key)) "Duplicate INI setting: $section/$key"
        $sections[$section][$key] = $value
    }
    return $sections
}
$expectedFiles = @{}
foreach ($asset in Get-ChildItem -LiteralPath $runtimeAssets -Recurse -File) {
    $relative = $asset.FullName.Substring($runtimeAssets.Length + 1)
    if (IsRuntimeAsset $relative) { $expectedFiles[$relative] = $asset.FullName }
}
$expectedFiles['SKSE\Plugins\Precision.dll'] = $null
$expectedDirectories = @{}
foreach ($relative in $expectedFiles.Keys) {
    $directory = Split-Path -Parent $relative
    while ($directory) {
        $expectedDirectories[$directory] = $true
        $directory = Split-Path -Parent $directory
    }
}
$installed = @{}
foreach ($item in Get-ChildItem -LiteralPath $stage -Recurse -Force) {
    Require (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) "Package contains a reparse point: $($item.FullName)"
    $relative = $item.FullName.Substring($stage.Length + 1)
    if ($item.PSIsContainer) {
        Require ($expectedDirectories.ContainsKey($relative)) "Unexpected runtime package directory: $relative"
    } else {
        Require ($expectedFiles.ContainsKey($relative)) "Non-runtime or unexpected package file: $relative"
        Require (-not $installed.ContainsKey($relative)) "Duplicate runtime package file: $relative"
        $installed[$relative] = $item.FullName
    }
}
Require ($installed.Count -eq $expectedFiles.Count) 'Runtime package file count does not match the runtime asset set and DLL'
foreach ($relative in $expectedFiles.Keys) { $null = StageFile $relative }
$pluginsInPackage = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object { $_.Extension -in @('.esp', '.esm', '.esl') })
Require ($pluginsInPackage.Count -eq 0) 'Redsand must not package ESP, ESM or ESL plugins'
Require (@(Get-ChildItem -LiteralPath $stage -Recurse -Filter '*.pex' -File).Count -eq 0) 'The runtime menu must not require Papyrus scripts'
foreach ($required in @('SKSE\Plugins\Precision.dll', 'SKSE\Plugins\RedsandParticles.ini', 'SKSE\Plugins\Precision\Precision_ElementalDesert.toml', 'Meshes\RedsandParticles\Default.nif', 'Textures\RedsandParticles\Default.dds')) {
    Require (Test-Path -LiteralPath (Join-Path $stage $required) -PathType Leaf) "Missing required runtime file: $required"
}
$weaponProfiles = @('Sword', 'Dagger', 'WarAxe', 'Mace', 'Greatsword', 'Battleaxe', 'Warhammer', 'Bow', 'Crossbow')
$styleMeshRoot = Join-Path $stage 'Meshes\RedsandParticles'
$styleTextureRoot = Join-Path $stage 'Textures\RedsandParticles'
Require (Test-Path -LiteralPath $styleMeshRoot -PathType Container) 'Missing weapon style meshes'
Require (Test-Path -LiteralPath $styleTextureRoot -PathType Container) 'Missing weapon shape textures'
Require (@(Get-ChildItem -LiteralPath $styleMeshRoot -Directory).Count -eq 0) 'Weapon meshes must use one flat folder'
Require (@(Get-ChildItem -LiteralPath $styleTextureRoot -Directory).Count -eq 0) 'Particle textures must use one flat folder'
Require (@(Get-ChildItem -LiteralPath $stage -Recurse -File -Filter '*.nif').Count -eq 10) 'Expected ten shared-color particle NIFs'
Require (@(Get-ChildItem -LiteralPath $stage -Recurse -File -Filter '*.dds').Count -eq 10) 'Expected default texture and nine weapon shape DDS textures'
Require (-not (Test-Path -LiteralPath (Join-Path $styleMeshRoot 'Staff'))) 'Removed staff style must not be packaged'
Require (-not (Test-Path -LiteralPath (Join-Path $styleTextureRoot 'Staff.dds'))) 'Removed staff texture must not be packaged'
foreach ($profile in @('Default') + $weaponProfiles) {
    Require (Test-Path -LiteralPath (Join-Path $styleMeshRoot ($profile + '.nif')) -PathType Leaf) "Missing particle mesh: $profile"
    $texture = Join-Path $styleTextureRoot ($profile + '.dds')
    Require (Test-Path -LiteralPath $texture -PathType Leaf) "Missing weapon shape texture: $profile"
}
Require ($installed.ContainsKey('SKSE\Plugins\Precision.dll')) 'Required game files missing from installation'
$settings = ReadSettings (StageFile 'SKSE\Plugins\RedsandParticles.ini')
$defaults = @{
    Menu = @{ Language='English'; DiagnosticLogging='false' }
    SwordParticles = @{ Enabled='true'; Player='true'; NPC='true'; Tip='true'; Brightness='1.0'; Density='1.0'; Spread='1.0'; Size='1.0' }
    BowParticles = @{ Enabled='true'; Player='true'; NPC='true'; Brightness='1.0'; Density='1.0'; Gathering='true'; Rings='true'; Trails='true' }
}
Require (-not $settings.ContainsKey('StaffParticles')) 'Removed staff settings must not be packaged'
Require ($settings.Count -eq $defaults.Count) 'Unexpected or missing INI sections'
foreach ($section in $defaults.Keys) {
    Require ($settings.ContainsKey($section) -and $settings[$section].Count -eq $defaults[$section].Count) "Incomplete INI section: $section"
    foreach ($key in $defaults[$section].Keys) {
        Require ($settings[$section].ContainsKey($key) -and $settings[$section][$key] -eq $defaults[$section][$key]) "Incorrect default INI setting: $section/$key"
    }
}
foreach ($relative in $expectedFiles.Keys) {
    $asset = $expectedFiles[$relative]
    if (-not $asset) { continue }
    $copy = StageFile $relative
    Require ((Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash) "Asset changed during packaging: $relative"
}
$sourceRoot = Join-Path (Split-Path -Parent $assets) 'source'
$nifCheck = @'
import math
from pathlib import Path
import struct
import sys
if sys.version_info < (3, 10):
    raise SystemExit('Package NIF checks require Python 3.10 or newer')
sys.path.insert(0, str(Path(sys.argv[1]) / 'tools'))
import generate_weapon_assets as assets
root = Path(sys.argv[2]) / 'Meshes/RedsandParticles'
paths = sorted(root.rglob('*.nif'))
assert len(paths) == 10, f'Expected 10 runtime NIFs, found {len(paths)}'
expected_chain = ('NiPSysEmitterCtlr', 'NiPSysEmitterSpeedCtlr',
                  'NiPSysEmitterInitialRadiusCtlr', 'NiPSysUpdateCtlr')
for path in paths:
    data = path.read_bytes()
    doc = assets.NifDocument.parse(data)
    assert doc.serialize() == data, f'NIF round-trip failed: {path}'
    assert len(doc.blocks) == 108, f'Unexpected NIF block count: {path}'
    systems = assets.particle_systems(doc)
    assert len(systems) == 1, f'Expected one named particle system: {path}'
    system = systems[0]
    texture, _, _ = assets.shader_texture(doc, system.shader)
    expected_texture = f'textures\\RedsandParticles\\{path.stem}.dds'
    assert texture == expected_texture, f'Wrong particle texture: {path}'
    assert (Path(sys.argv[2]) / texture.replace('\\', '/')).is_file(), f'Missing texture: {texture}'
    chain = assets.particle_controllers(doc, system)
    assert tuple(doc.types[item.index] for item in chain) == expected_chain, f'Wrong controller order: {path}'
    birth = chain[0]
    emitters = [index for index in system.modifiers if doc.types[index] == 'NiPSysMeshEmitter']
    assert len(emitters) == 1, f'Expected one mesh emitter: {path}'
    fields = assets.emitter_fields(doc, emitters[0], system.index)
    modifier_name = struct.unpack_from('<I', doc.blocks[emitters[0]])[0]
    assert birth.modifier_name == modifier_name, f'Wrong birth modifier: {path}'
    assert len({item.interpolator for item in chain[:-1]}) == 3, f'Shared interpolator: {path}'
    for item, key in zip(chain[1:3], ('speed', 'radius')):
        assert item.modifier_name == modifier_name, f'Wrong motion modifier: {path}'
        assert (item.flags, item.timing) == (birth.flags, birth.timing), f'Wrong controller timing: {path}'
        value, reference = struct.unpack('<fi', doc.require_block(item.interpolator, 'NiFloatInterpolator'))
        assert math.isfinite(value) and value == fields[key][1] and reference == -1, f'Wrong constant {key}: {path}'
    assert fields['radius'][1] * .25 > fields['radius_variation'][1], f'Unsafe minimum radius: {path}'
print('PASS: 10 packaged NIFs retain particle controllers and resolve their flat texture paths')
'@
& $PythonPath -I -B -c $nifCheck (Split-Path -Parent $assets) $stage
Require ($LASTEXITCODE -eq 0) 'Packaged NIF/controller validation failed'
$manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
Require ($manifest.packageFormat -eq 'RuntimeFiles') 'Manifest packageFormat must be RuntimeFiles'
Require ($manifest.PSObject.Properties.Name -contains 'packageVersion' -and $manifest.packageVersion -eq '0.5.7') 'Manifest packageVersion must be 0.5.7'
Require ($manifest.PSObject.Properties.Name -contains 'redsandESPRequired' -and $manifest.redsandESPRequired -is [bool] -and !$manifest.redsandESPRequired) 'Redsand must not require its legacy ESP'
Require ($manifest.PSObject.Properties.Name -contains 'independentDiagnosticLog' -and $manifest.independentDiagnosticLog -ceq 'RedsandParticles.log') 'Diagnostics must use the independent RedsandParticles.log file'
Require ($manifest.PSObject.Properties.Name -contains 'diagnosticLoggingDefault' -and $manifest.diagnosticLoggingDefault -is [bool] -and !$manifest.diagnosticLoggingDefault) 'Diagnostic logging must default to off'
Require ($manifest.diagnosticLogMode -ceq 'OverwritePerGameSession' -and $manifest.diagnosticLogMaxBytes -eq 8388608) 'Diagnostics must overwrite previous game sessions and cap one file at 8 MiB'
Require ($manifest.footIKDriverInfoOffset -eq '0x88') 'Incorrect Foot IK driver layout offset'
Require ($manifest.footIKFixRuntimes -contains 'SE' -and $manifest.footIKFixRuntimes -contains 'AE') 'Foot IK fix must cover SE and AE'
Require ($manifest.dllSHA256 -eq (Get-FileHash -LiteralPath (StageFile 'SKSE\Plugins\Precision.dll') -Algorithm SHA256).Hash) 'Packaged DLL checksum does not match the local build manifest'
foreach ($field in @('redsandRealtimeMenu', 'runtimeParticleSettings', 'bowContinuousSamplingRecovery')) {
    Require ($manifest.PSObject.Properties.Name -contains $field -and $manifest.$field -eq 1) "Missing runtime-settings provenance: $field"
}
Require ($manifest.PSObject.Properties.Name -contains 'swordTipAttachment' -and $manifest.swordTipAttachment -eq 2) 'Sword-tip provenance must declare the swept-emitter implementation'
foreach ($field in @('swordTipSweptEmitter', 'swordEmitterObjectTraversal', 'staffParticlesRemoved', 'swordSpreadControl', 'swordSizeControl', 'bilingualMenu', 'sharedWeaponColorMeshes', 'flatParticleAssetPaths')) {
    Require ($manifest.PSObject.Properties.Name -contains $field -and $manifest.$field -is [bool] -and $manifest.$field) "Missing 0.5.7 runtime provenance: $field"
}
Require ($manifest.particleMeshFiles -eq 10) 'Manifest must declare ten particle meshes shared by all colors'
foreach ($field in @('staffParticles', 'staffIndependentOfBowOptions', 'staffParticleEffects')) {
    Require (-not ($manifest.PSObject.Properties.Name -contains $field)) "Removed staff provenance remains: $field"
}
Require ($manifest.PSObject.Properties.Name -contains 'defaultMenuLanguage' -and $manifest.defaultMenuLanguage -ceq 'English') 'Packaged menu must default to English'
Require ($manifest.PSObject.Properties.Name -contains 'bowParticles' -and $manifest.bowParticles -eq 17) 'Bow provenance must declare completed custom draws and bounded native path sampling'
Require ($manifest.bowMotionDiagnostics -eq 2) 'Read-only bow motion diagnostics must be declared'
Require ($manifest.PSObject.Properties.Name -contains 'bowRapidFireRecovery' -and $manifest.bowRapidFireRecovery -eq 4) 'Bow recovery provenance must declare the current rapid-fire recovery implementation'
Require ($manifest.PSObject.Properties.Name -contains 'vanillaWeaponClassStyles' -and $manifest.vanillaWeaponClassStyles -eq 1) 'Missing vanilla weapon category style provenance'
Require ($manifest.PSObject.Properties.Name -contains 'modWeaponStyleFallback' -and $manifest.modWeaponStyleFallback -eq 'Default') 'Mod-added weapons must use the default style'
Require ($manifest.PSObject.Properties.Name -contains 'weaponStyleProfiles' -and $manifest.weaponStyleProfiles -eq 9) 'Manifest must declare nine weapon style profiles'
Require ($manifest.PSObject.Properties.Name -contains 'weaponShapeTextures' -and $manifest.weaponShapeTextures -eq 9) 'Manifest must declare nine weapon shape textures'
Require ($manifest.skseMenuFrameworkAPI -eq 2) 'Menu must use the v2-compatible SKSE Menu Framework API'
Require ($manifest.PSObject.Properties.Name -contains 'sourceSHA256') 'Missing source checksum provenance'
foreach ($name in @('RedsandMenu.cpp', 'RedsandSettings.cpp', 'RedsandLog.cpp', 'RedsandLog.h', 'RedsandWeaponProfiles.cpp', 'RedsandWeaponProfiles.h')) {
    Require ($manifest.sourceSHA256.PSObject.Properties.Name -contains $name) "Missing source checksum: $name"
}
foreach ($property in $manifest.sourceSHA256.PSObject.Properties) {
    Require ($property.Name -notlike 'RedsandStaff*') "Removed staff source remains in provenance: $($property.Name)"
    Require (-not [IO.Path]::IsPathRooted($property.Name)) "Invalid source provenance filename: $($property.Name)"
    $sourceFile = [IO.Path]::GetFullPath((Join-Path $sourceRoot $property.Name))
    Require ($sourceFile.StartsWith($sourceRoot + '\', [StringComparison]::OrdinalIgnoreCase)) "Source provenance path escapes source directory: $($property.Name)"
    Require (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Source provenance file missing: $($property.Name)"
    Require ($property.Value -match '^[0-9a-fA-F]{64}$' -and $property.Value -eq (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash) "Source checksum mismatch: $($property.Name)"
}
Require (@(Get-ChildItem -LiteralPath $sourceRoot -Recurse -File).Count -eq @($manifest.sourceSHA256.PSObject.Properties).Count) 'Local build manifest must cover every source file'
Write-Output 'PASS: 0.5.7 flat runtime files only, ten shared-color NIFs, ten DDS textures, no ESP/ESM/ESL, bilingual/melee/bow defaults and local asset/DLL/source provenance'
