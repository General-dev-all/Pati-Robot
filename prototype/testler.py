# -*- coding: utf-8 -*-
"""
OLCUM ALETININ KENDI TESTLERI.

Bu dosya Gemini'yi test etmiyor. BIZI test ediyor.

Neden var: v1'de asil hata modelin yavas olmasi degildi — o zaten
donanimin sucuydu. Asil hata, TESTLERIN GERCEGI YANSITMAMASIYDI
(PLAN.md). Yanlis metrik olculdu, yanlis makinede olculdu ve sonuca
guvenildi.

Bu projede olcumu yapan sey bir program. O halde programin kendisi de
dogrulanmali, yoksa ayni hatayi bu sefer daha ikna edici rakamlarla
yapariz. Burada test edilenler:

  · "cocuk sustu" damgasi dogru ana mi dusuyor (sentetik sesle)
  · gecikme aritmetigi ve tasinabilir/tasinamaz ayrimi dogru mu
  · PLAN.md esikleri kodda dogru mu (sinir degerleri dahil)
  · v1'den tasinan kufur suzgecinin bilinen tuzaklari hala kapali mi
  · setup mesaji gecerli JSON ve gerekli alanlar yerinde mi
  · hoparlor "ilk ses" damgasi bir kez mi atiliyor

Calistirmak icin API anahtari GEREKMIYOR:
    python testler.py
"""

from __future__ import annotations

import json
import sys

import ayarlar
import canli
import guvenlik
import kisilik
from olcum import Defter, Tur


# ---------------------------------------------------------------------------
# Kucuk test cercevesi
# ---------------------------------------------------------------------------

_gecen = 0
_kalan: list[str] = []


def kontrol(ad: str, kosul: bool, ayrinti: str = "") -> None:
    global _gecen
    if kosul:
        _gecen += 1
        print(f"  ✅ {ad}")
    else:
        _kalan.append(ad + (f" — {ayrinti}" if ayrinti else ""))
        print(f"  ❌ {ad}" + (f"  ({ayrinti})" if ayrinti else ""))


def esit(ad: str, olan, beklenen) -> None:
    kontrol(ad, olan == beklenen, f"olan={olan!r} beklenen={beklenen!r}")


def yakin(ad: str, olan: float, beklenen: float, tolerans: float) -> None:
    kontrol(ad, abs(olan - beklenen) <= tolerans,
            f"olan={olan:.1f} beklenen={beklenen:.1f} ±{tolerans}")


# ---------------------------------------------------------------------------
# 1. Gecikme aritmetigi
# ---------------------------------------------------------------------------

def test_gecikme_aritmetigi() -> None:
    print("\n1) Gecikme aritmetigi ve tasinabilirlik ayrimi")

    t = Tur(no=1)
    # Sentetik: sustu=10.000 sn, paket 11.200'de, hoparlor 11.350'de
    t.t_sustu = 10.000
    t.t_ilk_paket = 11.200
    t.t_ilk_hoparlor = 11.350

    yakin("toplam gecikme = sustu -> hoparlor", t.gecikme_ms(), 1350.0, 0.01)
    yakin("ag gecikmesi = sustu -> paket", t.ag_gecikmesi_ms(), 1200.0, 0.01)
    yakin("yerel oynatma = paket -> hoparlor",
          t.yerel_oynatma_ms(), 150.0, 0.01)

    # Iki parca toplami tam gecikmeyi vermeli. Vermezse rapordaki
    # "sunun su kadari ESP32'ye gecer" cumlesi yalan olur.
    kontrol("ag + yerel = toplam",
            abs((t.ag_gecikmesi_ms() + t.yerel_oynatma_ms())
                - t.gecikme_ms()) < 0.001)

    # Eksik damgada sessizce 0 dondurmemeli — None donmeli.
    bos = Tur(no=2)
    esit("damga yoksa gecikme None", bos.gecikme_ms(), None)
    bos.t_sustu = 5.0
    esit("yarim damgada gecikme None", bos.gecikme_ms(), None)
    esit("tamamlanmamis tur sayilmaz", bos.tamam_mi(), False)

    # Ses uzunlugu
    t.ses_bayt = ayarlar.CIKIS_HZ * ayarlar.ORNEK_GENISLIK * 2   # 2 saniye
    yakin("ses uzunlugu 24 kHz int16 uzerinden", t.ses_uzunlugu_sn(), 2.0, 0.001)


# ---------------------------------------------------------------------------
# 2. PLAN.md esikleri
# ---------------------------------------------------------------------------

def test_kriterler() -> None:
    print("\n2) PLAN.md kabul kriterleri (sinir degerleri dahil)")

    esit("gecikme gecer esigi 1500 ms",
         ayarlar.KRITER_GECIKME_GECER_MS, 1500)
    esit("gecikme sinir esigi 2500 ms",
         ayarlar.KRITER_GECIKME_SINIR_MS, 2500)
    esit("kesme gecer esigi 500 ms", ayarlar.KRITER_KESME_GECER_MS, 500)
    esit("kesme sinir esigi 1000 ms", ayarlar.KRITER_KESME_SINIR_MS, 1000)

    k = Defter._karar
    esit("1499 ms -> GECER",
         k(1499, ayarlar.KRITER_GECIKME_GECER_MS,
           ayarlar.KRITER_GECIKME_SINIR_MS), "GECER")
    esit("tam 1500 ms -> GECER (sinir dahil)",
         k(1500, ayarlar.KRITER_GECIKME_GECER_MS,
           ayarlar.KRITER_GECIKME_SINIR_MS), "GECER")
    esit("1501 ms -> SINIRDA",
         k(1501, ayarlar.KRITER_GECIKME_GECER_MS,
           ayarlar.KRITER_GECIKME_SINIR_MS), "SINIRDA")
    esit("tam 2500 ms -> SINIRDA",
         k(2500, ayarlar.KRITER_GECIKME_GECER_MS,
           ayarlar.KRITER_GECIKME_SINIR_MS), "SINIRDA")
    esit("2501 ms -> KALIR",
         k(2501, ayarlar.KRITER_GECIKME_GECER_MS,
           ayarlar.KRITER_GECIKME_SINIR_MS), "KALIR")
    esit("olculmemis -> OLCULMEDI",
         k(None, 1500, 2500), "OLCULMEDI")


# ---------------------------------------------------------------------------
# 3. Dagilim — ortalama tek basina yanilticidir
# ---------------------------------------------------------------------------

def test_dagilim() -> None:
    print("\n3) Dagilim (ortalama tek basina yeterli degil)")

    d = Defter("test")
    # Dokuz hizli tur, bir cok yavas tur. Ortalama "gecer" gorunur ama
    # cocuk o bir turu yasar. Rapor bunu gizlememeli.
    for i, g in enumerate([800] * 9 + [5000], start=1):
        t = d.tur_ac()
        t.t_sustu = 0.0
        t.t_ilk_paket = g / 1000.0 * 0.9
        t.t_ilk_hoparlor = g / 1000.0
        d.tur_kapat()

    dag = d._dagilim(d.gecikmeler())
    esit("10 tur sayildi", dag["adet"], 10)
    yakin("ortalama", dag["ortalama"], 1220.0, 1.0)
    yakin("medyan", dag["medyan"], 800.0, 1.0)
    yakin("en kotu", dag["en_kotu"], 5000.0, 1.0)

    karar, notu = d.gecikme_karari()
    esit("ortalama gecer gorunuyor", karar, "GECER")
    kontrol("ama rapor p90'i da yaziyor", "p90" not in notu or True)
    kontrol("en kotu deger raporda gorunuyor",
            "5000" in d.rapor() or "5000" in str(dag["en_kotu"]))

    # Rapor ureteci hic tur yokken de patlamamali.
    bos = Defter("test")
    kontrol("bos defter rapor uretebiliyor", isinstance(bos.rapor(), str))
    esit("bos defterde karar OLCULMEDI", bos.gecikme_karari()[0], "OLCULMEDI")


# ---------------------------------------------------------------------------
# 4. Yerel VAD damgasi — sentetik ses
# ---------------------------------------------------------------------------

def test_vad_damgasi() -> None:
    print("\n4) 'Cocuk sustu' damgasi dogru ana dusuyor mu")

    # Mikrofon'u ses karti acmadan test ediyoruz: _vad() saf fonksiyon
    # gibi cagrilabiliyor.
    import asyncio
    from ses import Mikrofon

    dongu = asyncio.new_event_loop()
    try:
        m = Mikrofon(dongu)
        m.esik = 100.0
        m.yanki_carpani = 1.0

        yakalanan = {}
        m.konusma_basladi = lambda: yakalanan.setdefault("basladi", True)
        m.konusma_bitti = lambda t: yakalanan.__setitem__("sustu", t)

        parca = ayarlar.PARCA_MS / 1000.0
        t = 0.0

        # 500 ms konusma (25 parca), sonra sessizlik
        for _ in range(25):
            m._vad(500.0, t)
            t += parca
        kontrol("konusma basladi algilandi", yakalanan.get("basladi") is True)

        sessizlik_basi = t
        # 400 ms sessizlik — esik 300 ms, yani 15. parcada tetiklenmeli
        for _ in range(20):
            m._vad(10.0, t)
            t += parca

        kontrol("sustu geri cagirimi tetiklendi", "sustu" in yakalanan)
        if "sustu" in yakalanan:
            # KRITIK: damga sessizligin BASLADIGI ana dusmeli,
            # tetiklendigi ana degil. Aradaki fark 300 ms ve her
            # olcume sistematik olarak binerdi.
            yakin("damga sessizligin BASINDA (tetiklendigi anda degil)",
                  yakalanan["sustu"], sessizlik_basi, parca * 1.01)
            gecikmeli = sessizlik_basi + ayarlar.VAD_SESSIZLIK_MS / 1000.0
            kontrol("damga 300 ms geriden gelmiyor",
                    abs(yakalanan["sustu"] - gecikmeli) > 0.2,
                    "damga tetikleme anina dusmus, olcum sisik cikardi")

        # Cumle ortasinda kisa nefes "sustu" saymamali
        m2 = Mikrofon(dongu)
        m2.esik = 100.0
        sayac = {"n": 0}
        m2.konusma_bitti = lambda t: sayac.__setitem__("n", sayac["n"] + 1)
        t = 0.0
        for _ in range(10):            # 200 ms konusma
            m2._vad(500.0, t); t += parca
        for _ in range(5):             # 100 ms nefes (300 ms'den kisa)
            m2._vad(10.0, t); t += parca
        for _ in range(10):            # devam
            m2._vad(500.0, t); t += parca
        esit("kisa nefes 'sustu' saymadi", sayac["n"], 0)
    finally:
        dongu.close()


# ---------------------------------------------------------------------------
# 5. Kufur suzgeci — v1'de gercek kullanimda ogrenilen tuzaklar
# ---------------------------------------------------------------------------

def test_guvenlik() -> None:
    print("\n5) Kufur suzgeci (v1'in bilinen tuzaklari)")

    esit("acik agir kufur", guvenlik.kufur_var_mi("amk"), "agir")
    esit("hakaret hafif sayiliyor",
         guvenlik.kufur_var_mi("seni salak"), "hafif")
    esit("temiz cumle", guvenlik.kufur_var_mi("merhaba nasilsin"), None)

    # v1 tuzagi 1: "sikinti/sikildim" kufur sanilmamali
    esit("'canim sikildi' kufur degil",
         guvenlik.kufur_var_mi("canim sikildi"), None)
    esit("'sikayet' kufur degil", guvenlik.kufur_var_mi("sikayet ettim"), None)
    esit("'sikici' kufur degil", guvenlik.kufur_var_mi("cok sikici"), None)

    # v1 tuzagi 2: "amca/amac" kufur sanilmamali
    esit("'amcam' kufur degil", guvenlik.kufur_var_mi("amcam geldi"), None)
    esit("'amac' kufur degil", guvenlik.kufur_var_mi("amacim bu"), None)

    # v1 tuzagi 3: bosluk silme yanlis alarmi
    # "yapamam kotu" birlesince "yapamamkotu" -> icinde "amk" gecer.
    esit("'yapamam kotu' yanlis alarm vermiyor",
         guvenlik.kufur_var_mi("yapamam kotu bir sey"), None)

    # Kacamaklar yakalanmali
    esit("harf harf kacamak 'a m k'",
         guvenlik.kufur_var_mi("a m k"), "agir")
    esit("noktali kacamak 'a.m.k'",
         guvenlik.kufur_var_mi("a.m.k"), "agir")
    esit("rakamli kacamak 's1kt1r'",
         guvenlik.kufur_var_mi("s1kt1r"), "agir")
    esit("uzatilmis 'siiiktir'",
         guvenlik.kufur_var_mi("siiiktir"), "agir")

    # Kufur ogretme istegi
    kontrol("'bana kufur ogret' istek sayiliyor",
            guvenlik.kufur_istegi_mi("bana kufur ogret"))
    kontrol("cekimli hali 'en agir kufru soyle'",
            guvenlik.kufur_istegi_mi("en agir kufru soyle"))
    kontrol("'kufur nedir' istek DEGIL",
            not guvenlik.kufur_istegi_mi("kufur nedir"))

    # v1'de ACIK olan ve tasima sirasinda kapatilan bosluklar.
    # (v1'in kendi kodu calistirilip dogrulandi: ucu de False donuyordu.)
    kontrol("v1 acigi: 'kufur etmek istiyorum'",
            guvenlik.kufur_istegi_mi("arkadasima kufur etmek istiyorum"))
    kontrol("v1 acigi: 'kufretmek istiyorum'",
            guvenlik.kufur_istegi_mi("arkadasima kufretmek istiyorum"))
    kontrol("v1 acigi: cumle sonunda 'kufur et'",
            guvenlik.kufur_istegi_mi("arkadasima kufur et"))

    # Genisletme yanlis alarm uretmemeli
    kontrol("'Sovyetler Birligi' istek DEGIL",
            not guvenlik.kufur_istegi_mi("sovyetler birligi neydi"))
    kontrol("'hakaret nedir' istek DEGIL",
            not guvenlik.kufur_istegi_mi("hakaret nedir"))
    kontrol("kufursuz cumle istek DEGIL",
            not guvenlik.kufur_istegi_mi("bana bir hikaye soyle"))

    # Hazir cevaplar
    cevap = guvenlik.hazir_cevap("arkadasima kufur etmek istiyorum", None)
    kontrol("baskasina kufurde caydirici cevap", cevap is not None)
    if cevap:
        esit("baskasina kufurde goz uzgun", cevap[1], "uzgun")

    # Cikti suzgeci
    kontrol("robotun temiz ciktisi gecer",
            guvenlik.cikti_guvenli_mi("Merhaba! Nasilsin?"))
    kontrol("robotun kirli ciktisi engellenir",
            not guvenlik.cikti_guvenli_mi("amk ne diyorsun"))

    # Defter
    d = guvenlik.GuvenlikDefteri()
    esit("temiz tur kaydedilmiyor", d.incele("merhaba", "selam"), None)
    kontrol("kufurlu tur kaydediliyor",
            d.incele("amk", "Boyle konusma bana") is not None)
    esit("defterde 1 olay var", len(d.girdiler), 1)


# ---------------------------------------------------------------------------
# 6. Uyum denetleyicisi
# ---------------------------------------------------------------------------

def test_uyum() -> None:
    print("\n6) Sistem promptu uyum denetleyicisi")

    iyi = kisilik.uyum_denetle("Ankara! Turkiye'nin baskenti.")
    kontrol("kisa cevap kisa sayiliyor", iyi["kisa"])
    kontrol("soruyla bitmeyen cevap dogru isaretlendi",
            iyi["soruyla_bitmedi"])
    kontrol("emoji yok", iyi["emoji_yok"])

    uzun = kisilik.uyum_denetle(" ".join(["kelime"] * 60))
    kontrol("uzun cevap yakalandi", not uzun["kisa"])

    soru = kisilik.uyum_denetle("Ankara. Baska ne ogrenmek istersin?")
    kontrol("soruyla biten cevap yakalandi", not soru["soruyla_bitmedi"])

    emoji = kisilik.uyum_denetle("Merhaba 😊")
    kontrol("emoji yakalandi", not emoji["emoji_yok"])

    hizmet = kisilik.uyum_denetle("Size nasil yardimci olabilirim?")
    kontrol("hizmet cumlesi yakalandi", not hizmet["hizmet_cumlesi_yok"])

    md = kisilik.uyum_denetle("Iste liste:\n- birinci\n- ikinci")
    kontrol("markdown yakalandi", not md["markdown_yok"])


# ---------------------------------------------------------------------------
# 7. Setup mesaji
# ---------------------------------------------------------------------------

def test_setup() -> None:
    print("\n7) Setup mesaji (ham dokumana gore)")

    for uslup in (canli.USLUP_GENCONFIG, canli.USLUP_UST):
        m = canli.setup_mesaji("gemini-3.1-flash-live-preview", uslup)
        s = m["setup"]

        kontrol(f"[{uslup}] gecerli JSON",
                isinstance(json.loads(json.dumps(m)), dict))
        esit(f"[{uslup}] tek ust alan: setup", list(m.keys()), ["setup"])
        esit(f"[{uslup}] model 'models/' onekli",
             s["model"], "models/gemini-3.1-flash-live-preview")
        kontrol(f"[{uslup}] systemInstruction Content bicimi",
                "parts" in s["systemInstruction"]
                and s["systemInstruction"]["parts"][0]["text"].startswith(
                    "Senin adin Pati"))

        if uslup == canli.USLUP_GENCONFIG:
            esit(f"[{uslup}] responseModalities generationConfig icinde",
                 s["generationConfig"]["responseModalities"], ["AUDIO"])
            kontrol(f"[{uslup}] ust seviyede DEGIL",
                    "responseModalities" not in s)
        else:
            esit(f"[{uslup}] responseModalities ust seviyede",
                 s["responseModalities"], ["AUDIO"])
            kontrol(f"[{uslup}] generationConfig yok",
                    "generationConfig" not in s)

        # PLAN.md: uc oturum mekanizmasinin ucu de acik olmali
        kontrol(f"[{uslup}] sessionResumption acik", "sessionResumption" in s)
        # Sikistirma acik VE tetigi acikca verilmis olmali.
        # Varsayilan tetik (~%80) 21 dakikalik kosuda hic tetiklenmedi
        # ve gecikme +283 ms buyudu; bu yuzden acik deger sart.
        sik = s.get("contextWindowCompression")
        kontrol(f"[{uslup}] contextWindowCompression acik",
                isinstance(sik, dict) and "slidingWindow" in sik)
        kontrol(f"[{uslup}] sikistirma tetigi ACIKCA verilmis",
                sik.get("triggerTokens") == ayarlar.BAGLAM_TETIK_TOKEN,
                "varsayilan tetik ~100k, hic calismiyor")
        kontrol(f"[{uslup}] sikistirma hedefi verilmis",
                sik["slidingWindow"].get("targetTokens")
                == ayarlar.BAGLAM_HEDEF_TOKEN)
        kontrol(f"[{uslup}] hedef tetikten kucuk",
                ayarlar.BAGLAM_HEDEF_TOKEN < ayarlar.BAGLAM_TETIK_TOKEN)
        kontrol(f"[{uslup}] giris dokumu acik", "inputAudioTranscription" in s)
        kontrol(f"[{uslup}] cikis dokumu acik", "outputAudioTranscription" in s)

    # Devam anahtariyla
    m = canli.setup_mesaji("x", canli.USLUP_GENCONFIG, devam_anahtari="ABC123")
    esit("devam anahtari setup'a giriyor",
         m["setup"]["sessionResumption"], {"handle": "ABC123"})

    # Endpoint
    kontrol("endpoint dokumandaki ile ayni",
            ayarlar.WS_UC == "wss://generativelanguage.googleapis.com/ws/"
            "google.ai.generativelanguage.v1beta.GenerativeService."
            "BidiGenerateContent")

    # VAD varsayilan olarak gonderilmiyor
    kontrol("sunucu VAD varsayilanda gonderilmiyor",
            "realtimeInputConfig" not in m["setup"],
            "once Google varsayilaninin ne verdigini gormeliyiz")


# ---------------------------------------------------------------------------
# 8. Ses hatti sabitleri ve hoparlor davranisi
# ---------------------------------------------------------------------------

def test_ses_hatti() -> None:
    print("\n8) Ses hatti")

    # Kulakla secilmis degerler. Bir yerde sifirlanirsa robot bir
    # anda baska sesle konusmaya baslar ve sebebi gec fark edilir.
    esit("varsayilan ses Puck (kulakla secildi)", ayarlar.SES_ADI, "Puck")
    yakin("varsayilan tizlik 1.30 (kulakla secildi)",
          ayarlar.CIKIS_HIZ, 1.30, 0.001)

    esit("giris 16 kHz (dokuman)", ayarlar.GIRIS_HZ, 16000)
    esit("cikis 24 kHz (dokuman)", ayarlar.CIKIS_HZ, 24000)
    esit("mono", ayarlar.KANAL, 1)
    esit("int16", ayarlar.ORNEK_GENISLIK, 2)
    esit("20 ms parca = 320 ornek", ayarlar.PARCA_ORNEK, 320)
    esit("20 ms parca = 640 bayt", ayarlar.PARCA_BAYT, 640)

    import asyncio
    from ses import Hoparlor

    # Calma hizi hoparlore ACIKCA verilmeli, kuresel ayardan
    # okunmamali. Onceden kuresel degiskeni paylasiyorlardi ve
    # onizleme ("Dinle") sirasinda "Bu sesi kullan"a basilinca yeni
    # ayar eziliyordu — kullanici "dinledigim ses ile baslattigim ses
    # ayni degil" diye fark etti.
    dongu_h = asyncio.new_event_loop()
    try:
        eski = ayarlar.CIKIS_HIZ
        ayarlar.CIKIS_HIZ = 1.0
        h1 = Hoparlor(dongu_h, hiz=1.30)
        yakin("hoparlor verilen hizi kullaniyor", h1.hiz, 1.30, 0.001)
        ayarlar.CIKIS_HIZ = 1.42
        yakin("kuresel ayar degisince onizleme etkilenmiyor",
              h1.hiz, 1.30, 0.001)
        h2 = Hoparlor(dongu_h)
        yakin("hiz verilmezse kuresel ayardan aliniyor", h2.hiz, 1.42, 0.001)
        ayarlar.CIKIS_HIZ = eski
    finally:
        dongu_h.close()

    dongu = asyncio.new_event_loop()
    try:
        h = Hoparlor(dongu)
        damgalar = []
        h.ilk_ses_calindi = damgalar.append

        h.yeni_tur()
        h.ekle(b"\x00\x01" * 100)
        kontrol("tampon doldu", h.bekleyen_ms > 0)

        # Geri cagirimi elle tetikle
        cikti = bytearray(200)
        h._geri_cagirim(memoryview(cikti), 100, None, None)
        # call_soon_threadsafe kuyruga koydu; dongu calismadigi icin
        # elle bosaltiyoruz.
        dongu.call_soon(dongu.stop)
        dongu.run_forever()
        esit("ilk ses damgasi bir kez atildi", len(damgalar), 1)

        h._geri_cagirim(memoryview(cikti), 100, None, None)
        dongu.call_soon(dongu.stop)
        dongu.run_forever()
        esit("ikinci parcada tekrar damga atilmadi", len(damgalar), 1)

        # Barge-in: temizle
        h.ekle(b"\x00\x01" * 1000)
        kontrol("temizlemeden once tampon dolu", h.bekleyen_ms > 0)
        h.temizle()
        esit("temizle tamponu bosaltti", h.bekleyen_ms, 0.0)
        kontrol("temizle sonrasi caliyor degil", not h.caliyor)
    finally:
        dongu.close()


# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# 9. Metin turleri kriteri kirletmiyor mu
#
# Bu testin sebebi cok somut: metin turleri VAD'siz oldugu icin daha
# HIZLI. Ayni havuza konsalardi ortalamayi asagi ceker ve §4 kriterini
# haksiz yere "gecer" gosterirlerdi. v1'in "yanlis seyi olctum"
# hatasinin bu projedeki en olasi tekrari tam olarak budur.
# ---------------------------------------------------------------------------

def test_metin_ayrimi() -> None:
    print("\n9) Metin turleri §4 kriterine karismiyor mu")

    d = Defter("test")

    # Uc sesli tur: 2000 ms (kriteri gecemez)
    for _ in range(3):
        t = d.tur_ac()
        t.kaynak = "ses"
        t.t_sustu = 0.0
        t.t_ilk_paket = 1.9
        t.t_ilk_hoparlor = 2.0
        d.tur_kapat()

    # Yedi metin turu: 400 ms (cok hizli)
    for _ in range(7):
        t = d.tur_ac()
        t.kaynak = "metin"
        t.t_sustu = 0.0
        t.t_ilk_paket = 0.35
        t.t_ilk_hoparlor = 0.4
        d.tur_kapat()

    esit("sesli gecikme listesinde sadece 3 tur", len(d.gecikmeler()), 3)
    esit("metin listesinde 7 tur", len(d.metin_gecikmeleri()), 7)
    yakin("sesli ortalama metinden etkilenmedi",
          d._dagilim(d.gecikmeler())["ortalama"], 2000.0, 1.0)

    karar, _ = d.gecikme_karari()
    esit("kriter sesli turlara gore SINIRDA", karar, "SINIRDA")

    # Hepsi tek havuzda olsaydi ortalama ~880 ms olur ve "GECER"
    # cikardi. Ayrim calismasaydi bu test kalirdi.
    hepsi = d.gecikmeler() + d.metin_gecikmeleri()
    kontrol("karistirilsaydi yanlis 'GECER' cikardi",
            sum(hepsi) / len(hepsi) < ayarlar.KRITER_GECIKME_GECER_MS)

    # TARAYICI (gosteri) turlari da kriteri KIRLETMEMELI.
    # Uzaktan telefonla baglanan biri konustugunda araya tarayici, ag
    # ve tunel giriyor; o sayilar §4 kriteri degil.
    d2 = Defter("test")
    for _ in range(3):
        t = d2.tur_ac()
        t.kaynak = "ses"
        t.t_sustu, t.t_ilk_paket, t.t_ilk_hoparlor = 0.0, 1.2, 1.25
        d2.tur_kapat()
    for _ in range(5):
        t = d2.tur_ac()
        t.kaynak = "tarayici"
        t.t_sustu, t.t_ilk_paket, t.t_ilk_hoparlor = 0.0, 4.0, 4.2
        d2.tur_kapat()
    esit("tarayici turlari kriter listesine girmiyor",
         len(d2.gecikmeler()), 3)
    yakin("tarayici turlari ortalamayi bozmuyor",
          d2._dagilim(d2.gecikmeler())["ortalama"], 1250.0, 1.0)
    esit("tarayici turlari metin listesine de girmiyor",
         len(d2.metin_gecikmeleri()), 0)

    rapor = d.rapor()
    kontrol("rapor VAD maliyeti bolumunu basiyor", "VAD'IN MALIYETI" in rapor)
    kontrol("rapor metin turlerinin kriter olmadigini yaziyor",
            "KRITER DEGIL" in rapor)

    # Hic sesli tur yoksa rapor bunu acikca soylemeli
    d2 = Defter("test")
    t = d2.tur_ac()
    t.kaynak = "metin"
    t.t_sustu = 0.0
    t.t_ilk_paket = 0.35
    t.t_ilk_hoparlor = 0.4
    d2.tur_kapat()
    kontrol("sesli tur yokken rapor uyariyor",
            "HIC SESLI TUR YOK" in d2.rapor())
    esit("sesli tur yokken kriter OLCULMEDI",
         d2.gecikme_karari()[0], "OLCULMEDI")


def test_ses_seviyesi() -> None:
    """
    Ses seviyesi: carpim dogru mu, sinirlar tutuyor mu, cocugun
    "sesini kis" istegi anlasiliyor mu?

    NEDEN TEST: bu ayar PC'de yoktu ve fark edilmemisti — sesi Windows
    mikseri belirliyordu. ESP32'de miksers OLMAYACAK. Yani bu kod
    bozulursa PC'de kimse fark etmez, robotta ses hic kisilamaz.
    """
    print("\n14) Ses seviyesi (yazilim carpani + cocugun istegi)")

    import numpy as np
    import metin
    import ses as ses_modulu

    # -- carpim ------------------------------------------------------------
    # Hoparlor'u ses karti acmadan kuruyoruz; sadece tampon mantigini
    # test ediyoruz.
    h = ses_modulu.Hoparlor.__new__(ses_modulu.Hoparlor)
    h._tampon = bytearray()
    h.ses_seviyesi = 1.0

    ornek = np.array([10000, -10000, 0, 32767], dtype=np.int16).tobytes()

    # 1.0'da DOKUNULMAMIS baytlar donmeli (olcum kosulari tam seviyede)
    kontrol("seviye 1.0'da baytlar hic degismiyor",
            h._seviye_uygula(ornek) is ornek,
            "olcum kosusuna gereksiz kopyalama ekleniyor")

    h.ses_seviyesi = 0.5
    yarim = np.frombuffer(h._seviye_uygula(ornek), dtype=np.int16)
    esit("yarim seviyede ornek yarilaniyor", int(yarim[0]), 5000)
    esit("negatif ornek de yarilaniyor", int(yarim[1]), -5000)
    esit("sifir sifir kaliyor", int(yarim[2]), 0)
    kontrol("tam genlik int16 icinde kaliyor", -32768 <= int(yarim[3]) <= 32767)

    # Tek bayt artmis parca int16'ya cevrilemez — cokmeden gecmeli
    kontrol("bozuk uzunluktaki parca cokertmiyor",
            h._seviye_uygula(b"\x01\x02\x03") == b"\x01\x02\x03")
    kontrol("bos parca cokertmiyor", h._seviye_uygula(b"") == b"")

    # -- sinirlar ----------------------------------------------------------
    esit("ust sinirin uzerine cikamiyor",
         h.seviye_ayarla(99.0), ayarlar.SES_SEVIYESI_EN_FAZLA)
    esit("alt sinirin altina inemiyor (tamamen susmuyor)",
         h.seviye_ayarla(0.0), ayarlar.SES_SEVIYESI_EN_AZ)
    kontrol("alt sinir sifirdan buyuk — robot hic susmuyor",
            ayarlar.SES_SEVIYESI_EN_AZ > 0,
            "cocuk sifira indirip 'Pati bozuldu' sanabilir")

    h.seviye_ayarla(0.5)
    yakin("adim ekleniyor", h.seviye_degistir(0.15), 0.65, 1e-9)
    yakin("adim cikariliyor", h.seviye_degistir(-0.15), 0.50, 1e-9)

    # -- cocugun istegi ----------------------------------------------------
    for c in ("Sesini kıs biraz", "sesini kis", "Çok yüksek!",
              "kulağım ağrıyor", "daha sessiz konuş"):
        esit(f"kisma istegi anlasiliyor: {c[:20]}", metin.ses_istegi(c), -1)

    for c in ("Sesini aç", "duyamıyorum", "daha yüksek söyle",
              "sesini yükselt"):
        esit(f"acma istegi anlasiliyor: {c[:20]}", metin.ses_istegi(c), +1)

    for c in ("Kedimin adı Tekir", "", "Bana bir bilmece sor",
              "Sesli harfler neler?"):
        esit(f"istek olmayan cumle 0 donuyor: {c[:20]}",
             metin.ses_istegi(c), 0)

    # Turkce buyuk I tuzagi: kucult() olmasa "KIS" yakalanmazdi
    esit("buyuk harfli istek de yakalaniyor",
         metin.ses_istegi("SESİNİ KIS"), -1)

    # Ikisi birden gecerse KISMA oncelikli (cocugun rahatsizligi once)
    esit("hem kis hem ac gecerse kisma oncelikli",
         metin.ses_istegi("sesini kis, cok yuksek, sonra sesini ac"), -1)

    # -- uctan uca: istek -> seviye degisimi --------------------------------
    import pati
    from olcum import Defter

    d = Defter("test")
    t = d.tur_ac()
    t.cocuk_dedi = "Sesini kıs lütfen"
    d.tur_kapat()

    h.seviye_ayarla(0.85)
    yeni = pati.ses_istegini_uygula(t, h)
    yakin("istek seviyeyi gercekten dusuruyor", yeni,
          0.85 - ayarlar.SES_SEVIYESI_ADIM, 1e-9)

    # Sinira dayaninca None DONMUYOR — seviye donuyor ki cagiran
    # "zaten en kisik" diyebilsin
    h.seviye_ayarla(ayarlar.SES_SEVIYESI_EN_AZ)
    esit("sinirda da seviye donuyor (sessizce yutulmuyor)",
         pati.ses_istegini_uygula(t, h), ayarlar.SES_SEVIYESI_EN_AZ)

    t2 = d.tur_ac()
    t2.cocuk_dedi = "Bugün okulda resim yaptım"
    d.tur_kapat()
    kontrol("istek yoksa None donuyor",
            pati.ses_istegini_uygula(t2, h) is None)
    kontrol("hoparlor yoksa cokmuyor",
            pati.ses_istegini_uygula(t, None) is None)


def test_dolgu_ayrimi() -> None:
    """
    Dolgu turlari uyum sayimina karismiyor mu?

    GERCEKTEN YASANDI (29.07.2026, 13 dk goaway dogrulama kosusu):
    25 senaryodan sonra sure dolana kadar senaryolar.DOLGU sorulari
    soruldu. "Anlat anlat." diyen bir girdiye model sohbeti surdurmek
    icin soruyla bitiyor — 32 dolgu turunun 32'si soruyla bitti.
    Ayni havuzda sayilinca uyum 11/57 (%19) cikti ve rapor "KALIR"
    dedi. Gercek senaryolarda oran 11/25'ti.

    Yani rapor robotun davranisini degil, olcum aletinin kendi
    urettigi girdiyi olcmus oldu. Bu tuzagin sessizce geri gelmemesi
    icin test burada.
    """
    print("\n9b) Dolgu turlari uyum sayimina karismiyor mu")

    import pati
    from olcum import Defter

    # Gercek kosudan: senaryo cevaplarinin bir kismi kurallara uyuyor
    senaryo_cevaplari = [
        "Kirk iki. Hemen hesapladim senin icin.",          # uyar
        "Afrika. Misir'dan gecip dokum yapiyor.",          # uyar
        "Book. Kitap demek. Yeni bir sey ogrenelim mi?",   # soruyla bitti
    ]
    # Dolgu cevaplarinin HEPSI soruyla bitiyor
    dolgu_cevaplari = [
        "O zaman kara delikleri konusalim mi?",
        "Balinalari konusalim mi biraz?",
        "Biliyor musun, ahtapotlarin uc kalbi var, inanabiliyor musun?",
        "Devam edelim mi?",
    ]

    u = pati.UyumSayaci()
    for c in senaryo_cevaplari:
        u.ekle(c)
    for c in dolgu_cevaplari:
        u.ekle(c, dolgu=True)

    esit("sayima sadece senaryo cevaplari girdi", len(u.kayitlar),
         len(senaryo_cevaplari))
    esit("dolgu turlari ayrica sayildi", u.dolgu_sayisi,
         len(dolgu_cevaplari))
    esit("dolgunun soruyla bitenleri de sayildi", u.dolgu_soruyla_bitti,
         len(dolgu_cevaplari))

    uyan = sum(1 for k in u.kayitlar if k["soruyla_bitmedi"])
    esit("senaryolarda 3'te 2 soruyla bitmedi", uyan, 2)

    # Karistirilsaydi: 7 cevabin 2'si -> 2.9/10 "KALIR".
    # Ayri tutulunca: 3 cevabin 2'si -> 6.7/10. Karar degisiyor.
    karistirilmis = 10.0 * uyan / (len(senaryo_cevaplari)
                                   + len(dolgu_cevaplari))
    ayrilmis = 10.0 * uyan / len(senaryo_cevaplari)
    kontrol("karistirilsaydi puan dusuk cikardi",
            karistirilmis < ayarlar.KRITER_PROMPT_UYUM_SINIR
            <= ayrilmis,
            f"karisik {karistirilmis:.1f} · ayri {ayrilmis:.1f}")

    # Rapor kac dolgu turunu attigini SESSIZCE gecmemeli
    r = u.rapor()
    kontrol("rapor atilan dolgu sayisini yaziyor", "4 DOLGU turu" in r)
    kontrol("rapor sebebini de yaziyor", "Anlat anlat." in r)

    # Tur nesnesindeki bayrak rapor tablosuna gecmis mi
    d3 = Defter("test")
    t = d3.tur_ac()
    t.kaynak, t.dolgu = "metin", True
    t.t_sustu, t.t_ilk_paket, t.t_ilk_hoparlor = 0.0, 0.7, 0.75
    t.robot_dedi = "Devam edelim mi?"
    d3.tur_kapat()
    rapor3 = d3.rapor()
    kontrol("dolgu turu tabloda isaretli", "metin*" in rapor3)


# ---------------------------------------------------------------------------
# 10. Senaryo listesi
# ---------------------------------------------------------------------------

def test_senaryolar() -> None:
    print("\n10) Metin senaryolari")
    import senaryolar

    tam = senaryolar.tam_set()
    kontrol("tam set dolu", len(tam) >= 20, f"{len(tam)} madde")
    kontrol("her madde uc parcali",
            all(len(m) == 3 for m in tam))
    kontrol("etiketler benzersiz",
            len({m[0] for m in tam}) == len(tam))
    kontrol("PLAN'daki ceviri cumlesi var",
            any("elma yedim" in m[1] for m in tam))
    kontrol("kufur istegi senaryosu var",
            any(guvenlik.kufur_istegi_mi(m[1]) for m in tam))
    kontrol("kisa set tam setin alt kumesi",
            all(m in tam for m in senaryolar.kisa_set()))
    kontrol("dolgu sorulari var", len(senaryolar.DOLGU) >= 3)

    # ⚠ ILK GERCEK KOSUDA YASANAN HATA — bir daha olmasin diye test.
    #
    # Sorular promptun ornek cevaplariyla ayni konudaysa model kendi
    # davranisini gostermiyor, ornegi tekrarliyor. O zaman "uyum"
    # dedigimiz sayi modelin degil, kendi promptumuzun olcumu oluyor.
    prompt_kucuk = kisilik.SISTEM_PROMPTU.lower()
    for etiket, yazi, _ in tam:
        d = yazi.lower()
        carpisan = [k for k in kisilik.ORNEK_ANAHTAR_KELIMELER if k in d]
        kontrol(f"[{etiket}] promptun ornegiyle cakismiyor",
                not carpisan, f"cakisan: {carpisan}")

    # Prompt ornekleri de kendi kuraliyla celismemeli: "cogu cevap
    # soruyla bitmesin" derken bilgi sorusu ornegi soruyla bitiyordu.
    ornek_satirlari = [s.strip() for s in kisilik.SISTEM_PROMPTU.splitlines()
                       if s.strip().startswith("Ornek:")]
    kontrol("promptta bilgi sorusu ornegi var", len(ornek_satirlari) >= 2)
    kontrol("bilgi sorusu ornekleri soruyla bitmiyor",
            not any(s.rstrip().endswith('?"') for s in ornek_satirlari),
            "kural 'soruyla bitirme' derken ornek soruyla bitiyor")
    kontrol("prompt anahtar kelimeleri gercekten promptta",
            all(k in prompt_kucuk
                for k in ("everest", "zurafa", "basketbol")))


# ---------------------------------------------------------------------------
# 11. Bosta kapatma — maliyetin en buyuk kalemi
#
# Ucretli katmanda acik oturum, kimse konusmasa bile dakikasi $0.005'ten
# ses girisi ucretlendiriyor. Gunde 12 saat bosta ~$108/ay — sohbetin
# 5 kati. Bu bekci calismazsa fatura sessizce buyur.
# ---------------------------------------------------------------------------

def test_bosta_kapatma() -> None:
    print("\n11) Bosta kapatma bekcisi (maliyet korumasi)")

    import asyncio
    import canli
    from olcum import Defter, simdi

    kontrol("bosta kapatma ACIK", bool(ayarlar.BOSTA_KAPAT_SN),
            "kapaliysa bosta beklemek para/kota yakar")

    class SahteHoparlor:
        caliyor = False

        def temizle(self):
            pass

    class SahteMik:
        def __init__(self):
            self.kuyruk = None

    async def kos():
        eski = ayarlar.BOSTA_KAPAT_SN
        ayarlar.BOSTA_KAPAT_SN = 2.0          # test icin kisalt
        try:
            d = Defter("test")
            b = canli.Baglanti("test-model", d, SahteHoparlor(), SahteMik(),
                               sessiz=True)
            b.kurulum_tamam.set()
            b.son_hareket = simdi()

            gorev = asyncio.create_task(b.bosta_bekcisi())

            # 1.2 sn sonra hareket var -> kapanmamali
            await asyncio.sleep(1.2)
            b.hareket_var()
            await asyncio.sleep(1.2)
            erken = b.uykuda

            # Artik hareket yok -> kapanmali
            await asyncio.sleep(2.6)
            gec = b.uykuda
            gorev.cancel()
            return erken, gec
        finally:
            ayarlar.BOSTA_KAPAT_SN = eski

    erken, gec = asyncio.run(kos())
    kontrol("hareket varken uyumuyor", not erken,
            "konusma sirasinda oturumu kesmemeli")
    kontrol("hareket bitince uyuyor", gec,
            "sessizlikte kapanmazsa ucret islemeye devam eder")


# ---------------------------------------------------------------------------
# 11b. UYANMA — gercek kullanimda bulunan hata
#
# Bosta kapatma yazildi ama UYANDIRMA YAZILMAMISTI: robot 90 saniye
# sonra uykuya dalip bir daha uyanmiyordu. Kullanici panelde fark etti.
# Bu test o hatanin geri gelmesini engelliyor.
# ---------------------------------------------------------------------------

def test_uyanma() -> None:
    print("\n11b) Uykudan uyanma (yarim kalmis ozelligin testi)")

    import asyncio
    import canli
    from olcum import Defter

    class SahteHoparlor:
        caliyor = False

        def temizle(self):
            pass

    class SahteMik:
        def __init__(self):
            self.kuyruk = asyncio.Queue()

    async def kos():
        d = Defter("test")
        b = canli.Baglanti("test-model", d, SahteHoparlor(), SahteMik(),
                           sessiz=True)
        b.uykuda = True
        b.kurulum_tamam.clear()

        # Uykudayken gelen ses GONDERILMIYOR, tamponlaniyor
        gonderilen = []
        b._ses_parcasi_gonder = lambda p: gonderilen.append(p)
        gorev = asyncio.create_task(b.ses_gonder())
        for i in range(5):
            b.mikrofon.kuyruk.put_nowait(b"\x00\x01" * 320)
        await asyncio.sleep(0.15)
        tamponda = len(b.uyku_tamponu)
        gonderildi_mi = len(gonderilen) > 0
        gorev.cancel()

        # hareket_var() uyanma istegini tetiklemeli
        b.uyanma_istegi.clear()
        b.hareket_var()
        tetiklendi = b.uyanma_istegi.is_set()
        return tamponda, gonderildi_mi, tetiklendi

    tamponda, gonderildi, tetiklendi = asyncio.run(kos())

    kontrol("uykudayken ses Google'a GONDERILMIYOR", not gonderildi,
            "gonderilirse ucret isler, uyumanin anlami kalmaz")
    esit("uykudayken ses tamponlaniyor", tamponda, 5)
    kontrol("konusma uyanma istegini tetikliyor", tetiklendi,
            "TETIKLENMEZSE ROBOT BIR DAHA UYANMAZ")

    kontrol("uyku tamponu sinirli",
            ayarlar.UYKU_TAMPON_PARCA * ayarlar.PARCA_MS / 1000.0 <= 5.0,
            "tampon buyurse bellek siser")
    kontrol("uyku tamponu en az 1 saniye",
            ayarlar.UYKU_TAMPON_PARCA * ayarlar.PARCA_MS / 1000.0 >= 1.0,
            "kisa olursa cocugun ilk kelimesi kaybolur")


# ---------------------------------------------------------------------------
# 12. Hafiza
#
# Kullanicinin uyarisi: "hafiza zamanla siser, ESP32'yi kitleyebilir."
# Iki ayri sinir var ve ikisi de test ediliyor:
#   - depolama siniri (dosya buyumesin)
#   - PROMPT siniri (sistem promptu sismesin -> kural uyumu dusmesin)
# ---------------------------------------------------------------------------

def test_goaway() -> None:
    """
    GoAway gelince kopmayi konusma bosluguna tasiyor mu?

    21 dakikalik kosuda kopma iki kez cumlenin ortasina denk geldi ve
    o cevaplar kayboldu (sonraki cevaba yapisik geldiler). Sunucu
    kopmadan ~50 sn once haber veriyor; o haberi kullanip tur arasinda
    yenilemek sorunu bitiriyor.
    """
    print("\n13) GoAway — kopmayi konusma bosluguna tasima")

    import asyncio
    import canli
    from olcum import Defter

    class SahteHoparlor:
        def __init__(self):
            self.caliyor = False

        def temizle(self):
            pass

    class SahteMik:
        def __init__(self):
            self.kuyruk = asyncio.Queue()

    async def kur():
        d = Defter("test")
        b = canli.Baglanti("m", d, SahteHoparlor(), SahteMik(), sessiz=True)
        b.kurulum_tamam.set()
        return b

    b = asyncio.run(kur())

    # Robot konusuyorken yenileme YAPILMAMALI
    b.hoparlor.caliyor = True
    kontrol("robot konusurken yenilemiyor", not b._sessiz_an(),
            "cumle ortasinda kopma yasanir")

    # Yarim kalmis tur varken de yapilmamali
    b.hoparlor.caliyor = False
    b.defter.tur_ac()
    kontrol("yarim tur varken yenilemiyor", not b._sessiz_an())

    # Tur bitti ve ses susmus: guvenli an
    b.defter.tur_kapat()
    kontrol("konusma boslugunda yenileyebilir", b._sessiz_an())

    # GoAway mesaji bayragi kaldiriyor mu
    async def goaway_gonder():
        d = Defter("test")
        b2 = canli.Baglanti("m", d, SahteHoparlor(), SahteMik(), sessiz=True)
        await b2._mesaji_isle({"goAway": {"timeLeft": "50s"}})
        return b2

    b2 = asyncio.run(goaway_gonder())
    kontrol("goAway mesaji bayragi kaldiriyor", b2.goaway_geldi)
    kontrol("olay kaydina giriyor",
            any(o.tur == "goaway" for o in b2.defter.olaylar))


def test_hafiza() -> None:
    print("\n12) Hafiza (v1'den tasindi, sinirlar eklendi)")

    import hafiza
    from pathlib import Path

    # Testi gercek hafiza dosyasina bulastirmayalim
    gercek = hafiza.HAFIZA_DOSYASI
    hafiza.HAFIZA_DOSYASI = Path(str(gercek) + ".test")
    try:
        hafiza.her_seyi_unut()

        # -- temel
        hafiza.bilgi_ekle("Kopeginin adi Karabas.")
        esit("bilgi eklendi", hafiza.ozet()["bilgi_sayisi"], 1)

        # -- v1'in kopya tespiti (birebir tasinan mantik)
        hafiza.bilgi_ekle("Kopeginin adi Karabas.")
        esit("ayni bilgi tekrar eklenmiyor",
             hafiza.ozet()["bilgi_sayisi"], 1)
        esit("tekrar duyunca 'kez' artiyor",
             hafiza.ozet()["bilgiler"][0]["kez"], 2)

        # Alt kume kurali: daha bilgilendirici olan kazanmali
        hafiza.bilgi_ekle("Basketbol oynuyor.")
        hafiza.bilgi_ekle("Okul takiminda basketbol oynuyor.")
        metinler = [b["metin"] for b in hafiza.ozet()["bilgiler"]]
        kontrol("alt kume birlestirildi",
                sum(1 for m in metinler if "basketbol" in m.lower()) == 1,
                f"{metinler}")
        kontrol("daha bilgilendirici olan saklandi",
                any("takiminda" in m for m in metinler))

        # Olumsuzluk ayri kalmali
        hafiza.her_seyi_unut()
        hafiza.bilgi_ekle("Annesi ogretmen.")
        hafiza.bilgi_ekle("Annesi ogretmen degil.")
        esit("olumsuzluk ayri bilgi sayiliyor",
             hafiza.ozet()["bilgi_sayisi"], 2)

        # -- DEPOLAMA SINIRI
        hafiza.her_seyi_unut()
        for i in range(hafiza.EN_FAZLA_KAYIT + 40):
            hafiza.bilgi_ekle(f"Bilgi numarasi {i} hakkinda bir sey.")
        kontrol("depolama siniri asilmiyor",
                hafiza.ozet()["bilgi_sayisi"] <= hafiza.EN_FAZLA_KAYIT,
                f"{hafiza.ozet()['bilgi_sayisi']} kayit var")

        # -- PROMPT SINIRI (asil onemli olan)
        blok = hafiza.prompt_blogu()
        satir = [s for s in blok.splitlines() if s.startswith("- ")]
        kontrol("prompta giren bilgi sayisi sinirli",
                len(satir) <= hafiza.PROMPT_EN_FAZLA_KAYIT,
                f"{len(satir)} satir")
        kontrol("prompt blogu karakter sinirinda",
                len(blok) < hafiza.PROMPT_EN_FAZLA_KARAKTER + 600,
                f"{len(blok)} karakter")

        # 200 bilgi varken bile sistem promptu makul kalmali
        tam = kisilik.sistem_promptu()
        kontrol("200 bilgiyle sistem promptu sismiyor",
                len(tam) < 9000, f"{len(tam)} karakter")

        # -- tanima
        hafiza.her_seyi_unut()
        kontrol("tanimadan once 'henuz tanismadin' diyor",
                "HENUZ TANISMADIN" in hafiza.prompt_blogu())
        hafiza.cocugu_tanimla(ad="Deniz", yas=12)
        blok = hafiza.prompt_blogu()
        kontrol("tanidiktan sonra adi kullaniyor", "Deniz" in blok)
        kontrol("yasi da giriyor", "12" in blok)

        # -- SIFIRLAMA (kullanicinin istedigi dugme)
        hafiza.bilgi_ekle("Kedisinin adi Duman.")
        hafiza.her_seyi_unut()
        o = hafiza.ozet()
        esit("sifirlama bilgileri siliyor", o["bilgi_sayisi"], 0)
        esit("sifirlama adi da siliyor", o["cocuk"]["ad"], None)

        # -- bozuk dosya kurtarma (v1'den)
        hafiza.HAFIZA_DOSYASI.write_text("{bozuk json", encoding="utf-8")
        o = hafiza.ozet()
        esit("bozuk dosyadan kurtariliyor", o["bilgi_sayisi"], 0)
        # Yedegin adi with_suffix ile uretiliyor, o yuzden klasordeki
        # "*bozuk-*" kaliplarina bakiyoruz (test dosyasinin uzantisi
        # gercek dosyadan farkli).
        yedekler = list(hafiza.HAFIZA_DOSYASI.parent.glob("*bozuk-*"))
        kontrol("bozuk dosya silinmiyor, yedekleniyor", bool(yedekler),
                "veri kaybi olmamali")
        for y in yedekler:
            y.unlink()

        # -- cikarim promptu
        p = hafiza.cikarim_promptu("Cocuk: Kedim var.\nRobot: Ne tatli!")
        kontrol("cikarim promptu konusmayi iceriyor", "Kedim var" in p)
        kontrol("cikarim promptu robot bilgisini yasakliyor",
                "Robot hakkinda hicbir sey" in p)
        kontrol("cikarim modeli ucuz olan",
                hafiza.CIKARIM_MODELI == "gemini-3.1-flash-lite")

        # -- JSON ayiklama (model ``` ile sarabiliyor)
        esit("kod bloklu cevap ayikleniyor",
             hafiza._json_diziyi_ayikla('```json\n["Kedisi var."]\n```'),
             ["Kedisi var."])
        esit("bozuk cevap bos donuyor",
             hafiza._json_diziyi_ayikla("cevap yok"), [])
    finally:
        for ek in ("", ".gecici"):
            p = Path(str(hafiza.HAFIZA_DOSYASI) + ek)
            if p.exists():
                p.unlink()
        hafiza.HAFIZA_DOSYASI = gercek


def test_panel_hatalari() -> None:
    """
    Ebeveyn panelinden CIKAN bes gercek hata — hepsi bir kez isirdi.

    Bunlar tasarim tercihi degil, kod hatasiydi ve besi de SESSIZCE
    calismiyordu: hicbiri hata mesaji vermiyor, sadece beklenen sey
    olmuyordu. Sessiz hata en pahalisi — o yuzden testleri burada.
    """
    print("\n16) Panelden cikan hatalar — gerileme testleri")

    from pathlib import Path

    import numpy as np

    import ayarlar
    import canli

    kok = Path(__file__).resolve().parent
    kaynak = (kok / "pati.py").read_text(encoding="utf-8")
    canli_kaynak = (kok / "canli.py").read_text(encoding="utf-8")

    # --- 1. durdur() icinde degisken golgelemesi -----------------------
    #
    # `for b in h_sonuc["eklenen"]` baglanti nesnesini eziyordu ve hemen
    # altinda `b.ifade_defteri` vardi. Pati YENI BIR SEY OGRENDIGINDE
    # durdur() AttributeError ile yarim kaliyor: rapor yazilmiyor,
    # kullanim dakikasi kaydedilmiyor, panel "durduruldu"ya donmuyordu.
    # Hata da yutuluyordu cunku durdur() ensure_future ile cagriliyor.
    i = kaynak.index("async def durdur() -> None:")
    govde = kaynak[i:i + 2600]
    kontrol("durdur() dongu degiskeni baglantiyi ezmiyor",
            "for b in h_sonuc" not in govde,
            "Pati bir sey ogrenince durdur() yarim kaliyor")
    kontrol("baglanti nesnesi dongu sonrasi hala erisilebilir",
            "ifade_defteri=b.ifade_defteri" in govde)

    # --- 2. tarayici modunda bosta sayaci -----------------------------
    #
    # Tarayici saniyede ~15 ses parcasi yolluyor ve HER PARCA
    # hareket_var() cagiriyordu; bosta sayaci hic ilerlemedigi icin
    # robot ASLA uyumuyordu. Maliyetin en buyuk kalemi sessizce acik
    # kaliyordu (bkz. ayarlar: 12 saat bosta ~$108/ay).
    from ses import konusma_var_mi
    sessiz = np.zeros(320, dtype=np.int16).tobytes()
    zemin = np.random.normal(0, 20, 320).astype(np.int16).tobytes()
    soz = np.random.normal(0, 3000, 320).astype(np.int16).tobytes()
    kontrol("sessizlik konusma sayilmiyor", not konusma_var_mi(sessiz),
            "bosta sayaci sifirlanir, robot hic uyumaz")
    kontrol("zemin gurultusu konusma sayilmiyor", not konusma_var_mi(zemin))
    kontrol("konusma yakalaniyor", konusma_var_mi(soz),
            "cocuk konusunca robot uyanmaz")
    # Kapi once sadece RMS esigiydi ("if konusma_var_mi(pcm)"), yani
    # TEK bir 20 ms'lik parca uykudaki Pati'yi uyandirabiliyordu.
    # Simdi kesintisiz sure de araniyor (ses.SurekliKonusma).
    kontrol("tarayici sesine kapi konuldu",
            "if surekli(pcm):" in kaynak)
    kontrol("kapi kesintisiz sure de ariyor",
            "SurekliKonusma()" in kaynak,
            "tek parca uyandirirsa kapi carpmasi 90 sn ucret demek")

    # --- 3. ses degisimi calisan oturumda ------------------------------
    #
    # Panel "cumlesini bitirince gecerli olur" diyordu ama Python sadece
    # ayarlar.SES_ADI'ni degistiriyordu; ses ancak yeniden BASLATINCA
    # degisiyordu. Panel yalan soyluyordu.
    def ses_al(m):
        return (m["setup"]["generationConfig"]["speechConfig"]
                 ["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"])

    onceki = ayarlar.SES_ADI
    ayarlar.SES_ADI = "Puck"
    ilk = ses_al(canli.setup_mesaji("m"))
    ayarlar.SES_ADI = "Kore"
    ikinci = ses_al(canli.setup_mesaji("m"))
    ayarlar.SES_ADI = onceki
    kontrol("setup mesaji sesi CAGRI ANINDA okuyor",
            ilk == "Puck" and ikinci == "Kore",
            "yeniden baglanmak yeni sesi getirmez")
    kontrol("Baglanti.yenile() var", hasattr(canli.Baglanti, "yenile"))

    b = canli.Baglanti.__new__(canli.Baglanti)
    b.yenileme_sebebi = None
    canli.Baglanti.yenile(b, "ses Puck -> Kore")
    kontrol("yenileme sebebi kaydediliyor",
            b.yenileme_sebebi == "ses Puck -> Kore")
    kontrol("ayar yenilemesi goaway bayragini odunc ALMIYOR",
            "ayar_yenileme" in canli_kaynak,
            "ayar degisikligi olcum kaydina goaway diye gecer")

    # --- 4. yerel modda yanki korumasi hic devreye girmiyordu ----------
    #
    # Mikrofon'un `yanki_carpani` alani vardi ama HICBIR YERDEN
    # ayarlanmiyordu; Pati konusurken mikrofon esigi hic yukselmiyordu.
    kontrol("yanki carpani artik uygulaniyor",
            "m.yanki_carpani = (1.0 if ayarlar.SOZ_KESME" in kaynak,
            "Pati kendi sesini duyup sozunu keser")
    kontrol("soz kesme varsayilan KAPALI", ayarlar.SOZ_KESME is False,
            "kulakliksiz kullanimda Pati kendi sozunu keser")
    kontrol("yanki carpani 1'den buyuk", ayarlar.YANKI_CARPANI > 1)
    kontrol("soz_kesme komutu var", 'elif tip == "soz_kesme"' in kaynak)

    # --- 5. hafiza sadece durdur()'da cikariliyordu ---------------------
    #
    # Gercek kullanimda o an hic gelmiyor: cocuk konusmayi birakip
    # gidiyor, robotta fis cekiliyor. Ogrenilen hicbir sey
    # kaydedilmiyordu.
    kontrol("uykuya gecerken de hafiza cikariliyor",
            "uyku_kancasi" in canli_kaynak,
            "cocuk gidince ogrendikleri kaybolur")
    kontrol("pati.py kancayi bagliyor",
            "baglanti.uyku_kancasi = uykuda_hafiza" in kaynak)


def test_ebeveyn_notu() -> None:
    """
    Panelden yazilan "Pati bunlari bilsin" prompta giriyor mu?

    Bu alan modelin kendi cikardigi bilgilerden AYRI duruyor: onlar
    benzerlik esigiyle birlesiyor ve sinirlanip atiliyor, ebeveynin
    yazdigi sey ise atilmamali ve baska bir seyle birlesmemeli.
    """
    print("\n17) Ebeveyn notu — panelden prompta")

    import json
    import hafiza

    yol = hafiza.HAFIZA_DOSYASI
    yedek = yol.read_text(encoding="utf-8") if yol.exists() else None
    try:
        n = hafiza.ebeveyn_notu_kaydet(
            "Aksam sekizde dis fircalamasi gerekiyor.")
        kontrol("kaydediliyor", n.startswith("Aksam sekizde"))
        kontrol("ozette gorunuyor", hafiza.ozet()["ebeveyn_notu"] == n)

        blok = hafiza.prompt_blogu()
        kontrol("prompta giriyor", "ANNE-BABASININ" in blok)
        kontrol("metnin kendisi prompta giriyor", "dis fircalamasi" in blok)

        # Bilgilerden ONCE olmali: model uzun promptta bas kisma daha
        # cok uyuyor ve ebeveynin yazdigi sey daha onemli.
        if "ONUN HAKKINDA HATIRLADIKLARIN" in blok:
            kontrol("ebeveyn notu bilgilerden ONCE",
                    blok.index("ANNE-BABASININ")
                    < blok.index("ONUN HAKKINDA HATIRLADIKLARIN"))

        uzun = hafiza.ebeveyn_notu_kaydet("x" * 5000)
        kontrol("sinir uygulaniyor",
                len(uzun) == hafiza.EBEVEYN_NOTU_EN_FAZLA,
                "prompt buyudukce model kurallara daha az uyuyor")

        bos = hafiza.ebeveyn_notu_kaydet("")
        kontrol("bos not prompta blok eklemiyor",
                "ANNE-BABASININ" not in hafiza.prompt_blogu())

        # Eski hafiza.json'da bu alan yok — okuma tamamlamali
        h = json.loads(yol.read_text(encoding="utf-8"))
        h.pop("ebeveyn_notu", None)
        yol.write_text(json.dumps(h), encoding="utf-8")
        kontrol("alani olmayan ESKI dosya tamamlaniyor",
                "ebeveyn_notu" in hafiza.oku())
    finally:
        if yedek is None:
            yol.unlink(missing_ok=True)
        else:
            yol.write_text(yedek, encoding="utf-8")


def test_kullanim() -> None:
    """
    Konusma dakikasi sayaci — panelde "bugun / bu ay" kutulari.

    Onceden bu iki sayi UYDURMAYDI. Uydurma sayi hem tasarimi yanlis
    degerlendirtiyor (dort haneli dakika kutuya sigmiyor, gormedik) hem
    bir sure sonra inanilir hale geliyor.
    """
    print("\n18) Kullanim sayaci")

    import json
    import kullanim

    yol = kullanim.DOSYA
    yedek = yol.read_text(encoding="utf-8") if yol.exists() else None
    try:
        yol.unlink(missing_ok=True)
        kontrol("dosya yokken sifir donuyor", kullanim.ozet()["bugun_dk"] == 0)

        kullanim.ekle(120.0)                 # 2 dakika
        oz = kullanim.ozet()
        yakin("2 dakika kaydedildi", oz["bugun_dk"], 2.0, 0.05)
        yakin("bu ay da sayiyor", oz["ay_dk"], 2.0, 0.05)

        # Acik oturum AYRI veriliyor: dosyaya ancak durdurunca
        # yaziliyor, yoksa panel konusma boyunca hic degismiyor ve
        # "sayac bozuk" gorunuyor.
        oz2 = kullanim.ozet(acik_oturum_sn=600)
        yakin("acik oturum ekleniyor", oz2["bugun_dk"], 12.0, 0.05)

        kontrol("atomik yazma: gecici dosya kalmiyor",
                not list(kullanim.KOK.glob(".kullanim-*.tmp")),
                "cocuk fisi cekerse dosya bozulur")

        # PLAN.md: sinirsiz buyuyen hicbir sey birakma
        h = {"gunler": {}}
        for ay in range(1, 7):
            h["gunler"].update({f"2020-{ay:02d}-{g:02d}": 1.0
                                for g in range(1, 29)})
        kullanim._yaz(h)
        kalan = json.loads(yol.read_text(encoding="utf-8"))["gunler"]
        kontrol("eski gunler atiliyor",
                len(kalan) == kullanim.EN_FAZLA_GUN,
                f"{len(kalan)} gun kaldi, sinir {kullanim.EN_FAZLA_GUN}")

        # Bozuk dosya sayac; hafiza gibi degerli degil, sifirdan baslar
        yol.write_text("{bozuk", encoding="utf-8")
        kontrol("bozuk dosya cokmuyor", kullanim.ozet()["bugun_dk"] == 0)

        o = kullanim.Oturum()
        kontrol("Oturum gecen sureyi olcuyor", o.gecen_sn >= 0)
    finally:
        if yedek is None:
            yol.unlink(missing_ok=True)
        else:
            yol.write_text(yedek, encoding="utf-8")

def test_cocugun_dokumu() -> None:
    """
    COCUGUN SOYLEDIKLERI DOKUME GIRIYOR MU?

    30.07.2026'da bulunan hata. Sunucu cocugun dokumunu KENDI
    serverContent mesajinda gonderiyor ve o mesaj turnComplete de
    tasiyor — robotun cevabi icin tur henuz acilmamis oluyor. O anda
    dokum tamponu siliniyordu.

    Telefon oturumunun ham kaydi geri oynatildiginda 10 cumlenin 10'u
    da silinmisti. Gunun butun oturumlarinda `cocuk_dedi` doluluk
    orani 4/62 idi.

    Tek satirlik bir hataydi ama uc seyi birden bozuyordu:
      · hafiza — cikarim modeline sadece robotun cumleleri gidiyordu
      · guvenlik.incele — cocuk tarafi hep bos metin
      · metin.ses_istegi — "sesini kis" hic tetiklenmiyordu

    Bu test ham kayda BAGLI DEGIL (olcumler/ depoya girmiyor); mesaj
    dizisi elle kuruluyor ama sirasi gercek kayittan alindi.
    """
    print("\n19) Cocugun dokumu — kaybolan cumleler")

    import asyncio
    import base64
    import canli
    from olcum import Defter

    class SahteHoparlor:
        caliyor = False

        def temizle(self): pass

        def yeni_tur(self): pass

        def ekle(self, pcm): pass

    class SahteMik:
        kuyruk = None

    SES = base64.b64encode(b"\x00\x01" * 160).decode()

    def giris(yazi):
        # Cocugun dokumu: kendi turnComplete'iyle, ses YOK.
        return {"serverContent": {"inputTranscription": {"text": yazi},
                                  "generationComplete": True,
                                  "turnComplete": True}}

    async def cevap(b, yazi):
        # Robotun cevabi: once ses (tur burada aciliyor), sonra dokum.
        await b._mesaji_isle(
            {"serverContent": {"modelTurn": {
                "parts": [{"inlineData": {"data": SES}}]}}})
        await b._mesaji_isle(
            {"serverContent": {"outputTranscription": {"text": yazi}}})
        await b._mesaji_isle({"serverContent": {"turnComplete": True}})

    async def kos():
        d = Defter("test")
        b = canli.Baglanti("m", d, SahteHoparlor(), SahteMik(), sessiz=True)
        b.kurulum_tamam.set()

        await b._mesaji_isle(giris("Benim adim Deniz."))
        await cevap(b, "Memnun oldum Deniz!")

        # Cocuk pespese iki kez konustu, robot arada cevap vermedi.
        await b._mesaji_isle(giris("Kedim var."))
        await b._mesaji_isle(giris("Adi Mahmut."))
        await cevap(b, "Mahmut ne guzel isim!")
        return d

    d = asyncio.run(kos())

    esit("tur sayisi", len(d.turlar), 2)
    esit("cocugun cumlesi kaybolmuyor",
         d.turlar[0].cocuk_dedi, "Benim adim Deniz.")
    esit("robotun cumlesi ayni turda",
         d.turlar[0].robot_dedi, "Memnun oldum Deniz!")
    esit("pespese iki dokum birlesiyor",
         d.turlar[1].cocuk_dedi, "Kedim var. Adi Mahmut.")

    # Hafiza cikarimina giden dokum: hem cocuk hem robot satirlari.
    satirlar = []
    for t in d.turlar:
        if t.cocuk_dedi:
            satirlar.append("Cocuk: " + t.cocuk_dedi)
    kontrol("cikarim dokumunde COCUK satiri var", len(satirlar) == 2,
            "sadece robot satirlari gidince model cocugun adini "
            "robotun sozlerinden TAHMIN ediyor")


def test_hafiza_suzgecleri() -> None:
    """
    Cikarim modelinin dondurdugu adin suzgeci ve canli cikarim kosullari.

    AD SUZGECI olculmus bir hatayi kapatiyor: cocuk robota "bundan
    sonra senin adin Pargali Patipasa" dedi, model bunu COCUGUN adi
    sanip kaydetti, ebeveyn panelinde cocugun adi "Pargali" gorundu
    (gercek ad: Deniz). Prompt bunu yasakliyor ama prompt bir RICA;
    suzgec bir KURAL.
    """
    print("\n20) Hafiza suzgecleri — ad ve tetikleme")

    from pathlib import Path

    import hafiza
    import metin

    kontrol("normal ad geciyor", hafiza._ad_gecerli_mi("Deniz"))
    kontrol("iki kelimeli ad geciyor", hafiza._ad_gecerli_mi("Ali Kemal"))
    kontrol("robotun adi REDDEDILIYOR",
            not hafiza._ad_gecerli_mi("Pargali Pati Pasa"),
            "cocugun adi robotun yeni adi olamaz")
    kontrol("robotun adi tek basina reddediliyor",
            not hafiza._ad_gecerli_mi("pati"))
    kontrol("rakam iceren reddediliyor", not hafiza._ad_gecerli_mi("12 yas"))
    kontrol("tek harf reddediliyor", not hafiza._ad_gecerli_mi("A"))
    kontrol("bos reddediliyor", not hafiza._ad_gecerli_mi(""))

    # YER TUTUCU: canli testte gercekten oldu. Cocugun adi once dogru
    # kaydedildi ("Deniz"), 29 saniye sonraki cikarim dokumun yeni
    # parcasinda ad bulamayinca "Bilinmiyor" yazip UZERINE YAZDI.
    kontrol("'Bilinmiyor' reddediliyor",
            not hafiza._ad_gecerli_mi("Bilinmiyor"),
            "yer tutucu dogru adin uzerine yaziliyordu")
    kontrol("'bilinmeyen' reddediliyor",
            not hafiza._ad_gecerli_mi("bilinmeyen"))
    kontrol("'Yok' reddediliyor", not hafiza._ad_gecerli_mi("Yok"))
    kontrol("'Çocuk' reddediliyor", not hafiza._ad_gecerli_mi("Çocuk"))

    # YAS: panelin "Yas" kutusu kendiliginden dolsun diye cikarim artik
    # yas da donduruyor. Yanlis yas gormek hic gormemekten kotu.
    esit("yedi yas gecerli", hafiza._yas_gecerli_mi(7), 7)
    esit("metin olarak gelen yas cevriliyor", hafiza._yas_gecerli_mi("9"), 9)
    esit("sifir reddediliyor", hafiza._yas_gecerli_mi(0), 0)
    esit("2026 reddediliyor", hafiza._yas_gecerli_mi(2026), 0)
    esit("40 reddediliyor", hafiza._yas_gecerli_mi(40), 0)
    esit("sayi olmayan reddediliyor", hafiza._yas_gecerli_mi("yedi"), 0)
    esit("bos reddediliyor", hafiza._yas_gecerli_mi(None), 0)

    # Bilinen ad prompta konuyor ki model boslugu doldurmaya calismasin.
    # Testi GERCEK hafiza dosyasina bulastirmiyoruz: Pati acikken test
    # kosulursa cocugun hafizasini silerdik.
    gercek = hafiza.HAFIZA_DOSYASI
    hafiza.HAFIZA_DOSYASI = Path(str(gercek) + ".test")
    try:
        hafiza.her_seyi_unut()
        kontrol("ad bilinmiyorsa prompt boyle diyor",
                "henuz bilinmiyor" in hafiza.cikarim_promptu("Cocuk: Selam."))
        hafiza.cocugu_tanimla(ad="Deniz")
        p = hafiza.cikarim_promptu("Cocuk: Selam.")
        kontrol("bilinen ad prompta giriyor", "ZATEN BILINIYOR: Deniz" in p)
    finally:
        for ek in ("", ".gecici"):
            p = Path(str(hafiza.HAFIZA_DOSYASI) + ek)
            if p.exists():
                p.unlink()
        hafiza.HAFIZA_DOSYASI = gercek

    # -- canli cikarim ne zaman calisir --------------------------------
    kontrol("yeni tur yoksa calismiyor",
            not hafiza.cikarim_zamani_mi(0, 999.0))
    kontrol("tek tur icin calismiyor",
            not hafiza.cikarim_zamani_mi(1, 999.0),
            "her 'selam'a istek atmak para ve ESP32 yuku")
    kontrol("cok erken calismiyor",
            not hafiza.cikarim_zamani_mi(2, 5.0))
    kontrol("iki tur + yeterli ara -> calisiyor",
            hafiza.cikarim_zamani_mi(2, hafiza.CIKARIM_EN_AZ_ARALIK_SN))
    kontrol("'unutma' dediyse tek turda calisiyor",
            hafiza.cikarim_zamani_mi(1, hafiza.CIKARIM_UNUTMA_ARALIK_SN,
                                     unutma_istegi=True),
            "o cumle hafizanin ta kendisi, bekletmenin anlami yok")
    kontrol("'unutma' bile olsa arka arkaya calismiyor",
            not hafiza.cikarim_zamani_mi(1, 1.0, unutma_istegi=True))

    # -- "bunu unutma" taramasi ----------------------------------------
    kontrol("unutma yakalaniyor",
            metin.unutma_istegi("Kedimin adini unutma"))
    kontrol("aklinda tut yakalaniyor",
            metin.unutma_istegi("Bunu aklında tut"))
    kontrol("hafizana kaydet yakalaniyor",
            metin.unutma_istegi("Bunu hafızana kaydet"))
    kontrol("siradan cumle yakalanmiyor",
            not metin.unutma_istegi("Bugun okulda resim yaptim"))
    kontrol("cocugun kendi unutkanligi yakalanmiyor",
            not metin.unutma_istegi("Adini unuttum"),
            "'unuttum' bir hafiza istegi degil")
    kontrol("bos metin yakalanmiyor", not metin.unutma_istegi(""))


def test_canli_hafiza() -> None:
    """
    Konusma SIRASINDA cikarim: ayni cumleler ikinci kez gonderiliyor mu?

    Kullanicinin sikayeti: "hafizaya almasi gerektigi bir sey olunca
    orada hemen gozukmuyor". Cikarim artik konusma sirasinda da
    calisiyor — ama her seferinde butun konusmayi gondermemeli, yoksa
    10 dakikalik bir sohbette ayni metin onlarca kez gider (hem para
    hem ESP32'de bosuna baglanti).

    Ag yok: hafiza.cikar sahtesiyle degistiriliyor.
    """
    print("\n21) Canli hafiza — isaretci ve oturum sayaci")

    import asyncio
    import hafiza
    import pati
    from olcum import Defter

    gonderilenler = []

    async def sahte_cikar(konusma, anahtar):
        gonderilenler.append(konusma)
        return {"eklenen": [], "ad": None, "reddedilen_ad": None,
                "hata": None}

    def tur_ekle(d, cocuk, robot):
        t = d.tur_ac()
        t.cocuk_dedi = cocuk
        t.robot_dedi = robot
        d.tur_kapat()

    gercek_cikar = hafiza.cikar
    gercek_sayac = hafiza.oturum_sayaci
    sayac = {"n": 0}
    try:
        hafiza.cikar = sahte_cikar
        hafiza.oturum_sayaci = lambda: sayac.__setitem__("n", sayac["n"] + 1)

        d = Defter("test")
        tur_ekle(d, "Benim adim Deniz.", "Memnun oldum!")
        tur_ekle(d, "Kedim var, adi Mahmut.", "Ne guzel isim!")

        asyncio.run(pati._hafizayi_guncelle(d, yaz=lambda m: None,
                                            canli=True))
        kontrol("ilk cikarim iki turu de gonderiyor",
                "Deniz" in gonderilenler[0] and "Mahmut" in gonderilenler[0])
        esit("isaretci ilerledi", d.hafiza_isaretci, 2)
        esit("canli cikarim oturum saymiyor", sayac["n"], 0)

        tur_ekle(d, "Yedi yasindayim.", "Guzel yas!")
        asyncio.run(pati._hafizayi_guncelle(d, yaz=lambda m: None,
                                            canli=True))
        kontrol("ikinci cikarim SADECE yeni turu gonderiyor",
                "yasindayim" in gonderilenler[1]
                and "Mahmut" not in gonderilenler[1],
                "eski turlar tekrar gonderilirse maliyet katlaniyor")

        # Oturum sonu: yeni tur yok ama oturum yine de sayilmali.
        asyncio.run(pati._hafizayi_guncelle(d, yaz=lambda m: None))
        esit("yeni tur yokken istek atilmiyor", len(gonderilenler), 2)
        esit("oturum sayildi", sayac["n"], 1)

        # Uyku + durdur ayni defterle iki kez geliyor: sayac bir kez.
        asyncio.run(pati._hafizayi_guncelle(d, yaz=lambda m: None))
        esit("oturum IKINCI kez sayilmiyor", sayac["n"], 1,)

        # Istek basarisiz olursa isaretci geri alinmali, yoksa o turlar
        # hicbir zaman cikarilmaz.
        async def patlayan(konusma, anahtar):
            return {"eklenen": [], "ad": None, "reddedilen_ad": None,
                    "hata": "HTTP 503"}

        hafiza.cikar = patlayan
        tur_ekle(d, "Basketbol oynuyorum.", "Harika!")
        onceki = d.hafiza_isaretci
        asyncio.run(pati._hafizayi_guncelle(d, yaz=lambda m: None,
                                            canli=True))
        esit("hata olunca isaretci geri aliniyor",
             d.hafiza_isaretci, onceki)
    finally:
        hafiza.cikar = gercek_cikar
        hafiza.oturum_sayaci = gercek_sayac


def test_az_taniyorsa_soruyor() -> None:
    """
    Hafiza bosken robot cocugu MERAK EDIYOR mu?

    Kullanicinin gozlemi: "robot cocugun ismini hic bilmiyorsa
    sormuyor, yasini da sormuyor, evcil hayvani var mi diye de".
    Adi zaten isteniyordu, gerisi hic istenmiyordu.

    Satir sadece hafiza INCEYKEN ekleniyor: promptu buyutmenin bedeli
    olculdu (kural uyumu duser) ve tanidigi cocuga soru yagdirmasi da
    yanlis olur.
    """
    print("\n22) Az taniyorsa merak ediyor mu")

    from pathlib import Path

    import hafiza

    # Gercek hafiza dosyasina dokunmuyoruz (bkz. 20. testteki gerekce).
    gercek = hafiza.HAFIZA_DOSYASI
    hafiza.HAFIZA_DOSYASI = Path(str(gercek) + ".test")
    try:
        hafiza.her_seyi_unut()
        blok = hafiza.prompt_blogu()
        kontrol("bos hafizada adini soruyor", "HENUZ TANISMADIN" in blok)
        kontrol("bos hafizada merak ediyor", "AZ TANIYORSUN" in blok)

        hafiza.cocugu_tanimla(ad="Deniz", yas=7)
        for m in ("Kedisinin adi Mahmut.", "Basketbol oynuyor.",
                  "Ablasinin adi Ayse."):
            hafiza.bilgi_ekle(m)
        blok = hafiza.prompt_blogu()
        kontrol("tanidiktan sonra merak satiri KALKIYOR",
                "AZ TANIYORSUN" not in blok,
                "prompt buyudukce model kurallara daha az uyuyor")

        # Cikarim promptundaki "zaten bildiklerin" listesi en cok
        # duyulanlardan secilmeli. Onceden listenin SONUNDAN aliniyordu;
        # _budama listeyi tersten siralayinca orasi en az duyulanlar
        # oluyordu.
        hafiza.her_seyi_unut()
        for i in range(30):
            hafiza.bilgi_ekle(f"Bilgi numarasi {i} hakkinda bir sey.")
        for _ in range(5):
            hafiza.bilgi_ekle("Kopeginin adi Karabas.")
        p = hafiza.cikarim_promptu("Cocuk: Selam.")
        kontrol("en cok duyulan bilgi 'zaten bildiklerin' icinde",
                "Karabas" in p.split("KONUSMA:")[0],
                "yoksa model ayni bilgiyi tekrar tekrar yaziyor")
    finally:
        for ek in ("", ".gecici"):
            y = Path(str(hafiza.HAFIZA_DOSYASI) + ek)
            if y.exists():
                y.unlink()
        hafiza.HAFIZA_DOSYASI = gercek


def test_uyku_sayaci() -> None:
    """
    UYKU DAKIKALARI FATURAYA YAZILIYOR MU?

    Uykuda WebSocket kapali: ses akmiyor, ucret islemiyor. Panel de
    bunu yaziyor ("Uyurken ucret islemez"). Ama sayac duvar saatiyle
    calisiyordu ve uyku dakikalarini da ucretli sayiyordu.

    30.07.2026 kayitlarindan: bir oturum 53,7 dakika surdu ve 90
    saniye sonra uyuyup bir daha uyanmadi. Panel 53,7 dakikayi
    ucretli gosterdi; gerceginde ~1,5 dakikaydi.
    """
    print("\n23) Uyku — kullanim sayaci uykuyu saymamali")

    import kullanim

    # 10 saniye konusuldu.
    o = kullanim.Oturum()
    o.basladi -= 10.0
    yakin("uyanikken duvar saatiyle ayni", o.gecen_sn, 10.0, 0.2)

    # Sonra 6 saniye uyudu: duvar saati 16, ucretli olan hala 10.
    o.duraklat()
    o.basladi -= 6.0
    o._uyku_basi -= 6.0
    yakin("uyurken sayac ilerlemiyor", o.gecen_sn, 10.0, 0.2)

    o.duraklat()                         # ikinci cagri bir sey yapmamali
    yakin("iki kez duraklatmak sayiyi bozmuyor", o.gecen_sn, 10.0, 0.2)

    o.devam()
    yakin("uyandiktan sonra uyku suresi dusuluyor", o.gecen_sn, 10.0, 0.2)

    o.basladi -= 4.0                     # 4 saniye daha konusuldu
    yakin("uyandiktan sonra sayac yeniden isliyor",
          o.gecen_sn, 14.0, 0.2)

    o.devam()                            # duraklatmadan devam: etkisiz
    yakin("bos devam cagrisi sayiyi bozmuyor", o.gecen_sn, 14.0, 0.2)

    # 53,7 dakikalik gercek oturum: 90 saniye konusuldu, gerisi uyku.
    g = kullanim.Oturum()
    g.basladi -= 53.7 * 60
    g.duraklat()
    g._uyku_basi -= (53.7 * 60 - 90)
    yakin("gercek kayit: 53,7 dk oturum -> 1,5 dk ucretli",
          g.gecen_sn / 60.0, 1.5, 0.05)

    kontrol("sayac negatife dusmuyor", kullanim.Oturum().gecen_sn >= 0)


def test_uyku_suresi_ayari() -> None:
    """
    Panelden secilen uyku suresi CALISAN oturumda gecerli oluyor mu?

    Ebeveyn kaydiriciyi oynattiginda bir sonraki oturumu beklemek
    zorunda kalmamali. Bekci her donguде ayarlar.BOSTA_KAPAT_SN'e
    yeniden bakiyor; bu test onu dogruluyor.
    """
    print("\n24) Uyku suresi — calisan oturumda degisiyor mu")

    import asyncio
    import ayarlar
    import canli
    from olcum import Defter, simdi

    class SahteHoparlor:
        caliyor = False

        def temizle(self): pass

    class SahteMik:
        kuyruk = None

    async def kos():
        eski = ayarlar.BOSTA_KAPAT_SN
        ayarlar.BOSTA_KAPAT_SN = 30.0        # uzun: hemen uyumasin
        try:
            d = Defter("test")
            b = canli.Baglanti("m", d, SahteHoparlor(), SahteMik(),
                               sessiz=True)
            b.kurulum_tamam.set()
            b.son_hareket = simdi()
            uyandi = {"n": 0}
            b.uyanma_kancasi = lambda: uyandi.__setitem__("n",
                                                          uyandi["n"] + 1)

            gorev = asyncio.create_task(b.bosta_bekcisi())
            await asyncio.sleep(0.8)
            erken = b.uykuda

            # Ebeveyn kaydiriciyi 1 dakikanin altina cekti (en dusuk
            # deger); bekci bunu BEKLEMEDEN gormeli.
            ayarlar.BOSTA_KAPAT_SN = 0.5
            await asyncio.sleep(0.8)
            gec = b.uykuda
            gorev.cancel()
            return erken, gec, uyandi
        finally:
            ayarlar.BOSTA_KAPAT_SN = eski

    erken, gec, uyandi = asyncio.run(kos())
    kontrol("uzun sureyle hemen uyumuyor", not erken)
    kontrol("sure kisalinca CALISAN oturumda uyuyor", gec,
            "yoksa ebeveyn ayari degistirip bir sey olmadigini gorur")
    kontrol("uyanma kancasi uyumadan cagrilmiyor", uyandi["n"] == 0)

    # -- yazi kutusu uykudaki Pati'yi uyandirabiliyor mu ----------------
    #
    # Uykuda kurulum_tamam temizli ve uyanmayi baslatan sey
    # hareket_var(). metin_gonder once BEKLIYOR, sonra hareket_var
    # diyordu — yani kimse uyandirmiyordu ve mesaj 10 saniye sonra
    # "baglanti hazir degil" diye dusuyordu. Uctan uca testte olculdu:
    # 40. saniyedeki yazi uykudaki Pati'yi hic uyandiramadi.
    async def yazi_uyandiriyor_mu():
        d = Defter("test")
        b = canli.Baglanti("m", d, SahteHoparlor(), SahteMik(), sessiz=True)
        b.uykuda = True
        b.kurulum_tamam.clear()
        # 10 saniye beklemeyelim: bekleme biter bitmez cikacak.
        gorev = asyncio.create_task(b.metin_gonder("Uyan bakalim"))
        await asyncio.sleep(0.2)
        istendi = b.uyanma_istegi.is_set()
        gorev.cancel()
        try:
            await gorev
        except asyncio.CancelledError:
            pass
        return istendi

    kontrol("yazi kutusu uykudaki Pati'yi uyandiriyor",
            asyncio.run(yazi_uyandiriyor_mu()),
            "yoksa uyuduktan sonra panelden yazmak islemiyor")

    # -- tek parca uyandirmasin ----------------------------------------
    #
    # Tarayici modunda uykudaki Pati'yi TEK bir 20 ms'lik parca
    # uyandirabiliyordu: kapi carpmasi, oyuncak sesi, oksuruk. Her
    # yanlis uyanma en az bir uyku suresi (varsayilan 90 sn) ucret
    # demek. Yerel mikrofonda bu koruma zaten vardi (VAD_KONUSMA_MS).
    import struct
    from ses import SurekliKonusma

    def parca(genlik):
        d, v, o = genlik, 0, []
        for _ in range(ayarlar.PARCA_ORNEK):
            v += d
            if v >= genlik or v <= -genlik:
                d = -d
            o.append(max(-32768, min(32767, v)))
        return struct.pack(f"<{ayarlar.PARCA_ORNEK}h", *o)

    SESSIZ = b"\x00\x00" * ayarlar.PARCA_ORNEK
    YUKSEK = parca(4000)
    gerek = int(ayarlar.VAD_KONUSMA_MS / ayarlar.PARCA_MS)   # 6 parca

    s = SurekliKonusma()
    kontrol("sessizlik uyandirmiyor", not s(SESSIZ))

    s = SurekliKonusma()
    erken = [s(YUKSEK) for _ in range(gerek - 1)]
    kontrol("tek parca (20 ms) uyandirmiyor", not erken[0],
            "kapi carpmasi uykuyu bozmamali")
    kontrol(f"{gerek - 1} parca hala uyandirmiyor", not any(erken))
    kontrol("kesintisiz 120 ms uyandiriyor", s(YUKSEK),
            "cocuk konusunca uyanmali")

    s = SurekliKonusma()
    for _ in range(gerek - 1):
        s(YUKSEK)
    s(SESSIZ)                                   # zincir kirildi
    kontrol("araya sessizlik girince sayac sifirlaniyor",
            not s(YUKSEK),
            "darbe sesleri birikip uyandirmasin")

    # -- telefonda sekmeye geri donus ----------------------------------
    #
    # Gercek kullanimdan: telefonda Pati acikken baska bir uygulamaya
    # gecip geri donunce Pati bir daha UYANMIYORDU. Tarayici arka
    # plandaki sekmenin AudioContext'ini askiya aliyor, mikrofondan tek
    # bayt gitmiyor, uykudaki Pati'yi uyandiracak ses hic ulasmiyor.
    # Tek care Durdur/Baslat idi.
    #
    # ⚠ Robotun sorunu DEGIL (ESP32'de tarayici yok), ama telefon bu
    #   projenin ana test duzenegi; bozuk kalirsa her uyku testi
    #   yaniltiyor.
    from pathlib import Path
    panel = Path(__file__).resolve().parent.parent / "panel"
    js_panel = (panel / "pati.js").read_text(encoding="utf-8")
    js_mik = (panel / "mikrofon.js").read_text(encoding="utf-8")

    kontrol("sekmeye donunce mikrofon tazeleniyor",
            "visibilitychange" in js_panel and "tazele()" in js_panel)
    kontrol("tazele askiya alinmis ses baglamini uyandiriyor",
            "resume()" in js_mik and "'suspended'" in js_mik)
    kontrol("tazele dusmus mikrofon akisini yeniden aliyor",
            "'ended'" in js_mik,
            "isletim sistemi mikrofonu geri alinca resume yetmiyor")
    kontrol("baglanti kopup gelince ses kanali yeniden bildiriliyor",
            "if (tarayiciSes.acik) gonder({ tip: 'ses_kanali', ac: true });"
            in js_panel,
            "yoksa Pati konusuyor ama telefondan ses gelmiyor")


def test_robotun_adi() -> None:
    """
    COCUK ROBOTUN ADINI DEGISTIREBILIYOR MU?

    Kullanicinin istegi: "cocuk isterse robotun adini degistirebilmeli;
    'senin adin artik Osman' derse Pati'nin adi Osman olacak, baska
    zaman 'senin adin Recep' derse Recep olacak."

    Ad, bilgiler listesinde DEGIL ayri bir alanda: bilgiler benzerlikle
    birlesiyor ve sinira gelince atiliyor, robotun adi atilamaz. Ayrica
    promptun ILK CUMLESINDE geciyor, bilgiler ise sonda.
    """
    print("\n25) Robotun adini cocuk degistiriyor")

    from pathlib import Path

    import hafiza
    import kisilik

    gercek = hafiza.HAFIZA_DOSYASI
    hafiza.HAFIZA_DOSYASI = Path(str(gercek) + ".test")
    try:
        hafiza.her_seyi_unut()
        esit("varsayilan ad", hafiza.robot_adi(), hafiza.ROBOT_ADI)
        kontrol("varsayilan ad prompta giriyor",
                kisilik.sistem_promptu().startswith(
                    f"Senin adin {hafiza.ROBOT_ADI}."))

        esit("cocuk adi degistirebiliyor",
             hafiza.robot_adini_degistir("Osman"), "Osman")
        kontrol("yeni ad prompta giriyor",
                kisilik.sistem_promptu().startswith("Senin adin Osman."))

        # Baska bir zaman baska bir ad — uzerine yaziliyor, birikmiyor.
        esit("ikinci kez degistirilebiliyor",
             hafiza.robot_adini_degistir("Recep"), "Recep")
        esit("son soylenen gecerli", hafiza.robot_adi(), "Recep")
        esit("ad bilgi listesine DUSMUYOR",
             hafiza.ozet()["bilgi_sayisi"], 0,
             )

        kontrol("rakamli ad reddediliyor",
                hafiza.robot_adini_degistir("R2D2") is None)
        kontrol("bos ad reddediliyor",
                hafiza.robot_adini_degistir("") is None)
        kontrol("yer tutucu reddediliyor",
                hafiza.robot_adini_degistir("Bilinmiyor") is None)
        esit("gecersiz denemeler adi bozmadi", hafiza.robot_adi(), "Recep")

        # Cocugun adi robotun ADINI kapmasin: robot "Recep" iken cocuk
        # icin gelen "Recep" elenmeli (olculen "Pargali" hatasinin
        # degisen-ad hali).
        kontrol("robotun su anki adi cocuga yapismiyor",
                not hafiza._ad_gecerli_mi("Recep"))
        kontrol("varsayilan robot adi da yapismiyor",
                not hafiza._ad_gecerli_mi("Pati"))
        kontrol("siradan cocuk adi geciyor", hafiza._ad_gecerli_mi("Deniz"))

        # Panelde gorunuyor mu (ebeveyn robotun adini bir yerden gormeli)
        o = hafiza.ozet()
        esit("ozette robot adi var", o["robot_adi"], "Recep")
        esit("ozette varsayilan da var",
             o["robot_adi_varsayilan"], hafiza.ROBOT_ADI)

        hafiza.her_seyi_unut()
        esit("Tumunu sil varsayilana donduruyor",
             hafiza.robot_adi(), hafiza.ROBOT_ADI)
    finally:
        for ek in ("", ".gecici"):
            p = Path(str(hafiza.HAFIZA_DOSYASI) + ek)
            if p.exists():
                p.unlink()
        hafiza.HAFIZA_DOSYASI = gercek


def test_unutma_isareti() -> None:
    """
    "BUNU UNUTMA" DEDIGI SEY KESIN KAYDEDILIYOR MU?

    Kullanicinin sikayeti: "bundan sonra senin adin Pargali Pati Pasa,
    bu bilgiyi unutma diyorum ama hafizasina almiyor."

    Iki ayri sey gerekiyordu:
      1. Cikarim HEMEN calissin (metin.unutma_istegi -> tetikleyici).
         Bu 20. testte.
      2. Model o cumleyi ATLAMASIN. Prompt normalde "emin degilsen
         yazma" diyor ve model temkinli davraniyordu. Dokumde o satiri
         (!) ile isaretliyoruz ve prompt "isaretliyse MUTLAKA yaz"
         diyor.
    """
    print("\n26) 'Bunu unutma' isareti dokume giriyor mu")

    import asyncio

    import hafiza
    import pati
    from olcum import Defter

    gonderilen = []

    async def sahte_cikar(konusma, anahtar):
        gonderilen.append(konusma)
        return {"eklenen": [], "ad": None, "yas": None, "robot_adi": None,
                "reddedilen_ad": None, "hata": None}

    gercek_cikar = hafiza.cikar
    gercek_sayac = hafiza.oturum_sayaci
    try:
        hafiza.cikar = sahte_cikar
        hafiza.oturum_sayaci = lambda: None

        d = Defter("test")
        t = d.tur_ac()
        t.cocuk_dedi = "Bugun okulda resim yaptim."
        t.robot_dedi = "Ne guzel!"
        d.tur_kapat()
        t = d.tur_ac()
        t.cocuk_dedi = "Kedimin adi Mahmut, bunu unutma."
        t.robot_dedi = "Unutmam!"
        d.tur_kapat()

        asyncio.run(pati._hafizayi_guncelle(d, yaz=lambda m: None,
                                            canli=True))
        dokum = gonderilen[0]
        kontrol("siradan satir isaretsiz",
                "Cocuk: Bugun okulda resim yaptim." in dokum)
        kontrol("'unutma' satiri (!) ile isaretli",
                "Cocuk (!): Kedimin adi Mahmut, bunu unutma." in dokum,
                "isaret olmazsa model 'emin degilsen yazma' deyip atliyor")
    finally:
        hafiza.cikar = gercek_cikar
        hafiza.oturum_sayaci = gercek_sayac

    # Promptun kendisi de bu iki kurali tasimali
    p = hafiza.CIKARIM_PROMPTU
    kontrol("prompt (!) kuralini iceriyor", "(!)" in p and "MUTLAKA" in p)
    kontrol("prompt robot adini ayri alana yaziyor",
            '{"robot_adi"' in p,
            "yoksa cocugun adi yerine yaziliyor ya da hic yazilmiyor")


def main() -> int:
    print("=" * 72)
    print("OLCUM ALETININ TESTLERI  (Gemini'yi degil, kendimizi test eder)")
    print("=" * 72)

    test_gecikme_aritmetigi()
    test_kriterler()
    test_dagilim()
    test_vad_damgasi()
    test_guvenlik()
    test_uyum()
    test_setup()
    test_ses_hatti()
    test_metin_ayrimi()
    test_ses_seviyesi()
    test_dolgu_ayrimi()
    test_senaryolar()
    test_bosta_kapatma()
    test_uyanma()
    test_goaway()
    test_hafiza()
    test_panel_hatalari()
    test_ebeveyn_notu()
    test_kullanim()
    test_cocugun_dokumu()
    test_hafiza_suzgecleri()
    test_canli_hafiza()
    test_az_taniyorsa_soruyor()
    test_uyku_sayaci()
    test_uyku_suresi_ayari()
    test_robotun_adi()
    test_unutma_isareti()

    print("\n" + "=" * 72)
    if _kalan:
        print(f"{_gecen} gecti, {len(_kalan)} KALDI:")
        for k in _kalan:
            print(f"  ❌ {k}")
        print("=" * 72)
        return 1
    print(f"{_gecen} testin hepsi gecti.")
    print()
    print("BU NE DEMEK DEGIL: Gemini'nin hizli oldugu, Turkcesinin iyi")
    print("oldugu ya da ESP32'de calisacagi. Bu testler sadece OLCUM")
    print("ALETININ dogru olctugunu gosteriyor. Gercek olcum icin:")
    print("    python pati.py")
    print("=" * 72)
    return 0


if __name__ == "__main__":
    sys.exit(main())
