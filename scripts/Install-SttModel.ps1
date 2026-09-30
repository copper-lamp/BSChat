<#
.SYNOPSIS
    BSChat 服务端 STT 模型安装器。

.DESCRIPTION
    交互式选择并安装 sherpa-onnx 在线识别模型：下载、解压、校验、写入 bschat.json。
    模型族与运行时按 sherpa-onnx 在线识别器的支持范围选定
    （transducer / paraformer / zipformer2_ctc），离线模型不在本期范围内。

    脚本不修改服务端进程，配置写入后需重启服务端生效。

.PARAMETER Model
    非交互模式：直接指定模型 ID，跳过菜单。可重复传入以安装多个。

.PARAMETER ModRoot
    模组根目录，默认为本脚本上级目录。

.PARAMETER Force
    即使目标目录已存在也重新下载。

.EXAMPLE
    .\Install-SttModel.ps1
    交互式选择模型。

.EXAMPLE
    .\Install-SttModel.ps1 -Model bilingual-zh-en
    静默安装中英双语模型（供自动化使用）。
#>
[CmdletBinding()]
param(
    [string[]]$Model,
    [string]$ModRoot,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 运行时版本与 docs/stt-model-installer.md 记录保持一致。
$SherpaRuntimeVersion = '1.13.8'

# 模型清单。Files 为安装后必须存在的文件（缺失即判定下载不完整）。
# Map 描述如何把模型目录下的文件映射到 bschat.json 的 sttModel 字段。
$ReleaseBase = 'https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models'

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

# 仅保留 STT 推理需要的 DLL。sherpa-onnx-c-api.dll 与 onnxruntime.dll 必须同目录，
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

function Install-Runtime {
    $runtimeDir = Get-RuntimeDirectory
    Write-Step "准备 sherpa-onnx 运行时 $SherpaRuntimeVersion"

    $existing = @($RuntimeFiles | Where-Object { Test-Path -LiteralPath (Join-Path $runtimeDir $_) })
    if ($existing.Count -eq $RuntimeFiles.Count -and -not $Force) {
        Write-Detail "运行时已就绪，跳过"
        return
    }

    New-Item -ItemType Directory -Path $runtimeDir -Force | Out-Null

    $archive = "sherpa-onnx-v$SherpaRuntimeVersion-win-x64-shared-MD-Release-no-tts-lib.tar.bz2"
    $url = "$ReleaseBase/$archive"
    $temp = Join-Path ([System.IO.Path]::GetTempPath()) $archive

    Write-Detail "下载 $url"
    Invoke-WebRequest -Uri $url -OutFile $temp -UseBasicParsing

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
    if ($complete -and -not $Force) {
        Write-Detail '模型已存在，跳过下载（-Force 可强制重装）'
    }
    else {
        $temp = Join-Path ([System.IO.Path]::GetTempPath()) $archiveName
        Write-Detail "下载 $url"
        Write-Detail "预计体积 $(Format-Size $Entry.ArchiveSize)"

        $job = Start-Job -ScriptBlock {
            param($u, $o)
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -Uri $u -OutFile $o -UseBasicParsing
        } -ArgumentList $url, $temp

        $lastPercent = -1
        while ($job.State -eq 'Running') {
            Start-Sleep -Milliseconds 400
            if (Test-Path -LiteralPath $temp) {
                $current = (Get-Item -LiteralPath $temp).Length
                $percent = [int]($current / $Entry.ArchiveSize * 100)
                if ($percent -ge $lastPercent + 5) {
                    $lastPercent = $percent - ($percent % 5)
                    $bar = '#' * [int]($lastPercent / 2.5)
                    $pad = ' ' * (40 - $bar.Length)
                    Write-Host ("`r    [{0}{1}] {2,3}%  {3}" -f $bar, $pad, $lastPercent, (Format-Size $current)) -NoNewline
                }
            }
        }
        Write-Host ''

        Receive-Job -Job $job -ErrorAction Stop | Out-Null
        Remove-Job -Job $job -Force

        $downloaded = (Get-Item -LiteralPath $temp).Length
        if ($downloaded -ne $Entry.ArchiveSize) {
            Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
            throw "下载大小不符：期望 $($Entry.ArchiveSize) 字节，实际 $downloaded 字节"
        }
        Write-Detail "下载完成 $(Format-Size $downloaded)"

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

    $configPath = Get-ConfigPath
    Write-Step '写入配置'

    $libraryName = 'stt/runtime/sherpa-onnx-c-api.dll'
    $libraryPath = Join-Path (Get-RuntimeDirectory) 'sherpa-onnx-c-api.dll'
    if (-not (Test-Path -LiteralPath $libraryPath)) {
        throw "未找到运行时 $libraryPath，无法写入 libraryPath"
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
        libraryPath       = $libraryName
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
    Write-Host '  ' + ('-' * 62) -ForegroundColor DarkGray

    $current = $null
    if (Test-Path -LiteralPath (Get-ConfigPath)) {
        try {
            $parsed = Get-Content -LiteralPath (Get-ConfigPath) -Raw | ConvertFrom-Json
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

if (-not $ModRoot) {
    $ModRoot = Split-Path -Parent $PSScriptRoot
}
$ModRoot = [System.IO.Path]::GetFullPath($ModRoot)

if (-not (Test-Path -LiteralPath $ModRoot)) {
    throw "模组根目录不存在：$ModRoot"
}

Write-Host ''
Write-Host "模组根目录: $ModRoot" -ForegroundColor DarkGray

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
