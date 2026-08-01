# Pati — Asama 2 firmware'i
#
# Amaci TEK BIR SEY olcmek: gercek kartta, gercek ev wifi'sinde
# "cocuk sustu -> ilk ses" kac milisaniye. Kriter PLAN.md §3'te,
# olcumden ONCE yazildi: <=1500 ms gecer.
#
# Guzel gorunmesi gerekmiyor. Cirkin ama olculebilir bir kutu.

cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# 🔴 sdkconfig.defaults.local KALDIRILDI — geri eklemeyin.
# ---------------------------------------------------------------------------
#
# Burada bir zincir vardi: sdkconfig.defaults + sdkconfig.defaults.local.
# Ikinci dosya wifi bilgisini ve Gemini anahtarini tasiyordu ve
# .gitignore'daydi.
#
# Ucu de gitti:
#   anahtar   -> cihazda, panelden giriliyor (pati_anahtar.hpp)
#   wifi      -> cihazda, captive portal'dan giriliyor (pati_ag.cpp)
#   dosyanin kendisi -> tasiyacak bir sey kalmadigi icin islevsiz
#
# CONFIG_PATI_WIFI_SSID/PASS hala Kconfig'de duruyordu ama HICBIR kaynak
# dosya okumuyordu: yalnizca uretilen sdkconfig.h'de goruniyorlardi. Olu
# ayar, olmayan ayardan kotudur — birileri doldurur ve calistigini sanir.
#
# Dosya yokken CMake her derlemede "wifi ve API anahtari bos kalacak"
# diye UYARIYORDU. GitHub'in sunucusunda o dosya hicbir zaman yok, yani
# uyari her CI kosusunda cikiyordu ve soyledigi sey dogru DEGILDI. Her
# yayimda goruunen yanlis bir uyari, insani uyarilara bakmamaya alistirir.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")

# ---------------------------------------------------------------------------
# SURUM — panelin "guncelleme var mi" sorusunun dayanagi
# ---------------------------------------------------------------------------
#
# surum.txt IKI SEY tutuyor ve bilerek tek dosya:
#
#   1. satir   surum numarasi   -> buraya, PROJECT_VER'e
#   gerisi     annenin okuyacagi not -> GitHub Actions'a, oradan panele
#
# Ayri iki dosya olsaydi biri guncellenip oteki unutulurdu: surum 2.3.0
# olur, notta hala 2.2.0'in degisiklikleri yazardi. Anne panelde yanlis
# seyi okur ve kimse fark etmez. Ayni dosyada durunca ikisini birden
# degistirmemek zor.
#
# Buradan PROJECT_VER'e, oradan uygulama imzasina (esp_app_desc), oradan
# da panele. Yani panelde gorunen sayi ile flash'taki yapinin ayni olmasi
# TESADUFE bagli degil.
#
# ESP-IDF kendiliginden `version.txt` diye bakiyor; dosyanin adi Turkce
# oldugu icin elle okuyoruz. Dosya yoksa IDF `git describe` kullanirdi ve
# surum "v2.0-3-gaacea4f" gibi bir seye donerdi — anneye gosterilecek ve
# karsilastirilacak bir sayi degil.
#
# ⚠️ Surum burada yukselmezse panel yeni yapiyi GORMEZ: karsilastirma
# surum.json'daki sayi ile bunun arasinda yapiliyor. YENI-SURUM.bat bu
# dosyayi okuyup surum.json'a yaziyor, yani ikisi ayrisamiyor.
# 🔴 SESSIZ TUZAK — bir kez ısırdı, bir daha ısırmasın.
#
# `file(READ)` YAPILANDIRMA anında çalışıyor. CMake bu dosyayı bir
# bağımlılık saymadığı için surum.txt değişince yeniden yapılandırmıyor:
# sürümü yükseltirsin, derlersin, binary'nin içinde ESKİ sürüm kalır.
#
# 01.08.2026'da tam bunu yaşadım: surum.txt 2.1.1 yazıyordu, derlenen
# pati.bin 2.1.0 diyordu. Yayımlansaydı cihaz dosyayı indirip "beklenen
# sürüm değil" diye reddederdi (pati_guncelleme.cpp o denetimi yapıyor)
# ve sebebi surum.txt'ye bakan biri için görünmez olurdu — dosyada
# doğru sayı yazıyor.
#
# Bu satır CMake'e "bu dosya değişirse baştan yapılandır" diyor.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/surum.txt")
# YALNIZCA ILK SATIR. Gerisi annenin okuyacagi not ve derlemeyi
# ilgilendirmiyor.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/surum.txt" _pati_surum
     LIMIT_COUNT 1)
string(STRIP "${_pati_surum}" _pati_surum)
# Bicim denetimi BURADA yapiliyor, cunku hata en ucuz burada yakalanir.
# Yanlis bicim derlemeyi gecerse cihaz karsilastirmayi yapamaz ve
# guncelleme sessizce HIC gorunmez — arizasi olmayan bir ariza.
if(NOT _pati_surum MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "Pati: surum.txt'nin ILK SATIRI '2.2.0' gibi uc sayi olmali, "
        "'${_pati_surum}' bulundu. Panel karsilastirmayi sayi sayi "
        "yapiyor; baska bicim guncellemeyi HIC gostermez.")
endif()
set(PROJECT_VER "${_pati_surum}")
message(STATUS "Pati: surum ${PROJECT_VER}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pati)
