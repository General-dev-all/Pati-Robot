@echo off
chcp 65001 >nul
title Pati - telefondan
cd /d "%~dp0\..\prototype"

echo.
echo  ============================================================
echo   PATI - TELEFONDAN
echo  ============================================================
echo.
echo   Asagida telefondan yazacagin adres cikacak.
echo   Bir kez acip YER IMINE EKLE - adres degismiyor.
echo.
echo   ! ILK SEFERDE WINDOWS GUVENLIK DUVARI SORACAK.
echo     'Ozel aglar' isaretli olmali, yoksa telefon baglanmaz.
echo.
echo   ! TELEFONDAN KONUSMAK icin bu YETMEZ.
echo     Duz ag adresinde (http) tarayicilar mikrofona izin
echo     vermiyor. Konusmak icin PAYLAS.bat kullan - o https
echo     veriyor. Panel tasarimina bakmak icin bu yeterli.
echo.
echo   BU PENCEREYI KAPATMA. Bitirmek icin Ctrl+C.
echo.

.venv\Scripts\python.exe pati.py --arayuz --sayfa pati --disa

echo.
pause
