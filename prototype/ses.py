# -*- coding: utf-8 -*-
"""
Ses hatti: mikrofon -> 16 kHz PCM  /  24 kHz PCM -> hoparlor.

Kasitli olarak CIPLAK. Tarayici yok, SDK yok, ses efekti yok, yeniden
orneklemeli filtre yok. Sebebi PLAN.md: "araya tarayici, SDK katmani,
ses efekti koyarsak olcum bozulur". ESP32'nin I2S'ten okuyup I2S'e
yazmasi disinda bir sey yapmiyoruz.

Yerel VAD burada. NE ISE YARADIGI konusunda net olmak gerekiyor cunku
kolayca yanlis anlasilir:

  Konusma sirasini Gemini'nin SUNUCU TARAFI VAD'i yonetiyor. Bizim
  yerel VAD'imiz konusma sirasina KARISMIYOR, robotu tetiklemiyor,
  mikrofonu kesmiyor. Sadece iki isi var:

    1. "Cocuk sustu" anina milisaniye damgasi koymak (olcumun sifiri)
    2. Robot konusurken cocugun araya girdigini gorup barge-in
       tepkisini olcmek

  Yani yerel VAD yanilirsa ROBOT BOZULMAZ, sadece olcum bozulur.
  Bu ayrim onemli: ESP32'de bu kod olmayacak.
"""

from __future__ import annotations

import asyncio
import queue
import sys

import numpy as np
import sounddevice as sd

import ayarlar
from olcum import simdi


class SesHatasi(RuntimeError):
    pass


def cihazlari_listele() -> str:
    try:
        return str(sd.query_devices())
    except Exception as e:                       # pragma: no cover
        return f"Ses cihazlari okunamadi: {e}"


def kulaklik_bul() -> tuple[int | None, int | None, str]:
    """
    Hem mikrofonu hem hoparloru olan TEK bir cihaz (kulaklik) arar.

    NEDEN: yankiyi onlemenin en kolay yolu giris ve cikisi ayni
    kulakliga vermek. Kullanici ayri ayri cihaz numarasi yazmak zorunda
    kalmasin diye otomatik esliyoruz.

    Bluetooth kulakliklarda cihaz numaralari her baglantida
    degisebiliyor; o yuzden numaraya degil ADA bakiyoruz.

    Doner: (giris_no, cikis_no, aciklama)
    """
    try:
        cihazlar = sd.query_devices()
    except Exception as e:
        return None, None, f"cihazlar okunamadi: {e}"

    def kok(ad: str) -> str:
        # "Kulaklık (Redmi Buds 3 Pro)" ve "Kulaklıklar (Redmi Buds 3 Pro)"
        # ayni donanim. Parantez icindeki kisim ortak anahtar.
        if "(" in ad and ")" in ad:
            return ad[ad.index("(") + 1:ad.rindex(")")].strip().lower()
        return ad.strip().lower()

    girisler: dict[str, int] = {}
    cikislar: dict[str, int] = {}
    for i, c in enumerate(cihazlar):
        # MME en uyumlu arayuz; WDM-KS'te ozel mod sorunlari cikiyor.
        if "MME" not in str(c.get("hostapi_name", "")) and \
                c.get("hostapi") not in (0,):
            continue
        k = kok(c["name"])
        if c["max_input_channels"] > 0 and k not in girisler:
            girisler[k] = i
        if c["max_output_channels"] > 0 and k not in cikislar:
            cikislar[k] = i

    ortak = [k for k in girisler if k in cikislar]
    if not ortak:
        return None, None, ("hem mikrofonu hem hoparloru olan tek cihaz "
                            "bulunamadi")

    # Birden fazlaysa "kulak" gecen ismi tercih et
    ortak.sort(key=lambda k: (0 if ("buds" in k or "head" in k or
                                    "kulak" in k) else 1))
    sec = ortak[0]
    return girisler[sec], cikislar[sec], cihazlar[girisler[sec]]["name"]


def _rms(ham: bytes) -> float:
    """
    Parcanin ses siddeti (RMS).

    int16 -> float32'ye ceviriyoruz; int16 uzerinde kare almak tasma
    yapiyor (32767^2 int16'ya sigmaz) ve sessiz parcalar gurultulu
    gorunuyordu.
    """
    if not ham:
        return 0.0
    x = np.frombuffer(ham, dtype=np.int16).astype(np.float32)
    if x.size == 0:
        return 0.0
    return float(np.sqrt(np.mean(x * x)))


def konusma_var_mi(ham: bytes, esik: float | None = None) -> bool:
    """
    Bu parcada konusma var mi? Esik tek yerde dursun diye burada.

    NEDEN GEREKTI: tarayici modunda yerel VAD yok ve gelen HER ses
    parcasi "hareket" sayiliyordu. Tarayici saniyede ~15 parca
    yolluyor, yani bosta sayaci hic ilerlemiyordu ve robot ASLA
    uyumuyordu — maliyetin en buyuk kalemi (bkz. ayarlar
    §BOSTA_KAPAT_SN) sessizce acik kaliyordu.

    Mikrofon sinifinin kendi esigi uyarlanabilir (zemin gurultusune
    gore); burada o yok, taban esik kullaniliyor. Uyandirmak icin
    yeterli, cunku yanlis pozitif sadece "biraz daha uyanik kaldi"
    demek — yanlis negatif ise cocugu duymamak olurdu.
    """
    if esik is None:
        esik = float(ayarlar.VAD_ESIK_TABAN)
    return _rms(ham) >= esik


class SurekliKonusma:
    """
    Kesintisiz ses VAD_KONUSMA_MS'i gecti mi?

    NEDEN GEREKTI: tarayici modunda uykudaki Pati'yi TEK bir 20 ms'lik
    parca uyandirabiliyordu. Kapi carpmasi, oyuncak sesi, oksuruk —
    hepsi uyandiriyor ve her yanlis uyanma en az bir uyku suresi
    (varsayilan 90 sn) ucret demek.

    Yerel mikrofon yolunda bu koruma zaten VARDI: Mikrofon "konusma
    basladi" demek icin VAD_KONUSMA_MS kadar kesintisiz ses ariyor
    (bkz. Mikrofon._parca). Burasi ayni kuralin tarayici karsiligi.

    Gecikmeye etkisi yok: uyanma zaten ~600-700 ms suruyor.

    Sessiz bir parca zinciri KIRIYOR — darbe sesleri (carpma, dusme)
    kisa oldugu icin esigi gecse bile birikemiyor.
    """

    def __init__(self, esik_ms: float | None = None):
        self.esik_ms = (ayarlar.VAD_KONUSMA_MS if esik_ms is None
                        else esik_ms)
        self.sure_sn = 0.0

    def sifirla(self) -> None:
        self.sure_sn = 0.0

    def __call__(self, ham: bytes) -> bool:
        if not konusma_var_mi(ham):
            self.sure_sn = 0.0
            return False
        self.sure_sn += (len(ham) / ayarlar.ORNEK_GENISLIK
                         / ayarlar.GIRIS_HZ)
        return self.sure_sn * 1000 >= self.esik_ms


# ---------------------------------------------------------------------------
# Mikrofon
# ---------------------------------------------------------------------------

class Mikrofon:
    """
    16 kHz, mono, int16 yakalar ve asyncio kuyruguna birakir.

    Damgalama inceligi: PortAudio geri cagirimi, parca ZATEN
    YAKALANDIKTAN sonra tetikleniyor. O yuzden damgayi geri cagirimin
    calistigi ana degil, parcanin BASLADIGI ana koyuyoruz
    (simdi() - parca_suresi). Bu kadarini duzeltmezsek her olcume
    sistematik olarak +20 ms bineridi.

    Kalan hassasiyet: ±1 parca (20 ms). Rapor bunu boyle yaziyor,
    "1.482 ms" gibi sahte bir kesinlik iddia etmiyoruz.
    """

    def __init__(self, dongu: asyncio.AbstractEventLoop,
                 cihaz: int | str | None = None):
        self.dongu = dongu
        self.cihaz = cihaz
        self.kuyruk: asyncio.Queue[bytes] = asyncio.Queue()
        self.akis: sd.RawInputStream | None = None

        self.parca_sn = ayarlar.PARCA_MS / 1000.0

        # VAD durumu
        self.esik = float(ayarlar.VAD_ESIK_TABAN)
        self.zemin_gurultu = 0.0
        self.konusuyor = False
        self._sesli_sure = 0.0
        self._sessiz_sure = 0.0
        self._sessizlik_basi: float | None = None

        # Robot konusurken yanki korumasi icin esik carpani
        self.yanki_carpani = 1.0
        self.yedege_dustu = False   # secilen cihaz olmadi, varsayilana dusuldu

        # Geri cagirimlar (ana dongude calistirilir)
        self.konusma_basladi = None      # () -> None
        self.konusma_bitti = None        # (t_sustu: float) -> None

        self._kalibrasyon: list[float] | None = None
        self.giris_gecikmesi_ms: float | None = None

    # -- akis --------------------------------------------------------------

    def _geri_cagirim(self, ham, kare_sayisi, zaman, durum):
        t_parca_basi = simdi() - self.parca_sn
        veri = bytes(ham)

        # PortAudio geri cagirimi ayri bir is parcaciginda calisiyor ve
        # kapanis sirasinda dongu bizden once kapanabiliyor. Korumasiz
        # birakinca Ctrl+C her seferinde yigin dokumu basiyordu.
        try:
            self.dongu.call_soon_threadsafe(self.kuyruk.put_nowait, veri)
        except RuntimeError:
            return

        siddet = _rms(veri)
        if self._kalibrasyon is not None:
            self._kalibrasyon.append(siddet)
            return
        try:
            self.dongu.call_soon_threadsafe(self._vad, siddet, t_parca_basi)
        except RuntimeError:
            pass

    def _vad(self, siddet: float, t_parca_basi: float) -> None:
        esik = self.esik * self.yanki_carpani

        if siddet >= esik:
            self._sesli_sure += self.parca_sn
            # Sessizlik zinciri kirildi
            if self._sessizlik_basi is not None:
                self._sessizlik_basi = None
            self._sessiz_sure = 0.0

            if (not self.konusuyor
                    and self._sesli_sure * 1000 >= ayarlar.VAD_KONUSMA_MS):
                self.konusuyor = True
                if self.konusma_basladi:
                    self.konusma_basladi()
        else:
            # Sessizligin BASLADIGI ani sakliyoruz. Damga buraya
            # konacak; asagidaki bekleme suresi olculen gecikmeye
            # EKLENMIYOR. (Eklenseydi her olcum 300 ms sisikti.)
            if self._sessizlik_basi is None:
                self._sessizlik_basi = t_parca_basi
            self._sessiz_sure += self.parca_sn
            self._sesli_sure = 0.0

            if (self.konusuyor
                    and self._sessiz_sure * 1000 >= ayarlar.VAD_SESSIZLIK_MS):
                self.konusuyor = False
                t_sustu = self._sessizlik_basi or t_parca_basi
                self._sessizlik_basi = None
                if self.konusma_bitti:
                    self.konusma_bitti(t_sustu)

    async def kalibre_et(self) -> float:
        """
        Ortamin gurultusunu olcup esigi ona gore koyar.

        Sabit esik kullanmak, v1'in "hizli makinede olctum" hatasinin
        ses tarafindaki karsiligi olurdu: sessiz odada calisip
        gurultulu odada bozulan bir olcum.
        """
        self._kalibrasyon = []
        await asyncio.sleep(ayarlar.VAD_KALIBRASYON_SN)
        ornekler = self._kalibrasyon or [0.0]
        self._kalibrasyon = None

        self.zemin_gurultu = float(np.median(ornekler))
        self.esik = max(self.zemin_gurultu * ayarlar.VAD_ESIK_CARPANI,
                        float(ayarlar.VAD_ESIK_TABAN))
        return self.esik

    def basla(self) -> None:
        def _ac(cihaz):
            akis = sd.RawInputStream(
                samplerate=ayarlar.GIRIS_HZ,
                blocksize=ayarlar.PARCA_ORNEK,
                device=cihaz,
                channels=ayarlar.KANAL,
                dtype="int16",
                callback=self._geri_cagirim,
            )
            akis.start()
            return akis

        try:
            self.akis = _ac(self.cihaz)
        except Exception as e:
            # Cihaz numarasi bayatlamis olabilir (Bluetooth kulaklik
            # kopunca liste degisiyor). Sistem varsayilanina dusuyoruz.
            if self.cihaz is None:
                raise SesHatasi(
                    f"Mikrofon acilamadi: {e}\n"
                    f"Cihaz listesi icin:  python pati.py --cihazlar"
                ) from e
            try:
                self.akis = _ac(None)
                self.cihaz = None
                self.yedege_dustu = True
            except Exception as e2:
                raise SesHatasi(
                    f"Mikrofon acilamadi (varsayilan cihaz da olmadi): {e2}\n"
                    f"Cihaz listesi icin:  python pati.py --cihazlar"
                ) from e2

        try:
            self.giris_gecikmesi_ms = float(self.akis.latency) * 1000.0
        except Exception:
            self.giris_gecikmesi_ms = None

    def dur(self) -> None:
        if self.akis:
            try:
                self.akis.stop()
                self.akis.close()
            except Exception:
                pass
            self.akis = None


# ---------------------------------------------------------------------------
# Hoparlor
# ---------------------------------------------------------------------------

class Hoparlor:
    """
    24 kHz, mono, int16 calar.

    "Ilk ses" damgasi buradan cikiyor ve NEREYE konuldugu onemli:
    paket soketten geldigi ana DEGIL, ses karti o baytlari gercekten
    ISTEDIGI ana konuyor. Aradaki fark bizim yerel oynatma gecikmemiz
    ve ESP32'de farkli olacak - o yuzden ayri olculuyor (PLAN.md).

    Yine de tam durust olmak icin: ses karti baytlari istedikten sonra
    da kulaga varmasi biraz suruyor (donanim tamponu). O sureyi
    PortAudio'nun bildirdigi 'latency' degerinden alip raporda AYRI
    satirda gosteriyoruz, olculen gecikmeye gizlice eklemiyoruz.
    """

    def __init__(self, dongu: asyncio.AbstractEventLoop,
                 cihaz: int | str | None = None,
                 hiz: float | None = None):
        self.dongu = dongu
        self.cihaz = cihaz
        # Calma hizi ACIKCA veriliyor, kuresel ayardan OKUNMUYOR.
        #
        # NEDEN: onizleme ("Dinle") gecici olarak ayarlar.CIKIS_HIZ'i
        # degistirip sonra geri aliyordu. Kullanici onizleme calarken
        # "Bu sesi kullan"a basarsa, onizlemenin geri alma adimi yeni
        # ayari eziyordu — panelde "dinledigim ses ile baslattigim ses
        # ayni degil" diye goruluyordu. Kuresel degiskeni paylasmayi
        # birakinca yaris durumu ortadan kalkti.
        self.hiz = ayarlar.CIKIS_HIZ if hiz is None else hiz
        self.akis: sd.RawOutputStream | None = None
        self.yedege_dustu = False   # secilen cihaz olmadi, varsayilana dusuldu

        self._tampon = bytearray()
        self._kilit = None            # threading kilidi yerine GIL yeterli
        self._ilk_bekleniyor = False
        self.ilk_ses_calindi = None   # (t: float) -> None
        self.bitti = None             # () -> None

        self.cikis_gecikmesi_ms: float | None = None
        self._caliyor = False
        self._bos_gecti = 0

        # Ses seviyesi. Carpim ekle()'de yapiliyor, _geri_cagirim()'da DEGIL.
        #
        # NEDEN BURADA DEGIL ORADA: _geri_cagirim ses kartinin gercek
        # zamanli geri cagirimi ve "ilk ses" damgasi oradan cikiyor —
        # yani OLCUMUN kendisi orada. Oraya fazladan is koymak olctugumuz
        # sayiyi kirletir. ekle() ise sunucudan paket gelince calisiyor,
        # olcumun disinda.
        #
        # Bedeli: seviye degisince tamponda BEKLEYEN ses eski seviyeden
        # calmaya devam eder. Sorun degil — seviye zaten tur arasinda
        # degisiyor (cocuk "sesini kis" dedikten sonra), ve boylece
        # cumlenin ortasinda ani ses siçramasi da olmuyor.
        self.ses_seviyesi = ayarlar.SES_SEVIYESI

    # -- akis --------------------------------------------------------------

    def _geri_cagirim(self, cikti, kare_sayisi, zaman, durum):
        gereken = kare_sayisi * ayarlar.ORNEK_GENISLIK

        if self._tampon:
            if self._ilk_bekleniyor:
                self._ilk_bekleniyor = False
                t = simdi()
                if self.ilk_ses_calindi:
                    try:
                        self.dongu.call_soon_threadsafe(
                            self.ilk_ses_calindi, t)
                    except RuntimeError:
                        pass

            n = min(gereken, len(self._tampon))
            cikti[:n] = bytes(self._tampon[:n])
            del self._tampon[:n]
            if n < gereken:
                cikti[n:gereken] = b"\x00" * (gereken - n)
                self._bos_gecti += 1
            else:
                self._bos_gecti = 0
            self._caliyor = True
        else:
            cikti[:gereken] = b"\x00" * gereken
            if self._caliyor:
                self._bos_gecti += 1
                # Ust uste bos parca = konusma bitti
                if self._bos_gecti > 3:
                    self._caliyor = False
                    self._bos_gecti = 0
                    if self.bitti:
                        try:
                            self.dongu.call_soon_threadsafe(self.bitti)
                        except RuntimeError:
                            pass

    # -- kullanim ----------------------------------------------------------

    def yeni_tur(self) -> None:
        """Bir sonraki gelen ses parcasi 'ilk ses' sayilsin."""
        self._ilk_bekleniyor = True

    def ekle(self, pcm: bytes) -> None:
        self._tampon.extend(self._seviye_uygula(pcm))

    def _seviye_uygula(self, pcm: bytes) -> bytes:
        """
        int16 ornekleri ses seviyesiyle carpar.

        1.0'da HIC IS YAPMIYOR — dokunulmamis baytlar geri doner. Bu
        onemli: olcum kosulari tam seviyede yapiliyor ve o kosularda bu
        fonksiyon bir kopyalama bile eklemesin.

        Kirpma (clipping) yok cunku 1.0 ust sinir ve carpan hep <= 1.0;
        yine de tek tarafli tasmaya karsi int16 sinirlarina kelepceliyoruz
        (tek sayi genisligi degisirse burasi sessizce bozulmasin).
        """
        if self.ses_seviyesi >= 1.0:
            return pcm
        if not pcm:
            return pcm
        # Tek bayt artmis parca gelirse int16'ya cevrilemez; oldugu gibi gec.
        if len(pcm) % ayarlar.ORNEK_GENISLIK:
            return pcm
        ornekler = np.frombuffer(pcm, dtype=np.int16)
        olcekli = np.clip(ornekler * self.ses_seviyesi,
                          -32768, 32767).astype(np.int16)
        return olcekli.tobytes()

    def seviye_ayarla(self, yeni: float) -> float:
        """
        Seviyeyi sinirlar icinde ayarlar, gercekte ne oldugunu dondurur.

        Sinirlar ayarlar.py'de: cocuk ne kadar ustelerse ustelesin
        SES_SEVIYESI_EN_FAZLA'nin uzerine cikamaz, EN_AZ'in altina inip
        robotu tamamen susturamaz.
        """
        self.ses_seviyesi = max(ayarlar.SES_SEVIYESI_EN_AZ,
                                min(ayarlar.SES_SEVIYESI_EN_FAZLA,
                                    float(yeni)))
        return self.ses_seviyesi

    def seviye_degistir(self, adim: float) -> float:
        """Mevcut seviyeye adim ekler (kismak icin negatif ver)."""
        return self.seviye_ayarla(self.ses_seviyesi + adim)

    def temizle(self) -> None:
        """
        Barge-in: sunucu 'interrupted' dedi, kuyrukta bekleyen sesi AT.

        Atmazsak robot susmus gibi gorunup birkac saniye sonra eski
        cumlesine devam ediyor - cocuk icin en sinir bozucu davranis.
        Dokumanin kendi uyarisi da bu: "stop playing audio and clear
        queued playback here".
        """
        self._tampon.clear()
        self._ilk_bekleniyor = False
        self._caliyor = False

    @property
    def bekleyen_ms(self) -> float:
        bayt_sn = ayarlar.CIKIS_HZ * ayarlar.ORNEK_GENISLIK
        return len(self._tampon) * 1000.0 / bayt_sn

    @property
    def caliyor(self) -> bool:
        return self._caliyor or bool(self._tampon)

    def basla(self) -> None:
        blok = ayarlar.CIKIS_HZ * ayarlar.CIKIS_TAMPON_MS // 1000

        # v1'in hilesi: sesi OLDUGUNDAN HIZLI calarsak hem hizlaniyor
        # hem tizlesiyor (daha genc, daha tatli). Veriye hic
        # dokunmuyoruz, sadece "bunu daha hizli cal" diyoruz.
        # ESP32'de karsiligi I2S saat ayari — sifir islemci yuku.
        calma_hz = int(ayarlar.CIKIS_HZ * self.hiz)

        def _ac(cihaz):
            akis = sd.RawOutputStream(
                samplerate=calma_hz,
                blocksize=blok,
                device=cihaz,
                channels=ayarlar.KANAL,
                dtype="int16",
                callback=self._geri_cagirim,
            )
            akis.start()
            return akis

        try:
            self.akis = _ac(self.cihaz)
        except Exception as e:
            # ⚠ CIHAZ NUMARASI BAYATLAMIS OLABILIR.
            #
            # Gercekten yasandi: program acilirken Bluetooth kulaklik
            # bagliydi, numarasi alindi. Kulaklik sonra koptu, Windows'un
            # cihaz listesi degisti ve o numara baska bir seye denk geldi
            # ("device ID out of range"). Program da komple durdu.
            #
            # Numara sabit degil, liste degisken. Basarisiz olursak
            # sistem varsayilanina dusuyoruz — sessiz kalmaktansa
            # baska bir hoparlorden calmak iyidir.
            if self.cihaz is None:
                raise SesHatasi(
                    f"Hoparlor acilamadi: {e}\n"
                    f"Cihaz listesi icin:  python pati.py --cihazlar"
                ) from e
            try:
                self.akis = _ac(None)
                self.cihaz = None
                self.yedege_dustu = True
            except Exception as e2:
                raise SesHatasi(
                    f"Hoparlor acilamadi (varsayilan cihaz da olmadi): {e2}\n"
                    f"Cihaz listesi icin:  python pati.py --cihazlar"
                ) from e2

        try:
            self.cikis_gecikmesi_ms = float(self.akis.latency) * 1000.0
        except Exception:
            self.cikis_gecikmesi_ms = None

    def dur(self) -> None:
        if self.akis:
            try:
                self.akis.stop()
                self.akis.close()
            except Exception:
                pass
            self.akis = None
