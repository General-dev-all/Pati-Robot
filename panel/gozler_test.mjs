// Cizim motorunun testi — tarayici olmadan.
//
// NEDEN: bu kod C++'a gececek. "Tarayicida guzel gorunuyor" bir test
// degil; sinirdan tasan piksel, bos kalan kare, ters kapak hesabi
// gozle gorulmez. Burada sayiyla bakiliyor.

import { performance } from 'node:perf_hooks';

class ImageData {
  constructor(g, y) {
    this.width = g; this.height = y;
    this.data = new Uint8ClampedArray(g * y * 4);
  }
}
globalThis.ImageData = ImageData;
globalThis.performance = performance;
globalThis.window = { devicePixelRatio: 1 };

// Dongu kendiliginden donmesin: kareleri elle suruyoruz.
let sira = [];
globalThis.requestAnimationFrame = (f) => { sira.push(f); return sira.length; };

// Dinamik import — statik `import` hoist edilip yukaridaki taklitler
// kurulmadan calisirdi. Yol da dosyaya gore, mutlak degil.
const { Gozler240, Tampon, DURUMLAR, AYAR, EKRAN, butce } =
  await import(new URL('./gozler240.js', import.meta.url));

let sonKare = null;
const tuval = {
  width: 0, height: 0,
  getContext: () => ({ putImageData: (v) => { sonKare = v; } }),
};

let hata = 0;
const bak = (kosul, yazi) => {
  if (!kosul) { console.log('  HATA  ' + yazi); hata++; }
};

const g = new Gozler240(tuval);
let t = 0;
const BOSLUK = 16.7;

function sur(kare = 1) {
  for (let i = 0; i < kare; i++) {
    t += BOSLUK;
    const f = sira.shift();
    if (f) f(t);
  }
}

// Bir ifadeye gec ve yumusatmanin oturmasini bekle.
function otur(ad, kare = 90) {
  g.durum = null;
  g.ayarla(ad);
  sur(kare);
}

// Dolu pikselleri dikeyde tara — goz yuksekligi olcusu.
function doluYukseklik(d) {
  let ust = -1, alt = -1;
  for (let y = 0; y < EKRAN.y; y++) {
    let dolu = false;
    for (let x = 0; x < EKRAN.g; x++) {
      if (d[(y * EKRAN.g + x) * 4 + 1] > 30) { dolu = true; break; }
    }
    if (dolu) { if (ust < 0) ust = y; alt = y; }
  }
  return ust < 0 ? 0 : alt - ust + 1;
}

// Kenar sutunlarina degen satir sayisi — tasma kaniti.
function kenarDegen(d) {
  let sol = 0, sag = 0;
  for (let y = 0; y < EKRAN.y; y++) {
    if (d[(y * EKRAN.g) * 4 + 1] > 20) sol++;
    if (d[(y * EKRAN.g + EKRAN.g - 1) * 4 + 1] > 20) sag++;
  }
  return { sol, sag };
}


console.log('\n=== 1. Tampon sinirlari ===');
{
  // Kasten ekranin disina tasan sekil: tamponun disina yazilmamali.
  const T = new Tampon();
  T.temizle();
  const acik = { ustA: -1e6, ustB: 0, altA: 1e6, altB: 0 };
  T.yuvarlakDoldur(-80, -80, 400, 400, 20, acik, () => [255, 0, 0], 1);
  bak(T.dokunulan === EKRAN.g * EKRAN.y,
      `tasan sekil tam ekrani doldurmali, ${T.dokunulan} piksel doldu`);
  bak(T.p.length === EKRAN.g * EKRAN.y * 4, 'tampon boyu degismemeli');
  console.log(`   tasan sekil -> ${T.dokunulan} piksel (beklenen ${EKRAN.g * EKRAN.y})`);
}

console.log('\n=== 2. Her ifade bir sey ciziyor mu ===');
const sayim = {};
for (const ad of Object.keys(DURUMLAR)) {
  otur(ad);
  const p = g.olcum.piksel;
  sayim[ad] = p;
  bak(p > 500, `${ad}: sadece ${p} piksel — goz cizilmemis olabilir`);
  bak(p < EKRAN.g * EKRAN.y * 3,
      `${ad}: ${p} piksel — ayni pikselin uzerinden fazla gecilmis`);
}
console.log('\n   ifade            piksel/kare   cizim yuku');
for (const [ad, p] of Object.entries(sayim).sort((a, b) => b[1] - a[1])) {
  console.log(`   ${ad.padEnd(16)} ${String(p).padStart(8)}   %${butce(p, 30).cizimYuzde.toFixed(0)}`);
}

console.log('\n=== 3. Ekrani tasan ifade var mi ===');
for (const ad of ['saskin', 'cok_mutlu', 'somurtkan', 'haylaz', 'dinliyor']) {
  otur(ad, 120);
  let enKotu = { sol: 0, sag: 0 };
  // Sakkad gozu kenara dogru goturebilir: bir sure izle.
  for (let i = 0; i < 300; i++) {
    sur();
    const k = kenarDegen(sonKare.data);
    if (k.sol + k.sag > enKotu.sol + enKotu.sag) enKotu = k;
  }
  bak(enKotu.sol === 0 && enKotu.sag === 0,
      `${ad}: ekran kenarina degiyor (sol ${enKotu.sol}, sag ${enKotu.sag} satir)`);
  console.log(`   ${ad.padEnd(12)} kenar temas: sol ${enKotu.sol}, sag ${enKotu.sag}`);
}

console.log('\n=== 4. Kirpma gercekten kapatiyor mu ===');
{
  otur('notr');
  g.sonrakiKirpma = 0;              // kirpmayi zorla
  let enAz = Infinity, enCok = 0;
  for (let i = 0; i < 40; i++) {
    sur();
    const y = doluYukseklik(sonKare.data);
    enAz = Math.min(enAz, y);
    enCok = Math.max(enCok, y);
  }
  bak(enCok > 40, `kirpma disinda goz yuksekligi ${enCok} px olmali (>40)`);
  bak(enAz < enCok * 0.5, `kirpma gozu kapatmali: ${enAz} .. ${enCok} px`);
  console.log(`   goz yuksekligi kirpma boyunca: ${enAz} .. ${enCok} px`);
}

console.log('\n=== 5. Kapak egimi dogru yone mi ===');
{
  // Kizgin: ic kenar (ortaya bakan) ASAGI inmeli, yani gozun ic
  // tarafi dis tarafindan daha alcak baslamali. Uzgun tam tersi.
  // Ters yazilirsa ifade tamamen degisiyor ve gozle fark edilmiyor —
  // bu yuzden sayiyla bakiliyor.
  const ustSinir = (d, x) => {
    for (let y = 0; y < EKRAN.y; y++) {
      if (d[(y * EKRAN.g + x) * 4 + 1] > 60) return y;
    }
    return EKRAN.y;
  };
  // Sol gozun dis (sol) ve ic (sag) kenarina yakin iki sutun
  const disX = 44, icX = 96;

  for (const [ad, beklenen] of [['kizgin', 'ic asagi'], ['uzgun', 'ic yukari']]) {
    otur(ad, 150);
    // Sakkadin etkisini azaltmak icin bakisi sifirla ve tek kare al
    g.bakis = { x: 0, y: 0 };
    g.sakkad.t = 1;
    sur(2);
    const d = sonKare.data;
    const dis = ustSinir(d, disX);
    const ic = ustSinir(d, icX);
    if (ad === 'kizgin') {
      bak(ic > dis, `kizgin: ic kenar asagi inmeli (dis ${dis}, ic ${ic})`);
    } else {
      bak(ic < dis, `uzgun: ic kenar yukari cikmali (dis ${dis}, ic ${ic})`);
    }
    console.log(`   ${ad.padEnd(8)} ust sinir: dis ${dis}, ic ${ic}  (${beklenen})`);
  }
}

console.log('\n=== 6. RGB565 gercekten daraltiyor mu ===');
{
  const onceki = AYAR.rgb565;

  AYAR.rgb565 = false;
  otur('notr');
  const y24 = new Set();
  for (let i = 1; i < sonKare.data.length; i += 4) y24.add(sonKare.data[i]);

  AYAR.rgb565 = true;
  sur(40);
  const y16 = new Set();
  for (let i = 1; i < sonKare.data.length; i += 4) y16.add(sonKare.data[i]);

  bak(y16.size < y24.size,
      `565 renk sayisini azaltmali: 24 bit ${y24.size}, 565 ${y16.size}`);
  console.log(`   farkli yesil tonu: 24 bit ${y24.size} -> RGB565 ${y16.size}`);
  AYAR.rgb565 = onceki;
}

console.log('\n=== 7. Parlama katmani maliyeti ===');
{
  const onceki = AYAR.parlamaKat;
  const olc = (k) => { AYAR.parlamaKat = k; otur('konusuyor', 60); return g.olcum.piksel; };
  const p0 = olc(0), p3 = olc(3), p6 = olc(6);
  console.log(`   0 katman ${p0} · 3 katman ${p3} · 6 katman ${p6} piksel`);
  bak(p0 < p3 && p3 < p6, 'katman artinca piksel de artmali');
  console.log(`   6 katmanda cizim yuku %${butce(p6, 30).cizimYuzde.toFixed(0)}`);
  AYAR.parlamaKat = onceki;
}

console.log('\n=== 8. SPI darbogazi ===');
{
  const b = butce(20000, 30);
  console.log(`   tam ekran @${EKRAN.spiMhz} MHz = ${b.spiMs.toFixed(1)} ms`);
  console.log(`   SPI'nin izin verdigi en yuksek fps = ${b.enFazlaFps.toFixed(0)}`);
  console.log(`   30 fps hedefinde SPI yuku = %${b.spiYuzde.toFixed(0)}`);
  bak(b.enFazlaFps > 30, "SPI 30 fps'e yetmiyor — kismi guncelleme SART");
}

console.log('\n' + '='.repeat(50));
console.log(hata === 0 ? '  TUM TESTLER GECTI' : `  ${hata} TEST BASARISIZ`);
console.log('='.repeat(50) + '\n');
process.exit(hata ? 1 : 0);
