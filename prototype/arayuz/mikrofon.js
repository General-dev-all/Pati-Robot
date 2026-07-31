// -*- coding: utf-8 -*-
//
// TARAYICI MIKROFONU — sadece GOSTERI icin.
//
// ⚠ BU DOSYA OLCUM ICIN DEGIL.
//   Normalde ses bilgisayarin mikrofonundan alinip Python'da islenir;
//   PLAN.md "araya tarayici koyarsak olcum bozulur" diyor ve o kural
//   gecerli. Bu dosya o kurali CIGNEMEK icin degil, uzaktaki birinin
//   (Cloudflare tunnel uzerinden telefonuyla) Pati'yle konusabilmesi
//   icin var.
//
//   Bu modda uretilen gecikme sayilari §4 kriteri DEGILDIR: araya
//   tarayici, ag ve tunel giriyor. Python tarafi turlari
//   kaynak="tarayici" diye isaretliyor ve kriter hesabina almiyor.
//
// Ses yolu:
//   telefon mikrofonu → 16 kHz mono Int16 → WebSocket (ikili) → Python
//   Python → 24 kHz Int16 → WebSocket (ikili) → telefon hoparloru

const GIRIS_HZ = 16000;   // Gemini'nin bekledigi
const CIKIS_HZ = 24000;   // Gemini'nin urettigi

// Robot konustuktan sonra mikrofonun kac ms daha kapali kalacagi.
// Hoparlorden cikan sesin odada yankilanip mikrofona ulasmasi zaman
// aliyor; hemen acarsak son hecenin yankisi yakalaniyor.
const YANKI_KUYRUGU_MS = 250;

export class TarayiciSes {
  constructor(gonderPcm) {
    this.gonderPcm = gonderPcm;
    this.girisCtx = null;
    this.cikisCtx = null;
    this.akis = null;
    this.dugum = null;
    this.siradaki = 0;     // cikis zamanlamasi
    this.acik = false;

    // ---------------------------------------------------------------
    // YARIM DUBLEKS — kulakliksiz kullanim icin
    //
    // Gercekten yasandi (iPhone, kulakliksiz): telefonun hoparlorunden
    // cikan Pati'nin sesi ayni telefonun mikrofonuna giriyor, sunucu
    // bunu "cocuk konusuyor" sanip robotun KENDI SOZUNU KESIYOR.
    // Robot cumlenin ortasinda susuyor.
    //
    // Cozum: robot konusurken mikrofonu gondermeyi durdur. Bedeli
    // sozunu kesememek — ama kulakliksiz kullanimda robotun kendi
    // sozunu kesmesi cok daha rahatsiz edici.
    //
    // Kulaklik varsa bu kapatilabilir (panelden), o zaman sozunu
    // kesme de calisir.
    this.yarimDubleks = true;
    this.susturuldu = false;
    this.susturmaGeri = null;   // (bool) -> void ; panele haber
  }

  // Robot su an konusuyor mu? Cikis zamanlamasindan biliyoruz.
  get caliyor() {
    if (!this.cikisCtx) return false;
    return this.cikisCtx.currentTime <
           this.siradaki + YANKI_KUYRUGU_MS / 1000;
  }

  async ac() {
    if (this.acik) return;

    // Mikrofon izni. HTTPS sart — tunel zaten https veriyor.
    this.akis = await navigator.mediaDevices.getUserMedia({
      audio: {
        channelCount: 1,
        echoCancellation: true,   // telefonda hoparlor+mikrofon yan yana
        noiseSuppression: true,
        autoGainControl: true,
      },
    });

    // 16 kHz'i dogrudan istiyoruz; tarayici destekliyorsa yeniden
    // orneklemeye gerek kalmiyor.
    this.girisCtx = new (window.AudioContext || window.webkitAudioContext)(
      { sampleRate: GIRIS_HZ });
    this.cikisCtx = new (window.AudioContext || window.webkitAudioContext)(
      { sampleRate: CIKIS_HZ });

    const kaynak = this.girisCtx.createMediaStreamSource(this.akis);

    // ScriptProcessor eskimis sayiliyor ama her telefonda calisiyor ve
    // burada islenen sey cok basit: float → int16. AudioWorklet ayri
    // dosya ve daha cok kurulum isterdi; gosteri icin gereksiz.
    const parca = 1024;
    this.dugum = this.girisCtx.createScriptProcessor(parca, 1, 1);
    this.dugum.onaudioprocess = (e) => {
      // Robot konusurken mikrofonu GONDERME — yoksa hoparlorden
      // cikan ses mikrofona girip robotun kendi sozunu kestiriyor.
      if (this.yarimDubleks) {
        const sussun = this.caliyor;
        if (sussun !== this.susturuldu) {
          this.susturuldu = sussun;
          if (this.susturmaGeri) this.susturmaGeri(sussun);
        }
        if (sussun) return;
      }

      const giris = e.inputBuffer.getChannelData(0);
      const oran = this.girisCtx.sampleRate / GIRIS_HZ;
      const n = Math.floor(giris.length / oran);
      const pcm = new Int16Array(n);
      for (let i = 0; i < n; i++) {
        // oran 1 ise dogrudan; degilse en yakin ornek (kaba ama
        // gosteri icin yeterli)
        const s = giris[Math.floor(i * oran)];
        const k = Math.max(-1, Math.min(1, s));
        pcm[i] = k < 0 ? k * 0x8000 : k * 0x7fff;
      }
      this.gonderPcm(pcm.buffer);
    };
    kaynak.connect(this.dugum);
    // ScriptProcessor calismasi icin cikisa bagli olmali; sessiz bir
    // kazanc dugumune bagliyoruz ki kendi sesimizi duymayalim.
    const sessiz = this.girisCtx.createGain();
    sessiz.gain.value = 0;
    this.dugum.connect(sessiz);
    sessiz.connect(this.girisCtx.destination);

    this.siradaki = this.cikisCtx.currentTime;
    this.acik = true;
  }

  // Python'dan gelen 24 kHz PCM'i sirayla calar.
  cal(arrayBuffer) {
    if (!this.cikisCtx) return;
    const pcm = new Int16Array(arrayBuffer);
    if (!pcm.length) return;

    const tampon = this.cikisCtx.createBuffer(1, pcm.length, CIKIS_HZ);
    const kanal = tampon.getChannelData(0);
    for (let i = 0; i < pcm.length; i++) kanal[i] = pcm[i] / 0x8000;

    const kaynak = this.cikisCtx.createBufferSource();
    kaynak.buffer = tampon;
    kaynak.connect(this.cikisCtx.destination);

    // Parcalari ust uste bindirmeden arka arkaya diziyoruz.
    const simdi = this.cikisCtx.currentTime;
    if (this.siradaki < simdi) this.siradaki = simdi + 0.05;
    kaynak.start(this.siradaki);
    this.siradaki += tampon.duration;
    this.calanlar = this.calanlar || [];
    this.calanlar.push(kaynak);
    kaynak.onended = () => {
      const i = this.calanlar.indexOf(kaynak);
      if (i >= 0) this.calanlar.splice(i, 1);
    };
  }

  // Sozunu kesme: kuyruktaki sesi at.
  temizle() {
    for (const k of this.calanlar || []) {
      try { k.stop(); } catch { /* zaten bitmis olabilir */ }
    }
    this.calanlar = [];
    if (this.cikisCtx) this.siradaki = this.cikisCtx.currentTime;
  }

  kapat() {
    this.temizle();
    if (this.dugum) { this.dugum.disconnect(); this.dugum = null; }
    if (this.akis) {
      this.akis.getTracks().forEach((t) => t.stop());
      this.akis = null;
    }
    for (const c of [this.girisCtx, this.cikisCtx]) {
      if (c) { try { c.close(); } catch { /* kapali olabilir */ } }
    }
    this.girisCtx = this.cikisCtx = null;
    this.acik = false;
  }
}
