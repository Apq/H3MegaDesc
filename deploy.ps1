$ErrorActionPreference = 'Stop'
# 共享部署目标：$env:H3_GAME_DIR > 仓库根 H3Env.ps1 > 内置默认值（换版本改 H3Env.ps1）。
$h3root = Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath "$h3root\H3Env.ps1") { . "$h3root\H3Env.ps1" }
if (-not (Get-Command Get-H3GameDir -ErrorAction SilentlyContinue)) {
    function Get-H3GameDir {
        if ($env:H3_GAME_DIR -and (Test-Path -LiteralPath $env:H3_GAME_DIR)) { $env:H3_GAME_DIR }
        else { 'D:\Heroes3\Heroes3_2026.10.09' }
    }
}
$gameDir = Get-H3GameDir
$packsDst = "$gameDir\_HD3_Data\Packs\大描述框"
# $PSScriptRoot 是 PowerShell 自动变量，表示当前 deploy.ps1 所在目录。
$src = "$PSScriptRoot\Release"
$imgSrc = "$PSScriptRoot\pcx"

# --- MegaDesc 插件 ---
if (-not (Test-Path $packsDst)) {
    New-Item -ItemType Directory -Path $packsDst -Force | Out-Null
}
Copy-Item "$src\MegaDesc.dll" $packsDst -Force
Copy-Item "$PSScriptRoot\MegaDesc.ini" $packsDst -Force
Copy-Item "$PSScriptRoot\使用说明.txt" $packsDst -Force

# --- 24-bit PCX 素材部署到插件目录\pcx ---
$imgDst = "$packsDst\pcx"
if (-not (Test-Path $imgDst)) {
    New-Item -ItemType Directory -Path $imgDst -Force | Out-Null
}
$pcxFiles = Get-ChildItem "$imgSrc\bv_*.pcx" | Where-Object {
    $_.Name -notmatch 'prev'
}
Write-Host "PCX 源目录: $imgSrc"
Write-Host "PCX 目标目录: $imgDst"
Write-Host "待复制 PCX 文件数: $($pcxFiles.Count)"
$copiedCount = 0
foreach ($f in $pcxFiles) {
    Copy-Item $f.FullName $imgDst -Force
    $copiedCount++
}
Write-Host "成功复制 PCX 文件数: $copiedCount"

Write-Host "已部署到 $packsDst"
