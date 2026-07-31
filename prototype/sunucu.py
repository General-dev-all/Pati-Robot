# -*- coding: utf-8 -*-
"""
Yerel panel sunucusu — gozleri ve olcumu tarayicida gosterir.

╔══════════════════════════════════════════════════════════════════════╗
║ MIMARI KURALI: SES TARAYICIDAN GECMIYOR.                             ║
╚══════════════════════════════════════════════════════════════════════╝

PLAN.md acikca soyle diyor:

    "Araya tarayici, SDK katmani, ses efekti koyarsak olcum bozulur."

Bu dogru ve bu dosya o kurali CIGNEMIYOR. Ses yolu aynen duruyor:

    mikrofon -> Python -> 16 kHz PCM -> WSS -> Gemini
    Gemini -> 24 kHz PCM -> Python -> hoparlor

Tarayicinin ses hattinda HICBIR rolu yok. Tarayici sadece Python'un
yayinladigi olaylari aliyor ve ciziyor:

    Python --(yerel WebSocket, sadece JSON)--> tarayici
            ifade, durum, gecikme, dokum, olay

Yani panel bir GOSTERGE, bir katman degil. Kapatirsan olcum aynen
devam eder. Bunu boyle kurmasaydik PC'de olculen sayinin ESP32'ye
gecmesi iddiasi curur du.

NEDEN VAR: iki isi birden yapiyor.
  1. Terminali kullanmak zorunda kalmadan olcum calistirmak.
  2. Gozleri ekran/donanim almadan ONCE gormek. PLAN.md: "Gozler tek
     basina butun kisiliktir." Ifadelerin nasil durdugunu 250 TL'lik
     ekrani siparis etmeden gormek mantikli — v1'in ARASTIRMA.md'sindeki
     "once kartondan maket" dersinin yazilim tarafi.
"""

from __future__ import annotations

import asyncio
import json
import mimetypes
from pathlib import Path

from websockets.asyncio.server import serve
from websockets.datastructures import Headers
from websockets.http11 import Response

KOK = Path(__file__).resolve().parent
ARAYUZ = (KOK / "arayuz").resolve()


def klasor_ayarla(yol) -> None:
    """
    Servis edilecek klasoru degistirir.

    NEDEN GEREKTI: iki ayri on yuz var ve ikisi KARISMAMALI.

      prototype/arayuz/   olcum tezgahi — p90, dolgu turu, ham dokum
      panel/        Pati'nin kendisi — cocugun gordugu yuz + ebeveyn
                       paneli. Robot geldiginde bu dosyalar ESP32'den
                       servis edilecek

    Ikisi ayni sunucuyu kullaniyor cunku arkadaki is ayni: Gemini
    oturumu, ses hatti, olcum defteri. Degisen sadece hangi klasorun
    disari verildigi.
    """
    global ARAYUZ
    ARAYUZ = Path(yol).resolve()

# ---------------------------------------------------------------------------
# TEK PORT — hem sayfa hem WebSocket
#
# Onceden ikiydi: sayfa 8756, WebSocket 8757. Yerelde sorun degildi ama
# disari acmak (Cloudflare tunnel gibi) imkansizdi: tek adres
# tunellenince sayfa aciliyor, canli veri gelmiyordu.
#
# Artik ayni port hem dosyalari servis ediyor hem WebSocket yukseltmesini
# kabul ediyor. Tarayici da WS adresini sayfanin kendi adresinden
# tureterek kuruyor (panel.js), yani http/https ve alan adi ne olursa
# olsun calisiyor.
# ---------------------------------------------------------------------------

HTTP_PORT = 8756
WS_PORT = HTTP_PORT      # geriye donuk uyum; ayni port


def zaten_calisiyor() -> bool:
    """
    Baska bir Pati acik mi?

    Kontrolu asyncio'ya girmeden ONCE yapiyoruz; iceride patlayinca
    kullaniciya yigin dokumu goruluyordu ve "ne oldu" belli olmuyordu.
    """
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.3)
        return s.connect_ex(("127.0.0.1", HTTP_PORT)) == 0


def _dosya_yanit(yol: str) -> Response:
    """
    WebSocket olmayan istekler icin arayuz dosyasini dondurur.

    Guvenlik: sadece arayuz/ klasoru servis ediliyor. Yol icindeki
    ".." temizleniyor — tunel disariya acilinca sunucunun geri kalanini
    gostermek istemiyoruz.
    """
    ad = yol.split("?")[0].lstrip("/")
    hedef = (ARAYUZ / ad).resolve()

    # Klasor istendiyse icindeki index.html. Onceden sadece "/" ozel
    # durumu vardi; alt klasorler ("/panel/") 404 donuyordu.
    if hedef.is_dir():
        hedef = hedef / "index.html"

    if not str(hedef).startswith(str(ARAYUZ.resolve())) or not hedef.is_file():
        return Response(404, "Not Found",
                        Headers({"Content-Type": "text/plain"}),
                        b"bulunamadi")

    icerik = hedef.read_bytes()
    tur = mimetypes.guess_type(str(hedef))[0] or "application/octet-stream"
    if tur.startswith("text/") or tur.endswith(("javascript", "json")):
        tur += "; charset=utf-8"
    return Response(200, "OK", Headers({
        "Content-Type": tur,
        "Content-Length": str(len(icerik)),
        "Cache-Control": "no-store",
    }), icerik)


class Panel:
    def __init__(self, dongu: asyncio.AbstractEventLoop):
        self.dongu = dongu
        self.istemciler: set = set()
        self._http = None
        self._ws = None
        self.komut_geri = None      # (dict) -> None ; tarayicidan gelen
        self.ses_geri = None        # (bytes) -> None ; tarayici mikrofonu

        # Sesi hangi tarayiciya gonderecegiz. Birden fazla sekme aciksa
        # ses sadece mikrofonu acan sekmeye gitmeli, yoksa herkes ayni
        # anda konusuyor.
        self.ses_istemcisi = None

        # Tarayici gec baglanirsa ilk olaylari kacirmasin diye
        # son durumu sakliyoruz.
        self.son_durum: dict = {}

    # -- baslatma ----------------------------------------------------------

    def http_baslat(self) -> None:
        """Ayri HTTP sunucusu kalmadi; ws_baslat() ikisini birden yapiyor."""
        return

    async def ws_baslat(self, disa_ac: bool = False) -> None:
        """
        Sayfayi ve WebSocket'i AYNI portta acar.

        disa_ac=False (varsayilan): sadece 127.0.0.1. Bilgisayarin
        disindan erisilemez.

        disa_ac=True: butun arayuzler (0.0.0.0). Cloudflare tunnel ya da
        ayni agdaki telefon icin gerekli. Tunel kullanilacaksa yine de
        127.0.0.1 yeter — cloudflared ayni makinede calisiyor.
        """
        adres = "0.0.0.0" if disa_ac else "127.0.0.1"
        self._ws = await serve(
            self._istemci, adres, HTTP_PORT,
            process_request=self._istek,
            max_size=None,
        )

    @staticmethod
    def _istek(baglanti, istek):
        """
        WebSocket olmayan istekleri sayfa olarak yanitlar.

        None donmek "bu bir WebSocket, el sikismaya devam et" demek.
        """
        if istek.headers.get("Upgrade", "").lower() == "websocket":
            return None
        return _dosya_yanit(istek.path)

    async def _istemci(self, ws):
        self.istemciler.add(ws)
        try:
            # Yeni baglanan tarayiciya son bilinen durumu gonder
            for mesaj in self.son_durum.values():
                await ws.send(json.dumps(mesaj, ensure_ascii=False))
            async for ham in ws:
                # IKILI cerceve = tarayicinin mikrofonundan gelen ham
                # PCM. Metin cerceve = JSON komut. Ayrimi bu kadar
                # basit tutuyoruz; ses icin base64/JSON sarmalamak hem
                # %33 daha cok bayt hem gereksiz islem olurdu.
                if isinstance(ham, (bytes, bytearray)):
                    if self.ses_geri:
                        self.ses_geri(bytes(ham))
                    continue
                try:
                    komut = json.loads(ham)
                except Exception:
                    continue
                if komut.get("tip") == "ses_kanali":
                    # Bu sekme mikrofonunu acti; robotun sesi buraya
                    # gidecek.
                    self.ses_istemcisi = ws if komut.get("ac") else None
                if self.komut_geri:
                    self.komut_geri(komut)
        except Exception:
            pass
        finally:
            self.istemciler.discard(ws)
            if self.ses_istemcisi is ws:
                self.ses_istemcisi = None

    # -- yayin -------------------------------------------------------------

    def yayinla(self, mesaj: dict) -> None:
        """
        Olayi butun tarayicilara gonderir.

        Ses geri cagirimlarindan da cagrilabilsin diye is parcacigi
        guvenli: dogrudan gondermiyor, dongude gorev aciyor.
        """
        # Tarayici Python'dan SONRA aciliyor; acilistan once yayinlanan
        # mesajlari kacirmasin diye son halini sakliyoruz.
        #
        # ⚠ BURASI ONCE "IZIN LISTESI" IDI VE UC KEZ HATA URETTI:
        #   listeye eklemeyi unutulan her yeni mesaj tipi panelde BOS
        #   goruluyordu (once hafiza "—" kaldi, sonra ses listesi hic
        #   gelmedi, sonra gecikme ayarlari).
        #
        # Artik YASAK LISTESI: varsayilan olarak her sey onbellege
        # giriyor, sadece BIRIKEN kayit turleri disarida. Yeni bir
        # durum mesaji eklemek icin burayi degistirmek gerekmiyor.
        tip = mesaj.get("tip")
        BIRIKENLER = ("olay", "tur")     # gecmis kaydi, tekrar oynatilmaz
        if tip and tip not in BIRIKENLER:
            self.son_durum[tip] = mesaj
        try:
            self.dongu.call_soon_threadsafe(
                lambda: asyncio.ensure_future(self._gonder(mesaj)))
        except RuntimeError:
            pass

    def ses_yolla(self, pcm: bytes) -> None:
        """
        Robotun sesini tarayiciya gonderir (24 kHz ham PCM, ikili).

        Sadece mikrofonunu acan sekmeye gidiyor; baska sekmeler ayni
        anda konusmasin.
        """
        ws = self.ses_istemcisi
        if not ws:
            return
        try:
            self.dongu.call_soon_threadsafe(
                lambda: asyncio.ensure_future(self._ses(ws, pcm)))
        except RuntimeError:
            pass

    async def _ses(self, ws, pcm: bytes) -> None:
        try:
            await ws.send(pcm)
        except Exception:
            if self.ses_istemcisi is ws:
                self.ses_istemcisi = None

    async def _gonder(self, mesaj: dict) -> None:
        if not self.istemciler:
            return
        ham = json.dumps(mesaj, ensure_ascii=False)
        olu = []
        for ws in list(self.istemciler):
            try:
                await ws.send(ham)
            except Exception:
                olu.append(ws)
        for ws in olu:
            self.istemciler.discard(ws)

    # -- kapanis -----------------------------------------------------------

    async def kapat(self) -> None:
        if self._ws:
            self._ws.close()
            try:
                await self._ws.wait_closed()
            except Exception:
                pass

    @property
    def adres(self) -> str:
        return f"http://127.0.0.1:{HTTP_PORT}/"
