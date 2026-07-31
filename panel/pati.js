// -*- coding: utf-8 -*-
//
// Pati — tek sayfa: cocugun gordugu yuz + ebeveynin paneli.
//
// ===========================================================================
// BU SAYFA HEM ONIZLEME HEM GERCEK TEST
// ===========================================================================
//
// Robot yokken: gozler cizilir, panel ornek veriyle dolar. Tasarim
// incelenebilir. Ustte sari serit "ornek veri" der.
//
// Python calisiyorken (`pati.py --arayuz --sayfa pati`): ayni sayfa
// GERCEKTEN konusur. Gozler gercek konusma akisindan surulur, panel
// gercek hafizayi ve gercek kullanim dakikasini gosterir, kaydiricilar
// gercekten uygulanir.
//
// Robot geldiginde: panel kismi OLDUGU GIBI ESP32'den servis edilecek,
// gozler ise C++'a cevrilmis haliyle ekranda cizilecek. gozler240.js
// zaten tarayicinin cizim komutlarini kullanmiyor — 240x240 tamponuna
// piksel piksel yaziyor — o yuzden buradaki gorunum tasinabilir.
//
// ===========================================================================
// OLCUM SAYILARI BURADA GOSTERILMIYOR — ama KAYDEDILIYOR
// ===========================================================================
//
// p90 gecikme, dolgu turu, tur dokumu, kriter karari: Python tarafi
// hepsini tutmaya devam ediyor ve durdurunca `prototype/olcumler/` icine
// rapor yaziyor. Ebeveyn onlari gormek istemiyor; gerektiginde
// dosyadan okunuyor.
//
// ===========================================================================
// SES YOLU — iki mod, ve neden kendiliginden secilyor
// ===========================================================================
//
//   YEREL (bu bilgisayarda acildi)
//     mikrofon -> Python -> Gemini -> Python -> hoparlor
//     Tarayici ses hattinda YOK. Olculen gecikme §4 kriteri sayilir.
//
//   UZAK (telefondan / tunelden acildi)
//     telefon mikrofonu -> tarayici -> Python -> Gemini -> ... -> telefon
//     Araya tarayici, ag ve tunel giriyor. Python bu turlari
//     kaynak="tarayici" diye isaretliyor ve kriter hesabina ALMIYOR.
//
// Mod kullaniciya SORULMUYOR. Sormak, her seferinde yanlis secme
// firsati vermek olurdu; adres zaten hangisi oldugunu soyluyor.

import { Gozler240 } from './gozler240.js';
import { TarayiciSes } from './mikrofon.js';
import { DURUM as ORNEK, SINIR, SESLER as ORNEK_SESLER, TAKIM_ADI, tara }
  from './ornek.js';

const $ = (s) => document.querySelector(s);

// ---------------------------------------------------------------------------
// ISTENMEYEN ODAK — iOS'un captive portal penceresi
// ---------------------------------------------------------------------------
//
// 🔴 31.07.2026, iPhone'da gorildi. Pati'nin agina baglaninca sayfa
// "Kısıtlanmış Wi-Fi" penceresinde aciliyor: Safari degil, isletim
// sisteminin kendi mini tarayicisi. O pencere sayfayi bir GIRIS FORMU
// saniyor ve ilk gorunur metin kutusuna KENDILIGINDEN odakleniyor.
//
// Bizdeki ilk gorunur metin kutusu "Çocuğun adı" — wifi sifre kutusu
// ondan once ama `hidden`. Sonuc: sayfa acilir acilmaz klavye
// aciliyordu, sayfa surekli o alana kayiyordu, klavye kapatilsa bile
// bir sonraki tazelemede geri aciliyordu. Wifi listesine dokunmak bile
// mumkun degildi — kurulumun TEK isi o.
//
// Not: cocugun adi zorunlu DEGIL, hic olmadi. Ne `required` var ne
// form dogrulamasi. Sorun bastan beri bu odaklanmaydi.
//
// Cozum: kullanicinin DOKUNMADIGI odaklanmayi kabul etmiyoruz. Gercek
// dokunustan sonra gelen odak (ag secince acilan sifre kutusu gibi)
// milisaniyeler icinde geldigi icin gecerli sayiliyor.
let son_dokunus = 0;
for (const olay of ['pointerdown', 'touchstart', 'mousedown', 'keydown']) {
  addEventListener(olay, () => { son_dokunus = Date.now(); }, true);
}
addEventListener('focusin', (e) => {
  const h = e.target;
  if (!h || !h.matches || !h.matches('input, textarea, select')) return;
  if (Date.now() - son_dokunus < 700) return;   // kullanici gercekten istedi
  h.blur();
}, true);

// Bizim dinleyicimiz kurulmadan ONCE odaklanmis olabilir.
const odagi_birak = () => {
  const a = document.activeElement;
  if (a && a !== document.body && typeof a.blur === 'function') a.blur();
};
odagi_birak();
addEventListener('load', odagi_birak);
// WebSheet odaklamayi sayfa yuklendikten SONRA da yapabiliyor.
setTimeout(odagi_birak, 400);

const gozler = new Gozler240($('#gozler'));

// ---------------------------------------------------------------------------
// Durum
// ---------------------------------------------------------------------------

const YEREL = ['127.0.0.1', 'localhost', '::1'].includes(location.hostname);

// Tarayici mikrofonu HTTPS ister (localhost hariç). Duz http ile ev
// agindan acilirsa `getUserMedia` sessizce reddediliyor ve kullanici
// "mikrofon calismiyor" diye kod arar. Onceden soyluyoruz.
const GUVENLI = window.isSecureContext || YEREL;

const D = {
  canli: false,              // Python'a baglandik mi
  calisiyor: false,          // Pati acik mi
  konusuyor: false,
  uyuyor: false,             // sessizlikten sonra oturum kapandi mi
  robotAdi: '',              // cocugun robota taktigi ad
  robotAdiVarsayilan: 'Pati',
  seviye: ORNEK.ses.seviye,
  seviyeEnAz: SINIR.seviyeEnAz,
  seviyeEnFazla: SINIR.seviyeEnFazla,
  hiz: ORNEK.ses.hiz,
  sesAdi: ORNEK.ses.sesAdi,
  sesler: ORNEK_SESLER.map((a) => ({ ad: a, tanim: '' })),
  uyku: ORNEK.uyku,
  // Konusma ayarlari. Soz kesme VARSAYILAN KAPALI: kulakliksizken
  // Pati kendi sozunu kesiyor (yasandi, uydurma degil).
  sozKesme: false,
  vad: '',                   // '' = Google varsayilani
  yuz: true,
  cocuk: { ...ORNEK.cocuk },
  ekBilgi: ORNEK.ekBilgi,
  hafiza: ORNEK.hafiza.map((h) => ({ ...h })),
  kota: { ...ORNEK.kota },
  wifi: { ...ORNEK.wifi },
  bagli: ORNEK.bagli,
  kurulum: false,
  // Gemini anahtari. `durum` firmware'den geliyor ve panelin gosterecegi
  // uyariyi belirliyor: yok · bilinmiyor · gecerli · gecersiz · kota ·
  // ulasilamadi (bkz. ANAHTAR_HALI).
  anahtar: { ...ORNEK.anahtar },
  // Guncelleme. `durum`: bos · bakiliyor · guncel · var · iniyor · bitti
  guncelleme: { ...ORNEK.guncelleme },
};

// ---------------------------------------------------------------------------
// Tost
// ---------------------------------------------------------------------------

let tostZaman = null;
function tost(yazi, hata = false) {
  const t = $('#tost');
  t.textContent = yazi;
  t.className = 'tost' + (hata ? ' hata' : '');
  t.hidden = false;
  if (tostZaman) clearTimeout(tostZaman);
  tostZaman = setTimeout(() => { t.hidden = true; }, 1900);
}

// Parmak kaydirirken her degeri gondermek hem gereksiz hem NVS yazma
// sayisini artiriyor. Son deger gonderiliyor, aradakiler atiliyor.
function geciktir(ms, is) {
  let z = null;
  return (...a) => {
    if (z) clearTimeout(z);
    z = setTimeout(() => is(...a), ms);
  };
}

// ---------------------------------------------------------------------------
// Baglanti
// ---------------------------------------------------------------------------
//
// WS adresi SAYFANIN KENDI adresinden turetiliyor. Sabit port yazmak
// tunel arkasinda kiriliyordu: sayfa https'ten geliyor, ws:// engelli.

let ws = null;
const tarayiciSes = new TarayiciSes((pcm) => {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(pcm);
});

// Robot konusurken mikrofon kapaniyor (yanki korumasi). Kullanici
// sessizligin sebebini bilsin diye guc notuna yaziliyor.
tarayiciSes.susturmaGeri = (sussun) => {
  if (!D.calisiyor) return;
  gucNot(sussun ? 'Pati konuşuyor · mikrofon kapalı' : 'dinliyor');
};

// ---------------------------------------------------------------------------
// ROBOT MODU
// ---------------------------------------------------------------------------
//
// Ayni sayfa iki yerde calisiyor:
//
//   TEZGAH  Python sunucusu · WebSocket · tarayici mikrofonu
//   ROBOT   ESP32 · REST (/api/durum, /api/ayar, /api/aglar, ...)
//
// Hangisinde oldugunu sayfa KENDISI anliyor: `/api/durum` cevap verirse
// robot. Kullaniciya sormuyoruz — sormak her acilista yanlis secme
// firsati vermek olurdu.
//
// Panel kodunun geri kalani IKISINI DE BILMIYOR: `gonder()` komutu
// hangi tarafa gidiyorsa oraya ceviriyor, `mesaj()` ise iki taraftan
// gelen olaylari ayni sekilde isliyor. Boylece tek kod yolu kaliyor ve
// "tezgahta calisiyor ama robotta calismiyor" durumu olusmuyor.

let ROBOT = false;

// WS komutlarini REST'e ceviren tablo. Panel kodu degismiyor.
async function reste_cevir(nesne) {
  const t = nesne.tip;
  const gonderJson = (yol, govde) =>
    fetch(yol, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(govde),
    });

  switch (t) {
    case 'ses_seviyesi':
      return gonderJson('/api/ayar', { alan: 'seviye', deger: nesne.deger });
    case 'ses_ayarla':
      // Iki ayri alan: firmware ses ve tizligi ayri sakliyor cunku
      // biri setup mesajinda, oteki I2S saatinde.
      await gonderJson('/api/ayar', { alan: 'ses', deger: nesne.ses });
      return gonderJson('/api/ayar', { alan: 'hiz', deger: nesne.hiz });
    case 'uyku':
      return gonderJson('/api/ayar', { alan: 'uyku', deger: nesne.dakika });
    case 'soz_kesme':
      return gonderJson('/api/ayar',
                        { alan: 'soz_kesme', deger: nesne.acik ? 1 : 0 });
    case 'gecikme_ayar':
      await gonderJson('/api/ayar',
                       { alan: 'vad', deger: parseInt(nesne.vad || 0, 10) });
      return gonderJson('/api/ayar', { alan: 'yuz', deger: nesne.yuz ? 1 : 0 });
    case 'cocuk_ayarla':
      return gonderJson('/api/ayar',
                        { alan: 'ad', ad: nesne.ad, yas: nesne.yas || 0 });
    case 'robot_adi':
      return gonderJson('/api/ayar',
                        { alan: 'robot_adi', deger: nesne.ad });
    case 'ebeveyn_notu':
      return gonderJson('/api/ayar',
                        { alan: 'ebeveyn_notu', yazi: nesne.yazi });
    case 'bilgi_sil':
      return gonderJson('/api/hafiza', { id: nesne.id });
    case 'hafiza_sifirla':
      return gonderJson('/api/hafiza', { hepsi: true });
    case 'anahtar_yaz':
      return gonderJson('/api/anahtar', { deger: nesne.deger });
    case 'guncelleme_bak':
      return gonderJson('/api/guncelleme', { is: 'bak' });
    case 'guncelleme_kur':
      return gonderJson('/api/guncelleme', { is: 'kur' });
    case 'ses_dene':
      // Robotta panel YOK: ayarlanan ses robotun kendi
      // hoparlorunden zaten duyulacak. Sahte bir dugme birakmak
      // yerine kullaniciya soyluyoruz.
      tost('Ses, Pati bir sonraki cümlesinde duyulacak');
      return null;
    case 'baslat':
    case 'durdur':
    case 'ses_kanali':
      // Robot fis takiliyken zaten calisiyor; guc dugmesi gizli.
      return null;
    default:
      console.warn('robot modunda karsiligi olmayan komut:', t);
      return null;
  }
}

// ---------------------------------------------------------------------------
// TEZGAHTA ANAHTAR VE GUNCELLEME — benzetim
// ---------------------------------------------------------------------------
//
// Bu ikisinin Python tarafinda karsiligi YOK ve olamaz: anahtar robotun
// NVS'inde duruyor, guncelleme robotun kendi flash'ina yaziyor.
// Bilgisayarda calisan bir sunucunun ikisini de yapacak bir sey yok.
//
// Ama TASARIMLARI incelenebilmeli. TELEFONDAN-INCELE.bat ile bakilan
// panel, annenin gordugu panelin AYNISI olmali — kart varsa ikisinde de
// var, uyari kirmiziysa ikisinde de kirmizi. Aksi halde burada
// onaylanan bir tasarimin robotta baska gorunmesi mumkun olur ve o
// zaman testin hicbir seyi dogrulamadigi anlasilmaz.
//
// O yuzden tezgahta bu komutlar sunucuya HIC gitmiyor; burada, gercek
// firmware'in yaptigi durum gecislerinin aynisi taklit ediliyor.
// Gecikmeler de uydurma degil: 1,2 sn agdan cevap beklemenin, ~25 sn de
// 1,4 MB'in ev wifi'sinden inmesinin kabaca karsiligi.
//
// Hangi sonucun cikacagini VERI TAKIMI belirliyor (ornek.js): `zor`
// takiminda anahtar kotasi doluyor, otekilerde calisiyor. Boylece
// uyarinin kirmizi ve sari hali de, "guncelleme yok" hali de
// gorulebiliyor:  panel/?veri=zor
let tezgah_sayac = null;

function tezgah_komut(nesne) {
  const gecikmeli = (ms, is) => { setTimeout(is, ms); return true; };

  switch (nesne.tip) {
    case 'anahtar_yaz':
      return gecikmeli(1200, () => {
        // Kaydedilen anahtarin son dort karakteri — firmware de tam
        // bunu geri gonderiyor, anahtarin kendisini degil.
        const d = ORNEK.anahtar.kaydedince || 'gecerli';
        D.anahtar = {
          var: true,
          durum: d,
          kuyruk: nesne.deger.slice(-4),
          ayrinti: d === 'kota'
            ? 'You exceeded your current quota, please check your plan '
              + 'and billing details.'
            : '',
        };
        anahtarYaz();
        tost(d === 'gecerli' ? 'Anahtar çalışıyor ✓' : 'Anahtar kabul edilmedi',
             d !== 'gecerli');
      });

    case 'guncelleme_bak':
      return gecikmeli(1200, () => {
        const o = ORNEK.guncelleme;
        D.guncelleme = { ...D.guncelleme, ...o.bakinca };
        guncellemeYaz();
      });

    case 'guncelleme_kur':
      // Cubugu gercekten yuruttuyoruz: yerlesim %0'da bozulmuyor ama
      // %100'de ya da uc haneli yuzde yazisinda bozulabilir.
      if (tezgah_sayac) clearInterval(tezgah_sayac);
      tezgah_sayac = setInterval(() => {
        const y = (D.guncelleme.yuzde || 0) + 4;
        if (y >= 100) {
          clearInterval(tezgah_sayac);
          tezgah_sayac = null;
          D.guncelleme = { ...D.guncelleme, durum: 'bitti', yuzde: 100 };
        } else {
          D.guncelleme = { ...D.guncelleme, durum: 'iniyor', yuzde: y };
        }
        guncellemeYaz();
      }, 1000);
      return true;

    default:
      return false;
  }
}

function gonder(nesne) {
  if (ROBOT) {
    // Hata sessizce yutulmuyor: kaydedilemedi ise kullanici gorsun.
    reste_cevir(nesne)
      .then((c) => {
        if (c && !c.ok) {
          return c.json().catch(() => ({})).then((h) => {
            tost(h.hata || 'kaydedilemedi', true);
          });
        }
        return null;
      })
      .catch(() => tost('Pati\'ye ulaşılamadı', true));
    return true;
  }
  // Tezgahta anahtar ve guncelleme sunucuya GITMIYOR, burada taklit
  // ediliyor (yukarida gerekcesi). Bu kontrol WebSocket'ten ONCE
  // olmali: Python bu komutlari tanimiyor ve sessizce yutardi — dugme
  // basilir, hicbir sey olmaz, sebebi de gorunmezdi.
  if (tezgah_komut(nesne)) return true;

  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(nesne));
    return true;
  }
  return false;
}

// /api/durum cevabini panelin bekledigi olaylara cevirir. Boylece
// robot modu da `mesaj()` yolunu kullaniyor.
function durumu_uygula(d) {
  if (d.ag) {
    D.wifi = { ad: d.ag.ad || '—', guc: d.ag.guc || 0 };
    D.bagli = !!d.ag.bagli;
    D.kurulum = !!d.ag.kurulum;
    wifiYaz();
  }
  if (d.ses) {
    mesaj({ tip: 'ses_seviyesi', deger: d.ses.seviye,
            en_az: d.ses.en_az, en_fazla: d.ses.en_fazla });
    // Ses listesi robotta sabit degil: firmware secili adi biliyor,
    // liste ornek.js'ten geliyor. Secili olan listede yoksa ekliyoruz,
    // yoksa acilir menu bos gorunur.
    const liste = D.sesler.map((v) => v.ad);
    if (d.ses.ses_adi && !liste.includes(d.ses.ses_adi)) {
      D.sesler = [{ ad: d.ses.ses_adi, tanim: '' }, ...D.sesler];
    }
    mesaj({ tip: 'sesler', liste: D.sesler, secili: d.ses.ses_adi,
            hiz: d.ses.hiz });
  }
  if (typeof d.uyku === 'number') {
    mesaj({ tip: 'uyku', dakika: d.uyku });
  }
  if (d.konusma) {
    mesaj({ tip: 'gecikme_ayar', vad: d.konusma.vad || '',
            yuz: d.konusma.yuz, soz_kesme: d.konusma.soz_kesme });
  }
  if (d.kullanim) {
    mesaj({ tip: 'kullanim', bugun_dk: d.kullanim.bugun_dk,
            ay_dk: d.kullanim.ay_dk, tahmin_usd: d.kullanim.tahmin_usd });
  }
  if (d.hafiza) {
    hafizaAl(d.hafiza);
  }
  if (d.ifade) {
    // Gozler robotun EKRANINDA; buradaki yuz onun aynasi. Ebeveyn
    // Pati'nin o an ne hissettigini gorüyor.
    gozler.ayarla(d.ifade);
  }
  // 🔴 UYKU AYRI ALANDAN. Robot modunda bu satir YOKTU: gozler dogru
  // "uykulu" gorunuyor ama ustteki yazi "dinliyor" diyordu
  // (31.07.2026, telefonda goruldu). Ifadeden CIKARMIYORUZ — "uykulu"
  // ifadesini model de secebiliyor, yani uyanikken de gorulebilir.
  if (typeof d.uyuyor === 'boolean') {
    D.uyuyor = d.uyuyor;
  }
  if (d.anahtar) {
    D.anahtar = {
      var: !!d.anahtar.var,
      durum: d.anahtar.durum || 'bilinmiyor',
      kuyruk: d.anahtar.kuyruk || '',
      ayrinti: d.anahtar.ayrinti || '',
    };
    anahtarYaz();
  }
  if (d.guncelleme) {
    D.guncelleme = {
      durum: d.guncelleme.durum || 'bos',
      suAnki: d.guncelleme.su_anki || '',
      yeni: d.guncelleme.yeni || '',
      notlar: d.guncelleme.notlar || '',
      yuzde: d.guncelleme.yuzde || 0,
      hata: d.guncelleme.hata || '',
    };
    guncellemeYaz();
  }
  D.calisiyor = !!(d.ag && d.ag.bagli);
  durumYaz();
}

// Ust uste kac yoklama basarisiz oldu.
//
// 🔴 31.07.2026: TEK bir basarisiz yoklamada "baglanti koptu" seridi
// aciliyordu ve iki saniye sonra tekrar kapaniyordu. Serit sayfanin
// EN USTUNE ekleniyor, yani her acilip kapanista butun icerik kayiyor
// — telefonda odaktaki alana surekli geri firlatiyordu, panel
// gezilemiyordu.
//
// Yoklamalar gercekten basarisiz oluyordu: ESP32'nin soketleri
// tukeniyordu (httpd_accept_conn error 23). O ayri duzeltildi
// (sdkconfig.defaults, LWIP_MAX_SOCKETS) ama tek bir kayip istegin
// paneli sarsmasi zaten yanlisti. Uc ust uste hata gerekiyor.
let yoklama_hatasi = 0;

async function robot_yokla() {
  try {
    const c = await fetch('/api/durum', { cache: 'no-store' });
    if (!c.ok) throw new Error('durum');
    durumu_uygula(await c.json());
    yoklama_hatasi = 0;
    if (!D.canli) {
      D.canli = true;
      seritGuncelle();
      gucYaz();
    }
  } catch {
    if (++yoklama_hatasi < 3) return;
    if (D.canli) {
      D.canli = false;
      seritGuncelle();
      gucYaz();
      durumYaz();
    }
  }
}

// ---------------------------------------------------------------------------
// SEKMEYE GERI DONUS — telefonda mikrofonu dirilt
//
// Gercek kullanimdan (30.07.2026): telefonda Pati acikken baska bir
// uygulamaya gecip geri donunce Pati bir daha uyanmiyordu. Sebep
// tarayicinin arka plandaki sekmenin AudioContext'ini askiya almasi;
// mikrofondan tek bayt gitmedigi icin uykudaki Pati'yi uyandiracak
// ses hic ulasmiyor. Tek care Pati'yi durdurup baslatmakti.
//
// ⚠ ROBOTUN SORUNU DEGIL: ESP32'de tarayici yok, mikrofon I2S'ten
//   kesintisiz akiyor. Burasi telefondan test yolunu ayakta tutuyor.
document.addEventListener('visibilitychange', async () => {
  if (document.visibilityState !== 'visible') return;
  if (!tarayiciSes.acik) return;
  try {
    if (await tarayiciSes.tazele()) {
      tarayiciSes.hiz = D.hiz;
      gonder({ tip: 'ses_kanali', ac: true });
      tost('mikrofon yeniden bağlandı');
    }
  } catch {
    // Izin gercekten iptal edilmis olabilir; kullanici gorsun.
    tost('mikrofon izni gerekiyor · Pati’yi durdurup başlat', true);
  }
});

function bagla() {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${proto}//${location.host}/ws`);
  ws.binaryType = 'arraybuffer';

  ws.onopen = () => {
    D.canli = true;
    seritGuncelle();
    durumYaz();
    // ⚠ Bu satir eksikti ve dugme kilitli kaliyordu: gucYaz() dugmeyi
    //   `D.canli`ye gore etkinlestiriyor, cagirilmadan once devre disi
    //   basliyor. Baglandi ama basilamiyordu.
    gucYaz();

    // SES KANALINI YENIDEN BILDIR.
    //
    // Sunucu "robotun sesi hangi sekmeye gidecek" bilgisini WebSocket
    // NESNESINE bagliyor (sunucu.ses_istemcisi). Baglanti kopup geri
    // gelince o nesne yenisi oluyor ve kanal sahipsiz kaliyor: Pati
    // konusuyor ama telefondan ses gelmiyordu. Telefonda sekme arka
    // plana atilinca baglanti gercekten kopuyor, yani bu her gun
    // yasanan bir durum.
    if (tarayiciSes.acik) gonder({ tip: 'ses_kanali', ac: true });
  };

  ws.onclose = () => {
    D.canli = false;
    D.calisiyor = false;
    seritGuncelle();
    gucYaz();
    durumYaz();
    gozler.ayarla('bos');
    // Python yeniden baslatilirsa kendiliginden geri baglansin.
    setTimeout(bagla, 1500);
  };

  ws.onmessage = (e) => {
    // Ikili cerceve = robotun sesi (24 kHz PCM). Metin = JSON olay.
    if (e.data instanceof ArrayBuffer) { tarayiciSes.cal(e.data); return; }
    let m;
    try { m = JSON.parse(e.data); } catch { return; }
    mesaj(m);
  };
}

// ---------------------------------------------------------------------------
// Python'dan gelen olaylar
// ---------------------------------------------------------------------------

function mesaj(m) {
  switch (m.tip) {

    // --- gozler
    case 'durum':
      gozler.ayarla(m.durum);
      D.konusuyor = m.durum === 'konusuyor';
      // "uykulu" = oturum kapandi (bkz. pati.uyku_durumu). Once
      // sadece gozler degisiyordu ve panel uyurken de "dinliyor"
      // yaziyordu; robotun uyuyup uyumadigi anlasilmiyordu.
      D.uyuyor = m.durum === 'uykulu';
      durumYaz();
      gucYaz();
      break;

    case 'ifade':
      // Modelin kendi sectigi ifade (yuz_ifadesi araci).
      gozler.ayarla(m.ifade);
      break;

    case 'ses_temizle':
      // Sozunu kesme: kuyruktaki sesi at, yoksa robot kesilmis
      // cumleyi calmaya devam ediyor.
      tarayiciSes.temizle();
      break;

    // --- durum
    case 'baglanti':
      D.calisiyor = m.durum === 'bagli';
      gucYaz();
      durumYaz();
      break;

    // --- ayarlar
    case 'ses_seviyesi':
      D.seviye = m.deger;
      if (m.en_az != null) D.seviyeEnAz = m.en_az;
      if (m.en_fazla != null) D.seviyeEnFazla = m.en_fazla;
      seviyeYaz();
      break;

    case 'sesler':
      D.sesler = m.liste || D.sesler;
      D.sesAdi = m.secili || D.sesAdi;
      D.hiz = m.hiz != null ? m.hiz : D.hiz;
      tarayiciSes.hiz = D.hiz;
      seslerYaz();
      break;

    case 'ses_durum':
      // Onizleme caliyor mu — dugmeyi kilitlemek icin.
      $('#dene').disabled = !!m.calisiyor;
      $('#dene').textContent = m.calisiyor ? 'çalıyor…' : 'Dinle';
      break;

    case 'uyku':
      // ⚠ ONCEDEN Math.round VARDI. Varsayilan 90 saniye = 1,5 dk;
      //   yuvarlaninca panel "2 dk" yaziyordu ve kaydirici da 2'ye
      //   oturuyordu. Yani panel, ayarlanmamis bir degeri ayarlanmis
      //   gibi gosteriyordu — kullanici 2 dk sanip 90 saniye
      //   yasiyordu. Gercek degeri gosteriyoruz.
      D.uyku = m.dakika;
      uykuYaz();
      break;

    case 'gecikme_ayar':
      D.vad = m.vad == null ? '' : String(m.vad);
      D.yuz = !!m.yuz;
      D.sozKesme = !!m.soz_kesme;
      konusmaYaz();
      break;

    case 'kullanim':
      D.kota = { bugunDk: m.bugun_dk, ayDk: m.ay_dk, tahminUsd: m.tahmin_usd };
      kotaYaz();
      break;

    case 'hafiza':
      hafizaAl(m.ozet);
      break;

    // --- olcum ve gunluk: panelde GOSTERILMIYOR.
    // Konsola dusuyor cunku bir sey ters gittiginde ("baglanilamadi",
    // "hoparlor acilamadi") sebebini gormek gerekiyor. Kullanici
    // bunlari gormek zorunda degil, ben gerektiginde bakiyorum.
    case 'olay':
      (m.seviye === 'hata' ? console.error : console.log)('pati:', m.metin);
      if (m.seviye === 'hata') gucNot(m.metin, true);
      break;

    default:
      break;   // tur / ozet / gecikme_ayar — olcum tezgahina ait
  }
}

function hafizaAl(ozet) {
  if (!ozet) return;
  D.cocuk = {
    ad: ozet.cocuk?.ad || '',
    yas: ozet.cocuk?.yas || '',
  };
  // Robotun adini COCUK degistiriyor; burasi salt okunur gosterge.
  D.robotAdi = ozet.robot_adi || D.robotAdi;
  D.robotAdiVarsayilan = ozet.robot_adi_varsayilan || D.robotAdiVarsayilan;
  D.ekBilgi = ozet.ebeveyn_notu || '';
  D.hafiza = (ozet.bilgiler || []).map((b) => ({
    id: b.id, metin: b.metin, kez: b.kez || 1,
  }));
  cocukYaz();
  hafizaYaz();
}

// ---------------------------------------------------------------------------
// Serit — ornek veri uyarisi
// ---------------------------------------------------------------------------

let serit = null;
function seritGuncelle() {
  if (D.canli) {
    if (serit) { serit.remove(); serit = null; }
    return;
  }
  if (serit) return;
  serit = document.createElement('div');
  // Robot modunda serit AKISI BOZMUYOR (bkz. stil.css .ucan): baglanti
  // bir anligina kopunca sayfa asagi yukari kaymasin.
  serit.className = 'ornek-serit' + (ROBOT ? ' ucan' : '');
  serit.textContent = ROBOT
    ? 'Pati’ye ulaşılamıyor — bağlantı koptu'
    : (TAKIM_ADI === 'normal'
        ? 'örnek veri · Pati çalışmıyor'
        : `örnek veri: ${TAKIM_ADI} · Pati çalışmıyor`);
  document.body.prepend(serit);
  // Surum yazisi artik Guncelleme kartinda ve guncellemeYaz() yaziyor.
  // Iki yerden birden yazilsaydi biri otekini eziyordu.
}

// ---------------------------------------------------------------------------
// Guc dugmesi
// ---------------------------------------------------------------------------

function gucNot(yazi, hata = false) {
  const n = $('#gucNot');
  n.textContent = yazi;
  n.className = 'guc-not' + (hata ? ' hata' : '');
}

function gucYaz() {
  const d = $('#guc');
  d.textContent = D.calisiyor ? 'Pati’yi durdur' : 'Pati’yi başlat';
  d.classList.toggle('calisiyor', D.calisiyor);
  d.disabled = !D.canli;
  if (!D.canli) {
    gucNot('Pati çalışmıyor · önizleme');
  } else if (D.calisiyor) {
    // Uyku ayri yaziliyor: iki yazi da "dinliyor" derse robotun
    // uyudugu hicbir yerden anlasilmiyor (tek isaret gozlerdi).
    gucNot(D.uyuyor ? 'uyuyor · konuşunca uyanır'
                    : (YEREL ? 'konuşabilirsin' : 'dinliyor'));
  } else if (!GUVENLI) {
    // Bu, telefondan denerken karsilasilan tek gercek engel.
    gucNot('konuşmak için https gerekiyor · PAYLAS.bat', true);
  } else {
    gucNot(YEREL ? 'bilgisayarın mikrofonu'
                 : 'bu cihazın mikrofonu · izin istenecek');
  }
}

$('#guc').addEventListener('click', async () => {
  if (!D.canli) return;

  if (D.calisiyor) {
    gonder({ tip: 'durdur' });
    if (tarayiciSes.acik) {
      gonder({ tip: 'ses_kanali', ac: false });
      tarayiciSes.kapat();
    }
    return;
  }

  // UZAK MOD: once mikrofon izni, SONRA baslat.
  //
  // Sirasi onemli: once baslatip sonra izin istersek, kullanici izin
  // kutusuyla ugrasirken Gemini oturumu bos calisir ve para yakar.
  if (!YEREL) {
    if (!GUVENLI) {
      gucNot('bu adreste mikrofon kapalı · https gerekiyor', true);
      return;
    }
    gucNot('mikrofon izni…');
    try {
      await tarayiciSes.ac();
    } catch (e) {
      gucNot('mikrofon izni verilmedi', true);
      return;
    }
    tarayiciSes.hiz = D.hiz;
    gonder({ tip: 'ses_kanali', ac: true });
  }

  gucNot('bağlanıyor…');
  gonder({ tip: 'baslat', mod: YEREL ? 'ses' : 'tarayici' });
});

// ---------------------------------------------------------------------------
// Durum satiri
// ---------------------------------------------------------------------------

function durumYaz() {
  const s = $('#durumYazi');
  if (!D.canli) {
    s.textContent = 'önizleme';
    s.className = '';
  } else if (!D.calisiyor) {
    s.textContent = 'hazır';
    s.className = 'iyi';
  } else if (D.uyuyor) {
    // Uykuda soket kapali: ses gitmiyor, ucret islemiyor. Konusunca
    // ~0,6 saniyede uyaniyor, o yuzden bu bir ariza degil.
    s.textContent = 'uyuyor · konuşunca uyanır';
    s.className = 'iyi';
  } else {
    s.textContent = D.konusuyor ? 'konuşuyor' : 'dinliyor';
    s.className = 'iyi';
  }
  $('#dWifi').textContent = D.wifi.ad;
}

// ---------------------------------------------------------------------------
// Ses
// ---------------------------------------------------------------------------

{
  const k = $('#kSeviye');
  k.step = 0.05;

  const gonderSeviye = geciktir(250, () => {
    if (!gonder({ tip: 'ses_seviyesi', deger: D.seviye })) return;
    tost('Ses seviyesi ✓');
  });

  k.addEventListener('input', () => {
    D.seviye = parseFloat(k.value);
    $('#vSeviye').textContent = Math.round(D.seviye * 100) + '%';
    gonderSeviye();
  });
}

function seviyeYaz() {
  const k = $('#kSeviye');
  k.min = D.seviyeEnAz;
  k.max = D.seviyeEnFazla;
  k.value = D.seviye;
  $('#vSeviye').textContent = Math.round(D.seviye * 100) + '%';
}

{
  const k = $('#kHiz');
  k.min = SINIR.hizEnAz;
  k.max = SINIR.hizEnFazla;
  k.step = 0.01;

  const gonderHiz = geciktir(250, () => {
    if (!gonder({ tip: 'ses_ayarla', ses: D.sesAdi, hiz: D.hiz })) return;
    tost(D.konusuyor ? 'Tizlik ✓ · cümlesini bitirince' : 'Tizlik ✓');
  });

  k.addEventListener('input', () => {
    D.hiz = parseFloat(k.value);
    $('#vHiz').textContent = D.hiz.toFixed(2) + '×';
    // Tarayici modunda tizligi tarayici uyguluyor; hemen bildir ki
    // "Dinle" dogru hizda calsin.
    tarayiciSes.hiz = D.hiz;
    gonderHiz();
  });
}

function seslerYaz() {
  const s = $('#sSes');
  s.innerHTML = '';
  for (const v of D.sesler) {
    const o = document.createElement('option');
    o.value = v.ad;
    o.textContent = v.tanim ? `${v.ad} — ${v.tanim}` : v.ad;
    s.appendChild(o);
  }
  s.value = D.sesAdi;
  $('#kHiz').value = D.hiz;
  $('#vHiz').textContent = D.hiz.toFixed(2) + '×';
}

$('#sSes').addEventListener('change', (e) => {
  D.sesAdi = e.target.value;
  if (gonder({ tip: 'ses_ayarla', ses: D.sesAdi, hiz: D.hiz })) {
    tost(D.konusuyor ? `Ses: ${D.sesAdi} · cümlesini bitirince`
                     : `Ses: ${D.sesAdi} ✓`);
  }
});

// "Dinle": Pati kendi sesiyle tek cumle soyluyor. Uzaktan baglaniyorsa
// ses TARAYICIYA geliyor (pati.py _OnizlemeTarayiciHoparlor) — yoksa
// ayarladigin sesi duyamazdin.
$('#dene').addEventListener('click', async () => {
  if (!D.canli) { tost('Pati çalışmıyor', true); return; }
  if (!YEREL && !tarayiciSes.acik) {
    if (!GUVENLI) { tost('https gerekiyor · PAYLAS.bat', true); return; }
    try { await tarayiciSes.ac(); } catch { tost('mikrofon izni gerekli', true); return; }
    gonder({ tip: 'ses_kanali', ac: true });
  }
  tarayiciSes.hiz = D.hiz;
  gonder({ tip: 'ses_dene', ses: D.sesAdi, hiz: D.hiz });
});

// ---------------------------------------------------------------------------
// Konusma ayarlari
// ---------------------------------------------------------------------------
//
// Ucu de setup mesajinda gidiyor, yani calisan oturumda degismiyorlar.
// Ama yeniden baglanma cozulmus bir is (GoAway, 568 ms, hafiza
// korunuyor) — Python tur arasinda kendiliginden uyguluyor.
//
// SOZ KESME'nin tarayici tarafi ayri: mikrofon.js Pati konusurken
// mikrofonu gondermeyi kesiyor. O yuzden hem sunucuya haber veriyoruz
// hem yerel bayragi ceviriyoruz.

function konusmaYaz() {
  $('#sozKesme').checked = D.sozKesme;
  $('#vadSecim').value = D.vad;
  $('#yuzAcik').checked = D.yuz;
  // Tarayici tarafi: soz kesme KAPALIYSA yarim dupleks ACIK.
  tarayiciSes.yarimDubleks = !D.sozKesme;
}

$('#sozKesme').addEventListener('change', (e) => {
  D.sozKesme = e.target.checked;
  tarayiciSes.yarimDubleks = !D.sozKesme;
  if (gonder({ tip: 'soz_kesme', acik: D.sozKesme })) {
    tost(D.sozKesme ? 'Söz kesme açık · kulaklık tak' : 'Söz kesme kapalı');
  }
});

function gecikmeGonder(yaziMetni) {
  if (gonder({ tip: 'gecikme_ayar', vad: D.vad, yuz: D.yuz })) {
    tost(D.calisiyor ? `${yaziMetni} ✓ · cümlesini bitirince`
                     : `${yaziMetni} ✓`);
  }
}

$('#vadSecim').addEventListener('change', (e) => {
  D.vad = e.target.value;
  gecikmeGonder('Cevap hızı');
});

$('#yuzAcik').addEventListener('change', (e) => {
  D.yuz = e.target.checked;
  gecikmeGonder(D.yuz ? 'Gözler açık' : 'Gözler kapalı');
});

// ---------------------------------------------------------------------------
// Uyku + kota
// ---------------------------------------------------------------------------

{
  const k = $('#kUyku');
  const gonderUyku = geciktir(250, () => {
    if (gonder({ tip: 'uyku', dakika: D.uyku })) tost('Uyku süresi ✓');
  });
  k.addEventListener('input', () => {
    D.uyku = parseFloat(k.value);
    $('#vUyku').textContent = uykuYazisi(D.uyku);
    kotaYaz();
    gonderUyku();
  });
}

// "1,5 dk" — Turkce ondalik virgul. Tam sayida virgul gosterilmiyor.
function uykuYazisi(dk) {
  return (Number.isInteger(dk) ? dk : dk.toFixed(1).replace('.', ','))
         + ' dk';
}

function uykuYaz() {
  $('#kUyku').value = D.uyku;
  $('#vUyku').textContent = uykuYazisi(D.uyku);
  kotaYaz();
}

function kotaYaz() {
  $('#dBugun').textContent = `${Math.round(D.kota.bugunDk)} dk`;
  $('#dAy').textContent = `${Math.round(D.kota.ayDk)} dk`;
  $('#kotaBilgi').innerHTML =
    `Bu ay <b>${Math.round(D.kota.ayDk)} dakika</b> · tahmini ` +
    `<b>${(D.kota.tahminUsd || 0).toFixed(2)} $</b>. ` +
    `Uyurken ücret işlemez.`;
}

// ---------------------------------------------------------------------------
// Wifi
// ---------------------------------------------------------------------------
//
// ⚠ Ag listesi SIMDILIK ORNEK. Bu sayfa bilgisayarda calisirken
//   yonetecek bir wifi yongasi yok; robot gelince ayni akis gercek
//   taramaya baglanacak. Akisin kendisi (liste -> sifre -> baglandi)
//   incelenebilir durumda, cunku degerlendirilecek olan o.

const cubuk = (g) => '▁▃▅▇'.slice(0, Math.max(1, g)) || '▁';

function wifiYaz() {
  $('#wAd').textContent = D.wifi.ad;
  const s = $('#wGuc');
  s.textContent = D.bagli
    ? `sinyal ${cubuk(D.wifi.guc)}`
    : (D.kurulum
        // Kurulum modunda ebeveyn ZATEN Pati'nin agina bagli ve tam
        // bunu yapmaya gelmis; "Değiştir"e yonlendirmek dogru is.
        ? 'Pati kendi ağını açtı · aşağıdan ev wifi’sini seç'
        : 'ağa bağlı değil · “Değiştir”e dokun');
  s.classList.toggle('dikkat', !D.bagli);
}

let secilenAg = null;

async function wifiPaneliAc() {
  const p = $('#wPanel');
  p.hidden = false;
  $('#wSifreKutu').hidden = true;
  const liste = $('#wListe');
  liste.innerHTML = '<div class="bos-yazi">Ağlar aranıyor…</div>';

  // Robotta GERCEK tarama; tezgahta ornek liste (bilgisayarda
  // yonetilecek wifi yongasi yok).
  let aglar;
  if (ROBOT) {
    try {
      const c = await fetch('/api/aglar', { cache: 'no-store' });
      aglar = c.ok ? await c.json() : [];
    } catch {
      aglar = [];
    }
    if (!aglar.length) {
      liste.innerHTML = '<div class="bos-yazi">Ağ bulunamadı. ' +
                        'Pati’yi routera yaklaştırıp tekrar dene.</div>';
      return;
    }
  } else {
    aglar = await tara();
  }
  liste.innerHTML = '';
  for (const a of aglar) {
    const d = document.createElement('div');
    d.className = 'oge tiklanir';
    d.innerHTML = `<span class="guc-cubuk">${cubuk(a.guc)}</span>` +
                  `<span class="metin"></span>` +
                  `<span class="kilit">${a.kilit ? '🔒' : ''}</span>`;
    d.querySelector('.metin').textContent = a.ad;
    d.addEventListener('click', () => {
      secilenAg = a;
      $('#wSecilen').textContent = `${a.ad} şifresi`;
      $('#wSifreKutu').hidden = false;
      $('#wSifre').value = '';
      $('#wSifre').focus();
    });
    liste.appendChild(d);
  }
}

$('#wDegistir').addEventListener('click', async () => {
  const p = $('#wPanel');
  if (!p.hidden) { p.hidden = true; return; }
  await wifiPaneliAc();
});

$('#wGoster').addEventListener('change', (e) => {
  $('#wSifre').type = e.target.checked ? 'text' : 'password';
});

// Kurulum bitince YENI ADRESI kalici olarak gosterir.
//
// 🔴 31.07.2026, tezgahta yasandi: ebeveyn ev wifi'sini girdi, Pati
// kendi agini kapatti ve panel bir daha ACILAMADI. Adres artik
// 192.168.4.1 degil, routerin verdigi bir numara — ve bunu kimse
// bilmiyor. "Ev wifi'sine bağlan" demek yetmiyor, NEREYE gidilecegi
// yazmali.
//
// Tost yeterli degil: birkac saniyede kayboluyor ve tam o sirada
// telefon ag degistiriyor. Adres ekranda KALMALI.
function adresiYaz(ip) {
  if ($('#yeniAdres')) return;

  // 192.168.4.1 Pati'nin KENDI agindaki adresi. Cevap, baglanti
  // tamamlanmadan donmus olabilir; o durumda eski deger geliyor ve
  // yazmak yanlis yonlendirme olur.
  const gercek = ip && ip !== '192.168.4.1' && ip !== '0.0.0.0' ? ip : null;

  const k = document.createElement('div');
  k.id = 'yeniAdres';
  k.className = 'yeni-adres';

  const baslik = document.createElement('b');
  baslik.textContent = 'Pati ev ağına geçti ✓';
  const p1 = document.createElement('p');
  p1.textContent = 'Telefonunu ev wifi’sine bağla, sonra tarayıcıya yaz:';
  const adres = document.createElement('p');
  adres.className = 'adres';
  adres.textContent = 'pati.local';
  k.append(baslik, p1, adres);

  if (gercek) {
    const p2 = document.createElement('p');
    p2.className = 'ip';
    p2.textContent = `açılmazsa: ${gercek}`;
    k.append(p2);
  }
  const p3 = document.createElement('p');
  p3.className = 'ip';
  p3.textContent = 'Ayarlara bundan sonra bu adresten girilecek — not al.';
  k.append(p3);

  document.body.prepend(k);
  k.scrollIntoView({ block: 'start' });
}

$('#wBagla').addEventListener('click', async () => {
  if (!secilenAg) return;
  const sifre = $('#wSifre').value;
  if (secilenAg.kilit && !sifre) { tost('Şifre boş', true); return; }

  if (!ROBOT) {
    // Tezgah: yonetilecek wifi yongasi yok, akisi gostermek icin.
    D.wifi = { ad: secilenAg.ad, guc: secilenAg.guc };
    D.bagli = true;
    wifiYaz();
    durumYaz();
    $('#wPanel').hidden = true;
    tost(`${secilenAg.ad} · bağlandı ✓`);
    return;
  }

  const dugme = $('#wBagla');
  dugme.disabled = true;
  dugme.textContent = 'Bağlanıyor…';
  try {
    const c = await fetch('/api/wifi', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ad: secilenAg.ad, sifre }),
    });
    const h = await c.json().catch(() => ({}));
    if (c.ok) {
      $('#wPanel').hidden = true;
      tost(`${secilenAg.ad} · bağlandı ✓`);
      // Kurulum modundaysak Pati'nin agi kapaniyor ve telefon ev
      // agina donuyor; sayfa artik ulasilamaz. Bunu SOYLEMEK sart,
      // yoksa "sayfa dondu" saniiliyor.
      if (D.kurulum) {
        // 🔴 ADRESI SOYLEMEK SART. Once sadece "ev wifi'sine bağlan"
        // yaziyordu ve panel bir daha ACILAMIYORDU: Pati'nin adresi
        // artik 192.168.4.1 degil, routerin verdigi bir numara.
        // 31.07.2026'da tezgahta tam bunu yasadik.
        adresiYaz(h.ip);
      }
      robot_yokla();
    } else {
      // Sebebi firmware soyluyor: sifre yanlis mi, ag menzil disi mi.
      tost(h.hata || 'bağlanılamadı', true);
    }
  } catch {
    // Kurulum modunda BASARILI baglanti da bu dala dusebiliyor: AP
    // kapaninca istek yarida kaliyor. O yuzden hata degil bilgi.
    tost('Pati ağı kapandı — ev wifi’sine geç ve sayfayı yenile');
  } finally {
    dugme.disabled = false;
    dugme.textContent = 'Bağlan';
  }
});

// ---------------------------------------------------------------------------
// Gemini anahtari
// ---------------------------------------------------------------------------
//
// Pati konusmak icin Google'in servisini kullaniyor; anahtar faturanin
// kime yazilacagini soyluyor. Anahtar ROBOTTA duruyor, firmware'in
// icinde degil — yoksa guncelleme dosyasiyla birlikte herkese acilirdi
// (firmware/main/pati_anahtar.hpp).
//
// 🔴 ANAHTARIN KENDISI HIC GERI GELMIYOR. Firmware yalnizca son dort
// karakteri gonderiyor. Panel ev agindaki herkese acik ve anahtar
// kutuda dolu dursaydi aga giren biri onu okurdu. Bu yuzden kutu her
// zaman BOS aciliyor: gorunen sey "yazili olan" degil, "yazilacak olan".

// Durumdan anneye gorunen hal. Uc sey birden lazim: BASLIK (ne oldu),
// YAZI (ne yapmali) ve AGIRLIK (kirmizi mi sari mi).
//
// "Pati konusmuyor" tek basina bir ise yaramiyor; yapilacak sey sebebe
// gore degisiyor ve metin onu SOYLEMEK zorunda:
//
//   gecersiz  yeni anahtar yazacak
//   kota      Google'a para yukleyecek (ya da baska anahtar)
//   yok       ilk kurulum, anahtar hic girilmemis
//   ulasilamadi  HICBIR SEY yapmayacak — ag sorunu, kendi duzelir
//
// Sonuncusu ayri tutulmasaydi en pahali yanlisa yol acardi: wifi
// koptugu icin susan robota bakip Google'a para yuklemek.
const ANAHTAR_HALI = {
  yok: {
    agir: true,
    rozet: 'girilmedi',
    baslik: 'Pati’nin Gemini anahtarı gerekiyor',
    yazi: 'Pati konuşmak için Google’ın servisini kullanıyor. '
        + 'aistudio.google.com/apikey adresinden bir anahtar alıp '
        + 'aşağıya yapıştırın. Bir kez yapılıyor.',
  },
  gecersiz: {
    agir: true,
    rozet: 'geçersiz',
    baslik: 'Google anahtarı kabul etmiyor',
    yazi: 'Anahtar yanlış, iptal edilmiş ya da süresi geçmiş olabilir. '
        + 'aistudio.google.com/apikey adresinden yeni bir tane alıp '
        + 'yazın. Pati o zamana kadar konuşamaz.',
  },
  kota: {
    agir: false,
    rozet: 'kota doldu',
    baslik: 'Google kotası dolmuş görünüyor',
    yazi: 'Anahtar çalışıyor ama Google şu an istek kabul etmiyor: '
        + 'ücretsiz kota bitmiş ya da hesapta bakiye kalmamış olabilir. '
        + 'Google hesabına bakiye yükleyin, ya da başka bir anahtar yazın. '
        + 'Geçici bir hız sınırıysa kendiliğinden düzelir.',
  },
  ulasilamadi: {
    agir: false,
    rozet: 'denenemedi',
    baslik: 'Google’a ulaşılamıyor',
    yazi: 'Anahtarın durumu şu an öğrenilemiyor — internet bağlantısı '
        + 'kopmuş olabilir. Bu anahtarla ilgili değil; bağlantı '
        + 'gelince kendiliğinden düzelir.',
  },
};

function anahtarYaz() {
  const a = D.anahtar;
  const hal = ANAHTAR_HALI[a.durum];

  // --- kartin kendi satiri
  if (a.durum === 'gecerli') {
    $('#aDurum').textContent = 'Anahtar çalışıyor';
    $('#aNot').textContent = a.kuyruk ? `…${a.kuyruk} ile biten` : '';
  } else if (a.durum === 'bilinmiyor') {
    $('#aDurum').textContent = a.var ? 'Anahtar yazılı' : 'Anahtar yok';
    $('#aNot').textContent = a.var ? 'henüz denenmedi' : '';
  } else if (hal) {
    $('#aDurum').textContent = 'Anahtar ' + hal.rozet;
    // Google'in kendi cumlesi (Ingilizce). Anneye yazilmis bir metin
    // degil ama sorun beklenmedik bir seyse tek ipucu bu — ve gizlemek,
    // sebebi bilinen bir hatayi bilinmez yapmak olurdu.
    $('#aNot').textContent = a.ayrinti || '';
  }
  $('#aDurum').className = (a.durum === 'gecerli') ? 'iyi' : '';

  // --- ustteki uyari bandi
  const u = $('#anahtarUyari');
  if (!hal) {
    u.hidden = true;
    return;
  }
  u.hidden = false;
  u.className = 'uyari' + (hal.agir ? ' agir' : '');
  $('#anahtarUyariBaslik').textContent = hal.baslik;
  $('#anahtarUyariYazi').textContent = hal.yazi;
}

function anahtarPaneliAc() {
  $('#aPanel').hidden = false;
  $('#aGiris').value = '';
}

$('#aDegistir').addEventListener('click', () => {
  const p = $('#aPanel');
  p.hidden = !p.hidden;
  if (!p.hidden) $('#aGiris').value = '';
});

$('#anahtarUyariDugme').addEventListener('click', () => {
  anahtarPaneliAc();
  $('#anahtarKart').scrollIntoView({ behavior: 'smooth', block: 'center' });
});

$('#aGoster').addEventListener('change', (e) => {
  $('#aGiris').type = e.target.checked ? 'text' : 'password';
});

$('#aKaydet').addEventListener('click', () => {
  const deger = $('#aGiris').value.trim();
  if (!deger) { tost('Önce anahtarı yapıştırın', true); return; }

  // Iyimser gosterim: durum HEMEN "deneniyor"a geciyor. Firmware
  // sinamayi arka planda yapiyor ve sonuc iki saniyelik yoklamayla
  // geliyor; o ana kadar hicbir sey degismezse anne dugmenin calisip
  // calismadigini bilemez.
  D.anahtar = { ...D.anahtar, var: true, durum: 'bilinmiyor', ayrinti: '' };
  anahtarYaz();
  $('#aGiris').value = '';
  $('#aPanel').hidden = true;

  if (gonder({ tip: 'anahtar_yaz', deger })) {
    tost('Anahtar kaydedildi · deneniyor');
  }
});

// ---------------------------------------------------------------------------
// Guncelleme
// ---------------------------------------------------------------------------
//
// Pati kendini yeniliyor: yeni surum GitHub'da duruyor, Pati oradan
// indirip yaziyor ve yeniden basliyor. Bilgisayar, kablo, program
// yuklemek yok.
//
// Panel burada SADECE iki dugme: "kontrol et" ve "guncelle". Isin
// tamami firmware'de (pati_guncelleme.cpp) ve durum /api/durum
// yoklamasindan geliyor — ayri bir baglanti acilmiyor.

const GUNCELLEME_YAZI = {
  bos: '',
  bakiliyor: 'bakılıyor…',
  guncel: 'en son sürüm',
  var: 'yeni sürüm var',
  iniyor: 'indiriliyor…',
  bitti: 'yeniden başlıyor…',
};

function guncellemeYaz() {
  const g = D.guncelleme;

  $('#gSurum').textContent = g.suAnki ? `Sürüm ${g.suAnki}` : '—';
  $('#gNot').textContent = g.hata || GUNCELLEME_YAZI[g.durum] || '';
  $('#gNot').className = g.hata ? 'dikkat' : '';

  // "Yeni sürüm var" kutusu: sürüm numarası + Mert'in yazdığı not.
  const yeniVar = g.durum === 'var';
  $('#gYeni').hidden = !yeniVar;
  if (yeniVar) {
    $('#gNotlar').textContent = g.notlar
      ? `${g.yeni} · ${g.notlar}`
      : `${g.yeni} sürümüne güncellenecek.`;
  }

  // Ilerleme cubugu. "bitti"de de duruyor ve %100 gosteriyor: cihaz
  // yeniden baslarken cubugun kaybolmasi, isin yarida kaldigi
  // izlenimini verirdi.
  const iniyor = g.durum === 'iniyor' || g.durum === 'bitti';
  $('#gIlerleme').hidden = !iniyor;
  if (iniyor) {
    $('#gCubuk').style.width = `${g.yuzde || 0}%`;
    $('#gYuzde').textContent = g.durum === 'bitti'
      ? 'Yazıldı · Pati yeniden başlıyor. Bu sayfa birazdan kendiliğinden '
        + 'geri gelir.'
      : `%${g.yuzde || 0} indi · Pati’nin fişini çekmeyin.`;
  }

  // Indirme sirasinda iki dugme de kilitli: ikinci bir indirme ayni
  // bolume yazmaya kalkardi. Firmware bunu ayrica engelliyor ama
  // basilabilen bir dugmenin hicbir sey yapmamasi da yanlis.
  const mesgul = g.durum === 'bakiliyor' || iniyor;
  $('#gBak').disabled = mesgul;
  $('#gKur').disabled = mesgul;
  $('#gBak').textContent = g.durum === 'bakiliyor' ? 'bakılıyor…' : 'Kontrol et';
}

$('#gBak').addEventListener('click', () => {
  D.guncelleme = { ...D.guncelleme, durum: 'bakiliyor', hata: '' };
  guncellemeYaz();
  gonder({ tip: 'guncelleme_bak' });
});

$('#gKur').addEventListener('click', () => {
  if (!confirm('Pati güncellensin mi?\n\nBir dakika kadar susar ve '
             + 'yeniden başlar. Hafızası ve ayarları korunur.')) return;
  D.guncelleme = { ...D.guncelleme, durum: 'iniyor', yuzde: 0, hata: '' };
  guncellemeYaz();
  gonder({ tip: 'guncelleme_kur' });
});

// ---------------------------------------------------------------------------
// Cocuk bilgisi
// ---------------------------------------------------------------------------

function cocukYaz() {
  // Kullanici o anda yaziyorsa ustune YAZMA — sunucudan gelen tazeleme
  // yarim kalmis cumleyi silerdi.
  // Iki koruma: kullanici o alanda yaziyorsa DOKUNMA, ve deger zaten
  // ayniysa yine DOKUNMA. Ikincisi onemli — degismeyen bir kutuya
  // her iki saniyede bir deger atamak bazi tarayicilarda imleci
  // oynatiyor ve alani gorunur tutmak icin sayfayi kaydiriyor.
  const yaz = (sec, deger) => {
    const e = $(sec);
    if (document.activeElement === e) return;
    const y = deger === 0 ? '0' : String(deger || '');
    if (e.value !== y) e.value = y;
  };
  yaz('#cAd', D.cocuk.ad);
  yaz('#cYas', D.cocuk.yas);
  yaz('#cEk', D.ekBilgi);

  const varsayilan = D.robotAdiVarsayilan || 'Pati';
  const ad = D.robotAdi || varsayilan;
  yaz('#rAd', ad);

  // Ad sayfanin USTUNDE de yaziyor — kimligin gorundugu yer orasi.
  // Sabit kalirsa ad degistirildiginde panel kendi kendini yalanliyor.
  // Sekme basligi da ayni: telefonda yer imine eklenince dogru ad
  // gorunsun.
  if ($('#ustAd').textContent !== ad) $('#ustAd').textContent = ad;
  if (document.title !== ad) document.title = ad;

  // ⚠ Adin GECTIGI cumle kurulmuyor. Turkcede ek ada gore degisiyor:
  // "Pati'nin", "Ömer'in", "Şükrü'nün". Ad serbest metin oldugu icin
  // dogru eki secmek mumkun degil; cumleler adsiz yaziliyor.
  $('#rAdNot').textContent =
    ad === varsayilan
      ? 'Konuşma sırasında da değiştirilebilir: “bundan sonra senin '
        + 'adın …”. Buradan da yazılabilir; son yazılan geçerli olur.'
      : 'Konuşma sırasında yeniden değiştirilebilir. '
        + `Boş bırakılırsa ${varsayilan} olur.`;
}

// "change" kullaniliyor, "input" degil: her harfte kayit gondermek hem
// gereksiz hem flash omrunu tuketiyor.
// Robotun adi — IKI TARAF DA yazabiliyor.
//
// Cocuk sohbette ("bundan sonra senin adin Omer"), ebeveyn de buradan.
// Ayri bir oncelik kurali YOK: son yazan gecerli. Ikisi de mesru, biri
// oyunun icinde biri dusunerek.
//
// Bos birakmak HATA DEGIL, "varsayilana don" demek. Ad hicbir zaman bos
// kalmiyor; firmware bos gorunce PATI_ROBOT_ADI'ni kullaniyor.
//
// Reddedilirse (rakam iceriyor, tek harf, 40 karakterden uzun) iki
// saniyelik yoklama alani robotun gercek adiyla geri dolduruyor —
// kullanici ne oldugunu ekranda goruyor.
$('#rAd').addEventListener('change', (e) => {
  const ad = e.target.value.trim();
  D.robotAdi = ad;
  if (gonder({ tip: 'robot_adi', ad })) {
    tost(ad ? `Adı “${ad}” · sonraki konuşmada`
            : `${D.robotAdiVarsayilan || 'Pati'}’ye döndü`);
  }
});

$('#cAd').addEventListener('change', (e) => {
  D.cocuk.ad = e.target.value.trim();
  if (gonder({ tip: 'cocuk_ayarla', ad: D.cocuk.ad, yas: D.cocuk.yas }))
    tost('Ad ✓ · sonraki konuşmada');
});
$('#cYas').addEventListener('change', (e) => {
  D.cocuk.yas = parseInt(e.target.value, 10) || '';
  if (gonder({ tip: 'cocuk_ayarla', ad: D.cocuk.ad, yas: D.cocuk.yas }))
    tost('Yaş ✓ · sonraki konuşmada');
});
$('#cEk').addEventListener('change', (e) => {
  D.ekBilgi = e.target.value;
  if (gonder({ tip: 'ebeveyn_notu', yazi: D.ekBilgi }))
    tost('Ek bilgi ✓ · sonraki konuşmada');
});

// ---------------------------------------------------------------------------
// Hafiza
// ---------------------------------------------------------------------------

// Liste DEGISMEDIYSE DOM'a hic dokunma.
//
// Eskiden her yoklamada (iki saniyede bir) butun liste silinip yeniden
// kuruluyordu. Icerik ayni olsa bile bu, sayfanin yuksekliginin bir an
// icin degismesi demek — telefonda okudugun yer kayiyor, odakli bir
// alan varsa tarayici seni oraya geri kaydiriyor.
let hafiza_imza = null;

function hafizaYaz() {
  const imza = JSON.stringify(D.hafiza);
  if (imza === hafiza_imza) return;
  hafiza_imza = imza;

  const l = $('#hListe');
  l.innerHTML = '';
  if (!D.hafiza.length) {
    l.innerHTML = '<div class="bos-yazi">Henüz bir şey hatırlamıyor.</div>';
    return;
  }
  D.hafiza.forEach((h, i) => {
    const d = document.createElement('div');
    d.className = 'oge';
    // textContent kullaniliyor: hafiza metni MODELDEN geliyor, icinde
    // ne olacagi bilinmiyor. innerHTML ile basmak sayfayi kirabilir.
    d.innerHTML = `<span class="metin"></span>` +
                  `<span class="kez">${h.kez}×</span>` +
                  `<button class="sil" title="Sil">×</button>`;
    d.querySelector('.metin').textContent = h.metin;
    d.querySelector('.sil').addEventListener('click', () => {
      D.hafiza.splice(i, 1);
      hafizaYaz();
      if (h.id != null) gonder({ tip: 'bilgi_sil', id: h.id });
      tost('Silindi ✓');
    });
    l.appendChild(d);
  });
}

$('#hepsiniSil').addEventListener('click', () => {
  // Geri alinamaz: robot cocugu tanimayi birakacak.
  if (!confirm('Pati bildiği her şeyi unutsun mu?\n\nGeri alınamaz.'))
    return;
  D.hafiza = [];
  hafizaYaz();
  gonder({ tip: 'hafiza_sifirla' });
  tost('Hafıza silindi');
});

$('#fabrika').addEventListener('click', async () => {
  if (!confirm('Wifi, hafıza ve ayarların tümü silinsin mi?')) return;
  if (!ROBOT) {
    gonder({ tip: 'hafiza_sifirla' });
    tost('Fabrika ayarlarına dönüldü');
    return;
  }
  try {
    await fetch('/api/fabrika', { method: 'POST' });
  } catch {
    // Robot yeniden basliyor; istek yarida kalabilir. Beklenen.
  }
  tost('Pati sıfırlandı ve yeniden başlıyor');
});

// ---------------------------------------------------------------------------
// Ilk cizim
// ---------------------------------------------------------------------------

seviyeYaz();
seslerYaz();
konusmaYaz();
uykuYaz();
wifiYaz();
anahtarYaz();
guncellemeYaz();
cocukYaz();
hafizaYaz();
durumYaz();
gucYaz();
seritGuncelle();

// ---------------------------------------------------------------------------
// Hangi taraftayiz?
// ---------------------------------------------------------------------------
//
// Once robota soruyoruz. Cevap gelirse REST modunda calisiyoruz ve
// WebSocket HIC denenmiyor — ESP32'de WS yok, denemek her 1,5 saniyede
// bir basarisiz baglanti demek olurdu.

(async () => {
  try {
    const c = await fetch('/api/durum', { cache: 'no-store' });
    if (c.ok) {
      const d = await c.json();
      if (d && d.robot) {
        ROBOT = true;
        // Guc dugmesi gizleniyor: robot fis takiliyken calisiyor,
        // basilacak bir sey yok.
        $('#guc').hidden = true;
        $('#gucNot').hidden = true;
        D.canli = true;
        durumu_uygula(d);
        seritGuncelle();
        // KURULUM MODUNDA wifi panelini KENDILIGINDEN ac.
        //
        // 🔴 31.07.2026, gercek cihazda gorulen sorun: ebeveyn Pati'nin
        // agina baglaniyor, sayfa aciliyor ve panelde "wifi'ye baglan"
        // diye bir sey GORUNMUYOR. Aslinda var ama "Değiştir"
        // dugmesinin arkasinda. Ebeveyn buraya tek bir is icin geldi;
        // o isi saklamak dogru degil.
        if (D.kurulum) {
          wifiPaneliAc();
          $('#wPanel').scrollIntoView({ behavior: 'smooth', block: 'center' });
        } else if (D.anahtar.durum === 'yok') {
          // ANAHTAR HIC GIRILMEMIS: kutuyu kendiliginden ac.
          //
          // Wifi kurulumundaki gerekcenin aynisi. Anne buraya tek bir is
          // icin gelmis — Pati konusmuyor — ve o isi "Değiştir"
          // dugmesinin arkasinda saklamak dogru degil. Wifi kurulumu
          // varsa ONCE o bitiyor: anahtari sinamak icin zaten internet
          // gerekiyor.
          anahtarPaneliAc();
          $('#anahtarKart').scrollIntoView({ behavior: 'smooth',
                                            block: 'center' });
        }
        // 2 saniye: ifade aynasi akici gorunsun ama ESP32'yi de
        // gereksiz mesgul etmesin. Sohbet gorevinin onunde degil
        // (panel gorevi daha dusuk oncelikte).
        setInterval(robot_yokla, 2000);
        return;
      }
    }
  } catch {
    // Robot degil: tezgah modu.
  }
  bagla();
})();
