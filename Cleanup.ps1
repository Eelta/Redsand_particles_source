[CmdletBinding()]
param([switch]$Apply)
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path -LiteralPath $PSScriptRoot).ProviderPath.TrimEnd('\')
foreach($taskRequired in @('Build.ps1','source\RedsandSettings.h','output\Redsand Particles.zip')) {
 if(-not (Test-Path -LiteralPath (Join-Path $taskRoot $taskRequired))){throw "Missing current workspace file: $taskRequired"}
}
$taskKeep=@('Redsand Particles.zip')
$taskPreserved=@{}
foreach($taskName in $taskKeep){$taskPath=Join-Path $taskRoot ('output\'+$taskName);if(Test-Path -LiteralPath $taskPath -PathType Leaf){$taskPreserved[$taskPath]=(Get-FileHash -LiteralPath $taskPath).Hash}}
$taskTargets=@()
foreach($taskName in @('.build','backups','recovery','build','tools\__pycache__','tests\__pycache__')){ $taskPath=Join-Path $taskRoot $taskName;if(Test-Path -LiteralPath $taskPath){$taskTargets+=Get-Item -LiteralPath $taskPath -Force} }
$taskTargets+=@(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'output') -Force | Where-Object Name -NotIn $taskKeep)
foreach($taskItem in $taskTargets){
 $taskResolved=(Resolve-Path -LiteralPath $taskItem.FullName).ProviderPath.TrimEnd('\')
 if(-not $taskResolved.StartsWith($taskRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Archive source escaped the workspace'}
 $taskChildren=@($taskItem)
 if($taskItem.PSIsContainer){$taskChildren+=@(Get-ChildItem -LiteralPath $taskResolved -Force -Recurse)}
 foreach($taskChild in $taskChildren){
  if($taskChild.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked path is not archived: $($taskChild.FullName)"}
 }
 Write-Output "Archive target: $taskResolved"
}
if(-not $Apply){Write-Output 'Preview only. Run ./Cleanup.ps1 -Apply to move intermediates into cache.';return}
if($taskTargets.Count -eq 0){Write-Output 'No generated intermediates remain.';return}
$taskCache=[IO.Path]::GetFullPath((Join-Path $taskRoot 'cache'))
if($taskCache -ne $taskRoot+'\cache'){throw 'Unexpected cache destination'}
New-Item -ItemType Directory -Path $taskCache -Force | Out-Null
$taskMarker=Join-Path $taskCache '.redsand-cache'
if((Test-Path -LiteralPath $taskMarker) -and [IO.File]::ReadAllText($taskMarker).Trim() -ne $taskRoot){throw 'Cache belongs to another workspace'}
[IO.File]::WriteAllText($taskMarker,$taskRoot)
$taskBatch=Join-Path $taskCache ('build-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
if(-not ([IO.Path]::GetFullPath($taskBatch)).StartsWith($taskCache+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Archive batch escaped cache'}
New-Item -ItemType Directory -Path $taskBatch | Out-Null
$taskMoves=@()
foreach($taskItem in $taskTargets){
 $taskRelative=$taskItem.FullName.Substring($taskRoot.Length+1)
 $taskDestination=[IO.Path]::GetFullPath((Join-Path $taskBatch $taskRelative))
 if(-not $taskDestination.StartsWith($taskBatch+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Move target escaped cache batch'}
 New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskDestination)) -Force | Out-Null
 Move-Item -LiteralPath $taskItem.FullName -Destination $taskDestination
 $taskMoves+=[pscustomobject]@{source=$taskItem.FullName;target=$taskDestination}
}
foreach($taskPath in $taskPreserved.Keys){if((Get-FileHash -LiteralPath $taskPath).Hash -ne $taskPreserved[$taskPath]){throw 'Current output changed during archive'}}
$taskMoves | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskBatch 'MANIFEST.json') -Encoding UTF8
Write-Output "Archived intermediates into $taskBatch. ClearCache.cmd deletes only cache."
