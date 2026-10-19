<#
.SYNOPSIS
  Renders the app icon and MSIX tile assets (all scales/target sizes) plus the multi-size .ico.
  The classic 7-Zip icons are 16-48 px bitmaps; Windows 11 needs crisp art up to 256 px, so the
  logo is drawn here from vector primitives in the 7-Zip black/white style.
#>
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$out = $PSScriptRoot
New-Item -ItemType Directory -Force $out | Out-Null

function New-RoundedPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure(); return $p
}

# Draws the logo into a square of side $s at ($ox,$oy).
function Draw-Logo($g, [float]$ox, [float]$oy, [float]$s) {
    $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'; $g.InterpolationMode = 'HighQualityBicubic'
    $m = $s * 0.06
    $rect = New-RoundedPath ($ox + $m) ($oy + $m) ($s - 2 * $m) ($s - 2 * $m) ($s * 0.2)
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF($ox, $oy)), (New-Object System.Drawing.PointF($ox, ($oy + $s))),
        [System.Drawing.Color]::FromArgb(255, 62, 62, 66), [System.Drawing.Color]::FromArgb(255, 18, 18, 20))
    $g.FillPath($brush, $rect)
    if ($s -ge 32) {
        $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(70, 255, 255, 255), [Math]::Max(1, $s / 64))
        $g.DrawPath($pen, $rect)
    }
    # Zipper teeth down the left edge for larger sizes.
    if ($s -ge 48) {
        $tooth = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0, 120, 212))
        $tw = $s * 0.07; $th = $s * 0.045; $x0 = $ox + $s * 0.17
        for ($i = 0; $i -lt 6; $i++) {
            $ty = $oy + $s * 0.22 + $i * $th * 2.1
            $dx = if ($i % 2 -eq 0) { 0 } else { $tw * 0.6 }
            $g.FillRectangle($tooth, $x0 + $dx, $ty, $tw, $th)
        }
    }
    $fontSize = $s * 0.46
    $font = New-Object System.Drawing.Font('Segoe UI', $fontSize, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat; $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $textRect = New-Object System.Drawing.RectangleF(($ox + ($s -ge 48 ? $s * 0.08 : 0)), ($oy - $s * 0.02), $s, $s)
    $g.DrawString('7z', $font, [System.Drawing.Brushes]::White, $textRect, $fmt)
}

function Save-Png([int]$w, [int]$h, [string]$name, [float]$logoScale = 1.0) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::Transparent)
    $side = [Math]::Min($w, $h) * $logoScale
    Draw-Logo $g (($w - $side) / 2) (($h - $side) / 2) $side
    $g.Dispose(); $bmp.Save((Join-Path $out $name), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}

foreach ($t in 16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 256) {
    Save-Png $t $t "Square44x44Logo.targetsize-$t.png"
    Save-Png $t $t "Square44x44Logo.targetsize-${t}_altform-unplated.png"
    Save-Png $t $t "Square44x44Logo.targetsize-${t}_altform-lightunplated.png"
}
foreach ($scale in 100, 125, 150, 200, 400) {
    $f = $scale / 100
    Save-Png ([int](44 * $f)) ([int](44 * $f)) "Square44x44Logo.scale-$scale.png"
    Save-Png ([int](150 * $f)) ([int](150 * $f)) "Square150x150Logo.scale-$scale.png" 0.6
    Save-Png ([int](310 * $f)) ([int](150 * $f)) "Wide310x150Logo.scale-$scale.png" 0.6
    Save-Png ([int](71 * $f)) ([int](71 * $f)) "SmallTile.scale-$scale.png" 0.7
    Save-Png ([int](310 * $f)) ([int](310 * $f)) "LargeTile.scale-$scale.png" 0.5
    Save-Png ([int](50 * $f)) ([int](50 * $f)) "StoreLogo.scale-$scale.png"
    Save-Png ([int](620 * $f)) ([int](300 * $f)) "SplashScreen.scale-$scale.png" 0.5
}
Save-Png 44 44 'Square44x44Logo.png'
Save-Png 50 50 'StoreLogo.png'

# Jump list task glyphs (Segoe Fluent Icons)
function Save-Glyph([string]$glyph, [string]$name) {
    $bmp = New-Object System.Drawing.Bitmap(32, 32); $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.TextRenderingHint = 'AntiAlias'; $g.Clear([System.Drawing.Color]::Transparent)
    $font = New-Object System.Drawing.Font('Segoe Fluent Icons', 22, [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat; $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $g.DrawString($glyph, $font, [System.Drawing.Brushes]::White, (New-Object System.Drawing.RectangleF(0, 0, 32, 32)), $fmt)
    $g.Dispose(); $bmp.Save((Join-Path $out $name), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
Save-Glyph ([string][char]0xF012) 'JumpCompress.png'
Save-Glyph ([string][char]0xE73E) 'JumpHash.png'

# Multi-resolution .ico with PNG-compressed frames.
$sizes = 16, 20, 24, 32, 40, 48, 64, 256
# Render every frame at its exact size.
$frames = foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s); $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::Transparent)
    Draw-Logo $g 0 0 $s; $g.Dispose(); $ms = New-Object IO.MemoryStream; $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); , $ms.ToArray()
}
$icoPath = Join-Path $out '7zip.ico'
$fs = [IO.File]::Create($icoPath); $bw = New-Object IO.BinaryWriter($fs)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $len = $frames[$i].Length
    $bw.Write([byte]($s % 256)); $bw.Write([byte]($s % 256)); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]$len); $bw.Write([uint32]$offset)
    $offset += $len
}
foreach ($f in $frames) { $bw.Write($f) }
$bw.Close()
Write-Host "Assets written to $out" -ForegroundColor Green
