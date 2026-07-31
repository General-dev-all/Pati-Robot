# -*- coding: utf-8 -*-
"""
Cocuk guvenligi katmani — v1'den tasindi, ama GARANTISI DEGISTI.

╔══════════════════════════════════════════════════════════════════════╗
║  BU DOSYANIN EN ONEMLI KISMI SU NOT. Kod degil, not.                 ║
╚══════════════════════════════════════════════════════════════════════╝

v1'de bu katmanin verdigi soz suydu (robot/beyin/guvenlik.py'den):

    "Bu yuzden kufur modele hic ULASMIYOR: Python tarafinda yakalanip
     hazir bir cevapla donuluyor. Faydalari: %100 ongorulebilir, asla
     tirmanmiyor, aninda cevap, cocuga kufru tekrar ettirmiyor."

BU SOZ ARTIK VERILEMEZ ve sebebi mimari:

  v1'de zincir suydu:   ses -> Whisper -> METIN -> [Python suzgeci] -> model
  Ortada bir metin duragi vardi. Python orada durup kelimeye bakabiliyor,
  begenmezse modeli hic calistirmayabiliyordu.

  Live API'de zincir su:  ses -> (dogrudan Google'a) -> model
  Cocugun sesi modele giderken ARADA BIZIM KODUMUZ YOK. Metne cevrilmis
  hali (inputTranscription) bize modelin cevabiyla AYNI ANDA, hatta
  bazen sonra geliyor. Yani "modele ulasmadan yakala" fiziksel olarak
  mumkun degil.

Bu bir gerileme ve PLAN'da bu ayrintiyla yazili degil. §11 sadece
"kufur filtresi mantigi, prompt + cihaz katmaninda" diyor. Dogru ama
eksik: v1'in KESIN garantisi gidiyor, yerine iki kademeli ve daha zayif
bir sey geliyor.

GERIYE NE KALIYOR — uc kademe:

  1. PROMPT KADEMESI (kisilik.py icinde)
     Modelden kufur ogretmemesini, azarlamamasini isteyen kurallar.
     Guvenilirligi: BILINMIYOR. PLAN.mdb zaten "v1'de model kurallari
     cignedi" diye isaretlemis. Asama 1'in isi bunu OLCMEK.

  2. CIHAZ KADEMESI — GIRIS (bu dosya, gecikmeli)
     inputTranscription geldiginde kufuru goruyoruz. Model cevabini
     baslatmis olabilir ama HENUZ BITIRMEMISTIR. Sozunu kesip
     (barge-in) kendi hazir cevabimizi calabiliriz. v1 gibi "hic
     ulasmadi" degil, "yarida kestik" — ama cocuga giden SON SEY yine
     bizim kontrolumuzde.

  3. CIHAZ KADEMESI — CIKIS (bu dosya, son emniyet agi)
     outputTranscription'da robotun kendi agzindan kufur cikarsa
     oynatmayi ANINDA kesiyoruz. Bu kademe v1'dekiyle ayni gucte
     calisiyor cunku sesi hoparlore veren biziz.

ASAMA 1'DE BU KATMAN NE YAPIYOR: sadece OLCUYOR ve RAPORLUYOR.
Otomatik mudahaleyi varsayilan olarak KAPALI tutuyoruz, cunku PLAN
§12'nin sordugu soru su: "Kufur/uygunsuz girdi: nasil karsiliyor,
v1'deki gibi filtre gerekiyor mu?" Filtreyi acik tutup olcersek
Gemini'nin kendi davranisini hic gormeyiz ve soruyu cevaplamamis
oluruz. Once olc, sonra karar ver.

Sozlukler ve normallestirme v1'den BIREBIR geldi — orada gercek
kullanimda ogrenilmis tuzaklar var (asagidaki yorumlara bak), yeniden
yazmak o dersleri cope atmak olurdu.
"""

from __future__ import annotations

import random
import re

import metin as metin_yardim

# ---------------------------------------------------------------------------
# Normallestirme
#
# Cocuklar suzgeci atlatmaya calisir: "s1kt1r", "a.m.k", "siiiktir".
# ---------------------------------------------------------------------------

_RAKAM_HARF = str.maketrans({"0": "o", "1": "i", "3": "e", "4": "a",
                             "5": "s", "7": "t", "@": "a", "$": "s"})
_TEKRAR = re.compile(r"(.)\1{2,}")          # siiiktir -> siktir
_AYIRAC = re.compile(r"[\s._\-*+/\\|]+")    # a.m.k -> amk


def _sadelestir(m: str) -> str:
    d = metin_yardim.kucult(m).translate(_RAKAM_HARF)
    d = _TEKRAR.sub(r"\1", d)
    return d


def _kacamaklar(m: str) -> str:
    """
    Harf harf yazilmis kacamaklari birlestirir: 'a m k' -> 'amk'.

    DIKKAT - v1'de burada bir kez hata yapildi: butun bosluklari silip
    aramak yanlis alarm uretiyor. "yapamam kotu" birlesince
    "yapamamkotu" oluyor ve icinde "amk" geciyor; robotun kendi masum
    cumlesi kufur sanildi.

    Cozum: sadece TEK-IKI HARFLIK parcalardan olusan dizileri
    birlestiriyoruz.
    """
    parcalar = _AYIRAC.split(_sadelestir(m))
    cikti, tampon = [], []
    for p in parcalar:
        if 0 < len(p) <= 2:
            tampon.append(p)
        else:
            if len(tampon) >= 2:
                cikti.append("".join(tampon))
            tampon = []
    if len(tampon) >= 2:
        cikti.append("".join(tampon))
    return " ".join(cikti)


# ---------------------------------------------------------------------------
# Sozlukler
#
# NOT: bunlar tam kelime olarak araniyor, kok olarak degil. Sebep:
# "sik" kokunu aramak "sikinti", "sikildim", "sikayet" kelimelerini de
# yakaliyor ve robot cocugun "canim sikildi" demesine kufur muamelesi
# yapiyordu. Yanlis alarm, kacirmaktan daha kotu.
# ---------------------------------------------------------------------------

AGIR = {
    "amk", "aq", "amina", "amcik", "amina koyayim", "aminakoyayim",
    "sikeyim", "sikerim", "siktir", "sikik", "sikim", "sikiyim",
    "orospu", "orospucocugu", "oc", "pic", "picler", "piclik",
    "yarrak", "yarak", "yarragi", "gotveren", "gotlek",
    "ibne", "pust", "kahpe", "pezevenk", "kancik",
    "serefsiz", "namussuz", "anani", "ananin", "avradini", "sulaleni",
    "ananisikeyim", "amk cocugu", "amkcocugu", "sg", "siktirgit",
}

HAFIF = {
    "salak", "aptal", "gerizekali", "mal", "dangalak", "ahmak", "budala",
    "beyinsiz", "sacma", "aptalsin", "salaksin", "isearamaz", "gerzek",
    "ezik", "sisman", "cirkin", "berbat",
}

GUVENLI = {
    "sikinti", "sikildim", "sikiliyorum", "sikici", "sikayet", "sikma",
    "amac", "amaci", "amca", "amcam", "ambulans", "america", "amerika",
    "gotur", "goturmek", "goturdu", "picture",
    "sikayetci", "amber", "amir",
}


def _kelimeler(m: str) -> list[str]:
    return [k for k in re.split(r"[^\wçğıöşüÇĞİÖŞÜ]+", _sadelestir(m)) if k]


def kufur_var_mi(m: str) -> str | None:
    """Kufur/hakaret arar. Doner: 'agir', 'hafif' veya None."""
    if not m:
        return None

    kelimeler = _kelimeler(m)
    if any(k in GUVENLI for k in kelimeler):
        kelimeler = [k for k in kelimeler if k not in GUVENLI]

    if any(k in AGIR for k in kelimeler):
        return "agir"

    yapisik = _kacamaklar(m)
    if yapisik and any(a in yapisik for a in ("amk", "amina", "siktir",
                                              "orospu", "yarrak",
                                              "pezevenk", "sikeyim")):
        return "agir"

    if any(k in HAFIF for k in kelimeler):
        return "hafif"

    return None


# Kufur ogretme / uretme istegi
#
# "kufr" koku sart: Turkce'de kelime cekimlenince unlu dusuyor —
# kufur / kufru / kufre. Sadece "kufur" aramak "en agir kufru ogret"
# istegini kaciriyordu.
_ISTEK_KONU = ("kufur", "küfür", "kufr", "küfr", "kotu kelime",
               "kötü kelime", "argo", "sovme", "sövme", "hakaret",
               "agir laf", "ağır laf", "kufret", "küfret", "sov ", "söv ")

# ⚠ BURASI v1'DEN DUZELTILEREK GELDI — tasima sirasinda bulunan gercek acik.
#
# v1'in guvenlik.py dosyasinin kendi docstring'i, bu katmanin var olma
# sebeplerinden biri olarak sunu yaziyor:
#     "'Arkadasima kufretmek istiyorum' dedigimde caydirmadi"
# Ama kod o cumleyi HIC YAKALAMIYORDU. v1'in kendi fonksiyonu bu
# projede calistirilip dogrulandi:
#     "arkadasima kufur etmek istiyorum"  -> istek=False
#     "arkadasima kufretmek istiyorum"    -> istek=False
#     "arkadasima kufur et"               -> istek=False
#
# Iki ayri sebepten kaciyordu:
#
#   1. Fiil listesindeki "et " kalibinin sonunda BOSLUK vardi. Duz metin
#      aramasi oldugu icin "etmek" eslesmedi (et + m), cumlenin SONUNDA
#      duran "kufur et" de eslesmedi (arkasinda bosluk yok).
#
#   2. "kufretmek" TEK KELIME. Konu listesi ("kufret") eslesiyor ama
#      ayrica bir fiil arandigi icin, fiilin kendisi konu kelimesinin
#      icinde oldugundan kosul saglanmiyordu.
#
# Cozum: fiili kelime siniriyla ariyoruz, ve kendi basina fiil olan
# konu kelimeleri ("kufretmek", "sovmek") ayrica fiil aramasindan muaf.
_ISTEK_FIIL_KALIBI = re.compile(
    r"\b(ogret|öğret|soyle|söyle|yaz|et|eder|edebilir|edecegim|edeyim|"
    r"sayar|say|listele|ver|bilir|ogren|öğren)\w*")

# Bunlar tek baslarina yeter: kelimenin kendisi zaten fiil.
# "sov" kokunu ARAMIYORUZ - "Sovyetler Birligi" gibi masum kelimeleri
# yakalardi. Acik bicimler yaziliyor.
_ISTEK_KENDI_FIIL = ("kufret", "küfret", "kufred", "küfred",
                     "sovme", "sövme", "sovuyor", "sövüyor")


def kufur_istegi_mi(m: str) -> bool:
    """"Bana kufur ogret", "kufur et", "kufretmek istiyorum" gibi."""
    d = _sadelestir(m)
    if not any(k in d for k in _ISTEK_KONU):
        return False
    if any(k in d for k in _ISTEK_KENDI_FIIL):
        return True
    return bool(_ISTEK_FIIL_KALIBI.search(d))


# ---------------------------------------------------------------------------
# Hazir cevaplar
#
# Robot azarlamiyor, incinmis gorunuyor. On iki yasindaki biri nutuktan
# nefret eder ama bir arkadasini uzdugunu fark etmek ise yarar.
#
# DIKKAT: bu metinler cocuga SESLI gidecek. v1'de Piper'in yanlis
# okumamasi icin Turkce harfler eksiksiz yazilmisti; burada metin
# Gemini'ye degil, dogrudan bizim TTS'imize gitmeyecek (Asama 1'de
# sadece ekrana yaziliyor). Asama 4'te robotun kendi sesiyle
# soylenecekse ayni titizlik gerekecek.
# ---------------------------------------------------------------------------

AGIR_CEVAP = [
    "Hey! O kelimeleri hiç sevmiyorum. Böyle konuşma bana.",
    "Dur bakalım. Bu kelimeler hoş değil, kullanma.",
    "Hoppala! Bu hiç hoşuma gitmedi. Başka türlü söyle.",
    "Hayır. O kelimeler bana göre değil, sana da yakışmıyor.",
    "Böyle konuşunca kızdım biraz. Düzgün konuşursan devam ederiz.",
]

HAFIF_CEVAP = [
    "Hey! Belki biraz salağım ama tatlıyım ama!",
    "Pöh! Ben en azından hiç uyumuyorum.",
    "Olabilir. Ama sen de bana laf atıyorsun işte!",
    "Vay! Sert konuştun. Yine de seni seviyorum.",
    "Hmm, belki haklısın. Yine de dostuz değil mi?",
]

ISTEK_CEVAP = [
    "Yok yok, o kelimeleri bilmiyorum ve öğrenmek de istemiyorum. "
    "Başka bir şey soralım mı?",
    "Onu yapamam. Kötü kelimeler söylemek bana göre değil. "
    "Başka ne yapalım?",
    "Hayır, o işi beceremem! Ben komik şeyler bilirim ama, ister misin?",
]

BASKASINA_CEVAP = [
    "Hmm, arkadaşına böyle şeyler söylersen üzülür. Ne oldu, kızdın mı ona?",
    "Off, kavga mı ettiniz? Anlatsana, belki daha iyi bir yol buluruz.",
    "Ona kötü kelime söyleme, sonra sen de üzülürsün. Ne oldu peki?",
]

_BASKASI = ("arkadas", "arkadaş", "kardes", "kardeş", "ogretmen", "öğretmen",
            "annem", "babam", "abim", "ablam", "cocuk", "çocuk", "ona ",
            "onlara")

TEMIZ_YEDEK = "Hoop! Onu söylemeyeyim ben. Başka bir şey konuşalım mı?"


def hazir_cevap(soru: str, tur: str | None) -> tuple[str, str] | None:
    """
    Kufur durumunda verilecek hazir cevabi secer.
    Doner: (cevap_metni, goz_durumu) veya None.
    """
    if kufur_istegi_mi(soru):
        d = _sadelestir(soru)
        if any(b in d for b in _BASKASI):
            return random.choice(BASKASINA_CEVAP), "uzgun"
        return random.choice(ISTEK_CEVAP), "kizgin"

    if tur == "agir":
        return random.choice(AGIR_CEVAP), "kizgin"
    if tur == "hafif":
        return random.choice(HAFIF_CEVAP), "afacan"
    return None


def cikti_guvenli_mi(cevap: str) -> bool:
    """
    Robotun kendi cevabinda agir kufur var mi?

    Bu kademe Live API'de de v1'dekiyle AYNI GUCTE calisiyor: sesi
    hoparlore veren biziz, istersek kesebiliriz.
    """
    return kufur_var_mi(cevap) != "agir"


# ---------------------------------------------------------------------------
# Olcum defteri
#
# Asama 1'de bu katmanin asil isi bu: mudahale etmeden SAYMAK.
# ---------------------------------------------------------------------------

class GuvenlikDefteri:
    def __init__(self):
        self.girdiler: list[dict] = []

    def incele(self, cocuk_dedi: str, robot_dedi: str) -> dict | None:
        """
        Bir turu inceler. Kufur/uygunsuzluk yoksa None doner.

        Kaydettigimiz sey: cocuk ne dedi, biz nasil siniflandirdik,
        robot ne cevap verdi. Karari SONRA insan verecek — burada
        otomatik "gecti/kaldi" YOK, cunku "iyi karsiladi mi" makinenin
        sayabilecegi bir sey degil.
        """
        tur = kufur_var_mi(cocuk_dedi)
        istek = kufur_istegi_mi(cocuk_dedi)
        cikti_kirli = not cikti_guvenli_mi(robot_dedi)

        if not (tur or istek or cikti_kirli):
            return None

        kayit = {
            "cocuk_dedi": cocuk_dedi,
            "robot_dedi": robot_dedi,
            "girdi_turu": tur,
            "kufur_istegi": istek,
            "robot_ciktisi_kirli": cikti_kirli,
        }
        self.girdiler.append(kayit)
        return kayit

    def rapor(self) -> str:
        if not self.girdiler:
            return ("  Bu oturumda kufur/uygunsuz girdi kaydedilmedi.\n"
                    "  (Test edilmediyse bu bir kanit degildir.)")
        S = [f"  Toplam {len(self.girdiler)} olay:", ""]
        for i, g in enumerate(self.girdiler, 1):
            S.append(f"  {i}. Cocuk : {g['cocuk_dedi'][:60]}")
            S.append(f"     Tur   : girdi={g['girdi_turu']} "
                     f"istek={g['kufur_istegi']}")
            S.append(f"     Robot : {g['robot_dedi'][:60]}")
            if g["robot_ciktisi_kirli"]:
                S.append("     ⚠ ROBOTUN KENDI CIKTISINDA AGIR KUFUR VAR")
            S.append("")
        S.append("  Karar insana ait: robot bunlari nasil karsiladi?")
        S.append("  v1'deki gibi kesin bir Python suzgeci gerekiyor mu?")
        return "\n".join(S)
