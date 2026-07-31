@echo off
chcp 65001 >nul
title Pati - paylasim tuneli
cd /d "%~dp0"

echo.
echo  ============================================================
echo   PATI - PAYLASIM TUNELI  (telefondan KONUSMAK icin)
echo  ============================================================
echo.
echo   ONCE SUNUCU calisiyor olmali:
echo     TELEFONDAN-INCELE.bat  - yanindaki dosya, cift tikla
echo.
echo   Port 8756'yi kullaniyor. Ayakta mi, asagida kontrol
echo   ediliyor - degilse tunel ACILMIYOR.
echo.
echo   Birazdan asagida bir adres cikacak:
echo     https://....trycloudflare.com
echo.
echo   TEST O ADRESTEN yapiliyor - telefonda onu ac.
echo.
echo   O adresi telefonunda ac. https oldugu icin tarayici
echo   mikrofona IZIN VERIYOR - Pati ile gercekten konusabilirsin.
echo.
echo   Kulaklik tak. Kulakliksizken telefonun hoparlorunden
echo   cikan ses ayni telefonun mikrofonuna giriyor ve Pati
echo   KENDI SOZUNU kesiyor.
echo.
echo   BITINCE BU PENCEREYI KAPAT. Tunel kapanir, adres oler.
echo.
echo  ------------------------------------------------------------
echo.

REM Sunucu gercekten ayakta mi?
REM
REM NEDEN: cloudflared, arkasinda hicbir sey olmasa da tunel aciyor ve
REM adres veriyor. Telefonda o adres 502 donuyor, ve "Pati bozuldu"
REM gibi gorunuyor. Sebebi ise sadece sunucunun kapali olmasi.
REM Desen "port ONCE, LISTENING SONRA" sirasinda: netstat satiri
REM   TCP    127.0.0.1:8756    0.0.0.0:0    LISTENING
REM Ters yazilirsa hicbir zaman eslesmez ve kontrol sessizce ise
REM yaramaz hale gelir (gercek dinleyiciyle iki yonlu sinandi).
netstat -an | findstr /r /c:":8756 .*LISTENING" >nul 2>&1
if errorlevel 1 (
    echo.
    echo   !! SUNUCU CALISMIYOR - port 8756 bos.
    echo.
    echo   Once TELEFONDAN-INCELE.bat'a cift tikla, o pencere
    echo   acik kalsin, sonra buraya don.
    echo.
    echo   Simdi tunel acsak adres verirdi ama telefonda
    echo   502 hatasi cikardi.
    echo.
    pause
    exit /b 1
)

cloudflared tunnel --url http://127.0.0.1:8756

echo.
echo  ============================================================
echo   Tunel kapandi. Adres artik calismiyor.
echo  ============================================================
pause
