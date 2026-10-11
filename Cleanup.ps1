[CmdletBinding()]
param([switch]$Apply)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path -LiteralPath $PSScriptRoot).ProviderPath.TrimEnd('\')
$outputPath = Join-Path $workspace 'output'
$packagePath = Join-Path $outputPath 'Redsand Particles.zip'
$manifestPath = Join-Path $outputPath 'BUILD.json'
$buildPath = Join-Path $workspace '.build'
$historicalTestsPath = Join-Path $workspace 'build'
$previewPath = Join-Path $outputPath 'weapon-particle-styles.png'
$referencePath = Join-Path $workspace 'Redsand Particles 191545 0.1.0 2026-09-12T14-08Z RHaV1OkR1'

function Assert-LocalItem([IO.FileSystemInfo]$Item) {
    $path = [IO.Path]::GetFullPath($Item.FullName)
    if (-not $path.StartsWith($workspace + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside this workspace: $path"
    }
    if ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Linked paths are not supported: $path"
    }
}

if ((Get-Item -LiteralPath $workspace -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw 'The workspace must not be a linked directory.'
}
foreach ($required in @('Build.ps1', 'build.json', 'source/RedsandBowParticles.cpp', 'source/RedsandEmissionTuning.h', 'assets/Basic/PrecisionParticleMatch.esp', 'tests/package_layout_tests.ps1')) {
    $item = Get-Item -LiteralPath (Join-Path $workspace $required) -Force
    Assert-LocalItem $item
    if ($item.PSIsContainer) { throw "Expected a source file: $required" }
}
foreach ($path in @($outputPath, $packagePath, $manifestPath)) { Assert-LocalItem (Get-Item -LiteralPath $path -Force) }
$preservedPaths = @($packagePath, $manifestPath, $previewPath)
$preservedHashes = @{}
foreach ($path in $preservedPaths) {
    if (Test-Path -LiteralPath $path) { $preservedHashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.packageFormat -ne 'RuntimeFiles' -or $manifest.packageVersion -ne '0.5.5' -or
    $manifest.footIKDriverInfoOffset -ne '0x88' -or
    $manifest.footIKFixRuntimes -notcontains 'SE' -or $manifest.footIKFixRuntimes -notcontains 'AE' -or
    $manifest.bowParticlesOptional -ne $true -or $manifest.bowIndependentOfSwordOptions -ne $true -or
    $manifest.defaultMenuLanguage -cne 'English' -or $manifest.modWeaponStyleFallback -ne 'Default' -or
    $manifest.independentDiagnosticLog -cne 'RedsandParticles.log' -or $manifest.diagnosticLoggingDefault -ne $false -or
    $manifest.diagnosticLogMode -cne 'OverwritePerGameSession' -or $manifest.diagnosticLogMaxBytes -ne 8388608 -or
    $manifest.licenseNoticesSHA256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'The local build manifest does not describe the current runtime release.'
}
foreach ($field in @('swordTipSweptEmitter', 'swordEmitterObjectTraversal', 'staffParticlesRemoved',
    'swordSpreadControl', 'swordSizeControl', 'bilingualMenu', 'embeddedLicenseNotices')) {
    if ($manifest.PSObject.Properties.Name -notcontains $field -or $manifest.$field -isnot [bool] -or -not $manifest.$field) {
        throw "The local build manifest is missing the current fix: $field"
    }
}
$currentVersions = @{
    redsandRealtimeMenu=1; runtimeParticleSettings=1; swordTipAttachment=2;
    bowParticles=17; bowMotionDiagnostics=2; bowRapidFireRecovery=4; bowContinuousSamplingRecovery=1;
    vanillaWeaponClassStyles=1; weaponStyleProfiles=9; weaponShapeTextures=9; skseMenuFrameworkAPI=2
}
foreach ($field in $currentVersions.Keys) {
    if ($manifest.PSObject.Properties.Name -notcontains $field -or $manifest.$field -ne $currentVersions[$field]) {
        throw "The local build manifest has an outdated fix version: $field"
    }
}
foreach ($field in @('staffParticles', 'staffIndependentOfBowOptions', 'staffParticleEffects')) {
    if ($manifest.PSObject.Properties.Name -contains $field) { throw "Removed staff provenance remains: $field" }
}

$runtimeAssets = Join-Path $workspace 'assets/Basic'
$expectedEntries = @{}
foreach ($asset in Get-ChildItem -LiteralPath $runtimeAssets -Recurse -File) {
    Assert-LocalItem $asset
    $relative = $asset.FullName.Substring($runtimeAssets.Length + 1).Replace('\', '/')
    if ($relative -match '^Meshes/Effects/WeaponTrails/ElementalDesert/Density(75|50|25|5)/' -or
        $asset.Name -match '^(LICENSE|COPYING|NOTICE)([. -]|$)') { continue }
    if ($relative -eq 'PrecisionParticleMatch.esp' -or $relative -match '^Meshes/.+\.nif$' -or
        $relative -match '^Textures/.+\.dds$' -or $relative -match '^SKSE/.+\.(ini|toml)$') {
        $expectedEntries[$relative] = $true
    }
}
$expectedEntries['SKSE/Plugins/Precision.dll'] = $true
$expectedDirectories = @{}
foreach ($entryName in $expectedEntries.Keys) {
    $directory = $entryName
    while ($directory.Contains('/')) {
        $directory = $directory.Substring(0, $directory.LastIndexOf('/'))
        $expectedDirectories[$directory + '/'] = $true
    }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($packagePath)
try {
    $foundEntries = @{}
    foreach ($entry in $archive.Entries) {
        $name = $entry.FullName
        if ($name.Contains('\') -or $name.StartsWith('/') -or $name -match '(^|/)[.]{1,2}(/|$)') {
            throw "Invalid runtime ZIP path: $name"
        }
        if ($name.EndsWith('/')) {
            if (-not $expectedDirectories.ContainsKey($name)) { throw "Unexpected runtime ZIP directory: $name" }
            continue
        }
        if (-not $expectedEntries.ContainsKey($name)) { throw "Non-runtime file in retained ZIP: $name" }
        if ($foundEntries.ContainsKey($name)) { throw "Duplicate runtime ZIP entry: $name" }
        $foundEntries[$name] = $true
    }
    if ($foundEntries.Count -ne $expectedEntries.Count) { throw 'The retained ZIP does not contain the complete runtime file set.' }
    foreach ($name in $expectedEntries.Keys) {
        if (-not $archive.GetEntry($name)) { throw "Incomplete release package: $name" }
    }
    $dllStream = $archive.GetEntry('SKSE/Plugins/Precision.dll').Open()
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $dllHash = [BitConverter]::ToString($hasher.ComputeHash($dllStream)).Replace('-', '') }
    finally { $dllStream.Dispose(); $hasher.Dispose() }
    if ($dllHash -ne $manifest.dllSHA256) { throw 'The retained DLL does not match the local build manifest.' }
} finally { $archive.Dispose() }

if (Test-Path -LiteralPath $buildPath) {
    Assert-LocalItem (Get-Item -LiteralPath $buildPath -Force)
    $marker = Join-Path $buildPath '.redsand-workspace'
    Assert-LocalItem (Get-Item -LiteralPath $marker -Force)
    if ([IO.File]::ReadAllText($marker) -ne $workspace) { throw 'Build workspace marker mismatch.' }
}

$generatedPaths = @(
    $referencePath, $buildPath, $historicalTestsPath,
    (Join-Path $workspace 'tools/__pycache__'), (Join-Path $workspace 'tests/__pycache__')
)
$targets = @($generatedPaths | Where-Object { Test-Path -LiteralPath $_ })
$targets += @(Get-ChildItem -LiteralPath $outputPath -Force | Where-Object { $_.FullName -notin $preservedPaths } | Select-Object -ExpandProperty FullName)
$items = New-Object 'System.Collections.Generic.List[System.IO.FileSystemInfo]'

foreach ($target in $targets) {
    $resolved = (Resolve-Path -LiteralPath $target).ProviderPath.TrimEnd('\')
    if ($resolved -ne $target -or $resolved -in $preservedPaths) { throw "Invalid cleanup target: $target" }
    $rootItem = Get-Item -LiteralPath $resolved -Force
    Assert-LocalItem $rootItem
    $items.Add($rootItem)
    if ($rootItem.PSIsContainer) {
        foreach ($child in Get-ChildItem -LiteralPath $resolved -Force -Recurse) {
            Assert-LocalItem $child
            if (-not $child.FullName.StartsWith($resolved + '\', [StringComparison]::OrdinalIgnoreCase)) {
                throw "Item is outside its cleanup target: $($child.FullName)"
            }
            $items.Add($child)
        }
    }
}
$files = @($items | Where-Object { -not $_.PSIsContainer })
$directories = @($items | Where-Object PSIsContainer | Sort-Object { $_.FullName.Length } -Descending)
$bytes = [long](($files | Measure-Object -Property Length -Sum).Sum)
foreach ($path in $preservedPaths) {
    if (Test-Path -LiteralPath $path) { Write-Output "Keep: $path" }
}
$targets | ForEach-Object { Write-Output "Cleanup target: $_" }
Write-Output "Selected: $($files.Count) files, $($directories.Count) directories, $bytes bytes."
if (-not $Apply) {
    Write-Output 'Preview only. Run ./Cleanup.ps1 -Apply to remove these generated files.'
    return
}

foreach ($file in $files) { Remove-Item -LiteralPath $file.FullName -Force }
foreach ($directory in $directories) {
    if (@(Get-ChildItem -LiteralPath $directory.FullName -Force).Count -ne 0) {
        throw "Directory is not empty; cleanup stopped: $($directory.FullName)"
    }
    Remove-Item -LiteralPath $directory.FullName -Force
}
foreach ($path in $preservedHashes.Keys) {
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $preservedHashes[$path]) {
        throw "A retained file changed during cleanup: $path"
    }
}
foreach ($target in $targets) {
    if (Test-Path -LiteralPath $target) { throw "Cleanup target remains: $target" }
}
Write-Output 'Cleanup complete. Source, assets, tests, Git history, the release package, preview and local build manifest were preserved.'
