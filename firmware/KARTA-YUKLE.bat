@echo off
chcp 65001 >nul
title Pati - karta yukle
cd /d "%~dp0"

echo.
echo  ============================================================
echo   PATI - FIRMWARE'I KARTA YUKLE
echo  ============================================================
echo.
echo   Karti SAGDAKI (UART) Type-C porttan tak.
echo   COM portu kendiliginden bulunuyor, elle yazman gerekmiyor.
echo.
echo   Yukleme baslamazsa: BOOT basili tut - RESET'e bas ve birak
echo   - BOOT'u birak, sonra bu dosyaya tekrar cift tikla.
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
  "$p = Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'CH343' -and $_.Name -match '\(COM\d+\)' } | Select-Object -First 1;" ^
  "if (-not $p) {" ^
  "  Write-Host '';" ^
  "  Write-Host '  !! KART BULUNAMADI.' -ForegroundColor Red;" ^
  "  Write-Host '';" ^
  "  Write-Host '  Sirayla bak:';" ^
  "  Write-Host '    1. Kart SAGDAKI (UART) Type-C porta takili mi?';" ^
  "  Write-Host '    2. Kablo VERI kablosu mu? Bazi kablolar sadece sarj eder,';" ^
  "  Write-Host '       takinca kart isik yakar ama bilgisayar gormez.';" ^
  "  Write-Host '    3. Aygit Yoneticisi > Baglanti Noktalari (COM ve LPT)';" ^
  "  Write-Host '';" ^
  "  Read-Host '  Kapatmak icin Enter'; exit 1 }" ^
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
