# -*- coding: utf-8 -*-
"""
Asama 1 prototipinin butun ayarlari tek yerde.

Buradaki sayilar OLCUMU dogrudan etkiliyor. Degistirirsen olctugun sey
degisir - o yuzden her birinin yaninda neden o deger oldugu yaziyor ve
rapor ciktisi bu degerleri aynen basiyor (hangi ayarla olculdugu
kaybolmasin diye).

PLAN.md ile bagi: §7 (protokol), §4 (kabul kriterleri), §12 (Asama 1).
"""

from __future__ import annotations

import os
from pathlib import Path

KOK = Path(__file__).resolve().parent


# ---------------------------------------------------------------------------
# API anahtari
#
# Derlemeye/koda GOMULMEZ. Iki yol var; ikisi de .gitignore'da.
# ---------------------------------------------------------------------------

def api_anahtari() -> str:
    anahtar = os.environ.get("PATI_API_ANAHTARI", "").strip()
    if anahtar:
        return anahtar

    dosya = KOK / "anahtar.txt"
    if dosya.exists():
        anahtar = dosya.read_text(encoding="utf-8").strip()
        if anahtar:
            return anahtar

    raise SystemExit(
        "API anahtari bulunamadi.\n"
        "  Ya  prototype/anahtar.txt  dosyasina yapistir,\n"
        "  ya da  PATI_API_ANAHTARI  ortam degiskenine koy.\n"
        "  Anahtar: https://aistudio.google.com/apikey"
    )


# ---------------------------------------------------------------------------
# Model
#
# 29 Temmuz 2026 itibariyla dokumanda listelenen Live API modelleri
# (ham metinden dogrulandi, bkz. NOTLAR.md):
#
#   gemini-3.1-flash-live-preview
#       "high-quality, low-latency audio-to-audio (A2A) model designed for
#        real-time dialogue and voice-first AI applications"
#       Getting-started dokumaninin ornek kodunda kullanilan model.
#
#   gemini-2.5-flash-native-audio-preview-12-2025
#       "flagship Live API model ... with native audio reasoning"
#       Dusunme (thinking) varsayilan acik -> gecikmeyi artirabilir.
#
# IKISINI DE olcecegiz. Hangisinin Turkcesi daha iyi, hangisi daha hizli
# bunu tahmin etmiyoruz - §4'teki kriterlere ikisini de sokuyoruz.
# ---------------------------------------------------------------------------

MODELLER = {
    "3.1": "gemini-3.1-flash-live-preview",
    "2.5": "gemini-2.5-flash-native-audio-preview-12-2025",
}
VARSAYILAN_MODEL = "3.1"

# Ham WebSocket ucu. Ham dokumandan birebir alindi (get-started-websocket).
WS_UC = ("wss://generativelanguage.googleapis.com/ws/"
         "google.ai.generativelanguage.v1beta.GenerativeService."
         "BidiGenerateContent")


# ---------------------------------------------------------------------------
# Ses hatti
#
# Bu sayilar ESP32'nin yapacagi isi taklit ediyor. Dokumandan:
#   giris : ham 16-bit PCM, 16 kHz, little-endian
#   cikis : ham 16-bit PCM, 24 kHz, little-endian
# ---------------------------------------------------------------------------

GIRIS_HZ = 16000
CIKIS_HZ = 24000
KANAL = 1                      # mono
ORNEK_GENISLIK = 2             # int16

# Mikrofon parca boyutu. 20 ms = 320 ornek = 640 bayt.
#
# NEDEN ONEMLI: bu deger dogrudan "tamponlama gecikmesi"ne giriyor
# (PLAN.md'de 0.1-0.2 sn diye tahmin edilen kalem). ESP32'nin I2S DMA
# tamponu da benzer buyuklukte olacak. Buyutursen ag trafigi azalir,
# gecikme artar.
PARCA_MS = 20
PARCA_ORNEK = GIRIS_HZ * PARCA_MS // 1000        # 320
PARCA_BAYT = PARCA_ORNEK * ORNEK_GENISLIK        # 640

# Hoparlor tarafinda kac ms'lik tampon tutalim. Kucuk tutuyoruz:
# amac dusuk gecikme, kesintisiz muzik degil.
CIKIS_TAMPON_MS = 60


# ---------------------------------------------------------------------------
# Yerel VAD - SADECE OLCUM VE BARGE-IN ICIN
#
# DIKKAT, BU AYRIM KRITIK:
# Konusma sirasini Gemini'nin kendi VAD'i yonetiyor (sunucu tarafi).
# Buradaki yerel VAD konusma sirasina KARISMIYOR. Iki isi var:
#   1. "Cocuk sustu" anini milisaniye damgasiyla isaretlemek (olcumun
#      sifir noktasi)
#   2. Robot konusurken cocugun araya girdigini fark etmek (barge-in
#      tepkisini olcmek icin)
#
# Esik degeri sabit degil: program acilista ortamin gurultusunu olcup
# esigi ona gore koyuyor. Sabit esik sessiz odada calisip gurultulu
# odada bozuluyordu - v1'in "hizli makinede olctum" hatasinin ses
# tarafindaki karsiligi bu olurdu.
# ---------------------------------------------------------------------------

VAD_KALIBRASYON_SN = 1.5       # acilista ne kadar dinleyip gurultuyu olcelim
VAD_ESIK_CARPANI = 3.5         # gurultunun kac kati "konusma" sayilsin
VAD_ESIK_TABAN = 90            # cok sessiz odada esigin altina dusmesin (RMS)

# Konusmanin bittigine karar vermek icin kac ms sessizlik bekleyelim.
# Bu SADECE bizim damgamiz icin; Gemini kendi karari icin kendi
# suresini kullaniyor (asagida silenceDurationMs).
#
# Damgayi geriye donuk koyuyoruz: sessizligin BASLADIGI ana. Yani bu
# bekleme suresi olculen gecikmeye eklenmiyor.
VAD_SESSIZLIK_MS = 300
VAD_KONUSMA_MS = 120           # bu kadar surekli ses = konusma basladi


# ---------------------------------------------------------------------------
# Gemini sunucu tarafi VAD
#
# PLAN.md: "Gemini'nin 'sustu' karari 0.5-0.8 sn - en buyuk parca,
# AYARLANABILIR". Ayarlanabilir olan tam olarak burasi.
#
# silenceDurationMs dusurmek gecikmeyi dogrudan dusurur ama cocugun
# cumle ortasinda nefes almasini "sustu" sanma riskini artirir.
# Prototipte bunu tarayacagiz (bkz. testler.py, VAD taramasi).
#
# None = gonderme, Google'in varsayilanini kullan. Ilk olcum boyle
# yapilmali: once varsayilanin ne verdigini gorelim, sonra kurcalayalim.
# ---------------------------------------------------------------------------

# SOZ KESME (barge-in) — cocuk Pati'nin sozunu kesebilsin mi?
#
# VARSAYILAN KAPALI. Sebep olculdu degil YASANDI: hoparlorle mikrofon
# yan yana oldugunda Pati'nin kendi sesi mikrofona giriyor, sunucunun
# VAD'i bunu "cocuk konusuyor" sanip cevabi kesiyor. Robot kendi sozunu
# cumlenin ortasinda kesiyor ve sebebi disardan anlasilmiyor.
#
# ACIKKEN sozunu kesme calisiyor ve testte "harika" bulundu — ama
# kulaklik ya da hoparloru mikrofondan uzaga koyan bir govde gerekiyor.
# Gercek cozum govde geometrisi (PLAN.md); bu ayar o zamana kadar
# secim hakki.
SOZ_KESME: bool = False

# Soz kesme KAPALIYKEN, Pati konusurken mikrofon esigi kac katina
# cikarilsin. 8 kat: kendi sesini duymuyor ama cocuk bagirirsa yine
# yakaliyor — tamamen sagir etmek de dogru degil.
YANKI_CARPANI: float = 8.0

VAD_SESSIZLIK_SUNUCU_MS: int | None = None
VAD_ONEK_DOLGU_MS: int | None = None


# ---------------------------------------------------------------------------
# Ses ve dil
#
# Ham dokumandan: "Native audio output models automatically choose the
# appropriate language and don't support explicitly setting the language
# code." -> Turkce'yi languageCode ile ZORLAMIYORUZ, sistem promptu
# Turkce oldugu icin model Turkce konusuyor. Bunu olcecegiz.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# SES SECIMI
#
# Google 30 hazir ses veriyor (speech-generation dokumani). Tam liste ve
# tanimlar ses_secimi.py'de.
#
# ⚠ ILK SECIM YANLISTI: "Kore" kullaniyorduk cunku dokumandaki ornek
#   kodda o vardi. Tanimi "Firm" (sert/kararli) — afacan bir cocuk
#   robotu icin ters. Ezberden degil, DINLEYEREK secilmeli.
#
# SECILDI — KULAKLA, panelden dinlenerek (29.07.2026).
#
# Adaylar dinlendi (Leda "Youthful", Puck "Upbeat", Fenrir "Excitable",
# Sadachbia "Lively", Achird "Friendly", Zephyr "Bright") ve PUCK
# secildi. Karar sayiyla degil dinleyerek verildi; ses secimi zaten
# olculebilecek bir sey degil.
SES_ADI = "Puck"

# Dil kodu. Ham dokumandan: "Native audio output models automatically
# choose the appropriate language and don't support explicitly setting
# the language code." Sema'da tr-TR gecerli bir deger ama native audio
# modelde gonderilmiyor. Turkce'yi sistem promptunun dili belirliyor.
DIL_KODU = None


# ---------------------------------------------------------------------------
# TIZLIK / HIZ — v1'in hilesi
#
# Live API'de tizlik ve hiz AYARLANAMIYOR. Dogrulandi:
#   - SpeechConfig semasinda sadece voiceConfig / multiSpeakerVoiceConfig
#     / languageCode var; speakingRate ya da pitch YOK.
#   - googleapis/python-genai#2029 acik ozellik istegi: "Speech rate is
#     fixed or only indirectly controllable."
#
# Ama v1'in cozumu hala gecerli. v1'in tts.py'sinden:
#   "Cozum eski bir hile: sesi daha YAVAS urettirip WAV basligindaki
#    ornekleme frekansini yukseltiyoruz. Calici sesi daha hizli
#    oynatiyor..."
#
# Bizde uretimi yavaslatamiyoruz ama CALMA hizini degistirebiliyoruz:
# Gemini 24.000 Hz PCM veriyor; 26.400 Hz'de calarsak ses %10 hizli ve
# ~%10 tiz oluyor. ESP32'de bu BEDAVA — sadece I2S saat ayari, sifir
# islemci yuku.
#
# ⚠ DURUST SINIR: v1 tizligi ve hizi AYRI ayarlayabiliyordu (uretimi
#   yavaslatip calmayi hizlandirarak). Bizde ikisi BIRLIKTE degisiyor.
#   Cok yukseltirsek robot hem tiz hem aceleci olur. v1'in tizlik 1.52
#   degerini buraya OLDUGU GIBI tasimak yanlis olur.
#
# SECILDI: 1.30 — panelden dinlenerek (29.07.2026). Puck ile birlikte
# Turkce'si "akici ve hizli" bulundu.
#
# v1'in 1.52 tizligine yaklasiyor ama birebir ayni sey degil: v1'de
# uretim ayri yavaslatilabildigi icin tiz olup aceleci olmuyordu.
# Burada 1.30 hem tizlik hem hiz demek.
CIKIS_HIZ = 1.30


# ---------------------------------------------------------------------------
# Ses seviyesi (PLAN.md, Asama 2)
#
# NEDEN VAR: bu ayar yoktu. PC'de sesi Windows'un mikseri belirliyordu ve
# fark edilmiyordu — ama ESP32'de isletim sistemi mikseri OLMAYACAK. Ayar
# yazilmazsa robotun sesi hic kisilamaz.
#
# NASIL: Gemini'nin 24 kHz int16 ornekleri hoparlore verilmeden once bu
# katsayiyla carpiliyor. ESP32'de maliyeti saniyede 24.000 carpma — yok
# sayilir, gecikmeye etkisi yok.
#
# DONANIM DEGIL YAZILIM: MAX98357'nin GAIN pini de 3-15 dB arasi
# ayarlanabiliyor (bagli degilse 9 dB varsayilan) ama yazilim carpani
# sifira kadar inebildigi icin donanimdan kismaya gerek kalmiyor. GAIN
# pini hicbir seye baglanmayacak.
SES_SEVIYESI = 0.85

# UST SINIR — cocuk bunun uzerine cikaramaz.
#
# Cocuk "sesini ac" dedikce sonsuza kadar acilmasin. 1.0 modelin
# gonderdigi tam genlik demek; kirpma (clipping) orada baslar.
SES_SEVIYESI_EN_FAZLA = 1.0

# ALT SINIR — tamamen sessize almak KASITLI olarak yok.
#
# Cocuk yanlislikla 0'a indirip "Pati bozuldu" sanmasin. En kisik
# haliyle bile duyulur kalsin; susturmak isteyen fisi ceker.
SES_SEVIYESI_EN_AZ = 0.15

# "Sesini kis / ac" her dendiginde ne kadar degissin.
SES_SEVIYESI_ADIM = 0.15


# ---------------------------------------------------------------------------
# Yuz ifadesi araci (PLAN.md)
#
# Modelin gozleri kendi yonetmesi icin arac tanimlaniyor. Kapatirsan
# gozler sadece konusma akisina gore degisir (dinliyor/dusunuyor/
# konusuyor) — karsilastirma yapmak icin ise yarar.
# ---------------------------------------------------------------------------

YUZ_ARACI = True


# ---------------------------------------------------------------------------
# Oturum yonetimi (PLAN.md)
#
#   tek oturum (sadece ses) : 15 dakika
#   baglanti zaman asimi    : ~10 dakika
#   devam anahtari omru     : 2 saat
#
# Ucunu de aciyoruz. GoAway gelince cumle ortasinda degil, konusma
# arasinda yeniden baglanmak istiyoruz.
# ---------------------------------------------------------------------------

OTURUM_DEVAMI = True           # sessionResumption
BAGLAM_SIKISTIRMA = True       # contextWindowCompression

# ---------------------------------------------------------------------------
# Baglam siniri — HIZ ICIN. Olculmus bir yavaslamanin uzerine kondu.
#
# 21 dakikalik kosuda (29.07.2026, 89 tur) baglam 2.243'ten 36.532
# token'a cikti ve gecikme duzenli olarak buyudu:
#
#     1. ceyrek (tur  1-22)  medyan   708 ms
#     2. ceyrek (tur 23-44)  medyan   938 ms
#     3. ceyrek (tur 45-66)  medyan   976 ms
#     4. ceyrek (tur 67-89)  medyan 1.100 ms
#
# Ilk yariya gore +283 ms. Sebep: model her turda daha cok gecmis
# okuyor.
#
# Sikistirma ACIKTI ama HIC CALISMADI. Ham dokumandan sebebi:
#   "If not set, the default is 80% of the model's context window
#    limit."
# Yani tetik ~100.000 token civari; biz 36.500'de kaldigimiz icin
# sikistirma tetiklenmedi.
#
# Dokumanin kendi ifadesi bu ayarin ne ise yaradigini soyluyor:
#   "This can be used to balance quality against latency as shorter
#    context windows may result in faster model responses. However,
#    any compression operation will cause a temporary latency
#    increase, so they should not be triggered frequently."
#
# KARAR: cocuk icin bir sohbet arkadasi yapiyoruz; onceligi HIZ.
# Robotun yarim saat oncesini hatirlamasi sart degil, ama gec cevap
# vermesi sohbeti oldururuyor. Baglami sinirliyoruz.
#
# 16.000 tetik / 8.000 hedef secildi cunku:
#   - Olculen buyume ~1.630 token/dk -> sikistirma ~10 dakikada bir
#     calisir. Dokuman "sik tetiklenmesin" diyor; 10 dakika seyrek.
#   - 8.000 token geriye donuk yaklasik 15-20 dakikalik sohbet demek;
#     cocuk icin fazlasiyla yeterli.
#
# ⚠ Bu degerler METIN kosusundan hesaplandi. Gercek robotta mikrofon
#   surekli akacak ve baglam daha hizli buyuyecek — mikrofonlu
#   olcumden sonra yeniden ayarlanmali.
BAGLAM_TETIK_TOKEN: int | None = 16000
BAGLAM_HEDEF_TOKEN: int | None = 8000

# ---------------------------------------------------------------------------
# BOSTA KAPATMA — maliyetin EN BUYUK kalemi burasi
#
# Fiyatlandirma (29.07.2026, gemini-3.1-flash-live-preview, ucretli):
#     ses girisi : $0.005 / dakika
#     ses cikisi : $0.018 / dakika
#
# Robot masada duruyor. Oturum acik kaldigi surece mikrofon akar ve
# KIMSE KONUSMASA BILE giris ucreti isler:
#
#     gunde 12 saat bosta = 720 dk x $0.005 = $3.60/gun = ~$108/ay
#
# Oysa gunde 1 saat GERCEK sohbet ~$0.66/gun = ~$20/ay.
# Yani bosta durmak, sohbetin kendisinden 5 KAT pahali.
#
# Ucretsiz katmanda para yanmiyor ama KOTA yaniyor — ayni mantik.
#
# Cozum: konusma olmayinca oturumu kapat. Cocuk tekrar konusunca
# yeniden acilir (yeniden baglanma ~500-640 ms olculdu, cocuk fark
# etmez). ESP32'de de aynisi yapilacak; orada ayrica yerel VAD
# mikrofonu uyandirir.
BOSTA_KAPAT_SN: float | None = 90.0

# Uykudayken mikrofonun son kac parcasi tamponda tutulsun.
# 100 parca x 20 ms = 2 saniye. Cocuk konusmaya baslayinca uyanma
# suresince soyledigi sey kaybolmasin diye; yoksa robot ilk kelimeyi
# duymamis gibi davraniyor.
UYKU_TAMPON_PARCA = 100

YENIDEN_BAGLANMA_DENEME = 5
YENIDEN_BAGLANMA_BEKLEME_SN = 0.5

# Konusma dokumu. Kapali olsa da olur ama:
#   - kufur suzgeci (guvenlik.py) metin olmadan calisamiyor
#   - "sistem promptuna uyum" testini elle degil otomatik sayabilmek icin
#     robotun ne dedigini yazili gormek gerekiyor
GIRIS_DOKUMU = True            # inputAudioTranscription
CIKIS_DOKUMU = True            # outputAudioTranscription


# ---------------------------------------------------------------------------
# Kabul kriterleri - PLAN.md'ten BIREBIR
#
# Bunlar olcumden ONCE yazildi ve kod icine kondu ki rapor "gecti/kaldi"
# yazsin, biz sonradan yorumlamayalim. v1'de en pahaliya patlayan sey
# olcumu sonradan yorumlamakti.
# ---------------------------------------------------------------------------

KRITER_GECIKME_GECER_MS = 1500
KRITER_GECIKME_SINIR_MS = 2500
KRITER_KESME_GECER_MS = 500
KRITER_KESME_SINIR_MS = 1000
KRITER_PROMPT_UYUM_GECER = 8       # 10 denemede
KRITER_PROMPT_UYUM_SINIR = 6


# ---------------------------------------------------------------------------
# Kayit
# ---------------------------------------------------------------------------

KAYIT_KLASORU = KOK / "olcumler"
HAM_KAYIT = True               # her mesajin ham JSON'u dosyaya yazilsin mi
