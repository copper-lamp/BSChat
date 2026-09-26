[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory = $true)]
    [string]$BdsRoot,

    [Parameter(Mandatory = $true)]
    [string]$ClientRoot,

    [string]$ServerArtifact = (Join-Path $PSScriptRoot '..\artifacts\server\voicechat'),
    [string]$ClientArtifact = (Join-Path $PSScriptRoot '..\artifacts\client\voicechat'),
    [string]$BdsExecutable = 'bedrock_server.exe',
    [string]$ClientCommand,
    [int]$StartupTimeoutSeconds = 45,
    [int]$TestDurationSeconds = 30,
    [switch]$Launch,
    [switch]$KeepProcesses
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-ExistingPath([string]$Path, [string]$Description) {
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue
    if ($null -eq $resolved) { throw "$Description not found: $Path" }
    return $resolved.Path
}

function Find-FirstExisting([string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    return $null
}

function Copy-ModArtifact([string]$Artifact, [string]$Destination, [string]$Side) {
    $artifactRoot = Resolve-ExistingPath $Artifact "$Side artifact"
    $dll = Join-Path $artifactRoot 'voicechat.dll'
    $manifest = Join-Path $artifactRoot 'manifest.json'
    if (!(Test-Path -LiteralPath $dll)) { throw "$Side artifact is missing voicechat.dll: $artifactRoot" }
    if (!(Test-Path -LiteralPath $manifest)) { throw "$Side artifact is missing manifest.json: $artifactRoot" }
    # 模组目录是「整目录」交付物：dll/manifest 之外还有 lang（i18n）、panels（设置/管理面板）
    # 和 icons（HUD 状态图标）。只拷 dll+manifest 会让这些目录变空，面板不可用、文案回落键名、
    # HUD 图标缺失。这里按目录树整体拷贝，缺哪个就在日志里点名。
    foreach ($required in @('lang', 'panels', 'icons')) {
        if (!(Test-Path -LiteralPath (Join-Path $artifactRoot $required))) {
            throw "$Side artifact is missing the '$required' directory: $artifactRoot"
        }
    }
    if (!(Test-Path -LiteralPath $Destination)) { New-Item -ItemType Directory -Force -Path $Destination | Out-Null }
    # 不整目录删除：安装目录里的 config/（客户端配置与日志）必须保留。
    Copy-Item -Path (Join-Path $artifactRoot '*') -Destination $Destination -Recurse -Force
}

function New-Backup([string]$Path) {
    if (!(Test-Path -LiteralPath $Path)) { return $null }
    $backup = "$Path.smoke-backup-$(Get-Date -Format yyyyMMdd-HHmmss)"
    Move-Item -LiteralPath $Path -Destination $backup
    return $backup
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bdsRootResolved = Resolve-ExistingPath $BdsRoot 'BDS root'
$clientRootResolved = Resolve-ExistingPath $ClientRoot 'client root'
$serverInstall = Join-Path $bdsRootResolved 'plugins\voicechat'
$clientInstall = Join-Path $clientRootResolved 'mods\voicechat'
$bdsExe = Find-FirstExisting @(
    (Join-Path $bdsRootResolved $BdsExecutable),
    (Join-Path $bdsRootResolved 'bedrock_server_mod.exe')
)
if ($Launch -and $null -eq $bdsExe) { throw "BDS executable not found below $bdsRootResolved" }

$runRoot = Join-Path $repo ('smoke-results\' + (Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$transcript = Join-Path $runRoot 'smoke.log'
Start-Transcript -LiteralPath $transcript | Out-Null
$serverBackup = $null
$clientBackup = $null
$bdsProcess = $null
$clientProcess = $null
$result = [ordered]@{
    startedAt = (Get-Date).ToString('o')
    bdsRoot = $bdsRootResolved
    clientRoot = $clientRootResolved
    serverArtifact = (Resolve-ExistingPath $ServerArtifact 'server artifact')
    clientArtifact = (Resolve-ExistingPath $ClientArtifact 'client artifact')
    checks = [ordered]@{}
    passed = $false
}

try {
    Write-Host "[1/6] Validating artifacts and destinations"
    $serverBackup = New-Backup $serverInstall
    $clientBackup = New-Backup $clientInstall
    Copy-ModArtifact $ServerArtifact $serverInstall 'server'
    Copy-ModArtifact $ClientArtifact $clientInstall 'client'
    $result.checks.artifactsInstalled = $true

    $result.checks.serverManifest = (Get-Content (Join-Path $serverInstall 'manifest.json') -Raw | ConvertFrom-Json).platform -eq 'server'
    $result.checks.clientManifest = (Get-Content (Join-Path $clientInstall 'manifest.json') -Raw | ConvertFrom-Json).platform -eq 'client'
    if (!$result.checks.serverManifest -or !$result.checks.clientManifest) {
        throw 'Artifact manifest platform mismatch; build server and client artifacts separately.'
    }

    Write-Host "[2/6] Installation verified"
    $result.checks.serverDll = Test-Path (Join-Path $serverInstall 'voicechat.dll')
    $result.checks.clientDll = Test-Path (Join-Path $clientInstall 'voicechat.dll')

    if ($Launch) {
        Write-Host "[3/6] Starting BDS"
        $bdsProcess = Start-Process -FilePath $bdsExe -WorkingDirectory $bdsRootResolved -PassThru -RedirectStandardOutput (Join-Path $runRoot 'bds.stdout.log') -RedirectStandardError (Join-Path $runRoot 'bds.stderr.log')
        $deadline = (Get-Date).AddSeconds($StartupTimeoutSeconds)
        do {
            Start-Sleep -Milliseconds 500
            $alive = !$bdsProcess.HasExited
            $stdout = if (Test-Path (Join-Path $runRoot 'bds.stdout.log')) { Get-Content (Join-Path $runRoot 'bds.stdout.log') -Raw } else { '' }
            $ready = $stdout -match '(?i)(server started|IPv4|listening|running)'
        } while ($alive -and !$ready -and (Get-Date) -lt $deadline)
        $result.checks.bdsStarted = $alive
        $result.checks.bdsReadyLog = $ready
        if (!$alive) { throw 'BDS exited before readiness was observed.' }

        if ($ClientCommand) {
            Write-Host "[4/6] Starting client command"
            $clientProcess = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-Command',$ClientCommand) -WorkingDirectory $clientRootResolved -PassThru -RedirectStandardOutput (Join-Path $runRoot 'client.stdout.log') -RedirectStandardError (Join-Path $runRoot 'client.stderr.log')
            $result.checks.clientStarted = $true
        } else {
            Write-Host "[4/6] Client launch command not supplied; waiting for manual client connection"
            $result.checks.clientStarted = $null
        }

        Write-Host "[5/6] Collecting logs for $TestDurationSeconds seconds"
        Start-Sleep -Seconds $TestDurationSeconds
        $result.checks.bdsProcessAlive = !$bdsProcess.HasExited
        if ($clientProcess) { $result.checks.clientProcessAlive = !$clientProcess.HasExited }
    } else {
        Write-Host "[3-5/6] Deployment-only mode; no processes launched"
        $result.checks.bdsStarted = $null
        $result.checks.clientStarted = $null
        $result.checks.manualConnectionRequired = $true
    }

    $result.passed = [bool]($result.checks.artifactsInstalled -and $result.checks.serverManifest -and $result.checks.clientManifest -and $result.checks.serverDll -and $result.checks.clientDll)
    Write-Host "[6/6] Smoke preflight passed: $($result.passed)"
}
catch {
    $result.error = $_.Exception.Message
    Write-Error $_
}
finally {
    $result.finishedAt = (Get-Date).ToString('o')
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
    if (!$KeepProcesses) {
        if ($clientProcess -and !$clientProcess.HasExited) { Stop-Process -Id $clientProcess.Id -Force }
        if ($bdsProcess -and !$bdsProcess.HasExited) { Stop-Process -Id $bdsProcess.Id -Force }
    }
    Stop-Transcript | Out-Null
}

Write-Host "Results: $runRoot"
if (!$result.passed) { exit 1 }
