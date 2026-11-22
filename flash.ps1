<#
    flash.ps1 - compile + upload + serial monitor untuk E:\02_Projek\nrftest

    Cara pakai (PowerShell, dari folder ini):
        .\flash.ps1                 -> cari port COM otomatis
        .\flash.ps1 -Port COM3      -> pilih port manual
        .\flash.ps1 -Port COM3 -NoMonitor

    Tekan Ctrl+C untuk berhenti dari serial monitor.
#>
[CmdletBinding()]
param(
    [string]$Port,
    [switch]$NoMonitor,
    [int]$Baud = 115200
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$cli  = Join-Path $root 'tools\arduino-cli.exe'
if (-not (Test-Path $cli)) {
    $cmd = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if (-not $cmd) { throw "arduino-cli tidak ditemukan. Jalankan: winget install ArduinoSA.CLI" }
    $cli = $cmd.Source
}
$fqbn = 'esp8266:esp8266:nodemcuv2'   # NodeMCU 1.0 / ESP-12E

function Get-SerialInventory {
    Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.Name -match '\(COM\d+\)' } |
        ForEach-Object {
            $n = $_.Name
            $null = $n -match '\((COM\d+)\)'
            [pscustomobject]@{ Port = $Matches[1]; Name = $n }
        } | Sort-Object Port
}

if (-not $Port) {
    $all = Get-SerialInventory
    Write-Host "== Port serial yang ada ==" -ForegroundColor Cyan
    if ($all) { $all | Format-Table -AutoSize | Out-String | Write-Host }
    else { Write-Host "  (kosong)" -ForegroundColor Yellow }

    $cands = @($all | Where-Object { $_.Name -match 'USB|UART|CH34|CP210|FTDI|Silicon|WCH' })
    if ($cands.Count -eq 1) {
        $Port = $cands[0].Port
        Write-Host "-> Pakai otomatis: $Port  ($($cands[0].Name))" -ForegroundColor Green
    }
    elseif ($cands.Count -gt 1) {
        throw "Ada $($cands.Count) port kandidat. Jalankan ulang: .\flash.ps1 -Port COMx"
    }
    else {
        throw "Board ESP tidak terdeteksi. Colok USB-nya, cek driver CH340/CP210x, lalu ulangi."
    }
}

Write-Host "`n== [1/2] Compile ==" -ForegroundColor Cyan
& $cli compile --fqbn $fqbn --warnings default $root
if ($LASTEXITCODE -ne 0) { throw "Compile gagal." }

Write-Host "`n== [2/2] Upload ke $Port ==" -ForegroundColor Cyan
& $cli upload -p $Port --fqbn $fqbn $root
if ($LASTEXITCODE -ne 0) {
    Write-Host "Upload gagal. Coba cabut kabel CSN (GPIO0) sebentar saat upload, atau tekan tombol FLASH/BOOT." -ForegroundColor Yellow
    throw "Upload gagal."
}

if ($NoMonitor) {
    Write-Host "`nSelesai. Buka Serial Monitor 115200 baud untuk melihat hasil." -ForegroundColor Green
    return
}

Write-Host "`n== Serial Monitor ($Baud baud) - Ctrl+C untuk berhenti ==" -ForegroundColor Cyan
& $cli monitor -p $Port -c "baudrate=$Baud"
