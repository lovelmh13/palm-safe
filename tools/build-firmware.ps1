# 职责：在专用英文目录编译固件，并将交付产物复制回掌心保险箱工程，不刷写设备。
param(
    # 本机 ESP-IDF 环境入口。
    [string]$ActivationScript = 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1',
    # 独立构建目录，不能含中文或空格。
    [string]$BuildDirectory = 'D:\esp\palm-safe-work'
)
$ErrorActionPreference = 'Stop'
# 固定项目根目录、同步源目录及本地交付目录。
$projectRoot = Split-Path $PSScriptRoot -Parent
$sourceDirectory = Join-Path $projectRoot 'firmware'
$releaseDirectory = Join-Path $projectRoot 'local\releases'
if ($BuildDirectory -match '[^\x00-\x7F]|\s') { throw '构建目录不能含中文或空格。' }
$resolvedBuild = [IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\')
if ([IO.Path]::GetFileName($resolvedBuild) -ne 'palm-safe-work') { throw '构建目录末级必须为 palm-safe-work，避免覆盖其他项目。' }
if (Test-Path -LiteralPath $resolvedBuild) {
    $existingProject = Join-Path $resolvedBuild 'CMakeLists.txt'
    if (!(Test-Path -LiteralPath $existingProject) -or (Get-Content -LiteralPath $existingProject -Raw) -notmatch 'project\(palm_safe\)') {
        throw '构建目录已存在且不属于本工程，停止复制。'
    }
}
New-Item -ItemType Directory -Path $resolvedBuild -Force | Out-Null
robocopy $sourceDirectory $resolvedBuild /E /XD build managed_components /XF sdkconfig sdkconfig.old /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw '同步固件源码失败。' }
$env:PYTHONUTF8 = '1'
. $ActivationScript
# 默认配置变更使用新的生成配置文件，防止旧 sdkconfig 静默覆盖新源码配置。
$defaultsHash = (Get-FileHash -LiteralPath (Join-Path $sourceDirectory 'sdkconfig.defaults') -Algorithm SHA256).Hash.Substring(0, 16).ToLowerInvariant()
$generatedConfiguration = Join-Path $resolvedBuild "sdkconfig.safe-$defaultsHash"
idf.py -C $resolvedBuild -D "SDKCONFIG=$generatedConfiguration" build
if ($LASTEXITCODE -ne 0) { throw '固件构建失败。' }
New-Item -ItemType Directory -Path $releaseDirectory -Force | Out-Null
# 按烧录清单保持相对目录，避免地址表引用不存在的平铺文件。
$buildOutput = Join-Path $resolvedBuild 'build'
$flashManifest = Get-Content -LiteralPath (Join-Path $buildOutput 'flasher_args.json') -Raw | ConvertFrom-Json
foreach ($entry in $flashManifest.flash_files.PSObject.Properties) {
    # 每项只能读取本构建目录中的产物，并复制到相同相对目录。
    $relativePath = [string]$entry.Value
    $inputPath = [IO.Path]::GetFullPath((Join-Path $buildOutput $relativePath))
    $outputPath = [IO.Path]::GetFullPath((Join-Path $releaseDirectory $relativePath))
    if (!$inputPath.StartsWith($buildOutput + '\', [StringComparison]::OrdinalIgnoreCase) -or !$outputPath.StartsWith($releaseDirectory + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw '烧录清单包含构建目录之外的路径，停止归档。'
    }
    New-Item -ItemType Directory -Path (Split-Path $outputPath -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $inputPath -Destination $outputPath -Force
}
Copy-Item -LiteralPath (Join-Path $buildOutput 'flash_args') -Destination $releaseDirectory -Force
Copy-Item -LiteralPath (Join-Path $buildOutput 'flasher_args.json') -Destination $releaseDirectory -Force
Copy-Item -LiteralPath (Join-Path $resolvedBuild 'dependencies.lock') -Destination (Join-Path $sourceDirectory 'dependencies.lock') -Force
Write-Host "构建完成，固件位于：$releaseDirectory"


