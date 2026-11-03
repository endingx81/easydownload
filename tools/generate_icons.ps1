Add-Type -AssemblyName System.Drawing

function Create-DownloadBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

    $g.Clear([System.Drawing.Color]::Transparent)

    $pad = [Math]::Max(1, [int]($size * 0.06))
    $rect = New-Object System.Drawing.Rectangle($pad, $pad, ($size - 2 * $pad), ($size - 2 * $pad))
    $radius = [int]($size * 0.22)
    
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2
    $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
    $path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
    $path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
    $path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF(0, 0)),
        (New-Object System.Drawing.PointF($size, $size)),
        [System.Drawing.Color]::FromArgb(255, 30, 60, 240),
        [System.Drawing.Color]::FromArgb(255, 0, 190, 255)
    )
    $g.FillPath($brush, $path)

    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(140, 255, 255, 255), [Math]::Max(1.0, $size * 0.02))
    $g.DrawPath($pen, $path)

    $arrowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
    $cx = [float]($size / 2.0)
    $cy = [float]($size / 2.0)
    $scale = [float]($size / 64.0)

    $stemW = [float](6.0 * $scale)
    $stemTop = [float](14.0 * $scale)
    $stemBottom = [float](34.0 * $scale)
    $headW = [float](16.0 * $scale)
    $headTip = [float](44.0 * $scale)

    $arrowPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $points = @(
        (New-Object System.Drawing.PointF(($cx - $stemW), $stemTop)),
        (New-Object System.Drawing.PointF(($cx + $stemW), $stemTop)),
        (New-Object System.Drawing.PointF(($cx + $stemW), $stemBottom)),
        (New-Object System.Drawing.PointF(($cx + $headW), $stemBottom)),
        (New-Object System.Drawing.PointF($cx, $headTip)),
        (New-Object System.Drawing.PointF(($cx - $headW), $stemBottom)),
        (New-Object System.Drawing.PointF(($cx - $stemW), $stemBottom))
    )
    $arrowPath.AddPolygon($points)
    $g.FillPath($arrowBrush, $arrowPath)

    $barW = [float](18.0 * $scale)
    $barH = [float](4.0 * $scale)
    $barY = [float](48.0 * $scale)
    $barPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $barPath.AddRectangle((New-Object System.Drawing.RectangleF(($cx - $barW), $barY, ($barW * 2.0), $barH)))
    $g.FillPath($arrowBrush, $barPath)

    $g.Dispose()
    return $bmp
}

# Export extension PNGs
@(16, 32, 48, 128) | ForEach-Object {
    $bmp = Create-DownloadBitmap $_
    $bmp.Save("extension\icons\icon$_.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# Multi-frame ICO generation
$sizes = @(256, 128, 64, 48, 32, 24, 16)
$pngStreams = @()
foreach ($s in $sizes) {
    $bmp = Create-DownloadBitmap $s
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $pngStreams += $ms
}

$fs = [System.IO.File]::Create("res\easydownload.ico")
$bw = New-Object System.IO.BinaryWriter($fs)

$bw.Write([uint16]0)
$bw.Write([uint16]1)
$bw.Write([uint16]$sizes.Count)

$offset = 6 + ($sizes.Count * 16)
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $data = $pngStreams[$i].ToArray()
    $w = if ($s -eq 256) { 0 } else { [byte]$s }
    $h = if ($s -eq 256) { 0 } else { [byte]$s }
    $bw.Write([byte]$w)
    $bw.Write([byte]$h)
    $bw.Write([byte]0)
    $bw.Write([byte]0)
    $bw.Write([uint16]1)
    $bw.Write([uint16]32)
    $bw.Write([uint32]$data.Length)
    $bw.Write([uint32]$offset)
    $offset += $data.Length
}

for ($i = 0; $i -lt $sizes.Count; $i++) {
    $data = $pngStreams[$i].ToArray()
    $bw.Write($data)
    $pngStreams[$i].Dispose()
}

$bw.Close()
$fs.Close()
Write-Host "Icons generated successfully!"

