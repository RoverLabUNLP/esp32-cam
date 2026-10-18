# ============================================================================
# Re-empaqueta tools/index_ov3660.html (gzip) dentro de camera_index.h
# Uso (desde la carpeta del proyecto):
#   powershell -ExecutionPolicy Bypass -File tools\build_index.ps1
# ============================================================================
$root   = Split-Path $PSScriptRoot -Parent
$html   = Join-Path $PSScriptRoot 'index_ov3660.html'
$header = Join-Path $root 'camera_index.h'

# --- Comprimir HTML en gzip ---
$raw = [System.IO.File]::ReadAllBytes($html)
$ms  = New-Object System.IO.MemoryStream
$gz  = New-Object System.IO.Compression.GZipStream($ms, [System.IO.Compression.CompressionLevel]::Optimal)
$gz.Write($raw, 0, $raw.Length)
$gz.Close()
$bytes = $ms.ToArray()

# --- Generar bloque C ---
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("//File: index_ov3660.html.gz, Size: $($bytes.Length)")
[void]$sb.AppendLine("#define index_ov3660_html_gz_len $($bytes.Length)")
[void]$sb.AppendLine("const uint8_t index_ov3660_html_gz[] = {")
for ($i = 0; $i -lt $bytes.Length; $i += 16) {
  $end   = [Math]::Min($i + 16, $bytes.Length) - 1
  $chunk = ($bytes[$i..$end] | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', '
  if ($end -lt $bytes.Length - 1) { $chunk += ',' }
  [void]$sb.AppendLine(" $chunk")
}
[void]$sb.Append("};")

# --- Reemplazar bloque existente en camera_index.h ---
$text    = [System.IO.File]::ReadAllText($header)
$pattern = '(?s)//File: index_ov3660\.html\.gz.*?const uint8_t index_ov3660_html_gz\[\] = \{.*?\};'
if ($text -notmatch $pattern) { throw "No se encontro el bloque index_ov3660 en camera_index.h" }
$block = $sb.ToString() -replace "`r?`n", "`r`n"
$text  = [regex]::Replace($text, $pattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $block }, 1)
[System.IO.File]::WriteAllText($header, $text, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "OK: HTML $($raw.Length) bytes -> gzip $($bytes.Length) bytes en camera_index.h"
