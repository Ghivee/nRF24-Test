# nRF24-Test

A self-test sketch for an nRF24L01+ module wired to an ESP-12E/NodeMCU (ESP8266). It bit-bangs SPI directly, with no library dependency, so it uploads and runs without installing anything first. It auto-detects which of six common wiring layouts is connected, then writes and reads the CONFIG register and checks the chip's default STATUS value to confirm the module is alive.

[Bahasa Indonesia](README.id.md)

## Files

| Path | What it holds |
| --- | --- |
| `nrftest.ino` | The self-test sketch |
| `flash.ps1` | PowerShell helper: compiles, uploads and opens a serial monitor, with automatic COM port detection |
| `hasil-tes.txt` | A recorded test run |
| `tools/arduino-cli.exe` | Arduino CLI used by `flash.ps1` (not tracked in git; download it yourself) |

## Running it

```powershell
.\flash.ps1                 # auto-detect the COM port
.\flash.ps1 -Port COM3      # pick it manually
.\flash.ps1 -Port COM3 -NoMonitor
```
