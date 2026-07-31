# Pati — Asama 2 firmware'i
#
# Amaci TEK BIR SEY olcmek: gercek kartta, gercek ev wifi'sinde
# "cocuk sustu -> ilk ses" kac milisaniye. Kriter PLAN.md §3'te,
# olcumden ONCE yazildi: <=1500 ms gecer.
#
# Guzel gorunmesi gerekmiyor. Cirkin ama olculebilir bir kutu.

cmake_minimum_required(VERSION 3.16)

# Wifi sifresi ve API anahtari sdkconfig.defaults.local'dan geliyor.
# O dosya .gitignore'da; depoya girmiyor. Yoksa derleme yine calisiyor
# ama bos degerlerle — kart wifi'ye baglanamayip bunu soyluyor.
#
# ESP-IDF .local dosyasini KENDILIGINDEN okumuyor, zincire elle
# ekleniyor. Sonda olmasi onemli: sonraki dosya oncekini eziyor.
set(_pati_defaults "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    list(APPEND _pati_defaults "sdkconfig.defaults.local")
    message(STATUS "Pati: sdkconfig.defaults.local bulundu, zincire eklendi")

    # 🔴 SESSIZ TUZAK — bir kez ısırdı, bir daha ısırmasın.
    #
    # ESP-IDF, sdkconfig BIR KEZ olustuktan sonra defaults dosyalarina
    # bakmiyor: eski (bos) degerler orada cakili kaliyor. Yani wifi
    # sifresini .local'a yazip derlersen, sifre GECMEZ ve kart "sifre
    # yanlis" der. Dosyaya bakan da "ama sifre burada yaziyor" der.
    # Saatler yanar.
    #
    # Cozum tek satir: sdkconfig'i silmek. Burada zaman damgalarina
    # bakip haber veriyoruz.
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig")
        if("${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local"
           IS_NEWER_THAN "${CMAKE_CURRENT_LIST_DIR}/sdkconfig")
            message(WARNING
                "Pati: sdkconfig.defaults.local, sdkconfig'den DAHA YENI. "
                "Yeni degerler (wifi sifresi, API anahtari) BU DERLEMEYE "
                "GIRMEYECEK. Duzeltmek icin: sdkconfig dosyasini sil ve "
                "yeniden derle.")
        endif()
    endif()
else()
    message(WARNING
        "Pati: sdkconfig.defaults.local YOK. Wifi ve API anahtari bos "
        "kalacak. sdkconfig.defaults.local.ornek dosyasini kopyalayip "
        "doldurun.")
endif()
set(SDKCONFIG_DEFAULTS "${_pati_defaults}")

# ---------------------------------------------------------------------------
# SURUM — panelin "guncelleme var mi" sorusunun dayanagi
# ---------------------------------------------------------------------------
#
# Tek kaynak surum.txt. Buradan PROJECT_VER'e gidiyor, oradan uygulama
# imzasina (esp_app_desc), oradan da panele. Yani panelde gorunen sayi ile
# flash'taki yapinin ayni olmasi TESADUFE bagli degil.
#
# ESP-IDF kendiliginden `version.txt` diye bakiyor; dosyanin adi Turkce
# oldugu icin elle okuyoruz. Dosya yoksa IDF `git describe` kullanirdi ve
# surum "v2.0-3-gaacea4f" gibi bir seye donerdi — anneye gosterilecek ve
# karsilastirilacak bir sayi degil.
#
# ⚠️ Surum burada yukselmezse panel yeni yapiyi GORMEZ: karsilastirma
# surum.json'daki sayi ile bunun arasinda yapiliyor. YENI-SURUM.bat bu
# dosyayi okuyup surum.json'a yaziyor, yani ikisi ayrisamiyor.
file(READ "${CMAKE_CURRENT_LIST_DIR}/surum.txt" _pati_surum)
string(STRIP "${_pati_surum}" _pati_surum)
if(_pati_surum STREQUAL "")
    message(FATAL_ERROR "Pati: surum.txt bos. Panel surum karsilastirmasi "
                        "yapamaz, guncelleme hic gorunmez.")
endif()
set(PROJECT_VER "${_pati_surum}")
message(STATUS "Pati: surum ${PROJECT_VER}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pati)
