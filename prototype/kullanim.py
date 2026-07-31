# -*- coding: utf-8 -*-
"""
Konusma dakikasi sayaci — ebeveyn panelindeki "bugun / bu ay" kutulari.

NEDEN VAR
---------
Panelde bu iki sayi vardi ve UYDURMAYDI (ornek veride "18 dk / 214 dk").
Uydurma sayi gostermek iki sekilde zarar veriyor: birincisi tasarimi
yanlis degerlendiriyoruz (dort haneli dakika kutuya sigmiyor, bunu
gormedik), ikincisi bir sure sonra ona inaniyoruz.

NEDEN OLCUM DEFTERINDEN AYRI
-----------------------------
`olcum.py` bir KOSUNUN defteri: gecikme, p90, kriter karari. Oturum
bitince rapor yazilip kapaniyor. Buradaki sayi ise KUMULATIF ve
ebeveyne ait — "bu ay ne kadar konustuk". Ayni dosyaya koymak, olcum
raporunu ebeveyn verisiyle kirletirdi.

NEDEN PARA ONEMLI
-----------------
Ucret dakikayla isliyor: ses girisi $0.005/dk, cikisi $0.018/dk. Ama en
buyuk kalem sohbet degil BOSTA BEKLEMEK (ayarlar.py §BOSTA_KAPAT_SN:
gunde 12 saat bosta ~ $108/ay). Bu yuzden sayilan sey "oturum acik
kaldigi sure" — konusulan sure degil. Ikisi ayni sey degil ve fatura
birincisine bakiyor.

UYKU SAYILMIYOR: oturum uykudayken WebSocket kapali, ses akmiyor,
ucret islemiyor. O dakikalar sayaca girmiyor (bkz. Oturum.duraklat).

ESP32'DE
--------
Ayni mantik NVS'te olacak. Bu yuzden:
  - yazma ATOMIK (cocuk fisi cekerse dosya bozulmasin)
  - dosya SINIRLI buyur (60 gunden eskisi atiliyor)
Ikisi de PLAN.md'in "sinirsiz buyuyen hicbir sey birakma" kurali.
"""

from __future__ import annotations

import json
import os
import tempfile
import threading
import time
from datetime import date, datetime
from pathlib import Path

KOK = Path(__file__).resolve().parent
DOSYA = KOK / "kullanim.json"

_kilit = threading.RLock()

# Kac gunluk kayit tutulacak. Panelde "bu ay" gosteriliyor, yani 31 gun
# yeterdi; 60 gun tutmak ay basi gecislerinde onceki aya bakabilmek
# icin. Daha fazlasi ise yaramiyor ve dosya buyuyor.
EN_FAZLA_GUN = 60


def _oku() -> dict:
    if not DOSYA.exists():
        return {"gunler": {}}
    try:
        h = json.loads(DOSYA.read_text(encoding="utf-8"))
        if not isinstance(h.get("gunler"), dict):
            return {"gunler": {}}
        return h
    except Exception:
        # Bozuk dosya sadece bir sayac; hafiza gibi degerli degil.
        # Sessizce sifirdan basliyoruz — ama SILMIYORUZ, ustune
        # yaziyoruz ki bir sonraki yazma duzeltsin.
        return {"gunler": {}}


def _yaz(h: dict) -> None:
    """Atomik yazma: gecici dosyaya yaz, sonra yerine tasi."""
    h["gunler"] = dict(sorted(h["gunler"].items())[-EN_FAZLA_GUN:])
    gecici = None
    try:
        with tempfile.NamedTemporaryFile(
                "w", encoding="utf-8", dir=str(KOK),
                prefix=".kullanim-", suffix=".tmp", delete=False) as f:
            gecici = Path(f.name)
            json.dump(h, f, ensure_ascii=False, indent=2)
            f.flush()
            os.fsync(f.fileno())
        os.replace(gecici, DOSYA)
        gecici = None
    finally:
        if gecici and gecici.exists():
            try:
                gecici.unlink()
            except OSError:
                pass


def ekle(saniye: float) -> None:
    """Bir oturumun suresini bugunun hanesine ekler."""
    if saniye <= 0:
        return
    with _kilit:
        h = _oku()
        bugun = date.today().isoformat()
        h["gunler"][bugun] = round(h["gunler"].get(bugun, 0.0) + saniye / 60.0, 2)
        _yaz(h)


def ozet(acik_oturum_sn: float = 0.0) -> dict:
    """
    Panel icin bugun ve bu ayin dakikalari.

    acik_oturum_sn: SU AN calisan oturumun suresi. Henuz dosyaya
    yazilmadigi icin ayri veriliyor — yoksa panel konusma bitene kadar
    hic degismiyor ve "sayac bozuk" gorunuyor.
    """
    with _kilit:
        h = _oku()
    bugun = date.today().isoformat()
    ay = bugun[:7]

    ek = acik_oturum_sn / 60.0
    bugun_dk = h["gunler"].get(bugun, 0.0) + ek
    ay_dk = sum(d for g, d in h["gunler"].items() if g.startswith(ay)) + ek

    # Tahmini tutar: oturum acik kaldigi sure x giris ucreti. Cikis
    # ucreti ($0.018/dk) sadece robot KONUSURKEN isliyor ve o sureyi
    # ayri olcmuyoruz — bu yuzden sayi ALT SINIR. Panelde "tahmini"
    # diye yaziyor.
    usd = ay_dk * 0.005

    return {
        "bugun_dk": round(bugun_dk, 1),
        "ay_dk": round(ay_dk, 1),
        "tahmin_usd": round(usd, 2),
        "gun_sayisi": len(h["gunler"]),
    }


class Oturum:
    """
    Oturumun UCRETLI suresini olcer.

    Cokme durumunda da kaydetmesi onemli: cocuk fisi cekince ya da
    program hata verince o dakikalar da harcandi.

    ⚠ UYKU SURESI SAYILMIYOR — ve bu bir ayrinti degil, sayinin
    dogru olup olmadigini belirleyen sey.

    Uykuda WebSocket KAPALI: ses akmiyor, ucret islemiyor. Panel de
    bunu yaziyor ("Uyurken ucret islemez"). Ama sayac duvar saatiyle
    calisiyordu ve uyku dakikalarini da faturaya yaziyordu.

    Ne kadar yaniltiyordu: 30.07.2026'nin gercek kayitlarinda bir
    oturum 53,7 dakika surdu ve 90 saniye sonra uyuyup bir daha
    uyanmadi — panel 53,7 dakikayi ucretli gosterdi, gerceginde ~1,5
    dakikaydi. Panelin "bu ay 102 dk · 0,51 $" sayisi bu yuzden
    gercegin katiydi.
    """

    def __init__(self):
        self.basladi = time.monotonic()
        self._uyku_basi: float | None = None
        self._uyku_toplam = 0.0

    @property
    def gecen_sn(self) -> float:
        gecen = time.monotonic() - self.basladi - self._uyku_toplam
        if self._uyku_basi is not None:
            gecen -= time.monotonic() - self._uyku_basi
        return max(0.0, gecen)

    def duraklat(self) -> None:
        """Uykuya gecildi: buradan sonrasi faturaya yazilmiyor."""
        if self._uyku_basi is None:
            self._uyku_basi = time.monotonic()

    def devam(self) -> None:
        """Uyandi: sayac yeniden isliyor."""
        if self._uyku_basi is not None:
            self._uyku_toplam += time.monotonic() - self._uyku_basi
            self._uyku_basi = None

    def bitir(self) -> float:
        gecen = self.gecen_sn
        ekle(gecen)
        return gecen
