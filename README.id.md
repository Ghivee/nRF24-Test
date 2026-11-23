# nRF24-Test

Sketch self-test untuk modul nRF24L01+ yang disambungkan ke ESP-12E/NodeMCU (ESP8266). SPI di-bit-bang langsung tanpa library tambahan, jadi bisa langsung diunggah dan dijalankan tanpa instalasi apa pun dulu. Sketch ini mendeteksi otomatis mana dari enam susunan kabel umum yang terpasang, lalu menulis dan membaca register CONFIG serta memeriksa nilai STATUS bawaan chip untuk memastikan modulnya benar-benar hidup.

[English](README.md)

## Berkas

| Path | Isi |
| --- | --- |
| `nrftest.ino` | Sketch self-test |
| `flash.ps1` | Pembantu PowerShell: compile, upload, dan buka serial monitor, dengan deteksi port COM otomatis |
| `hasil-tes.txt` | Rekaman hasil tes |
| `tools/arduino-cli.exe` | Arduino CLI yang dipakai `flash.ps1` (tidak tersimpan di git, unduh sendiri) |

## Menjalankan

```powershell
.\flash.ps1                 # deteksi port COM otomatis
.\flash.ps1 -Port COM3      # pilih manual
.\flash.ps1 -Port COM3 -NoMonitor
```
