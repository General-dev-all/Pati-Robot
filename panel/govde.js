// -*- coding: utf-8 -*-
//
// Pati'nin govdesini MILIMETRE olcusunde cizer.
//
// ===========================================================================
// NEDEN BU DOSYA VAR
// ===========================================================================
//
// "Gozler guzel mi" sorusunun cevabi gozlere bakarak verilemez, cunku
// gozler 25 x 14 mm'lik bir yuzeyde duracak. Bilgisayar ekraninda 600
// piksele buyutulmus bir goz cifti harika gorunur; gercekte tirnak
// buyuklugunde bir alandir.
//
// Bu dosya o gercegi gosteriyor: govde ve ekran AYNI mm olceginde
// ciziliyor, yanina da kredi karti konabiliyor.
//
// ===========================================================================
// 🔴 BU DOSYA 23.08.2026'DA TAMAMEN DEGISTI
// ===========================================================================
//
// Onceden burada iki parcali bir robot vardi: kafa (54 x 50 mm), govde
// (64 x 62 mm), icinde 5 cm'lik hoparlor, ayri ekran modulu ve dikey
// duran bir ESP32-S3 DevKit karti. Hepsi 3D baskiyla yapilacak bir
// kabuk icin cizilmisti.
//
// O donanim emekli oldu. Pati artik tek bir parca: M5Stack StickS3,
// 48 x 24 x 15 mm, ekrani ve hoparloru icinde. Baski yok, montaj yok.
//
// Eski cizim `devkit` dalinda duruyor:
//     git show v2.2.8-devkit:panel/govde.js
//
// ⚠️ DIS GOVDE HENUZ YOK. Ileride cubugu icine alan bir kabuk
// yapilabilir; yapilinca buraya gercek olculerle eklenir. Simdi
// olmayan bir govdeyi cizmiyoruz — tahmini bir kabuk cizmek, karar
// verilmis gibi gorunmesine yol acardi.

// ===========================================================================
// OLCULER
// ===========================================================================
//
// KESIN olanlar M5Stack'in urun sayfasindan (docs.m5stack.com/en/core/
// StickS3, "Specifications" ve "Model Size"). `?` isaretliler tahmin —
// stuudyo bunlari ayri listeliyor ki "olculdu" saniilmasin.
//
// Cubuk YATAY tutuluyor: 48 mm genislik, 24 mm yukseklik. Ekran da
// yatay, cunku iki goz yan yana ancak boyle sigiyor (bkz. gozler240.js).

export const OLCU = {
  // --- cubugun kendisi (kesin)
  cubukG: 48.0,
  cubukY: 24.0,
  cubukD: 15.0,           // derinlik — cizimde gorunmuyor, olcu icin
  cubukYaricap: 3.5,      // ? kose yuvarlamasi, fotograftan

  // --- ekranin AKTIF alani (kesin: 1.14" kosegen, 135x240 piksel)
  //
  // Kosegen 1.14 inc = 28,96 mm. 135:240 oraniyla kisa kenar 14,2 mm,
  // uzun kenar 25,2 mm. Yatay kullandigimiz icin uzun kenar genislik.
  ekranG: 25.2,
  ekranY: 14.2,

  // Ekranin cubugun sol kenarina uzakligi.
  //
  // ? TAHMIN. Fotografta ekran ortada degil, bir uca dogru kaymis
  // duruyor. Kumpasla olculecek — kart gelince duzeltilecek tek sayi
  // burasi.
  ekranSolBosluk: 8.0,

  // --- ic parcalar (yerlesim gostergesi)
  hoparlorG: 20.0,        // 2011 kasa: 20 x 11 mm
  hoparlorY: 11.0,
  usbG: 9.2,              // ? USB-C disi yuva
  usbY: 3.4,
};

// Kredi karti: ISO/IEC 7810 ID-1 = 85,60 x 53,98 mm.
// Hem ekran olcegini kalibre etmek hem "ne kadar kucuk" hissini
// vermek icin kullaniliyor. Cubuk kartin YARISINDAN kisa.
export const KART_MM = { g: 85.60, y: 53.98 };

const RENK = {
  kabuk:      '#3a4046',
  kabukKoyu:  '#23282d',
  kabukKenar: '#14181b',
  ic:         'rgba(23, 196, 196, .22)',
  icCizgi:    'rgba(23, 196, 196, .75)',
  golge:      'rgba(0, 0, 0, .35)',
};

// Ekranin aktif alaninin, cubugun sol-ust kosesine gore yeri (mm).
// Stuudyo goz tuvalini tam buraya oturtuyor.
export function ekranYeri() {
  return {
    x: OLCU.ekranSolBosluk,
    y: (OLCU.cubukY - OLCU.ekranY) / 2,
    gen: OLCU.ekranG,
    yuk: OLCU.ekranY,
  };
}

export function toplamOlcu() {
  return { g: OLCU.cubukG, y: OLCU.cubukY };
}

function yuvarlakYol(c, x, y, g, y2, r) {
  r = Math.min(r, g / 2, y2 / 2);
  c.beginPath();
  c.moveTo(x + r, y);
  c.lineTo(x + g - r, y);
  c.arcTo(x + g, y, x + g, y + r, r);
  c.lineTo(x + g, y + y2 - r);
  c.arcTo(x + g, y + y2, x + g - r, y + y2, r);
  c.lineTo(x + r, y + y2);
  c.arcTo(x, y + y2, x, y + y2 - r, r);
  c.lineTo(x, y + r);
  c.arcTo(x, y, x + r, y, r);
  c.closePath();
}

// Cubugu cizer. `olcek` = piksel / mm.
//
// icGoster: hoparlor ve USB yuvasini seffaf gosterir.
// akrilik: ekranin onunde koyu bir pencere varmis gibi gosterir —
//          cubugun kendi cami zaten koyu, bu daha cok ileride bir dis
//          govde yapilirsa nasil gorunecegini denemek icin.
export function ciz(tuval, olcek, secenek = {}) {
  const { icGoster = false, akrilik = true } = secenek;
  const c = tuval.getContext('2d');

  const T = toplamOlcu();
  const kenar = 6;                       // mm cinsinden pay
  const G = Math.round((T.g + kenar * 2) * olcek);
  const Y = Math.round((T.y + kenar * 2) * olcek);

  const dpr = window.devicePixelRatio || 1;
  tuval.width = Math.round(G * dpr);
  tuval.height = Math.round(Y * dpr);
  tuval.style.width = G + 'px';
  tuval.style.height = Y + 'px';
  c.setTransform(dpr * olcek, 0, 0, dpr * olcek,
                 dpr * kenar * olcek, dpr * kenar * olcek);
  c.clearRect(-kenar, -kenar, T.g + kenar * 2, T.y + kenar * 2);

  // --- cubugun govdesi
  c.save();
  c.shadowColor = RENK.golge;
  c.shadowBlur = 3;
  c.shadowOffsetY = 1.2;
  const gGrad = c.createLinearGradient(0, 0, 0, T.y);
  gGrad.addColorStop(0, RENK.kabuk);
  gGrad.addColorStop(1, RENK.kabukKoyu);
  c.fillStyle = gGrad;
  yuvarlakYol(c, 0, 0, T.g, T.y, OLCU.cubukYaricap);
  c.fill();
  c.restore();

  c.save();
  c.strokeStyle = RENK.kabukKenar;
  c.lineWidth = 0.3;
  yuvarlakYol(c, 0, 0, T.g, T.y, OLCU.cubukYaricap);
  c.stroke();
  c.restore();

  // --- ekran penceresi
  const e = ekranYeri();
  const pay = akrilik ? 1.6 : 0.5;
  const pX = e.x - pay;
  const pY = e.y - pay;
  const pG = e.gen + pay * 2;
  const pY2 = e.yuk + pay * 2;

  c.save();
  const camGrad = c.createLinearGradient(pX, pY, pX, pY + pY2);
  camGrad.addColorStop(0, '#141b20');
  camGrad.addColorStop(1, '#05080a');
  c.fillStyle = camGrad;
  yuvarlakYol(c, pX, pY, pG, pY2, akrilik ? 1.8 : 0.6);
  c.fill();
  c.strokeStyle = 'rgba(0,0,0,.55)';
  c.lineWidth = 0.3;
  c.stroke();
  c.restore();

  // --- ic parcalar
  if (icGoster) {
    c.save();
    c.fillStyle = RENK.ic;
    c.strokeStyle = RENK.icCizgi;
    c.lineWidth = 0.25;
    c.setLineDash([1.0, 0.7]);

    // Hoparlor — ekranin sag tarafinda kalan bosluga.
    const hX = e.x + e.gen + (T.g - e.x - e.gen - OLCU.hoparlorG) / 2;
    const hY = (T.y - OLCU.hoparlorY) / 2;
    c.beginPath();
    c.rect(hX, hY, OLCU.hoparlorG, OLCU.hoparlorY);
    c.fill(); c.stroke();

    c.restore();

    c.save();
    c.fillStyle = 'rgba(255,255,255,.55)';
    c.font = '2.2px sans-serif';
    c.fillText('hoparlör', hX + 1, hY + OLCU.hoparlorY / 2 + 0.8);
    c.restore();
  }

  // USB-C yuvasi — cubugun sag ucunda (yatay tutulunca).
  c.save();
  c.fillStyle = '#181c20';
  yuvarlakYol(c, T.g - OLCU.usbY - 1.2, T.y / 2 - OLCU.usbG / 2,
              OLCU.usbY, OLCU.usbG, 1.4);
  c.fill();
  c.restore();

  // Donen olculer MM cinsinden ve TUVAL kosesine gore (kenar payi
  // dahil) — stuudyo goz tuvalini ve ortuyu buna gore oturtuyor.
  //
  // ⚠️ EKRAN ARTIK KARE DEGIL. Onceden tek bir `boy` doniyordu ve iki
  // eksende de o kullaniliyordu; 240x135'te bu gozleri ezerdi.
  return {
    G, Y, olcek,
    ekran:   { x: kenar + e.x, y: kenar + e.y, gen: e.gen, yuk: e.yuk },
    pencere: { x: kenar + pX,  y: kenar + pY,  gen: pG,    yuk: pY2 },
  };
}
