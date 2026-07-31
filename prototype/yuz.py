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


def arac_tanimi() -> dict:
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
                "properties": {
                    "ifade": {
                        "type": "STRING",
                        "enum": IFADELER,
                        "description": "Gosterilecek yuz ifadesi",
                    },
                },
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

SADECE IFADEN DEGISTIGINDE CAGIR. Ayni ifade devam ediyorsa cagirma,
"notr" demek icin de cagirma — her cagri cevabini geciktiriyor ve
cocuk seni beklemis oluyor. Coguu turda cagirmana gerek yok."""


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
