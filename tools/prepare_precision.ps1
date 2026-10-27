param([string]$SourceRoot, [string]$UpstreamRoot)
$ErrorActionPreference="Stop"
$utf8=New-Object System.Text.UTF8Encoding($false)
$precision=$UpstreamRoot
$config=Get-Content -LiteralPath (Join-Path $SourceRoot "build.json") -Raw | ConvertFrom-Json
function Text([string]$Path) { return [IO.File]::ReadAllText($Path).Replace("`r`n", "`n") }
function Save([string]$Path, [string]$Content) { [IO.File]::WriteAllText($Path, $Content, $utf8) }
function Replace-One([string]$Content, [string]$Before, [string]$After) {
    if ([regex]::Matches($Content, [regex]::Escape($Before)).Count -ne 1) {
        throw 'Upstream source changed. Update the source fragments before building.'
    }
    return $Content.Replace($Before, $After)
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
    $header = Replace-One $header 'std::optional<TrailOverride> a_trailOverride = std::nullopt);' 'std::optional<TrailOverride> a_trailOverride = std::nullopt, bool a_redsandNativeOnly = false);'
    $header = Replace-One $header "`tbool Update(float a_deltaTime);" "`t~AttackTrail();`n`tbool ShouldAddPrecisionTrail() const;`n`tbool Update(float a_deltaTime);"
    $cpp = Replace-One $cpp 'std::optional<TrailOverride> a_trailOverride /*= std::nullopt*/) :' 'std::optional<TrailOverride> a_trailOverride /*= std::nullopt*/, bool a_redsandNativeOnly /*= false*/) :'
    $cpp = Replace-One $cpp "`tactorHandle(a_actorHandle)" "`tactorHandle(a_actorHandle), redsandNativeOnly(a_redsandNativeOnly)"
    $anchor = "bool AttackTrail::Update(float a_deltaTime)`n{`n"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $SourceRoot 'source/NativeTrailLifecycle.inc')) + $anchor + (Text (Join-Path $SourceRoot 'source/NativeTrailUpdate.inc')))
    $includes = "#include `"RE/N/NiParticleSystem.h`"`n#include `"RE/N/NiPSysModifier.h`"`n#include `"RE/N/NiParticlesData.h`"`n#include `"RE/N/NiPSysData.h`"`n#include `"RE/N/NiInterpController.h`"`n#include `"RE/N/NiFloatInterpolator.h`"`n#include `"RE/N/NiFloatData.h`"`n#include `"RE/N/NiSourceTexture.h`"`n#include <atomic>`n#include <limits>`n"
    $cpp = Replace-One $cpp '#include "AttackTrail.h"' ("#include `"AttackTrail.h`"`n" + $includes)
    $anchor = "`t`t`tif (a_init) {"
    $cpp = Replace-One $cpp $anchor ("`t`t`tbool redsandMaterialCopied = false;`n" + $anchor)
    $anchor = "`t`t`t`t`teffectShader->SetMaterial(newMaterial, false);"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $SourceRoot 'source/ParticleMaterial.inc')).TrimEnd("`n"))
    $anchor = "`t`t`t`t`teffectShaderMaterial->baseColorScale *= Settings::fTrailBaseColorScaleMult;"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $SourceRoot 'source/ParticleBrightness.inc')) + $anchor)
    $anchor = "`t`t`tif (a_bExpired) {  // fade out alpha"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $SourceRoot 'source/ParticleLiveBrightness.inc')) + $anchor)
    $anchor = "`t`tstd::string trailMeshPath = Settings::attackTrailMeshPath;"
    $defaultMesh = "`t`tstd::string trailMeshPath = redsandLegacyTrail ? Settings::attackTrailMeshPath : `"RedsandParticles/Default.nif`";"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $SourceRoot 'source/TrailActorStyle.inc')) + (Text (Join-Path $SourceRoot 'source/NativeTrailOverride.inc')) + $defaultMesh)

    foreach ($list in @('All', 'Any')) {
        $anchor = "auto search$list = std::find_if(Settings::trailDefinitions$list.begin(), Settings::trailDefinitions$list.end(), [&](const TrailDefinition& a_trailDefinition) {"
        $guard = "`n`t`tconst bool particleDefinition = a_trailDefinition.trailOverride.meshOverride && Redsand::Weapons::IsParticleMesh(*a_trailDefinition.trailOverride.meshOverride);`n`t`tif (!Redsand::Weapons::TrailDefinitionAllowed(redsandLegacyTrail || a_ribbonColor, particleDefinition)) {`n`t`t`treturn false;`n`t`t}"
        $cpp = Replace-One $cpp $anchor ($anchor + $guard)
    }




    $magicGuard = 'if (actorValueForCost != RE::ActorValue::kNone) {'
    $chargeGuard = 'if (actor->AsActorValueOwner()->GetActorValue(actorValueForCost) >= cost) {'
    if ([regex]::Matches($cpp, [regex]::Escape($magicGuard)).Count -ne 2 -or [regex]::Matches($cpp, [regex]::Escape($chargeGuard)).Count -ne 2) { throw 'Upstream enchantment matching changed.' }
    $cpp = $cpp.Replace($magicGuard, 'if (Redsand::Weapons::CheckMagicConditions(redsandLegacyTrail, actorValueForCost != RE::ActorValue::kNone, redsandNativeOnly)) {')
    $cpp = $cpp.Replace($chargeGuard, 'if (actorValueForCost == RE::ActorValue::kNone || actor->AsActorValueOwner()->GetActorValue(actorValueForCost) >= cost) {')


    $anchor = "`t`t`t`tif (trailDefinition.trailOverride.meshOverride) {"
    $cpp = Replace-One $cpp $anchor ("`t`t`t`tif (!redsandLegacyTrail && trailDefinition.trailOverride.meshOverride && !Redsand::Weapons::IsParticleMesh(*trailDefinition.trailOverride.meshOverride)) { trailDefinition.trailOverride.meshOverride.reset(); }`n" + $anchor)

    foreach ($setting in @(@('fTrailSegmentLifetime', 3, '0.24f'), @('fTrailFadeOutTime', 4, '4.f'), @('fTrailBaseColorScaleMult', 1, '5.4f'))) {
        $needle = 'Settings::' + $setting[0]
        if ([regex]::Matches($cpp, [regex]::Escape($needle)).Count -ne $setting[1]) { throw 'Upstream trail settings changed. Review the ribbon compatibility patch.' }
        $cpp = $cpp.Replace($needle, "(redsandLegacyTrail ? $needle : $($setting[2]))")
    }
    $anchor = "`t`ttrailParticle = RE::NiPointer<RE::BSTempEffectParticle>(RE::BSTempEffectParticle::Spawn("
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $SourceRoot 'source/TrailEnchantment.inc')) + (Text (Join-Path $SourceRoot 'source/TrailDensity.inc')) + $anchor)
    $anchor = "`t`tconstexpr RE::NiPoint3 forwardVector{ 1.f, 0.f, 0.f };"
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $SourceRoot 'source/TrailUpdate.inc')) + $anchor)
    $anchor = 'if (!bAppliedTrailColorSettings || bExpired) {'
    $cpp = Replace-One $cpp $anchor 'if (!bAppliedTrailColorSettings || bExpired || redsandManaged) {'
    $anchor = "`t`tcurrentTime += a_deltaTime;"
    $cpp = Replace-One $cpp $anchor ("`t`tUpdateRedsandEmission();`n" + $anchor)
    $cpp = Replace-One $cpp 'TrailDefinition& a_outTrailDefinition) const' 'TrailDefinition& a_outTrailDefinition, bool a_ribbonColor) const'
    $header = Replace-One $header 'TrailDefinition& a_outTrailDefinition) const' 'TrailDefinition& a_outTrailDefinition, bool a_ribbonColor = false) const'
    $anchor = 'bool AttackTrail::GetTrailDefinition('
    $cpp = Replace-One $cpp $anchor ((Text (Join-Path $SourceRoot 'source/SwingTrailRuntime.inc')) + (Text (Join-Path $SourceRoot 'source/ParticleDrain.cpp.inc')) + (Text (Join-Path $SourceRoot 'source/ParticleDiagnostics.cpp.inc')) + (Text (Join-Path $SourceRoot 'source/SwordRuntime.inc')) + $anchor)
    $header = Replace-One $header '#include "Settings.h"' "#include `"Settings.h`"`n#include `"RedsandDrainState.h`"`n#include `"RedsandSwingTrail.h`"`n#include `"RedsandSettings.h`"`n#include `"RedsandMaterialColor.h`"`n#include `"RedsandLog.h`"`n#include `"RedsandMeleeStyle.h`"`n#include `"RedsandEnchantmentProfiles.h`"`n#include `"RedsandEmissionTuning.h`"`n#include `"RE/N/NiPSysGravityModifier.h`"`n#include `"RE/N/NiFloatInterpolator.h`""
    $header = Replace-One $header "private:`n" ("private:`n" + (Text (Join-Path $SourceRoot 'source/TrailMembers.inc')))
    Save $cppPath $cpp
    Save $headerPath $header

    $collisionPath = Join-Path $src 'AttackCollision.cpp'
    $collision = Text $collisionPath
    $anchor = "`t`t`t`t`t`t`tPrecisionHandler::GetSingleton()->_attackTrails.emplace_back(std::make_shared<AttackTrail>(attackCollisionNode.get(), actorHandle, cell, weaponItem, bIsLeftHand, Settings::bTrailUseAttackCollisionLength ? true : a_collisionDefinition.bTrailUseTrueLength, a_collisionDefinition.trailOverride));"
    $collision = Replace-One $collision $anchor ((Text (Join-Path $SourceRoot 'source/NativeTrailRegistration.inc')).TrimEnd("`n"))
    Save $collisionPath $collision

    $hooksPath = Join-Path $src 'Hooks.cpp'
    $hooks = Text $hooksPath
    $hooks = Replace-One $hooks '#include "Hooks.h"' "#include `"Hooks.h`"`n#include `"RedsandBowParticles.h`"`n#include `"RedsandSettings.h`"`n#include `"RedsandLog.h`"`n#include `"RedsandHavokLayout.h`"`n#include `"RedsandImpactVFX.h`"`n#include `"RedsandBlockLightning.h`""
    $anchor = "`t`t`t`t`tif (character->footIkDriver && character->setup && character->setup->data && character->setup->data->footIkDriverInfo) {"
    $hooks = Replace-One $hooks $anchor ((Text (Join-Path $SourceRoot 'source/HavokFootIK.inc')).TrimEnd("`n"))
    $anchor = "`t`tPrecisionHandler::GetSingleton()->Update(*g_deltaTime);"
    $hooks = Replace-One $hooks $anchor ($anchor + "`n`t`tRedsandBowParticles::Update(*g_deltaTime);`n`t`tRedsandImpactVFX::Update();`n`t`tRedsandSettings::TickPendingSave();`n`t`tRedsandLog::Sync();")
    Save $hooksPath $hooks
    $mainPath = Join-Path $src 'main.cpp'
    $main = Text $mainPath
    $main = Replace-One $main '#include "Events.h"' "#include `"Events.h`"`n#include `"RedsandBowParticles.h`"`n#include `"RedsandSettings.h`"`n#include `"RedsandLog.h`"`n#include `"RedsandMenu.h`"`n#include `"RedsandImpactVFX.h`"`n#include `"RedsandBlockLightning.h`""
    $anchor = "`t`tSettings::ReadSettings();"
    $main = Replace-One $main $anchor ($anchor + "`n`t`tRedsandImpactVFX::Initialize();")
    $anchor = "`tcase SKSE::MessagingInterface::kPostLoad:`n`t`tbreak;"
    $main = Replace-One $main $anchor "`tcase SKSE::MessagingInterface::kPostLoad:`n`t`tRedsandSettings::Load();`n`t`tRedsandLog::Initialize();`n`t`tRedsandLog::info(`"Redsand Particles $($config.packageVersion): material isolation, independent drain and Havok foot IK fix; runtime={}`", REL::Module::get().version().string());`n`t`tRedsandMenu::Register();`n`t`tbreak;"
    foreach ($anchor in @("`t`tPrecisionHandler::GetSingleton()->Initialize();", "`t`tSettings::OnPostLoadGame();")) {
        $main = Replace-One $main $anchor ($anchor + "`n`t`tRedsandBowParticles::SetEnabled(true);")
    }
    $anchor = "`t`tPrecisionHandler::GetSingleton()->OnPreLoadGame();"
    $main = Replace-One $main $anchor ("`t`tRedsandSettings::FlushPendingSave();`n`t`tRedsandBowParticles::SetEnabled(false);`n`t`tRedsandBlockLightning::Reset();`n" + $anchor)
    $anchor = "`tHooks::Install();"
    $main = Replace-One $main $anchor ($anchor + "`n`tRedsandBowParticles::Install();")

    $collisionPath = Join-Path $src 'AttackCollision.h'
    $collision = Text $collisionPath
    $collision = Replace-One $collision '#include "HitRefs.h"' "#include `"HitRefs.h`"`n#include `"RedsandSlashRandom.h`""
    $collision = Replace-One $collision "struct AttackCollision`n{" "struct AttackCollision`n{`n`tstd::uint32_t redsandSlashSeed = RedsandSlashRandom::NextSeed();"
    Save $collisionPath $collision
    $pendingPath = Join-Path $src 'PendingHit.cpp'
    $pending = Text $pendingPath
    $pending = Replace-One $pending '#include "PendingHit.h"' "#include `"PendingHit.h`"`n#include `"RedsandSlashRandom.h`""
    $pending = Replace-One $pending "void PendingHit::Run()`n{" "void PendingHit::Run()`n{`n`tRedsandSlashRandom::AttackScope randomSlashAttack(attackCollision ? attackCollision->redsandSlashSeed : 0u);"
    Save $pendingPath $pending
    $nativeFiles = @(
        'RedsandSwingTrail.h', 'RedsandDrainState.h', 'RedsandHavokLayout.h',
        'RedsandBowParticles.h', 'RedsandBowParticles.cpp', 'RedsandBowSimulation.h',
        'RedsandBowChargeState.h', 'RedsandBowEventState.h', 'RedsandBowStreamBudget.h',
        'BowGraphSnapshot.inc', 'BowArrowProcessing.inc', 'BowRenderer.inc', 'BowMotionDiagnostics.inc', 'RedsandMotionTrace.h',
        'RedsandEnchantmentProfiles.h', 'RedsandEnchantmentProfiles.cpp', 'RedsandSettings.h', 'RedsandSettings.cpp', 'RedsandLog.h', 'RedsandLog.cpp', 'RedsandMenu.h', 'RedsandMenu.cpp',
        'RedsandBlockLightning.h', 'RedsandBlockLightning.cpp', 'RedsandBlockLightningPolicy.h', 'RedsandImpactVFX.h', 'RedsandImpactVFX.cpp', 'RedsandImpactRouting.h', 'RedsandImpactManifest.h', 'RedsandSlashFrame.h',
        'RedsandAppearancePolicy.h', 'RedsandLiveAppearance.cpp', 'RedsandSlashRandom.h', 'RedsandMaterialColor.h', 'RedsandUpdateScope.h', 'SlashEmitterControls.inc',
        'RedsandWeaponProfiles.h', 'RedsandWeaponProfiles.cpp', 'RedsandMeleeStyle.h', 'RedsandEmissionTuning.h'
    )
    foreach ($file in $nativeFiles) {
        Copy-Item -LiteralPath (Join-Path $SourceRoot "source/$file") -Destination (Join-Path $src $file) -Force
    }
    Copy-Item -LiteralPath (Join-Path $SourceRoot 'source/vendor') -Destination $src -Recurse -Force
    $handlerPath = Join-Path $src 'PrecisionHandler.cpp'
    $handler = Text $handlerPath
    $handler = Replace-One $handler '#include "PrecisionHandler.h"' "#include `"PrecisionHandler.h`"`n#include `"RedsandBowParticles.h`""
    $anchor = "PrecisionHandler::EventResult PrecisionHandler::ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource)`n{"
    $handler = Replace-One $handler $anchor ($anchor + "`n`tRedsandBowParticles::OnAnimationEvent(a_event, a_eventSource);")
    Save $handlerPath $handler
    $main = [regex]::Replace($main, '(?m)^\s*REL::Module::reset\(\);[^\n]*\n', '')
    Save $mainPath $main
