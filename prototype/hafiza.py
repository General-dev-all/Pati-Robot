# -*- coding: utf-8 -*-
"""
Robotun kalici hafizasi — v1'in robot/beyin/hafiza.py dosyasindan tasindi.

╔══════════════════════════════════════════════════════════════════════╗
║ PLAN'DA YOKTU. Bu bir ACIK'ti, karar degil.                          ║
╚══════════════════════════════════════════════════════════════════════╝

PLAN.md kalici hafizadan HIC bahsetmiyor: §11'in "v1'den ne tasiniyor"
tablosunda da "dusenler" listesinde de yok. Oysa v1'de vardi ve
calisiyordu — acilis mesaji cocugun adiyla selam veriyordu:
"Deniz! Geldin! Seni bekliyordum."

v1'in tasarim ilkesi aynen korunuyor:

    "Hafiza SEFFAF olmali. Cocuk robotun kendisi hakkinda ne bildigini
     gorebilmeli, yanlisi duzeltebilmeli, istemedigini silebilmeli.
     Yapay zekayi sihirli bir kutu olmaktan cikarip anlasilir bir sey
     yapan sey tam olarak budur."


LIVE API ICIN NE DEGISTI — ve neden ONEMLI
───────────────────────────────────────────
v1'de `ilgili_bilgiler(soru)` HER SORUDA calisiyor, o soruyla ilgili
bilgileri secip prompta koyuyordu. Live API'de bu MUMKUN DEGIL: sistem
promptu oturum basinda BIR KEZ gonderiliyor ve degistirilemiyor.

Yani bilgileri oturum ACILIRKEN, soruyu bilmeden secmek zorundayiz.
Bu da secimin SINIRLI olmasini zorunlu kiliyor.


IKI AYRI SINIR — karistirilmamali
──────────────────────────────────
1. DEPOLAMA SINIRI (EN_FAZLA_KAYIT)
   Dosya sonsuza kadar buyumesin. ESP32'de bu bir NVS/dosya sistemi
   meselesi; kullanicinin dedigi gibi "hafiza zamanla siser ve ESP32'yi
   kitleyebilir".

2. PROMPT SINIRI (PROMPT_EN_FAZLA_KAYIT / PROMPT_EN_FAZLA_KARAKTER)
   ASIL onemli sinir bu ve sebebi OLCULDU: sistem promptu buyudukce
   model kurallara daha az uyuyor. Soru kurali olcumunde gorduk —
   ayni model, ayni sohbet, sadece kural ifadesi degisince uyum
   %9'dan %86'ya cikti. Prompta 50 satir bilgi eklemek, kisilik
   kurallarini seyreltir ve robot "kisa konus" kuralini unutur.

   Ayrica her token gecikme demek. Cocuk icin oncelik hiz.

Yani depolamada 200 bilgi tutabiliriz ama prompta 25'ini koyariz.
"""

from __future__ import annotations

import json
import re
import threading
import time
from datetime import datetime
from pathlib import Path

import metin as metin_yardim

KOK = Path(__file__).resolve().parent
HAFIZA_DOSYASI = KOK / "hafiza.json"

_kilit = threading.RLock()

BOS_HAFIZA = {
    "surum": 1,
    "cocuk": {"ad": None, "yas": None, "tanisma_tarihi": None},
    # Cocugun robota taktigi ad. None ise varsayilan: ROBOT_ADI.
    #
    # NEDEN BILGILERDEN AYRI: bu bir "bilgi" degil, robotun KIMLIGI.
    # Bilgiler listesi benzerlikle birlesiyor, tekrar sayisina gore
    # siralaniyor ve sinira gelince ATILIYOR — robotun adi atilamaz.
    # Ayrica sistem promptunun EN BASINDA gecmesi gerekiyor ("Senin
    # adin X"), bilgiler ise sonda duruyor.
    #
    # Tek bir kisa dize; ESP32'de NVS'te birkac bayt yer tutuyor.
    "robot_adi": None,
    "bilgiler": [],
    # Ebeveynin panelden yazdigi serbest metin ("Pati bunlari bilsin").
    #
    # NEDEN BILGILERDEN AYRI DURUYOR: `bilgiler` modelin kendi cikardigi
    # seyler — benzerlik esigiyle birlesiyor, tekrar sayisina gore
    # siralaniyor, sinirlanip atiliyor. Ebeveynin yazdigi sey ATILMAMALI
    # ve baska bir seyle BIRLESMEMELI. Ayni kutuya koyarsak "Aksam
    # sekizde dis firculamasi gerekiyor" bir gun benzerlik esigine takilip
    # baska bir cumleyle karisir.
    "ebeveyn_notu": "",
    "istatistik": {"oturum": 0, "ilk_calistirma": None},
}

EBEVEYN_NOTU_EN_FAZLA = 600      # ~200 token; prompt sisirmesin

# -- sinirlar (yukaridaki aciklamaya bak) -----------------------------------
EN_FAZLA_KAYIT = 200            # dosyada saklanan
PROMPT_EN_FAZLA_KAYIT = 25      # prompta giren
PROMPT_EN_FAZLA_KARAKTER = 1200  # ~400 token

# Ayni bilgi tekrar ogrenilmesin diye benzerlik esigi.
# v1'den birebir: 0.70 -> "Basketbol oynamayi seviyor" ile "Basketbol
# oynamayi COK seviyor" birlesir (0.75), ama "Annesinin adi Ayse" ile
# "Ablasinin adi Ayse" ayri kalir (0.50). Aradaki bosluk rahat.
BENZERLIK_ESIGI = 0.70

# Robotun DOGUSTAN gelen adi. kisilik._GOVDE ile ayni olmali.
#
# Cocuk bunu degistirebiliyor ("bundan sonra senin adin Osman") ve
# degistirdiginde `robot_adi` alanina yaziliyor. Burasi sadece
# varsayilan: hafiza sifirlanirsa robot yine Pati oluyor.
ROBOT_ADI = "Pati"

# -- konusma sirasinda cikarim (canli hafiza) --------------------------------
#
# Cikarim eskiden SADECE oturum bitince calisiyordu. Sonucu: cocuk
# "kedimin adini unutma" dedikten sonra ebeveyn panelinde o bilgiyi
# gormek icin konusmanin bitmesini (90 sn sessizlik) beklemek
# gerekiyordu. Kullanicinin sozleri: "hafizaya almasi gerektigi bir sey
# olunca orada hemen gozukmuyor".
#
# Cozum her turda cikarim YAPMAK DEGIL — o hem para hem ESP32 yuku.
# Cozum, cikarimi konusmanin DOGAL BOSLUKLARINA koymak:
#
#   · en az 2 yeni tur birikmis olacak (tek "selam"a istek atmayalim)
#   · son cikarimdan bu yana en az 25 sn gecmis olacak
#   · cocuk "unutma / aklinda tut" dediyse 5 sn yeter — o cumle
#     hafizanin ta kendisi, bekletmenin anlami yok (metin.unutma_istegi)
#
# MALIYET: istek yalnizca YENI turlari tasiyor (bkz. pati._hafizayi_guncelle
# isaretci mantigi), yani ~200-400 token. gemini-3.1-flash-lite ile
# cagri basina ~$0.0001. 10 dakikalik yogun bir sohbette en fazla ~20
# cagri -> ~$0.002. Oturum sonu cikariminin ($0.00065) uc kati; ayda
# birkac lira.
#
# ESP32 YUKU: istek konusma yolunun DISINDA, ayri bir HTTPS cagrisi ve
# tur bittikten sonra atiliyor. Ses hattina hic dokunmuyor, cocuk
# beklemiyor. Yine de sinirsiz degil: iki cikarim arasinda en az 25 sn
# var, yani kart en kotu ihtimalle dakikada 2-3 kisa istek yapiyor.
CIKARIM_EN_AZ_YENI_TUR = 2
CIKARIM_EN_AZ_ARALIK_SN = 25.0
CIKARIM_UNUTMA_ARALIK_SN = 5.0


def _simdi() -> str:
    return datetime.now().isoformat(timespec="seconds")


# ---------------------------------------------------------------------------
# Dosya islemleri
#
# Atomik yazma v1'den geliyor ve ESP32'de DAHA DA onemli: cocuk
# powerbank'i cekince yazma yarim kalmamali.
# ---------------------------------------------------------------------------

def oku() -> dict:
    with _kilit:
        if not HAFIZA_DOSYASI.exists():
            h = json.loads(json.dumps(BOS_HAFIZA))
            h["istatistik"]["ilk_calistirma"] = _simdi()
            _yaz_ham(h)
            return h
        try:
            h = json.loads(HAFIZA_DOSYASI.read_text(encoding="utf-8"))
            # ESKI DOSYAYI TAMAMLA. Yeni bir alan eklendiginde (ornek:
            # ebeveyn_notu) eldeki hafiza.json'da o alan yok ve her
            # okuyan yerde .get() yazmak gerekiyordu — biri atlanirsa
            # KeyError. Burada bir kez tamamlaniyor.
            for anahtar, varsayilan in BOS_HAFIZA.items():
                h.setdefault(anahtar, json.loads(json.dumps(varsayilan)))
            return h
        except Exception:
            # Bozuk dosya: yedekle, sifirdan basla. Cocugun hafizasi
            # kaybolmasin diye bozugu SILMIYORUZ.
            bozuk = HAFIZA_DOSYASI.with_suffix(f".bozuk-{int(time.time())}.json")
            try:
                HAFIZA_DOSYASI.rename(bozuk)
            except Exception:
                pass
            h = json.loads(json.dumps(BOS_HAFIZA))
            h["istatistik"]["ilk_calistirma"] = _simdi()
            _yaz_ham(h)
            return h


def _yaz_ham(h: dict) -> None:
    # Once gecici dosyaya yaz, sonra tasi. Elektrik kesilirse hafiza
    # bozulmasin.
    #
    # ⚠ with_suffix DEGIL. with_suffix son uzantiyi DEGISTIRIYOR:
    #   hafiza.json      -> hafiza.gecici
    #   hafiza.json.test -> hafiza.gecici   (AYNI DOSYA!)
    #   Testler hafiza.json.test kullaniyor; Pati acikken test
    #   kosuldugunda ikisi ayni gecici dosyaya yazip birbirinin
    #   tasimasini bozdu (WinError 5). Adin sonuna EKLIYORUZ.
    gecici = Path(str(HAFIZA_DOSYASI) + ".gecici")
    gecici.write_text(json.dumps(h, ensure_ascii=False, indent=2),
                      encoding="utf-8")
    gecici.replace(HAFIZA_DOSYASI)


def yaz(h: dict) -> None:
    with _kilit:
        _yaz_ham(h)


# ---------------------------------------------------------------------------
# Benzerlik — v1'den birebir tasindi
# ---------------------------------------------------------------------------

_EK = re.compile(r"['’]\w*")
_NOKTALAMA = re.compile(r"[^\w\s]", re.UNICODE)

# Turkce eklemeli bir dil: ekler kelimenin SONUNA geliyor. "kopegin" ve
# "kopeginin" ayni seyi anlatiyor ama kelime olarak farkli. Ilk 5 harfi
# almak kaba bir govdeleme ama bu is icin sasirtici derecede iyi
# calisiyor ve sifir bagimlilik gerektiriyor.
_GOVDE_UZUNLUK = 5

# Bu kelimeler cumlenin anlamini TERSINE cevirir; alt kume kuralini
# uygularken dikkat etmezsek "Annesi ogretmen" ile "Annesi ogretmen
# degil" ayni sayilir.
_OLUMSUZ = {"degil", "değil", "yok", "hic", "hiç", "asla", "hicbir", "hiçbir"}


def _kokler(m: str) -> set[str]:
    d = _EK.sub("", metin_yardim.kucult(m))
    d = _NOKTALAMA.sub(" ", d)
    return {k[:_GOVDE_UZUNLUK] for k in d.split() if len(k) > 1}


def _benzerlik(a: str, b: str) -> float:
    ka, kb = _kokler(a), _kokler(b)
    if not ka or not kb:
        return 0.0
    return len(ka & kb) / len(ka | kb)


def _ayni_bilgi_mi(a: str, b: str) -> bool:
    """
    Jaccard'a ek olarak ALT KUME kurali: "Basketbol oynuyor" cumlesi
    "Okul takiminda basketbol oynuyor" icinde geciyor. Jaccard bunu
    0.50 veriyordu ve ikisini ayri kaydediyordu.
    """
    ka, kb = _kokler(a), _kokler(b)
    if not ka or not kb:
        return False
    if len(ka & kb) / len(ka | kb) >= BENZERLIK_ESIGI:
        return True
    kucuk, buyuk = (ka, kb) if len(ka) <= len(kb) else (kb, ka)
    if len(kucuk) >= 2 and kucuk < buyuk:
        fark = {k[:_GOVDE_UZUNLUK] for k in _OLUMSUZ}
        if not (buyuk - kucuk) & fark:
            return True
    return False


# ---------------------------------------------------------------------------
# Bilgiler
# ---------------------------------------------------------------------------

def bilgi_ekle(yazi: str, kaynak: str = "sohbet") -> dict | None:
    """
    Yeni kalici bilgi kaydeder. Ayni sey biliniyorsa yeni kayit acmaz,
    'kez' sayacini artirir (robot bir seyi ne kadar cok duyduysa o
    kadar emin olsun diye).
    """
    yazi = (yazi or "").strip()
    if len(yazi) < 3 or len(yazi) > 200:
        return None

    with _kilit:
        h = oku()
        for b in h["bilgiler"]:
            if _ayni_bilgi_mi(b["metin"], yazi):
                b["kez"] = b.get("kez", 1) + 1
                b["son_gorulme"] = _simdi()
                # Yeni cumle eskisini kapsiyorsa daha bilgilendirici
                # olani sakla.
                if _kokler(b["metin"]) < _kokler(yazi):
                    b["metin"] = yazi
                yaz(h)
                return b

        yeni = {
            "id": max([b["id"] for b in h["bilgiler"]], default=0) + 1,
            "metin": yazi,
            "kaynak": kaynak,
            "tarih": _simdi(),
            "son_gorulme": _simdi(),
            "kez": 1,
        }
        h["bilgiler"].append(yeni)
        _budama(h)
        yaz(h)
        return yeni


def _budama(h: dict) -> None:
    """
    Depolama sinirini uygular.

    Kullanicinin uyarisi: "hafiza zamanla siser, ESP32'yi kitleyebilir."
    Dogru. En az duyulmus ve en eski bilgiyi atiyoruz; cok tekrarlanan
    bilgi (kopeginin adi gibi) kaliyor.
    """
    if len(h["bilgiler"]) <= EN_FAZLA_KAYIT:
        return
    h["bilgiler"].sort(
        key=lambda b: (b.get("kez", 1), b.get("son_gorulme", "")),
        reverse=True)
    h["bilgiler"] = h["bilgiler"][:EN_FAZLA_KAYIT]


def bilgi_sil(bilgi_id: int) -> bool:
    with _kilit:
        h = oku()
        onceki = len(h["bilgiler"])
        h["bilgiler"] = [b for b in h["bilgiler"] if b["id"] != bilgi_id]
        yaz(h)
        return len(h["bilgiler"]) < onceki


def cocugu_tanimla(ad: str | None = None, yas: int | None = None) -> dict:
    with _kilit:
        h = oku()
        if ad:
            ad = ad.strip()[:40]
            if h["cocuk"]["ad"] is None:
                h["cocuk"]["tanisma_tarihi"] = _simdi()
            h["cocuk"]["ad"] = ad
        if yas:
            try:
                h["cocuk"]["yas"] = int(yas)
            except (TypeError, ValueError):
                pass
        yaz(h)
        return h["cocuk"]


def robot_adi() -> str:
    """
    Robotun SU ANKI adi. Cocuk degistirmediyse varsayilan.

    Sistem promptunun ilk cumlesi bunu kullaniyor (kisilik.py).
    """
    return (oku().get("robot_adi") or ROBOT_ADI).strip()


def robot_adini_degistir(ad: str) -> str | None:
    """
    Cocuk robota yeni bir ad takti.

    Kullanicinin istegi: "cocuk isterse robotun adini degistirebilmeli;
    'senin adin artik Osman' derse Pati'nin adi Osman olacak, baska
    zaman 'senin adin Recep' derse Recep olacak."

    Bu bir OYUN ve cocugun robot uzerindeki sahipligini kuruyor —
    v1'in "hafiza seffaf olmali, cocuk duzeltebilmeli" ilkesinin ad
    tarafi. Ebeveyn panelden "Tumunu sil" derse varsayilana donuyor.

    Doner: kabul edilen ad, gecersizse None.
    """
    ad = (ad or "").strip()
    if not _ad_bicimi_uygun_mu(ad):
        return None
    with _kilit:
        h = oku()
        h["robot_adi"] = ad[:40]
        yaz(h)
        return h["robot_adi"]


def ebeveyn_notu_kaydet(yazi: str) -> str:
    """
    Panelden gelen "Pati bunlari bilsin" metnini saklar.

    Sinirlama var cunku prompt buyudukce model kurallara daha az uyuyor
    (bkz. dosya basi). Ebeveyn 5 sayfa yazarsa Pati'nin kisiligi
    seyreliyor ve sebebi hicbir yerde gorunmuyor.
    """
    with _kilit:
        h = oku()
        h["ebeveyn_notu"] = (yazi or "").strip()[:EBEVEYN_NOTU_EN_FAZLA]
        yaz(h)
        return h["ebeveyn_notu"]


def her_seyi_unut() -> None:
    """
    Tam sifirlama. Panelde onay isteyen tek dugme bu.

    v1'de de vardi ve ayni sebeple: cocuk robotun kendisi hakkinda
    ne bildigini gorebilmeli ve SILEBILMELI.
    """
    with _kilit:
        h = json.loads(json.dumps(BOS_HAFIZA))
        h["istatistik"]["ilk_calistirma"] = _simdi()
        yaz(h)


def oturum_sayaci() -> None:
    with _kilit:
        h = oku()
        h["istatistik"]["oturum"] = h["istatistik"].get("oturum", 0) + 1
        yaz(h)


# ---------------------------------------------------------------------------
# Sistem promptuna giren blok
# ---------------------------------------------------------------------------

def prompt_blogu() -> str:
    """
    Oturum ACILIRKEN prompta eklenecek metin.

    v1'de bu secim soruya gore yapiliyordu; Live API'de soru
    bilinmiyor (sistem promptu oturum basinda sabitleniyor). O yuzden
    "en cok tekrarlanan + en yeni" bilgileri aliyoruz ve SINIRLIYORUZ.

    Sinirin sebebi olculdu: prompt buyudukce model kurallara daha az
    uyuyor (bkz. dosya basi).
    """
    h = oku()
    ad = h["cocuk"]["ad"]
    yas = h["cocuk"]["yas"]
    bilgiler = h["bilgiler"]

    S = []
    if ad:
        tanisma = f"Konustugun cocugun adi {ad}."
        if yas:
            tanisma += f" {yas} yasinda."
        tanisma += (" Adini ara sira kullan ama her cumlede degil, "
                    "bunaltici olur.")
        S.append(tanisma)
    else:
        S.append("Bu cocukla HENUZ TANISMADIN. Ilk isin sicak bir sekilde "
                 "kendini tanitip onun adini sormak olsun. Adini soyleyince "
                 "sevindigini belli et.")

    # EBEVEYN NOTU — bilgilerden ONCE ve ayri basligin altinda.
    #
    # Once olmasinin sebebi: model uzun promptta bas ve son kisma daha
    # cok uyuyor. Ebeveynin yazdigi sey (ornek: "aksam sekizde dis")
    # modelin kendi cikardigi bilgilerden daha onemli.
    #
    # Buraya kural EKLENMIYOR — sadece bilgi. "Kisa konus" gibi
    # ifadeler burada tekrarlanirsa promptun kural yogunlugu duser
    # ve olculen uyum (soru kurali %86) bozulur.
    notu = (h.get("ebeveyn_notu") or "").strip()
    if notu:
        S.append("")
        S.append("ANNE-BABASININ SANA BILDIRDIKLERI:")
        S.append(notu)

    # AZ SEY BILIYORSAN MERAK ET.
    #
    # Kullanicinin gozlemi: "robot cocugun ismini hic bilmiyorsa
    # sormuyor, yasini da sormuyor, evcil hayvani var mi diye de".
    # Adi zaten yukarida isteniyor ama gerisi hic istenmiyordu — robot
    # bos bir hafizayla da sohbeti gotururdu ve hafiza bos kalirdi.
    #
    # Tek cumle, ve sadece hafiza INCEYKEN. Dolu hafizada bu satir
    # yok: promptu buyutmenin bedeli olculdu (kural uyumu %86'dan
    # duser) ve zaten tanidigi cocuga soru yagdirmasi da yanlis olur.
    if len(bilgiler) < 3:
        S.append("")
        S.append("Onu HENUZ AZ TANIYORSUN. Sohbetin akisinda merak et: "
                 "kac yasinda, evcil hayvani var mi, nelerden hoslaniyor. "
                 "Arada bir tane sor, sorgu gibi degil arkadas gibi.")

    if bilgiler:
        secili = sorted(bilgiler,
                        key=lambda b: (b.get("kez", 1),
                                       b.get("son_gorulme", "")),
                        reverse=True)
        satirlar, uzunluk = [], 0
        for b in secili[:PROMPT_EN_FAZLA_KAYIT]:
            satir = "- " + b["metin"]
            if uzunluk + len(satir) > PROMPT_EN_FAZLA_KARAKTER:
                break
            satirlar.append(satir)
            uzunluk += len(satir)
        if satirlar:
            S.append("")
            S.append("ONUN HAKKINDA HATIRLADIKLARIN:")
            S.extend(satirlar)
            S.append("")
            S.append("Bunlari bir arkadas nasil hatirlarsa oyle kullan: "
                     "dogrudan konusmanin icinde. Hatirladigin seyi "
                     "soylerken sadece soyle, nereden bildigini anlatma.")

    return "\n".join(S)


def ozet() -> dict:
    h = oku()
    return {
        "cocuk": h["cocuk"],
        # Panelde gorunsun: cocuk robotun adini degistirdiginde
        # ebeveyn bunu bir yerden gormeli.
        "robot_adi": (h.get("robot_adi") or ROBOT_ADI),
        "robot_adi_varsayilan": ROBOT_ADI,
        "ebeveyn_notu": h.get("ebeveyn_notu", ""),
        "bilgi_sayisi": len(h["bilgiler"]),
        "bilgiler": sorted(h["bilgiler"], key=lambda b: b["id"], reverse=True),
        "oturum": h["istatistik"].get("oturum", 0),
        "ilk_calistirma": h["istatistik"].get("ilk_calistirma"),
        "depolama_siniri": EN_FAZLA_KAYIT,
        "prompt_siniri": PROMPT_EN_FAZLA_KAYIT,
    }


# ---------------------------------------------------------------------------
# Cikarim — konusma bitince ne ogrendik
#
# NEDEN ARAC CAGRISI DEGIL: modele `hatirla()` diye bir arac verip
# konusma sirasinda cagirtmak da mumkundu. YAPMIYORUZ, cunku olculdu:
# yuz ifadesi aracinda her cagri cevabin onune fazladan bir gidis-donus
# koydu ve medyan gecikme ~790 ms'den ~1.428 ms'ye cikti. Hafiza icin
# ayni bedeli odemeye gerek yok — konusma BITTIKTEN sonra, cocugu
# bekletmeden yapilabilir.
#
# Maliyet: gemini-3.1-flash-lite ($0.25/1M giris, $1.50/1M cikis).
# 10 dakikalik bir sohbetin dokumu ~2.000 token -> oturum basina
# ~$0.00065. Ayda ~40 sent.
# ---------------------------------------------------------------------------

CIKARIM_MODELI = "gemini-3.1-flash-lite"

# v1'in CIKARIM_PROMPTU'su — icindeki kurallar gercek kullanimda
# ogrenilmis, aynen tasindi.
CIKARIM_PROMPTU = """Asagidaki konusmadan SADECE COCUK hakkinda kalici bilgi cikar.

⚠ SADECE "Cocuk:" ile baslayan satirlara bak. "Robot:" satirlari yalnizca
baglam icindir; oradan BILGI CIKARMA. Robot yanlis duymus ya da uydurmus
olabilir — onun cumlesi kanit degildir.

YAZILACAKLAR (sadece cocugun KENDI soyledikleri):
yasi, ailesi, evcil hayvani, okulu, sinifi, sevdigi ve sevmedigi seyler,
hobileri, arkadaslari, hedefleri.

ASLA YAZILMAYACAKLAR:
- Robot hakkinda hicbir sey. Robotun adi, ne hissettigi, ne hatirladigi.
- Robotun sordugu sorular veya soyledikleri.
- O anki ruh hali, hava durumu, gecici olaylar.

⚠ COCUK ROBOTA YENI BIR AD TAKABILIR: "bundan sonra senin adin X",
"sana X diyecegim", "adin artik X olsun". Bu ROBOTUN adidir, COCUGUN
DEGIL. Bilgi olarak YAZMA, {"ad": ...} olarak da yazma; bunun yerine
{"robot_adi": "X"} nesnesi koy. Cocuk her istedigi zaman
degistirebilir; en son soyledigi gecerlidir.

⚠ COCUK "BUNU UNUTMA" DERSE: o cumledeki bilgiyi MUTLAKA yaz. Bu
satirlar dokumde (!) ile isaretli. Cocuk ozellikle istediyse
"emin degilsen yazma" kurali GECERSIZDIR — istedigi seyi yaz.

BICIM:
- Her bilgi tek cumle, ucuncu sahis, kisa. Ornek: "Kopeginin adi Karabas."
- Cumleye "Cocuk" veya "Robot" diye baslama.
- Emin degilsen yazma. Bos liste, yanlis bilgiden iyidir.
- Sadece JSON dizisi dondur, baska hicbir sey yazma.

COCUGUN ADI ayri tutuluyor. Adini ogrendiysen bilgi olarak YAZMA;
bunun yerine dizinin ilk elemani olarak {"ad": "..."} nesnesi koy.
Ad SADECE cocuk kendi adini soylediginde yazilir: "benim adim X",
"ben X", "bana X derler".

⚠ ADINI SOYLEMEDIYSE {"ad": ...} NESNESINI HIC KOYMA. "Bilinmiyor",
"Bilinmeyen", "?" gibi yer tutucu ASLA yazma — bilinen dogru adin
uzerine yazilir ve cocugun adi kaybolur.
{ad_durumu}

YASI da ayri tutuluyor. Cocuk kac yasinda oldugunu soylediyse ayni
nesneye sayi olarak ekle: {"ad": "Deniz", "yas": 7} ya da sadece
{"yas": 7}. Yasi AYRICA bilgi olarak yazma.

ORNEKLER:
Konusma: Cocuk: "Kedim var, adi Duman." / Robot: "Ne tatli!"
Cikti: ["Kedisinin adi Duman."]

Konusma: Cocuk: "Ben Deniz." / Robot: "Memnun oldum!"
Cikti: [{"ad": "Deniz"}]

Konusma: Cocuk: "Yedi yasindayim, basketbol oynuyorum." / Robot: "Harika!"
Cikti: [{"yas": 7}, "Basketbol oynuyor."]

Konusma: Cocuk: "Bundan sonra senin adin Pargali Patipasa." / Robot: "Havali isim!"
Cikti: [{"robot_adi": "Pargali Pati Pasa"}]

Konusma: Cocuk: "Senin adin artik Osman." / Robot: "Osman mi? Tamam!"
Cikti: [{"robot_adi": "Osman"}]

Konusma: Cocuk (!): "Kirmizi rengi sevmiyorum, bunu unutma." / Robot: "Tamam!"
Cikti: ["Kirmizi rengi sevmiyor."]

Konusma: Cocuk: "Adin ne senin?" / Robot: "Benim adim Pati."
Cikti: []

Konusma: Robot: "Mahmut mu? Kedi arkadasim Mahmut'u aklimda tutacagim!"
Cikti: []

Konusma: Cocuk: "Bugun canim sikkin." / Robot: "Uzuldum."
Cikti: []

ZATEN BILDIKLERIN (bunlari TEKRAR YAZMA, farkli kelimelerle bile olsa):
{bilinen}

KONUSMA:
{konusma}

Cikti:"""


def cikarim_promptu(konusma: str) -> str:
    h = oku()
    bilgiler = h["bilgiler"]
    if bilgiler:
        # prompt_blogu ile AYNI siralama: en cok duyulan + en yeni.
        #
        # Onceden `bilgiler[-25:]` yaziyordu, yani listenin sonundaki 25
        # kayit. Depolama siniri asilip _budama() listeyi (kez, son_gorulme)
        # ile TERSTEN siraladiktan sonra listenin sonu EN AZ duyulan
        # bilgiler oluyor — model "bunlari tekrar yazma" diye tam da
        # unutulmaya en yakin 25 kaydi goruyordu.
        secili = sorted(bilgiler,
                        key=lambda b: (b.get("kez", 1),
                                       b.get("son_gorulme", "")),
                        reverse=True)[:25]
        bilinen = "\n".join("- " + b["metin"] for b in secili)
    else:
        bilinen = "(henuz hicbir sey)"

    # Bilinen adi prompta KOYUYORUZ. Sebep: canli cikarim dokumun
    # sadece yeni parcasini gonderiyor, o parcada ad genelde gecmiyor
    # ve model bosluk gorunce doldurmaya calisiyor.
    ad = h["cocuk"]["ad"]
    if ad:
        ad_durumu = (f'COCUGUN ADI ZATEN BILINIYOR: {ad}. Cocuk bu '
                     f'konusmada YENI bir ad soylemedikce {{"ad": ...}} '
                     f'yazma.')
    else:
        ad_durumu = "Cocugun adi henuz bilinmiyor."

    return (CIKARIM_PROMPTU
            .replace("{ad_durumu}", ad_durumu)
            .replace("{bilinen}", bilinen)
            .replace("{konusma}", konusma[:12000]))


# Modelin ad yerine koydugu YER TUTUCULAR.
#
# Konusma sirasinda cikarim, dokumun sadece YENI kismini gonderiyor.
# O parcada cocugun adi gecmiyorsa model bazen alani bos birakmak
# yerine dolduruyor: 30.07.2026 canli testinde cocugun adi once dogru
# ("Deniz") kaydedildi, 29 saniye sonraki cikarimda "Bilinmiyor"
# yazilip UZERINE YAZILDI. Panelde cocugun adi "Bilinmiyor" gorundu.
_AD_YER_TUTUCU = {
    "bilinmiyor", "bilinmeyen", "bilmiyorum", "belirtilmemis",
    "yok", "bos", "none", "null", "isimsiz", "adsiz", "cocuk",
    "bilinmez", "anonim", "?", "-",
}


def _ad_bicimi_uygun_mu(ad: str) -> bool:
    """
    Bir ad olarak makul mu? (Hem cocugun hem robotun adi icin.)

    Reddedilenler:
      · yer tutucular ("Bilinmiyor", "Yok", "?")
      · tek harf ya da 40 karakterden uzun
      · rakam iceren ("12 yasindayim" gibi bir cumle parcasi)
    """
    ad = (ad or "").strip()
    if len(ad) < 2 or len(ad) > 40:
        return False
    if any(k.isdigit() for k in ad):
        return False
    return metin_yardim.sadele(ad) not in _AD_YER_TUTUCU


def _ad_gecerli_mi(ad: str) -> bool:
    """
    Cikarim modelinin dondurdugu COCUK adi makul mu?

    Prompt zaten "robotun adini cocugun adi sanma" diyor ama prompt bir
    RICA. Bu suzgec bir KURAL ve olculmus IKI hatayi kapatiyor:

      1. Cocuk robota "bundan sonra senin adin Pargali Patipasa" dedi;
         model bunu COCUGUN adi sanip kaydetti. Panelde cocugun adi
         "Pargali" gorundu (gercek ad: Deniz).
      2. Canli cikarim yeni dokum parcasinda ad bulamayinca
         "Bilinmiyor" yazip dogru adin uzerine yazdi.

    Bicim kurallarina EK olarak: icinde robotun adi gecen bir sey
    cocugun adi olamaz. Robotun adi degisebildigi icin karsilastirma
    HEM varsayilana HEM su anki ada bakiyor — cocuk robota "Osman"
    deyip sonra "benim adim da Osman" derse ikincisi elenir; bu
    kaybetmeye deger, cunku tersi (robotun adinin cocuga yapismasi)
    panelde yanlis isim demek.
    """
    if not _ad_bicimi_uygun_mu(ad):
        return False
    k = metin_yardim.kucult(ad)
    for yasak in {ROBOT_ADI, robot_adi()}:
        if metin_yardim.kucult(yasak) in k:
            return False
    return True


def _yas_gecerli_mi(ham) -> int:
    """
    Cikarim modelinin dondurdugu yas makul mu?

    Doner: gecerliyse yasin kendisi, degilse 0.

    2-17 disi bir sayi cocugun yasi degildir; model cumleden rastgele
    bir sayi kapmis demektir ("yedi kardesim var", "2026'da").
    Panelde yanlis yas gormek, hic yas gormemekten kotu.
    """
    try:
        yas = int(ham)
    except (TypeError, ValueError):
        return 0
    return yas if 2 <= yas <= 17 else 0


def cikarim_zamani_mi(yeni_tur: int, gecen_sn: float,
                      unutma_istegi: bool = False) -> bool:
    """
    Konusma SIRASINDA cikarim yapilsin mi?

    Karar burada duruyor (pati.py'de degil) cunku sinirlarin sebebi
    hafizaya ait ve testi buradan yapiliyor. Sinirlarin gerekcesi
    dosya basindaki "canli hafiza" bolumunde.
    """
    if yeni_tur <= 0:
        return False
    if unutma_istegi:
        return gecen_sn >= CIKARIM_UNUTMA_ARALIK_SN
    return (yeni_tur >= CIKARIM_EN_AZ_YENI_TUR
            and gecen_sn >= CIKARIM_EN_AZ_ARALIK_SN)


def _json_diziyi_ayikla(ham: str) -> list:
    """Model bazen ```json ile sariyor; ilk diziyi cekiyoruz."""
    if not ham:
        return []
    bas = ham.find("[")
    son = ham.rfind("]")
    if bas < 0 or son <= bas:
        return []
    try:
        veri = json.loads(ham[bas:son + 1])
        return veri if isinstance(veri, list) else []
    except Exception:
        return []


async def cikar(konusma: str, anahtar: str) -> dict:
    """
    Konusma dokumunden kalici bilgi cikarir ve kaydeder.

    Doner: {"eklenen": [...], "ad": str|None, "yas": int|None,
            "robot_adi": str|None, "reddedilen_ad": str|None,
            "hata": str|None}

    "eklenen" SADECE YENI bilgileri tasiyor. Zaten bilinen bir sey
    tekrar duyulmussa `kez` sayaci artiyor ama listeye girmiyor:
    konusma sirasinda cikarim artik birkac kez calisiyor ve ayni bilgi
    her seferinde "hatirladi: ..." diye panele dusmemeli.
    """
    import asyncio
    import urllib.error
    import urllib.request

    sonuc = {"eklenen": [], "ad": None, "yas": None, "robot_adi": None,
             "reddedilen_ad": None, "hata": None}
    if not konusma.strip():
        return sonuc

    govde = json.dumps({
        "contents": [{"parts": [{"text": cikarim_promptu(konusma)}]}],
        "generationConfig": {"temperature": 0.0, "maxOutputTokens": 800},
    }).encode("utf-8")

    url = (f"https://generativelanguage.googleapis.com/v1beta/models/"
           f"{CIKARIM_MODELI}:generateContent?key={anahtar}")

    def istek() -> str:
        r = urllib.request.Request(
            url, data=govde,
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(r, timeout=30) as c:
            return c.read().decode("utf-8")

    try:
        ham = await asyncio.to_thread(istek)
    except urllib.error.HTTPError as e:
        sonuc["hata"] = f"HTTP {e.code}: {e.read()[:200].decode(errors='replace')}"
        return sonuc
    except Exception as e:
        sonuc["hata"] = f"{type(e).__name__}: {e}"
        return sonuc

    try:
        cevap = json.loads(ham)
        yazi = (cevap["candidates"][0]["content"]["parts"][0]["text"])
    except Exception:
        sonuc["hata"] = "cevap cozulemedi: " + ham[:200]
        return sonuc

    h_onceki = oku()
    onceki = h_onceki["cocuk"]
    onceki_ad, onceki_yas = onceki["ad"], onceki["yas"]
    onceki_robot_adi = h_onceki.get("robot_adi") or ROBOT_ADI
    for kalem in _json_diziyi_ayikla(yazi):
        if isinstance(kalem, dict):
            if kalem.get("robot_adi"):
                # Cocuk robota yeni bir ad takti. Bilgilerden AYRI
                # tutuluyor: bu robotun kimligi, atilabilir bir bilgi
                # degil (bkz. BOS_HAFIZA §robot_adi).
                yeni = robot_adini_degistir(str(kalem["robot_adi"]))
                if yeni and yeni != onceki_robot_adi:
                    sonuc["robot_adi"] = yeni
            if kalem.get("ad"):
                ad = str(kalem["ad"]).strip()
                if _ad_gecerli_mi(ad):
                    cocugu_tanimla(ad=ad)
                    # Ad DEGISTIYSE haber ver. Ayni ad her oturumda
                    # yeniden "ogrenildi" diye bildirim uretmesin.
                    if ad != onceki_ad:
                        sonuc["ad"] = ad
                else:
                    sonuc["reddedilen_ad"] = ad
            yas = _yas_gecerli_mi(kalem.get("yas"))
            if yas:
                cocugu_tanimla(yas=yas)
                if yas != onceki_yas:
                    sonuc["yas"] = yas
        elif isinstance(kalem, str):
            eklendi = bilgi_ekle(kalem, kaynak="cikarim")
            # kez == 1 -> yeni kayit. Daha buyukse zaten biliniyordu,
            # sadece bir kez daha duyuldu.
            if eklendi and eklendi.get("kez", 1) == 1:
                sonuc["eklenen"].append(eklendi["metin"])
    return sonuc
