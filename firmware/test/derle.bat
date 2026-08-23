@echo off
rem Konak testlerini derler ve calistirir.
rem
rem NEDEN AYRI: bunlar firmware'in parcasi DEGIL. `main/` icinde
rem olmadiklari icin idf.py build'i etkilemiyorlar; konak derleyicisi
rem olmayan bir makinede sadece testler atlaniyor.
rem
rem UC TEST VAR:
rem
rem   goz_karsilastir     pati_gozler.cpp <-> panel/gozler240.js
rem                       Ayni girdiyle PIKSEL PIKSEL karsilastiriyor.
rem                       Uc gercek hata buldu: kesme/yuvarlama,
rem                       renk gecisinin erken 8 bite inmesi, satir
rem                       araligi hesabi.
rem
rem   hafiza_karsilastir  pati_hafiza.cpp <-> prototype/hafiza.py
rem                       Govdeleme KARAKTER sayiyor; bayt sayilirsa
rem                       Turkce kelimeler bozuk kesiliyor ve hafiza
rem                       ayni seyi iki kez kaydediyor.
rem
rem   ses_karsilastir     pati_ornekleyici.hpp — Pati'nin sesini uretiyor
rem                       Iki gercek hata buldu: dilim sinirinda ayrisma
rem                       (her sinirda tik sesi) ve uzun akista faz
rem                       kaymasi. Ikisi de kulakla duyulur, gozle
rem                       gorulmez.
rem
rem Ucunun de ortak gerekcesi: "portladim" ile "dogru portladim" ayri
rem seyler ve fark gozle gorulmuyor.
rem
rem NOT: bu dosya CRLF satir sonuyla durmali. LF ile yazilirsa cmd.exe
rem her satirin ilk harfini yiyor ve sebebi hic anlasilmiyor.

setlocal
cd /d "%~dp0"

rem Atama TIRNAK ICINDE: yolda "(x86)" var ve tirnaksiz atanirsa
rem parantez blogu icinde genisletilince cmd parantezi sozdizimi
rem saniyor ("\Microsoft was unexpected at this time").
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
rem cJSON, ESP-IDF ile geliyor. IDF_PATH kuruluysa oradan, degilse
rem varsayilan kurulum yolundan.
if defined IDF_PATH (
  set "CJSON=%IDF_PATH%\components\json\cJSON"
) else (
  set "CJSON=%USERPROFILE%\esp\esp-idf\components\json\cJSON"
)
set "PY=..\..\prototype\.venv\Scripts\python.exe"

rem %VCVARS% BU BLOGUN ICINDE YAZDIRILMIYOR: degerin icinde "(x86)"
rem var ve parantez blogu icinde genisletilince o ")" blogu erken
rem kapatiyor.
if not exist "%VCVARS%" (
  echo.
  echo   MSVC 2019 Build Tools bulunamadi.
  echo   Testler atlandi. Firmware derlemesi bundan etkilenmiyor.
  echo.
  exit /b 0
)

call "%VCVARS%" >nul
if not exist obj mkdir obj

rem ==================================================== 1) GOZLER
echo.
echo  ============================================================
echo   1) GOZLER  -  C++ cizim motoru vs tarayici
echo  ============================================================
echo   tarayici kareleri dokuluyor...
node goz_js_dok.mjs
if errorlevel 1 exit /b 1

echo   derleniyor...
cl /nologo /std:c++20 /EHsc /O2 /W3 /I stub goz_karsilastir.cpp /Fe:goz_karsilastir.exe /Fo:obj\ >derleme.log 2>&1
if errorlevel 1 (
  echo.
  echo   DERLEME HATASI - derleme.log:
  type derleme.log
  exit /b 1
)

rem ".\" ONEKI SART: bazi ortamlarda cmd gecerli klasoru PATH'te
rem aramiyor (NoDefaultCurrentDirectoryInExePath) ve "komut
rem bulunamadi" diyor - oysa exe orada duruyor.
.\goz_karsilastir.exe .
if errorlevel 1 set GOZ_HATA=1

rem ==================================================== 2) HAFIZA
echo.
echo  ============================================================
echo   2) HAFIZA  -  C++ metin mantigi vs Python
echo  ============================================================
echo   Python ciktisi dokuluyor...
"%PY%" hafiza_dok.py
if errorlevel 1 exit /b 1

echo   derleniyor...
cl /nologo /std:c++20 /EHsc /O2 /W3 /I stub /I "%CJSON%" hafiza_karsilastir.cpp "%CJSON%\cJSON.c" /Fe:hafiza_karsilastir.exe /Fo:obj\ >derleme2.log 2>&1
if errorlevel 1 (
  echo.
  echo   DERLEME HATASI - derleme2.log:
  type derleme2.log
  exit /b 1
)

.\hafiza_karsilastir.exe .
if errorlevel 1 set HAFIZA_HATA=1

rem ==================================================== 3) SES
echo.
echo  ============================================================
echo   3) SES  -  yeniden ornekleyici
echo  ============================================================
rem Pati'nin sesini ureten matematik: 24 kHz Gemini sesini 48 kHz'e
rem cevirirken 1.30x hiz carpanini da uyguluyor.
rem
rem EN ONEMLI SINAMA PARCA SINIRI. Gemini sesi 200-280 ms'lik dilimler
rem halinde geliyor; faz dilimler arasinda tasinmazsa her sinirda tik
rem sesi oluyor ve koda bakarak gorulmuyor. Sinama ayni sesi tek parca
rem ve duzensiz dilimler halinde isleyip ciktilari karsilastiriyor.
rem
rem Iki gercek hata buldu (23.08.2026): dilim sinirinda ayrisma ve
rem 500 saniyede 1.661 ornek kayma. Ikisinin de sebebi konumun tek bir
rem float'ta biriktirilmesiydi.
echo   derleniyor...
cl /nologo /std:c++20 /EHsc /O2 /W3 ses_karsilastir.cpp /Fe:ses_karsilastir.exe /Fo:obj\ >derleme3.log 2>&1
if errorlevel 1 (
  echo.
  echo   DERLEME HATASI - derleme3.log:
  type derleme3.log
  exit /b 1
)

.\ses_karsilastir.exe
if errorlevel 1 set SES_HATA=1

rem KULAKLA DINLEMEK ICIN: elinde 24 kHz mono int16 ham bir Gemini
rem kaydi varsa
rem
rem     ses_karsilastir.exe --pcm kayit.pcm --cikti .
rem
rem iki WAV yaziyor — biri onceki kartin caldigi sey, digeri yeni
rem yolun ciktisi. Ikisi ayni duyulmali.

rem ====================================================
echo.
echo  ============================================================
if defined GOZ_HATA echo   GOZLER: BASARISIZ
if defined HAFIZA_HATA echo   HAFIZA: BASARISIZ
if defined SES_HATA echo   SES: BASARISIZ
if not defined GOZ_HATA if not defined HAFIZA_HATA if not defined SES_HATA echo   UC TEST DE GECTI
echo  ============================================================
echo.

if defined GOZ_HATA exit /b 1
if defined HAFIZA_HATA exit /b 1
if defined SES_HATA exit /b 1
exit /b 0
