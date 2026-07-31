idf_component_register(
    SRCS
        "app_main.cpp"
        "pati_ag.cpp"
        "pati_anahtar.cpp"
        "pati_ayar.cpp"
        "pati_cikarim.cpp"
        "pati_ekran.cpp"
        "pati_gozler.cpp"
        "pati_guncelleme.cpp"
        "pati_hafiza.cpp"
        "pati_kullanim.cpp"
        "pati_olcum.cpp"
        "pati_ses.cpp"
        "pati_panel.cpp"
        "pati_sohbet.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        conversation      # stackchan-idf'ten alindi (BSL-1.0): Gemini Live istemcisi
        driver            # I2S, GPIO, SPI master
        json              # cJSON — hafizanin NVS bicimi ve panel API'si
        esp_lcd           # ST7789 paneli — ESP-IDF'in KENDI bileseni, ucuncu parti yok
        esp_timer         # olcumun mikrosaniye damgasi
        esp_psram         # PSRAM'in gercekten 8 MB oldugunu dogrulamak icin
        esp_wifi          # Asama 2: wifi bilgisi gomulu (Asama 4'te captive portal)
        esp_event         # wifi olaylari
        esp_http_client   # hafiza cikarimi (Gemini flash-lite)
        esp_http_server   # ebeveyn paneli + captive portal
        esp_netif         # SNTP ve ag arayuzleri
        lwip              # captive portal DNS soketi
        esp-tls           # cikarim istegi TLS
        esp_https_ota     # panelden guncelleme: pati.bin'i indirip yazmak
        app_update        # OTA bolumleri, geri alma, kosan surumun imzasi
        nvs_flash         # wifi bilgisi, hafiza ve (ayri bolumde) anahtar
    EMBED_FILES
        # Panel sayfasi. panel/ klasorunun AYNISI gomuluyor: bir
        # kopya cikarsak biri gunceenip oteki eskir ve fark sessizce
        # buyur. Tek kaynak.
        "../../panel/index.html"
        "../../panel/stil.css"
        "../../panel/pati.js"
        "../../panel/gozler240.js"
        "../../panel/mikrofon.js"
        "../../panel/ornek.js"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
