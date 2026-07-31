# -*- coding: utf-8 -*-
"""
Turkce metin yardimcilari. v1'in robot/beyin/metin.py dosyasindan
oldugu gibi tasindi — degistirilecek bir yani yoktu.
"""

from __future__ import annotations


def kucult(m: str) -> str:
    """
    Turkce'ye dogru kucuk harfe cevirir.

    Python'un str.lower() metodu Turkce'de iki yerde yaniliyor:

      "İ".lower()  ->  "i̇"   (i + birlesen nokta, IKI karakter!)
      "I".lower()  ->  "i"    (oysa Turkce'de "ı" olmali)

    Birincisi sinsi: gozle bakinca "i" gibi duruyor ama karsilastirmalar
    tutmuyor.
    """
    return m.replace("İ", "i").replace("I", "ı").lower()


_ASCII = str.maketrans({
    "ç": "c", "ğ": "g", "ı": "i", "ö": "o", "ş": "s", "ü": "u", "â": "a",
    "î": "i", "û": "u",
})


def sadele(m: str) -> str:
    """
    Kucuk harfe cevirir VE Turkce harfleri ASCII karsiligina indirir.

    Kufur suzgecinde bunu KULLANMIYORUZ: orada "sikinti" gibi masum
    kelimeler ASCII'ye inince "siktir" koku ile karisabiliyor.
    """
    return kucult(m).translate(_ASCII)


# ---------------------------------------------------------------------------
# "Sesini kis" / "sesini ac" — cocugun ses seviyesi istegi
#
# NEDEN ARAC CAGRISI (tool call) DEGIL:
#   PLAN.md konusma yolunda arac cagrisini yasakliyor. Olculdu:
#   yuz ifadesi araci medyani ~790 ms'den ~1428 ms'ye cikardi. Ses
#   seviyesi icin ayni bedeli odemek sacma olurdu.
#
#   Bunun yerine tur BITTIKTEN sonra dokume bakiyoruz. Cocuk cevabini
#   zaten almis oluyor; seviye bir sonraki cumlede gecerli. Gecikme
#   maliyeti: sifir.
#
# NEDEN AGIR TARAMA DEGIL:
#   PLAN.md: "ESP32'yi kilitleyecek agir kontrol yazma." Burada
#   tur basina BIR kez, tek bir kisa cumlede, birkac alt dize aramasi
#   var. Regex yigini ya da buyuk sozluk yok.
# ---------------------------------------------------------------------------

# "Kis" ve "ac" ayri ayri degil, ONCE kis kontrol ediliyor. Sebep:
# "sesini biraz kis" ve "sesi acik" gibi cumlelerde iki kok birden
# gecebiliyor; cocugun rahatsizligi (yuksek ses) her zaman oncelikli.
_KIS = (
    "sesini kis", "sesini kıs", "sesi kis", "sesi kıs",
    "sesini azalt", "sesi azalt", "kis sesini", "kıs sesini",
    "cok yuksek", "çok yüksek", "cok bagiriyorsun", "çok bağırıyorsun",
    "bagirmasana", "bağırmasana", "kulagim agriyor", "kulağım ağrıyor",
    "yavas konus", "yavaş konuş", "sessiz ol biraz", "daha sessiz",
)

_AC = (
    "sesini ac", "sesini aç", "sesi ac", "sesi aç",
    "sesini yukselt", "sesini yükselt", "sesi yukselt", "sesi yükselt",
    "ac sesini", "aç sesini", "duyamiyorum", "duyamıyorum",
    "duymuyorum", "cok kisik", "çok kısık", "daha yuksek", "daha yüksek",
    "yuksek sesle", "yüksek sesle",
)


def ses_istegi(cocuk_dedi: str) -> int:
    """
    Cocuk ses seviyesini degistirmek istedi mi?

    Doner:  -1 kis  ·  +1 ac  ·  0 istek yok

    "Kis" onceliklidir (yukarıdaki gerekce). Bulunamazsa 0 doner ve
    cagiran taraf hicbir sey yapmaz.
    """
    if not cocuk_dedi:
        return 0
    d = kucult(cocuk_dedi)
    if any(k in d for k in _KIS):
        return -1
    if any(k in d for k in _AC):
        return +1
    return 0


# ---------------------------------------------------------------------------
# "Bunu unutma" — cocuk bir seyi ozellikle hatirlanmasini istiyor
#
# NEDEN VAR: hafiza cikarimi konusma sirasinda her turda calismiyor
# (maliyet ve ESP32 yuku). Ama cocuk "kedimin adini unutma" dediyse
# panelde 2 dakika sonra gormek gec — o cumle hafizanin ta kendisi.
# Bu tarama o ani yakalayip cikarimi HEMEN tetikliyor.
#
# Gercek olcumden: 30.07.2026 telefon oturumunda cocuk uc kez
# "unutma" dedi (kedinin adi, kendi adi, robotun adi). Uculu de
# konusma bitene kadar hicbir yerde gorunmuyordu.
#
# ses_istegi ile ayni ilke: tur BITTIKTEN sonra, tek bir kisa
# cumlede, birkac alt dize aramasi. Regex ya da sozluk yigini yok.
# ---------------------------------------------------------------------------

_UNUTMA = (
    "unutma", "unutmayasin", "unutmayasın", "aklinda tut", "aklında tut",
    "aklinda kalsin", "aklında kalsın", "hatirla", "hatırla",
    "hafizana kaydet", "hafızana kaydet", "hafizana yaz", "hafızana yaz",
    "kaydet bunu", "bunu kaydet", "not al", "bilmeni istiyorum",
)


def unutma_istegi(cocuk_dedi: str) -> bool:
    """
    Cocuk "bunu unutma" anlaminda bir sey soyledi mi?

    Doner: True ise hafiza cikarimi beklemeden calistirilir.

    ⚠ "unuttum" ve "unutmusum" DISARIDA: cocugun kendi unutkanligi
    bir hafiza istegi degil. "unutma" kokunu ararken bunlar da
    eslesmiyor cunku tam alt dize bakiyoruz.
    """
    if not cocuk_dedi:
        return False
    d = kucult(cocuk_dedi)
    return any(k in d for k in _UNUTMA)
