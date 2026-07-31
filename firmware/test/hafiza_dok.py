# -*- coding: utf-8 -*-
"""
Python hafiza mantiginin ciktisini dosyaya doker.

hafiza_karsilastir.cpp bunlari C++ portunun urettikleriyle
karsilastiriyor. Gerekce o dosyanin basinda.

NEDEN GEREKLI: hafiza sistem promptuna giriyor. Prompt degisirse modelin
davranisi degisiyor ve PC'de olculen sayilar (medyan 1325 ms, uyum %86)
ESP32 icin gecersiz olur. "Benzer bir sey yazdim" yetmiyor.

EN SINSI TUZAK — govdeleme KARAKTER sayiyor:
    Python  k[:5]  "cicek" -> "cicek"   (5 KARAKTER)
    C++'ta bayt sayilsa  -> "ciç" + yarim bayt  (bozuk)
Corpus bu yuzden Turkce harfle dolu.

Kullanim:  cd firmware && python test/hafiza_dok.py
"""

from __future__ import annotations

import sys
from pathlib import Path

BURASI = Path(__file__).resolve().parent
ASAMA1 = BURASI.parent.parent / "prototype"
sys.path.insert(0, str(ASAMA1))

import hafiza  # noqa: E402

AYIRAC = "\t"


def kacir(m: str) -> str:
    """Satir bazli bicim: satir sonu ve sekme kacirilyor."""
    return m.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t")


# ---------------------------------------------------------------------------
# Corpus — tuzaklarin her biri temsil edilsin
# ---------------------------------------------------------------------------

METINLER = [
    # Turkce buyuk/kucuk harf tuzagi
    "İstanbul'da yasiyor",
    "ISTANBUL cok kalabalik",
    "Iyi bir cocuk",
    "İyi bir cocuk",
    # Kesme isareti (ASCII ve tipografik)
    "Karabas'in tuyleri kahverengi",
    "Karabas’in tuyleri kahverengi",
    "Kedisinin adi Pamuk",
    # Turkce harfler — govde siniri cok baytli karakterin ortasina dusuyor
    "Cicekleri suluyor",
    "Çiçekleri suluyor",
    "Çiçekçiden çiçek aldi",
    "Ogretmeninin adi Sevgi",
    "Öğretmeninin adı Sevgi",
    "Güneşli günleri sever",
    # Noktalama
    "Dinozorlari cok seviyor!",
    "Dinozorlari, cok seviyor.",
    "Dinozorlari cok seviyor",
    # Alt kume kurali
    "Basketbol oynuyor",
    "Okul takiminda basketbol oynuyor",
    # Olumsuzluk — alt kume OLMASINA ragmen ayni sayilmamali
    "Annesi ogretmen",
    "Annesi ogretmen degil",
    "Annesi öğretmen değil",
    "Brokoli sever",
    "Brokoli sevmez, hic sevmez",
    # Tek harfli kelimeler (atiliyor)
    "O bir kedi",
    "Bu bir kopek",
    # Uzun ve kisa
    "Ali",
    "Aksam sekizde dislerini fircalamasi gerekiyor, hatirlatman iyi olur",
    # Ayni kok, farkli ek
    "Kopegin adi ne",
    "Kopeginin adi Karabas",
]

satirlar: list[str] = []

# --- 1. kucult
for m in METINLER:
    satirlar.append(AYIRAC.join(["K", kacir(m), kacir(hafiza.metin_yardim.kucult(m))]))

# --- 2. kokler (sirali, karsilastirilabilir olsun)
for m in METINLER:
    kokler = sorted(hafiza._kokler(m))
    satirlar.append(AYIRAC.join(["G", kacir(m), kacir(",".join(kokler))]))

# --- 3. benzerlik ve ayni_bilgi_mi — butun ciftler
for i, a in enumerate(METINLER):
    for b in METINLER[i:]:
        satirlar.append(AYIRAC.join(
            ["B", kacir(a), kacir(b), f"{hafiza._benzerlik(a, b):.6f}"]))
        satirlar.append(AYIRAC.join(
            ["A", kacir(a), kacir(b),
             "1" if hafiza._ayni_bilgi_mi(a, b) else "0"]))

# ---------------------------------------------------------------------------
# 4. prompt_blogu — gercek hafiza dosyasina DOKUNMADAN
# ---------------------------------------------------------------------------
#
# Kullanicinin hafiza.json'i test yuzunden degismesin: gecici bir yola
# yonlendirip sonra geri aliyoruz.

gercek_yol = hafiza.HAFIZA_DOSYASI
gecici_yol = BURASI / "hafiza_test.json"
gercek_simdi = hafiza._simdi
hafiza.HAFIZA_DOSYASI = gecici_yol

# ZAMAN DAMGASI BELIRLENIMCI YAPILIYOR — iki sebeple.
#
# 1. TEST KARARSIZDI. `_simdi()` SANIYE cozunurlugunde
#    (timespec="seconds"). Asagidaki on ekleme normalde ayni saniyeye
#    dusuyor, yani butun damgalar ESIT oluyor ve `prompt_blogu`
#    siralamasi kararli-sira'ya, yani EKLEME sirasina dusuyor. Ama
#    eklemeler bir saniye sinirini gecerse siralama degisiyor —
#    yani test bazen gecip bazen kalirdi.
#
# 2. CIHAZDA DAMGALAR FARKLI. ESP32 tarafinda damga milisaniye
#    tabanli, yani her kayit ayri damga aliyor ve siralama gercekten
#    "en yeni once" oluyor — hafiza.py'nin yorumunda yazan niyet de bu.
#    Damgalari burada da ayirmak, iki tarafi AYNI YOLDAN gecirmek
#    demek; esitlemek ise C++'ta hic olusmayacak bir durumu test etmek
#    olurdu.
_sayac = [0]


def _artan_simdi() -> str:
    _sayac[0] += 1
    return f"2026-07-30T12:00:{_sayac[0]:02d}"


hafiza._simdi = _artan_simdi
try:
    gecici_yol.unlink(missing_ok=True)

    # --- bos hafiza
    satirlar.append(AYIRAC.join(["P", "bos", kacir(hafiza.prompt_blogu())]))

    # --- sadece ad
    hafiza.cocugu_tanimla("Bulut", None)
    satirlar.append(AYIRAC.join(["P", "ad", kacir(hafiza.prompt_blogu())]))

    # --- ad + yas
    hafiza.cocugu_tanimla("Bulut", 6)
    satirlar.append(AYIRAC.join(["P", "ad_yas", kacir(hafiza.prompt_blogu())]))

    # --- ebeveyn notu
    hafiza.ebeveyn_notu_kaydet("Aksam sekizde dis fircalamasi gerekiyor.")
    satirlar.append(AYIRAC.join(["P", "not", kacir(hafiza.prompt_blogu())]))

    # --- bilgiler (ekleme sirasi ve kez sayaci onemli)
    EKLENECEK = [
        "Kedisinin adi Pamuk",
        "Dinozorlari cok seviyor",
        "Karanliktan biraz korkuyor",
        "Öğretmeninin adı Sevgi",
        "Basketbol oynuyor",
        "Okul takiminda basketbol oynuyor",   # alt kume -> birlesmeli
        "Brokoli sever",
        "Brokoli sevmez",                     # olumsuz -> AYRI kalmali
    ]
    for m in EKLENECEK:
        hafiza.bilgi_ekle(m)
    # "Kedisinin adi Pamuk" iki kez daha duyulsun: kez sayaci artsin ve
    # siralamada one gecsin.
    hafiza.bilgi_ekle("Kedisinin adi Pamuk")
    hafiza.bilgi_ekle("Kedisinin adi Pamuk")

    satirlar.append(AYIRAC.join(["P", "tam", kacir(hafiza.prompt_blogu())]))

    # Eklemeden SONRA olusan kayitlar: C++ ayni birlesmeleri yapmali
    h = hafiza.oku()
    for b in h["bilgiler"]:
        satirlar.append(AYIRAC.join(
            ["R", kacir(b["metin"]), str(b["kez"])]))

    # --- sinir: cok uzun ebeveyn notu
    hafiza.ebeveyn_notu_kaydet("ç" * 5000)
    satirlar.append(AYIRAC.join(
        ["N", str(len(hafiza.oku()["ebeveyn_notu"]))]))

    # -----------------------------------------------------------------------
    # AD SUZGECI — olculmus iki hatanin kapisi
    # -----------------------------------------------------------------------
    #
    # Bu bolum 31.07.2026'da eklendi. Sebep: suzgecler C++'ta
    # pati_cikarim.cpp'nin isimsiz uzayindaydi, yani konak testi
    # goremiyordu ve iki taraf SESSIZCE ayrilmisti — C++ Turkce
    # harfleri ASCII'ye indirmedigi icin "Boş" yer tutucusunu
    # eliyemiyordu.
    #
    # Corpus tuzaklari: yer tutucular, Turkce harfli yer tutucu,
    # rakam, uzunluk sinirlari, ve robotun adinin cocuga yapismasi.
    AD_DENEMELERI = [
        "Deniz",                    # siradan ad -> gecer
        "Ali",
        "Şükrü",                   # Turkce harfli ad -> gecer
        "Zeynep Naz",              # iki kelime -> gecer
        "Bilinmiyor",              # yer tutucu
        "bilinmiyor",
        "BELIRTILMEMIS",
        "Boş",                     # Turkce harfli yer tutucu (sadele sart)
        "Yok",
        "Çocuk",                   # Turkce harfli yer tutucu
        "?",
        "-",
        "A",                       # tek harf -> kisa
        "x" * 41,                  # 40 karakter siniri asildi
        "ç" * 40,                  # 40 KARAKTER ama 80 bayt -> gecmeli
        "ç" * 41,                  # 41 karakter -> elenmeli
        "12 yasindayim",           # rakam
        "Ahmet2",                  # rakam
        "  Deniz  ",                # kirpma
        "Pati",                    # robotun adi -> cocugun adi olamaz
        "pati",
        "Pargali Patipasa",        # olculmus hata: icinde robot adi geciyor
        "Patiye benziyor",
    ]
    for ad in AD_DENEMELERI:
        satirlar.append(AYIRAC.join(
            ["D", kacir(ad),
             "1" if hafiza._ad_bicimi_uygun_mu(ad) else "0",
             "1" if hafiza._ad_gecerli_mi(ad) else "0"]))

    # Robotun adi DEGISINCE suzgec de degismeli: cocuk robota "Osman"
    # dediyse "Osman" artik cocugun adi olarak kabul edilmemeli.
    # Bu, C++'taki eski surumun kaciracagi tek durum (o sadece sabit
    # "pati" ariyordu).
    hafiza.robot_adini_degistir("Osman")
    for ad in ["Osman", "Osmanli", "Deniz", "Pati"]:
        satirlar.append(AYIRAC.join(
            ["E", kacir(ad), "1" if hafiza._ad_gecerli_mi(ad) else "0"]))

    # --- yas suzgeci
    for ham in [-1, 0, 1, 2, 7, 17, 18, 40, 2026]:
        satirlar.append(AYIRAC.join(
            ["Y", str(ham), str(hafiza._yas_gecerli_mi(ham))]))

    # --- robot adini degistirme: kabul edilen ad ne oluyor
    for ad in ["Osman", "  Zeynep  ", "R2D2", "A", "Bilinmiyor",
               "Kara Şimşek"]:
        sonuc = hafiza.robot_adini_degistir(ad)
        satirlar.append(AYIRAC.join(
            ["Z", kacir(ad), kacir(sonuc if sonuc else "")]))
finally:
    gecici_yol.unlink(missing_ok=True)
    hafiza.HAFIZA_DOSYASI = gercek_yol
    hafiza._simdi = gercek_simdi

hedef = BURASI / "hafiza_beklenen.txt"
hedef.write_text("\n".join(satirlar) + "\n", encoding="utf-8")
print(f"  yazildi: test/hafiza_beklenen.txt ({len(satirlar)} satir)")
