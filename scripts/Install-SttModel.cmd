@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

rem ===================================================================
rem  BSChat STT model downloader
rem
rem  Interactive installer for sherpa-onnx online ASR models. Downloads the
rem  runtime and the selected model, verifies them, then writes the
rem  sttModel section of config\bschat.json. Restart the server afterwards.
rem
rem  Usage:
rem    Install-SttModel.cmd                    interactive menu
rem    Install-SttModel.cmd bilingual-zh-en    install a tier silently
rem
rem  Tiers: zh-14m / zh-standard / zh-xlarge / bilingual-zh-en /
rem         trilingual-zh-cantonese-en
rem
rem  The actual logic is embedded PowerShell 5.1 (ships with Windows).
rem  Keep every line above the payload marker ASCII-only: cmd.exe parses
rem  this file byte by byte under the console codepage, and non-ASCII text
rem  in comments corrupts %variable% expansion on the following lines.
rem ===================================================================

set "SCRIPT_DIR=%~dp0"

where powershell.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] powershell.exe not found.
    echo         This script needs PowerShell 5.1, which ships with Windows.
    pause
    exit /b 1
)

rem Extract the PowerShell payload that follows the marker into a temp file,
rem so the deployment directory keeps this single .cmd and no leftovers.
rem Paths travel through environment variables to avoid nested quoting.
set "BSCHAT_CMD_SELF=%~f0"
set "BSCHAT_CMD_TEMP=%TEMP%\bschat-stt-install-%RANDOM%%RANDOM%.ps1"
set "BSCHAT_CMD_BEGIN=### POWERSHELL_PAYLOAD_BEGIN ###"

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $c = Get-Content -LiteralPath $env:BSCHAT_CMD_SELF -Encoding UTF8; $i = [array]::IndexOf($c, $env:BSCHAT_CMD_BEGIN); if ($i -lt 0) { throw 'payload marker not found' }; $j = $c.Length - 1; while ($c[$j] -like '### *') { $j-- }; [System.IO.File]::WriteAllLines($env:BSCHAT_CMD_TEMP, $c[($i+1)..$j], (New-Object System.Text.UTF8Encoding($true)))"

if errorlevel 1 (
    echo [ERROR] Failed to extract the installer payload. Please re-download this file.
    pause
    exit /b 1
)

rem Forward a caller-supplied tier id as an explicit -Model argument.
rem Use goto labels rather than an if/else block: inside a parenthesised
rem block cmd.exe expands every %VAR% at parse time and merges the
rem adjacent -ModRoot value with the following argument.
if "%~1"=="" goto run_interactive
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%BSCHAT_CMD_TEMP%" -ModRoot "%SCRIPT_DIR%.." -Model "%~1"
goto after_run

:run_interactive
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%BSCHAT_CMD_TEMP%" -ModRoot "%SCRIPT_DIR%.."

:after_run

set "EXITCODE=%ERRORLEVEL%"
del /q "%BSCHAT_CMD_TEMP%" >nul 2>&1

echo.
if "%EXITCODE%"=="0" (
    echo Installation finished. Restart the server to apply the configuration.
    pause
    exit /b 0
)
echo [FAILED] Installation did not complete. Exit code: %EXITCODE%
pause
exit /b %EXITCODE%

### POWERSHELL_PAYLOAD_BEGIN ###
# BSChat STT 模型安装器（由 Install-SttModel.cmd 内嵌调用，请勿直接运行本段）。
# 部署目录只分发一个 .cmd，实际逻辑在此以 PowerShell 5.1 实现。

[CmdletBinding()]
param(
    [string[]]$Model,
    [string]$ModRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$SherpaRuntimeVersion = '1.13.8'
$SherpaRuntimeArchive = 'sherpa-onnx-v1.13.8-win-x64-shared-MD-Release-no-tts-lib.tar.bz2'
$SherpaRuntimeBytes = 6907798

# 运行时二进制挂在 vX.Y.Z 发布页；模型权重挂在 asr-models 标签页。两者不同源。
$RuntimeBase = 'https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.13.8'
$ReleaseBase = 'https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models'

# 模型清单。Files 为安装后必须存在的文件，缺失即判定下载不完整。
# Map 描述模型目录下的文件如何映射到 bschat.json 的 sttModel 字段。
$Catalog = @(
    [pscustomobject]@{
        Id          = 'zh-14m'
        Name        = '极速 · 纯中文'
        Languages   = '中文'
        SizeText    = '25 MB'
        Rtf         = '0.038'
        Note        = '体积最小，低配服务器或带宽受限时使用'
        Archive     = 'sherpa-onnx-streaming-zipformer-small-ctc-zh-int8-2025-04-01.tar.bz2'
        ArchiveSize = 21288448
        ModelType   = 'zipformer2_ctc'
        Files       = @('model.int8.onnx', 'tokens.txt')
        Map         = @{ modelPath = 'model.int8.onnx'; tokensPath = 'tokens.txt' }
    }
    [pscustomobject]@{
        Id          = 'zh-standard'
        Name        = '标准 · 纯中文'
        Languages   = '中文'
        SizeText    = '155 MB'
        Rtf         = '0.15'
        Note        = '14k 小时中文语料训练，纯中文场景推荐'
        Archive     = 'sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30.tar.bz2'
        ArchiveSize = 132646912
        ModelType   = 'transducer'
        Files       = @('encoder.int8.onnx', 'decoder.onnx', 'joiner.int8.onnx', 'tokens.txt')
        Map         = @{
            encoderPath = 'encoder.int8.onnx'
            decoderPath = 'decoder.onnx'
            joinerPath  = 'joiner.int8.onnx'
            tokensPath  = 'tokens.txt'
        }
    }
    [pscustomobject]@{
        Id          = 'zh-xlarge'
        Name        = '高配 · 纯中文'
        Languages   = '中文'
        SizeText    = '570 MB'
        Rtf         = '0.46'
        Note        = '纯中文精度天花板，需要较强的服务端 CPU'
        Archive     = 'sherpa-onnx-streaming-zipformer-zh-xlarge-int8-2025-06-30.tar.bz2'
        ArchiveSize = 597688704
        ModelType   = 'transducer'
        Files       = @('encoder.int8.onnx', 'decoder.onnx', 'joiner.int8.onnx', 'tokens.txt')
        Map         = @{
            encoderPath = 'encoder.int8.onnx'
            decoderPath = 'decoder.onnx'
            joinerPath  = 'joiner.int8.onnx'
            tokensPath  = 'tokens.txt'
        }
    }
    [pscustomobject]@{
        Id          = 'bilingual-zh-en'
        Name        = '中英双语'
        Languages   = '中文 / 英语 / 多种方言'
        SizeText    = '227 MB'
        Rtf         = '0.15'
        Note        = '国际服默认档，可处理中英混说'
        Archive     = 'sherpa-onnx-streaming-paraformer-bilingual-zh-en.tar.bz2'
        ArchiveSize = 237797376
        ModelType   = 'paraformer'
        Files       = @('encoder.int8.onnx', 'decoder.int8.onnx', 'tokens.txt')
        Map         = @{
            encoderPath = 'encoder.int8.onnx'
            decoderPath = 'decoder.int8.onnx'
            tokensPath  = 'tokens.txt'
        }
    }
    [pscustomobject]@{
        Id          = 'trilingual-zh-cantonese-en'
        Name        = '中英粤三语'
        Languages   = '中文 / 粤语 / 英语'
        SizeText    = '229 MB'
        Rtf         = '0.14'
        Note        = '需要粤语支持时使用'
        Archive     = 'sherpa-onnx-streaming-paraformer-trilingual-zh-cantonese-en.tar.bz2'
        ArchiveSize = 240172288
        ModelType   = 'paraformer'
        Files       = @('encoder.int8.onnx', 'decoder.int8.onnx', 'tokens.txt')
        Map         = @{
            encoderPath = 'encoder.int8.onnx'
            decoderPath = 'decoder.int8.onnx'
            tokensPath  = 'tokens.txt'
        }
    }
)

# 仅保留 STT 推理需要的 DLL。sherpa-onnx-c-api.dll 与 onnxruntime.dll 必须同目录：
# 运行时用 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 加载，异目录会导致依赖解析失败。
$RuntimeFiles = @(
    'sherpa-onnx-c-api.dll',
    'onnxruntime.dll',
    'onnxruntime_providers_shared.dll'
)

function Write-Step {
    param([string]$Message)
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Detail {
    param([string]$Message)
    Write-Host "    $Message" -ForegroundColor DarkGray
}

function Format-Size {
    param([long]$Bytes)
    if ($Bytes -ge 1GB) { return ('{0:N2} GB' -f ($Bytes / 1GB)) }
    if ($Bytes -ge 1MB) { return ('{0:N1} MB' -f ($Bytes / 1MB)) }
    return ('{0:N0} KB' -f ($Bytes / 1KB))
}

function Get-RelativePath {
    param([string]$Base, [string]$Target)
    $baseFull = [System.IO.Path]::GetFullPath($Base)
    $targetFull = [System.IO.Path]::GetFullPath($Target)
    if (-not $targetFull.StartsWith($baseFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $targetFull
    }
    $relative = $targetFull.Substring($baseFull.Length).TrimStart('\', '/')
    return ($relative -replace '\\', '/')
}

function Get-RuntimeDirectory {
    return (Join-Path $ModRoot 'stt\runtime')
}

function Get-ModelDirectory {
    param([string]$Id)
    return (Join-Path $ModRoot "stt\models\$Id")
}

function Get-ConfigPath {
    return (Join-Path $ModRoot 'config\bschat.json')
}

# StrictMode 下空对象上访问 .PSObject.Properties.Name 会抛 PropertyNotFoundStrict，
# 因此统一用 Test-JsonProperty 逐个枚举比较。
function Test-JsonProperty {
    param([object]$Target, [string]$Name)
    foreach ($property in $Target.PSObject.Properties) {
        if ($property.Name -eq $Name) { return $true }
    }
    return $false
}

# PowerShell 5.1 的 Invoke-WebRequest 走旧 HttpWebRequest，下载大文件经过
# GitHub CDN 多级重定向时经常中途断连（"连接被意外关闭"）。curl.exe 随
# Windows 10 1803 起自带，对重定向链和大文件传输都稳定得多，故优先使用；
# 没有 curl.exe 时退回 Invoke-WebRequest。两条路径都带重试。
function Get-CurlPath {
    $command = Get-Command curl.exe -CommandType Application -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return $null
}

function Show-DownloadProgress {
    param([System.Diagnostics.Process]$Process, [string]$OutFile, [long]$ExpectedBytes)

    $lastPercent = -1
    # 进度轮询只负责画条，不负责判定成败；成败一律由 WaitForExit 后的
    # 文件大小校验决定。
    while (-not $Process.HasExited) {
        Start-Sleep -Milliseconds 400
        if (-not (Test-Path -LiteralPath $OutFile)) { continue }
        $current = (Get-Item -LiteralPath $OutFile).Length
        if ($ExpectedBytes -le 0) { continue }
        $percent = [int]($current / $ExpectedBytes * 100)
        if ($percent -ge 100) { $percent = 99 }
        if ($percent -lt $lastPercent + 5) { continue }
        $lastPercent = $percent - ($percent % 5)
        $bar = '#' * [int]($lastPercent / 2.5)
        $pad = ' ' * (40 - $bar.Length)
        Write-Host ("`r    [{0}{1}] {2,3}%  {3}" -f $bar, $pad, $lastPercent, (Format-Size $current)) -NoNewline
    }
}

function Invoke-Download {
    param(
        [string]$Url,
        [string]$OutFile,
        [long]$ExpectedBytes = 0,
        [switch]$ShowProgress
    )

    # GitHub 要求 TLS 1.2，而 PS 5.1 所在机器的 .NET 配置不一定默认开启。
    $previousProtocol = [Net.ServicePointManager]::SecurityProtocol
    [Net.ServicePointManager]::SecurityProtocol =
        $previousProtocol -bor [Net.SecurityProtocolType]::Tls12

    $curl = Get-CurlPath
    $attempts = 3
    try {
        for ($i = 1; $i -le $attempts; $i++) {
            try {
                if (Test-Path -LiteralPath $OutFile) { Remove-Item -LiteralPath $OutFile -Force }

                $curlLog = $null
                if ($curl) {
                    $curlLog = "$OutFile.curl.log"
                    # --ssl-no-revoke: 用户的网络环境访问不到证书吊销点，
                    # schannel 会报 CRYPT_E_NO_REVOCATION_CHECK 直接失败。
                    # 这里不做吊销检查，TLS 本身仍由 schannel 校验。
                    $arguments = @(
                        '-L', '--fail', '--silent', '--show-error',
                        '--retry', '3', '--retry-delay', '2', '--connect-timeout', '30',
                        '--ssl-no-revoke',
                        '-o', $OutFile, $Url
                    )
                    $process = Start-Process -FilePath $curl -ArgumentList $arguments `
                        -NoNewWindow -PassThru -RedirectStandardError $curlLog
                    if ($ShowProgress -and $ExpectedBytes -gt 0) {
                        Show-DownloadProgress -Process $process -OutFile $OutFile -ExpectedBytes $ExpectedBytes
                    }
                    $process.WaitForExit()
                    # Start-Process -PassThru 的 Process 对象在本环境下 WaitForExit
                    # 之后 ExitCode 仍为 $null（实测），因此不依赖退出码判成败。
                }
                else {
                    $previousRevocation = [Net.ServicePointManager]::CheckCertificateRevocationList
                    [Net.ServicePointManager]::CheckCertificateRevocationList = $false
                    try {
                        $ProgressPreference = 'SilentlyContinue'
                        Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing
                    }
                    finally {
                        [Net.ServicePointManager]::CheckCertificateRevocationList = $previousRevocation
                    }
                }

                # 成功判据只有一条：文件存在且非空。退出码与字节数都不校验。
                if (-not (Test-Path -LiteralPath $OutFile)) {
                    $tail = ''
                    if ($curlLog -and (Test-Path -LiteralPath $curlLog)) {
                        $tail = (Get-Content -LiteralPath $curlLog -Tail 2 -ErrorAction SilentlyContinue) -join ' '
                    }
                    throw "未下载到文件：$tail"
                }
                $actual = (Get-Item -LiteralPath $OutFile).Length
                if ($actual -le 0) { throw '下载得到空文件' }
                if ($ShowProgress) { Write-Host '' }
                if ($curlLog) { Remove-Item -LiteralPath $curlLog -Force -ErrorAction SilentlyContinue }
                return
            }
            catch {
                if (Test-Path -LiteralPath $OutFile) { Remove-Item -LiteralPath $OutFile -Force -ErrorAction SilentlyContinue }
                if ($i -lt $attempts) {
                    Write-Host ("`r    第 {0}/{1} 次下载失败：{2}，{3} 秒后重试" -f $i, $attempts, $_.Exception.Message, (2 * $i)) -NoNewline
                    Start-Sleep -Seconds (2 * $i)
                    Write-Host ''
                }
                else {
                    throw "下载失败（已重试 $attempts 次）：$($_.Exception.Message)"
                }
            }
        }
    }
    finally {
        [Net.ServicePointManager]::SecurityProtocol = $previousProtocol
    }
}

function Install-Runtime {
    $runtimeDir = Get-RuntimeDirectory
    Write-Step "准备 sherpa-onnx 运行时 $SherpaRuntimeVersion"

    $existing = @($RuntimeFiles | Where-Object { Test-Path -LiteralPath (Join-Path $runtimeDir $_) })
    if ($existing.Count -eq $RuntimeFiles.Count) {
        Write-Detail '运行时已就绪，跳过'
        return
    }

    New-Item -ItemType Directory -Path $runtimeDir -Force | Out-Null

    $url = "$RuntimeBase/$SherpaRuntimeArchive"
    $temp = Join-Path ([System.IO.Path]::GetTempPath()) $SherpaRuntimeArchive

    Write-Detail "下载 $url"
    Invoke-Download -Url $url -OutFile $temp
    Write-Detail "下载完成 $(Format-Size (Get-Item -LiteralPath $temp).Length)"

    $extractDir = Join-Path ([System.IO.Path]::GetTempPath()) "bschat-stt-runtime-$([guid]::NewGuid().ToString('N'))"
    try {
        New-Item -ItemType Directory -Path $extractDir -Force | Out-Null
        Write-Detail '解压中'
        & tar.exe -xf $temp -C $extractDir
        if ($LASTEXITCODE -ne 0) { throw "解压运行时失败，tar 退出码 $LASTEXITCODE" }

        $libDir = Get-ChildItem -Path $extractDir -Recurse -Directory -Filter 'lib' |
            Select-Object -First 1
        if (-not $libDir) { throw '运行时包中未找到 lib 目录' }

        foreach ($file in $RuntimeFiles) {
            $source = Join-Path $libDir.FullName $file
            if (-not (Test-Path -LiteralPath $source)) {
                throw "运行时包缺少 $file"
            }
            Copy-Item -LiteralPath $source -Destination (Join-Path $runtimeDir $file) -Force
            Write-Detail "已安装 $file"
        }
    }
    finally {
        Remove-Item -LiteralPath $extractDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
    }
}

function Install-Model {
    param([pscustomobject]$Entry)

    $modelDir = Get-ModelDirectory $Entry.Id
    $archiveName = $Entry.Archive
    $url = "$ReleaseBase/$archiveName"

    Write-Step "安装 $($Entry.Name) [$($Entry.Id)]"

    $complete = $true
    foreach ($file in $Entry.Files) {
        if (-not (Test-Path -LiteralPath (Join-Path $modelDir $file))) { $complete = $false; break }
    }
    if ($complete) {
        Write-Detail '模型已存在，跳过下载'
    }
    else {
        $temp = Join-Path ([System.IO.Path]::GetTempPath()) $archiveName
        Write-Detail "下载 $url"
        Write-Detail "参考体积 $(Format-Size $Entry.ArchiveSize)"

        Invoke-Download -Url $url -OutFile $temp -ExpectedBytes $Entry.ArchiveSize -ShowProgress
        Write-Detail "下载完成 $(Format-Size (Get-Item -LiteralPath $temp).Length)"

        $extractDir = Join-Path ([System.IO.Path]::GetTempPath()) "bschat-stt-$([guid]::NewGuid().ToString('N'))"
        try {
            New-Item -ItemType Directory -Path $extractDir -Force | Out-Null
            Write-Detail '解压中'
            & tar.exe -xf $temp -C $extractDir
            if ($LASTEXITCODE -ne 0) { throw "解压失败，tar 退出码 $LASTEXITCODE" }

            # 官方包解压后是一层同名目录，剥掉它让模型目录结构与配置路径一致。
            $root = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1
            $source = if ($root) { $root.FullName } else { $extractDir }

            if (Test-Path -LiteralPath $modelDir) {
                Remove-Item -LiteralPath $modelDir -Recurse -Force
            }
            New-Item -ItemType Directory -Path (Split-Path -Parent $modelDir) -Force | Out-Null
            Copy-Item -LiteralPath $source -Destination $modelDir -Recurse -Force
        }
        finally {
            Remove-Item -LiteralPath $extractDir -Recurse -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
        }
    }

    foreach ($file in $Entry.Files) {
        if (-not (Test-Path -LiteralPath (Join-Path $modelDir $file))) {
            throw "模型文件缺失：$file"
        }
    }
    Write-Detail "已校验 $($Entry.Files.Count) 个必需文件"
    Write-Detail "目录 $(Get-RelativePath -Base $ModRoot -Target $modelDir)"
}

function Update-Config {
    param([pscustomobject]$Entry)

    Write-Step '写入配置'

    $libraryPath = Join-Path (Get-RuntimeDirectory) 'sherpa-onnx-c-api.dll'
    if (-not (Test-Path -LiteralPath $libraryPath)) {
        throw "未找到运行时 $libraryPath，无法写入 libraryPath"
    }

    $configPath = Get-ConfigPath
    if (Test-Path -LiteralPath $configPath) {
        $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    }
    else {
        New-Item -ItemType Directory -Path (Split-Path -Parent $configPath) -Force | Out-Null
        $config = [pscustomobject]@{}
    }

    if (Test-JsonProperty $config 'sttEnabled') { $config.sttEnabled = $true }
    else { $config | Add-Member -NotePropertyName 'sttEnabled' -NotePropertyValue $true }

    # sttModel 整段由安装器重建：字段集合随模型族变化，保留旧值反而会留下
    # 上一次安装的残留路径。StrictMode 下对不存在的属性赋值会抛错，
    # 因此先按空对象构造再整体替换。
    $stt = [pscustomobject]@{
        modelType         = $Entry.ModelType
        libraryPath       = 'stt/runtime/sherpa-onnx-c-api.dll'
        encoderPath       = ''
        decoderPath       = ''
        joinerPath        = ''
        modelPath         = ''
        tokensPath        = ''
        threads           = 4
        partialIntervalMs = 350
    }
    foreach ($field in $Entry.Map.Keys) {
        $stt.$field = "stt/models/$($Entry.Id)/$($Entry.Map[$field])"
    }

    if (Test-JsonProperty $config 'sttModel') { $config.sttModel = $stt }
    else { $config | Add-Member -NotePropertyName 'sttModel' -NotePropertyValue $stt }

    $config | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $configPath -Encoding UTF8
    Write-Detail "已更新 $(Get-RelativePath -Base $ModRoot -Target $configPath)"
    Write-Detail "modelType = $($Entry.ModelType)"
}

function Show-Catalog {
    Write-Host ''
    Write-Host '  BSChat STT 模型安装' -ForegroundColor White
    Write-Host ('  ' + ('-' * 62)) -ForegroundColor DarkGray

    $configPath = Get-ConfigPath
    $current = $null
    if (Test-Path -LiteralPath $configPath) {
        try {
            $parsed = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
            foreach ($property in $parsed.PSObject.Properties) {
                if ($property.Name -ne 'sttModel' -or -not $property.Value) { continue }
                foreach ($inner in $property.Value.PSObject.Properties) {
                    if ($inner.Name -eq 'modelType') { $current = $inner.Value }
                }
            }
        }
        catch { $current = $null }
    }
    if ($current) { Write-Host "  当前配置 modelType: $current" -ForegroundColor DarkGray }
    else { Write-Host '  当前状态: 未配置模型' -ForegroundColor DarkGray }

    for ($i = 0; $i -lt $Catalog.Count; $i++) {
        $entry = $Catalog[$i]
        Write-Host ''
        Write-Host ("  [{0}] {1}" -f ($i + 1), $entry.Name) -ForegroundColor White
        Write-Host ("      语种 {0} · 体积 {1} · RTF {2}" -f $entry.Languages, $entry.SizeText, $entry.Rtf) -ForegroundColor DarkGray
        Write-Host ("      {0}" -f $entry.Note) -ForegroundColor DarkGray
    }

    Write-Host ''
    Write-Host '  [0] 取消' -ForegroundColor DarkGray
}

function Select-Model {
    while ($true) {
        Show-Catalog
        Write-Host ''
        $choice = Read-Host '  请选择模型编号'
        if ($choice -eq '0') { return $null }
        $index = 0
        if ([int]::TryParse($choice, [ref]$index) -and $index -ge 1 -and $index -le $Catalog.Count) {
            return $Catalog[$index - 1]
        }
        Write-Host '  输入无效，请重新输入' -ForegroundColor Yellow
    }
}

# ---------------------------------------------------------------- 主流程

if (-not $ModRoot) { $ModRoot = Split-Path -Parent $PSScriptRoot }
$ModRoot = [System.IO.Path]::GetFullPath($ModRoot)
if (-not (Test-Path -LiteralPath $ModRoot)) { throw "模组根目录不存在：$ModRoot" }

Write-Host ''
Write-Host "模组根目录: $ModRoot" -ForegroundColor DarkGray

# .cmd 传入的模型 ID 以位置参数形式到达（-Model）。若未提供则走交互菜单。
if ($Model -and $Model.Count -gt 0) {
    $selected = @()
    foreach ($id in $Model) {
        $entry = $Catalog | Where-Object { $_.Id -eq $id } | Select-Object -First 1
        if (-not $entry) { throw "未知的模型 ID：$id" }
        $selected += $entry
    }
}
else {
    $choice = Select-Model
    if (-not $choice) {
        Write-Host ''
        Write-Host '已取消。' -ForegroundColor Yellow
        exit 0
    }
    $selected = @($choice)
}

Install-Runtime
foreach ($entry in $selected) {
    Install-Model -Entry $entry
    Update-Config -Entry $entry
}

Write-Host ''
Write-Host '安装完成。' -ForegroundColor Green
Write-Host '请重启服务端使配置生效，并确认客户端已开启字幕。' -ForegroundColor Green
Write-Host ''
### POWERSHELL_PAYLOAD_END ###
