param([string]$WorkRoot = (Join-Path $PSScriptRoot '.build'), [switch]$KeepWork, [switch]$PrepareOnly)
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
    if ($PrepareOnly) {
        Write-Host 'Managed dependencies prepared. Native source and release package were not changed.'
        exit 0
    }
    $src = Join-Path $precision 'src'

    foreach ($retired in @('RedsandStaffCasting.h', 'RedsandStaffCasting.cpp', 'RedsandStaffCastPolicy.h', 'RedsandStaffProjectileSource.h')) {
        $retiredPath = [IO.Path]::GetFullPath((Join-Path $src $retired))
        if (!$retiredPath.StartsWith($precision.TrimEnd('\') + '\src\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Retired source escaped managed checkout.' }
        if (Test-Path -LiteralPath $retiredPath) {
            $retiredItem = Get-Item -LiteralPath $retiredPath -Force
            if ($retiredItem.PSIsContainer -or ($retiredItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unexpected retired source path.' }
            Remove-Item -LiteralPath $retiredPath -Force
        }
    }
    $cppPath = Join-Path $src 'AttackTrail.cpp'
    $headerPath = Join-Path $src 'AttackTrail.h'
    $cpp = Text $cppPath
    $header = Text $headerPath
    if ($cpp.Contains('UpdateRedsandParticles')) { throw 'Unexpected pre-patched Precision checkout.' }
    $includes = "#include `"RE/N/NiParticleSystem.h`"`n#include `"RE/N/NiPSysModifier.h`"`n#include `"RE/N/NiParticlesData.h`"`n#include `"RE/N/NiPSysData.h`"`n#include `"RE/N/NiInterpController.h`"`n#include `"RE/N/NiFloatInterpolator.h`"`n#include `"RE/N/NiFloatData.h`"`n#include `"RE/N/NiSourceTexture.h`"`n#include <atomic>`n#include <limits>`n"
    $cpp = Replace-One $cpp '#include "AttackTrail.h"' ("#include `"AttackTrail.h`"`n" + $includes)
    $anchor = "`t`t`tif (a_init) {"
    $cpp = Replace-One $cpp $anchor ("`t`t`tbool redsandMaterialCopied = false;`n" + $anchor)
    $anchor = "`t`t`t`t`teffectShader->SetMaterial(newMaterial, false);"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/ParticleMaterial.inc')).TrimEnd("`n"))
    $anchor = "`t`t`t`t`teffectShaderMaterial->baseColorScale *= Settings::fTrailBaseColorScaleMult;"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/ParticleBrightness.inc')) + $anchor)
    $anchor = "`t`t`tif (a_bExpired) {  // fade out alpha"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/ParticleLiveBrightness.inc')) + $anchor)
    $anchor = "`t`tstd::string trailMeshPath = Settings::attackTrailMeshPath;"
    $defaultMesh = "`t`tstd::string trailMeshPath = redsandLegacyTrail ? Settings::attackTrailMeshPath : `"RedsandParticles/Default.nif`";"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/TrailActorStyle.inc')) + $defaultMesh)

    foreach ($list in @('All', 'Any')) {
        $anchor = "auto search$list = std::find_if(Settings::trailDefinitions$list.begin(), Settings::trailDefinitions$list.end(), [&](const TrailDefinition& a_trailDefinition) {"
        $guard = "`n`t`tconst bool particleDefinition = a_trailDefinition.trailOverride.meshOverride && Redsand::Weapons::IsParticleMesh(*a_trailDefinition.trailOverride.meshOverride);`n`t`tif (particleDefinition == redsandLegacyTrail) {`n`t`t`treturn false;`n`t`t}"
        $cpp = Replace-One $cpp $anchor ($anchor + $guard)
    }

    foreach ($setting in @(@('fTrailSegmentLifetime', 3, '0.24f'), @('fTrailFadeOutTime', 4, '4.f'), @('fTrailBaseColorScaleMult', 1, '5.4f'))) {
        $needle = 'Settings::' + $setting[0]
        if ([regex]::Matches($cpp, [regex]::Escape($needle)).Count -ne $setting[1]) { throw 'Upstream trail settings changed. Review the ribbon compatibility patch.' }
        $cpp = $cpp.Replace($needle, "(redsandLegacyTrail ? $needle : $($setting[2]))")
    }
    $anchor = "`t`ttrailParticle = RE::NiPointer<RE::BSTempEffectParticle>(RE::BSTempEffectParticle::Spawn("
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/TrailDensity.inc')) + $anchor)
    $anchor = "`t`tconstexpr RE::NiPoint3 forwardVector{ 1.f, 0.f, 0.f };"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/TrailUpdate.inc')) + $anchor)
    $anchor = 'if (!bAppliedTrailColorSettings || bExpired) {'
    $cpp = Replace-One $cpp $anchor 'if (!bAppliedTrailColorSettings || bExpired || redsandManaged) {'
    $anchor = "`t`tcurrentTime += a_deltaTime;"
    $cpp = Replace-One $cpp $anchor ("`t`tUpdateRedsandEmission();`n" + $anchor)
    $anchor = 'bool AttackTrail::GetTrailDefinition('
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $PSScriptRoot 'source/ParticleDrain.cpp.inc')) + (Text (Join-Path $PSScriptRoot 'source/ParticleDiagnostics.cpp.inc')) + (Text (Join-Path $PSScriptRoot 'source/SwordRuntime.inc')) + $anchor)
    $header = Replace-One $header '#include "Settings.h"' "#include `"Settings.h`"`n#include `"RedsandDrainState.h`"`n#include `"RedsandSettings.h`"`n#include `"RedsandLog.h`"`n#include `"RedsandMeleeStyle.h`"`n#include `"RedsandEmissionTuning.h`"`n#include `"RE/N/NiPSysGravityModifier.h`"`n#include `"RE/N/NiFloatInterpolator.h`""
    $header = Replace-One $header "private:`n" ("private:`n" + (Text (Join-Path $PSScriptRoot 'source/TrailMembers.inc')))
    Save $cppPath $cpp
    Save $headerPath $header

    $hooksPath = Join-Path $src 'Hooks.cpp'
    $hooks = Text $hooksPath
    $hooks = Replace-One $hooks '#include "Hooks.h"' "#include `"Hooks.h`"`n#include `"RedsandBowParticles.h`"`n#include `"RedsandSettings.h`"`n#include `"RedsandLog.h`"`n#include `"RedsandHavokLayout.h`""
    $anchor = "`t`t`t`t`tif (character->footIkDriver && character->setup && character->setup->data && character->setup->data->footIkDriverInfo) {"
    $hooks = Replace-One $hooks $anchor ((Text (Join-Path $PSScriptRoot 'source/HavokFootIK.inc')).TrimEnd("`n"))
    $anchor = "`t`tPrecisionHandler::GetSingleton()->Update(*g_deltaTime);"
    $hooks = Replace-One $hooks $anchor ($anchor + "`n`t`tRedsandBowParticles::Update(*g_deltaTime);`n`t`tRedsandSettings::TickPendingSave();`n`t`tRedsandLog::Sync();")
    Save $hooksPath $hooks
    $mainPath = Join-Path $src 'main.cpp'
    $main = Text $mainPath
    $main = Replace-One $main '#include "Events.h"' "#include `"Events.h`"`n#include `"RedsandBowParticles.h`"`n#include `"RedsandSettings.h`"`n#include `"RedsandLog.h`"`n#include `"RedsandMenu.h`""
    $anchor = "`tcase SKSE::MessagingInterface::kPostLoad:`n`t`tbreak;"
    $main = Replace-One $main $anchor "`tcase SKSE::MessagingInterface::kPostLoad:`n`t`tRedsandSettings::Load();`n`t`tRedsandLog::Initialize();`n`t`tRedsandLog::info(`"Redsand Particles $($config.packageVersion): material isolation, independent drain and Havok foot IK fix; runtime={}`", REL::Module::get().version().string());`n`t`tRedsandMenu::Register();`n`t`tbreak;"
    foreach ($anchor in @("`t`tPrecisionHandler::GetSingleton()->Initialize();", "`t`tSettings::OnPostLoadGame();")) {
        $main = Replace-One $main $anchor ($anchor + "`n`t`tRedsandBowParticles::SetEnabled(true);")
    }
    $anchor = "`t`tPrecisionHandler::GetSingleton()->OnPreLoadGame();"
    $main = Replace-One $main $anchor ("`t`tRedsandSettings::FlushPendingSave();`n`t`tRedsandBowParticles::SetEnabled(false);`n" + $anchor)
    $anchor = "`tHooks::Install();"
    $main = Replace-One $main $anchor ($anchor + "`n`tRedsandBowParticles::Install();")
    $nativeFiles = @(
        'RedsandDrainState.h', 'RedsandHavokLayout.h',
        'RedsandBowParticles.h', 'RedsandBowParticles.cpp', 'RedsandBowSimulation.h',
        'RedsandBowChargeState.h', 'RedsandBowEventState.h', 'RedsandBowStreamBudget.h',
        'BowGraphSnapshot.inc', 'BowArrowProcessing.inc', 'BowRenderer.inc', 'BowMotionDiagnostics.inc', 'RedsandMotionTrace.h',
        'RedsandSettings.h', 'RedsandSettings.cpp', 'RedsandLog.h', 'RedsandLog.cpp', 'RedsandMenu.h', 'RedsandMenu.cpp',
        'RedsandWeaponProfiles.h', 'RedsandWeaponProfiles.cpp', 'RedsandMeleeStyle.h', 'RedsandEmissionTuning.h'
    )
    foreach ($file in $nativeFiles) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "source/$file") -Destination (Join-Path $src $file) -Force
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'source/vendor') -Destination $src -Recurse -Force
    $handlerPath = Join-Path $src 'PrecisionHandler.cpp'
    $handler = Text $handlerPath
    $handler = Replace-One $handler '#include "PrecisionHandler.h"' "#include `"PrecisionHandler.h`"`n#include `"RedsandBowParticles.h`""
    $anchor = "PrecisionHandler::EventResult PrecisionHandler::ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource)`n{"
    $handler = Replace-One $handler $anchor ($anchor + "`n`tRedsandBowParticles::OnAnimationEvent(a_event, a_eventSource);")
    Save $handlerPath $handler
    $main = [regex]::Replace($main, '(?m)^\s*REL::Module::reset\(\);[^\n]*\n', '')
    Save $mainPath $main

    $noticeFile = Join-Path $WorkRoot 'RedsandThirdPartyNotices.txt'
    $resourceTemplate = Join-Path $precision 'cmake/version.rc.in'
    Save $resourceTemplate ((Text $resourceTemplate) + "`nREDSAND_THIRD_PARTY_LICENSES RCDATA `"$($noticeFile.Replace('\', '/'))`"`n")
    Write-Host 'Building the modified Precision DLL.'
    $build = Join-Path $precision 'build-release'
    Run 'cmake' @('-S', $precision, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64', "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake", '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md', "-DCompiledPluginsPath=$WorkRoot/output", '-DCOPY_OUTPUT=OFF', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5', '-DBUILD_TESTS=OFF', '-DENABLE_SKYRIM_SE=ON', '-DENABLE_SKYRIM_AE=ON', '-DENABLE_SKYRIM_VR=OFF', "-DCMAKE_CXX_FLAGS=/MP$($config.jobs) /EHsc /utf-8")
    $noticeText = "Redsand Particles - Third-party copyright and license notices`n`n"
    foreach ($license in (Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'licenses') -File | Sort-Object Name)) {
        $noticeText += "===== $($license.Name) =====`n" + (Text $license.FullName) + "`n`n"
    }
    $noticeText += "===== glm =====`n" + (Text (Join-Path $precision 'extern/glm/copying.txt')) + "`n`n"
    $share = Join-Path $build 'vcpkg_installed/x64-windows-static-md/share'
    foreach ($dependency in (Get-ChildItem -LiteralPath $share -Directory | Sort-Object Name)) {
        $copyright = Join-Path $dependency.FullName 'copyright'
        if (Test-Path -LiteralPath $copyright) { $noticeText += "===== $($dependency.Name) =====`n" + (Text $copyright) + "`n`n" }
    }
    Save $noticeFile $noticeText
    Run 'cmake' @('--build', $build, '--config', 'Release', '--parallel', "$($config.jobs)")
    $testBuild = Join-Path $WorkRoot 'drain-tests'
    Run 'cmake' @('-S', (Join-Path $PSScriptRoot 'tests'), '-B', $testBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64')
    Run 'cmake' @('--build', $testBuild, '--config', 'Release')
    Run 'ctest' @('--test-dir', $testBuild, '-C', 'Release', '--output-on-failure')
    $dll = Join-Path $build 'src/Release/Precision.dll'
    if (!(Test-Path -LiteralPath $dll)) { throw 'Precision.dll was not produced.' }
    if (![Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($dll)).Contains($noticeText)) { throw 'The DLL is missing its embedded license resource.' }
    $stage = Join-Path $WorkRoot ('package-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($stage) | Out-Null

    $runtimeAssets = Join-Path $PSScriptRoot 'assets/Basic'
    foreach ($asset in Get-ChildItem -LiteralPath $runtimeAssets -Recurse -File) {
        $relative = $asset.FullName.Substring($runtimeAssets.Length + 1)
        if ($relative -notmatch '^(Meshes[\\/].+\.nif|Textures[\\/].+\.dds|SKSE[\\/].+\.(ini|toml))$') { continue }
        if ($relative -match '[\\/]Density(5|25|50|75)[\\/]' -or $asset.Name -match '^(LICENSE|COPYING|NOTICE)([. -]|$)') { continue }
        $target = [IO.Path]::GetFullPath((Join-Path $stage $relative))
        if (!$target.StartsWith($stage + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Runtime asset escaped staging directory.' }
        [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
        Copy-Item -LiteralPath $asset.FullName -Destination $target -Force
    }
    $plugins = Join-Path $stage 'SKSE/Plugins'
    [IO.Directory]::CreateDirectory($plugins) | Out-Null
    Copy-Item -LiteralPath $dll -Destination (Join-Path $plugins 'Precision.dll') -Force
    $dllHash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
    $sourceHashes = [ordered]@{}
    $nativeSourceRoot = Join-Path $sourceRoot 'source'
    foreach ($sourceFile in (Get-ChildItem -LiteralPath $nativeSourceRoot -Recurse -File | Sort-Object FullName)) {
        $sourceName = $sourceFile.FullName.Substring($nativeSourceRoot.Length + 1).Replace('\', '/')
        $sourceHashes[$sourceName] = (Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash
    }
    $provenance = [ordered]@{
        precisionRepository=$config.precisionRepository
        precisionVersion=[Diagnostics.FileVersionInfo]::GetVersionInfo($dll).FileVersion
        precisionCommit=(& git -C $precision rev-parse HEAD)
        commonLibRepository=$config.commonLibRepository
        commonLibCommit=(& git -C $commonlib rev-parse HEAD)
        dllSHA256=$dllHash
        particleRate=1600
        havokFootIKLayoutFix=2
        footIKDriverInfoOffset='0x88'
        footIKFixRuntimes=@('SE', 'AE')
        runtimePolicy='Upstream Precision'
        buildUtc=[DateTime]::UtcNow.ToString('o')
        particleMaterialIsolationFix=1
        particleDiagnostics=1
        diagnosticLoggingDefault=$false
        independentDiagnosticLog='RedsandParticles.log'
        diagnosticLogMode='OverwritePerGameSession'
        diagnosticLogMaxBytes=8388608
        scopedMeleeMeshSelection=1
        drainStopsEmissionOnce=$true
        bowParticles=17
        bowMotionDiagnostics=2
        embeddedLicenseNotices=$true
        licenseNoticesSHA256=(Get-FileHash -LiteralPath $noticeFile -Algorithm SHA256).Hash
        redsandRealtimeMenu=1
        skseMenuFrameworkAPI=2
        swordTipAttachment=2
        swordTipSweptEmitter=$true
        swordEmitterObjectTraversal=$true
        staffParticlesRemoved=$true
        swordSpreadControl=$true
        swordSizeControl=$true
        bilingualMenu=$true
        defaultMenuLanguage='English'
        bowRapidFireRecovery=4
        runtimeParticleSettings=1
        bowContinuousSamplingRecovery=1
        vanillaWeaponClassStyles=1
        modWeaponStyleFallback='Default'
        weaponStyleProfiles=9
        weaponShapeTextures=9
        sharedWeaponColorMeshes=$true
        particleMeshFiles=10
        flatParticleAssetPaths=$true
        sourceSHA256=$sourceHashes
        bowRapidFireStreamRecovery=1
        bowOrderedReleaseProcessing=1
        bowRendererInitializationRecovery=1
        bowCameraSwitchFix=2
        bowHeldGraphPolling=1
        bowGraphTraversalCrashFix=1
        bowRapidComboCompatibility=1
        bowParticlesOptional=$true
        bowParticleScope='RuntimeMenu'
        bowIndependentOfSwordOptions=$true
        packageType='full'
        packageFormat='RuntimeFiles'
        redsandESPRequired=$false
        packageVersion=$config.packageVersion
        requiresPreviousRedsand=$false
        bowParticleEffects=@('chargeConvergence', 'releaseRing', 'worldSpaceArrowTrail')
    }
    $manifestPath = Join-Path $output 'BUILD.json'
    Save $manifestPath ($provenance | ConvertTo-Json)
    & (Join-Path $PSScriptRoot 'tests/package_layout_tests.ps1') -StagePath $stage -AssetsPath (Join-Path $PSScriptRoot 'assets') -ManifestPath $manifestPath
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = Join-Path $output 'Redsand Particles.zip'
    $pending = Join-Path $WorkRoot ('package-' + [guid]::NewGuid().ToString('N') + '.zip')
    $zip = [IO.Compression.ZipFile]::Open($pending, [IO.Compression.ZipArchiveMode]::Create)
    try {

        foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse) {
            $entryName = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entryName) | Out-Null
        }
    } finally { $zip.Dispose() }
    Move-Item -LiteralPath $pending -Destination $archive -Force
    Write-Host "Created: $archive"
    if ($KeepWork) {
        Write-Host 'Build completed. Generated intermediates retained by -KeepWork.'
        exit 0
    }
    $resolvedWork = (Resolve-Path -LiteralPath $WorkRoot).ProviderPath.TrimEnd('\')
    if (![string]::Equals($resolvedWork, $workPath, [StringComparison]::OrdinalIgnoreCase) -or [IO.File]::ReadAllText($workspaceMarker) -ne $sourceRoot) {
        throw 'Workspace verification failed. Cleanup was skipped.'
    }
    Write-Host 'Removing generated build intermediates.'
    Remove-Item -LiteralPath $resolvedWork -Recurse -Force -ErrorAction Stop
    if (Test-Path -LiteralPath $resolvedWork) { throw 'The archive was created, but workspace cleanup did not complete.' }
    Write-Host 'Build completed. Generated intermediates were removed.'
    exit 0
} catch {
    Write-Host $_.Exception.Message
    Save (Join-Path $WorkRoot 'BUILD_ERROR.txt') $_.Exception.ToString()
    exit 1
}
