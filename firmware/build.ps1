param(
    [Parameter(Mandatory=$true)][ValidateSet('ssd1680', 'jd79661')][string]$Panel,
    [string]$CacheRoot = 'C:\cwesp32',
    [string]$Cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
    [string]$ProvisioningPublicKey = $env:INVENTORY_COUNTER_PROVISIONING_PUBLIC_KEY,
    [ValidateRange(30,900)][int]$BuildTimeoutSeconds = 600,
    [switch]$Bootstrap
)
$ErrorActionPreference = 'Stop'
if ($ProvisioningPublicKey -cnotmatch '^04[0-9a-f]{128}$') {
    throw 'Supply -ProvisioningPublicKey with the persistent Inventory Counter authority public key (130 lowercase hex characters). Never use a test key.'
}
if (-not (Test-Path -LiteralPath $Cli)) { throw "Arduino CLI not found: $Cli" }
$CacheRoot = [IO.Path]::GetFullPath($CacheRoot)
if ($CacheRoot -eq [IO.Path]::GetFullPath("$env:LOCALAPPDATA\Arduino15")) {
    throw 'Use a separate Inventory Counter cache, not the Arduino IDE package directory.'
}
New-Item -ItemType Directory -Path $CacheRoot -Force | Out-Null
$configPath = Join-Path $CacheRoot 'arduino-cli.yaml'
$yamlRoot = $CacheRoot.Replace('\', '/')
@"
board_manager:
  additional_urls:
    - https://espressif.github.io/arduino-esp32/package_esp32_index.json
directories:
  data: $yamlRoot/data
  downloads: $yamlRoot/downloads
  user: $yamlRoot/user
"@ | Set-Content -LiteralPath $configPath -Encoding utf8
if ($Bootstrap) {
    & $Cli core update-index --config-file $configPath
    if ($LASTEXITCODE) { throw 'Board index download failed.' }
    & $Cli core install 'esp32:esp32@2.0.10' --config-file $configPath
    if ($LASTEXITCODE) { throw 'Pinned ESP32 core installation failed.' }
}
$platform = Join-Path $CacheRoot 'data\packages\esp32\hardware\esp32\2.0.10'
if (-not (Test-Path -LiteralPath $platform)) { throw 'Run with -Bootstrap to install ESP32 core 2.0.10.' }
$installed = @((Get-ChildItem -LiteralPath (Split-Path $platform) -Directory).Name)
if ($installed.Count -ne 1 -or $installed[0] -ne '2.0.10') {
    throw 'This dedicated cache contains another ESP32 version. Choose a clean CacheRoot.'
}
$buildPath = Join-Path $CacheRoot $Panel
$sourceParent = Join-Path $CacheRoot ('source-' + $Panel)
$sourcePath = Join-Path $sourceParent 'inventory_counter_label'
New-Item -ItemType Directory -Path $sourcePath -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'inventory_counter_label') -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $sourcePath -Force
}
@"
#pragma once
// Public trust anchor exported from this installation's persistent authority.
constexpr char kProvisioningAuthorityPublicKey[] = "$ProvisioningPublicKey";
"@ | Set-Content -LiteralPath (Join-Path $sourcePath 'provisioning_authority.h') -Encoding ascii
$profileFlag = '-DINVENTORY_COUNTER_' + $Panel.ToUpperInvariant()
$compileArguments = @('compile', '--config-file', "`"$configPath`"", '--fqbn',
    'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,CDCOnBoot=default',
    '--build-property', "`"compiler.cpp.extra_flags=$profileFlag`"", '--build-path', "`"$buildPath`"", "`"$sourcePath`"")
$stdoutPath = Join-Path $CacheRoot ($Panel + '-compile.log')
$stderrPath = Join-Path $CacheRoot ($Panel + '-compile-errors.log')
$compiler = Start-Process -FilePath $Cli -ArgumentList $compileArguments -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
if (-not $compiler.WaitForExit($BuildTimeoutSeconds * 1000)) {
    & taskkill.exe /PID $compiler.Id /T /F | Out-Null
    throw "Build exceeded $BuildTimeoutSeconds seconds for $Panel. Logs: $stdoutPath, $stderrPath"
}
$compiler.Refresh()
Get-Content -LiteralPath $stdoutPath
Get-Content -LiteralPath $stderrPath
if ($compiler.ExitCode -ne 0) { throw "Build failed for $Panel (exit $($compiler.ExitCode))." }
Write-Output "Compiled $Panel with ESP32 core 2.0.10. Artifacts: $buildPath"
Write-Output 'No board was flashed. Identify the actual display controller before uploading.'
