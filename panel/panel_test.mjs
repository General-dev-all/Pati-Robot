// Panel duman testi — tarayici acmadan.
//
// ===========================================================================
// NE YAKALIYOR
// ===========================================================================
//
// Tek bir hata sinifini, ama pahali olani: `$('#birsey')` HTML'de
// olmayan bir ogeyi arayinca `null` donuyor ve bir sonraki satirda
// TypeError atiyor. Modul orada duruyor — yani SAYFANIN GERI KALANI DA
// cizilmiyor. Ekranda yari kurulmus bir panel kaliyor ve sebebi ancak
// telefonun gelistirici konsolunda gorunuyor.
//
// Bu, kart eklerken ya da bir id'yi yeniden adlandirirken olan siradan
// bir hata. Gozle bakarak fark edilmiyor cunku bozulan sey, bakilan sey
// degil — sayfanin ALTINDA kalan her sey.
//
// ===========================================================================
// NEDEN GERCEK BIR DOM DEGIL
// ===========================================================================
//
// jsdom kurmak icin bagimlilik gerekiyor ve bu depo bagimlilik
// tasimiyor. Buradaki sahte DOM, pati.js'in DOKUNDUGU kadarini
// karsiliyor: eksigi cikarsa test patlar ve o zaman eklenir. Amac
// tarayiciyi taklit etmek degil, "modul bastan sona kosabiliyor mu"
// sorusuna cevap vermek.
//
// Bu yuzden GECMESI sayfanin dogru gorundugu anlamina GELMEZ. Tasarim
// yine telefondan bakilarak onaylaniyor (panel/TELEFONDAN-INCELE.bat).
// Burasi yalnizca oraya bakmadan once patlamayi yakaliyor.
//
// ===========================================================================
// CALISTIRMA
// ===========================================================================
//
//   node panel/panel_test.mjs            dort veri takimini da dener
//   node panel/panel_test.mjs zor        yalnizca birini
//
// Depo kokunden calistirilmasi gerekmiyor; yollar bu dosyaya gore.

import { readFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { dirname, join } from 'node:path';
import { spawnSync } from 'node:child_process';

const BURASI = dirname(fileURLToPath(import.meta.url));
const TAKIMLAR = ['normal', 'zor', 'ilk', 'kopuk'];

// ---------------------------------------------------------------------------
// Alt surec: her takim AYRI kosuyor
// ---------------------------------------------------------------------------
//
// ornek.js hangi takimi kullanacagini `location.search`'ten MODUL
// YUKLENIRKEN bir kez okuyor, ve ES modulleri surec basina onbellege
// alaniyor. Ayni surecte dort takimi denemek mumkun degil: ikincisi
// birincinin verisini gorurdu ve test her seyi "gecti" diye yazardi.
if (!process.env.PATI_TAKIM) {
  let kalan = 0;
  for (const t of TAKIMLAR) {
    const s = spawnSync(process.execPath, [fileURLToPath(import.meta.url)], {
      env: { ...process.env, PATI_TAKIM: t },
      encoding: 'utf8',
    });
    process.stdout.write(s.stdout || '');
    process.stderr.write(s.stderr || '');
    if (s.status !== 0) kalan++;
  }
  console.log(kalan ? `\n${kalan} takim KALDI` : '\nDort takim da gecti');
  process.exit(kalan ? 1 : 0);
}

const takim = process.env.PATI_TAKIM;

// ---------------------------------------------------------------------------
// Sahte DOM
// ---------------------------------------------------------------------------

const html = readFileSync(join(BURASI, 'index.html'), 'utf8');
const idler = [...html.matchAll(/id="([^"]+)"/g)].map((m) => m[1]);

const dinleyici = {};
const eksik = [];

function oge(id) {
  return {
    id,
    _metin: '',
    value: '', hidden: false, disabled: false, className: '', type: 'text',
    style: {}, width: 240, height: 240,
    classList: { toggle() {}, add() {}, remove() {} },
    get textContent() { return this._metin; },
    set textContent(v) { this._metin = String(v); },
    get innerHTML() { return this._metin; },
    set innerHTML(v) { this._metin = String(v); },
    addEventListener(t, f) { (dinleyici[id] ||= {})[t] = f; },
    removeEventListener() {},
    appendChild() {}, prepend() {}, remove() {}, focus() {}, blur() {},
    scrollIntoView() {},
    querySelector: () => oge(`${id}:ic`),
    querySelectorAll: () => [],
    matches: () => false,
    getContext: () => ({
      createImageData: (g, y) => new globalThis.ImageData(g, y),
      getImageData: (a, b, g, y) => new globalThis.ImageData(g, y),
      putImageData() {}, fillRect() {}, clearRect() {},
    }),
  };
}

const kutu = new Map(idler.map((id) => [id, oge(id)]));

globalThis.ImageData = class {
  constructor(g, y) {
    this.width = g;
    this.height = y;
    this.data = new Uint8ClampedArray(g * y * 4);
  }
};
globalThis.document = {
  querySelector(s) {
    if (!s.startsWith('#')) return oge(s);
    const id = s.slice(1);
    // 🔴 TESTIN BUTUN AMACI BU SATIR. Gercek tarayici da null donuyor
    // ve cagiran taraf uzerinde ".textContent" deyince patliyor.
    if (!kutu.has(id)) { eksik.push(id); return null; }
    return kutu.get(id);
  },
  querySelectorAll: () => [],
  createElement: (t) => oge(`<${t}>`),
  addEventListener() {},
  body: oge('body'),
  title: '',
  activeElement: null,
  visibilityState: 'visible',
};
globalThis.window = globalThis;
globalThis.location = {
  hostname: '127.0.0.1', protocol: 'http:', host: '127.0.0.1:8756',
  search: `?veri=${takim}`,
};
globalThis.isSecureContext = true;
globalThis.addEventListener = () => {};
globalThis.setInterval = () => 0;
globalThis.requestAnimationFrame = () => 0;
globalThis.confirm = () => true;
globalThis.WebSocket = class { constructor() { this.readyState = 0; } send() {} close() {} };
globalThis.WebSocket.OPEN = 1;
globalThis.AudioContext = class { constructor() { this.state = 'running'; } close() {} };
// Robot YOK: /api/durum cevapsiz kaliyor, sayfa tezgah moduna dusuyor.
globalThis.fetch = async () => { throw new Error('robot yok'); };
Object.defineProperty(globalThis, 'navigator', {
  value: { mediaDevices: { getUserMedia: async () => ({}) } },
  configurable: true,
});

let cokme = null;
process.on('unhandledRejection', (e) => { cokme = e; });

// Yuklemenin KENDISI patlayabiliyor: ilk cizim modulun en ust
// duzeyinde kosuyor ve orada eksik bir ogeye dokunulursa TypeError
// atiyor. Yakalamazsak node ham bir yigin dokumu basiyor ve testin
// hangi denetimi yaptigi kayboluyor. Yigin dokumunu yine yaziyoruz —
// satir numarasi en degerli bilgi — ama once ne oldugunu soyluyoruz.
try {
  await import(pathToFileURL(join(BURASI, 'pati.js')).href);
  // Sayfanin acilis IIFE'si asenkron: fetch reddedilip tezgah moduna
  // dusmesi icin bir tur beklemek gerekiyor.
  await new Promise((c) => { setTimeout(c, 300); });
} catch (e) {
  console.log(`--- ${takim} ---`);
  console.log('  !! pati.js YUKLENIRKEN PATLADI');
  if (eksik.length) {
    console.log(`  !! HTML'de olmayan oge: ${[...new Set(eksik)]}`);
  }
  console.log(String(e.stack || e).split('\n').slice(0, 4)
    .map((s) => `     ${s.trim()}`).join('\n'));
  console.log('  KALDI\n');
  process.exit(1);
}

// ---------------------------------------------------------------------------
// Denetimler
// ---------------------------------------------------------------------------

const yaz = (id) => kutu.get(id)?.textContent || '';
const uyari = kutu.get('anahtarUyari');

console.log(`--- ${takim} ---`);
console.log('  anahtar    :', yaz('aDurum'), '·', yaz('aNot') || '(bos)');
console.log('  uyari      :', uyari.hidden
  ? 'gizli'
  : `[${uyari.className}] ${yaz('anahtarUyariBaslik')}`);
console.log('  guncelleme :', yaz('gSurum'), '·', yaz('gNot') || '(bos)');

const kusur = [];

if (eksik.length) kusur.push(`HTML'de olmayan oge: ${[...new Set(eksik)]}`);
if (cokme) kusur.push(`cokme: ${cokme.message}`);

// Dugmesi olup dinleyicisi olmayan bir kart, basildiginda hicbir sey
// yapmiyor ve sebebi hicbir yerde gorunmuyor.
for (const d of ['aKaydet', 'aDegistir', 'aGoster', 'gBak', 'gKur',
                 'anahtarUyariDugme', 'fabrika', 'hepsiniSil', 'wBagla']) {
  if (!dinleyici[d]) kusur.push(`dinleyicisi yok: #${d}`);
}

// Ilk cizim GERCEKTEN oldu mu? Modul hata vermeden yuklenip hicbir sey
// yazmamasi da bir kusur — o zaman panel bos acilirdi.
for (const d of ['aDurum', 'gSurum', 'dWifi', 'vUyku', 'ustAd']) {
  if (!yaz(d)) kusur.push(`ilk cizimde bos kaldi: #${d}`);
}

// 🔴 AG KOPUKKEN "ANAHTARIN BOZUK" DENMEZ.
//
// Ucu de robotu susturuyor ama annenin yapacagi sey bambaska: gecersiz
// anahtar degistirilir, dolu kota icin para yuklenir, kopuk ag icin
// HICBIR SEY yapilmaz. Kopuk agda kirmizi bir uyari cikarsa anne
// olmayan bir sorun icin Google'a para yukler.
if (takim === 'kopuk' && uyari.className.includes('agir')) {
  kusur.push('ag kopukken uyari AGIR (kirmizi) — anahtarin sucu degil');
}
// Kutudan yeni cikmis robotta anahtar hic yok: bu Pati'nin HIC
// konusamadigi hal ve gorulmesi sart.
if (takim === 'ilk' && uyari.hidden) {
  kusur.push('anahtar hic girilmemisken uyari gizli');
}
if (takim === 'normal' && !uyari.hidden) {
  kusur.push('her sey yolundayken uyari gorunuyor');
}

if (kusur.length) {
  console.log(kusur.map((k) => `  !! ${k}`).join('\n'));
  console.log('  KALDI\n');
  process.exit(1);
}
console.log('  gecti\n');
