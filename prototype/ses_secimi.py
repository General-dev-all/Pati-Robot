# -*- coding: utf-8 -*-
"""
Pati'nin sesini KULAKLA sec.

    python ses_secimi.py              adaylari sirayla dinlet
    python ses_secimi.py --hepsi      30 sesin hepsi
    python ses_secimi.py --hiz 1.12   tizlik/hiz denemesi
    python ses_secimi.py --ses Puck   tek sesi dinle

NEDEN VAR
─────────
Ses secimi olculebilir bir sey degil, DINLENIR. Ilk surumde "Kore"
kullaniyorduk — sirf dokumandaki ornek kodda o vardi. Tanimi "Firm"
(sert/kararli); afacan bir cocuk robotu icin ters. Ezberden secmek
yerine ayni cumleyi butun adaylara soyletip karsilastiriyoruz.

TIZLIK / HIZ HAKKINDA — dogrulandi
───────────────────────────────────
Live API'de tizlik ve hiz AYARLANAMIYOR:
  · SpeechConfig semasinda sadece voiceConfig / multiSpeakerVoiceConfig
    / languageCode var. speakingRate ya da pitch YOK (generate-content
    API referansinin tamaminda gecmiyor).
  · googleapis/python-genai#2029 acik ozellik istegi:
    "Speech rate is fixed or only indirectly controllable."

Ama v1'in hilesi calisiyor. v1'in tts.py'si:
  "Cozum eski bir hile: sesi daha YAVAS urettirip WAV basligindaki
   ornekleme frekansini yukseltiyoruz. Calici sesi daha hizli
   oynatiyor, boylece hem hizlaniyor hem tizlesiyor."

Bizde uretimi yavaslatamiyoruz ama CALMA hizini degistirebiliyoruz:
24.000 Hz'lik sesi 26.400 Hz'de calmak = %10 hizli + ~%10 tiz.
ESP32'de bu sadece bir I2S saat ayari, islemciye hic yuk binmiyor.

⚠ DURUST SINIR: v1 tizligi ve hizi AYRI ayarlayabiliyordu (uretimi
  yavaslatip calmayi hizlandirarak). Bizde ikisi BIRLIKTE degisiyor.
  Yani v1'in "tizlik 1.52" degerini oldugu gibi tasiyamayiz — o kadar
  yukseltirsek robot ayni oranda aceleci de olur.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import json
import sys

import websockets
from websockets.asyncio.client import connect

import ayarlar

# Google'in 30 hazir sesi (speech-generation dokumani, 29.07.2026).
# Yanlarindaki tanimlar dokumandan birebir.
SESLER = {
    "Zephyr": "Bright", "Puck": "Upbeat", "Charon": "Informative",
    "Kore": "Firm", "Fenrir": "Excitable", "Leda": "Youthful",
    "Orus": "Firm", "Aoede": "Breezy", "Callirrhoe": "Easy-going",
    "Autonoe": "Bright", "Enceladus": "Breathy", "Iapetus": "Clear",
    "Umbriel": "Easy-going", "Algieba": "Smooth", "Despina": "Smooth",
    "Erinome": "Clear", "Algenib": "Gravelly", "Rasalgethi": "Informative",
    "Laomedeia": "Upbeat", "Achernar": "Soft", "Alnilam": "Firm",
    "Schedar": "Even", "Gacrux": "Mature", "Pulcherrima": "Forward",
    "Achird": "Friendly", "Zubenelgenubi": "Casual",
    "Vindemiatrix": "Gentle", "Sadachbia": "Lively",
    "Sadaltager": "Knowledgeable", "Sulafat": "Warm",
}

# Cocuk robotu icin akla yatkin olanlar. Tanimlarindan secildi;
# karari kulak verecek.
ADAYLAR = ["Leda", "Puck", "Fenrir", "Sadachbia", "Achird", "Zephyr",
           "Aoede", "Kore"]

# Denenecek cumle. Bilerek boyle kuruldu:
#   · Turkce cumle → telaffuz ve ton
#   · icinde Ingilizce tam cumle → SES DEGISIYOR MU (degismemeli)
#   · unlem ve heyecan → afacanlik tasiyor mu
DENEME_CUMLESI = (
    "Merhaba! Ben Pati, senin afacan robot arkadasin. "
    "Bak sana bir sey soyleyeyim: I went home today and ate an apple. "
    "Yasasin, bugun cok eglenecegiz!"
)


async def sesi_dinlet(ses_adi: str, hoparlor, model: str,
                      cumle: str) -> bool:
    """Tek bir sesle baglanip cumleyi soyletir ve calar."""
    anahtar = ayarlar.api_anahtari()
    setup = {
        "setup": {
            "model": f"models/{model}",
            "generationConfig": {
                "responseModalities": ["AUDIO"],
                "speechConfig": {
                    "voiceConfig": {
                        "prebuiltVoiceConfig": {"voiceName": ses_adi}
                    }
                },
            },
            "systemInstruction": {"parts": [{
                "text": "Sana verilen cumleyi AYNEN, kelimesi kelimesine "
                        "soyle. Baska hicbir sey ekleme, yorum yapma."
            }]},
        }
    }

    url = f"{ayarlar.WS_UC}?key={anahtar}"
    try:
        async with connect(url, max_size=None) as ws:
            await ws.send(json.dumps(setup))
            async with asyncio.timeout(20):
                async for ham in ws:
                    if "setupComplete" in json.loads(ham):
                        break
            await ws.send(json.dumps(
                {"realtimeInput": {"text": cumle}}))

            hoparlor.yeni_tur()
            async with asyncio.timeout(45):
                async for ham in ws:
                    m = json.loads(ham)
                    ic = m.get("serverContent") or {}
                    for parca in (ic.get("modelTurn") or {}).get("parts", []):
                        g = parca.get("inlineData")
                        if g and g.get("data"):
                            hoparlor.ekle(base64.b64decode(g["data"]))
                    if ic.get("turnComplete"):
                        break
    except Exception as e:
        print(f"    ! {type(e).__name__}: {e}")
        return False

    # Ses bitene kadar bekle
    while hoparlor.caliyor:
        await asyncio.sleep(0.1)
    await asyncio.sleep(0.4)
    return True


async def calistir(sesler: list[str], model: str, hiz: float,
                   cumle: str) -> int:
    from ses import Hoparlor, SesHatasi, kulaklik_bul

    ayarlar.CIKIS_HIZ = hiz
    dongu = asyncio.get_running_loop()

    _, cikis, ad = kulaklik_bul()
    hoparlor = Hoparlor(dongu, cikis)
    try:
        hoparlor.basla()
    except SesHatasi as e:
        print(f"\n{e}")
        return 1

    print("=" * 68)
    print("PATI — SES SECIMI")
    print("=" * 68)
    print(f"\n  Model     : {model}")
    print(f"  Calma hizi: {hiz:g}x  "
          f"({int(ayarlar.CIKIS_HZ * hiz)} Hz)")
    if hiz != 1.0:
        print(f"              (ses ~%{(hiz-1)*100:.0f} daha tiz ve hizli)")
    print(f"  Cihaz     : {ad}")
    print(f"\n  Cumle: {cumle[:60]}...")
    print("\n  Her ses icin ayri baglanti kuruluyor, biraz surer.")
    print("  Not al: hangisi Pati'ye benziyor?\n")

    for i, s in enumerate(sesler, 1):
        print(f"  {i}/{len(sesler)}  {s}  —  {SESLER.get(s, '?')}")
        await sesi_dinlet(s, hoparlor, model, cumle)
        await asyncio.sleep(0.5)

    hoparlor.dur()
    print("\n" + "=" * 68)
    print("Begendigin sesi ayarlar.py'deki SES_ADI'na yaz.")
    print("Tizlik denemek icin:  python ses_secimi.py --ses <ad> --hiz 1.12")
    print()
    print("DIKKAT: tizlik ve hiz BIRLIKTE degisiyor (API'de ayri ayar yok).")
    print("Cok yukseltirsen robot tiz olur ama aceleci de olur.")
    print("=" * 68)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Pati'nin sesini kulakla sec")
    ap.add_argument("--hepsi", action="store_true", help="30 sesin hepsi")
    ap.add_argument("--ses", action="append", help="tek ses (tekrarlanabilir)")
    ap.add_argument("--hiz", type=float, default=1.0,
                    help="calma hizi: 1.0 dogal, 1.1 = %%10 tiz ve hizli")
    ap.add_argument("--model", default=ayarlar.VARSAYILAN_MODEL)
    ap.add_argument("--cumle", default=DENEME_CUMLESI)
    ap.add_argument("--liste", action="store_true", help="sesleri listele")
    a = ap.parse_args()

    if a.liste:
        print(f"\nGoogle'in {len(SESLER)} hazir sesi:\n")
        for ad, tanim in SESLER.items():
            im = "←" if ad in ADAYLAR else " "
            print(f"  {im} {ad:<16} {tanim}")
        print("\n  ← = cocuk robotu icin aday\n")
        return 0

    if a.ses:
        gecersiz = [s for s in a.ses if s not in SESLER]
        if gecersiz:
            print(f"Bilinmeyen ses: {gecersiz}")
            print("Liste icin: python ses_secimi.py --liste")
            return 1
        sesler = a.ses
    elif a.hepsi:
        sesler = list(SESLER)
    else:
        sesler = ADAYLAR

    model = ayarlar.MODELLER.get(a.model, a.model)
    try:
        return asyncio.run(calistir(sesler, model, a.hiz, a.cumle))
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
