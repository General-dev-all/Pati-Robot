// -*- coding: utf-8 -*-
//
// Tasarim inceleme studyosu — kaydiricilar, sahne, butce, C++ ciktisi.
//
// Bu dosya ESP32'ye HIC gecmeyecek. Amaci tek: begenilecek seyi
// begenilmeden once gostermek ve begenilen sayilari C++'a tasinabilir
// halde disari vermek.

import { Gozler240, DURUMLAR, AYAR, EKRAN, butce } from './gozler240.js';
import * as Govde from './govde.js';

const $ = (s) => document.querySelector(s);
const $$ = (s) => Array.from(document.querySelectorAll(s));

const gozler = new Gozler240($('#gozler'));

// ---------------------------------------------------------------------------
// Olcek
// ---------------------------------------------------------------------------
//
// Tarayici gercek DPI vermiyor: CSS'te 1 inc her zaman 96 piksel. Yani
// varsayilan 96/25.4 = 3,7795 px/mm sadece 96 DPI ekranda dogru.
// "1:1 gercek boyut" iddiasinda bulunacaksak olcegi OLCMEK gerekiyor —
// kalibrasyon penceresi bunu yapiyor ve sonucu saklıyor.
const VARSAYILAN_PXMM = 96 / 25.4;

let pxmm = parseFloat(localStorage.getItem('pati.pxmm')) || VARSAYILAN_PXMM;
let zum = 1;
let sadeceEkran = false;
let akrilik = true;
let icGoster = false;

// ---------------------------------------------------------------------------
// Yerlesim
// ---------------------------------------------------------------------------

function yerlestir() {
  const olcek = pxmm * zum;
  const gv = $('#govde');
  const gz = $('#gozler');
  const ort = $('#akrilikOrtu');

  if (sadeceEkran) {
    gv.hidden = true;
    const boy = EKRAN.mm * olcek;
    gz.style.position = 'static';
    gz.style.width = boy + 'px';
    gz.style.height = boy + 'px';
    ort.style.left = '0px';
    ort.style.top = '0px';
    ort.style.width = boy + 'px';
    ort.style.height = boy + 'px';
    ort.hidden = !akrilik;
  } else {
    gv.hidden = false;
    const r = Govde.ciz(gv, olcek, { icGoster, akrilik });
    gz.style.position = 'absolute';
    gz.style.left = (r.ekran.x * olcek) + 'px';
    gz.style.top = (r.ekran.y * olcek) + 'px';
    gz.style.width = (r.ekran.boy * olcek) + 'px';
    gz.style.height = (r.ekran.boy * olcek) + 'px';
    ort.style.left = (r.pencere.x * olcek) + 'px';
    ort.style.top = (r.pencere.y * olcek) + 'px';
    ort.style.width = (r.pencere.boy * olcek) + 'px';
    ort.style.height = (r.pencere.boy * olcek) + 'px';
    ort.hidden = !akrilik;
  }

  // PIKSEL GERCEKCILIGI — dogru yonde uygulanmasi gerekiyor.
  //
  // `pixelated` sadece BUYUTURKEN dogru: 240 piksel ekranda 240'tan
  // fazla piksele yayilinca gercek basamaklar goruluyor.
  //
  // KUCULTURKEN ise yanlis: 1:1 modda 240 piksel ~88 ekran pikseline
  // sigiyor. Nearest-neighbor orada piksel ATIYOR — yani gercek
  // ekrandan DAHA KOTU gorunuyor. Gercek ekranda o 240 piksel fiziksel
  // olarak orada duruyor ve goz onlari harmanliyor; yumusak kucultme
  // buna daha yakin.
  const oran = (EKRAN.mm * olcek * (window.devicePixelRatio || 1)) / EKRAN.g;
  gz.style.imageRendering = oran >= 1 ? 'pixelated' : 'auto';

  // Cetvel: 10 mm. 1:1 modda cetveli kumpasla olcunce 10 mm cikmali —
  // kalibrasyonun ise yarayip yaramadigi tek kanit bu.
  $('#cetvelCizgi').style.width = (10 * olcek) + 'px';
  $('#cetvelYazi').textContent =
    `10 mm  ·  ${olcek.toFixed(2)} px/mm  ·  ekran ${EKRAN.mm} mm  ·  ` +
    (oran >= 1
      ? `${oran.toFixed(1)}× büyütme — gerçek pikseller görünüyor`
      : `${(1 / oran).toFixed(1)}× küçültme — boyut doğru, piksel değil`);
}

// ---------------------------------------------------------------------------
// Kaydirici / onay kutusu uretimi
// ---------------------------------------------------------------------------

function kaydirici(kap, nesne, ad, etiket, en, ez, adim, birim, sonra) {
  const l = document.createElement('label');
  l.className = 'kd';
  l.innerHTML =
    `<span class="kd-ust"><span>${etiket}</span><b></b></span>` +
    `<input type="range" min="${en}" max="${ez}" step="${adim}">`;
  const g = l.querySelector('input');
  const b = l.querySelector('b');
  const yaz = () => { b.textContent = nesne[ad] + (birim ? ' ' + birim : ''); };
  g.value = nesne[ad];
  yaz();
  g.addEventListener('input', () => {
    nesne[ad] = parseFloat(g.value);
    yaz();
    if (sonra) sonra();
    ciktiYaz();
  });
  l._sifirla = () => { g.value = nesne[ad]; yaz(); };
  kap.appendChild(l);
  return l;
}

function onay(kap, nesne, ad, etiket, aciklama, sonra) {
  const l = document.createElement('label');
  l.className = 'onay';
  l.innerHTML = `<input type="checkbox"><span>${etiket} ` +
                `<em>— ${aciklama}</em></span>`;
  const k = l.querySelector('input');
  k.checked = !!nesne[ad];
  k.addEventListener('change', () => {
    nesne[ad] = k.checked;
    if (sonra) sonra();
    ciktiYaz();
  });
  kap.appendChild(l);
}

// --- goz olculeri: hepsi PIKSEL, hepsi C++'a gececek
const GOZ_ALANLARI = [
  ['gozG',    'Göz genişliği',    40, 110, 1],
  ['gozY',    'Göz yüksekliği',   40, 150, 1],
  ['aralik',  'Gözler arası',      4,  70, 1],
  ['yaricap', 'Köşe yarıçapı',     0,  50, 1],
  ['merkezY', 'Dikey merkez',     70, 170, 1],
  ['bakisX',  'Yatay bakış payı',  0,  30, 1],
  ['bakisY',  'Dikey bakış payı',  0,  24, 1],
];

const gozKaydiricilar = [];
{
  const kap = $('#gozAyarlari');
  for (const [ad, et, en, ez, ad2] of GOZ_ALANLARI) {
    gozKaydiricilar.push(kaydirici(kap, AYAR, ad, et, en, ez, ad2, 'px'));
  }
  const bilgi = document.createElement('p');
  bilgi.className = 'ipucu';
  bilgi.innerHTML =
    'Toplam genişlik <b id="toplamG">—</b> / 240 px. Taşarsa göz ekranın ' +
    'kenarında kesilir — özellikle <b>şaşkın</b> ve <b>çok mutlu</b> ' +
    'ifadelerinde göz büyüyor.';
  kap.appendChild(bilgi);
}

const VARSAYILAN_GOZ = { ...AYAR };

$('#gozSifirla').addEventListener('click', () => {
  Object.assign(AYAR, VARSAYILAN_GOZ);
  gozKaydiricilar.forEach((l) => l._sifirla());
  $$('#gorunumAyarlari input').forEach((k) => {
    if (k.type === 'checkbox' && k.dataset.ad) k.checked = !!AYAR[k.dataset.ad];
  });
  ciktiYaz();
});

// --- gorunum
{
  const kap = $('#gorunumAyarlari');
  onay(kap, AYAR, 'egimRenk', 'Renk geçişi',
       'satır başına bir renk, ESP32\'de bedava');
  onay(kap, AYAR, 'camParlamasi', 'Cam parlaması',
       'gözün üstündeki ışık lekesi');
  onay(kap, AYAR, 'kenarYumusatma', 'Kenar yumuşatma',
       'satır başına 2 piksel; kapatınca köşeler basamaklanır');
  onay(kap, AYAR, 'rgb565', 'Gerçek renk derinliği',
       'ekran 16 bit; kapatırsan 24 bit görürsün — ekranda YOK');

  kaydirici(kap, AYAR, 'parlamaKat', 'Parlama katmanı', 0, 6, 1, '');
  kaydirici(kap, AYAR, 'parlamaKalinlik', 'Parlama kalınlığı', 1, 12, 1, 'px');
  kaydirici(kap, AYAR, 'kafaEgimi', 'Kafa eğimi', 0, 3, 0.1, '×');

  const p = document.createElement('p');
  p.className = 'ipucu';
  p.innerHTML =
    'Kafa eğimi ESP32\'de <b>göz başına dikey kaydırma</b> olarak ' +
    'yapılıyor — çerçeve tamponunu gerçekten döndürmek piksel başına ' +
    'ters dönüşüm demek, o kadar bütçe yok. Bu boyutta eğim 2 dereceyi ' +
    'geçmediği için fark okunmuyor.';
  kap.appendChild(p);
}

// --- govde
{
  const kap = $('#govdeAyarlari');
  const y = () => yerlestir();
  kaydirici(kap, Govde.OLCU, 'kafaG', 'Kafa genişliği', 30, 110, 0.5, 'mm', y);
  kaydirici(kap, Govde.OLCU, 'kafaY', 'Kafa yüksekliği', 28, 100, 0.5, 'mm', y);
  kaydirici(kap, Govde.OLCU, 'kafaYaricap', 'Kafa köşe yarıçapı', 0, 40, 0.5, 'mm', y);
  kaydirici(kap, Govde.OLCU, 'govdeG', 'Gövde genişliği', 40, 120, 0.5, 'mm', y);
  kaydirici(kap, Govde.OLCU, 'govdeY', 'Gövde yüksekliği', 30, 120, 0.5, 'mm', y);

  const kutu = document.createElement('div');
  kap.appendChild(kutu);
  onay(kutu, { get akrilik() { return akrilik; }, set akrilik(v) { akrilik = v; } },
       'akrilik', 'Füme akrilik var',
       'Eilik\'in "siyah cam" hissi ekrandan değil bundan geliyor', y);
  onay(kutu, { get ic() { return icGoster; }, set ic(v) { icGoster = v; } },
       'ic', 'İç parçaları göster',
       'hoparlör Ø50, kart 28×57, ekran modülü 27×39 — sığıyor mu', y);

  const p = document.createElement('p');
  p.className = 'ipucu';
  p.innerHTML =
    '🔴 Ekranın aktif alanı (23,35 mm) dışındaki <b>tüm mm değerleri ' +
    'tahmin</b>. Kargo gelince kumpasla ölçülüp düzeltilecek. ' +
    'Kafayı büyüttükçe gözlerin nasıl kaybolduğuna bak — gövde ' +
    'ölçüsünü belirleyen şey estetik değil, <b>hoparlörün 50 mm çapı</b>.';
  kap.appendChild(p);
}

// ---------------------------------------------------------------------------
// Ifadeler ve sahne
// ---------------------------------------------------------------------------

{
  const kap = $('#ifadeDugmeleri');
  for (const ad of Object.keys(DURUMLAR)) {
    const d = document.createElement('button');
    d.textContent = ad;
    d.addEventListener('click', () => { sahneDur(); ifade(ad); });
    kap.appendChild(d);
  }
}

function ifade(ad, not = '') {
  gozler.ayarla(ad);
  $('#ifadeAdi').textContent = ad;
  $('#sahneNot').textContent = not;
}

// Gercek bir konusmanin akisi, OLCULEN surelerle.
//
// Tek tek ifadeye bakmak yaniltiyor: robotun hissi gecislerinde ve
// beklemelerinde. Dusunme payi uydurulmadi — Asama 1'de olculen
// medyan 1325 ms. Son iki adim para kuralini gosteriyor: bostaki
// oturum en buyuk maliyet kalemi, o yuzden uyutuluyor.
const SAHNE = [
  ['bos',       3000, 'boşta bekliyor'],
  ['dinliyor',  2200, 'çocuk konuşuyor'],
  ['dusunuyor', 1325, 'düşünüyor — ölçülen medyan 1325 ms'],
  ['konusuyor', 3800, 'cevap veriyor'],
  ['cok_mutlu', 1800, 'sevindi'],
  ['dinliyor',  1900, 'çocuk yine konuşuyor'],
  ['dusunuyor', 1325, 'düşünüyor'],
  ['konusuyor', 2600, 'cevap veriyor'],
  ['somurtkan', 2200, 'sitem etti'],
  ['meraklı',   2000, 'soru sordu'],
  ['saskin',    1600, 'şaşırdı'],
  ['uzgun',     2000, 'üzüldü'],
  ['afacan',    1800, 'şakalaşıyor'],
  ['uykulu',    3000, 'uzun sessizlik — uyuyor'],
  ['bos',       2500, 'uyudu · oturum uyutuldu, para akmıyor'],
];

let sahneZaman = null;
let sahneSira = 0;

function sahneAdim() {
  const [ad, ms, not] = SAHNE[sahneSira % SAHNE.length];
  ifade(ad, not);
  sahneSira++;
  sahneZaman = setTimeout(sahneAdim, ms);
}

function sahneDur() {
  if (sahneZaman) clearTimeout(sahneZaman);
  sahneZaman = null;
  $('#sahneOynat').textContent = '▶ Konuşmayı oynat';
  $('#sahneOynat').classList.add('birincil');
}

$('#sahneOynat').addEventListener('click', () => {
  if (sahneZaman) { sahneDur(); return; }
  sahneSira = 0;
  $('#sahneOynat').textContent = '⏸ Durdur';
  $('#sahneOynat').classList.remove('birincil');
  sahneAdim();
});

// ---------------------------------------------------------------------------
// Butce — hesap, olcum degil
// ---------------------------------------------------------------------------

function karar(yuzde) {
  if (yuzde < 30) return 'gecer';
  if (yuzde < 60) return 'sinir';
  return 'kalir';
}

function butceYaz() {
  const b = butce(gozler.olcum.piksel, 30);
  const en = Math.max(2 * AYAR.gozG * 1.12 + AYAR.aralik, 0);

  $('#toplamG') && ($('#toplamG').textContent = Math.round(en));
  $('#toplamG') &&
    $('#toplamG').closest('p').classList.toggle('tasti', en > EKRAN.g);

  $('#butce').innerHTML = `
    <div class="bs"><span>dokunulan piksel / kare</span>
      <b>${b.piksel.toLocaleString('tr-TR')}</b></div>
    <div class="bs"><span>çizim yükü (30 fps hedefi)</span>
      <b class="${karar(b.cizimYuzde)}">%${b.cizimYuzde.toFixed(0)}</b></div>
    <div class="bs"><span>SPI: tam ekran @${EKRAN.spiMhz} MHz</span>
      <b>${b.spiMs.toFixed(1)} ms</b></div>
    <div class="bs"><span>SPI yükü (30 fps hedefi)</span>
      <b class="${karar(b.spiYuzde)}">%${b.spiYuzde.toFixed(0)}</b></div>
    <div class="bs"><span>SPI'nin izin verdiği en yüksek fps</span>
      <b>${b.enFazlaFps.toFixed(0)}</b></div>
    <div class="bs"><span>tarayıcı (taşınmaz)</span>
      <b>${gozler.olcum.ms.toFixed(1)} ms · ${gozler.olcum.fps.toFixed(0)} fps</b></div>`;
}
setInterval(butceYaz, 400);

// ---------------------------------------------------------------------------
// C++ ciktisi
// ---------------------------------------------------------------------------
//
// NEDEN BOYLE BIR SEY VAR: burada begenilen sayi elle C++'a
// kopyalanirsa bir gun biri yanlis yazar ve iki taraf sessizce ayrilir.
// Ayni tuzak promptta da vardi; orada prompt_uret.py ile cozuldu.
function ciktiYaz() {
  const s = (n) => String(Math.round(n));
  $('#cikti').textContent =
`// pati_goz_ayar.h — panel/ studyosunda secildi
// Bu dosya ELLE DEGISTIRILMEZ: studyoda begenilen deger buraya gelir,
// yoksa tarayicidaki gorunum ile ekrandaki gorunum sessizce ayrilir.
#pragma once

#define PATI_GOZ_G            ${s(AYAR.gozG)}
#define PATI_GOZ_Y            ${s(AYAR.gozY)}
#define PATI_GOZ_ARALIK       ${s(AYAR.aralik)}
#define PATI_GOZ_YARICAP      ${s(AYAR.yaricap)}
#define PATI_GOZ_MERKEZ_Y     ${s(AYAR.merkezY)}
#define PATI_BAKIS_X          ${s(AYAR.bakisX)}
#define PATI_BAKIS_Y          ${s(AYAR.bakisY)}

#define PATI_PARLAMA_KAT      ${s(AYAR.parlamaKat)}
#define PATI_PARLAMA_KALINLIK ${s(AYAR.parlamaKalinlik)}
#define PATI_PARLAMA_ALFA     ${AYAR.parlamaAlfa}
#define PATI_KAFA_EGIMI       ${AYAR.kafaEgimi}

#define PATI_CAM_PARLAMASI    ${AYAR.camParlamasi ? 1 : 0}
#define PATI_EGIM_RENK        ${AYAR.egimRenk ? 1 : 0}
#define PATI_KENAR_YUMUSATMA  ${AYAR.kenarYumusatma ? 1 : 0}

// Govde (mm) — KUMPASLA DOGRULANMADI, ekranin 23,35'i haric hepsi tahmin
// kafa ${Govde.OLCU.kafaG} x ${Govde.OLCU.kafaY} · govde ${Govde.OLCU.govdeG} x ${Govde.OLCU.govdeY}`;
}

$('#ciktiKopyala').addEventListener('click', async () => {
  await navigator.clipboard.writeText($('#cikti').textContent);
  const d = $('#ciktiKopyala');
  d.textContent = 'kopyalandı ✓';
  setTimeout(() => { d.textContent = 'Kopyala'; }, 1200);
});

// ---------------------------------------------------------------------------
// Olcek dugmeleri, sekmeler, kalibrasyon
// ---------------------------------------------------------------------------

$$('.olcek[data-olcek]').forEach((d) => {
  d.addEventListener('click', () => {
    $$('.olcek[data-olcek]').forEach((x) => x.classList.remove('etkin'));
    d.classList.add('etkin');
    zum = parseFloat(d.dataset.olcek);
    yerlestir();
  });
});

$('#sadeceEkran').addEventListener('click', () => {
  sadeceEkran = !sadeceEkran;
  $('#sadeceEkran').classList.toggle('etkin', sadeceEkran);
  yerlestir();
});

$$('.sekme').forEach((d) => {
  d.addEventListener('click', () => {
    $$('.sekme').forEach((x) => x.classList.remove('etkin'));
    d.classList.add('etkin');
    const r = d.dataset.sekme === 'robot';
    $('#gorunumRobot').hidden = !r;
    $('#gorunumTelefon').hidden = r;
  });
});

// --- kalibrasyon
{
  const pencere = $('#kalibre');
  const kart = $('#kalibreKart');
  const kd = $('#kalibreKaydirici');

  const gunle = () => {
    const v = parseFloat(kd.value);
    kart.style.width = (Govde.KART_MM.g * v) + 'px';
    kart.style.height = (Govde.KART_MM.y * v) + 'px';
    $('#kalibreDeger').textContent =
      `${v.toFixed(2)} px/mm  (varsayılan ${VARSAYILAN_PXMM.toFixed(2)})`;
  };

  $('#kalibreAc').addEventListener('click', () => {
    kd.value = pxmm;
    gunle();
    pencere.hidden = false;
  });
  kd.addEventListener('input', gunle);
  $('#kalibreTamam').addEventListener('click', () => {
    pxmm = parseFloat(kd.value);
    localStorage.setItem('pati.pxmm', String(pxmm));
    pencere.hidden = true;
    yerlestir();
  });
}

// --- telefon cerceve boyutu
$$('.cihaz').forEach((d) => {
  d.addEventListener('click', () => {
    $$('.cihaz').forEach((x) => x.classList.remove('etkin'));
    d.classList.add('etkin');
    const f = $('#panelCerceve');
    f.style.width = d.dataset.g + 'px';
    f.style.height = d.dataset.y + 'px';
  });
});
// Panelin "Pati konusuyor" hali — iframe icinde oldugu icin
// postMessage ile soyluyoruz. Gercekte bu bilgi /api/durum'dan
// gelecek. Bildirim buraya, veri takimi degisince sifirlaniyor.
let panelKonusuyor = false;

// --- ornek veri takimi
//
// Adres sorgusuyla veriliyor (`./?veri=zor`), postMessage ile
// degil. Sebep: ayni adres telefonda TAM EKRAN da acilabiliyor ve yer
// imine eklenebiliyor. Iki ayri yol olsa biri eskirdi.
$$('.veri').forEach((d) => {
  d.addEventListener('click', () => {
    $$('.veri').forEach((x) => x.classList.remove('etkin'));
    d.classList.add('etkin');
    const t = d.dataset.veri;
    $('#panelCerceve').src = `./?veri=${t}`;
    $('.tamekran').href = `./?veri=${t}`;
    panelKonusuyor = false;
    $('#konusuyorAnahtar').classList.remove('etkin');
  });
});

$('#panelYenile').addEventListener('click', () => {
  const f = $('#panelCerceve');
  f.src = f.src;
});

$('#konusuyorAnahtar').addEventListener('click', () => {
  panelKonusuyor = !panelKonusuyor;
  $('#konusuyorAnahtar').classList.toggle('etkin', panelKonusuyor);
  $('#panelCerceve').contentWindow.postMessage({ konusuyor: panelKonusuyor }, '*');
});

$('.cihaz').click();

// ---------------------------------------------------------------------------

window.addEventListener('resize', yerlestir);
yerlestir();
ciktiYaz();
ifade('bos');
