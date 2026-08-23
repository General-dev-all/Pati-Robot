@echo off
chcp 65001 >nul
title Pati - karta yukle
cd /d "%~dp0"

echo.
echo  ============================================================
echo   PATI - FIRMWARE'I KARTA YUKLE
echo  ============================================================
echo.
echo   Karti Type-C kabloyla bilgisayara tak.
echo   COM portu kendiliginden bulunuyor; birden fazla varsa soruyor.
echo.
echo   Yukleme baslamazsa INDIRME MODU: yan dugmeye BASILI TUT,
echo   icerideki yesil isik yanip sonunce birak, sonra tekrar dene.
echo.
echo   Seri ekrandan cikmak icin: Ctrl + ]
echo.
echo  ------------------------------------------------------------
echo.

REM NEDEN POWERSHELL: ESP-IDF'in ortami PLAN.md'de PowerShell ile
REM dogrulandi. Git Bash calismiyor (idf_tools.py MSys'i reddediyor),
REM cmd icin export.bat da var ama HIC denenmedi. Tezgahta yeni bir
REM yol denemek yanlis zaman.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop';" ^
REM PORT BULMA — 23.08.2026'da genisletildi.
REM
REM Onceden yalnizca 'CH343' araniyordu: eski kart bir CH343 USB-UART
REM koprusu tasiyordu. StickS3'te oyle bir kopru YOK — ESP32-S3-PICO'nun
REM kendi USB'si kullaniliyor ve Windows'ta "USB Serial Device" ya da
REM indirme modunda "USB JTAG/serial debug unit" olarak gorunuyor.
REM
REM Eski filtre burada hicbir sey bulamaz ve "KART BULUNAMADI" der —
REM oysa kart takilidir. Simdi once bilinen adlar araniyor, bulunamazsa
REM butun COM portlari aday sayiliyor, birden fazlaysa soruluyor.
  "$hepsi = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' });" ^
  "$aday = @($hepsi | Where-Object { $_.Name -match 'USB Serial|USB JTAG|CH343|CH910|CP210|Silicon Labs' });" ^
  "if ($aday.Count -eq 0) { $aday = $hepsi };" ^
  "if ($aday.Count -eq 0) {" ^
  "  Write-Host '';" ^
  "  Write-Host '  !! KART BULUNAMADI.' -ForegroundColor Red;" ^
  "  Write-Host '';" ^
  "  Write-Host '  Sirayla bak:';" ^
  "  Write-Host '    1. Kablo takili mi?';" ^
  "  Write-Host '    2. Kablo VERI kablosu mu? Bazi kablolar sadece sarj eder,';" ^
  "  Write-Host '       takinca kart isik yakar ama bilgisayar gormez.';" ^
  "  Write-Host '    3. INDIRME MODU: yan dugmeye BASILI TUT, icerideki';" ^
  "  Write-Host '       yesil isik yanip sonunce birak.';" ^
  "  Write-Host '    4. Aygit Yoneticisi > Baglanti Noktalari (COM ve LPT)';" ^
  "  Write-Host '';" ^
  "  Read-Host '  Kapatmak icin Enter'; exit 1 }" ^
  "if ($aday.Count -gt 1) {" ^
  "  Write-Host '';" ^
  "  Write-Host '  Birden fazla port var:' -ForegroundColor Yellow;" ^
  "  for ($i=0; $i -lt $aday.Count; $i++) { Write-Host ('   ' + ($i+1) + ') ' + $aday[$i].Name) };" ^
  "  Write-Host '';" ^
  "  $sec = Read-Host '  Hangisi? (numara)';" ^
  "  $n = 0; if (-not [int]::TryParse($sec, [ref]$n) -or $n -lt 1 -or $n -gt $aday.Count) { Write-Host '  Gecersiz secim'; Read-Host; exit 1 };" ^
  "  $p = $aday[$n-1] } else { $p = $aday[0] };" ^
  "if ($p.Name -notmatch '\((COM\d+)\)') { Write-Host '  COM numarasi okunamadi'; Read-Host; exit 1 }" ^
  "$port = $Matches[1];" ^
  "Write-Host ('  Kart bulundu: ' + $p.Name) -ForegroundColor Green;" ^
  "Write-Host '';" ^
  "if (-not $env:IDF_PATH) { $env:IDF_PATH = Join-Path $env:USERPROFILE 'esp\esp-idf' };" ^
  "if (-not (Test-Path (Join-Path $env:IDF_PATH 'export.ps1'))) {" ^
  "  Write-Host '';" ^
  "  Write-Host ('  !! ESP-IDF bulunamadi: ' + $env:IDF_PATH) -ForegroundColor Red;" ^
  "  Write-Host '  IDF_PATH ortam degiskenini kur ya da ESP-IDF''yi';" ^
  "  Write-Host '  %%USERPROFILE%%\esp\esp-idf altina kur.';" ^
  "  Read-Host '  Kapatmak icin Enter'; exit 1 }" ^
  "$pyCalisiyor = $false;" ^
  "try { if ((& python -c 'print(7*6)' 2>$null) -eq '42') { $pyCalisiyor = $true } } catch {}" ^
  "if (-not $pyCalisiyor) {" ^
  "  $uv = Join-Path $env:APPDATA 'uv\python';" ^
  "  if (Test-Path $uv) {" ^
  "    $py = Get-ChildItem $uv -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'python.exe') } | Select-Object -First 1;" ^
  "    if ($py) { $env:PATH = $py.FullName + ';' + $env:PATH } } }" ^
  ". \"$env:IDF_PATH\export.ps1\" | Out-Null;" ^
  "idf.py -p $port flash monitor"

echo.
echo  ============================================================
pause
