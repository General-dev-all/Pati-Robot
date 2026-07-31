// -*- coding: utf-8 -*-
//
// Tarayici goz motorunun karelerini dosyaya doker.
//
// goz_karsilastir.cpp bunlari C++ motorunun urettikleriyle piksel
// piksel karsilastiriyor. Ayrintili gerekce o dosyanin basinda.
//
// Kullanim:  cd firmware && node test/goz_js_dok.mjs

import { writeFileSync } from 'node:fs';
import { performance } from 'node:perf_hooks';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const KOK = dirname(fileURLToPath(import.meta.url));

class ImageData {
  constructor(g, y) {
    this.width = g; this.height = y;
    this.data = new Uint8ClampedArray(g * y * 4);
  }
}
globalThis.ImageData = ImageData;
globalThis.performance = performance;
globalThis.window = { devicePixelRatio: 1 };
// Dongu calismasin: kareleri testKaresi() elle cizecek.
globalThis.requestAnimationFrame = () => 0;

const { Gozler240, DURUMLAR, EKRAN } =
  await import(new URL('../../panel/gozler240.js', import.meta.url));

const tuval = {
  width: 0, height: 0,
  getContext: () => ({ putImageData: () => {} }),
};

const gozler = new Gozler240(tuval);
const adlar = Object.keys(DURUMLAR);

// RGBA -> RGB. C++ tarafi RGB888 tutuyor; alfa kanali ikisinde de
// sabit 255, karsilastirmaya bir sey katmiyor.
const parcalar = [];
for (const ad of adlar) {
  const veri = gozler.testKaresi(ad);
  if (!veri) {
    console.log(`  HATA  ${ad} cizilemedi`);
    process.exit(1);
  }
  const rgb = Buffer.alloc(EKRAN.g * EKRAN.y * 3);
  for (let i = 0, k = 0; i < veri.data.length; i += 4, k += 3) {
    rgb[k] = veri.data[i];
    rgb[k + 1] = veri.data[i + 1];
    rgb[k + 2] = veri.data[i + 2];
  }
  parcalar.push(rgb);
}

const hedef = join(KOK, 'js_kareler.ham');
writeFileSync(hedef, Buffer.concat(parcalar));
console.log(`  yazildi: test/js_kareler.ham`);
console.log(`  ${adlar.length} kare x ${EKRAN.g}x${EKRAN.y} RGB888 ` +
            `= ${Buffer.concat(parcalar).length} bayt`);
