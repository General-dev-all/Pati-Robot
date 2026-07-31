# -*- coding: utf-8 -*-
"""
Gecikme olcumu ve rapor.

BU DOSYA v1'IN BATTIGI YERE CEVAP OLARAK YAZILDI.

PLAN.md'te v1'in dort hatasi sayiliyor. Ucu dogrudan olcumle ilgili:
"yanlis seyi olctum", "parcalari ayri olctum", "hedef cihazda hic
denemedim". Bu modul bunlarin uceyu de yapisal olarak engelliyor:

  1. TEK BIR SAYI YOK. Her tur icin gecikme UC parcaya bolunuyor ve
     hangisinin ESP32'ye TASINDIGI, hangisinin TASINMADIGI ayrica
     yaziliyor (PLAN.md'teki tablo). Boylece "PC'de 1.2 sn cikti,
     ESP32'de de oyle olur" gibi bir cumle kurmak imkansiz hale
     geliyor - rapor sana hangi parcanin degisecegini soyluyor.

  2. ORTALAMA TEK BASINA YAZILMIYOR. Ortalama, medyan, en kotu ve
     %90'lik dilim birlikte basiliyor. v1'de ortalama iyi gorunup
     gercek kullanimda en kotu durum yasandi.

  3. KRITER KODUN ICINDE. ayarlar.py'deki esikler PLAN.md'ten birebir
     alindi ve rapor "GECER/SINIRDA/KALIR" diye kendisi yaziyor.
     Olcumden sonra "iyi sayilir mi" diye tartisma yok.

  4. TAHMIN ile OLCUM ayri basiliyor. PLAN.md'nin tahmin tablosu
     raporun icinde duruyor ve olculen degerle yan yana gosteriliyor.
"""

from __future__ import annotations

import json
import statistics
import time
from dataclasses import dataclass, field, asdict
from datetime import datetime
from pathlib import Path

import ayarlar


def simdi() -> float:
    """
    Milisaniye hassasiyetinde, geri gitmeyen saat.

    time.time() DEGIL: sistem saati NTP ile duzeltilince geri gidebiliyor
    ve gecikme olcumu negatif cikiyor. perf_counter monoton.
    """
    return time.perf_counter()


def ms(baslangic: float, bitis: float) -> float:
    return (bitis - baslangic) * 1000.0


# ---------------------------------------------------------------------------
# Tek bir konusma turu
# ---------------------------------------------------------------------------

@dataclass
class Tur:
    """
    Bir tur = cocuk konustu, robot cevap verdi.

    Zaman damgalarinin hepsi perf_counter saniyesi. Hicbiri "yaklasik"
    degil - her biri kodun belirli bir satirinda, olayin oldugu anda
    aliniyor.
    """
    no: int

    # "ses"  = gercek mikrofon turu -> PLAN.md kriteri BUNA bakiyor
    # "metin" = metinle surulen tur -> VAD devrede DEGIL, kriter sayilmaz
    #
    # Ikisi ayni listede tutuluyor ama istatistikte KARISTIRILMIYOR.
    # Karistirilsaydi metin turlerinin hizi ortalamayi dusurup kriteri
    # haksiz yere "gecer" gosterirdi — v1'in "yanlis seyi olctum"
    # hatasinin bu projedeki en olasi tekrari tam olarak bu olurdu.
    kaynak: str = "ses"

    # Dolgu turu mu? (senaryolar.DOLGU: "Anlat anlat.", "Devam et bakalim.")
    #
    # Bunlar oturumu ayakta tutmak icin var, cocugun soracagi sey degil.
    # Cocuk hicbir sey sormayinca model sohbeti surdurmek icin cevabi
    # soruyla bitiriyor: 13 dakikalik kosuda 32 dolgu turunun 32'si
    # soruyla bitti, gercek senaryolarda oran 11/25'ti. Ikisi ayni
    # havuzda sayilinca uyum %19 cikti ve rapor "KALIR" dedi — olculen
    # sey modelin davranisi degil, olcum aletinin kendi urettigi girdiydi.
    #
    # GECIKME istatistigine giriyorlar (metin turu olarak; olculen sey
    # gercek), UYUM sayimina girmiyorlar.
    dolgu: bool = False

    # Cocuk tarafi (yerel mikrofon)
    t_konusma_basi: float | None = None
    t_sustu: float | None = None          # <-- OLCUMUN SIFIR NOKTASI

    # Robot tarafi
    t_ilk_paket: float | None = None      # ilk ses baytinin SOKETTEN gelisi
    t_ilk_hoparlor: float | None = None   # ilk ses baytinin HOPARLORE gidisi
    t_uretim_bitti: float | None = None   # generationComplete
    t_tur_bitti: float | None = None      # turnComplete

    # Sozunu kesme
    t_kesme_konusma: float | None = None  # cocuk robotun uzerine konustu
    t_kesme_onay: float | None = None     # sunucudan "interrupted" geldi
    kesildi: bool = False

    # Icerik (dokum acikken dolar)
    cocuk_dedi: str = ""
    robot_dedi: str = ""

    # Bu turda kac bayt ses geldi (ses uzunlugu hesabi icin)
    ses_bayt: int = 0

    def gecikme_ms(self) -> float | None:
        """
        KRITER BU: cocuk sustu -> ilk ses hoparlorden cikti.

        PLAN.md: "Cocuk sustu -> ilk ses  <=1.5 sn gecer".
        """
        if self.t_sustu is None or self.t_ilk_hoparlor is None:
            return None
        return ms(self.t_sustu, self.t_ilk_hoparlor)

    def ag_gecikmesi_ms(self) -> float | None:
        """
        Cocuk sustu -> ilk ses PAKETI soketten geldi.

        PLAN.md'teki tabloya gore bu parca ESP32'ye AYNEN GECER:
        Gemini'nin "sustu" karari + ag gidis-donusu + modelin dusunup
        ses uretmesi. Uc kalemin de ESP32'de degismesi icin sebep yok.
        """
        if self.t_sustu is None or self.t_ilk_paket is None:
            return None
        return ms(self.t_sustu, self.t_ilk_paket)

    def yerel_oynatma_ms(self) -> float | None:
        """
        Ilk paket geldi -> hoparlore verildi.

        BU PARCA ESP32'YE GECMEZ. PC'de PortAudio + Windows ses yigini
        var; ESP32'de I2S DMA var. Ikisi de farkli. Raporda ayri
        duruyor ki "PC'de olctuk, tamamdir" denmesin.
        """
        if self.t_ilk_paket is None or self.t_ilk_hoparlor is None:
            return None
        return ms(self.t_ilk_paket, self.t_ilk_hoparlor)

    def kesme_ms(self) -> float | None:
        """PLAN.md: "Sozunu kesme tepkisi <=0.5 sn gecer"."""
        if self.t_kesme_konusma is None or self.t_kesme_onay is None:
            return None
        return ms(self.t_kesme_konusma, self.t_kesme_onay)

    def ses_uzunlugu_sn(self) -> float:
        bayt_sn = ayarlar.CIKIS_HZ * ayarlar.ORNEK_GENISLIK
        return self.ses_bayt / bayt_sn if bayt_sn else 0.0

    def tamam_mi(self) -> bool:
        return self.gecikme_ms() is not None


# ---------------------------------------------------------------------------
# Oturum olaylari
# ---------------------------------------------------------------------------

@dataclass
class Olay:
    t: float
    tur: str          # "goaway" | "yeniden_baglandi" | "koptu" | "hata" | ...
    ayrinti: str = ""


# ---------------------------------------------------------------------------
# Olcum defteri
# ---------------------------------------------------------------------------

class Defter:
    def __init__(self, model: str, etiket: str = ""):
        self.model = model
        self.etiket = etiket
        self.t0 = simdi()
        self.baslangic_saati = datetime.now()

        self.turlar: list[Tur] = []
        self.olaylar: list[Olay] = []

        # Kota / token
        # ⚠ usageMetadata'nin ANLAMI ham veriden dogrulandi (29.07.2026,
        #   87 turluk kosu). Ilk yazdigimda YANLIS yorumlamistim ve
        #   rapor anlamsiz bir "token/dakika" sayisi basiyordu:
        #
        #     promptTokenCount   -> o andaki BAGLAM BOYUTU (birikmis
        #                           tuketim DEGIL). Monoton artiyor:
        #                           2.243 -> 36.532
        #     responseTokenCount -> O TURUN ciktisi (birikmis degil)
        #     totalTokenCount    -> prompt + response, yine o an
        #
        #   Yani promptTokenCount'u "harcanan token" sanip sureye
        #   bolmek, baglam boyutunu hiza cevirmek demekti. v1'in
        #   "yanlis seyi olctum" hatasinin kota tarafindaki hali.
        self.baglam_token = 0            # son promptTokenCount
        self.baglam_ilk = 0              # ilk promptTokenCount
        self.cikis_toplam = 0            # responseTokenCount TOPLAMI
        self.token_dusunme = 0
        self.son_kullanim: dict = {}

        # Modalite kirilimi (TEXT / AUDIO / ...).
        #
        # NEDEN ONEMLI: kota tarafinda tek baglayici sinir "dakikada
        # 65.000 GIRIS token'i" (AI Studio, free tier, 29.07.2026) ve
        # dokumana gore SES saniyede 32 token.
        #
        # baglam_modalite : baglamin SU ANKI kirilimi (anlik fotograf)
        # cikis_modalite  : uretilen ciktinin TOPLAMI (birikimli)
        self.baglam_modalite: dict[str, int] = {}
        self.cikis_modalite: dict[str, int] = {}

        # Oturum
        self.baglanti_sayisi = 0
        self.yeniden_baglanma = 0
        self.kopma_bosluklari_ms: list[float] = []
        self.son_devam_anahtari: str | None = None
        self.devam_anahtari_sayisi = 0
        self.uyanma_sureleri: list[float] = []

        # Ses karti gecikmesi (sounddevice'in bildirdigi)
        self.cikis_gecikmesi_ms: float | None = None
        self.giris_gecikmesi_ms: float | None = None

        self._aktif: Tur | None = None

        # Hafiza cikarimi dokumun neresine kadar geldi.
        #
        # Cikarim artik konusma SIRASINDA da calisiyor (ebeveyn paneli
        # bilgiyi hemen gorsun diye). Isaretci olmasaydi her seferinde
        # butun konusma yeniden gonderilirdi: 10 dakikalik bir sohbette
        # ayni metin onlarca kez, hem para hem ESP32'de bosuna baglanti.
        self.hafiza_isaretci: int = 0

        # Oturum sayaci bu defter icin arttirildi mi.
        #
        # Cikarim iki yerden bitiriliyor: uykuya gecerken ve "durdur"da.
        # Bir sohbet uc kez uyuyup uyanirsa oturum sayaci uc artiyordu
        # ve panelde "kacinci konusma" sayisi gercegin katiydi.
        self.oturum_sayildi: bool = False

    # -- tur yonetimi ------------------------------------------------------

    def tur_ac(self) -> Tur:
        t = Tur(no=len(self.turlar) + 1)
        self.turlar.append(t)
        self._aktif = t
        return t

    @property
    def aktif(self) -> Tur | None:
        return self._aktif

    def aktif_veya_ac(self) -> Tur:
        if self._aktif is None:
            return self.tur_ac()
        return self._aktif

    def tur_kapat(self) -> None:
        self._aktif = None

    # -- olaylar -----------------------------------------------------------

    def olay(self, tur: str, ayrinti: str = "") -> None:
        self.olaylar.append(Olay(simdi(), tur, ayrinti))

    def kullanim_guncelle(self, kullanim: dict) -> None:
        """
        usageMetadata sunucudan kumulatif geliyor; ustune eklemiyoruz,
        uzerine yaziyoruz. (Yanlis toplama = yanlis kota tahmini.)
        """
        if not kullanim:
            return
        self.son_kullanim = kullanim

        # Baglam boyutu: UZERINE YAZILIYOR (anlik deger)
        baglam = kullanim.get("promptTokenCount")
        if baglam:
            if not self.baglam_ilk:
                self.baglam_ilk = baglam
            self.baglam_token = baglam
            self.baglam_modalite = {
                k.get("modality", "?"): k.get("tokenCount", 0)
                for k in kullanim.get("promptTokensDetails", []) or []}

        # Cikis: TUR BASINA geliyor, TOPLANIYOR
        cikis = kullanim.get("responseTokenCount",
                             kullanim.get("candidatesTokenCount", 0))
        if cikis:
            self.cikis_toplam += cikis
            for k in kullanim.get("responseTokensDetails", []) or []:
                ad = k.get("modality", "?")
                self.cikis_modalite[ad] = (self.cikis_modalite.get(ad, 0)
                                           + k.get("tokenCount", 0))

        dusunme = kullanim.get("thoughtsTokenCount", 0)
        if dusunme:
            self.token_dusunme += dusunme

    # -- istatistik --------------------------------------------------------

    def _dagilim(self, degerler: list[float]) -> dict:
        if not degerler:
            return {}
        s = sorted(degerler)
        return {
            "adet": len(s),
            "ortalama": statistics.fmean(s),
            "medyan": statistics.median(s),
            "en_iyi": s[0],
            "en_kotu": s[-1],
            "p90": s[min(len(s) - 1, int(round(0.9 * (len(s) - 1))))],
        }

    def gecikmeler(self) -> list[float]:
        """SADECE sesli turlar. Kriter buna bakiyor."""
        return [t.gecikme_ms() for t in self.turlar
                if t.kaynak == "ses" and t.gecikme_ms() is not None]

    def ag_gecikmeleri(self) -> list[float]:
        return [t.ag_gecikmesi_ms() for t in self.turlar
                if t.kaynak == "ses" and t.ag_gecikmesi_ms() is not None]

    def yerel_gecikmeler(self) -> list[float]:
        return [t.yerel_oynatma_ms() for t in self.turlar
                if t.kaynak == "ses" and t.yerel_oynatma_ms() is not None]

    def metin_gecikmeleri(self) -> list[float]:
        """
        Metin gonderildi -> ilk ses paketi geldi.

        VAD devrede olmadigi icin bu deger KRITER DEGIL. Ise yaradigi
        yer su: sesli ag gecikmesinden cikarilinca geriye Gemini'nin
        "sustu" kararinin maliyeti kaliyor (PLAN.md'nin en buyuk ve
        ayarlanabilir kalemi).
        """
        return [t.ag_gecikmesi_ms() for t in self.turlar
                if t.kaynak == "metin" and t.ag_gecikmesi_ms() is not None]

    def kesme_gecikmeleri(self) -> list[float]:
        return [t.kesme_ms() for t in self.turlar if t.kesme_ms() is not None]

    def sure_sn(self) -> float:
        return simdi() - self.t0

    # -- karar -------------------------------------------------------------

    @staticmethod
    def _karar(deger: float | None, gecer: float, sinir: float) -> str:
        if deger is None:
            return "OLCULMEDI"
        if deger <= gecer:
            return "GECER"
        if deger <= sinir:
            return "SINIRDA"
        return "KALIR"

    def gecikme_karari(self) -> tuple[str, str]:
        """
        Karari EN KOTU degil, ama sadece ortalama da degil:
        ortalamaya bakip p90'i uyari olarak veriyoruz.

        Sebep: cocuk ortalamayi yasamiyor, her turu tek tek yasiyor.
        Ama tek bir kotu tur da butun olcumu curutmemeli.
        """
        g = self.gecikmeler()
        if not g:
            return "OLCULMEDI", "hic tamamlanmis tur yok"
        d = self._dagilim(g)
        ana = self._karar(d["ortalama"],
                          ayarlar.KRITER_GECIKME_GECER_MS,
                          ayarlar.KRITER_GECIKME_SINIR_MS)
        p90 = self._karar(d["p90"],
                          ayarlar.KRITER_GECIKME_GECER_MS,
                          ayarlar.KRITER_GECIKME_SINIR_MS)
        not_ = f"ortalama {ana}, turlarin %90'i {p90}"
        return ana, not_

    def kesme_karari(self) -> tuple[str, str]:
        k = self.kesme_gecikmeleri()
        if not k:
            return "OLCULMEDI", "sozunu kesme denenmedi"
        d = self._dagilim(k)
        return (self._karar(d["ortalama"],
                            ayarlar.KRITER_KESME_GECER_MS,
                            ayarlar.KRITER_KESME_SINIR_MS),
                f"{d['adet']} deneme, en kotu {d['en_kotu']:.0f} ms")

    # -- rapor -------------------------------------------------------------

    def _satir(self, ad: str, d: dict, birim: str = "ms") -> str:
        if not d:
            return f"  {ad:<34} olculmedi"
        return (f"  {ad:<34} ort {d['ortalama']:7.0f} · "
                f"med {d['medyan']:7.0f} · p90 {d['p90']:7.0f} · "
                f"en kotu {d['en_kotu']:7.0f} {birim}")

    def rapor(self) -> str:
        g = self._dagilim(self.gecikmeler())
        ag = self._dagilim(self.ag_gecikmeleri())
        yerel = self._dagilim(self.yerel_gecikmeler())
        kes = self._dagilim(self.kesme_gecikmeleri())

        gecikme_karar, gecikme_not = self.gecikme_karari()
        kesme_karar, kesme_not = self.kesme_karari()

        sure = self.sure_sn()
        tamam = [t for t in self.turlar if t.tamam_mi()]

        S = []
        y = S.append

        y("=" * 72)
        y("PATI — ASAMA 1 OLCUM RAPORU")
        y("=" * 72)
        y(f"Tarih          : {self.baslangic_saati:%d.%m.%Y %H:%M}")
        y(f"Model          : {self.model}")
        if self.etiket:
            y(f"Etiket         : {self.etiket}")
        y(f"Sure           : {sure/60:.1f} dakika")
        sesli = [t for t in tamam if t.kaynak == "ses"]
        metinli = [t for t in tamam if t.kaynak == "metin"]
        y(f"Tamamlanan tur : {len(tamam)} / {len(self.turlar)}"
          f"   (sesli {len(sesli)}, metinli {len(metinli)})")
        if not sesli:
            y("")
            y("⚠ HIC SESLI TUR YOK. PLAN.md'un ana kriteri (cocuk sustu ->")
            y("  ilk ses) OLCULMEDI. Asagidaki hicbir sayi o kriter")
            y("  hakkinda kanit degildir.")
        y("")

        # ---- 1. ana kriter
        y("-" * 72)
        y("1) COCUK SUSTU -> ILK SES   (PLAN.md ana kriter)")
        y("-" * 72)
        y(self._satir("olculen (hoparlore kadar)", g))
        y("")
        y(f"  KRITER: <=1500 ms gecer · 1500-2500 sinirda · >2500 kalir")
        y(f"  SONUC : {gecikme_karar}   ({gecikme_not})")
        y("")

        # ---- 2. tasinabilirlik
        y("-" * 72)
        y("2) BU SAYININ NE KADARI ESP32'YE GECER   (PLAN.md)")
        y("-" * 72)
        y("  PC'de olctugumuz gecikme iki parcadan olusuyor. Birincisi")
        y("  ESP32'de de AYNEN yasanacak, ikincisi ESP32'de FARKLI olacak.")
        y("  Bu ayrim yapilmazsa PC olcumu ESP32 icin yaniltici olur.")
        y("")
        y("  ✅ ESP32'ye AYNEN GECER (Gemini VAD + ag + model uretimi):")
        y(self._satir("sustu -> ilk paket soketten", ag))
        y("")
        y("  ❌ ESP32'de FARKLI OLACAK (PC ses yigini yerine I2S DMA):")
        y(self._satir("ilk paket -> hoparlor", yerel))
        if self.cikis_gecikmesi_ms is not None:
            y(f"  {'ses karti kendi gecikmesi':<34} "
              f"{self.cikis_gecikmesi_ms:.0f} ms (cikis)")
        if self.giris_gecikmesi_ms is not None:
            y(f"  {'':<34} {self.giris_gecikmesi_ms:.0f} ms (giris)")
        y("")
        y("  => ESP32 hedefi: yukaridaki ✅ satiri + kartin kendi ses")
        y("     hatti gecikmesi. Asama 2'de gercek kartta olculecek;")
        y("     BURADAN TAHMIN EDILMIYOR.")
        y("")

        # ---- 3. tahmin vs olcum
        y("-" * 72)
        y("3) TAHMIN  vs  OLCUM   (PLAN.md tahmin tablosu)")
        y("-" * 72)
        y("  PLAN.md'de yazili TAHMIN (olcum degil, o gun yapilan hesap):")
        y("     Gemini 'sustu' karari      0.5–0.8 sn")
        y("     Ag gidis-donus (TR→Google) 0.05–0.15 sn")
        y("     Model ilk ses uretimi      0.5–1.0 sn")
        y("     ESP32 ses tamponlama       0.1–0.2 sn")
        y("     TOPLAM tahmin              1.2–2.1 sn")
        y("")
        if ag:
            y(f"  OLCUM (ilk uc kalemin toplami, PC'de): "
              f"{ag['ortalama']:.0f} ms")
            tahmin_alt, tahmin_ust = 1050, 1950   # ilk uc kalem, ms
            if ag["ortalama"] < tahmin_alt:
                y("  -> Olculen deger tahminin ALTINDA. Tahmin karamsarmis.")
            elif ag["ortalama"] > tahmin_ust:
                y("  -> Olculen deger tahminin USTUNDE. Tahmin iyimsermis.")
            else:
                y("  -> Olculen deger tahmin araliginda.")
        else:
            y("  OLCUM: yok")
        y("")

        # ---- 3b. metin turleri / VAD maliyeti
        metin = self._dagilim(self.metin_gecikmeleri())
        if metin:
            y("-" * 72)
            y("3b) VAD'IN MALIYETI   (metin turleri — KRITER DEGIL)")
            y("-" * 72)
            y("  Metinle gonderilen turlarda Gemini'nin 'sustu' karari")
            y("  DEVREDE DEGIL. Bu yuzden asagidaki sayi §4 kriteriyle")
            y("  KARSILASTIRILAMAZ ve ortalamaya katilmadi.")
            y("")
            y(self._satir("metin -> ilk paket (VAD yok)", metin))
            if ag:
                fark = ag["ortalama"] - metin["ortalama"]
                y("")
                y(f"  Sesli ag gecikmesi   : {ag['ortalama']:7.0f} ms")
                y(f"  Metinli ag gecikmesi : {metin['ortalama']:7.0f} ms")
                y(f"  FARK ≈ VAD maliyeti  : {fark:7.0f} ms")
                y("")
                y("  PLAN.md bu kalemi 500–800 ms diye TAHMIN etmisti ve")
                y("  'en buyuk parca, ayarlanabilir' demisti.")
                if fark > 800:
                    y("  -> Olculen deger tahminin USTUNDE. ayarlar.py'deki")
                    y("     VAD_SESSIZLIK_SUNUCU_MS ile dusurmeyi dene.")
                elif fark < 500:
                    y("  -> Olculen deger tahminin ALTINDA.")
                else:
                    y("  -> Olculen deger tahmin araliginda.")
            else:
                y("")
                y("  ⚠ Sesli tur olculmedigi icin cikarma yapilamadi.")
                y("    VAD maliyetini gormek icin mikrofonlu bir kosu lazim.")
            y("")

        # ---- 4. sozunu kesme
        y("-" * 72)
        y("4) SOZUNU KESME   (PLAN.md)")
        y("-" * 72)
        y(self._satir("araya girdi -> sunucu onayladi", kes))
        y(f"  KRITER: <=500 ms gecer · 500-1000 sinirda · >1000 kalir")
        y(f"  SONUC : {kesme_karar}   ({kesme_not})")
        y("")

        # ---- 5. oturum
        y("-" * 72)
        y("5) OTURUM DAYANIKLILIGI   (PLAN.md — 15 dk siniri)")
        y("-" * 72)
        y(f"  Toplam baglanti      : {self.baglanti_sayisi}")
        y(f"  Yeniden baglanma     : {self.yeniden_baglanma}")
        y(f"  Devam anahtari geldi : {self.devam_anahtari_sayisi} kez")
        if self.kopma_bosluklari_ms:
            d = self._dagilim(self.kopma_bosluklari_ms)
            y(f"  Kopma boslugu        : ort {d['ortalama']:.0f} ms · "
              f"en kotu {d['en_kotu']:.0f} ms")
            y("  (cocugun 'robot sustu' diye hissettigi sure)")
        else:
            y("  Kopma boslugu        : kopma yasanmadi")

        # Uyanma suresi — maliyet korumasinin BEDELI.
        #
        # Bosta kapatma parayi kurtariyor ama cocuk tekrar konusunca
        # robot yeniden baglanmak zorunda. O sure cocugun "robot beni
        # duymadi" diye hissettigi sure.
        #
        # ⚠ BU SAYI ESP32'YE AYNEN GECMEZ. Yeniden baglanma TLS el
        #   sikismasi iceriyor ve minicik islemcide daha uzun surecek.
        #   Asama 2'de gercek kartta olculmeli; bosta kapatma suresi
        #   (ayarlar.BOSTA_KAPAT_SN) o olcume gore secilmeli.
        if self.uyanma_sureleri:
            d = self._dagilim(self.uyanma_sureleri)
            y("")
            y(f"  Uykudan uyanma       : {len(self.uyanma_sureleri)} kez · "
              f"ort {d['ortalama']:.0f} ms · en kotu {d['en_kotu']:.0f} ms")
            y("  (cocuk konustu → robot tekrar dinlemeye hazir)")
            y("  ⚠ ESP32'de bu SURE UZAYACAK (TLS el sikismasi).")
        if sure < 20 * 60:
            y("")
            y(f"  ⚠ Oturum {sure/60:.1f} dakika surdu. PLAN.md 20 DAKIKA")
            y("    kesintisiz test istiyor — 15 dk sinirinin asildigini")
            y("    gormeden bu satir kanit sayilmaz.")
        y("")

        # ---- 6. kota
        y("-" * 72)
        y("6) KOTA / TOKEN   (PLAN.md — 1 saat sohbet sigiyor mu)")
        y("-" * 72)
        y("  Iki AYRI sey olculuyor; karistirilirsa sayi anlamini yitirir:")
        y("")
        y("  A) BAGLAM BOYUTU (o anki fotograf, tuketim degil)")
        y(f"     Su an          : {self.baglam_token:,} token"
          .replace(",", "."))
        if self.baglam_modalite:
            kirilim = " · ".join(
                f"{k} {v:,}".replace(",", ".")
                for k, v in sorted(self.baglam_modalite.items()))
            y(f"     Kirilim        : {kirilim}")
        y("     Baglam penceresi: 128k (native audio) / 32k (digerleri)")
        y("     Baglam sikistirma acikken oturum suresiz uzayabiliyor.")
        y("")
        y("  B) URETILEN CIKTI (birikimli, gercek tuketim)")
        y(f"     Toplam         : {self.cikis_toplam:,} token"
          .replace(",", "."))
        if self.cikis_modalite:
            kirilim = " · ".join(
                f"{k} {v:,}".replace(",", ".")
                for k, v in sorted(self.cikis_modalite.items()))
            y(f"     Kirilim        : {kirilim}")
        if self.token_dusunme:
            y(f"     Dusunme        : {self.token_dusunme:,}"
              .replace(",", "."))
        y("")

        # AI Studio > Rate limits, free tier, 29.07.2026 (ekran goruntusu):
        #   Gemini 3 Flash Live -> RPM sinirsiz · TPM 65K · RPD sinirsiz
        # Tek baglayici sinir TPM (dakikada islenen GIRIS token'i).
        #
        # TPM'i usageMetadata'dan DOGRUDAN okuyamiyoruz. En yakin vekil
        # baglamin BUYUME HIZI: her turda baglama eklenen yeni girdi.
        # Bunu "TPM'dir" diye sunmuyoruz, "TPM'e en yakin vekil" diyoruz.
        TPM_SINIR = 65000
        if sure > 0 and self.baglam_token > self.baglam_ilk:
            buyume = (self.baglam_token - self.baglam_ilk) * 60.0 / sure
            oran = 100.0 * buyume / TPM_SINIR
            y(f"  Baglam buyume hizi : ~{buyume:,.0f} token/dakika"
              .replace(",", "."))
            y(f"  TPM sinirina       : ~%{oran:.1f}  (sinir "
              f"{TPM_SINIR:,} giris token/dk)".replace(",", "."))
            y("")
            y("  ⚠ Bu bir VEKIL olcu, TPM'in kendisi degil. Kesin deger")
            y("    icin AI Studio > Rate limits. Ayrica bu ORTALAMA;")
            y("    kota TEPE degere bakiyor.")
        y("")

        # -- ucretli katmana gecilirse ne tutar
        #
        # Fiyatlar 29.07.2026, gemini-3.1-flash-live-preview, ham
        # fiyatlandirma sayfasindan. Google ses icin dakika fiyati da
        # yayinliyor; token hesabina girmeden dogrudan onu kullaniyoruz.
        GIRIS_DK = 0.005      # $/dk, ses girisi
        CIKIS_DK = 0.018      # $/dk, ses cikisi
        cikis_sn = sum(t.ses_uzunlugu_sn() for t in self.turlar)
        giris_dk = sure / 60.0            # mikrofon acik kaldigi sure
        cikis_dk = cikis_sn / 60.0
        tutar = giris_dk * GIRIS_DK + cikis_dk * CIKIS_DK
        y("  UCRETLI KATMANDA BU KOSU NE TUTARDI:")
        y(f"    giris {giris_dk:5.1f} dk x $0.005 = ${giris_dk*GIRIS_DK:.4f}")
        y(f"    cikis {cikis_dk:5.1f} dk x $0.018 = ${cikis_dk*CIKIS_DK:.4f}")
        y(f"    TOPLAM                      = ${tutar:.4f}")
        if sure > 0:
            saatlik = tutar * 3600.0 / sure
            y(f"    bu hizla 1 saat             = ${saatlik:.2f}")
            y(f"    gunde 1 saat, 30 gun        = ${saatlik*30:.2f}/ay")
        y("")
        y("  ⚠ EN BUYUK KALEM SOHBET DEGIL, BOSTA BEKLEMEK.")
        y("    Oturum acik kaldigi surece mikrofon akiyor ve kimse")
        y("    konusmasa bile giris ucreti isliyor. Gunde 12 saat bosta")
        y("    = $3.60/gun = ~$108/ay — sohbetin 5 kati.")
        bk = ayarlar.BOSTA_KAPAT_SN
        y(f"    Bosta kapatma: {f'{bk:.0f} sn' if bk else 'KAPALI ⚠'}")
        y("")

        ses_baglam = self.baglam_modalite.get("AUDIO", 0)
        if any(t.kaynak == "ses" for t in self.turlar):
            y(f"  SES baglamda: {ses_baglam:,} token".replace(",", "."))
            y("  (bu kosuda mikrofon vardi — ses girisi olcume dahil)")
        else:
            y("  ⚠ BU KOSUDA MIKROFON YOKTU (metin modu).")
            y(f"    Baglamdaki {ses_baglam:,} AUDIO token robotun KENDI"
              .replace(",", "."))
            y("    cevaplarinin gecmise eklenmesinden geliyor, cocugun")
            y("    sesinden degil.")
            y("")
            y("    Dokumandan: ses GIRISI saniyede 32 token. Mikrofon")
            y("    surekli aktigi icin gercek robotta ayrica ~1.920")
            y("    token/dakika eklenecek. Bu bir HESAP, olcum degil —")
            y("    mikrofonlu kosuda dogrulanacak.")
        y("")

        # ---- 7. ayarlar
        y("-" * 72)
        y("7) BU OLCUM HANGI AYARLARLA YAPILDI")
        y("-" * 72)
        y(f"  Mikrofon parcasi      : {ayarlar.PARCA_MS} ms "
          f"({ayarlar.PARCA_BAYT} bayt)")
        y(f"  Cikis tamponu         : {ayarlar.CIKIS_TAMPON_MS} ms")
        y(f"  Sunucu VAD sessizligi : "
          f"{ayarlar.VAD_SESSIZLIK_SUNUCU_MS or 'Google varsayilani'}")
        y(f"  Ses                   : {ayarlar.SES_ADI} · "
          f"tizlik {ayarlar.CIKIS_HIZ:g}x")
        try:
            import kisilik
            y(f"  Soru kurali           : {kisilik.AKTIF_SORU_MODU}")
        except Exception:
            pass
        y(f"  Yuz ifadesi araci     : "
          f"{'acik' if ayarlar.YUZ_ARACI else 'KAPALI'}")
        if ayarlar.YUZ_ARACI:
            y("    ⚠ Arac acikken model once araci cagirip cevabi")
            y("      bekliyor, SONRA konusuyor. Olculen ilk ornekte")
            y("      bu ~600 ms ekledi. Gecikme kriterini olcerken")
            y("      --yuz kapali ile de olcup karsilastir.")
        y(f"  Oturum devami         : {ayarlar.OTURUM_DEVAMI}")
        y(f"  Baglam sikistirma     : {ayarlar.BAGLAM_SIKISTIRMA}")
        y("")

        # ---- 8. olaylar
        if self.olaylar:
            y("-" * 72)
            y("8) OLAY KAYDI")
            y("-" * 72)
            for o in self.olaylar:
                y(f"  [{o.t - self.t0:7.1f} sn] {o.tur:<20} {o.ayrinti}")
            y("")

        # ---- 9. tur tur
        if tamam:
            y("-" * 72)
            y("9) TUR TUR   (ortalamaya guvenme, dagilima bak)")
            y("-" * 72)
            y(f"  {'#':>3} {'kaynak':<6} {'gecikme':>9}  {'ag':>8}  "
              f"{'yerel':>7}  {'ses':>6}  soylenen")
            for t in self.turlar:
                if not t.tamam_mi():
                    continue
                soz = (t.robot_dedi or "").replace("\n", " ")[:28]
                # '*' = dolgu turu. Gecikmesi gercek, uyum sayimina girmedi.
                # Sutun 6 karakter: "metin*" tam siginiyor.
                etiket = (t.kaynak + "*") if t.dolgu else t.kaynak
                y(f"  {t.no:>3} {etiket:<6} {t.gecikme_ms():>7.0f}ms  "
                  f"{(t.ag_gecikmesi_ms() or 0):>6.0f}ms  "
                  f"{(t.yerel_oynatma_ms() or 0):>5.0f}ms  "
                  f"{t.ses_uzunlugu_sn():>5.1f}s  {soz}")
            y("")
            if metinli:
                y("  'metin' satirlarinda VAD devrede degil — bunlar §4")
                y("  kriterine GIRMEZ, ortalamalara da katilmadi.")
                y("")
            if any(t.dolgu for t in self.turlar):
                y("  '*' = dolgu turu ('Anlat anlat.' tipi girdi). Gecikmesi")
                y("  gercek ama cocugun soracagi sey degil — UYUM sayimina")
                y("  girmiyor (bkz. bolum 10).")
                y("")

        y("=" * 72)
        return "\n".join(S)

    # -- kayit -------------------------------------------------------------

    def kaydet(self) -> Path:
        ayarlar.KAYIT_KLASORU.mkdir(parents=True, exist_ok=True)
        damga = self.baslangic_saati.strftime("%Y%m%d-%H%M%S")
        ad = f"{damga}-{self.model.replace('/', '_')}"
        if self.etiket:
            ad += "-" + self.etiket

        metin_yolu = ayarlar.KAYIT_KLASORU / f"{ad}.txt"
        metin_yolu.write_text(self.rapor(), encoding="utf-8")

        veri = {
            "model": self.model,
            "etiket": self.etiket,
            "tarih": self.baslangic_saati.isoformat(),
            "sure_sn": self.sure_sn(),
            "ayarlar": {
                "parca_ms": ayarlar.PARCA_MS,
                "cikis_tampon_ms": ayarlar.CIKIS_TAMPON_MS,
                "sunucu_vad_sessizlik_ms": ayarlar.VAD_SESSIZLIK_SUNUCU_MS,
                "ses": ayarlar.SES_ADI,
            },
            "gecikme_ms": self._dagilim(self.gecikmeler()),
            "ag_gecikmesi_ms": self._dagilim(self.ag_gecikmeleri()),
            "yerel_oynatma_ms": self._dagilim(self.yerel_gecikmeler()),
            "metin_gecikmesi_ms_KRITER_DEGIL":
                self._dagilim(self.metin_gecikmeleri()),
            "kesme_ms": self._dagilim(self.kesme_gecikmeleri()),
            "ses_karti_cikis_ms": self.cikis_gecikmesi_ms,
            "ses_karti_giris_ms": self.giris_gecikmesi_ms,
            "token": {
                "baglam_boyutu_son": self.baglam_token,
                "baglam_boyutu_ilk": self.baglam_ilk,
                "baglam_modalite": self.baglam_modalite,
                "uretilen_cikti_toplam": self.cikis_toplam,
                "cikis_modalite": self.cikis_modalite,
                "dusunme": self.token_dusunme,
                "ham_son_mesaj": self.son_kullanim,
            },
            "oturum": {
                "baglanti": self.baglanti_sayisi,
                "yeniden_baglanma": self.yeniden_baglanma,
                "devam_anahtari": self.devam_anahtari_sayisi,
                "kopma_bosluklari_ms": self.kopma_bosluklari_ms,
            },
            "turlar": [asdict(t) for t in self.turlar],
            "olaylar": [asdict(o) for o in self.olaylar],
        }
        veri_yolu = ayarlar.KAYIT_KLASORU / f"{ad}.json"
        veri_yolu.write_text(
            json.dumps(veri, ensure_ascii=False, indent=2), encoding="utf-8")
        return metin_yolu
