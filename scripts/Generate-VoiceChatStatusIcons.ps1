#Requires -Version 5.1
<#
.SYNOPSIS
    生成 BSChat 语音聊天 HUD 的状态图标与资源包图标。

.DESCRIPTION
    使用 .NET System.Drawing 以纯几何图形绘制 64x64、带透明通道的 PNG，
    不依赖任何第三方素材（尤其不使用 lib/simple-voice-chat 下的图片，其 license 为
    "All Rights Reserved"，不可用于本项目）。

    输出：
      resource_pack/textures/ui/voicechat/status_idle.png      灰色话筒轮廓
      resource_pack/textures/ui/voicechat/status_speaking.png  亮绿实心话筒
      resource_pack/textures/ui/voicechat/status_muted.png     灰色话筒 + 红色斜杠
      resource_pack/textures/ui/voicechat/status_playing.png   喇叭 + 声波弧线
      resource_pack/textures/ui/voicechat/status_silent.png    喇叭（无声波）
      resource_pack/pack_icon.png                              资源包图标

    脚本可重复执行，会覆盖同名文件。默认输出到脚本所在仓库根目录，
    也可用 -RepoRoot 指定。
#>
[CmdletBinding()]
param(
    [string]$RepoRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

if (-not $RepoRoot) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
else {
    $RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
}

$iconDir  = Join-Path $RepoRoot 'resource_pack/textures/ui/voicechat'
$packRoot = Join-Path $RepoRoot 'resource_pack'
New-Item -ItemType Directory -Force -Path $iconDir | Out-Null

$size = 64

# 调色板：与 HUD 文字（浅蓝白）协调，图标本身不带文字。
$gray   = [System.Drawing.Color]::FromArgb(255, 176, 184, 196) # B0B8C4
$green  = [System.Drawing.Color]::FromArgb(255, 107, 224, 107) # 6BE06B
$red    = [System.Drawing.Color]::FromArgb(255, 224, 75, 75)   # E04B4B
$bright = [System.Drawing.Color]::FromArgb(255, 242, 246, 255) # F2F6FF
$slate  = [System.Drawing.Color]::FromArgb(255, 30, 36, 48)    # 1E2430

function New-RoundedRectPath {
    param([float]$X, [float]$Y, [float]$W, [float]$H, [float]$R)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d    = $R * 2
    $path.AddArc($X, $Y, $d, $d, 180, 90)
    $path.AddArc($X + $W - $d, $Y, $d, $d, 270, 90)
    $path.AddArc($X + $W - $d, $Y + $H - $d, $d, $d, 0, 90)
    $path.AddArc($X, $Y + $H - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-IconCanvas {
    $bitmap   = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.Clear([System.Drawing.Color]::Transparent)
    return @{ Bitmap = $bitmap; Graphics = $graphics }
}

function Save-IconCanvas {
    param([hashtable]$Canvas, [string]$Path)
    $Canvas.Graphics.Dispose()
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Force }
    $Canvas.Bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $Canvas.Bitmap.Dispose()
}

# 话筒：胶囊体 + 下方 U 形托架 + 立柱 + 底座。
function Draw-MicShape {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Color]$Color,
        [switch]$Filled
    )
    $pen           = New-Object System.Drawing.Pen($Color, 5.0)
    $pen.StartCap  = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap    = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin  = [System.Drawing.Drawing2D.LineJoin]::Round

    $capsule = New-RoundedRectPath -X 24 -Y 8 -W 16 -H 32 -R 8
    if ($Filled) {
        $brush = New-Object System.Drawing.SolidBrush($Color)
        $Graphics.FillPath($brush, $capsule)
        $brush.Dispose()
    }
    else {
        $Graphics.DrawPath($pen, $capsule)
    }
    $capsule.Dispose()

    $Graphics.DrawArc($pen, 18, 20, 28, 28, 0, 180) # 托架 U 形
    $Graphics.DrawLine($pen, 32, 48, 32, 55)        # 立柱
    $Graphics.DrawLine($pen, 23, 55, 41, 55)        # 底座
    $pen.Dispose()
}

# 斜杠：用于静音状态。
function Draw-Slash {
    param([System.Drawing.Graphics]$Graphics, [System.Drawing.Color]$Color)
    $pen          = New-Object System.Drawing.Pen($Color, 6.0)
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
    $Graphics.DrawLine($pen, 12, 52, 52, 12)
    $pen.Dispose()
}

# 喇叭：箱体 + 锥形，可选右侧声波弧线。
function Draw-SpeakerShape {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Color]$Color,
        [switch]$Waves
    )
    $points = [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(12, 24),
        [System.Drawing.PointF]::new(23, 24),
        [System.Drawing.PointF]::new(37, 12),
        [System.Drawing.PointF]::new(37, 52),
        [System.Drawing.PointF]::new(23, 40),
        [System.Drawing.PointF]::new(12, 40)
    )
    $brush = New-Object System.Drawing.SolidBrush($Color)
    $Graphics.FillPolygon($brush, $points)
    $brush.Dispose()

    if ($Waves) {
        $pen          = New-Object System.Drawing.Pen($Color, 5.0)
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
        $Graphics.DrawArc($pen, 27, 22, 20, 20, -45, 90)
        $Graphics.DrawArc($pen, 21, 16, 32, 32, -45, 90)
        $pen.Dispose()
    }
}

$written = New-Object System.Collections.Generic.List[string]

# status_idle：灰色话筒轮廓（未在会话）
$canvas = New-IconCanvas
Draw-MicShape -Graphics $canvas.Graphics -Color $gray
$path = Join-Path $iconDir 'status_idle.png'
Save-IconCanvas -Canvas $canvas -Path $path
$written.Add($path)

# status_speaking：亮绿实心话筒
$canvas = New-IconCanvas
Draw-MicShape -Graphics $canvas.Graphics -Color $green -Filled
$path = Join-Path $iconDir 'status_speaking.png'
Save-IconCanvas -Canvas $canvas -Path $path
$written.Add($path)

# status_muted：灰色话筒 + 红色斜杠
$canvas = New-IconCanvas
Draw-MicShape -Graphics $canvas.Graphics -Color $gray
Draw-Slash -Graphics $canvas.Graphics -Color $red
$path = Join-Path $iconDir 'status_muted.png'
Save-IconCanvas -Canvas $canvas -Path $path
$written.Add($path)

# status_playing：喇叭 + 声波弧线（亮色）
$canvas = New-IconCanvas
Draw-SpeakerShape -Graphics $canvas.Graphics -Color $bright -Waves
$path = Join-Path $iconDir 'status_playing.png'
Save-IconCanvas -Canvas $canvas -Path $path
$written.Add($path)

# status_silent：喇叭，无声波（灰色）
$canvas = New-IconCanvas
Draw-SpeakerShape -Graphics $canvas.Graphics -Color $gray
$path = Join-Path $iconDir 'status_silent.png'
Save-IconCanvas -Canvas $canvas -Path $path
$written.Add($path)

# pack_icon：深色圆角底 + 亮色话筒 + 绿色圆点
$canvas = New-IconCanvas
$bgPath = New-RoundedRectPath -X 2 -Y 2 -W 60 -H 60 -R 12
$bgBrush = New-Object System.Drawing.SolidBrush($slate)
$canvas.Graphics.FillPath($bgBrush, $bgPath)
$bgBrush.Dispose()
$bgPath.Dispose()
Draw-MicShape -Graphics $canvas.Graphics -Color $bright
$dot = New-Object System.Drawing.SolidBrush($green)
$canvas.Graphics.FillEllipse($dot, 44, 12, 12, 12)
$dot.Dispose()
$path = Join-Path $packRoot 'pack_icon.png'
Save-IconCanvas -Canvas $canvas -Path $path
$written.Add($path)

foreach ($file in $written) {
    $bitmap = [System.Drawing.Image]::FromFile($file)
    try {
        Write-Host ("{0}  {1}x{2}" -f $file, $bitmap.Width, $bitmap.Height)
    }
    finally {
        $bitmap.Dispose()
    }
}
