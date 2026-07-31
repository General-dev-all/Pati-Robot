# -*- coding: utf-8 -*-
"""
Gemini Live API — ham WebSocket istemcisi.

SDK YOK. Bilerek. PLAN.md: "Araya tarayici, SDK katmani, ses efekti
koyarsak olcum bozulur." SDK'nin kendi tamponlamasi, kendi yeniden
deneme mantigi ve kendi kuyrugu var; olctugumuz sey SDK'nin davranisi
olurdu, ESP32'nin yapacagi isin degil. ESP32 de ham WebSocket
kullanacak (stackchan-idf'in conversation/ bileseni oyle yapiyor).

Protokol bilgisinin tamami 29.07.2026'da resmi dokumanin HAM METNINDEN
alindi, ezberden yazilmadi. Ayrinti: NOTLAR.md.
"""

from __future__ import annotations

import asyncio
import base64
import json
from collections import deque
from pathlib import Path

import websockets
from websockets.asyncio.client import connect

import ayarlar
import kisilik
import yuz
from olcum import Defter, simdi


class ProtokolHatasi(RuntimeError):
    pass


# ---------------------------------------------------------------------------
# Setup mesaji
#
# ⚠ BURADA DOKUMANLAR KENDI ICINDE CELISIYOR — ve bu, PLAN.md'nin
#   "arac ciktilarina koru korune guvenme" uyarisinin birebir ornegi.
#
#   API referansi (ai.google.dev/api/live) responseModalities'i
#   generationConfig'in ICINDE gosteriyor:
#       { "model": ..., "generationConfig": { "responseModalities": [...] } }
#
#   Baslangic kilavuzu (get-started-websocket) ise setup'in UST
#   SEVIYESINDE gosteriyor:
#       { "setup": { "model": ..., "responseModalities": ["AUDIO"] } }
#
#   Ikisi ayni anda dogru olamaz. Tahmin etmiyoruz: once referansin
#   dedigini deniyoruz (sema referansi normatiftir), setup basarisiz
#   olursa otekine gecip HANGISININ CALISTIGINI RAPORA YAZIYORUZ.
#   Boylece bir dahaki sefere kimse ezberden bilmek zorunda kalmiyor.
# ---------------------------------------------------------------------------

USLUP_GENCONFIG = "generationConfig"
USLUP_UST = "ust_seviye"


def setup_mesaji(model: str, uslup: str = USLUP_GENCONFIG,
                 devam_anahtari: str | None = None) -> dict:
    uretim: dict = {"responseModalities": ["AUDIO"]}

    if ayarlar.SES_ADI:
        uretim["speechConfig"] = {
            "voiceConfig": {
                "prebuiltVoiceConfig": {"voiceName": ayarlar.SES_ADI}
            }
        }
        # Dil kodu BILEREK gonderilmiyor. Ham dokumandan:
        # "Native audio output models automatically choose the
        #  appropriate language and don't support explicitly setting
        #  the language code."
        # Turkce'yi sistem promptunun dili belirliyor. Bu bir varsayim
        # degil, olculecek bir sey (PLAN.md: Turkce telaffuz).
        if ayarlar.DIL_KODU:
            uretim["speechConfig"]["languageCode"] = ayarlar.DIL_KODU

    prompt = kisilik.sistem_promptu()
    if ayarlar.YUZ_ARACI:
        prompt += yuz.PROMPT_EKI

    setup: dict = {
        "model": f"models/{model}",
        # Sabiti degil FONKSIYONU cagiriyoruz: soru kurali kosudan
        # kosuya degisiyor ve hangisiyle olctugumuz raporda duruyor.
        "systemInstruction": {"parts": [{"text": prompt}]},
    }

    # PLAN.md: "yapay zeka gozlerin ifadesini arac cagirarak kendisi
    # yonetiyor". Bunu SINAMAK icin araci tanimliyoruz ve kac kez
    # cagrildigini sayiyoruz (yuz.IfadeDefteri).
    if ayarlar.YUZ_ARACI:
        setup["tools"] = [yuz.arac_tanimi()]

    if uslup == USLUP_UST:
        setup.update(uretim)
    else:
        setup["generationConfig"] = uretim

    # Sunucu tarafi VAD. None ise HIC GONDERMIYORUZ — Google'in
    # varsayilaninin ne verdigini once gormek istiyoruz.
    if (ayarlar.VAD_SESSIZLIK_SUNUCU_MS is not None
            or ayarlar.VAD_ONEK_DOLGU_MS is not None):
        vad: dict = {"disabled": False}
        if ayarlar.VAD_SESSIZLIK_SUNUCU_MS is not None:
            vad["silenceDurationMs"] = ayarlar.VAD_SESSIZLIK_SUNUCU_MS
        if ayarlar.VAD_ONEK_DOLGU_MS is not None:
            vad["prefixPaddingMs"] = ayarlar.VAD_ONEK_DOLGU_MS
        setup["realtimeInputConfig"] = {"automaticActivityDetection": vad}

    # PLAN.md: uc mekanizmanin UCUNU DE kullanmaliyiz.
    if ayarlar.OTURUM_DEVAMI:
        setup["sessionResumption"] = (
            {"handle": devam_anahtari} if devam_anahtari else {})
    if ayarlar.BAGLAM_SIKISTIRMA:
        # Tetik ve hedef ACIKCA veriliyor. Varsayilan tetik baglam
        # penceresinin %80'i (~100k) ve o yuzden 21 dakikalik kosuda
        # sikistirma HIC calismadi; baglam 36.500 token'a cikip
        # gecikmeyi +283 ms buyuttu. Ayrinti: ayarlar.py.
        pencere: dict = {}
        if ayarlar.BAGLAM_HEDEF_TOKEN:
            pencere["targetTokens"] = ayarlar.BAGLAM_HEDEF_TOKEN
        sikistirma: dict = {"slidingWindow": pencere}
        if ayarlar.BAGLAM_TETIK_TOKEN:
            sikistirma["triggerTokens"] = ayarlar.BAGLAM_TETIK_TOKEN
        setup["contextWindowCompression"] = sikistirma

    if ayarlar.GIRIS_DOKUMU:
        setup["inputAudioTranscription"] = {}
    if ayarlar.CIKIS_DOKUMU:
        setup["outputAudioTranscription"] = {}

    return {"setup": setup}


# ---------------------------------------------------------------------------
# Istemci
# ---------------------------------------------------------------------------

class Baglanti:
    """
    Tek bir oturumu (birden cok WebSocket baglantisi olabilir) yonetir.

    "Oturum" ile "baglanti" ayri seyler ve PLAN.md bu ayrimin uzerine
    kurulu: baglanti ~10 dakikada kopuyor, oturum devam anahtariyla
    yasamaya devam ediyor. Cocuk kopmayi fark etmemeli.
    """

    def __init__(self, model: str, defter: Defter, hoparlor, mikrofon,
                 gunluk_dosyasi: Path | None = None, sessiz: bool = False):
        self.model = model
        self.defter = defter
        self.hoparlor = hoparlor
        self.mikrofon = mikrofon
        self.sessiz = sessiz

        self.anahtar = ayarlar.api_anahtari()
        self.uslup = USLUP_GENCONFIG
        self.uslup_dogrulandi = False

        self.devam_anahtari: str | None = None
        self.ws = None
        self.kurulum_tamam = asyncio.Event()
        self.calisiyor = True
        self.goaway_geldi = False

        # Metin modunda siradaki soruyu gondermeden once turun bitmesini
        # beklemek icin. Pespese gondermek hem kotayi yakiyor hem de
        # cevaplari birbirine karistiriyor.
        self.tur_bitti_olayi = asyncio.Event()

        self._ham = None
        if gunluk_dosyasi:
            self._ham = gunluk_dosyasi.open("a", encoding="utf-8")

        # Guvenlik/uyum incelemesi icin biriken dokumler
        self._giris_dokum = ""
        self._cikis_dokum = ""

        # Bosta kapatma / uyku — maliyetin en buyuk kalemi (ayarlar.py).
        #
        # UYKU ≠ KAPANMA. Oturum kapaniyor (ucret islemiyor) ama nesne
        # yasiyor ve devam anahtarini sakliyor. Cocuk konusunca yeniden
        # baglanip KALDIGI YERDEN devam ediyor.
        self.son_hareket = simdi()

        # AYAR YENILEMESI — ses/prompt degisince oturumu tazelemek icin.
        #
        # Ses adi setup mesajinda gidiyor ve setup oturum ACILIRKEN bir
        # kez gonderiliyor; calisan oturumun sesini degistirmek mumkun
        # degil. Ama yeniden baglanmak zaten COZULMUS bir is (GoAway):
        # devam anahtariyla baglaniyoruz, hafiza korunuyor, olculen
        # bosluk 568 ms. Ayni yolu kullaniyoruz — yeni mekanizma yok.
        #
        # goaway_geldi bayragini ODUNC ALMIYORUZ: o sunucunun soyledigi
        # bir seyi kaydediyor, defterde "goaway_yenileme" diye
        # gorunuyor. Ayar degisikligini oraya karistirmak olcum
        # kayitlarini kirletirdi.
        self.yenileme_sebebi: str | None = None

        # Uykuya gecerken cagrilan es zamanli kanca (hafiza cikarimi).
        self.uyku_kancasi = None
        # Uyandiktan sonra cagrilan es zamansiz kanca. Uykunun
        # SIMETRIGI: panel yazisi ve kullanim sayaci geri donsun.
        self.uyanma_kancasi = None
        self.uykuda = False
        self.uyanma_istegi = asyncio.Event()
        self.uyanma_sureleri: list[float] = []

        # Uykudayken mikrofonu dinlemeye devam ediyoruz ama Google'a
        # GONDERMIYORUZ (ucret orada isliyor). Son birkac saniyeyi
        # tamponda tutuyoruz ki cocuk konusmaya baslayinca ILK
        # KELIMESI kaybolmasin — uyanma sirasinda soyledigi sey
        # yutulursa robot "beni duymadi" gibi gorunur.
        self.uyku_tamponu: deque = deque(maxlen=ayarlar.UYKU_TAMPON_PARCA)

        self.tur_bitti_geri = None       # (Tur) -> None
        self.ifade_geri = None           # (ifade: str) -> None
        self.olay_geri = None            # (metin: str, seviye: str) -> None
        self.ifade_defteri = yuz.IfadeDefteri()

    # -- yardimcilar -------------------------------------------------------

    def _yaz(self, *parcalar) -> None:
        if not self.sessiz:
            print(*parcalar, flush=True)

    def _ham_kaydet(self, yon: str, veri) -> None:
        if not self._ham:
            return
        try:
            self._ham.write(json.dumps(
                {"t": simdi() - self.defter.t0, "yon": yon, "veri": veri},
                ensure_ascii=False) + "\n")
        except Exception:
            pass

    def _url(self) -> str:
        return f"{ayarlar.WS_UC}?key={self.anahtar}"

    # -- baglanma ----------------------------------------------------------

    async def _tek_baglanti(self, uslup: str) -> bool:
        """
        Bir kez baglanmayi dener. setupComplete gelirse True.

        max_size=None: varsayilan 1 MB sinir, ses parcalari base64
        olarak buyuyebiliyor; sinira takilirsa baglanti sessizce
        kopuyor ve sebebini bulmak saatler aliyor.
        """
        setup = setup_mesaji(self.model, uslup, self.devam_anahtari)
        self._ham_kaydet("giden", setup)

        self.ws = await connect(self._url(), max_size=None,
                                ping_interval=20, ping_timeout=20)
        await self.ws.send(json.dumps(setup))

        # setupComplete'i bekle. Gelmezse setup reddedilmistir.
        try:
            async with asyncio.timeout(15):
                async for ham in self.ws:
                    mesaj = json.loads(ham)
                    self._ham_kaydet("gelen", mesaj)
                    if "setupComplete" in mesaj:
                        return True
                    # setupComplete'ten once baska bir sey gelirse
                    # (genelde hata) ham halini gosteriyoruz.
                    self._yaz("  ! setup oncesi beklenmedik mesaj:",
                              json.dumps(mesaj, ensure_ascii=False)[:300])
        except asyncio.TimeoutError:
            self._yaz("  ! setupComplete 15 saniyede gelmedi")
        return False

    async def baglan(self) -> None:
        """
        Baglanir; gerekirse setup uslubunu degistirip tekrar dener.
        """
        son_hata = None
        for deneme in range(ayarlar.YENIDEN_BAGLANMA_DENEME):
            uslup_sirasi = ([self.uslup] if self.uslup_dogrulandi
                            else [USLUP_GENCONFIG, USLUP_UST])
            for uslup in uslup_sirasi:
                oldu = False
                try:
                    oldu = await self._tek_baglanti(uslup)
                    if oldu:
                        if not self.uslup_dogrulandi:
                            self.uslup = uslup
                            self.uslup_dogrulandi = True
                            self.defter.olay(
                                "setup_uslubu",
                                f"responseModalities -> {uslup}")
                            self._yaz(f"  setup uslubu: {uslup}")
                        self.defter.baglanti_sayisi += 1
                        self.kurulum_tamam.set()
                        self.goaway_geldi = False
                        return
                except (websockets.exceptions.WebSocketException,
                        OSError, asyncio.TimeoutError) as e:
                    son_hata = e
                    self._yaz(f"  ! baglanti hatasi ({uslup}): "
                              f"{type(e).__name__}: {e}")
                finally:
                    # Basarisiz denemenin soketini KAPAT. Yeniden
                    # baglanmalarda uslup zaten dogrulanmis oluyor;
                    # "dogrulanmadiysa kapat" demek o durumda sizinti
                    # birakiyordu.
                    if self.ws and not oldu:
                        try:
                            await self.ws.close()
                        except Exception:
                            pass
                        self.ws = None

            await asyncio.sleep(ayarlar.YENIDEN_BAGLANMA_BEKLEME_SN
                                * (deneme + 1))

        raise ProtokolHatasi(
            f"Baglanti kurulamadi. Son hata: {son_hata}\n"
            f"  - API anahtari dogru mu?\n"
            f"  - Model adi gecerli mi: {self.model}\n"
            f"  - Ham kayit: olcumler/ klasorundeki .jsonl dosyasi")

    # -- gonderme ----------------------------------------------------------

    async def ses_gonder(self) -> None:
        """
        Mikrofon kuyrugunu bosaltip sunucuya akitir.

        Her parca ayri bir JSON mesaji. Cirkin ama ESP32 de aynisini
        yapacak — ve olctugumuz sey tam olarak bu.
        """
        while self.calisiyor:
            parca = await self.mikrofon.kuyruk.get()

            # UYKUDA: mikrofonu dinlemeye devam ediyoruz ama Google'a
            # GONDERMIYORUZ — ucret orada isliyor. Son 2 saniyeyi
            # tamponda tutuyoruz ki uyandiran cumle kaybolmasin.
            if self.uykuda:
                self.uyku_tamponu.append(parca)
                continue

            if not self.ws or not self.kurulum_tamam.is_set():
                continue
            await self._ses_parcasi_gonder(parca)

    async def _ses_parcasi_gonder(self, parca: bytes) -> None:
        if not self.ws:
            return
        mesaj = {
            "realtimeInput": {
                "audio": {
                    "data": base64.b64encode(parca).decode("ascii"),
                    "mimeType": f"audio/pcm;rate={ayarlar.GIRIS_HZ}",
                }
            }
        }
        try:
            await self.ws.send(json.dumps(mesaj))
        except websockets.exceptions.WebSocketException:
            # Baglanti koptu; alma dongusu yeniden baglanacak.
            await asyncio.sleep(0.05)

    async def metin_gonder(self, yazi: str, olc: bool = False,
                           dolgu: bool = False) -> None:
        """
        Test senaryolarinda ve acilis selaminda kullaniliyor.

        DIKKAT: bu yol §4 KRITERINE GIRMIYOR. Metin gonderince
        Gemini'nin "sustu" karari (VAD) devreye girmiyor, yani olculen
        sey cocugun yasadigi gecikme degil.

        olc=True verilirse tur "metin" kaynagiyla acilir ve damgalanir.
        dolgu=True ise tur uyum sayimindan cikariliyor (bkz. olcum.Tur.dolgu).
        Defter bunlari ayri istatistikte tutuyor; ortalamaya
        karismiyorlar. Ise yaradiklari yer: sesli olcumden cikarilinca
        VAD'in maliyeti ortaya cikiyor (bkz. olcum.metin_gecikmeleri).
        """
        # ⚠ ONCE UYANDIR, SONRA BEKLE. Sirasi onemli ve ters yazilmisti.
        #
        # Uykuda `kurulum_tamam` temizli ve uyanmayi baslatan sey
        # `hareket_var()`. Bekleme once gelince kimse uyandirmiyordu:
        # yazi kutusundan gonderilen mesaj 10 saniye bekleyip
        # "baglanti hazir degil" diye dusuyordu. Yani Pati uyuduktan
        # sonra panelin yazi kutusu OLU idi (30.07.2026 uctan uca
        # testinde olculdu: uyku 16,4 sn'de, 40. saniyedeki mesaj hic
        # uyandiramadi).
        #
        # Sesle uyanma etkilenmiyordu: orada `hareket_var()` dogrudan
        # ses geri cagiriminda cagriliyor.
        self.hareket_var()

        # Yenileme/yeniden baglanma sirasinda soket kapali olabilir.
        # Gercekten yasandi: GoAway yenilemesi tam bu anda calisti ve
        # program ConnectionClosedOK ile coktu.
        #
        # ESP32'de de ayni durum olacak — yeniden baglanirken gelen
        # veri programi durdurmamali. Once hazir olmasini bekliyoruz.
        try:
            async with asyncio.timeout(10):
                await self.kurulum_tamam.wait()
        except asyncio.TimeoutError:
            self._yaz("  ! baglanti hazir degil, mesaj gonderilemedi")
            return
        if not self.ws:
            return
        # Bekleme uzun surmus olabilir; bosta sayacini tazele.
        self.hareket_var()
        if olc:
            tur = self.defter.tur_ac()
            tur.kaynak = "metin"
            tur.dolgu = dolgu
            tur.t_sustu = simdi()
            tur.cocuk_dedi = yazi
        mesaj = {"realtimeInput": {"text": yazi}}
        self._ham_kaydet("giden", mesaj)
        try:
            await self.ws.send(json.dumps(mesaj))
        except websockets.exceptions.WebSocketException:
            # Tam bu anda koptu; alma dongusu geri baglayacak.
            self._yaz("  ! mesaj gonderilemedi (baglanti kopmus)")

    # -- alma --------------------------------------------------------------

    async def _mesaji_isle(self, mesaj: dict) -> None:
        defter = self.defter

        # -- kullanim / kota
        if "usageMetadata" in mesaj:
            defter.kullanim_guncelle(mesaj["usageMetadata"])

        # -- oturum devam anahtari
        if "sessionResumptionUpdate" in mesaj:
            g = mesaj["sessionResumptionUpdate"]
            if g.get("resumable") and g.get("newHandle"):
                self.devam_anahtari = g["newHandle"]
                defter.devam_anahtari_sayisi += 1

        # -- GoAway: baglanti kapanmak uzere
        if "goAway" in mesaj:
            kalan = mesaj["goAway"].get("timeLeft", "?")
            self.goaway_geldi = True
            defter.olay("goaway", f"kalan sure: {kalan}")
            self._yaz(f"\n  [oturum] sunucu kapanma uyarisi verdi "
                      f"(kalan: {kalan})")
            if self.olay_geri:
                self.olay_geri(f"sunucu kapanma uyarisi — kalan {kalan}",
                               "uyari")

        # -- arac cagrisi (yuz ifadesi)
        #
        # NON_BLOCKING tanimladigimiz icin model bizi beklemiyor; yine
        # de cevap gondermek gerekiyor, yoksa model araci "cevapsiz"
        # sayip tekrar deneyebiliyor.
        arac = mesaj.get("toolCall")
        if arac:
            cevaplar = []
            for cagri in arac.get("functionCalls", []) or []:
                if cagri.get("name") != yuz.ARAC_ADI:
                    continue
                istenen = (cagri.get("args") or {}).get("ifade", "")
                ifade = self.ifade_defteri.cagri(istenen)
                if self.ifade_geri:
                    self.ifade_geri(ifade)
                self._yaz(f"  [yuz] {ifade}")
                cevaplar.append({
                    "id": cagri.get("id"),
                    "name": cagri.get("name"),
                    # scheduling ZORUNLU (NON_BLOCKING araclarda).
                    # Ham dokumandan uc secenek var:
                    #   INTERRUPT  — yaptigini birak, hemen bundan bahset
                    #   WHEN_IDLE  — isini bitirince degerlendir
                    #   SILENT     — hicbir sey yapma, bilgiyi sakla
                    # Gozler icin SILENT dogru olan: robot ifade
                    # degistirdigini SOYLEMEMELI, sadece degistirmeli.
                    # Bu alani ilk yazdigimda unutmustum ve model
                    # sesli cevap uretmeden turu kapatiyordu.
                    "response": {"result": "ok", "scheduling": "SILENT"},
                })
            if cevaplar and self.ws:
                try:
                    await self.ws.send(json.dumps(
                        {"toolResponse": {"functionResponses": cevaplar}}))
                except websockets.exceptions.WebSocketException:
                    pass    # koptu; yeniden baglanma zaten devrede
            return

        # -- icerik
        icerik = mesaj.get("serverContent")
        if not icerik:
            return

        # Sozunu kesme onayi
        if icerik.get("interrupted"):
            t = simdi()
            tur = defter.aktif
            if tur and tur.t_kesme_konusma and tur.t_kesme_onay is None:
                tur.t_kesme_onay = t
                tur.kesildi = True
            # Dokumanin kendi talimati: kuyruktaki sesi at.
            self.hoparlor.temizle()
            self._yaz("  [kesildi]")

        # Ses
        model_turu = icerik.get("modelTurn") or {}
        for parca in model_turu.get("parts", []):
            gomulu = parca.get("inlineData")
            if not gomulu or not gomulu.get("data"):
                continue
            t_paket = simdi()
            self.hareket_var()
            pcm = base64.b64decode(gomulu["data"])

            tur = defter.aktif_veya_ac()
            if tur.t_ilk_paket is None:
                tur.t_ilk_paket = t_paket
                self.hoparlor.yeni_tur()
            tur.ses_bayt += len(pcm)
            self.hoparlor.ekle(pcm)

        # Dokumler
        gd = icerik.get("inputTranscription")
        if gd and gd.get("text"):
            self._giris_dokum += gd["text"]
        cd = icerik.get("outputTranscription")
        if cd and cd.get("text"):
            self._cikis_dokum += cd["text"]

        # Uretim bitti
        if icerik.get("generationComplete"):
            tur = defter.aktif
            if tur and tur.t_uretim_bitti is None:
                tur.t_uretim_bitti = simdi()

        # Tur bitti
        if icerik.get("turnComplete"):
            tur = defter.aktif

            # ⚠ ARAC CAGRISI TURU'NU "BITTI" SAYMA.
            #
            # Olculdu (29.07.2026): model once yuz_ifadesi'ni cagirip
            # generationComplete + turnComplete gonderiyor, ASIL SESLI
            # CEVABI bundan ~300 ms SONRA veriyor. Bu ilk turnComplete'i
            # "cevap bitti" sayinca siradaki soruyu gonderiyorduk ve
            # robotun sozunu kendimiz kesiyorduk — ham kayitta pespese
            # 'interrupted' olarak goruldu ve 5 turun 5'i olculemedi.
            #
            # Bu yuzden: ses de dokum de uretmemis bir tur BITMIS
            # sayilmiyor. Turu ACIK birakiyoruz ki arkadan gelen ses
            # ayni ture (ve ayni t_sustu damgasina) yazilsin — gecikme
            # olcumu boylece dogru kaliyor.
            bos_tur = (tur is not None
                       and tur.ses_bayt == 0
                       and not self._cikis_dokum.strip()
                       and not icerik.get("interrupted")
                       and not tur.kesildi)
            if bos_tur:
                return

            if tur:
                tur.t_tur_bitti = simdi()
                # Metin turlerinde inputTranscription gelmiyor; gonderdigimiz
                # yaziyi ustune bos dizeyle YAZMAYALIM.
                if self._giris_dokum.strip():
                    tur.cocuk_dedi = self._giris_dokum.strip()
                tur.robot_dedi = self._cikis_dokum.strip()
                if self.tur_bitti_geri:
                    self.tur_bitti_geri(tur)
                if tur.cocuk_dedi:
                    self._yaz(f"  cocuk : {tur.cocuk_dedi}")
                if tur.robot_dedi:
                    self._yaz(f"  Pati  : {tur.robot_dedi}")
                g = tur.gecikme_ms()
                if g is not None:
                    self._yaz(f"  ⏱ {g:.0f} ms  "
                              f"(ag {tur.ag_gecikmesi_ms():.0f} · "
                              f"yerel {tur.yerel_oynatma_ms():.0f})")
                self._yaz("")
                self._giris_dokum = ""
            else:
                # ╔══════════════════════════════════════════════════════╗
                # ║ COCUGUN SOYLEDIKLERI BURADA SILINIYORDU.             ║
                # ╚══════════════════════════════════════════════════════╝
                #
                # Sunucu cocugun dokumunu KENDI serverContent mesajinda
                # gonderiyor ve o mesaj turnComplete de tasiyor. O anda
                # robotun cevabi icin tur HENUZ ACILMAMIS oluyor
                # (`defter.aktif is None`), cunku tur ilk ses paketiyle
                # aciliyor ve o ses ~700 ms sonra geliyor.
                #
                # Eskiden temizleme bu blogun disindaydi: tur yokken de
                # calisiyor ve cocugun cumlesini atiyordu. 30.07.2026
                # telefon oturumunun ham kaydi geri oynatildiginda 10
                # cumlenin 10'u da silinmisti; gunun butun oturumlarinda
                # `cocuk_dedi` doluluk orani 4/62 idi (kurtulanlar sadece
                # tur acikken denk gelenler).
                #
                # Bu tek satir uc seyi birden bozuyordu:
                #   · hafiza — cikarim modeline SADECE robotun cumleleri
                #     gidiyordu; model cocugun adini robotun sozlerinden
                #     tahmin edip "Pargali" yaziyordu (gercek ad: Deniz)
                #   · guvenlik.incele — cocuk tarafi hep bos metin
                #   · metin.ses_istegi — "sesini kis" hic tetiklenmiyordu
                #
                # Dogrusu: silme, BEKLET. Robotun cevabi acilinca ayni
                # ture yazilir.
                if self._giris_dokum and not self._giris_dokum.endswith(" "):
                    # Iki dokum arka arkaya gelirse kelimeler yapismasin.
                    self._giris_dokum += " "
            self._cikis_dokum = ""
            defter.tur_kapat()
            self.tur_bitti_olayi.set()

    async def al_dongusu(self) -> None:
        """
        Mesajlari alir; baglanti koparsa devam anahtariyla geri baglanir.

        PLAN.md'nin somut gereksinimi burada: "20 dakikalik kesintisiz
        sohbette robot kendini toparliyor mu?"
        """
        while self.calisiyor:
            try:
                async for ham in self.ws:
                    mesaj = json.loads(ham)
                    self._ham_kaydet("gelen", mesaj)
                    await self._mesaji_isle(mesaj)

                # Dongu bittiyse sunucu kapatti.
                raise websockets.exceptions.ConnectionClosed(None, None)

            except (websockets.exceptions.ConnectionClosed,
                    websockets.exceptions.WebSocketException, OSError) as e:
                if not self.calisiyor:
                    return

                # UYKU: kopma bizim istegimizle oldu. Cocuk konusana
                # kadar hicbir sey yapma — ucret de kota da islemiyor.
                if self.uykuda:
                    await self.uyanma_istegi.wait()
                    if not self.calisiyor:
                        return
                    t_uyanma = simdi()
                    self._yaz("\n  [oturum] uyaniyor...")
                    try:
                        await self.baglan()
                    except ProtokolHatasi as hata:
                        self._yaz(f"  [oturum] uyanamadi: {hata}")
                        self.calisiyor = False
                        return
                    self.uykuda = False
                    sure = (simdi() - t_uyanma) * 1000.0
                    self.uyanma_sureleri.append(sure)
                    self.defter.olay("uyandi", f"{sure:.0f} ms")

                    # Uyku tamponunu once gonder: cocugun uyandiran
                    # cumlesi kaybolmasin.
                    tampon = list(self.uyku_tamponu)
                    self.uyku_tamponu.clear()
                    for parca in tampon:
                        await self._ses_parcasi_gonder(parca)

                    self._yaz(f"  [oturum] uyandi ({sure:.0f} ms, "
                              f"{len(tampon)} parca tampon gonderildi)")
                    if self.olay_geri:
                        self.olay_geri(f"uyandi ({sure:.0f} ms)", "")
                    # Uyandi. Panel "uyuyor" yazisindan cikmali ve
                    # kullanim sayaci yeniden islemeye baslamali —
                    # uyku_kancasi'nin simetrigi.
                    if self.uyanma_kancasi:
                        try:
                            self.uyanma_kancasi()
                        except Exception:
                            pass
                    self.hareket_var()
                    continue

                t_kopma = simdi()
                self.kurulum_tamam.clear()
                self.defter.olay("koptu", type(e).__name__)
                self._yaz(f"\n  [oturum] baglanti koptu ({type(e).__name__}), "
                          f"yeniden baglaniliyor...")

                # Bekleyen mikrofon verisini AT: yeniden baglaninca
                # birikmis eski sesi bosaltmak hem kotayi yakiyor hem
                # de robotun gecmise cevap vermesine yol aciyor.
                atilan = 0
                while not self.mikrofon.kuyruk.empty():
                    self.mikrofon.kuyruk.get_nowait()
                    atilan += 1

                try:
                    await self.baglan()
                except ProtokolHatasi as hata:
                    self.defter.olay("yeniden_baglanamadi", str(hata)[:120])
                    self._yaz(f"  [oturum] yeniden baglanilamadi: {hata}")
                    self.calisiyor = False
                    return

                bosluk = (simdi() - t_kopma) * 1000.0
                self.defter.yeniden_baglanma += 1
                self.defter.kopma_bosluklari_ms.append(bosluk)
                self.defter.olay(
                    "yeniden_baglandi",
                    f"{bosluk:.0f} ms boslukla"
                    + (" (devam anahtariyla)" if self.devam_anahtari
                       else " (ANAHTARSIZ - gecmis kayboldu)"))
                self._yaz(f"  [oturum] geri baglandi ({bosluk:.0f} ms, "
                          f"{atilan} parca atildi, "
                          f"anahtar={'var' if self.devam_anahtari else 'YOK'})")
                if self.olay_geri:
                    self.olay_geri(
                        f"koptu → {bosluk:.0f} ms'de geri baglandi "
                        f"({'hafiza korundu' if self.devam_anahtari else 'HAFIZA KAYBI'})",
                        "uyari")

    # -- kapanis -----------------------------------------------------------

    async def bosta_bekcisi(self) -> None:
        """
        Konusma olmayinca oturumu kapatir.

        NEDEN: acik oturum, kimse konusmasa bile mikrofonu akitiyor ve
        ses girisi dakika basina ucretleniyor ($0.005/dk). Masada
        bekleyen bir robot icin bu, sohbetin kendisinden pahaliya
        geliyor (bkz. ayarlar.BOSTA_KAPAT_SN).

        ESP32'de bunun karsiligi: yerel VAD sessizligi gorunce
        baglantiyi kapatir, cocuk konusunca yeniden acar. Olculen
        yeniden baglanma suresi 500-640 ms — cocuk fark etmiyor.
        """
        if not ayarlar.BOSTA_KAPAT_SN:
            return
        while self.calisiyor:
            # 0.25 sn: tur araları kisa olabiliyor (0.4 sn), saniyede bir
            # bakinca GoAway icin uygun ani kacirıyorduk. Dort kat sik
            # bakmanin maliyeti bir karsilastirma — ESP32'de bile yok
            # sayilir.
            await asyncio.sleep(0.25)
            if not self.kurulum_tamam.is_set():
                continue
            if self.uykuda:
                continue

            # --- GoAway: kopmayi zamanla ---------------------------------
            #
            # Sunucu baglantiyi kapatmadan ~50 sn once haber veriyor.
            # Beklersek kopma cumlenin ortasina denk gelebiliyor ve o
            # cevap kayboluyor; sonraki cevaba yapisik geliyor
            # ("...oncedeni!Merkur!..." — 21 dakikalik kosuda iki kez
            # goruldu).
            #
            # Cozum en ucuz olani: kopmayi ENGELLEMIYORUZ, sadece
            # konusma bosluguna tasiyoruz. Yeniden baglanma zaten
            # olacakti; biz sadece ne zaman oldugunu seciyoruz.
            #
            #   API maliyeti  : sifir — fazladan istek yok. Aksine,
            #                   yarida kesilen cevap bosa uretilmiyor.
            #   ESP32 yuku    : sifir — zaten calisan 1 sn'lik sayac
            #                   icinde, yeni gorev yok.
            #   Gecikme       : sifir — tur arasinda oluyor.
            #   Hafiza        : korunuyor — devam anahtariyla baglaniyor.
            if self.goaway_geldi and self._sessiz_an():
                self.goaway_geldi = False
                self.defter.olay("goaway_yenileme",
                                 "kopmadan once, tur arasinda yenilendi")
                self._yaz("\n  [oturum] kapanma uyarisi vardi, "
                          "konusma arasinda yenileniyor")
                if self.ws:
                    try:
                        await self.ws.close()   # al_dongusu geri baglar
                    except Exception:
                        pass
                continue

            # --- Ayar yenilemesi (ses degisti gibi) ----------------------
            if self.yenileme_sebebi and self._sessiz_an():
                sebep = self.yenileme_sebebi
                self.yenileme_sebebi = None
                self.defter.olay("ayar_yenileme", sebep)
                self._yaz(f"\n  [oturum] {sebep} — konusma arasinda "
                          f"yenileniyor")
                if self.ws:
                    try:
                        await self.ws.close()   # al_dongusu geri baglar
                    except Exception:
                        pass
                continue

            bosta = simdi() - self.son_hareket
            if bosta >= ayarlar.BOSTA_KAPAT_SN:
                await self.uyu(bosta)

    def _sessiz_an(self) -> bool:
        """
        Konusmanin dogal boslugu mu?

        Robot konusmuyor, ortada yarim kalmis tur yok. Yenilemek icin
        guvenli an.
        """
        if self.defter.aktif is not None:
            return False
        try:
            return not self.hoparlor.caliyor
        except Exception:
            return True

    async def uyu(self, bosta: float = 0.0) -> None:
        """
        Oturumu kapatir ama nesneyi yasatir.

        Devam anahtari saklandigi icin cocuk konusunca KALDIGI YERDEN
        devam ediliyor — robot unutmuyor, sadece susuyor.
        """
        if self.uykuda:
            return
        self.uykuda = True
        self.uyanma_istegi.clear()
        self.kurulum_tamam.clear()
        self.defter.olay("uykuya_gecti",
                         f"{bosta:.0f} sn sessizlik — ucret/kota yakmasin")
        self._yaz(f"\n  [oturum] {bosta:.0f} sn sessizlik → uyudu "
                  f"(konusunca uyanir)")
        if self.olay_geri:
            self.olay_geri(
                f"{bosta:.0f} sn sessizlik → uyudu. Konus, uyanir.",
                "uyari")
        if self.ifade_geri:
            self.ifade_geri("uykulu")
        if self.ws:
            try:
                await self.ws.close()
            except Exception:
                pass

        # HAFIZA CIKARIMI BURADA DA YAPILIYOR.
        #
        # Onceden sadece "durdur"da yapiliyordu ve gercek kullanimda o
        # an hic gelmiyor: cocuk konusmayi birakip gidiyor, kimse
        # dugmeye basmiyor. Gercek robotta ise fis cekiliyor. Sonuc:
        # ogrenilen hicbir sey kaydedilmiyordu.
        #
        # Uyku, "konusma bitti"nin dogal isareti — zaten N dakika
        # sessizlikten sonra geliyor. Ucret: oturum basina ~$0.00065
        # (ucuz cikarim modeli), cocuk beklemiyor cunku konusma bitmis.
        if self.uyku_kancasi:
            try:
                await self.uyku_kancasi()
            except Exception:
                pass

    def uyandir(self) -> None:
        """
        Cocuk konustu. Yerel VAD'den cagriliyor.

        Es zamansiz: ses geri cagirimindan da guvenle cagrilabilsin.
        """
        if not self.uykuda:
            return
        self.uyanma_istegi.set()

    def yenile(self, sebep: str) -> None:
        """
        Oturumu ILK DOGAL BOSLUKTA tazeler.

        Hemen yapmiyoruz: cumlenin ortasinda kesmek cevabi kaybediyor.
        bosta_bekcisi 0,25 sn'de bir bakiyor, yani gecikme fark
        edilmiyor.
        """
        self.yenileme_sebebi = sebep

    def hareket_var(self) -> None:
        self.son_hareket = simdi()
        if self.uykuda:
            self.uyandir()

    async def kapat(self) -> None:
        self.calisiyor = False
        if self.ws:
            try:
                await self.ws.close()
            except Exception:
                pass
        if self._ham:
            try:
                self._ham.close()
            except Exception:
                pass
