param([string]$WorkRoot = (Join-Path $PSScriptRoot '.build'))
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
$env:DOTNET_CLI_UI_LANGUAGE = 'en-US'
$env:VCPKG_DISABLE_METRICS = '1'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$config = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build.json') -Raw | ConvertFrom-Json
$WorkRoot = [IO.Path]::GetFullPath($WorkRoot)
$sourceRoot = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$workPath = $WorkRoot.TrimEnd('\')
if ($workPath -eq [IO.Path]::GetPathRoot($WorkRoot).TrimEnd('\') -or $workPath -eq $sourceRoot -or $sourceRoot.StartsWith($workPath + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The build workspace must not contain the source directory or a drive root.'
}
$output = Join-Path $sourceRoot 'output'
if ($output -eq $workPath -or $output.StartsWith($workPath + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The build workspace must not contain the output directory.'
}
$workspaceMarker = Join-Path $WorkRoot '.redsand-workspace'
if ((Test-Path -LiteralPath $WorkRoot) -and !(Test-Path -LiteralPath $workspaceMarker) -and (Get-ChildItem -LiteralPath $WorkRoot -Force | Select-Object -First 1)) {
    throw 'Use a new empty build workspace. Existing unmanaged directories are never cleaned.'
}
[IO.Directory]::CreateDirectory($WorkRoot) | Out-Null
if ((Get-Item -LiteralPath $WorkRoot -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'A linked build workspace is not supported.' }
if (Test-Path -LiteralPath $workspaceMarker) {
    if ([IO.File]::ReadAllText($workspaceMarker) -ne $sourceRoot) { throw 'The build workspace belongs to a different source directory.' }
} else {
    [IO.File]::WriteAllText($workspaceMarker, $sourceRoot, $utf8)
}
[IO.Directory]::CreateDirectory($output) | Out-Null

function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

function Text([string]$Path) { return [IO.File]::ReadAllText($Path).Replace("`r`n", "`n") }
function Save([string]$Path, [string]$Content) { [IO.File]::WriteAllText($Path, $Content, $utf8) }
function Replace-One([string]$Content, [string]$Before, [string]$After) {
    if ([regex]::Matches($Content, [regex]::Escape($Before)).Count -ne 1) {
        throw 'Upstream source changed. Update the source fragments before building.'
    }
    return $Content.Replace($Before, $After)
}

function Find-PowerShell7 {
    $candidates = @(
        (Get-Command pwsh.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
        (Join-Path $env:ProgramFiles 'PowerShell/7/pwsh.exe')
        (Join-Path $env:LOCALAPPDATA 'Programs/PowerShell/7/pwsh.exe')
        (Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe')
    )
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (!$candidate -or !(Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        try {
            $version = & $candidate -NoLogo -NoProfile -NonInteractive -Command '$PSVersionTable.PSVersion.ToString()' 2>$null
            if ($LASTEXITCODE -eq 0 -and ([version]($version | Select-Object -Last 1)).Major -ge 7) {
                Write-Host "Using local PowerShell $version`: $candidate"
                return $candidate
            }
        } catch { continue }
    }
}

function Invoke-VisibleTool([string]$Program, [string[]]$Arguments, [int]$TimeoutSeconds = 300) {
    $stdout = Join-Path $WorkRoot 'powershell-fetch.stdout.log'
    $stderr = Join-Path $WorkRoot 'powershell-fetch.stderr.log'
    $process = Start-Process -FilePath $Program -ArgumentList $Arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $process.Handle | Out-Null
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $positions = @{}
    try {
        do {
            $finished = $process.WaitForExit(1000)
            foreach ($log in @($stdout, $stderr)) {
                $content = [string](Get-Content -LiteralPath $log -Raw)
                $position = [int]$positions[$log]
                if ($content.Length -gt $position) { Write-Host -NoNewline $content.Substring($position) }
                $positions[$log] = $content.Length
            }
            if (!$finished) {
                Write-Progress -Activity 'Preparing PowerShell 7' -Status ("Waiting for vcpkg: {0}s / {1}s" -f [int]$timer.Elapsed.TotalSeconds, $TimeoutSeconds)
                if ($timer.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                    throw "PowerShell preparation timed out after $TimeoutSeconds seconds. Check your network/proxy or install PowerShell 7 and retry. Logs: $stdout and $stderr"
                }
            }
        } until ($finished)
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "PowerShell preparation failed with exit code $($process.ExitCode). Logs: $stdout and $stderr" }
        return [string](Get-Content -LiteralPath $stdout -Raw)
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
        Write-Progress -Activity 'Preparing PowerShell 7' -Completed
    }
}

function Sync-Repository([string]$Name, [string]$Url, [string]$Ref) {
    $repo = Join-Path $WorkRoot $Name
    $marker = Join-Path $repo '.redsand-managed'
    if (!(Test-Path -LiteralPath (Join-Path $repo '.git'))) {
        Run 'git' @('clone', '--depth', '1', $Url, $repo)
        Save $marker 'Redsand generated build workspace'
    }
    if (!(Test-Path -LiteralPath $marker)) { throw "Existing repository is not managed by this builder: $repo" }
    Run 'git' @('-C', $repo, 'fetch', '--depth', '1', $Url, $Ref)
    Run 'git' @('-C', $repo, 'checkout', '--detach', '--force', 'FETCH_HEAD')
    return $repo
}

try {
    Get-Command git -ErrorAction Stop | Out-Null
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ build tools and a Windows SDK.' }
    $vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'Visual Studio 2022 C++ build tools were not found.' }
    $env:VCPKG_VISUAL_STUDIO_PATH = $vs.Trim()
    $ninja = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
    $cmakeBin = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
    $env:PATH = "$ninja;$cmakeBin;$env:PATH"
    foreach ($candidate in @((Join-Path $env:ProgramFiles '7-Zip'), (Join-Path ([IO.Path]::GetPathRoot($vs)) '7-Zip'))) {
        if (Test-Path -LiteralPath (Join-Path $candidate '7z.exe')) { $env:PATH = "$candidate;$env:PATH" }
    }
    Get-Command cmake,ninja,7z -ErrorAction Stop | Out-Null
    $env:VCPKG_FORCE_SYSTEM_BINARIES = '1'
    $env:VCPKG_DOWNLOADS = Join-Path $WorkRoot 'vcpkg/downloads'
    $env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $WorkRoot 'vcpkg/cache'
    Write-Host 'Fetching Precision and build dependencies.'
    $precision = Sync-Repository 'Precision' $config.precisionRepository $config.precisionRef | Select-Object -Last 1
    $commonlib = Sync-Repository 'CommonLibSSE-NG' $config.commonLibRepository $config.commonLibRef | Select-Object -Last 1
    $vcpkg = Sync-Repository 'vcpkg' $config.vcpkgRepository 'HEAD' | Select-Object -Last 1
    [IO.Directory]::CreateDirectory($env:VCPKG_DEFAULT_BINARY_CACHE) | Out-Null
    Run 'git' @('-C', $precision, 'submodule', 'update', '--init', '--depth', '1')
    $manifest = Get-Content -LiteralPath (Join-Path $precision 'vcpkg.json') -Raw | ConvertFrom-Json
    Run 'git' @('-C', $vcpkg, 'fetch', '--depth', '1', $config.vcpkgRepository, $manifest.'builtin-baseline')
    if (!(Test-Path -LiteralPath (Join-Path $vcpkg 'vcpkg.exe'))) {
        Run (Join-Path $vcpkg 'bootstrap-vcpkg.bat') @('-disableMetrics')
    }
    Write-Host 'Preparing PowerShell 7 for vcpkg.'
    $powerShellExe = Find-PowerShell7
    if (!$powerShellExe) {
        Write-Host 'No local PowerShell 7 found. Downloading through vcpkg (five-minute timeout).'
        $env:VCPKG_FORCE_SYSTEM_BINARIES = $null
        try {
            $powerShellResult = Invoke-VisibleTool (Join-Path $vcpkg 'vcpkg.exe') @('fetch', 'powershell-core')
            $powerShellExe = ($powerShellResult.Trim() -split '\r?\n' | Select-Object -Last 1).Trim()
        } finally {
            $env:VCPKG_FORCE_SYSTEM_BINARIES = '1'
        }
    }
    if (!$powerShellExe -or !(Test-Path -LiteralPath $powerShellExe -PathType Leaf)) { throw 'The PowerShell 7 executable was not found.' }
    Run $powerShellExe @('-NoLogo', '-NoProfile', '-NonInteractive', '-Command', 'if ($PSVersionTable.PSVersion.Major -lt 7) { exit 1 }')
    $env:PATH = (Split-Path -Parent $powerShellExe) + ';' + $env:PATH
    $env:CommonLibSSEPath = $commonlib
    $src = Join-Path $precision 'src'
    $cppPath = Join-Path $src 'AttackTrail.cpp'
    $headerPath = Join-Path $src 'AttackTrail.h'
    $cpp = Text $cppPath
    $header = Text $headerPath
    if ($cpp.Contains('UpdateRedsandParticles')) { throw 'Unexpected pre-patched Precision checkout.' }
    $includes = "#include `"RE/N/NiParticleSystem.h`"`n#include `"RE/N/NiPSysModifier.h`"`n#include `"RE/N/NiParticlesData.h`"`n#include `"RE/N/NiPSysData.h`"`n"
    $cpp = Replace-One $cpp '#include "AttackTrail.h"' ("#include `"AttackTrail.h`"`n" + $includes)
    $anchor = "`t`t`t`t`teffectShaderMaterial->baseColorScale *= Settings::fTrailBaseColorScaleMult;"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/ParticleBrightness.inc')) + $anchor)
    $anchor = "`t`tstd::string trailMeshPath = Settings::attackTrailMeshPath;"
    $defaultMesh = "`t`tstd::string trailMeshPath = redsandLegacyTrail ? Settings::attackTrailMeshPath : `"Effects/WeaponTrails/ElementalDesert/DefaultYellow.nif`";"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/TrailActorStyle.inc')) + $defaultMesh)
    # Each actor searches only the definitions for its selected trail style.
    foreach ($list in @('All', 'Any')) {
        $anchor = "auto search$list = std::find_if(Settings::trailDefinitions$list.begin(), Settings::trailDefinitions$list.end(), [&](const TrailDefinition& a_trailDefinition) {"
        $guard = "`n`t`tconst bool particleDefinition = a_trailDefinition.trailOverride.meshOverride && a_trailDefinition.trailOverride.meshOverride->starts_with(`"Effects/WeaponTrails/ElementalDesert/`");`n`t`tif (particleDefinition == redsandLegacyTrail) {`n`t`t`treturn false;`n`t`t}"
        $cpp = Replace-One $cpp $anchor ($anchor + $guard)
    }
    # Ribbon actors retain their installed Precision settings; particles are independent.
    foreach ($setting in @(@('fTrailSegmentLifetime', 3, '0.24f'), @('fTrailFadeOutTime', 4, '4.f'), @('fTrailBaseColorScaleMult', 1, '5.4f'))) {
        $needle = 'Settings::' + $setting[0]
        if ([regex]::Matches($cpp, [regex]::Escape($needle)).Count -ne $setting[1]) { throw 'Upstream trail settings changed. Review the ribbon compatibility patch.' }
        $cpp = $cpp.Replace($needle, "(redsandLegacyTrail ? $needle : $($setting[2]))")
    }
    $anchor = "`t`ttrailParticle = RE::NiPointer<RE::BSTempEffectParticle>(RE::BSTempEffectParticle::Spawn("
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/TrailDensity.inc')) + $anchor)
    $anchor = "`t`tconstexpr RE::NiPoint3 forwardVector{ 1.f, 0.f, 0.f };"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/TrailUpdate.inc')) + $anchor)
    $anchor = 'bool AttackTrail::GetTrailDefinition('
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/ParticleDrain.cpp.inc')) + $anchor)
    $header = Replace-One $header '#include "Settings.h"' "#include `"Settings.h`"`n#include `"RedsandDrainState.h`""
    $header = Replace-One $header "private:`n" ("private:`n" + (Text (Join-Path $PSScriptRoot 'source/TrailMembers.inc')))
    Save $cppPath $cpp
    Save $headerPath $header
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'source/RedsandDrainState.h') -Destination (Join-Path $src 'RedsandDrainState.h') -Force
    $mainPath = Join-Path $src 'main.cpp'
    $main = Text $mainPath
    $main = [regex]::Replace($main, '(?m)^\s*REL::Module::reset\(\);[^\n]*\n', '')
    $anchor = 'logger::info("{} v{}"sv, Plugin::NAME, Plugin::VERSION.string());'
    $main = Replace-One $main $anchor ($anchor + "`n`tlogger::info(`"Redsand independent particle drain v1 enabled; tagged particle trails only`");")
    Save $mainPath $main
    Write-Host 'Building the modified Precision DLL.'
    $build = Join-Path $precision 'build-release'
    Run 'cmake' @('-S', $precision, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64', "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake", '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md', "-DCompiledPluginsPath=$WorkRoot/output", '-DCOPY_OUTPUT=OFF', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5', '-DBUILD_TESTS=OFF', '-DENABLE_SKYRIM_SE=ON', '-DENABLE_SKYRIM_AE=ON', '-DENABLE_SKYRIM_VR=OFF', "-DCMAKE_CXX_FLAGS=/MP$($config.jobs) /EHsc /utf-8")
    Run 'cmake' @('--build', $build, '--config', 'Release', '--parallel', "$($config.jobs)")
    $testBuild = Join-Path $WorkRoot 'drain-tests'
    Run 'cmake' @('-S', (Join-Path $PSScriptRoot 'tests'), '-B', $testBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64')
    Run 'cmake' @('--build', $testBuild, '--config', 'Release')
    Run 'ctest' @('--test-dir', $testBuild, '-C', 'Release', '--output-on-failure')
    $dll = Join-Path $build 'src/Release/Precision.dll'
    if (!(Test-Path -LiteralPath $dll)) { throw 'Precision.dll was not produced.' }
    $stage = Join-Path $WorkRoot ('package-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($stage) | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'assets') | Copy-Item -Destination $stage -Recurse -Force
    $plugins = Join-Path $stage 'SKSE/Plugins'
    [IO.Directory]::CreateDirectory($plugins) | Out-Null
    Copy-Item -LiteralPath $dll -Destination (Join-Path $plugins 'Precision.dll') -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $stage 'README.md')
    $licenses = Join-Path $stage 'licenses'
    [IO.Directory]::CreateDirectory($licenses) | Out-Null
    foreach ($item in @(@($precision, 'COPYING', 'Precision-COPYING.txt'), @($precision, 'EXCEPTIONS', 'Precision-EXCEPTIONS.txt'), @($commonlib, 'COPYING', 'CommonLibSSE-NG-COPYING.txt'), @($commonlib, 'EXCEPTIONS.md', 'CommonLibSSE-NG-EXCEPTIONS.txt'))) {
        Copy-Item -LiteralPath (Join-Path $item[0] $item[1]) -Destination (Join-Path $licenses $item[2])
    }
    Copy-Item -LiteralPath (Join-Path $precision 'extern/glm/copying.txt') -Destination (Join-Path $licenses 'glm.txt')
    $share = Join-Path $build 'vcpkg_installed/x64-windows-static-md/share'
    Get-ChildItem -LiteralPath $share -Directory | ForEach-Object {
        $copyright = Join-Path $_.FullName 'copyright'
        if (Test-Path -LiteralPath $copyright) { Copy-Item -LiteralPath $copyright -Destination (Join-Path $licenses ($_.Name + '.txt')) }
    }
    $hashAlgorithm = [Security.Cryptography.SHA256]::Create()
    $hashStream = [IO.File]::OpenRead($dll)
    try { $dllHash = [BitConverter]::ToString($hashAlgorithm.ComputeHash($hashStream)).Replace('-', '') }
    finally { $hashStream.Dispose(); $hashAlgorithm.Dispose() }
    $provenance = [ordered]@{ precisionRepository=$config.precisionRepository; precisionVersion=[Diagnostics.FileVersionInfo]::GetVersionInfo($dll).FileVersion; precisionCommit=(& git -C $precision rev-parse HEAD); commonLibRepository=$config.commonLibRepository; commonLibCommit=(& git -C $commonlib rev-parse HEAD); dllSHA256=$dllHash; particleRate=1600; runtimePolicy='Upstream Precision'; buildUtc=[DateTime]::UtcNow.ToString('o') }
    Save (Join-Path $stage 'BUILD.json') ($provenance | ConvertTo-Json)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = Join-Path $output 'Redsand_particles.zip'
    $pending = Join-Path $output ('Redsand_particles-' + [guid]::NewGuid().ToString('N') + '.zip')
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $pending)
    Move-Item -LiteralPath $pending -Destination $archive -Force
    Copy-Item -LiteralPath (Join-Path $stage 'BUILD.json') -Destination (Join-Path $output 'BUILD.json') -Force
    Write-Host "Created: $archive"
    $resolvedWork = (Resolve-Path -LiteralPath $WorkRoot).ProviderPath.TrimEnd('\')
    if (![string]::Equals($resolvedWork, $workPath, [StringComparison]::OrdinalIgnoreCase) -or [IO.File]::ReadAllText($workspaceMarker) -ne $sourceRoot) {
        throw 'Workspace verification failed. Cleanup was skipped.'
    }
    Write-Host 'Removing generated build intermediates.'
    Remove-Item -LiteralPath $resolvedWork -Recurse -Force -ErrorAction Stop
    if (Test-Path -LiteralPath $resolvedWork) { throw 'The installer was created, but workspace cleanup did not complete.' }
    Write-Host 'Build completed. Generated intermediates were removed.'
    exit 0
} catch {
    Write-Host $_.Exception.Message
    Save (Join-Path $output 'BUILD_ERROR.txt') $_.Exception.ToString()
    exit 1
}
