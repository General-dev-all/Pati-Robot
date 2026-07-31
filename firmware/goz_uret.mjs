// -*- coding: utf-8 -*-
//
// Goz ayarlarini ve duygu tablosunu C++ basligina cevirir.
//
// ===========================================================================
// NEDEN URETILIYOR, ELLE YAZILMIYOR
// ===========================================================================
//
// Ayni tuzak promptta da vardi ve `prompt_uret.py` ile cozuldu: iki
// tarafta duran ayni veri, bir gun sessizce ayrilir. Burada ayrilacak
// veri daha da tehlikeli — 17 duygunun her biri icin iki goz x sekiz
// alan, artı davranis sayilari. Elle kopyalanirsa bir gun "somurtkan"
// tarayicida sevimli, ekranda kizgin gorunur ve kimse sebebini bulamaz.
//
// TEK KAYNAK: panel/gozler240.js
// URETILEN  : main/pati_goz_uretilmis.h
//
// Kullanim:
//     cd firmware && node goz_uret.mjs
//
// ===========================================================================
// NEDEN NODE, NEDEN PYTHON DEGIL
// ===========================================================================
//
// Kaynak bir JS modulu ve duygu tablosu `D({...}, {...})` yardimcisiyla
// kuruluyor. Python'la ayristirmak duzenli ifade yazmak demek — kirilgan
// ve sessizce yanlis. Node modulu DOGRUDAN ice aliyor, yani okudugu sey
// tarayicinin gordugu seyin ta kendisi. Ayristirma yok, tahmin yok.
//
// Uretim bittikten sonra GERI OKUYUP karsilastiriyor: uretilen basligi
// yeniden ayristirip kaynaktaki sayilarla birebir tutuyor mu diye
// bakiyor. "Yazdim" ile "dogru yazdim" ayri seyler.

import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const KOK = dirname(fileURLToPath(import.meta.url));
const HEDEF = join(KOK, 'main', 'pati_goz_uretilmis.h');

// gozler240.js modul seviyesinde tarayici API'si kullanmiyor, ama
// Tampon'un yapicisi ImageData istiyor. Ice almak yapiciyi
// cagirmiyor; yine de emniyet icin kuruyoruz.
globalThis.ImageData = class {
  constructor(g, y) {
    this.width = g; this.height = y;
    this.data = new Uint8ClampedArray(g * y * 4);
  }
};
globalThis.requestAnimationFrame = () => 0;
globalThis.window = { devicePixelRatio: 1 };

const { EKRAN, AYAR, DURUMLAR, RENKLER, GIRIS_VURGUSU,
        GIRIS_VURGUSU_VARSAYILAN } =
  await import(new URL('../panel/gozler240.js', import.meta.url));

// ---------------------------------------------------------------------------
// Yardimcilar
// ---------------------------------------------------------------------------

// C++ float sabiti. JS'te 1 yazan sayi C++'ta 1.0f olmali, yoksa
// tamsayi bolmesine dusuyor.
const f = (x) => {
  const s = Number(x).toString();
  return (s.includes('.') || s.includes('e') ? s : s + '.0') + 'f';
};

const ALANLAR = ['g', 'y', 'ustKapak', 'altKapak',
                 'egim', 'altEgim', 'kaydirX', 'kaydirY'];

function sekil(e) {
  return '{ ' + ALANLAR.map((a) => f(e[a])).join(', ') + ' }';
}

// C dizesi. Turkce karakter var ("meraklı") — dosya UTF-8 yaziliyor ve
// C++'ta bu bir bayt dizisi; modelden gelen ad da UTF-8, karsilastirma
// dogru cikiyor.
const dize = (s) => '"' + s.replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';

// ---------------------------------------------------------------------------
// Uretim
// ---------------------------------------------------------------------------

const adlar = Object.keys(DURUMLAR);

const satirlar = [];
const y = (s = '') => satirlar.push(s);

y('// URETILMIS DOSYA — ELLE DEGISTIRILMEZ.');
y('//');
y('// Kaynak : panel/gozler240.js');
y('// Ureten : firmware/goz_uret.mjs   (cd firmware && node goz_uret.mjs)');
y('//');
y('// Buraya elle yazilan her sey bir sonraki uretimde silinir. Goz');
y('// olculerini degistirmek icin gelistirici.html studyosunu kullan,');
y('// begendigin degerleri gozler240.js icindeki AYAR blogua yaz, sonra');
y('// bu ureteci calistir. Boylece tarayicida gorulen sey ile ekranda');
y('// cizilen sey AYNI KALIR.');
y('');
y('#pragma once');
y('');
y('#include <cstdint>');
y('');
y('// ---------------------------------------------------------------------------');
y('// Olculer — hepsi PIKSEL');
y('// ---------------------------------------------------------------------------');
y('');
y(`#define PATI_GOZ_EKRAN_G        ${EKRAN.g}`);
y(`#define PATI_GOZ_EKRAN_Y        ${EKRAN.y}`);
y('');
y(`#define PATI_GOZ_G              ${AYAR.gozG}`);
y(`#define PATI_GOZ_Y              ${AYAR.gozY}`);
y(`#define PATI_GOZ_ARALIK         ${AYAR.aralik}`);
y(`#define PATI_GOZ_YARICAP        ${AYAR.yaricap}`);
y(`#define PATI_GOZ_MERKEZ_Y       ${AYAR.merkezY}`);
y(`#define PATI_GOZ_BAKIS_X        ${AYAR.bakisX}`);
y(`#define PATI_GOZ_BAKIS_Y        ${AYAR.bakisY}`);
y('');
y('// Parlama: gercek bulanik golge ESP32"de yok. Ana seklin biraz');
y('// buyugu, dusuk alfayla, birkac kat halinde ciziliyor. Olculdu:');
y('// cizim maliyetinin %81"i bu katmanlar (bkz. gozler_test.mjs).');
y(`#define PATI_GOZ_PARLAMA_KAT       ${AYAR.parlamaKat}`);
y(`#define PATI_GOZ_PARLAMA_KALINLIK  ${AYAR.parlamaKalinlik}`);
y(`#define PATI_GOZ_PARLAMA_ALFA      ${f(AYAR.parlamaAlfa)}`);
y('');
y('// Kafa egimi ESP32"de goz basina DIKEY KAYDIRMA olarak yapiliyor.');
y('// Cerceve tamponunu gercekten dondurmek piksel basina ters donusum');
y('// demek; bu boyutta egim 2 dereceyi gecmedigi icin fark okunmuyor.');
y(`#define PATI_GOZ_KAFA_EGIMI     ${f(AYAR.kafaEgimi)}`);
y('');
y(`#define PATI_GOZ_CAM_PARLAMASI  ${AYAR.camParlamasi ? 1 : 0}`);
y(`#define PATI_GOZ_EGIM_RENK      ${AYAR.egimRenk ? 1 : 0}`);
y(`#define PATI_GOZ_KENAR_YUMUSAT  ${AYAR.kenarYumusatma ? 1 : 0}`);
y('');
y(`#define PATI_GOZ_GIRIS_VURGUSU_VARSAYILAN ${f(GIRIS_VURGUSU_VARSAYILAN)}`);
y('');
y('namespace pati {');
y('');
y('struct GozRenk { std::uint8_t r, g, b; };');
y('');
y('// Turkuaz — prototype/arayuz/gozler.js ile ayni iki uc.');
y(`inline constexpr GozRenk GOZ_ACIK   { ${RENKLER.ACIK.join(', ')} };`);
y(`inline constexpr GozRenk GOZ_KOYU   { ${RENKLER.KOYU.join(', ')} };`);
y(`inline constexpr GozRenk GOZ_PARLAK { ${RENKLER.PARLAK.join(', ')} };`);
y('');
y('// Bir gozun sekli. Alanlarin anlami gozler240.js"teki NOTR ile ayni:');
y('//   g / y            : taban olcunun carpani');
y('//   ustKapak/altKapak: gozun ustunden/altindan kapanan oran');
y('//   egim / altEgim   : IC kenari asagi (+) ya da yukari (-) kaydirir');
y('//   kaydirX/kaydirY  : ekran boyunun orani olarak kayma');
y('struct GozSekli {');
y('    float g, y, ust_kapak, alt_kapak, egim, alt_egim, kaydir_x, kaydir_y;');
y('};');
y('');
y('struct GozDurumu {');
y('    const char* ad;');
y('    GozSekli    goz[2];        // 0 = sol, 1 = sag');
y('    float       kirpma_hizi;   // 1 = normal, buyuk = daha sik kirpar');
y('    float       hareketlilik;  // bakisin siklik ve genisligi');
y('    float       egim_kafa;');
y('    bool        bakis_var;     // sabit bir bakis yonu tanimli mi');
y('    float       bakis_x, bakis_y;');
y('    float       giris_vurgusu; // duruma girerken atilan kisa abartma');
y('};');
y('');
y(`inline constexpr int GOZ_DURUM_SAYISI = ${adlar.length};`);
y('');
y('inline constexpr GozDurumu GOZ_DURUMLARI[GOZ_DURUM_SAYISI] = {');

for (const ad of adlar) {
  const d = DURUMLAR[ad];
  const b = d.bakis || null;
  const vurgu = GIRIS_VURGUSU[ad] ?? GIRIS_VURGUSU_VARSAYILAN;
  y(`    { ${dize(ad)},`);
  y(`      { ${sekil(d.goz[0])},`);
  y(`        ${sekil(d.goz[1])} },`);
  y(`      ${f(d.kirpmaHizi ?? 1)}, ${f(d.hareketlilik ?? 1)}, ` +
    `${f(d.egimKafa ?? 0)},`);
  y(`      ${b ? 'true' : 'false'}, ${f(b ? b[0] : 0)}, ${f(b ? b[1] : 0)}, ` +
    `${f(vurgu)} },`);
}

y('};');
y('');
y('}  // namespace pati');
y('');

const icerik = satirlar.join('\n');
writeFileSync(HEDEF, icerik, 'utf8');

// ---------------------------------------------------------------------------
// GERI OKUMA — "yazdim" ile "dogru yazdim" ayri seyler
// ---------------------------------------------------------------------------
//
// Uretilen basligi yeniden ayristirip kaynaktaki sayilarla
// karsilastiriyoruz. Basit gorunuyor ama tam olarak bu adim
// prompt_uret.py'de bir kez ise yaradi.

const geri = readFileSync(HEDEF, 'utf8');
let hata = 0;
const bak = (kosul, ad) => {
  if (!kosul) { console.log('  HATA  ' + ad); hata++; }
};

// --- #define sayilari
const tanim = (ad) => {
  const m = geri.match(new RegExp(`^#define\\s+${ad}\\s+(\\S+)`, 'm'));
  return m ? parseFloat(m[1]) : NaN;
};
const eslesme = [
  ['PATI_GOZ_EKRAN_G', EKRAN.g], ['PATI_GOZ_EKRAN_Y', EKRAN.y],
  ['PATI_GOZ_G', AYAR.gozG], ['PATI_GOZ_Y', AYAR.gozY],
  ['PATI_GOZ_ARALIK', AYAR.aralik], ['PATI_GOZ_YARICAP', AYAR.yaricap],
  ['PATI_GOZ_MERKEZ_Y', AYAR.merkezY],
  ['PATI_GOZ_BAKIS_X', AYAR.bakisX], ['PATI_GOZ_BAKIS_Y', AYAR.bakisY],
  ['PATI_GOZ_PARLAMA_KAT', AYAR.parlamaKat],
  ['PATI_GOZ_PARLAMA_KALINLIK', AYAR.parlamaKalinlik],
  ['PATI_GOZ_PARLAMA_ALFA', AYAR.parlamaAlfa],
  ['PATI_GOZ_KAFA_EGIMI', AYAR.kafaEgimi],
  ['PATI_GOZ_CAM_PARLAMASI', AYAR.camParlamasi ? 1 : 0],
  ['PATI_GOZ_EGIM_RENK', AYAR.egimRenk ? 1 : 0],
  ['PATI_GOZ_KENAR_YUMUSAT', AYAR.kenarYumusatma ? 1 : 0],
];
for (const [ad, beklenen] of eslesme) {
  bak(Math.abs(tanim(ad) - beklenen) < 1e-9, `${ad}: ${tanim(ad)} != ${beklenen}`);
}

// --- duygu tablosu: her kaydin sayilarini geri oku
//
// Kayitlar cok satirli, o yuzden ic ice suslu parantezleri duzenli
// ifadeyle esitlemeye CALISMIYORUZ (bir kez denendi ve sessizce ilk
// gozu okuyup durdu). Onun yerine kayit BASLANGICINDAN boluyoruz:
// "\n    { \"" dizisi sadece kayit basinda geciyor — ic satirlar alti
// bosluk girintili.
const bas = geri.indexOf('GOZ_DURUMLARI[GOZ_DURUM_SAYISI] = {');
const son = geri.indexOf('\n};', bas);
const govde = geri.slice(bas, son);
const parcalar = govde.split('\n    { "').slice(1);

bak(parcalar.length === adlar.length,
    `tabloda ${parcalar.length} kayit var, ${adlar.length} olmali`);

for (let i = 0; i < adlar.length && i < parcalar.length; i++) {
  const ad = adlar[i];
  const d = DURUMLAR[ad];
  const parca = parcalar[i];
  const okunanAd = parca.slice(0, parca.indexOf('"'));
  bak(okunanAd === ad, `${i}. kayit adi ${okunanAd} != ${ad}`);

  const sayilar = parca.match(/-?\d+(?:\.\d+)?(?:e-?\d+)?f/g)
                    ?.map((s) => parseFloat(s)) ?? [];
  const b = d.bakis || null;
  const beklenen = [
    ...ALANLAR.map((a) => d.goz[0][a]),
    ...ALANLAR.map((a) => d.goz[1][a]),
    d.kirpmaHizi ?? 1, d.hareketlilik ?? 1, d.egimKafa ?? 0,
    b ? b[0] : 0, b ? b[1] : 0,
    GIRIS_VURGUSU[ad] ?? GIRIS_VURGUSU_VARSAYILAN,
  ];
  bak(sayilar.length === beklenen.length,
      `${ad}: ${sayilar.length} sayi okundu, ${beklenen.length} olmali`);
  for (let k = 0; k < Math.min(sayilar.length, beklenen.length); k++) {
    bak(Math.abs(sayilar[k] - beklenen[k]) < 1e-6,
        `${ad}[${k}]: ${sayilar[k]} != ${beklenen[k]}`);
  }
  const bakisVar = /\b(true|false)\b/.exec(parca);
  bak(bakisVar && (bakisVar[1] === 'true') === !!b,
      `${ad}: bakis_var yanlis`);
}

console.log(`  ${HEDEF.replace(KOK, 'firmware')}`);
console.log(`  ${adlar.length} duygu · ${icerik.length} karakter`);
console.log(hata === 0
  ? '  GERI OKUMA TAMAM — uretilen baslik kaynakla birebir'
  : `  ${hata} UYUSMAZLIK`);
process.exit(hata ? 1 : 0);
