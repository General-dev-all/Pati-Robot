// -*- coding: utf-8 -*-
//
// Robotun govde siluetini MILIMETRE olcusunde cizer.
//
// ===========================================================================
// NEDEN BU DOSYA VAR
// ===========================================================================
//
// "Gozler guzel mi" sorusunun cevabi gozlere bakarak verilemez, cunku
// gozler 23,35 mm'lik bir karenin icinde duracak. Bilgisayar ekraninda
// 600 piksele buyutulmus bir goz cifti harika gorunur; gercekte pul
// buyuklugunde bir yuzeydir.
//
// Bu dosya o gercegi gosteriyor: govde, ekran, hoparlor ve kart AYNI
// mm olceginde ciziliyor. Boylece iki sey aninda goruluyor:
//
//   1. Ekran, kafanin ne kadarini kapliyor. Kafa buyutulunce gozler
//      koca bir yuzde kaybolan iki noktaya donusuyor — bu bir TASARIM
//      KARARI ve simdi verilmesi gerekiyor, baski yapildiktan sonra
//      degil.
//   2. Hoparlor (cap 50 mm) ekrandan (23,35 mm) IKI KAT buyuk. Yani
//      govdenin boyunu belirleyen sey yuz degil, hoparlor.
//
// ===========================================================================
// OLCULER — hangisi kesin, hangisi tahmin
// ===========================================================================
//
// KESIN olan tek sey ekranin aktif alani: 1.3" kosegen kare = 23,35 mm.
// Geri kalan her sey PLAN.md'daki parametrik degiskenlerden geliyor
// ve KARGO GELINCE KUMPASLA DUZELTILECEK. Bu yuzden hepsi tek yerde,
// isimli ve degistirilebilir.
//
// `?` isaretli olanlar tahmin — stuudyo bunlari ayri listeliyor ki
// "olculdu" saniimasin.

export const OLCU = {
  // --- ekran modulu
  ekranModulG: 27,        // ? tipik 1.3" ST7789 modul karti
  ekranModulY: 39,        // ?
  ekranAktif: 23.35,      // kesin: 1.3" kosegen / V2
  aktifUstBosluk: 6,      // ? modul ustunden aktif alanin ustune

  // --- kafa
  kafaG: 54,
  kafaY: 50,
  kafaYaricap: 16,

  // --- govde
  govdeG: 64,
  govdeY: 62,
  govdeYaricap: 14,

  // --- ic parcalar (yerlestirme dogru mu diye gosteriliyor)
  hoparlorCap: 50,        // satici sayfasi: 5 cm
  kartG: 28,              // ? ESP32-S3-DevKitC-1 govde genisligi
  kartU: 57,              // ?
  duvar: 2,               // 3D baski et kalinligi
};

// Kredi karti: ISO/IEC 7810 ID-1 = 85,60 x 53,98 mm.
// Hem ekran olcegini kalibre etmek hem "ne kadar kucuk" hissini
// vermek icin kullaniliyor.
export const KART_MM = { g: 85.60, y: 53.98 };

const RENK = {
  kabuk:      '#e9edf1',
  kabukKoyu:  '#c6ccd3',
  cerceve:    '#2b3238',
  govde:      '#dfe4e9',
  govdeKoyu:  '#bcc3ca',
  ic:         'rgba(23, 196, 196, .22)',
  icCizgi:    'rgba(23, 196, 196, .75)',
  golge:      'rgba(0, 0, 0, .30)',
};


// Ekranin aktif alaninin, kafanin sol-ust kosesine gore yeri (mm).
// Stuudyo goz tuvalini tam buraya oturtuyor.
export function ekranYeri() {
  return {
    x: (OLCU.kafaG - OLCU.ekranAktif) / 2,
    y: (OLCU.kafaY - OLCU.ekranAktif) / 2,
    boy: OLCU.ekranAktif,
  };
}

export function toplamOlcu() {
  return {
    g: Math.max(OLCU.kafaG, OLCU.govdeG),
    y: OLCU.kafaY + OLCU.govdeY,
  };
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


// Govdeyi cizer. `olcek` = piksel / mm.
//
// icGoster: hoparlor, kart ve ekran modulunu seffaf gosterir — "sigiyor
// mu" sorusunun cevabi. Kapaliyken robot dısardan nasil gorunuyorsa o.
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
  c.setTransform(dpr * olcek, 0, 0, dpr * olcek, dpr * kenar * olcek, dpr * kenar * olcek);
  c.clearRect(-kenar, -kenar, T.g + kenar * 2, T.y + kenar * 2);

  const kafaX = (T.g - OLCU.kafaG) / 2;
  const govdeX = (T.g - OLCU.govdeG) / 2;
  const govdeY = OLCU.kafaY;

  // --- govde (once, kafanin arkasinda kalsin)
  c.save();
  c.shadowColor = RENK.golge;
  c.shadowBlur = 3;
  c.shadowOffsetY = 1.2;

  const gGrad = c.createLinearGradient(0, govdeY, 0, govdeY + OLCU.govdeY);
  gGrad.addColorStop(0, RENK.govde);
  gGrad.addColorStop(1, RENK.govdeKoyu);
  c.fillStyle = gGrad;
  yuvarlakYol(c, govdeX, govdeY, OLCU.govdeG, OLCU.govdeY, OLCU.govdeYaricap);
  c.fill();
  c.restore();

  // Hoparlor izgarasi — capi gercek hoparlorun capi.
  const hopX = T.g / 2;
  const hopY = govdeY + OLCU.govdeY * 0.42;
  c.save();
  c.strokeStyle = 'rgba(0,0,0,.22)';
  c.lineWidth = 0.35;
  for (let r = 3; r <= OLCU.hoparlorCap / 2 - 4; r += 3.2) {
    c.beginPath();
    c.arc(hopX, hopY, r, 0, Math.PI * 2);
    c.stroke();
  }
  c.restore();

  // --- kafa
  c.save();
  c.shadowColor = RENK.golge;
  c.shadowBlur = 3.5;
  c.shadowOffsetY = 1.5;
  const kGrad = c.createLinearGradient(0, 0, 0, OLCU.kafaY);
  kGrad.addColorStop(0, RENK.kabuk);
  kGrad.addColorStop(1, RENK.kabukKoyu);
  c.fillStyle = kGrad;
  yuvarlakYol(c, kafaX, 0, OLCU.kafaG, OLCU.kafaY, OLCU.kafaYaricap);
  c.fill();
  c.restore();

  // Yuz penceresi: fume akrilik ya da ciplak ekran.
  //
  // PLAN v1 §10b'nin bulgusu: Eilik'in "siyah cam" hissi ekrandan
  // degil onundeki koyu seffaf akrilikten geliyor. Akrilik olmadan
  // ekranin kenari ve modul karti goruluyor — robot "ekran yapistirilmis
  // kutu" gibi duruyor. Bu anahtar o farki gosteriyor.
  const e = ekranYeri();
  const pencereBosluk = akrilik ? 3.5 : 0.6;
  const pX = kafaX + e.x - pencereBosluk;
  const pY = e.y - pencereBosluk;
  const pB = e.boy + pencereBosluk * 2;

  c.save();
  const camGrad = c.createLinearGradient(pX, pY, pX, pY + pB);
  camGrad.addColorStop(0, akrilik ? '#141b20' : '#0a0d10');
  camGrad.addColorStop(1, akrilik ? '#05080a' : '#05080a');
  c.fillStyle = camGrad;
  yuvarlakYol(c, pX, pY, pB, pB, akrilik ? 5 : 1.2);
  c.fill();
  c.strokeStyle = 'rgba(0,0,0,.55)';
  c.lineWidth = 0.4;
  c.stroke();
  c.restore();

  // --- ic parcalar
  if (icGoster) {
    c.save();
    c.fillStyle = RENK.ic;
    c.strokeStyle = RENK.icCizgi;
    c.lineWidth = 0.3;
    c.setLineDash([1.2, 0.8]);

    // ekran modul karti
    const mX = T.g / 2 - OLCU.ekranModulG / 2;
    const mY = e.y - OLCU.aktifUstBosluk;
    c.beginPath();
    c.rect(mX, mY, OLCU.ekranModulG, OLCU.ekranModulY);
    c.fill(); c.stroke();

    // hoparlor
    c.beginPath();
    c.arc(hopX, hopY, OLCU.hoparlorCap / 2, 0, Math.PI * 2);
    c.fill(); c.stroke();

    // ESP32 karti — dikey, govdenin arkasinda
    const kX = T.g / 2 - OLCU.kartG / 2;
    const kY = govdeY + OLCU.govdeY - OLCU.kartU - OLCU.duvar;
    c.beginPath();
    c.rect(kX, Math.max(kY, govdeY + OLCU.duvar), OLCU.kartG, OLCU.kartU);
    c.fill(); c.stroke();

    c.restore();

    // Uyari: kart 57 mm, govde ic yuksekligi buna yetiyor mu
    const icY = OLCU.govdeY - OLCU.duvar * 2;
    if (OLCU.kartU > icY) {
      c.save();
      c.fillStyle = '#f85149';
      c.font = '3px sans-serif';
      c.fillText(`kart ${OLCU.kartU} mm > ic ${icY.toFixed(0)} mm`, govdeX + 2, govdeY + OLCU.govdeY - 2);
      c.restore();
    }
  }

  // USB-C yuvasi — cocuk kabloyu kendisi takip cikaracak (PLAN.md/5).
  c.save();
  c.fillStyle = '#3a4147';
  const uG = 9.2, uY = 3.4;                // USB-C disi yuva olculeri (?)
  yuvarlakYol(c, T.g / 2 - uG / 2, govdeY + OLCU.govdeY - uY - 3, uG, uY, 1.6);
  c.fill();
  c.restore();

  // Donen olculer MM cinsinden ve kafa kosesine gore degil TUVAL
  // kosesine gore (kenar payi dahil) — stuudyo goz tuvalini ve akrilik
  // ortusunu buna gore oturtuyor. Iki yerde ayni hesabi yapmamak icin
  // burada bir kez hesaplaniyor.
  return {
    G, Y, olcek,
    ekran:   { x: kenar + kafaX + e.x, y: kenar + e.y, boy: e.boy },
    pencere: { x: kenar + pX,          y: kenar + pY,  boy: pB },
  };
}
