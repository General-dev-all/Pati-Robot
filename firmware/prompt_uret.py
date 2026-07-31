# -*- coding: utf-8 -*-
r"""
Sistem promptunu prototype/kisilik.py'den okuyup C++ basligi uretir.

NEDEN ELLE KOPYALANMIYOR:
Prompt 5542 karakter ve Asama 1'de OLCULDU — kesin emirle uyum %9'dan
%86'ya cikti. ESP32'de baska bir metin kullanilirsa olculen sayilar
karsilastirilamaz hale gelir.

Elle kopyalanan bir metin zamanla ayrisir: biri promptu duzeltir, oteki
eski kalir, ve fark aylar sonra "neden robot ESP32'de farkli davraniyor"
diye ortaya cikar. Uretmek bu ihtimali tamamen kaldiriyor.

KULLANIM:
    cd firmware
    ..\prototype\.venv\Scripts\python.exe prompt_uret.py

Uretilen dosya depoya GIRIYOR — derleme icin Python gerekmesin.
Prompt degisirse bu betik yeniden kosulur.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

BURASI = Path(__file__).resolve().parent
PROTOTIP = BURASI.parent / "prototype"
HEDEF = BURASI / "main" / "pati_kisilik_uretilmis.h"

sys.path.insert(0, str(PROTOTIP))
import ayarlar  # noqa: E402
import hafiza  # noqa: E402
import kisilik  # noqa: E402
import yuz  # noqa: E402

# ---------------------------------------------------------------------------
# YUZ ARACI — sema BURADA cevriliyor, elle yazilmiyor
# ---------------------------------------------------------------------------
#
# yuz.arac_tanimi() Google'in OPENAPI bicimini veriyor ("OBJECT",
# "STRING"). stackchan'in istemcisi ise semayi `parametersJsonSchema`
# alanina koyuyor ve orasi DUZ JSON Schema bekliyor (kucuk harf) —
# gemini_live_client.cpp:551 bunu acikca yaziyor.
#
# Cevirmeyi burada yapiyoruz cunku IFADE LISTESI tek kaynaktan gelmeli:
# yuz.IFADELER. Elle yazilsa gozler240.js'e bir ifade eklendiginde
# firmware'in listesi eskir ve model ekranda karsiligi olmayan bir ad
# secer — belirtisi "gozler bazen degismiyor" olur ve sebebi aranmaz.
def _sema_kucult(dugum):
    """OpenAPI tip adlarini ("OBJECT") JSON Schema'ya ("object") indirir."""
    if isinstance(dugum, dict):
        cikti = {}
        for anahtar, deger in dugum.items():
            if anahtar == "type" and isinstance(deger, str):
                cikti[anahtar] = deger.lower()
            else:
                cikti[anahtar] = _sema_kucult(deger)
        return cikti
    if isinstance(dugum, list):
        return [_sema_kucult(e) for e in dugum]
    return dugum


_YUZ_TANIM = yuz.arac_tanimi()["functionDeclarations"][0]
YUZ_SEMA = json.dumps(_sema_kucult(_YUZ_TANIM["parameters"]),
                      ensure_ascii=False, separators=(",", ":"))

# 🔴 hafiza_ac=False — BU BIR HATADAN CIKTI.
#
# Once `kisilik.SISTEM_PROMPTU` kullaniliyordu. O sabit
# `sistem_promptu()` ile uretiliyor ve fonksiyonun `hafiza_ac`
# VARSAYILANI True. Yani uretilen C++ basligina, URETIM ANINDAKI
# hafiza blogu GOMULUYORDU. Iki ayri sorun:
#
#   1. CELISKILI TALIMAT. Gomulu metin "Bu cocukla HENUZ TANISMADIN"
#      diyor. Cihazda hafiza olustugunda calisma aninda "Konustugun
#      cocugun adi Bulut" blogu da EKLENIYOR. Prompt ikisini birden
#      soyluyor ve model hangisine uyacagini bilmiyor.
#
#   2. GIZLILIK. Uretimi yapan makinede dolu bir hafiza.json varsa,
#      BASKA BIR COCUGUN bilgileri firmware'e girer ve orada kalir.
#
# Hafiza blogu artik cihazda, calisma aninda ekleniyor
# (pati_hafiza.cpp `hafiza_prompt_blogu()`), tıpkı PC'de oldugu gibi:
# kurallar ONCE, hafiza SONRA (kisilik.py'nin gerekcesi: kurallari
# hafizanin arkasina koyarsak zamanla seyreliyorlar).
# 🔴 robot_adi="{ROBOT_ADI}" — TOKEN GOMULU KALIYOR.
#
# Cocuk robotun adini degistirebiliyor ("bundan sonra senin adin
# Osman"). Ad promptun ILK CUMLESINDE geciyor. Uretim aninda "Pati"
# diye gomseydik cihazdaki ad hic degismezdi: firmware sabit.
#
# Onun yerine token birakiyoruz; cihaz kendi hafizasindaki adi CALISMA
# ANINDA yerine koyuyor (firmware/main/pati_sohbet.cpp). PC'de ayni isi
# kisilik.sistem_promptu() yapiyor. Iki taraf da ayni metni kuruyor.
PROMPT = kisilik.sistem_promptu(hafiza_ac=False, robot_adi="{ROBOT_ADI}")

# C++ ham dize siniricisi metnin icinde GECMEMELI, yoksa dize erken
# kapanir ve derleme bozulur. Kontrol ediyoruz.
SINIR = "PATIPROMPT"


def sinirici_denetle(ad: str, metin: str) -> None:
    """Ham dizeyi erken kapatacak bir dizi var mi?

    Varsa derleme bozulur ya da — daha kotusu — metin sessizce kesilir.
    Uretilen HER dize buradan geciyor; biri unutulursa hata aylar sonra
    "model araci hic cagirmiyor" gibi alakasiz bir belirtiyle cikar.
    """
    if f'){SINIR}"' in metin:
        raise SystemExit(f"HATA: {ad} icinde ){SINIR}\" dizisi var, "
                         f"sinirici degistirilmeli")


sinirici_denetle("prompt", PROMPT)
sinirici_denetle("yuz araci aciklamasi", _YUZ_TANIM["description"])
sinirici_denetle("yuz araci semasi", YUZ_SEMA)
sinirici_denetle("yuz prompt eki", yuz.PROMPT_EKI)

# ASCII disi karakterler SORUN DEGIL — ayni karakterler kisilik.py'den
# geliyor, yani PC de aynisini gonderiyor ve iki taraf birebir ayni metni
# kullaniyor. (Promptta em dash "—" var.) Yine de yaziyoruz ki derleyici
# kaynak kodlamasi konusunda surpriz olmasin: GCC girdi olarak UTF-8
# bekliyor ve dosya UTF-8 yaziliyor.
ascii_disi = sorted({k for k in PROMPT if ord(k) > 127})
if ascii_disi:
    print(f"  not: ASCII disi karakter(ler): {ascii_disi} "
          f"(PC de ayni metni gonderiyor, sorun degil)")

HEDEF.write_text(f'''// URETILMIS DOSYA — ELLE DUZENLEMEYIN.
//
// Kaynak : prototype/kisilik.py  (kisilik.SISTEM_PROMPTU)
// Ureten : firmware/prompt_uret.py
//
// Prompt degistiyse betigi yeniden kosun:
//     cd firmware
//     ..\\prototype\\.venv\\Scripts\\python.exe prompt_uret.py
//
// NEDEN URETILIYOR: bu metin Asama 1'de OLCULDU — kesin emirle prompt
// uyumu %9'dan %86'ya cikti. ESP32'de baska bir metin kullanilirsa
// olculen sayilar PC'deki sayilarla karsilastirilamaz. Elle kopyalanan
// metin zamanla ayrisir; uretmek o ihtimali kaldiriyor.
//
// Uzunluk: {len(PROMPT)} karakter
// En fazla kelime kurali: {ayarlar.__name__ and kisilik.EN_FAZLA_KELIME}

#pragma once

namespace pati {{

// ⚠ Icinde {{ROBOT_ADI}} token'i VAR ve bilerek duruyor. Cocuk robotun
//   adini degistirebiliyor ("bundan sonra senin adin Osman"); cihaz
//   kendi hafizasindaki adi calisma aninda yerine koyuyor
//   (pati_sohbet.cpp). Uretim aninda gomseydik ad hic degismezdi.
inline constexpr const char* SISTEM_PROMPTU = R"{SINIR}({PROMPT}){SINIR}";

// Robotun DOGUSTAN gelen adi. Hafiza bos ya da sifirlanmissa bu
// kullaniliyor (pati_hafiza.cpp `hafiza_robot_adi()`).
inline constexpr const char* PATI_ROBOT_ADI = "{kisilik.ROBOT_ADI}";

// Prompttaki ad yerine gecen token. Iki tarafta ayni dize olmali.
inline constexpr const char* ROBOT_ADI_TOKEN = "{{ROBOT_ADI}}";

// -------------------------------------------------------------------------
// YUZ ARACI — model gozlerin ifadesini kendisi seciyor
// -------------------------------------------------------------------------
//
// Panelden acilip kapaniyor (varsayilan KAPALI, ayar_yuz_araci()).
// KAPALI olmasinin sebebi olculmus: arac cagrisi PC'de medyani
// 2007 -> 1325 ms'ye dusurmustu, yani ~682 ms EKLIYOR ve §3'un
// 1500 ms kriterini tek basina yiyebiliyor.
//
// ⚠ CIHAZDAKI BEDELI OLCULMEDI ve PC'dekinden BUYUK OLABILIR:
//   PC `behavior: NON_BLOCKING` gonderiyor (model cevabi beklemeden
//   devam ediyor). stackchan'in istemcisi setup'a bu alani YAZMIYOR
//   (gemini_live_client.cpp:547-560), yani cihazda arac SIRALI
//   calisiyor: model bizim cevabimizi bekliyor. Bu yuzden
//   submit_tool_result() geciktirilmeden gonderiliyor.
//
// Sema `parametersJsonSchema` alanina gidiyor, o da DUZ JSON Schema
// bekliyor — cevrim prompt_uret.py'de yapiliyor, elle yazilmiyor.
inline constexpr const char* YUZ_ARAC_ADI = "{yuz.ARAC_ADI}";
inline constexpr const char* YUZ_ARAC_ACIKLAMA =
    R"{SINIR}({_YUZ_TANIM["description"]}){SINIR}";
inline constexpr const char* YUZ_ARAC_SEMA =
    R"{SINIR}({YUZ_SEMA}){SINIR}";

// Arac acikken sistem promptunun SONUNA ekleniyor (PC: canli.py §80).
// Sadece tanim yetmiyor; modele araci hatirlatmak gerekiyor.
inline constexpr const char* YUZ_PROMPT_EKI =
    R"{SINIR}({yuz.PROMPT_EKI}){SINIR}";

// Asama 1'de olculen degerler. Firmware bunlari kullanmiyor ama
// karsilastirma yapan insan icin burada duruyor.
inline constexpr int PROMPT_KARAKTER = {len(PROMPT)};
inline constexpr int EN_FAZLA_KELIME = {kisilik.EN_FAZLA_KELIME};

}}  // namespace pati
''', encoding="utf-8")

print(f"uretildi: {HEDEF.relative_to(BURASI)}")
# ---------------------------------------------------------------------------
# CIKARIM PROMPTU — ayni gerekce
# ---------------------------------------------------------------------------
#
# Konusmadan kalici bilgi cikarirken kullanilan prompt. Icindeki
# kurallar v1'de gercek kullanimda ogrenilmis. Elle kopyalanirsa iki
# tarafta iki farkli kural olusur ve robot, PC'de olculdugunden baska
# seyler ogrenmeye baslar — hem de kimse fark etmeden.
CIKARIM = hafiza.CIKARIM_PROMPTU
if f"){SINIR}\"" in CIKARIM:
    raise SystemExit("HATA: cikarim promptunda sinirici dizisi var")

CIKARIM_BASLIK = f"""// URETILMIS DOSYA — ELLE DEGISTIRILMEZ.
//
// Kaynak : prototype/hafiza.py  (CIKARIM_PROMPTU, CIKARIM_MODELI)
// Ureten : firmware/prompt_uret.py

#pragma once

namespace pati {{

// Ucuz model: cikarim konusma bittikten sonra calisiyor, cocuk
// beklemiyor. Oturum basina ~$0,00065.
inline constexpr const char* CIKARIM_MODELI = "{hafiza.CIKARIM_MODELI}";

inline constexpr const char* CIKARIM_PROMPTU = R"{SINIR}({CIKARIM}){SINIR}";

// Prompta giren "bilinenler" listesinin siniri — prototype ile ayni.
inline constexpr int CIKARIM_BILINEN_EN_FAZLA = 25;

// Konusma dokumunun ust siniri (karakter). PC'de 12000; cihazda dokum
// her uykuda temizlendigi icin bu kadar birikmesi beklenmiyor.
inline constexpr int CIKARIM_DOKUM_EN_FAZLA = 8000;

}}  // namespace pati
"""

CIKARIM_HEDEF = BURASI / "main" / "pati_cikarim_uretilmis.h"
CIKARIM_HEDEF.write_text(CIKARIM_BASLIK, encoding="utf-8")

# Geri okuma: gomulu metin kaynakla birebir mi?
geri = CIKARIM_HEDEF.read_text(encoding="utf-8")
bas = geri.index(f"R\"{SINIR}(") + len(f"R\"{SINIR}(")
son = geri.index(f"){SINIR}\";", bas)
if geri[bas:son] != CIKARIM:
    raise SystemExit("HATA: cikarim promptu geri okumada uyusmadi")
print(f"uretildi: main/pati_cikarim_uretilmis.h")
print(f"  cikarim promptu {len(CIKARIM)} karakter, model {hafiza.CIKARIM_MODELI}")
print(f"  ✅ geri okuma: birebir ayni")

print(f"  prompt {len(PROMPT)} karakter, en fazla {kisilik.EN_FAZLA_KELIME} kelime")

# ---------------------------------------------------------------------------
# GERI OKUMA SINAMASI — gomulu metin kaynakla BAYT BAYT ayni mi?
#
# "Uretiyorum, o yuzden aynidir" bir varsayim. Sinirici kacisi, kodlama
# hatasi ya da satir sonu donusumu metni sessizce degistirebilir. Kontrol
# ucuz, sonucu ise butun karsilastirmanin gecerliligi.
# ---------------------------------------------------------------------------
metin = HEDEF.read_text(encoding="utf-8")

# Dosyada birden fazla ham dize var (prompt, yuz aciklamasi, sema, ek).
# Hepsini SIRAYLA ayikliyoruz — sadece ilkine bakmak, sonrakiler bozulsa
# bile "geri okuma tamam" demek olurdu.
parcalar: list[str] = []
i = 0
while True:
    bas = metin.find(f'R"{SINIR}(', i)
    if bas < 0:
        break
    bas += len(f'R"{SINIR}(')
    son = metin.index(f'){SINIR}"', bas)
    parcalar.append(metin[bas:son])
    i = son

BEKLENEN = [
    ("sistem promptu", PROMPT),
    ("yuz araci aciklamasi", _YUZ_TANIM["description"]),
    ("yuz araci semasi", YUZ_SEMA),
    ("yuz prompt eki", yuz.PROMPT_EKI),
]

if len(parcalar) != len(BEKLENEN):
    print(f"  ❌ GERI OKUMA: {len(BEKLENEN)} dize bekleniyordu, "
          f"{len(parcalar)} bulundu")
    raise SystemExit(1)

for (ad, kaynak), gomulu in zip(BEKLENEN, parcalar):
    if kaynak == gomulu:
        print(f"  ✅ geri okuma: {ad} birebir ayni ({len(gomulu)} karakter)")
        continue
    print(f"  ❌ GERI OKUMA UYUSMADI ({ad}): kaynak {len(kaynak)}, "
          f"gomulu {len(gomulu)} karakter")
    for k, (a, b) in enumerate(zip(kaynak, gomulu)):
        if a != b:
            print(f"     ilk fark {k}. karakterde: "
                  f"kaynak {a!r} vs gomulu {b!r}")
            break
    raise SystemExit(1)

# Semanin ifade listesi gozler tablosuyla ortusuyor mu?
#
# Bu sinama tam da bu oturumda dogan bir supheden cikti: model listede
# olmayan bir ifade secerse gozler degismiyor ve belirti "gozler bazen
# takiliyor" oluyor — sebebi aranmayacak kadar sessiz. Karsilastirma
# ucuz, kaybi buyuk.
GOZ_BASLIK = (BURASI / "main" / "pati_goz_uretilmis.h").read_text(
    encoding="utf-8")
eksik = [i for i in yuz.IFADELER if f'{{ "{i}",' not in GOZ_BASLIK]
if eksik:
    print(f"  ❌ yuz.IFADELER'de olup gozler tablosunda OLMAYAN: {eksik}")
    print("     Model bu ifadeyi secerse ekranda hicbir sey degismez.")
    raise SystemExit(1)
print(f"  ✅ {len(yuz.IFADELER)} ifadenin hepsi gozler tablosunda var")
