param(
    [Parameter(Mandatory=$true)][ValidateSet('ssd1680', 'jd79661')][string]$Panel,
    [string]$CacheRoot = 'C:\cwesp32-idf',
    [string]$ProvisioningPublicKey = $env:INVENTORY_COUNTER_PROVISIONING_PUBLIC_KEY
)
$ErrorActionPreference = 'Stop'
if ($ProvisioningPublicKey -cnotmatch '^04[0-9a-f]{128}$') {
    throw 'Supply the persistent Inventory Counter authority public key (130 lowercase hex characters). Never use a test key.'
}
$CacheRoot = [IO.Path]::GetFullPath($CacheRoot)
if ($CacheRoot -eq 'C:\cwesp32' -or $CacheRoot.StartsWith('C:\cwesp32\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Choose a separate IDF cache; the stock Arduino cache must remain intact.'
}
Get-Command docker, git -ErrorAction Stop | Out-Null
$arduinoCommit = '9b4622dc40e3cd0a9220d0e44b3291ea28500940'
$idfImage = 'espressif/idf@sha256:9e3861f1fa5fc6a16859de5e34aec99aebe5e5025943abdc6e80a43070865e33'
$component = Join-Path $CacheRoot 'arduino-2.0.10'
$stage = Join-Path $CacheRoot ('source-' + $Panel)
New-Item -ItemType Directory -Path $CacheRoot, $stage -Force | Out-Null
$cacheScope = $CacheRoot.TrimEnd('\') + '\'
$stage = [IO.Path]::GetFullPath($stage)
if (-not $stage.StartsWith($cacheScope, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Generated stage is outside the requested cache.'
}
# A failed or interrupted new build must not leave a valid old output manifest.
$manifest = Join-Path $stage 'output\sha256.json'
if (Test-Path -LiteralPath $manifest) { Remove-Item -LiteralPath $manifest -Force }
if (-not (Test-Path -LiteralPath $component)) {
    & git -c core.autocrlf=false clone --branch 2.0.10 --depth 1 https://github.com/espressif/arduino-esp32.git $component
    if ($LASTEXITCODE) { throw 'Pinned Arduino component download failed.' }
}
$actualCommit = (& git -C $component rev-parse HEAD)
if ($LASTEXITCODE -or $actualCommit -ne $arduinoCommit) { throw 'Arduino component does not match the pinned 2.0.10 commit.' }
$changes = (& git -C $component status --porcelain --untracked-files=all)
if ($LASTEXITCODE -or $changes) { throw 'Pinned Arduino component has local changes.' }
& docker image inspect $idfImage *> $null
if ($LASTEXITCODE) {
    & docker pull $idfImage
    if ($LASTEXITCODE) { throw 'Pinned ESP-IDF image download failed.' }
}
$main = Join-Path $stage 'main'
# Clear only generated source files in the verified stage, so deleted headers
# cannot silently remain available to a later build.
if (Test-Path -LiteralPath $main) {
    Get-ChildItem -LiteralPath $main -File |
        Where-Object { $_.Extension -in '.h', '.c', '.cpp' } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
}
Copy-Item -Path (Join-Path $PSScriptRoot 'idf\*') -Destination $stage -Recurse -Force
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'inventory_counter_label') -File |
    Where-Object { $_.Extension -in '.h', '.c', '.cpp' } |
    Copy-Item -Destination $main -Force
@"
#pragma once
// Public trust anchor exported from this installation's persistent authority.
constexpr char kProvisioningAuthorityPublicKey[] = "$ProvisioningPublicKey";
"@ | Set-Content -LiteralPath (Join-Path $main 'provisioning_authority.h') -Encoding ascii
# Regenerate from reviewed defaults on every run; do not retain old Kconfig choices.
foreach ($name in @('sdkconfig', 'sdkconfig.old')) {
    $generated = Join-Path $stage $name
    if (Test-Path -LiteralPath $generated) { Remove-Item -LiteralPath $generated -Force }
}
$hash = [Security.Cryptography.SHA256]::Create()
try {
    $cacheId = -join ($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($CacheRoot.ToLowerInvariant()))[0..5] | ForEach-Object { $_.ToString('x2') })
} finally { $hash.Dispose() }
$volume = 'inventory-counter-idf445-' + $cacheId + '-' + $Panel
$log = Join-Path $CacheRoot ($Panel + '-idf-build.log')
$arguments = @('run', '--rm', '--mount', "type=bind,source=$stage,target=/project",
    '--mount', "type=bind,source=$component,target=/arduino,readonly",
    '--mount', "type=volume,source=$volume,target=/build", '-w', '/project',
    $idfImage, 'sh', '/project/build.sh', $Panel)
& docker @arguments 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE) { throw "ESP-IDF build failed. Log: $log" }
Write-Output "Compiled $Panel with Arduino 2.0.10 and rebuilt ESP-IDF 4.4.5. Artifacts: $stage\output"
Write-Output 'No board was flashed. Use the exported flasher_args.json for the verified image offsets.'
