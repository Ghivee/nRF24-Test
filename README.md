# nRF24-Test

A minimal self-test sketch for an nRF24L01+ radio module wired to an ESP-12E/NodeMCU (ESP8266) board, written to answer one question as quickly as possible: is this specific module actually working? It bit-bangs SPI by hand instead of depending on a library, so it compiles and runs with nothing installed beyond the Arduino ESP8266 board core itself, which matters when you are debugging a module you suspect might be dead or miswired and do not want a missing dependency to be a second variable. It automatically tries six common wiring layouts for the SPI pins until one responds, then writes and reads back the module's CONFIG register and checks its default STATUS value against what a genuine nRF24L01+ chip should report.

[Baca dalam Bahasa Indonesia di bawah](#bahasa-indonesia)

## Files

| Path | What it holds |
| --- | --- |
| `nrftest.ino` | The self-test sketch itself |
| `flash.ps1` | A PowerShell helper that compiles, uploads and opens a serial monitor in one step, with automatic COM port detection |
| `hasil-tes.txt` | A recorded output from an actual test run, kept as a reference for what success looks like |
| `tools/arduino-cli.exe` | The Arduino CLI binary `flash.ps1` calls; not tracked in git (listed in `.gitignore`), download it yourself from the Arduino CLI releases page |

## Running it

```powershell
.\flash.ps1                 # auto-detect the COM port
.\flash.ps1 -Port COM3      # pick the port manually
.\flash.ps1 -Port COM3 -NoMonitor
```

---

## Bahasa Indonesia

Sketch self-test minimal untuk modul radio nRF24L01+ yang disambungkan ke board ESP-12E/NodeMCU (ESP8266), ditulis untuk menjawab satu pertanyaan secepat mungkin: apakah modul ini benar-benar berfungsi? SPI di-bit-bang sendiri secara manual, bukan mengandalkan library, sehingga sketch ini bisa di-compile dan dijalankan tanpa apa pun terpasang selain board core Arduino ESP8266 itu sendiri, yang penting saat sedang men-debug modul yang dicurigai mati atau salah kabel dan tidak ingin dependency yang hilang menjadi variabel kedua. Sketch ini otomatis mencoba enam susunan kabel SPI yang umum sampai salah satunya merespons, lalu menulis dan membaca ulang register CONFIG modulnya serta memeriksa nilai STATUS bawaan terhadap yang seharusnya dilaporkan chip nRF24L01+ asli.

### Berkas

| Path | Isi |
| --- | --- |
| `nrftest.ino` | Sketch self-test itu sendiri |
| `flash.ps1` | Pembantu PowerShell yang compile, upload, dan membuka serial monitor dalam satu langkah, dengan deteksi port COM otomatis |
| `hasil-tes.txt` | Rekaman keluaran dari satu kali pengujian sungguhan, disimpan sebagai referensi seperti apa hasil yang berhasil |
| `tools/arduino-cli.exe` | Binari Arduino CLI yang dipanggil `flash.ps1`; tidak tersimpan di git (tercantum di `.gitignore`), unduh sendiri dari halaman release Arduino CLI |

### Menjalankan

```powershell
.\flash.ps1                 # deteksi port COM otomatis
.\flash.ps1 -Port COM3      # pilih port manual
.\flash.ps1 -Port COM3 -NoMonitor
```
