@echo off
chcp 65001 >nul
title Pati - yeni surum hazirla
cd /d "%~dp0"

echo.
echo  ============================================================
echo   PATI - YENI SURUM HAZIRLA
echo  ============================================================
echo.
echo   Bu dosya SADECE HAZIRLIK yapiyor:
echo     1. surum.txt'yi okur
echo     2. firmware'i derler
echo     3. surum.json'i gunceller
echo.
echo   HICBIR SEY YAYIMLAMIYOR. Push da, Release de senin elinde;
echo   sonunda ne yapman gerektigini yaziyor.
echo.
echo   ONCE surum.txt'yi YUKSELT. Yukseltmezsen anne panelde
echo   "guncelleme var" GORMEZ - karsilastirma o sayiya bakiyor.
echo.
echo  ------------------------------------------------------------
echo.

REM NEDEN POWERSHELL: KARTA-YUKLE.bat ile ayni gerekce - ESP-IDF'in
REM ortami PowerShell ile dogrulandi, Git Bash calismiyor.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop';" ^
  "$kok = Split-Path -Parent (Get-Location);" ^
  "$surum = (Get-Content 'surum.txt' -Raw).Trim();" ^
  "if ($surum -notmatch '^\d+\.\d+\.\d+$') {" ^
  "  Write-Host '';" ^
  "  Write-Host ('  !! surum.txt bicimi yanlis: ' + $surum) -ForegroundColor Red;" ^
  "  Write-Host '  Bekleniyor: 2.1.0 gibi uc sayi. Panel karsilastirmayi';" ^
  "  Write-Host '  sayi sayi yapiyor; baska bicim GUNCELLEME GOSTERMEZ.';" ^
  "  Read-Host '  Kapatmak icin Enter'; exit 1 }" ^
  "Write-Host ('  Surum: ' + $surum) -ForegroundColor Cyan;" ^
  "$eski = $null;" ^
  "if (Test-Path (Join-Path $kok 'surum.json')) {" ^
  "  $eski = (Get-Content (Join-Path $kok 'surum.json') -Raw | ConvertFrom-Json).surum }" ^
  "if ($eski -eq $surum) {" ^
  "  Write-Host '';" ^
  "  Write-Host ('  !! surum.json ZATEN ' + $surum + ' diyor.') -ForegroundColor Yellow;" ^
  "  Write-Host '  surum.txt yukseltilmemis olabilir. Ayni sayiyla devam';" ^
  "  Write-Host '  edersen anne panelde hicbir degisiklik gormez.';" ^
  "  Write-Host '';" ^
  "  $c = Read-Host '  Yine de devam edilsin mi? (e/h)';" ^
  "  if ($c -ne 'e') { exit 1 } }" ^
  "Write-Host '';" ^
  "Write-Host '  Not girin - anne bunu panelde okuyacak.' -ForegroundColor Cyan;" ^
  "Write-Host '  Ornek: Gozler daha akici, uyku sayaci duzeltildi.';" ^
  "$notlar = Read-Host '  Not';" ^
  "if (-not $notlar) { $notlar = ('Surum ' + $surum) };" ^
  "Write-Host '';" ^
  "Write-Host '  Derleniyor...' -ForegroundColor Cyan;" ^
  "if (-not $env:IDF_PATH) { $env:IDF_PATH = Join-Path $env:USERPROFILE 'esp\esp-idf' };" ^
  "if (-not (Test-Path (Join-Path $env:IDF_PATH 'export.ps1'))) {" ^
  "  Write-Host ('  !! ESP-IDF bulunamadi: ' + $env:IDF_PATH) -ForegroundColor Red;" ^
  "  Read-Host '  Kapatmak icin Enter'; exit 1 }" ^
  "if (-not (Get-Command python -ErrorAction SilentlyContinue)) {" ^
  "  $uv = Join-Path $env:APPDATA 'uv\python';" ^
  "  if (Test-Path $uv) {" ^
  "    $py = Get-ChildItem $uv -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'python.exe') } | Select-Object -First 1;" ^
  "    if ($py) { $env:PATH = $py.FullName + ';' + $env:PATH } } }" ^
  ". \"$env:IDF_PATH\export.ps1\" | Out-Null;" ^
  "idf.py build;" ^
  "if ($LASTEXITCODE -ne 0) {" ^
  "  Write-Host '';" ^
  "  Write-Host '  !! DERLEME BASARISIZ - surum.json degistirilmedi.' -ForegroundColor Red;" ^
  "  Read-Host '  Kapatmak icin Enter'; exit 1 }" ^
  "$bin = 'build\pati.bin';" ^
  "if (-not (Test-Path $bin)) {" ^
  "  Write-Host '  !! build\pati.bin bulunamadi' -ForegroundColor Red;" ^
  "  Read-Host; exit 1 }" ^
  "$boy = (Get-Item $bin).Length;" ^
  "Write-Host '';" ^
  "Write-Host ('  pati.bin: ' + [math]::Round($boy/1MB,2) + ' MB') -ForegroundColor Green;" ^
  "$etiket = 'v' + $surum;" ^
  "$adres = 'https://github.com/General-dev-all/Pati-Robot/releases/download/' + $etiket + '/pati.bin';" ^
  "$j = [ordered]@{ surum = $surum; tarih = (Get-Date -Format 'yyyy-MM-dd'); notlar = $notlar; bin = $adres };" ^
  "$j | ConvertTo-Json | Set-Content (Join-Path $kok 'surum.json') -Encoding utf8;" ^
  "Write-Host '';" ^
  "Write-Host '  surum.json guncellendi.' -ForegroundColor Green;" ^
  "Write-Host '';" ^
  "Write-Host '  ============================================================';" ^
  "Write-Host '   SIRA SENDE - iki adim, SIRASI ONEMLI' -ForegroundColor Cyan;" ^
  "Write-Host '  ============================================================';" ^
  "Write-Host '';" ^
  "Write-Host '  1) ONCE Release. pati.bin oraya cikacak:';" ^
  "Write-Host '';" ^
  "Write-Host ('     gh release create ' + $etiket + ' \"' + (Resolve-Path $bin) + '\" --title ' + $etiket + ' --notes \"' + $notlar + '\"') -ForegroundColor White;" ^
  "Write-Host '';" ^
  "Write-Host '     (ya da GitHub sayfasindan Releases > Draft a new release,';" ^
  "Write-Host ('      etiket ' + $etiket + ', dosyayi surukle)');" ^
  "Write-Host '';" ^
  "Write-Host '  2) SONRA push:';" ^
  "Write-Host '';" ^
  "Write-Host '     git add -A; git commit; git push' -ForegroundColor White;" ^
  "Write-Host '';" ^
  "Write-Host '  NEDEN BU SIRA: push edilir edilmez surum.json yayina';" ^
  "Write-Host '  giriyor ve Pati o dosyaya bakiyor. Release henuz yoksa';" ^
  "Write-Host '  anne panelde guncellemeyi GORUR, basar ve indirme 404';" ^
  "Write-Host '  ile duser. Once dosyayi koy, sonra haberini ver.';" ^
  "Write-Host ''"

echo.
echo  ============================================================
pause
