# -*- coding: utf-8 -*-
"""
Pati — Asama 1 olcum prototipi.

    mikrofon -> 16 kHz PCM -> WSS -> Gemini Live -> 24 kHz PCM -> hoparlor

Kullanim:
    python pati.py --kontrol          API anahtari olmadan on kontrol
    python pati.py --cihazlar         ses cihazlarini listele
    python pati.py                    sohbet et ve olc
    python pati.py --dakika 20        20 dakikalik kesintisiz test (§12)
    python pati.py --model 2.5        oteki modeli olc
    python pati.py --senaryo          §12 test listesini gostererek calis

Cikista rapor ekrana basilir ve olcumler/ klasorune yazilir.
"""

from __future__ import annotations

import argparse
import asyncio
import sys
import time
from pathlib import Path

# ---------------------------------------------------------------------------
# Konsol kodlamasi
#
# Windows konsolu Turkce sistemlerde cp1254 olabiliyor ve "⚠" gibi
# karakterler yazilamayinca program COKUYOR. Gercekten yasandi:
# kulaklik bagli degilken uyari basilmaya calisildi ve UnicodeEncodeError
# ile her sey durdu — kullaniciya "kulaklik yok" demek yerine yigin
# dokumu gosterdi.
#
# Cikti kodlamasini UTF-8'e cevirip cevrilemeyen karakteri sessizce
# degistiriyoruz. Bir uyari mesaji ugruna program durmamali.
for _akis in (sys.stdout, sys.stderr):
    try:
        _akis.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

import ayarlar
import guvenlik
import kisilik
import metin
from olcum import Defter, simdi


# ---------------------------------------------------------------------------
# Cocugun ses seviyesi istegi
#
# Tur BITTIKTEN sonra cagriliyor — arac cagrisi degil. Gerekce
# metin.ses_istegi'nin basinda yazili: konusma yolunda arac cagrisi
# medyani ~790 ms'den ~1428 ms'ye cikariyor (PLAN.md).
# ---------------------------------------------------------------------------

def ses_istegini_uygula(tur, hoparlor, bildir=None) -> float | None:
    """
    Cocuk "sesini kis/ac" dediyse seviyeyi degistirir.

    Doner: yeni seviye, ya da istek yoksa None.

    Sinir kontrolu Hoparlor.seviye_ayarla'da: cocuk ustelese de
    ayarlar.SES_SEVIYESI_EN_FAZLA'yi gecemiyor, EN_AZ'in altina inip
    robotu tamamen susturamiyor.
    """
    if hoparlor is None:
        return None
    yon = metin.ses_istegi(tur.cocuk_dedi)
    if not yon:
        return None

    onceki = hoparlor.ses_seviyesi
    yeni = hoparlor.seviye_degistir(yon * ayarlar.SES_SEVIYESI_ADIM)

    # Sinira dayandiysa bunu SOYLE. Sessizce hicbir sey yapmamak,
    # cocugun "duymadi" sanip tekrar tekrar istemesine yol aciyor.
    if abs(yeni - onceki) < 1e-9:
        sinir = "en kisik" if yon < 0 else "en yuksek"
        if bildir:
            bildir(f"ses istegi: zaten {sinir} seviyede ({yeni:.2f})")
        return yeni

    if bildir:
        ok = "kisildi" if yon < 0 else "acildi"
        bildir(f"ses {ok}: {onceki:.2f} -> {yeni:.2f}")
    return yeni


# ---------------------------------------------------------------------------
# §12 test senaryolari — PLAN'dan birebir
# ---------------------------------------------------------------------------

SENARYOLAR = [
    ("20 tur normal sohbet",
     "Gecikme ortalamasi ve en kotusu",
     "OTOMATIK — program olcuyor"),
    ("Turkce telaffuz",
     "Dogal mi, robotik mi",
     "KULAKLA — sen karar vereceksin"),
    ("'bugun eve gittim ve elma yedim, Ingilizce nasil denir?'",
     "Ceviri + TELAFFUZ dogru mu",
     "KULAKLA — ozellikle telaffuza dikkat et"),
    ("Kisilik promptu: kisa cevap, cocuk dili, soru sormama",
     "10 denemede kac uyum",
     "YARI OTOMATIK — sayilabilir kurallari program sayiyor"),
    ("Sozunu kesme",
     "Robot konusurken araya gir, susuyor mu",
     "OTOMATIK — program olcuyor"),
    ("20 dakika kesintisiz",
     "15 dk sinirinda koptugunda kendini topluyor mu",
     "OTOMATIK — python pati.py --dakika 20"),
    ("Kufur/uygunsuz girdi",
     "Nasil karsiliyor, v1'deki gibi filtre gerekiyor mu",
     "YARI OTOMATIK — program kaydediyor, karari sen veriyorsun"),
    ("10 dk sohbet kotasi",
     "Gunde 1 saat icin ne gerekir",
     "OTOMATIK — token sayaci"),
]


def senaryolari_bas() -> None:
    print("\n" + "=" * 72)
    print("PLAN.md TEST LISTESI")
    print("=" * 72)
    for i, (ad, bakilacak, nasil) in enumerate(SENARYOLAR, 1):
        print(f"\n{i}. {ad}")
        print(f"   Ne bakilacak : {bakilacak}")
        print(f"   Nasil        : {nasil}")
    print("\n" + "=" * 72)
    print("KULAKLA isaretli olanlarda program sana yardim edemez.")
    print("Onlari not al; rapora elle yazacagiz.")
    print("=" * 72 + "\n")


# ---------------------------------------------------------------------------
# Uyum sayaci (PLAN.md: "sistem promptuna uyum, 10 denemede >=8")
# ---------------------------------------------------------------------------

class UyumSayaci:
    def __init__(self):
        self.kayitlar: list[dict] = []
        self.metinler: list[str] = []
        # Dolgu turlari AYRI sayiliyor, havuza karismiyor.
        # Sebep: bkz. olcum.Tur.dolgu — "Anlat anlat." diyen bir girdiye
        # model sohbeti surdurmek icin soruyla bitiyor. Bu bizim
        # olcum aletimizin urettigi bir davranis, robotun kusuru degil.
        self.dolgu_sayisi = 0
        self.dolgu_soruyla_bitti = 0

    def ekle(self, robot_dedi: str, dolgu: bool = False) -> None:
        if not robot_dedi:
            return
        sonuc = kisilik.uyum_denetle(robot_dedi)
        if dolgu:
            self.dolgu_sayisi += 1
            if not sonuc["soruyla_bitmedi"]:
                self.dolgu_soruyla_bitti += 1
            return
        self.kayitlar.append(sonuc)
        self.metinler.append(robot_dedi)

    def dolgu_notu(self) -> list[str]:
        """Kac dolgu turu sayima girmedi — sessizce atmiyoruz, yaziyoruz."""
        if not self.dolgu_sayisi:
            return []
        return [
            "",
            f"  {self.dolgu_sayisi} DOLGU turu bu sayima KATILMADI "
            f"(soruyla biten: {self.dolgu_soruyla_bitti}).",
            "  Dolgu = oturumu ayakta tutmak icin sorulan "
            "'Anlat anlat.' tipi girdi.",
            "  Cocugun soracagi sey degil; sayima katilsa robotun",
            "  davranisini degil olcum aletini olcmus olurduk.",
        ]

    def rapor(self) -> str:
        if not self.kayitlar:
            return "\n".join(["  Dokum alinmadi, uyum sayilamadi."]
                             + self.dolgu_notu())

        n = len(self.kayitlar)
        kurallar = sorted(self.kayitlar[0].keys())
        S = [f"  {n} cevap incelendi. Kural kural uyum:", ""]
        tam_uyan = 0
        for k in self.kayitlar:
            if all(k.values()):
                tam_uyan += 1
        for kural in kurallar:
            uyan = sum(1 for k in self.kayitlar if k[kural])
            oran = 100.0 * uyan / n
            S.append(f"    {kural:<22} {uyan:>3}/{n}  (%{oran:.0f})")
        S.append("")
        S.append(f"    {'HEPSINE BIRDEN UYAN':<22} {tam_uyan:>3}/{n}")
        S.append("")

        # PLAN.md kriteri 10 uzerinden yaziliyor; oranla karsilastiriyoruz.
        onluk = 10.0 * tam_uyan / n
        if onluk >= ayarlar.KRITER_PROMPT_UYUM_GECER:
            karar = "GECER"
        elif onluk >= ayarlar.KRITER_PROMPT_UYUM_SINIR:
            karar = "SINIRDA"
        else:
            karar = "KALIR"
        S.append(f"  KRITER: 10 denemede >=8 gecer · 6-7 sinirda · <6 kalir")
        S.append(f"  SONUC : {karar}   (10 uzerinden {onluk:.1f})")
        S.extend(self.dolgu_notu())
        S.append("")
        S.append("  ⚠ Bu sayi SADECE makinenin sayabilecegi kurallari olcer:")
        S.append("    uzunluk, soruyla bitis, emoji, markdown, hizmet cumlesi.")
        S.append("    'Cocuk dili mi, sicak mi, dogal mi' BURADA YOK —")
        S.append("    onu kulakla degerlendireceksin.")

        # Soruyla bitenleri LISTELIYORUZ, cunku makine "soruyla bitti mi"
        # diyebiliyor ama "hakli mi" diyemiyor. Bir bilmece soruyla
        # bitmek ZORUNDA; uzgun bir cocuga "anlatir misin?" demek de
        # promptun kendi istisnasi. Sayiyi otomatik affetmek olcumu
        # kandirmak olurdu — onun yerine insana gosteriyoruz.
        kacanlar = [m for k, m in zip(self.kayitlar, self.metinler)
                    if not k["soruyla_bitmedi"]]
        if kacanlar:
            S.append("")
            S.append("  SORUYLA BITEN CEVAPLAR — hakli mi, sen karar ver:")
            for m in kacanlar:
                S.append(f"    · {m[:100]}")
            S.append("")
            S.append("  (Bilmece soruyla bitmek zorunda. Uzgun cocuga")
            S.append("   'anlatir misin?' demek promptun kendi istisnasi.")
            S.append("   Bunlar kural ihlali DEGIL, olcumun siniri.)")
        return "\n".join(S)


# ---------------------------------------------------------------------------
# On kontrol — API anahtari GEREKTIRMEZ
#
# Anahtar gelmeden once dogrulanabilecek her seyi dogruluyoruz.
# v1'in dersi: "hazir" demeden once neyin denenmedigi belli olsun.
# ---------------------------------------------------------------------------

def on_kontrol() -> int:
    import json
    import canli

    print("=" * 72)
    print("ON KONTROL — API anahtari olmadan dogrulanabilenler")
    print("=" * 72)
    hata = 0

    # 1. Kutuphaneler
    print("\n1) Kutuphaneler")
    for ad in ("websockets", "sounddevice", "numpy"):
        try:
            m = __import__(ad)
            surum = getattr(m, "__version__", "?")
            print(f"   ✅ {ad:<14} {surum}")
        except Exception as e:
            print(f"   ❌ {ad:<14} {e}")
            hata += 1

    # 2. Ses cihazlari
    print("\n2) Ses cihazlari")
    mikrofon_var = False
    try:
        import sounddevice as sd
        varsayilan = sd.default.device
        cikis = sd.query_devices(kind="output")
        print(f"   ✅ hoparlor : {cikis['name']}")

        if varsayilan[0] is None or varsayilan[0] < 0:
            print("   ❌ MIKROFON YOK — Windows'ta varsayilan kayit cihazi")
            print("      tanimli degil (sd.default.device girisi = -1).")
            adaylar = [(i, d) for i, d in enumerate(sd.query_devices())
                       if d["max_input_channels"] > 0]
            if adaylar:
                print("\n      Cekirdek seviyesinde su girisler goruluyor,")
                print("      ama Windows bunlari uygulamalara ACMIYOR:")
                for i, d in adaylar:
                    print(f"        {i:>3}  {d['name']}")
                print("\n      Bu genelde su demek: mikrofon fisi TAKILI DEGIL,")
                print("      ya da Ses ayarlarinda devre disi.")
            else:
                print("      Hicbir giris cihazi goruntulenmiyor.")
            print("\n      YAPILACAK:")
            print("        1. Mikrofonlu kulakligi tak")
            print("        2. Ayarlar > Sistem > Ses > Giris'ten etkinlestir")
            print("        3. Gizlilik > Mikrofon: masaustu uygulamalarina izin")
            print("        4. Tekrar:  python pati.py --kontrol")
            hata += 1
        else:
            giris = sd.query_devices(kind="input")
            print(f"   ✅ mikrofon : {giris['name']}")
            mikrofon_var = True
    except Exception as e:
        print(f"   ❌ ses cihazi sorgulanamadi: {e}")
        hata += 1

    # 3. Ses hatti gercekten acilabiliyor mu (formatlar dahil)
    print("\n3) Ses hatti (16 kHz giris / 24 kHz cikis)")
    if not mikrofon_var:
        print(f"   ⏭ giris  {ayarlar.GIRIS_HZ} Hz — mikrofon olmadan "
              f"denenemez")
    else:
        try:
            import sounddevice as sd
            sd.check_input_settings(samplerate=ayarlar.GIRIS_HZ,
                                    channels=ayarlar.KANAL, dtype="int16")
            print(f"   ✅ giris  {ayarlar.GIRIS_HZ} Hz mono int16")
        except Exception as e:
            print(f"   ❌ giris  {ayarlar.GIRIS_HZ} Hz acilamadi: {e}")
            hata += 1
    try:
        import sounddevice as sd
        sd.check_output_settings(samplerate=ayarlar.CIKIS_HZ,
                                 channels=ayarlar.KANAL, dtype="int16")
        print(f"   ✅ cikis  {ayarlar.CIKIS_HZ} Hz mono int16")
    except Exception as e:
        print(f"   ❌ cikis  {ayarlar.CIKIS_HZ} Hz acilamadi: {e}")
        hata += 1

    # 4. Setup mesaji
    print("\n4) Setup mesaji (her iki uslup)")
    for uslup in (canli.USLUP_GENCONFIG, canli.USLUP_UST):
        try:
            m = canli.setup_mesaji(ayarlar.MODELLER["3.1"], uslup)
            boyut = len(json.dumps(m))
            alanlar = ", ".join(sorted(m["setup"].keys()))
            print(f"   ✅ {uslup:<16} {boyut} bayt")
            print(f"      alanlar: {alanlar}")
        except Exception as e:
            print(f"   ❌ {uslup}: {e}")
            hata += 1

    # 5. Kisilik ve guvenlik
    print("\n5) Kisilik ve guvenlik katmani")
    print(f"   ✅ sistem promptu {len(kisilik.SISTEM_PROMPTU)} karakter")
    ornekler = [("merhaba nasilsin", None),
                ("seni salak robot", "hafif"),
                ("amk", "agir"),
                ("canim sikildi", None),
                ("amcam geldi", None)]
    for yazi, beklenen in ornekler:
        sonuc = guvenlik.kufur_var_mi(yazi)
        im = "✅" if sonuc == beklenen else "❌"
        if sonuc != beklenen:
            hata += 1
        print(f"   {im} \"{yazi}\" -> {sonuc} (beklenen {beklenen})")

    # 6. Anahtar
    print("\n6) API anahtari")
    try:
        a = ayarlar.api_anahtari()
        print(f"   ✅ bulundu ({len(a)} karakter, "
              f"...{a[-4:] if len(a) > 4 else ''})")
    except SystemExit:
        print("   ⏳ HENUZ YOK — anahtar.txt'ye yapistir ya da")
        print("      PATI_API_ANAHTARI ortam degiskenine koy")

    print("\n" + "=" * 72)
    if hata:
        print(f"{hata} sorun var. Duzeltilmeden olcume gecilmemeli.")
    else:
        print("Yerel taraf hazir.")
    print()
    print("HENUZ OLCULMEYENLER (anahtar gelince olculecek):")
    print("  · gercek gecikme        · Turkce telaffuz kalitesi")
    print("  · sistem promptuna uyum · kota tuketimi")
    print("  · oturum kopma/toparlanma")
    print("Bunlar hakkinda su an HICBIR SEY bilmiyoruz. Tahmin PLAN.md'de.")
    print("=" * 72)
    return 1 if hata else 0


# ---------------------------------------------------------------------------
# Ana olcum
# ---------------------------------------------------------------------------

class _OnizlemeTarayiciHoparlor:
    """
    Ses onizlemesini ("Dinle") TARAYICIYA yollar.

    NEDEN VAR: onizleme bilgisayarin hoparlorunden caliyordu. Telefondan
    baglanan biri icin bu ise yaramaz — ayarlamaya calistigi sesi
    duyamiyor, ses bambaska bir odada caliyor.

    Calma hizi (tizlik) BURADA uygulanmiyor; tarayici uyguluyor
    (mikrofon.js, `hiz` alani). Sebep: sunucuda yeniden orneklemek
    fazladan is ve zaten tarayicinin AudioBuffer'i bunu bedava yapiyor —
    24 kHz veriyi daha yuksek orneklemeyle calmak hem hizlandiriyor hem
    tizlestiriyor, masaustu Hoparlor'un yaptigi seyin aynisi.
    """

    cikis_gecikmesi_ms = None

    def __init__(self, panel, hiz: float):
        self._panel = panel
        self._hiz = hiz or 1.0
        # Tarayici bitirdiginde bize haber vermiyor, o yuzden suresi
        # hesaplaniyor. sesi_dinlet() `caliyor` sonene kadar bekliyor;
        # yanlis hesap kisa keser ya da bosuna bekletir.
        self._biter = 0.0

    def basla(self) -> None:
        self._biter = time.monotonic()

    def yeni_tur(self) -> None:
        pass

    def ekle(self, pcm: bytes) -> None:
        self._panel.ses_yolla(pcm)
        sn = (len(pcm) / ayarlar.ORNEK_GENISLIK) / ayarlar.CIKIS_HZ / self._hiz
        self._biter = max(self._biter, time.monotonic()) + sn

    @property
    def caliyor(self) -> bool:
        return time.monotonic() < self._biter

    def temizle(self) -> None:
        self._panel.yayinla({"tip": "ses_temizle"})

    def dur(self) -> None:
        pass


async def arayuz_modu(model_kisa: str, etiket: str,
                      giris_cihaz, cikis_cihaz,
                      sayfa: str = "olcum", disa: bool = False) -> int:
    """
    Tarayicida gozler + canli olcum.

    SES TARAYICIDAN GECMIYOR — mikrofon ve hoparlor yine Python'da
    (bkz. sunucu.py bas yorumu). Tarayici sadece olay aliyor. Panel
    kapaliyken olcum birebir ayni calisir; PLAN.md'nin "araya
    tarayici koyma" kurali korunuyor.
    """
    import webbrowser

    import canli
    import sunucu
    from ses import Mikrofon, Hoparlor, SesHatasi

    model = ayarlar.MODELLER.get(model_kisa, model_kisa)
    dongu = asyncio.get_running_loop()

    # Hangi on yuz? Ikisi ayni arka ucu kullaniyor (bkz.
    # sunucu.klasor_ayarla aciklamasi).
    if sayfa == "pati":
        sunucu.klasor_ayarla(
            Path(__file__).resolve().parent.parent / "panel")

    panel = sunucu.Panel(dongu)
    panel.http_baslat()
    await panel.ws_baslat(disa_ac=disa)

    baslik = "PATI — CANLI" if sayfa == "pati" else "PATI — ASAMA 1 OLCUM PANELI"
    print("=" * 72)
    print(baslik)
    print("=" * 72)
    print(f"\n  Bu bilgisayardan:  {panel.adres}")
    if disa:
        # Telefondan bakmak icin. IP'yi burada bulmak zorundayiz cunku
        # kullanici "ipconfig" calistirmak zorunda kalmamali.
        import socket
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
        except OSError:
            ip = "127.0.0.1"
        finally:
            s.close()
        print(f"  TELEFONDAN:        http://{ip}:{sunucu.HTTP_PORT}/")
        print()
        print("  ! Ilk seferde Windows guvenlik duvari soracak —")
        print("    'Ozel aglar' isaretli olmali, yoksa telefon baglanmaz.")
    print("\n  Kapatmak icin bu pencerede Ctrl+C.\n")
    try:
        webbrowser.open(panel.adres)
    except Exception:
        pass

    # Oturum durumu — panelden baslat/durdur ile yonetiliyor
    durum: dict = {"baglanti": None, "gorevler": [], "mikrofon": None,
                   "hoparlor": None, "defter": None, "uyum": None,
                   "guv": None}

    def olay(metin: str, seviye: str = "") -> None:
        panel.yayinla({"tip": "olay", "metin": metin, "seviye": seviye})

    def hafiza_yayinla() -> None:
        import hafiza
        panel.yayinla({"tip": "hafiza", "ozet": hafiza.ozet()})

    def kullanim_yayinla() -> None:
        """
        Ebeveyn panelindeki "bugun / bu ay" kutulari.

        Acik oturumun suresi AYRI veriliyor cunku dosyaya ancak
        durdurunca yaziliyor — yoksa panel konusma boyunca hic
        degismez ve "sayac bozuk" gorunur.
        """
        import kullanim
        o = durum.get("kullanim_oturum")
        panel.yayinla({"tip": "kullanim",
                       **kullanim.ozet(o.gecen_sn if o else 0.0)})

    def gecikme_yayinla() -> None:
        panel.yayinla({
            "tip": "gecikme_ayar",
            "vad": ayarlar.VAD_SESSIZLIK_SUNUCU_MS,
            "yuz": ayarlar.YUZ_ARACI,
            "soz_kesme": ayarlar.SOZ_KESME,
        })

    def ses_yayinla() -> None:
        import ses_secimi
        panel.yayinla({
            "tip": "sesler",
            "liste": [{"ad": a, "tanim": t, "aday": a in ses_secimi.ADAYLAR}
                      for a, t in ses_secimi.SESLER.items()],
            "secili": ayarlar.SES_ADI,
            "hiz": ayarlar.CIKIS_HIZ,
        })
        panel.yayinla({
            "tip": "ses_seviyesi",
            "deger": ayarlar.SES_SEVIYESI,
            "en_az": ayarlar.SES_SEVIYESI_EN_AZ,
            "en_fazla": ayarlar.SES_SEVIYESI_EN_FAZLA,
        })

    # Ses onizleme: kendi kisa omurlu baglantisini ve hoparlorunu
    # kullaniyor. Sebep — ses oturum ACILIRKEN sabitleniyor, calisan
    # bir oturumun sesini degistirmek mumkun degil. O yuzden onizleme
    # ayri bir oturum aciyor.
    onizleme = {"calisiyor": False}

    async def ses_dene(ses_adi: str, hiz: float) -> None:
        import ses_secimi
        from ses import Hoparlor, SesHatasi

        if onizleme["calisiyor"]:
            return
        onizleme["calisiyor"] = True
        panel.yayinla({"tip": "ses_durum", "calisiyor": True})
        # Kuresel ayara DOKUNMUYORUZ; hizi hoparlore dogrudan veriyoruz.
        # Eskiden gecici olarak degistirip geri aliyorduk ve onizleme
        # sirasinda "Bu sesi kullan"a basilirsa yeni ayar eziliyordu.
        # Uzaktan (telefon) baglanan biri icin bilgisayarin hoparlorunden
        # calmak ise yaramaz — sesi duyamaz. Tarayici ses kanali acikken
        # onizlemeyi de oraya yolluyoruz.
        if panel.ses_istemcisi is not None:
            hop = _OnizlemeTarayiciHoparlor(panel, hiz)
        else:
            hop = Hoparlor(dongu, cikis_cihaz, hiz=hiz)
        try:
            hop.basla()
            olay(f"dinleniyor: {ses_adi} "
                 f"({ses_secimi.SESLER.get(ses_adi, '?')}) · "
                 f"tizlik {hiz:g}x")
            await ses_secimi.sesi_dinlet(
                ses_adi, hop, model, ses_secimi.DENEME_CUMLESI)
        except SesHatasi as e:
            olay(f"hoparlor acilamadi: {e}", "hata")
        except Exception as e:
            olay(f"ses denenemedi: {e}", "hata")
        finally:
            hop.dur()
            # ⚠ BURADA `ayarlar.CIKIS_HIZ = eski_hiz` YAZIYORDU ve
            #   eski_hiz HIC TANIMLI DEGILDI. Her "Dinle" basisinda
            #   NameError atiyor, finally yarim kaliyor ve
            #   onizleme["calisiyor"] True kaliyordu — yani dugme ilk
            #   basistan sonra bir daha calismiyordu.
            #   Hiz artik dogrudan Hoparlor'a veriliyor (yukari bak),
            #   kuresel ayara dokunulmuyor; geri alinacak bir sey yok.
            onizleme["calisiyor"] = False
            panel.yayinla({"tip": "ses_durum", "calisiyor": False})

    async def baslat(mod: str) -> None:
        # ⚠ CIFT BASLATMA KORUMASI — gercek kullanimda yasandi.
        #
        # Kullanici "Baslat"a iki kez basinca (ya da panel iki sekmede
        # acikken ikisinden de basilinca) iki baslat() es zamanli
        # calisiyordu. Sadece durum["baglanti"] kontrol etmek YETMIYOR:
        # o alan ancak baglan() bittikten SONRA doluyor, yani iki
        # coroutine de kontrolden gecip iki ayri Gemini oturumu aciyor.
        # Sonuc: iki Pati ayni anda konusuyor, ikisi de ayni hoparlore
        # yaziyor.
        #
        # Bayragi await'ten ONCE, es zamansiz olarak koyuyoruz.
        if durum["baglanti"] or durum.get("baslatiliyor"):
            olay("zaten calisiyor", "uyari")
            return
        durum["baslatiliyor"] = True
        try:
            await _baslat(mod)
        finally:
            durum["baslatiliyor"] = False

    async def _baslat(mod: str) -> None:

        defter = Defter(model, etiket or mod)
        uyum = UyumSayaci()
        guv = guvenlik.GuvenlikDefteri()
        durum.update(defter=defter, uyum=uyum, guv=guv)

        ayarlar.KAYIT_KLASORU.mkdir(parents=True, exist_ok=True)
        ham_yol = (ayarlar.KAYIT_KLASORU /
                   f"{defter.baslangic_saati:%Y%m%d-%H%M%S}-panel-ham.jsonl")

        # ---------------------------------------------------------------
        # TARAYICI MODU — sadece gosteri, OLCUM DEGIL
        #
        # Ses bilgisayarin degil, tarayicinin mikrofonundan geliyor ve
        # tarayicida caliniyor. Uzaktan (Cloudflare tunnel) baglanan
        # biri kendi telefonuyla Pati'yle konusabilsin diye.
        #
        # ⚠ BU MODDA OLCULEN GECIKME §4 KRITERI DEGILDIR: araya
        #   tarayici, ag ve tunel giriyor. Turlar kaynak="tarayici"
        #   olarak isaretleniyor ve kriter hesabina GIRMIYOR
        #   (olcum.gecikmeler() sadece kaynak="ses" turlari sayiyor).
        # ---------------------------------------------------------------
        tarayici = (mod == "tarayici")

        hoparlor = None
        if not tarayici:
            hoparlor = Hoparlor(dongu, cikis_cihaz)
            try:
                hoparlor.basla()
            except SesHatasi as e:
                olay(f"hoparlor acilamadi: {e}", "hata")
                return
            defter.cikis_gecikmesi_ms = hoparlor.cikis_gecikmesi_ms
            if hoparlor.yedege_dustu:
                olay("secili hoparlor bulunamadi (kulaklik kopmus "
                     "olabilir) — sistem varsayilanina gecildi", "uyari")

        mikrofon = None
        if mod == "ses":
            mikrofon = Mikrofon(dongu, giris_cihaz)
            try:
                mikrofon.basla()
            except SesHatasi as e:
                olay(f"mikrofon acilamadi: {e}", "hata")
                olay("metin moduna gecebilirsin", "uyari")
                if hoparlor:
                    hoparlor.dur()
                return
            defter.giris_gecikmesi_ms = mikrofon.giris_gecikmesi_ms
            olay("ortam gurultusu olculuyor, sessiz ol…")
            esik = await mikrofon.kalibre_et()
            olay(f"esik {esik:.0f} (zemin {mikrofon.zemin_gurultu:.0f})")

        # -- olcum kancalari (pati.py calistir() ile ayni mantik)
        def sustu(t_sustu: float) -> None:
            tur = defter.aktif
            if tur is None or tur.t_ilk_paket is not None:
                tur = defter.tur_ac()
                if tarayici:
                    # Kriter hesabina GIRMESIN (olcum.gecikmeler()
                    # sadece kaynak="ses" turlari sayiyor).
                    tur.kaynak = "tarayici"
            tur.t_sustu = t_sustu

        # Tarayici modunda hoparlor yok; sesi dogrudan tarayiciya
        # yolluyoruz. Baglanti nesnesi "hoparlor" arayuzu bekliyor,
        # o yuzden ayni metotlari tasiyan ince bir sarmalayici.
        class TarayiciHoparlor:
            caliyor = False
            cikis_gecikmesi_ms = None

            def __init__(self):
                # Ses seviyesi TARAYICI MODUNDA DA calisiyor. Sesi
                # tarayici caliyor ama olcekleme burada, gonderilmeden
                # once yapiliyor — yani "sesini kis" uzaktan baglanan
                # telefonda da ise yariyor.
                #
                # Gercek Hoparlor'un mantigini tekrar yazmiyoruz;
                # sinirlar ve carpim onun metotlarindan odunc aliniyor
                # ki iki yerde iki farkli davranis olusmasin.
                self.ses_seviyesi = ayarlar.SES_SEVIYESI

            _seviye_uygula = Hoparlor._seviye_uygula
            seviye_ayarla = Hoparlor.seviye_ayarla
            seviye_degistir = Hoparlor.seviye_degistir

            def yeni_tur(self):
                pass

            def ekle(self, pcm: bytes):
                panel.ses_yolla(self._seviye_uygula(pcm))

            def temizle(self):
                # Barge-in: tarayiciya "caldigini at" de.
                panel.yayinla({"tip": "ses_temizle"})

            def dur(self):
                pass

        if tarayici:
            hoparlor = TarayiciHoparlor()

        def konusma_basladi() -> None:
            b = durum.get("baglanti")
            if b and b.uykuda:
                # Uykudan uyandiran sey cocugun sesi. Yerel VAD burada,
                # ESP32'de de ayni islev olacak.
                b.uyandir()
                panel.yayinla({"tip": "durum", "durum": "dusunuyor"})
            else:
                panel.yayinla({"tip": "durum", "durum": "dinliyor"})
            if b:
                b.hareket_var()      # bosta kapatma sayacini sifirla
            if hoparlor.caliyor:
                tur = defter.aktif
                if tur and tur.t_kesme_konusma is None:
                    tur.t_kesme_konusma = simdi()

        def ilk_ses(t: float) -> None:
            tur = defter.aktif
            if tur and tur.t_ilk_hoparlor is None:
                tur.t_ilk_hoparlor = t
            panel.yayinla({"tip": "durum", "durum": "konusuyor"})
            # YANKI KORUMASI (yerel mod). Soz kesme kapaliyken Pati
            # konusurken mikrofon esigi yukseliyor; kendi sesini
            # "cocuk konusuyor" sanip sozunu kesmesin. Tarayici
            # modunda ayni isi mikrofon.js yariyor (yarimDubleks).
            #
            # Onceden `yanki_carpani` alani vardi ama HICBIR YERDEN
            # AYARLANMIYORDU — yani yerel modda yanki korumasi hic
            # devreye girmiyordu.
            m = durum.get("mikrofon")
            if m is not None:
                m.yanki_carpani = (1.0 if ayarlar.SOZ_KESME
                                   else ayarlar.YANKI_CARPANI)

        def ses_bitti() -> None:
            panel.yayinla({"tip": "durum", "durum": "bos"})
            m = durum.get("mikrofon")
            if m is not None:
                m.yanki_carpani = 1.0

        if mikrofon:
            mikrofon.konusma_bitti = sustu
            mikrofon.konusma_basladi = konusma_basladi
        if not tarayici:
            hoparlor.ilk_ses_calindi = ilk_ses
            hoparlor.bitti = ses_bitti

        sahte = mikrofon
        if sahte is None:
            class _Bos:
                def __init__(self):
                    self.kuyruk = asyncio.Queue()
            sahte = _Bos()

        if tarayici:
            # Tarayicidan gelen ham PCM'i mikrofon kuyruguna koyuyoruz;
            # ses_gonder() gerisini bilmiyor, aynen Gemini'ye akitiyor.
            from ses import SurekliKonusma

            surekli = SurekliKonusma()

            def tarayici_sesi(pcm: bytes) -> None:
                try:
                    sahte.kuyruk.put_nowait(pcm)
                except Exception:
                    pass
                b = durum.get("baglanti")
                if b is None:
                    return
                # ⚠ ONCEDEN HER PARCA `hareket_var()` CAGIRIYORDU.
                #   Tarayici saniyede ~15 parca yolluyor; bosta sayaci
                #   hic ilerlemiyordu ve robot ASLA uyumuyordu.
                #   Maliyetin en buyuk kalemi (ayarlar §BOSTA_KAPAT_SN)
                #   sessizce acik kaliyordu — 12 saat bosta ~$108/ay.
                #
                # Sonra sadece esigi gecen parca sayilmaya baslandi ama
                # TEK parca yetiyordu (20 ms). Kapi carpmasi, oyuncak
                # sesi, oksuruk uykudaki Pati'yi uyandiriyordu ve her
                # yanlis uyanma en az bir uyku suresi (varsayilan 90 sn)
                # ucret demek. Yerel mikrofon yolunda bu koruma VARDI:
                # ses.Mikrofon "konusma basladi" demek icin
                # ayarlar.VAD_KONUSMA_MS kadar KESINTISIZ ses ariyor.
                # Ayni kurali buraya da koyuyoruz.
                #
                # Gecikmeye etkisi yok: uyanma zaten ~600-700 ms
                # suruyor, 120 ms onun icinde kayboluyor.
                if surekli(pcm):
                    b.hareket_var()
            panel.ses_geri = tarayici_sesi

        baglanti = canli.Baglanti(model, defter, hoparlor, sahte, ham_yol,
                                  sessiz=True)

        canli_hafiza = _canli_hafiza_kancasi(
            defter, yaz=lambda m: olay(m), bitince=hafiza_yayinla,
            yenile=lambda s: baglanti.yenile(s))

        def tur_bitti(tur) -> None:
            # Tarayici modunda tur kaynagini ISARETLE. Yerel VAD
            # olmadigi icin t_sustu zaten dolmuyor ve tur kriter
            # hesabina girmiyor; yine de raporda "ses" gibi gorunmesin.
            if tarayici and tur.kaynak == "ses":
                tur.kaynak = "tarayici"
            uyum.ekle(tur.robot_dedi, dolgu=tur.dolgu)
            yeni_seviye = ses_istegini_uygula(
                tur, hoparlor, lambda m: olay(m, "bilgi"))
            if yeni_seviye is not None:
                panel.yayinla({"tip": "ses_seviyesi", "deger": yeni_seviye})
            if guv.incele(tur.cocuk_dedi, tur.robot_dedi):
                olay("guvenlik olayi kaydedildi", "uyari")
            panel.yayinla({
                "tip": "tur", "no": tur.no, "kaynak": tur.kaynak,
                "cocuk": tur.cocuk_dedi, "robot": tur.robot_dedi,
                "gecikme": tur.gecikme_ms(),
                "ag": tur.ag_gecikmesi_ms(),
                "yerel": tur.yerel_oynatma_ms(),
            })
            g = defter.gecikmeler()
            panel.yayinla({
                "tip": "ozet", "adet": len(g),
                "ortalama": (sum(g) / len(g)) if g else None,
                "en_kotu": max(g) if g else None,
            })
            panel.yayinla({"tip": "durum", "durum": "bos"})
            # Hafizayi konusma SIRASINDA guncelle — ebeveyn paneli
            # bilgiyi konusma bitmeden gorsun. Kosullar ve maliyet
            # hesabi hafiza.py'nin basinda.
            canli_hafiza(tur)

        baglanti.tur_bitti_geri = tur_bitti
        baglanti.ifade_geri = lambda i: panel.yayinla(
            {"tip": "ifade", "ifade": i})
        baglanti.olay_geri = olay

        panel.yayinla({"tip": "baglanti", "durum": "bekliyor",
                       "metin": "bağlanıyor…"})
        try:
            await baglanti.baglan()
        except canli.ProtokolHatasi as e:
            olay(f"baglanilamadi: {e}", "hata")
            hoparlor.dur()
            if mikrofon:
                mikrofon.dur()
            return

        gorevler = [asyncio.create_task(baglanti.al_dongusu()),
                    asyncio.create_task(baglanti.bosta_bekcisi())]
        if mikrofon or tarayici:
            gorevler.append(asyncio.create_task(baglanti.ses_gonder()))

        def uyku_durumu(uyuyor: bool) -> None:
            """
            Uyku panelde GORUNSUN ve faturaya YAZILMASIN.

            Iki hata birden kapaniyor:

            1. Panel uyurken de "dinliyor" yaziyordu. Gozler uykulu
               ifadeye geciyordu ama yazi degismedigi icin robotun
               uyudugu anlasilmiyordu — kullanici "uyuyor mu uyumuyor
               mu bilmiyorum" dedi. Haksiz degil: tek isaret gozlerdi.

            2. Kullanim sayaci duvar saatiyle isliyordu, yani uyku
               dakikalarini da ucretli sayiyordu. Oysa uykuda soket
               kapali ve panelin kendisi "Uyurken ucret islemez"
               yaziyor. 30.07.2026 kayitlarinda 53,7 dakikalik bir
               oturum 90 saniye sonra uyumustu; panel 53,7 dakikayi
               ucretli gosteriyordu.
            """
            o = durum.get("kullanim_oturum")
            if o is not None:
                if uyuyor:
                    o.duraklat()
                else:
                    o.devam()
            # "uykulu" gozlerin ifade adi; panel yaziyi ondan turetiyor.
            panel.yayinla({"tip": "durum",
                           "durum": "uykulu" if uyuyor else "bos"})
            kullanim_yayinla()

        # Uykuya gecince hafizayi cikar. Gercek kullanimda "durdur"
        # dugmesine kimse basmiyor — cocuk konusmayi birakip gidiyor,
        # robotta ise fis cekiliyor. Uyku, konusmanin bittiginin dogal
        # isareti.
        async def uykuda_hafiza() -> None:
            # ONCE sayaci durdur: cikarim birkac saniye suruyor ve o
            # saniyeler uykuya ait.
            uyku_durumu(True)
            try:
                h = await _hafizayi_guncelle(defter, yaz=lambda m: olay(m))
                if h.get("ad"):
                    olay(f"adini ogrendi: {h['ad']}")
                for yeni_bilgi in h.get("eklenen", []):
                    olay(f"hatirladi: {yeni_bilgi}")
                hafiza_yayinla()
            except Exception as e:
                olay(f"hafiza guncellenemedi: {e}", "hata")

        baglanti.uyku_kancasi = uykuda_hafiza
        baglanti.uyanma_kancasi = lambda: uyku_durumu(False)

        import kullanim
        durum.update(baglanti=baglanti, gorevler=gorevler,
                     mikrofon=mikrofon, hoparlor=hoparlor,
                     kullanim_oturum=kullanim.Oturum())
        kullanim_yayinla()

        panel.yayinla({"tip": "baglanti", "durum": "bagli",
                       "metin": f"bağlı · {model_kisa}"})
        # Panelde ham model adi gecmesin — arkadasa gosterilirken
        # "gemini-3.1-flash-live-preview" yerine kisa etiket yeter.
        olay(f"baglandi ({model_kisa})")
        # Hangi sesle basladigini acikca yaz — "dinledigim ses bu mu"
        # sorusu bir daha sorulmasin.
        olay(f"ses: {ayarlar.SES_ADI} · tizlik {ayarlar.CIKIS_HIZ:g}x")
        if mod == "ses":
            olay("konusabilirsin")
        elif tarayici:
            olay("TARAYICI MODU — ses bu cihazin mikrofonundan geliyor",
                 "uyari")
            olay("gecikme sayilari §4 kriteri DEGIL (araya tarayici ve "
                 "ag giriyor)", "uyari")
        else:
            olay("asagidaki kutuya yazarak konus")

        await asyncio.sleep(0.3)
        await baglanti.metin_gonder(kisilik.ACILIS_ISTEGI)

    async def durdur() -> None:
        b = durum.get("baglanti")
        if not b:
            return
        await b.kapat()
        for g in durum["gorevler"]:
            g.cancel()
        if durum["mikrofon"]:
            durum["mikrofon"].dur()
        if durum["hoparlor"]:
            durum["hoparlor"].dur()

        defter = durum["defter"]
        defter.uyanma_sureleri = list(b.uyanma_sureleri)

        # Hafizayi guncelle ve paneli haberdar et
        try:
            h_sonuc = await _hafizayi_guncelle(defter, yaz=lambda s: olay(s))
            if h_sonuc.get("ad"):
                olay(f"adini ogrendi: {h_sonuc['ad']}")
            # ⚠ Degisken adi `b` DEGIL: yukaridaki `b` baglanti nesnesi
            #   ve asagida `b.ifade_defteri` kullaniliyor. Once burada
            #   `for b in ...` yaziliyordu ve Pati yeni bir sey
            #   ogrendiginde durdur() AttributeError ile yarim kaliyordu.
            for yeni_bilgi in h_sonuc.get("eklenen", []):
                olay(f"hatirladi: {yeni_bilgi}")
        except Exception as e:
            olay(f"hafiza guncellenemedi: {e}", "hata")
        hafiza_yayinla()

        tam = defter.rapor() + "\n" + _ek_bolumler(
            durum["uyum"], durum["guv"], metin_modu_mu=False,
            ifade_defteri=b.ifade_defteri)
        yol = defter.kaydet()
        yol.write_text(tam, encoding="utf-8")
        print("\n" + tam)
        print(f"\nRapor kaydedildi: {yol}")
        olay(f"durduruldu · rapor: {yol.name}")

        # Sureyi ONCE kaydet, sonra durumu temizle — sirasi yanlis
        # olursa oturum nesnesi kaybolur ve o dakikalar sayilmaz.
        o = durum.get("kullanim_oturum")
        if o is not None:
            o.bitir()

        durum.update(baglanti=None, gorevler=[], mikrofon=None,
                     hoparlor=None, kullanim_oturum=None)
        kullanim_yayinla()
        panel.yayinla({"tip": "baglanti", "durum": "bekliyor",
                       "metin": "durduruldu"})
        panel.yayinla({"tip": "durum", "durum": "bos"})

    def komut(k: dict) -> None:
        tip = k.get("tip")
        if tip == "baslat":
            asyncio.ensure_future(baslat(k.get("mod", "ses")))
        elif tip == "durdur":
            asyncio.ensure_future(durdur())
        elif tip == "hafiza":
            hafiza_yayinla()
        elif tip == "hafiza_sifirla":
            import hafiza
            hafiza.her_seyi_unut()
            olay("hafiza sifirlandi — robot cocugu bastan taniyacak",
                 "uyari")
            hafiza_yayinla()
        elif tip == "bilgi_sil":
            import hafiza
            if hafiza.bilgi_sil(int(k.get("id", -1))):
                olay("bilgi silindi")
            hafiza_yayinla()
        elif tip == "gecikme_ayar":
            # PLAN.md: "Gemini'nin sustu karari 0.5-0.8 sn — EN BUYUK
            # PARCA, ayarlanabilir." Bugune kadar hic dokunulmadi.
            v = k.get("vad")
            eski_vad = ayarlar.VAD_SESSIZLIK_SUNUCU_MS
            eski_yuz = ayarlar.YUZ_ARACI
            ayarlar.VAD_SESSIZLIK_SUNUCU_MS = None if v in (None, "") else int(v)
            ayarlar.YUZ_ARACI = bool(k.get("yuz", ayarlar.YUZ_ARACI))

            # IKISI DE setup mesajinda gidiyor (canli.setup_mesaji):
            # VAD `realtimeInputConfig`te, yuz araci `tools`ta ve
            # sistem promptunda. Yani calisan oturumda degismezler —
            # ama yeniden baglanma zaten cozulmus bir is. Onceden
            # "sonraki baslatmada gecerli" deniyordu; artik tur
            # arasinda kendiliginden uygulaniyor.
            b = durum.get("baglanti")
            degisti = (eski_vad != ayarlar.VAD_SESSIZLIK_SUNUCU_MS
                       or eski_yuz != ayarlar.YUZ_ARACI)
            if b is not None and degisti:
                b.yenile("konusma ayari degisti")
                olay(f"sustu karari "
                     f"{ayarlar.VAD_SESSIZLIK_SUNUCU_MS or 'varsayilan'}"
                     f" · gozlerle duygu "
                     f"{'acik' if ayarlar.YUZ_ARACI else 'KAPALI'}"
                     f" — konusma arasinda gecerli")
            else:
                olay(f"sustu karari "
                     f"{ayarlar.VAD_SESSIZLIK_SUNUCU_MS or 'varsayilan'}"
                     f" · gozlerle duygu "
                     f"{'acik' if ayarlar.YUZ_ARACI else 'KAPALI'}")
            gecikme_yayinla()
        elif tip == "soz_kesme":
            # Tarayici modunda esas isi tarayici yapiyor
            # (mikrofon.js yarimDubleks); burada saklaniyor ki yerel
            # mod da ayni ayari kullansin ve robota tasinabilsin.
            ayarlar.SOZ_KESME = bool(k.get("acik"))
            olay("soz kesme " + ("ACIK — kulaklik gerekiyor"
                                 if ayarlar.SOZ_KESME else "kapali"))
            gecikme_yayinla()
        elif tip == "ses_dene":
            asyncio.ensure_future(ses_dene(
                k.get("ses", ayarlar.SES_ADI),
                float(k.get("hiz", ayarlar.CIKIS_HIZ))))
        elif tip == "ses_ayarla":
            eski_ses = ayarlar.SES_ADI
            ayarlar.SES_ADI = k.get("ses", ayarlar.SES_ADI)
            ayarlar.CIKIS_HIZ = float(k.get("hiz", ayarlar.CIKIS_HIZ))

            # SES DEGISTIYSE OTURUMU TAZELE.
            #
            # Ses adi setup mesajinda gidiyor ve setup oturum acilirken
            # BIR KEZ gonderiliyor — calisan oturumun sesini
            # degistirmek mumkun degil. Onceden burada sadece
            # "(sonraki baslatmada gecerli)" yaziyordu; panel ise
            # "cumlesini bitirince" diyordu. Panel YALAN SOYLUYORDU.
            #
            # canli.setup_mesaji() SES_ADI'ni cagri aninda okuyor, yani
            # yeniden baglanmak yeni sesi getiriyor. Yeniden baglanma
            # zaten cozulmus bir is (GoAway, olculen bosluk 568 ms,
            # hafiza korunuyor) — ayni yol kullaniliyor.
            b = durum.get("baglanti")
            if b is not None and ayarlar.SES_ADI != eski_ses:
                b.yenile(f"ses {eski_ses} -> {ayarlar.SES_ADI}")
                olay(f"ses: {ayarlar.SES_ADI} · tizlik "
                     f"{ayarlar.CIKIS_HIZ:g}x — konusma arasinda gecerli")
            else:
                olay(f"ses ayarlandi: {ayarlar.SES_ADI} · "
                     f"tizlik {ayarlar.CIKIS_HIZ:g}x")
            ses_yayinla()
        elif tip == "ses_seviyesi":
            # Ebeveynin kaydiricisi. Cocugun sesli istegiyle AYNI
            # sinirlardan geciyor (Hoparlor.seviye_ayarla) — panelden
            # de robot tamamen susturulamiyor, kirpma seviyesine
            # cikarilamiyor.
            #
            # Calisan oturum varsa ona uygulaniyor; ayrica varsayilan
            # olarak da yaziliyor ki sonraki baslatma hatirlasin.
            yeni = float(k.get("deger", ayarlar.SES_SEVIYESI))
            h = durum.get("hoparlor")
            if h is not None and hasattr(h, "seviye_ayarla"):
                yeni = h.seviye_ayarla(yeni)
            else:
                yeni = max(ayarlar.SES_SEVIYESI_EN_AZ,
                           min(ayarlar.SES_SEVIYESI_EN_FAZLA, yeni))
            ayarlar.SES_SEVIYESI = yeni
            olay(f"ses seviyesi: {yeni:.0%}")
            panel.yayinla({"tip": "ses_seviyesi", "deger": yeni})
        elif tip == "kullanim":
            kullanim_yayinla()
        elif tip == "cocuk_ayarla":
            # Ebeveyn panelinden ad/yas. Bir sonraki oturumun
            # promptuna giriyor (prompt oturum acilirken sabitleniyor).
            import hafiza
            hafiza.cocugu_tanimla(k.get("ad") or None, k.get("yas") or None)
            olay(f"cocuk bilgisi kaydedildi (sonraki konusmada gecerli)")
            hafiza_yayinla()
        elif tip == "ebeveyn_notu":
            import hafiza
            n = hafiza.ebeveyn_notu_kaydet(k.get("yazi", ""))
            olay(f"ebeveyn notu kaydedildi ({len(n)} karakter, "
                 f"sonraki konusmada gecerli)")
            hafiza_yayinla()
        elif tip == "uyku":
            # Bostaki oturumu ne kadar sonra kapatacagi. Maliyetin en
            # buyuk kalemi bu (ayarlar.py §BOSTA_KAPAT_SN).
            dk = float(k.get("dakika", 4))
            ayarlar.BOSTA_KAPAT_SN = max(60.0, min(900.0, dk * 60.0))
            olay(f"uyku: {ayarlar.BOSTA_KAPAT_SN / 60:g} dk")
            panel.yayinla({"tip": "uyku",
                           "dakika": ayarlar.BOSTA_KAPAT_SN / 60})
        elif tip == "metin":
            b = durum.get("baglanti")
            if b:
                panel.yayinla({"tip": "durum", "durum": "dusunuyor"})
                asyncio.ensure_future(b.metin_gonder(k.get("yazi", ""),
                                                     olc=True))

    panel.komut_geri = komut
    hafiza_yayinla()
    ses_yayinla()
    gecikme_yayinla()
    kullanim_yayinla()
    panel.yayinla({"tip": "uyku", "dakika": (ayarlar.BOSTA_KAPAT_SN or 240) / 60})

    try:
        while True:
            # 20 saniyede bir kullanim sayacini tazele. Daha sik
            # yapmanin anlami yok (dakika gosteriyoruz), daha seyrek
            # yapinca "sayac donmus" goruntusu veriyor.
            await asyncio.sleep(20)
            if durum.get("kullanim_oturum"):
                kullanim_yayinla()
    except (KeyboardInterrupt, asyncio.CancelledError):
        pass
    finally:
        print("\nKapatiliyor...")
        await durdur()
        await panel.kapat()
    return 0


async def metin_modu(model_kisa: str, dakika: float, etiket: str,
                     kisa: bool, cikis_cihaz) -> int:
    """
    MIKROFONSUZ olcum — PLAN.md'nin 8 testinden 6'si.

    Neden var: mikrofon gelmeden once yapilabilecek isi bekletmek
    gereksiz. Hoparlor calisiyor, dokum geliyor, oturum yonetimi ayni.

    ⚠ BU MOD §4'UN ANA KRITERINI OLCMEZ. Metin gonderince Gemini'nin
      "sustu" karari devreye girmiyor. Rapor bu turlari ayri tutuyor
      ve ortalamaya katmiyor — karistirsaydik kriteri haksiz yere
      "gecer" gosterirdik.
    """
    import canli
    import senaryolar
    from ses import Hoparlor, SesHatasi

    model = ayarlar.MODELLER.get(model_kisa, model_kisa)
    dongu = asyncio.get_running_loop()
    defter = Defter(model, etiket or "metin")
    uyum = UyumSayaci()
    guv = guvenlik.GuvenlikDefteri()

    ayarlar.KAYIT_KLASORU.mkdir(parents=True, exist_ok=True)
    ham_yol = (ayarlar.KAYIT_KLASORU /
               f"{defter.baslangic_saati:%Y%m%d-%H%M%S}-metin-ham.jsonl"
               ) if ayarlar.HAM_KAYIT else None

    print("=" * 72)
    print(f"PATI — Asama 1 METIN MODU (mikrofonsuz)   ·   model: {model}")
    print("=" * 72)
    print("\n⚠ Bu mod GECIKME KRITERINI olcmez. Olctugu seyler:")
    print("   Turkce telaffuz (kulakla) · prompt uyumu · kufur davranisi")
    print("   · oturum toparlanmasi · kota tuketimi · VAD maliyeti")
    print("\n  SESI DUYMAN LAZIM — hoparlorunu ac.\n")

    hoparlor = Hoparlor(dongu, cikis_cihaz)
    try:
        hoparlor.basla()
    except SesHatasi as e:
        print(f"\n{e}")
        return 1
    defter.cikis_gecikmesi_ms = hoparlor.cikis_gecikmesi_ms

    def ilk_ses(t: float) -> None:
        tur = defter.aktif
        if tur and tur.t_ilk_hoparlor is None:
            tur.t_ilk_hoparlor = t

    def tur_bitti(tur) -> None:
        uyum.ekle(tur.robot_dedi, dolgu=tur.dolgu)
        ses_istegini_uygula(tur, hoparlor, lambda m: print(f"  🔊 {m}"))
        if guv.incele(tur.cocuk_dedi, tur.robot_dedi):
            print("  ⚠ guvenlik olayi kaydedildi")

    hoparlor.ilk_ses_calindi = ilk_ses

    # Mikrofon yok; Baglanti bos bir kuyruk bekliyor.
    class SahteMikrofon:
        def __init__(self):
            self.kuyruk = asyncio.Queue()
    sahte = SahteMikrofon()

    baglanti = canli.Baglanti(model, defter, hoparlor, sahte, ham_yol)
    baglanti.tur_bitti_geri = tur_bitti

    print(f"Baglaniliyor: {model}")
    try:
        await baglanti.baglan()
    except canli.ProtokolHatasi as e:
        print(f"\n{e}")
        hoparlor.dur()
        return 1
    print("  baglandi.\n")

    # bosta_bekcisi ATLANMISTI ve GoAway kontrolu onun icinde oldugu icin
    # metin modunda hic calismadi: 13 dakikalik dogrulama kosusunda kopma
    # yine tur ortasina denk geldi ve cevaplar yapisti.
    alici = asyncio.create_task(baglanti.al_dongusu())
    bekci = asyncio.create_task(baglanti.bosta_bekcisi())

    liste = senaryolar.kisa_set() if kisa else senaryolar.tam_set()
    t_bitis = simdi() + dakika * 60 if dakika else None
    dolgu_no = 0

    async def bir_tur(etiket_: str, yazi: str, bakilacak: str,
                      dolgu: bool = False) -> None:
        print("-" * 72)
        print(f"[{etiket_}]  {yazi}")
        if bakilacak:
            print(f"   ↳ bakilacak: {bakilacak}")
        baglanti.tur_bitti_olayi.clear()
        await baglanti.metin_gonder(yazi, olc=True, dolgu=dolgu)
        try:
            async with asyncio.timeout(60):
                await baglanti.tur_bitti_olayi.wait()
        except asyncio.TimeoutError:
            print("   ⚠ 60 saniyede cevap tamamlanmadi")
        # Sesin calinip bitmesini bekle ki kulakla dinleyebilelim
        while hoparlor.caliyor:
            await asyncio.sleep(0.1)
        await asyncio.sleep(0.4)

    try:
        for etiket_, yazi, bakilacak in liste:
            if not baglanti.calisiyor:
                break
            await bir_tur(etiket_, yazi, bakilacak)

        # Sure dolmadiysa dolgu sorulariyla oturumu ayakta tut
        # (20 dakika testi: 15 dk sinirini gormek icin sart)
        while t_bitis and simdi() < t_bitis and baglanti.calisiyor:
            yazi = senaryolar.DOLGU[dolgu_no % len(senaryolar.DOLGU)]
            dolgu_no += 1
            kalan = (t_bitis - simdi()) / 60.0
            await bir_tur(f"dolgu-{dolgu_no:02d} ({kalan:.1f} dk kaldi)",
                          yazi, "", dolgu=True)
    except (KeyboardInterrupt, asyncio.CancelledError):
        pass
    finally:
        print("\nKapatiliyor...")
        await baglanti.kapat()
        alici.cancel()
        bekci.cancel()
        hoparlor.dur()

    try:
        await _hafizayi_guncelle(defter)
    except Exception as e:
        print(f"  ⚠ hafiza guncellenemedi: {e}")

    tam = defter.rapor() + "\n" + _ek_bolumler(uyum, guv, metin_modu_mu=True)
    print("\n" + tam)
    yol = defter.kaydet()
    yol.write_text(tam, encoding="utf-8")
    print(f"\nRapor kaydedildi: {yol}")
    if ham_yol:
        print(f"Ham mesajlar    : {ham_yol}")
    return 0


async def _hafizayi_guncelle(defter, yaz=print, canli: bool = False) -> dict:
    """
    Dokumun HENUZ CIKARILMAMIS kismindan kalici bilgi cikarir.

    Konusma yolunun disinda: ayri bir HTTPS istegi, tur bittikten
    sonra. Cocuk beklemiyor. (Yuz aracinda arac cagrisinin medyani
    ~790 ms'den ~1428 ms'ye cikardigini olcmustuk; hafiza icin ayni
    bedeli odemeye gerek yok.)

    canli=True  konusma sirasindaki tetikleme. Oturum sayaci artmaz,
                yeni bir sey cikmadiginda sessiz kalir.
    canli=False oturum sonu (durdur / uyku). Sayac artar, sonuc yazilir.
    """
    import hafiza

    def oturumu_say() -> None:
        # Bir sohbet = bir oturum. Uyku ve "durdur" ayni defterle iki
        # kez buraya geliyor; sayac bir kez artmali.
        if canli or defter.oturum_sayildi:
            return
        defter.oturum_sayildi = True
        hafiza.oturum_sayaci()

    bas = getattr(defter, "hafiza_isaretci", 0)
    satirlar = []
    for t in defter.turlar[bas:]:
        if t.cocuk_dedi:
            # (!) = cocuk bunu OZELLIKLE hatirlanmasini istedi.
            #
            # Kullanicinin istegi: "cocuk 'bu bilgiyi unutma' derse
            # soyledigi bilgiyi KESIN OLARAK hafizaya alsin." Prompt
            # normalde "emin degilsen yazma" diyor ve model temkinli
            # davranip atliyordu. Isaret o kurali bu satir icin
            # kaldiriyor (bkz. hafiza.CIKARIM_PROMPTU).
            #
            # Maliyeti uc karakter; ESP32'de de ayni sey yapiliyor.
            im = " (!)" if metin.unutma_istegi(t.cocuk_dedi) else ""
            satirlar.append(f"Cocuk{im}: {t.cocuk_dedi}")
        if t.robot_dedi:
            satirlar.append(f"Robot: {t.robot_dedi}")

    konusma = "\n".join(satirlar)
    if not konusma.strip():
        # Konusma yasandi ama cikarilacak YENI bir sey yok (canli
        # cikarim hepsini almis olabilir). Oturum yine de sayilir.
        oturumu_say()
        return {"eklenen": [], "ad": None, "hata": "dokum yok"}

    # Isaretci istek ATILMADAN once ilerliyor: cikarim 1-2 saniye
    # suruyor ve o sirada yeni tur bitebiliyor. Boyle olmazsa ayni
    # cumleler ikinci kez gonderilir.
    defter.hafiza_isaretci = len(defter.turlar)

    if not canli:
        yaz("\nHafiza guncelleniyor...")
    sonuc = await hafiza.cikar(konusma, ayarlar.api_anahtari())
    oturumu_say()

    if sonuc["hata"]:
        # Basarisiz istek dokumu YAKMASIN: isaretci geri aliniyor,
        # bu turlar oturum sonunda tekrar denenecek.
        defter.hafiza_isaretci = bas
        yaz(f"  ⚠ cikarim basarisiz: {sonuc['hata']}")
        return sonuc

    if sonuc.get("reddedilen_ad"):
        # Sessizce yutmuyoruz: bu suzgec olculmus bir hatayi kapatiyor
        # (robotun yeni adi cocugun adi sanilmisti) ve yanlis calisirsa
        # gunlukte gorunmesi lazim.
        yaz(f"  ad reddedildi (robotun adi): {sonuc['reddedilen_ad']}")
    if sonuc["ad"]:
        yaz(f"  cocugun adi ogrenildi: {sonuc['ad']}")
    if sonuc.get("yas"):
        yaz(f"  yasi ogrenildi: {sonuc['yas']}")
    if sonuc.get("robot_adi"):
        yaz(f"  artik adi: {sonuc['robot_adi']}")
    if sonuc["eklenen"]:
        yaz(f"  {len(sonuc['eklenen'])} yeni bilgi:")
        for b in sonuc["eklenen"]:
            yaz(f"    · {b}")
    elif not canli:
        yaz("  yeni kalici bilgi cikmadi")
    return sonuc


def _canli_hafiza_kancasi(defter, yaz, bitince=None, yenile=None):
    """
    Konusma sirasinda hafizayi guncelleyen tetikleyici.

    NEDEN VAR: cikarim eskiden sadece oturum bitince (90 sn sessizlik
    ya da "durdur") calisiyordu. Cocuk "kedimin adini unutma" dedikten
    sonra ebeveyn panelinde o bilgiyi gormek dakikalar suruyordu.

    NE YAPMIYOR: her turda cikarim. Kosullar hafiza.cikarim_zamani_mi
    icinde ve sebepleri hafiza.py'nin basinda yazili — ozeti: en az 2
    yeni tur + 25 sn ara, cocuk "unutma" dediyse 5 sn.

    Doner: tur_bitti kancasina takilacak fonksiyon.
    """
    import hafiza

    durum = {"son": simdi(), "calisiyor": False}

    def kanca(tur) -> None:
        # Ayni anda iki cikarim istemiyoruz: ikisi de ayni dokumu
        # gonderirdi ve `kez` sayaclari sisirdi.
        if durum["calisiyor"]:
            return
        yeni = len(defter.turlar) - getattr(defter, "hafiza_isaretci", 0)
        if not hafiza.cikarim_zamani_mi(
                yeni, simdi() - durum["son"],
                metin.unutma_istegi(tur.cocuk_dedi)):
            return
        durum["calisiyor"] = True

        async def kos() -> None:
            try:
                s = await _hafizayi_guncelle(defter, yaz=yaz, canli=True)
                # ROBOTUN ADI DEGISTIYSE OTURUMU TAZELE.
                #
                # Ad promptun ilk cumlesinde ("Senin adin X") ve sistem
                # promptu oturum basinda SABITLENIYOR. Tazelemezsek
                # cocuk "artik adin Osman" dedikten sonra robot konusma
                # boyunca kendine Pati demeye devam eder.
                #
                # yenile() hemen kesmiyor, ilk dogal boslukta yapiyor;
                # olculdu: 568 ms, hafiza korunuyor (canli.py §yenile).
                if s.get("robot_adi") and yenile:
                    yenile(f"robotun adi degisti: {s['robot_adi']}")
                if bitince:
                    bitince()
            except Exception as e:
                yaz(f"hafiza guncellenemedi: {e}")
            finally:
                durum["son"] = simdi()
                durum["calisiyor"] = False

        asyncio.ensure_future(kos())

    return kanca


def _ek_bolumler(uyum: "UyumSayaci", guv, metin_modu_mu: bool,
                 ifade_defteri=None) -> str:
    ek = []
    if ifade_defteri is not None:
        ek.append("-" * 72)
        ek.append("9b) YUZ IFADESI ARACI   (PLAN.md'daki varsayimin sinavi)")
        ek.append("-" * 72)
        ek.append("  PLAN.md: 'yapay zeka gozlerin ifadesini arac cagirarak")
        ek.append("  kendisi yonetiyor... yani uzulunce gozler uzuluyor,")
        ek.append("  BEDAVA.' Asama 4'un tamami buna dayaniyor ama Turkce")
        ek.append("  sohbette olculmemisti. Olcum:")
        ek.append("")
        ek.append(ifade_defteri.rapor())
        ek.append("")
    ek.append("-" * 72)
    ek.append("10) SISTEM PROMPTUNA UYUM   (PLAN.md)")
    ek.append("-" * 72)
    ek.append(uyum.rapor())
    ek.append("")
    ek.append("-" * 72)
    ek.append("11) COCUK GUVENLIGI   (PLAN.md)")
    ek.append("-" * 72)
    ek.append(guv.rapor())
    ek.append("")
    ek.append("-" * 72)
    ek.append("12) BU PROGRAMIN OLCEMEDIKLERI")
    ek.append("-" * 72)
    ek.append("  Asagidakiler bu raporda YOK. Kulakla degerlendirilecek:")
    ek.append("    · Turkce telaffuz dogal mi, robotik mi")
    ek.append("    · Ingilizce cumleler dogru telaffuz ediliyor mu")
    ek.append("    · Ses cocuga sicak geliyor mu")
    ek.append("    · Sohbet 'cocuk dili'nde mi")
    ek.append("")
    if metin_modu_mu:
        ek.append("  METIN MODU OLDUGU ICIN AYRICA OLCULMEDI:")
        ek.append("    · Cocuk sustu -> ilk ses (PLAN.md ANA KRITERI)")
        ek.append("    · Sozunu kesme tepkisi")
        ek.append("    Ikisi de mikrofon ister:  python pati.py")
        ek.append("")
    ek.append("  Ve bu raporda olan hicbir sey ESP32 hakkinda kanit degil.")
    ek.append("  Asama 2 gercek kartta olculecek (PLAN.md kurali).")
    ek.append("=" * 72)
    return "\n".join(ek)


async def calistir(model_kisa: str, dakika: float, etiket: str,
                   hoparlor_modu: bool, senaryo: bool,
                   giris_cihaz, cikis_cihaz) -> int:
    import canli
    from ses import Mikrofon, Hoparlor, SesHatasi

    model = ayarlar.MODELLER.get(model_kisa, model_kisa)
    dongu = asyncio.get_running_loop()
    defter = Defter(model, etiket)
    uyum = UyumSayaci()
    guv = guvenlik.GuvenlikDefteri()

    ayarlar.KAYIT_KLASORU.mkdir(parents=True, exist_ok=True)
    ham_yol = (ayarlar.KAYIT_KLASORU /
               f"{defter.baslangic_saati:%Y%m%d-%H%M%S}-ham.jsonl"
               ) if ayarlar.HAM_KAYIT else None

    if senaryo:
        senaryolari_bas()

    mikrofon = Mikrofon(dongu, giris_cihaz)
    hoparlor = Hoparlor(dongu, cikis_cihaz)

    # -- olcum kancalari ---------------------------------------------------

    def sustu(t_sustu: float) -> None:
        """
        Cocuk sustu. Damga zaten sessizligin BASLADIGI ana ait.

        Aktif tur varsa ve robot henuz cevap vermediyse damgayi
        GUNCELLIYORUZ. Sebep: cocuk cumle ortasinda nefes alinca bizim
        yerel VAD'imiz "sustu" diyor ama Gemini daha uzun bekliyor ve
        hakli olarak beklemeye devam ediyor. Guncellemezsek gecikmeyi
        oldugundan buyuk olcerdik — v1'in "yanlis seyi olctum"
        hatasinin bu projedeki karsiligi tam olarak bu olurdu.
        """
        tur = defter.aktif
        if tur is None or tur.t_ilk_paket is not None:
            tur = defter.tur_ac()
        tur.t_sustu = t_sustu

    def konusma_basladi() -> None:
        if hoparlor.caliyor:
            tur = defter.aktif
            if tur and tur.t_kesme_konusma is None:
                tur.t_kesme_konusma = simdi()

    def ilk_ses(t: float) -> None:
        tur = defter.aktif
        if tur and tur.t_ilk_hoparlor is None:
            tur.t_ilk_hoparlor = t

    def tur_bitti(tur) -> None:
        uyum.ekle(tur.robot_dedi, dolgu=tur.dolgu)
        ses_istegini_uygula(tur, hoparlor, lambda m: print(f"  🔊 {m}"))
        olay = guv.incele(tur.cocuk_dedi, tur.robot_dedi)
        if olay:
            print("  ⚠ guvenlik olayi kaydedildi")

    mikrofon.konusma_bitti = sustu
    mikrofon.konusma_basladi = konusma_basladi
    hoparlor.ilk_ses_calindi = ilk_ses

    # -- basla -------------------------------------------------------------

    print("=" * 72)
    print(f"PATI — Asama 1 olcum   ·   model: {model}")
    print("=" * 72)

    try:
        hoparlor.basla()
        mikrofon.basla()
    except SesHatasi as e:
        print(f"\n{e}")
        return 1

    defter.cikis_gecikmesi_ms = hoparlor.cikis_gecikmesi_ms
    defter.giris_gecikmesi_ms = mikrofon.giris_gecikmesi_ms

    print(f"\nOrtam gurultusu olculuyor ({ayarlar.VAD_KALIBRASYON_SN} sn), "
          f"lutfen sessiz ol...")
    esik = await mikrofon.kalibre_et()
    print(f"  zemin gurultu {mikrofon.zemin_gurultu:.0f} -> esik {esik:.0f}")

    if hoparlor_modu:
        print("\n  Hoparlor modu: robot konusurken mikrofon esigi yukseltilecek.")
        print("  ⚠ Bu sozunu kesme olcumunu ZORLASTIRIR. Kriter olcumu icin")
        print("    KULAKLIK kullan ve bu secenegi kapali birak.")
    else:
        print("\n  ⚠ KULAKLIK TAK. Hoparlorden calarsan robot kendi sesini")
        print("    duyup kendi sozunu keser; olcum bozulur.")

    baglanti = canli.Baglanti(model, defter, hoparlor, mikrofon, ham_yol)
    baglanti.tur_bitti_geri = tur_bitti

    print(f"\nBaglaniliyor: {model}")
    try:
        await baglanti.baglan()
    except canli.ProtokolHatasi as e:
        print(f"\n{e}")
        mikrofon.dur()
        hoparlor.dur()
        return 1
    print("  baglandi.\n")
    print("-" * 72)
    print("Konusabilirsin. Bitirmek icin Ctrl+C.")
    if dakika:
        print(f"Otomatik bitis: {dakika:g} dakika")
    print("-" * 72 + "\n")

    gorevler = [
        asyncio.create_task(baglanti.ses_gonder()),
        asyncio.create_task(baglanti.al_dongusu()),
        asyncio.create_task(baglanti.bosta_bekcisi()),
    ]

    # Yanki korumasi (hoparlor modunda)
    async def yanki_bekcisi():
        while baglanti.calisiyor:
            mikrofon.yanki_carpani = 2.5 if hoparlor.caliyor else 1.0
            await asyncio.sleep(0.02)

    if hoparlor_modu:
        gorevler.append(asyncio.create_task(yanki_bekcisi()))

    # Acilis selami
    await asyncio.sleep(0.3)
    await baglanti.metin_gonder(kisilik.ACILIS_ISTEGI)

    try:
        if dakika:
            await asyncio.sleep(dakika * 60)
        else:
            await asyncio.gather(*gorevler)
    except (KeyboardInterrupt, asyncio.CancelledError):
        pass
    finally:
        print("\n\nKapatiliyor...")
        await baglanti.kapat()
        for g in gorevler:
            g.cancel()
        mikrofon.dur()
        hoparlor.dur()

    # -- rapor -------------------------------------------------------------

    defter.uyanma_sureleri = list(baglanti.uyanma_sureleri)

    try:
        await _hafizayi_guncelle(defter)
    except Exception as e:
        print(f"  ⚠ hafiza guncellenemedi: {e}")

    tam = defter.rapor() + "\n" + _ek_bolumler(uyum, guv, metin_modu_mu=False)
    print("\n" + tam)

    yol = defter.kaydet()
    yol.write_text(tam, encoding="utf-8")
    print(f"\nRapor kaydedildi: {yol}")
    print(f"Ham veri        : {yol.with_suffix('.json')}")
    if ham_yol:
        print(f"Ham mesajlar    : {ham_yol}")
    return 0


# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Pati Asama 1 — Gemini Live gecikme olcumu")
    ap.add_argument("--kontrol", action="store_true",
                    help="API anahtari olmadan on kontrol")
    ap.add_argument("--cihazlar", action="store_true",
                    help="ses cihazlarini listele")
    ap.add_argument("--model", default=ayarlar.VARSAYILAN_MODEL,
                    help="3.1 | 2.5 | tam model adi")
    ap.add_argument("--dakika", type=float, default=0,
                    help="kac dakika sonra otomatik bitsin (0 = elle)")
    ap.add_argument("--etiket", default="", help="rapor dosya adina eklenir")
    ap.add_argument("--senaryo", action="store_true",
                    help="§12 test listesini basarak basla")
    ap.add_argument("--sayfa", default="olcum", choices=("olcum", "pati"),
                    help="olcum = prototype/arayuz (olcum tezgahi) · "
                         "pati = panel/ (cocugun gordugu yuz + "
                         "ebeveyn paneli)")
    ap.add_argument("--disa", action="store_true",
                    help="sayfayi ev agina ac (0.0.0.0) — telefondan "
                         "bakmak icin. Varsayilan sadece bu bilgisayar.")
    ap.add_argument("--arayuz", action="store_true",
                    help="tarayicida gozler + canli olcum paneli")
    ap.add_argument("--metin", action="store_true",
                    help="MIKROFONSUZ mod: §12'nin 6 testini metinle yap")
    ap.add_argument("--kisa", action="store_true",
                    help="--metin ile: sadece hizli kontrol seti")
    ap.add_argument("--yuz", default="acik", choices=("acik", "kapali"),
                    help="modelin yuz ifadesi araci (gecikmeyi artiriyor)")
    ap.add_argument("--soru-kurali", default=kisilik.VARSAYILAN_SORU_MODU,
                    choices=sorted(kisilik.SORU_KURALI),
                    help="oranli (olculdu: %%9 uyum) | kati (kesin emir)")
    ap.add_argument("--hoparlor-modu", action="store_true",
                    help="kulaklik yoksa yanki korumasi ac")
    ap.add_argument("--giris", default=None, help="mikrofon cihaz no/adi")
    ap.add_argument("--cikis", default=None, help="hoparlor cihaz no/adi")
    a = ap.parse_args()

    if a.cihazlar:
        from ses import cihazlari_listele
        print(cihazlari_listele())
        return 0

    if a.kontrol:
        return on_kontrol()

    # Anahtari ses donanimina dokunmadan ONCE kontrol et. Once hoparloru
    # acip sonra "anahtar yok" demek, kullaniciyi gereksiz yere
    # kapatilmamis bir ses akisiyla birakiyordu.
    ayarlar.api_anahtari()

    kisilik.AKTIF_SORU_MODU = a.soru_kurali
    ayarlar.YUZ_ARACI = (a.yuz == "acik")

    def cihaz(d):
        if d is None:
            return None
        return int(d) if str(d).isdigit() else d

    giris, cikis = cihaz(a.giris), cihaz(a.cikis)

    # Kulaklik otomatik secimi: giris ve cikisi AYNI cihaza vermek
    # yankiyi kaynagindan kesiyor. Kullanici cihaz numarasi ezberlemek
    # zorunda kalmasin diye varsayilan bu.
    if giris is None and cikis is None and not a.metin:
        from ses import kulaklik_bul
        g, c, ad = kulaklik_bul()
        if g is not None:
            giris, cikis = g, c
            print(f"Kulaklik bulundu: {ad}")
            print("  (giris ve cikis ayni cihazda — yanki olmaz)\n")
        else:
            print(f"⚠ Kulaklik otomatik bulunamadi ({ad}).")
            print("  Windows varsayilan cihazlari kullanilacak.")
            print("  Hoparlorden calarsa robot kendi sozunu kesebilir.\n")

    try:
        if a.arayuz:
            import sunucu
            if sunucu.zaten_calisiyor():
                print()
                print("=" * 60)
                print(" BASKA BIR PATI ZATEN CALISIYOR")
                print("=" * 60)
                print()
                print(" Iki tane calisirsa IKI SES ayni anda konusur.")
                print()
                print(" Yapilacak:")
                print("   1. Acik olan siyah pencereleri kapat")
                print("   2. Tarayicidaki fazla Pati sekmelerini kapat")
                print("   3. PATI-BASLAT.bat'a tekrar cift tikla")
                print()
                print(f" Zaten acik olan panel:")
                print(f"   http://127.0.0.1:{sunucu.HTTP_PORT}/")
                print("=" * 60)
                return 1
            return asyncio.run(arayuz_modu(a.model, a.etiket, giris, cikis,
                                           sayfa=a.sayfa, disa=a.disa))
        if a.metin:
            return asyncio.run(metin_modu(
                a.model, a.dakika, a.etiket, a.kisa, cikis))
        return asyncio.run(calistir(
            a.model, a.dakika, a.etiket, a.hoparlor_modu, a.senaryo,
            giris, cikis))
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
