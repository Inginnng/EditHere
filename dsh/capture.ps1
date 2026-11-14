param(
  [Parameter(Mandatory = $true)][string]$Path,
  [switch]$All,
  [string]$WindowTitle = '',
  [switch]$Cursor
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class EditHereCapture {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
  [DllImport("user32.dll", SetLastError = true)] public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
  [DllImport("user32.dll", SetLastError = true)] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
}
"@

[void][EditHereCapture]::SetProcessDPIAware()

$bounds = $null
if ($WindowTitle -ne '') {
  $handle = [EditHereCapture]::FindWindow($null, $WindowTitle)
  if ($handle -eq [IntPtr]::Zero) { throw "Window not found: $WindowTitle" }
  [void][EditHereCapture]::SetForegroundWindow($handle)
  Start-Sleep -Milliseconds 250
  $rect = New-Object EditHereCapture+RECT
  if (-not [EditHereCapture]::GetWindowRect($handle, [ref]$rect)) { throw "Cannot read the bounds of: $WindowTitle" }
  $bounds = New-Object System.Drawing.Rectangle($rect.Left, $rect.Top, ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
} elseif ($All) {
  $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
} else {
  $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
}

if ($bounds.Width -le 0 -or $bounds.Height -le 0) { throw 'The capture area is empty.' }

$bitmap = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
  $graphics.CopyFromScreen($bounds.Left, $bounds.Top, 0, 0, $bounds.Size)
} finally {
  $graphics.Dispose()
}

if ($Cursor) {
  $cursorSize = [System.Windows.Forms.SystemInformation]::CursorSize
  $cursor = New-Object System.Drawing.Rectangle(
    ([System.Windows.Forms.Cursor]::Position.X - $bounds.Left - [int]($cursorSize.Width / 2)),
    ([System.Windows.Forms.Cursor]::Position.Y - $bounds.Top - [int]($cursorSize.Height / 2)),
    $cursorSize.Width, $cursorSize.Height)
  $cursorBitmap = New-Object System.Drawing.Bitmap($cursorSize.Width, $cursorSize.Height)
  $cursorGraphics = [System.Drawing.Graphics]::FromImage($cursorBitmap)
  $cursorGraphics.CopyFromScreen($cursor.X, $cursor.Y, 0, 0, $cursorSize)
  $cursorGraphics.Dispose()
  $imageGraphics = [System.Drawing.Graphics]::FromImage($bitmap)
  $imageGraphics.DrawImage($cursorBitmap, $cursor)
  $imageGraphics.Dispose()
  $cursorBitmap.Dispose()
}

$directory = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($Path))
if ($directory -and -not (Test-Path -LiteralPath $directory)) { New-Item -ItemType Directory -Path $directory -Force | Out-Null }
$bitmap.Save([System.IO.Path]::GetFullPath($Path), [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Output ([System.IO.Path]::GetFullPath($Path))
