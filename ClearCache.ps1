[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$taskCache=[IO.Path]::GetFullPath((Join-Path $taskRoot 'cache'))
if($taskCache -ne ($taskRoot+'\cache')){throw 'Unexpected cache path'}
if(-not (Test-Path -LiteralPath $taskCache)){Write-Output 'Cache is already empty.';exit 0}
$taskResolved=(Resolve-Path -LiteralPath $taskCache).ProviderPath.TrimEnd('\')
if($taskResolved -ne $taskCache){throw 'Cache resolved outside its expected directory'}
$taskMarker=Join-Path $taskCache '.redsand-cache'
if(-not (Test-Path -LiteralPath $taskMarker) -or [IO.File]::ReadAllText($taskMarker).Trim() -ne $taskRoot){throw 'This is not the managed Redsand cache'}
foreach($taskItem in @((Get-Item -LiteralPath $taskCache -Force)) + @(Get-ChildItem -LiteralPath $taskCache -Force -Recurse)) {
 $taskPath=[IO.Path]::GetFullPath($taskItem.FullName)
 if($taskPath -ne $taskCache -and -not $taskPath.StartsWith($taskCache+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cache contains an out-of-scope path'}
 if($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked cache item is not supported: $taskPath"}
}
Remove-Item -LiteralPath $taskCache -Recurse -Force
Write-Output 'Only the Redsand cache was deleted. Source, output, recovery and installed mod were preserved.'
