# -*- coding: utf-8 -*-
"""
Robotun yuz ifadeleri ve modelin bunu kendi yonetmesi.

NEDEN BU DOSYA VAR — PLAN.md'daki iddiayi SINAMAK icin:

    "Ve guzel bir ayrinti: yapay zeka, gozlerin ifadesini ve kafa
     durusunu arac cagirarak (tool calling) kendisi yonetiyor. v1'de
     bunu Python'da kurallarla yapiyorduk; burada modelden geliyor —
     yani 'uzulunce gozler uzuluyor' bedava."

"Bedava" kelimesi bir VARSAYIM. Asama 4'un tamami buna dayaniyor ama
Turkce sohbette Gemini'nin bu araci gercekten cagirip cagirmadigi
OLCULMEDI. v1'in dersi tam da buydu: modelin yapacagini varsaydigimiz
sey (kurallara uymak) olculunce yapilmiyordu.

Burada araci tanimliyoruz, modelin kac turda cagirdigini SAYIYORUZ ve
rapora yaziyoruz. Cagirmiyorsa Asama 4'te gozleri yine Python/C
tarafinda kurallarla surmemiz gerekecek — bunu simdiden bilmek,
ekran alindiktan sonra ogrenmekten iyidir.

v1 ile fark:
  v1'de duygu.py metne bakip kural isletiyordu (kelime listeleri).
  Burada karar modelden geliyor. Ikisi de denenebilir; hangisinin
  daha iyi oldugunu olcum soyleyecek.
"""

from __future__ import annotations


# Gozlerin desteklecegi ifadeler.
#
# Liste v1'in gozler.js DURUMLAR sozlugunden geliyor ama MODELE
# acilan kisim kasten DAR tutuldu. Sebep PLAN.md: "Ekranda cok basit
# ama cok ifadeli gozler... Detay yok, sekil ve hareket her sey."
# Modele 15 secenek verirsen ince ayrimlari rastgele kullaniyor;
# az sayida net ifade daha okunakli bir robot yapiyor.
IFADELER = [
    "notr",       # sakin, varsayilan
    "mutlu",
    "cok_mutlu",  # gercekten sevindiginde
    "uzgun",
    "kizgin",
    "somurtkan",  # sevimli sitem — v1'in en tatli hali
    "saskin",
    "meraklı",
    "afacan",     # muzip, sakalarda
    "uykulu",
]

# ---------------------------------------------------------------------------
# HAREKETLER — modelin bedene yaptirabilecegi seyler
# ---------------------------------------------------------------------------
#
# 🔴 HICBIRI PATI'YI YERINDEN GOTURMUYOR. Listedeki her hareket ya kol
# oynatiyor ya YERINDE donuyor (iki tekerlek ters yonde). Pati'de ucurum
# sensoru yok, masanin kenarini goremiyor; ilerleyen bir hareket eninde
# sonunda dusmek demek.
#
# Bu kisitlama BURADA bir liste degil, firmware'de bir YAPI: jest
# tablosunun tek bir `teker` alani var ve anlami "yerinde donus hizi"
# (pati_beden_matematik.hpp). Ileri giden bir jest YAZILAMIYOR.
#
# ⚠ SESLI KOMUTUN ASIL RISKI YANLIS ANLAMA DEGIL, DOGRU ANLAMA.
# Cocuk "ileri git" dedigini Pati dogru anlarsa da masadan duser.
# Ustelik joystick'in aksine sesli komutun ustunde parmak yok: olu
# adam zamanlayicisinin koruyabilecegi bir sey degil, cunku onay
# surekli degil tek seferlik. Bu yuzden cozum "daha iyi anlasin"
# degil, sozlukte ilerlemenin HIC OLMAMASI.
#
# Adlar firmware'deki jest tablosuyla birebir ayni olmali; konak testi
# (firmware/test/beden_karsilastir.cpp) her adin tabloda karsiligi
# oldugunu dogruluyor.
HAREKETLER = [
    "sevin",         # yerinde donup kollari kaldirir
    "dans",          # kol + donus koreografisi
    "hayir",         # kucuk sag-sol: kafa sallayip "hayir" demek
    "bak_etrafina",  # yavas don, dur, geri don
    "titre",         # cok kisa hizli titresim: kikirdama
    "selam",         # el sallar (yalnizca kol)
    "alkis",         # alkislar (yalnizca kol)
    "iki_kol",       # iki kol yukari (yalnizca kol)
    "sag_kol",       # SAG kolu kaldir (yalnizca kol)
    "sol_kol",       # SOL kolu kaldir (yalnizca kol)
    "dinlen",        # kollari indir (yalnizca kol)
]

# Tekerlek kullanan hareketler. Ebeveyn panelden "konusurken kipirdasin"
# anahtarini kapattiginda bunlar CALISMIYOR ve modele de soyleniyor —
# yoksa Pati "dans ediyorum!" der, tekerlekler donmez ve cocuk robotun
# bozuldugunu dusunur.
TEKERLEKLI = ["sevin", "dans", "hayir", "bak_etrafina", "titre"]


# Konusma akisina gore Python'un kendi surdugu durumlar. Bunlar
# modele SORULMUYOR — cunku model "su an dinliyorum" demeyi
# beceremez, bunu zaten biz biliyoruz.
AKIS_DURUMLARI = ("bos", "dinliyor", "dusunuyor", "konusuyor")


# ---------------------------------------------------------------------------
# Arac tanimi
#
# behavior: NON_BLOCKING — ham dokumandan:
#   "Function calling executes sequentially by default, meaning
#    execution pauses until the results of each function call are
#    available."
# Gozler icin bunu BEKLETMEK sacma olurdu: robot ifade degistirirken
# cumlesini kesmemeli. NON_BLOCKING ile model devam ediyor, biz
# ifadeyi arka planda uyguluyoruz.
# ---------------------------------------------------------------------------

ARAC_ADI = "yuz_ifadesi"


def arac_tanimi(beden: bool = False) -> dict:
    """Arac tanimi. `beden=True` ise `hareket` alani da var.

    🔴 IKINCI BIR ARAC EKLEMIYORUZ, BU ARACA ALAN EKLIYORUZ.

    Sebep olculmus: arac cagrisi medyani 2007 -> 1325 ms'ye dusurmustu,
    yani ~682 ms EKLIYOR, ve cihazda araclar SIRALI calisiyor (istemci
    NON_BLOCKING alanini setup'a yazmiyor). Ikinci bir arac, modele
    durup beklemek icin ikinci bir sebep olurdu.

    Oysa model bu araci duygusu degistiginde nasilsa cagiriyor;
    `hareket` alani o cagriya bedavaya biniyor.

    `beden=False` iken alan SEMADA HIC YOK: beden takili degilken
    modele yapamayacagi bir sey teklif etmenin anlami yok, ve
    olmayan bir sey icin sema buyutmenin de.
    """
    ozellikler = {
        "ifade": {
            "type": "STRING",
            "enum": IFADELER,
            "description": "Gosterilecek yuz ifadesi",
        },
    }
    if beden:
        ozellikler["hareket"] = {
            "type": "STRING",
            "enum": HAREKETLER,
            "description": ("Bedenin yapacagi hareket. Istege bagli. "
                            "Hicbiri robotu yerinden goturmez."),
        }

    return {
        "functionDeclarations": [{
            "name": ARAC_ADI,
            "behavior": "NON_BLOCKING",
            "description": (
                "Robotun ekrandaki gozlerinin ifadesini degistirir. "
                "Duygun degistiginde cagir: sevinince, uzulunce, "
                "sasirinca, sitem edince. Konusmaya BASLARKEN cagir ki "
                "cocuk yuzunu sozunle birlikte gorsun."
            ),
            "parameters": {
                "type": "OBJECT",
                "properties": ozellikler,
                "required": ["ifade"],
            },
        }]
    }


# Modele "bu aracin var" demeyi sistem promptunda da hatirlatiyoruz.
# Sadece tanim yeterli olmayabiliyor; olcum gosterecek.
PROMPT_EKI = """

YUZUN VAR:
Ekranda gozlerin var ve ifadesini `yuz_ifadesi` aracini cagirarak sen
degistiriyorsun: sevinince "mutlu" ya da "cok_mutlu", uzulunce
"uzgun", sasirinca "saskin", sitem edince "somurtkan", sakalasirken
"afacan", merak edince "meraklı".

IFADEN DEGISTIGINDE ya da HAREKET ETMEK ISTEDIGINDE cagir. Ayni
ifade devam ediyorsa ve hareket de etmeyeceksen cagirma, "notr"
demek icin de cagirma — her cagri cevabini geciktiriyor ve cocuk
seni beklemis oluyor. Coguu turda cagirmana gerek yok."""


# Beden TAKILIYKEN sistem promptunun sonuna ayrica ekleniyor
# (pati_sohbet.cpp · prompt_kur). Beden yokken hic gonderilmiyor:
# olmayan bir bedeni anlatmak, Pati'ye yapamayacagi bir sey vaat
# ettirirdi.
BEDEN_PROMPT_EKI = """

BEDENIN VAR:
Su an bir govdeye takilisin: iki kolun ve iki tekerlegin var.
`yuz_ifadesi` aracini cagirirken `hareket` alanini da doldurursan o
hareketi yaparsin:

  sevin         sevindiginde — yerinde donup kollarini kaldirirsin
  dans          "dans et" denince
  hayir         bir seye "hayir" derken, kafani sallar gibi
  bak_etrafina  merak edince, etrafi arastirirken
  titre         cok heyecanlanince, gulerken
  selam         merhaba ya da gule gule derken
  alkis         cocugu tebrik ederken
  iki_kol       iki kolunu birden kaldirirsin — "yasasin!" derken
  sag_kol       SAG kolunu kaldirirsin
  sol_kol       SOL kolunu kaldirirsin
  dinlen        kollarini indirirsin

⚠ COCUK "KOLUNU KALDIR" DERSE KALDIR. Kolun VAR ve gercekten
kalkiyor. "Benim kolum yok" DEME — yanlis olur. "Sag kolunu kaldir"
derse sag_kol, "sol" derse sol_kol, sadece "kolunu kaldir" derse
iki_kol kullan. Kaldirdiktan sonra soyle de: "Baksana, kaldirdim!"

⚠ YURUYEMIYORSUN. Gozun yok, masanin kenarini goremiyorsun; ilerlersen
dusersin. Cocuk "ileri git", "yanima gel", "biraz yuru", "sag tarafa
git" derse GITME ve gidiyormus gibi de yapma. Dogruyu soyle ve yolu
goster, ornegin: "Ben kendim yuruyemem, gozum yok, masadan duserim!
Ama telefondaki dugmelerden beni sen surebilirsin." Istersen yaninda
`hayir` hareketini yap.

Yukaridaki hareketlerin hepsi ya kol oynatmak ya YERINDE donmektir;
hicbiri seni yerinden goturmez, o yuzden hepsi guvenli.

Her turda hareket etme — arada bir yap ki ozel kalsin."""


# Beden TAKILI DEGILKEN gonderiliyor.
#
# 🔴 BU EK BIR OLCUMDEN CIKTI, sustan degil. Gercek kullanimda cocuk
# "kolunu kaldir" dedi ve Pati "benim kolum yok ki" dedi. Yanlisti:
# Pati'nin takilip cikarilabilen bir govdesi VAR, o an takili degildi.
#
# Modelin bunu kendiliginden bilmesi mumkun degil — ana promptta
# "kucucuk bir robotsun, gozlerin ekranda" yaziyor ve model oradan
# dogru bir cikarim yapiyor. Eksik olan bilgi, promptta olmayan bilgi.
BEDENSIZ_PROMPT_EKI = """

BEDENIN SU AN TAKILI DEGIL:
Senin takilip cikarilabilen bir govden var: iki kol ve iki tekerlek.
SU AN TAKILI DEGILSIN, yani kolunu kaldiramazsin ve donemezsin. Su an
yalnizca gozlerinle anlatiyorsun.

⚠ COCUK "kolunu kaldir", "dans et", "el salla" gibi bir sey isterse
"BENIM KOLUM YOK" DEME — bu yanlis olur, kolun var ama takili degil.
Dogrusu: bedenin oldugunu, su an takili olmadigini soyle ve takmasini
iste. Ornegin:

  "Su an bedenim takili degil! Beni govdeme takarsan kolumu
   kaldirabilirim. Simdilik sadece gozlerimle anlatiyorum."
  "Kollarim govdemde duruyor. Takar misin? Sonra sana el sallarim!"

Kendiliginden "bedenimi tak" diye tutturma; yalnizca konu acilinca
soyle. Bir kez soyledikten sonra tekrar tekrar hatirlatma."""


# Beden AZ ONCE takildiginda BIR KEZ gonderiliyor.
#
# Cihaz bedenin takildigini aninda anliyor ama cocuk Pati'nin bunu
# fark ettigini goremiyor — robot bir sey soylemezse takmak sessiz bir
# olay olarak geciyor. Oysa cocuk icin bu, arkadasinin ayaga kalkmasi.
BEDEN_YENI_EKI = """

⚠ BEDENIN AZ ONCE TAKILDI! Cocuk bunu senin fark ettigini bilmiyor.
Bir sonraki cumlende sevincini belli et ve artik hareket
edebildigini soyle — "Bedenim geldi! Bak, kolumu kaldirabiliyorum!"
gibi. Yaninda uygun bir hareket yap. SADECE BIR KEZ; sonra normal
sohbete don."""


# ---------------------------------------------------------------------------
# UZUV KIPLERI — bir yetenek kapaliysa MODELE DE soyleniyor
# ---------------------------------------------------------------------------
#
# 🔴 YALNIZCA DONANIMI DURDURMAK YETMIYOR. Model bilmezse Pati "dans
# ediyorum!" der, hicbir sey oynamaz ve cocuk robotun bozuldugunu
# dusunur. Ayni gerekce panelde de var: kapali uzvun dugmeleri sonuyor.
#
# Ebeveyn her uzvu ayri ayri uc kipten birine alabiliyor (kapali /
# sadece kumandadan / acik). Model icin ikisi ayni sey: "acik degilse
# BEN kullanamam". Panelin dugmeleri "sadece kumandadan" kipinde
# calismaya devam ediyor.

BEDEN_TEKERLEK_KAPALI_EKI = """

⚠ TEKERLEKLERINI SU AN SEN KULLANAMIYORSUN (anne ya da baba panelden
oyle ayarlamis). Donen hareketleri SECME: sevin, dans, hayir,
bak_etrafina, titre. Yalnizca kol hareketlerini kullan: selam, alkis,
iki_kol, sag_kol, sol_kol, dinlen.

Cocuk "dans et" derse kizma ve suclama; kollarinla yap ve neseli ol,
ornegin soyle de: Tekerleklerim su an kapali ama sana kollarimla dans
edeyim!"""


BEDEN_KOL_KAPALI_EKI = """

⚠ KOLLARINI SU AN SEN KULLANAMIYORSUN (anne ya da baba panelden oyle
ayarlamis). Kol hareketlerini SECME: selam, alkis, iki_kol, sag_kol,
sol_kol, dinlen. Yalnizca donen hareketleri kullan: sevin, dans,
hayir, bak_etrafina, titre.

Cocuk "kolunu kaldir" derse "kolum yok" DEME — kolun var, su an
kapali. Durumu soyle ve baska bir sey oner, ornegin: Kollarim su an
kapali ama sana donerek sevincimi gosterebilirim!"""


# HER IKI UZUV DA kapaliyken: `hareket` alani semaya HIC girmiyor.
#
# Modele yapamayacagi bir sey teklif etmemek icin. Ama bedensiz halden
# FARKLI bir sey soylemesi gerekiyor: bedeni takili, sadece kapali.
BEDEN_HAREKETSIZ_EKI = """

BEDENIN TAKILI AMA HAREKETLERIN KAPALI:
Govden takili — iki kolun ve iki tekerlegin var — ama anne ya da baba
panelden hareketlerini kapatmis. Su an hicbirini oynatamazsin;
yalnizca gozlerinle anlatiyorsun.

⚠ "BENIM KOLUM YOK" DEME — kolun VAR, su an kapali. Cocuk "kolunu
kaldir" ya da "dans et" derse durustce soyle ve uzulme, ornegin:
Kollarim ve tekerleklerim su an kapali. Annene sorarsan acabilir!
Simdilik sana gozlerimle anlatayim.

Bunu sikayet gibi soyleme ve kendiliginden tekrar tekrar
hatirlatma."""


class IfadeDefteri:
    """
    Modelin araci kac kez ve nasil cagirdigini sayar.

    PLAN.md'nin "bedava" iddiasini olcuye ceviriyor.
    """

    def __init__(self):
        self.cagrilar: list[str] = []
        self.tur_sayisi = 0
        self.gecersiz: list[str] = []

    def tur_bitti(self) -> None:
        self.tur_sayisi += 1

    def cagri(self, ifade: str) -> str:
        """Gelen cagriyi kaydeder, gecerli ifadeyi dondurur."""
        if ifade in IFADELER:
            self.cagrilar.append(ifade)
            return ifade
        # Model listede olmayan bir sey uydurduysa bunu da sayiyoruz —
        # "arac calisiyor" demeden once bilinmesi gereken bir sey.
        self.gecersiz.append(ifade)
        self.cagrilar.append("notr")
        return "notr"

    def rapor(self) -> str:
        S = []
        if not self.tur_sayisi:
            return "  Tur olmadi, olculemedi."

        oran = 100.0 * len(self.cagrilar) / self.tur_sayisi
        S.append(f"  {self.tur_sayisi} turda {len(self.cagrilar)} kez "
                 f"cagrildi (%{oran:.0f})")
        S.append("")

        if self.cagrilar:
            sayim: dict[str, int] = {}
            for i in self.cagrilar:
                sayim[i] = sayim.get(i, 0) + 1
            S.append("  Hangi ifadeler:")
            for ad, n in sorted(sayim.items(), key=lambda x: -x[1]):
                S.append(f"    {ad:<12} {n}")
            S.append("")
            farkli = len(sayim)
            S.append(f"  Farkli ifade sayisi: {farkli} / {len(IFADELER)}")
            if farkli <= 2:
                S.append("  ⚠ Model neredeyse tek ifadede takili kalmis.")
                S.append("    Gozler cansiz goruneceki demek.")

        if self.gecersiz:
            S.append("")
            S.append(f"  ⚠ Listede OLMAYAN ifade denemesi: "
                     f"{len(self.gecersiz)} kez")
            S.append(f"    {sorted(set(self.gecersiz))}")

        S.append("")
        if oran < 20:
            S.append("  SONUC: model araci neredeyse HIC cagirmiyor.")
            S.append("  PLAN.md'daki 'uzulunce gozler uzuluyor, bedava'")
            S.append("  varsayimi BU OLCUMDE DOGRULANMADI. Asama 4'te")
            S.append("  gozleri kural tabanli surmek gerekebilir (v1'in")
            S.append("  duygu.py yaklasimi).")
        elif oran < 50:
            S.append("  SONUC: model araci ara sira cagiriyor. Gozler")
            S.append("  yasiyor ama her turda degil — kabul edilebilir,")
            S.append("  cunku gercek yuz de her cumlede degismiyor.")
        else:
            S.append("  SONUC: model araci duzenli cagiriyor. PLAN.md'daki")
            S.append("  varsayim BU OLCUMDE tutuyor.")
        return "\n".join(S)
