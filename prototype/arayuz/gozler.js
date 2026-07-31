// -*- coding: utf-8 -*-
//
// Pati'nin gozleri — v1'in robot/web/gozler.js dosyasindan tasindi.
//
// NE DEGISTI, NEDEN
// -----------------
// 1. RENK: v1'de yesildi (#22c94a). Eilik turkuaz kullaniyor ve
//    PLAN.md hedefi bu: "parlak turkuaz, sade sekiller".
// 2. Ekran cercevesi: PLAN.mdb'nin en degerli bulgusu — Eilik'in
//    "siyah cam" gorunumu ekrandan degil ONUNDEKI FUME AKRILIKTEN
//    geliyor. Burada onu taklit ediyoruz ki gercek robotun nasil
//    duracagini simdiden gorelim (akrilik parcasi birkac TL, ama
//    yanlis ekran almak 500 TL).
// 3. Geri kalan ANIMASYON MANTIGI aynen korundu — sakkad, asimetrik
//    kirpma, squash & stretch, nefes. PLAN.md "goz animasyonu
//    mantigi tasinacak" diyor; tasinan sey tam olarak bu.
//
// v1'in yorumlarindaki iki ders (aynen gecerli):
//   - Goz kirpma hizi duyguya bagli bir eksen. Sabit araliklarla
//     kirpan goz "makine" gibi duruyor.
//   - Gercek goz kaymaz, SICRAR (sakkad): 80-140 ms'lik hizli atlama,
//     sonra sabit bekleme. Ustel yumusatma "robot gibi" duruyordu.
//   - Kirpma simetrik degil: kapanma hizli (~90 ms), acilma yavas.
//
// ESP32'YE NE GECER: sayilar ve mantik gecer, ciziim kodu gecmez.
// Orada canvas yok; ayni sekiller LovyanGFX/dogrudan cerceve tamponu
// ile cizilecek. Bu yuzden sekiller kasten BASIT: yuvarlatilmis
// dikdortgen + kirpma cokgeni. Bezier egrisi yok.

const RENK_KOYU   = '#17c4c4';   // turkuaz, koyu ucu
const RENK_ACIK   = '#5df2f2';   // turkuaz, acik ucu
const RENK_PARLAK = 'rgba(225, 255, 255, 0.85)';
const PARLAMA     = 'rgba(60, 230, 230, 0.55)';

const NOTR = {
  g: 1, y: 1, ustKapak: 0, altKapak: 0,
  egim: 0, altEgim: 0, kaydirX: 0, kaydirY: 0,
};

const D = (a, b) => [{ ...NOTR, ...a }, { ...NOTR, ...(b || a) }];

// Her duygu: [sol goz, sag goz] + davranis ayarlari
//   kirpmaHizi  : 1 = normal, buyuk = daha sik kirpar
//   hareketlilik: bakisin ne siklikta ve ne kadar genis dolastigi
//   egimKafa    : butun yuzun egilmesi
export const DURUMLAR = {
  bos: {
    goz: D({}), kirpmaHizi: 1.0, hareketlilik: 1.0, egimKafa: 0,
  },
  notr: {
    goz: D({}), kirpmaHizi: 1.0, hareketlilik: 1.0, egimKafa: 0,
  },
  dinliyor: {
    goz: D({ g: 1.05, y: 1.20 }), kirpmaHizi: 0.7, hareketlilik: 0.5, egimKafa: 0,
  },
  dusunuyor: {
    goz: D({ y: 0.84, ustKapak: 0.16 }),
    kirpmaHizi: 0.6, hareketlilik: 0.3, egimKafa: 0.05, bakis: [-0.55, -0.5],
  },
  konusuyor: {
    goz: D({ y: 1.02 }), kirpmaHizi: 1.2, hareketlilik: 1.3, egimKafa: 0,
  },

  mutlu: {
    goz: D({ y: 1.05, altKapak: 0.46 }),
    kirpmaHizi: 1.5, hareketlilik: 1.4, egimKafa: 0,
  },
  cok_mutlu: {
    goz: D({ g: 1.10, y: 1.16, altKapak: 0.56 }),
    kirpmaHizi: 2.0, hareketlilik: 1.8, egimKafa: 0,
  },

  saskin: {
    goz: D({ g: 1.12, y: 1.50 }),
    kirpmaHizi: 0.4, hareketlilik: 0.4, egimKafa: 0,
  },
  uzgun: {
    goz: D({ y: 0.78, ustKapak: 0.34, egim: -0.55, altKapak: 0.10, kaydirY: 0.12 }),
    kirpmaHizi: 0.6, hareketlilik: 0.35, egimKafa: 0, bakis: [0, 0.3],
  },

  // KIZGIN — v1 notu: ilk surumde egim 0.75 idi ve ifade "tehditkar"
  // oluyordu. Bir cocuk robotunda bu fazla sert. Goz daha buyuk
  // kaliyor, kaslar daha az catik.
  kizgin: {
    goz: D({ y: 0.88, ustKapak: 0.24, egim: 0.48, altEgim: 0.10 }),
    kirpmaHizi: 0.9, hareketlilik: 0.7, egimKafa: 0,
  },

  // SOMURTKAN — v1'in en degerli bulgusu:
  // Bir kizginligi TATLI yapan sey gozu KUCULTMEK degil, ALT kapagi
  // (yanagi) yukari kaldirmak. Ust kapak kasten cok dusuk (0.06),
  // kapanma ALTTAN geliyor; boylece goz kizgindan bile daha BUYUK
  // kaliyor. Sevimliligi koruyan sey bu.
  somurtkan: {
    goz: D({ g: 1.04, y: 1.06, ustKapak: 0.06, altKapak: 0.28,
             egim: 0.30, altEgim: -0.10 }),
    kirpmaHizi: 1.7, hareketlilik: 1.9, egimKafa: 0.09, bakis: [-0.3, 0.12],
  },
  uykulu: {
    goz: D({ y: 0.64, ustKapak: 0.58 }),
    kirpmaHizi: 0.35, hareketlilik: 0.2, egimKafa: 0.04, bakis: [0, 0.25],
  },
  'meraklı': {
    goz: D({ y: 1.22 }, { y: 0.86, ustKapak: 0.20, egim: -0.18 }),
    kirpmaHizi: 1.1, hareketlilik: 1.5, egimKafa: 0.13,
  },
  anlamadim: {
    goz: D({ y: 0.90, ustKapak: 0.20, egim: 0.20 },
           { y: 1.14, ustKapak: 0.02, egim: -0.22 }),
    kirpmaHizi: 1.3, hareketlilik: 1.2, egimKafa: 0.15,
  },
  afacan: {
    goz: D({ y: 0.70, altKapak: 0.44, egim: 0.26 }, { y: 1.10, ustKapak: 0.02 }),
    kirpmaHizi: 1.8, hareketlilik: 2.0, egimKafa: 0.10, bakis: [0.35, 0],
  },
  haylaz: {
    goz: D({ g: 1.04, y: 0.46, ustKapak: 0.30, altKapak: 0.16, egim: 0.30 }),
    kirpmaHizi: 1.6, hareketlilik: 2.2, egimKafa: 0.06,
  },
};

// Bir duruma girerken atilan kisa "vurgu" hareketi. Ifade degisiminde
// goz once biraz abartip sonra yerine oturuyor — gercek yuzler de
// boyle yapiyor.
const GIRIS_VURGUSU = {
  cok_mutlu: 0.22, saskin: 0.30, afacan: 0.18, haylaz: 0.20,
  somurtkan: 0.26, kizgin: 0.14, 'meraklı': 0.14, mutlu: 0.12,
};

const kolay      = (t) => t * t * (3 - 2 * t);
const cikisKolay = (t) => 1 - (1 - t) * (1 - t);
const girisKolay = (t) => t * t;


export class Gozler {
  constructor(tuval) {
    this.t = tuval;
    this.c = tuval.getContext('2d');
    this.g = 0;
    this.y = 0;

    this.durum = 'bos';
    this.tanim = DURUMLAR.bos;
    this.hedef = DURUMLAR.bos.goz.map((e) => ({ ...e }));
    this.simdi = DURUMLAR.bos.goz.map((e) => ({ ...e }));

    this.bakis = { x: 0, y: 0 };
    this.sakkad = { basX: 0, basY: 0, sonX: 0, sonY: 0, t: 1, sure: 0.12 };
    this.sonrakiSakkad = 0;

    this.kirpmaT = -1;
    this.kirpmaSure = 0.26;
    this.sonrakiKirpma = 0;
    this.ikinciKirpma = false;

    this.gerilme = 0;
    this.oncekiBakis = { x: 0, y: 0 };

    this.egim = 0;
    this.hedefEgim = 0;
    this.vurgu = 0;

    this.baslangic = performance.now();
    this.sonZaman = this.baslangic;

    new ResizeObserver(() => { this.#olcekle(); this.tekKare(); }).observe(tuval);
    this.#olcekle();
    this.tekKare();
    requestAnimationFrame((z) => this.#cerceve(z));
  }

  ayarla(durum) {
    const yeni = DURUMLAR[durum];
    if (!yeni || durum === this.durum) return;

    this.durum = durum;
    this.tanim = yeni;
    this.hedef = yeni.goz.map((e) => ({ ...e }));
    this.hedefEgim = yeni.egimKafa || 0;
    this.vurgu = GIRIS_VURGUSU[durum] || 0.08;

    const b = yeni.bakis || [0, 0];
    this.#sakkadBaslat(b[0], b[1], 0.13);
    this.sonrakiSakkad = performance.now() / 1000 + 0.9;

    // Ifade degisiminde bir kirpma cok dogal duruyor — insanlar da
    // yuz ifadesi degistirirken kirpar.
    if (Math.random() < 0.55) this.#kirpmaBaslat();
  }

  tekKare() {
    if (!this.g || !this.y) return;
    this.#ciz((performance.now() - this.baslangic) / 1000, 0);
  }

  // ------------------------------------------------------------------ ic

  #olcekle() {
    const g = this.t.clientWidth, y = this.t.clientHeight;
    if (!g || !y) return;
    if (g === this.g && y === this.y) return;
    const o = window.devicePixelRatio || 1;
    this.t.width = Math.round(g * o);
    this.t.height = Math.round(y * o);
    this.c.setTransform(o, 0, 0, o, 0, 0);
    this.g = g; this.y = y;
  }

  #sakkadBaslat(x, y, sure) {
    this.sakkad = {
      basX: this.bakis.x, basY: this.bakis.y,
      sonX: x, sonY: y,
      t: 0,
      sure: sure || (0.075 + Math.hypot(x - this.bakis.x, y - this.bakis.y) * 0.05),
    };
  }

  #kirpmaBaslat(cift) {
    if (this.kirpmaT >= 0) return;
    this.kirpmaT = 0;
    this.kirpmaSure = 0.20 + Math.random() * 0.09;
    this.ikinciKirpma = cift !== undefined ? cift : Math.random() < 0.18;
  }

  #kirpmaZamanla(simdi) {
    const hiz = this.tanim.kirpmaHizi || 1;
    this.sonrakiKirpma = simdi + (1.6 + Math.random() * 4.2) / hiz;
  }

  #sakkadZamanla(simdi) {
    const h = this.tanim.hareketlilik || 1;
    this.sonrakiSakkad = simdi + (0.9 + Math.random() * 2.6) / h;
  }

  #cerceve(zaman) {
    this.#olcekle();
    const simdi = zaman / 1000;
    const gecen = (zaman - this.baslangic) / 1000;
    let dt = (zaman - this.sonZaman) / 1000;
    this.sonZaman = zaman;
    dt = Math.min(dt, 0.05);

    const k = 1 - Math.pow(0.001, dt * 3.2);
    for (let i = 0; i < 2; i++) {
      for (const a of Object.keys(NOTR)) {
        this.simdi[i][a] += (this.hedef[i][a] - this.simdi[i][a]) * k;
      }
    }
    this.egim += (this.hedefEgim - this.egim) * (1 - Math.pow(0.001, dt * 2.4));
    this.vurgu *= Math.pow(0.02, dt);

    if (this.sakkad.t < 1) {
      this.sakkad.t = Math.min(1, this.sakkad.t + dt / this.sakkad.sure);
      const p = cikisKolay(this.sakkad.t);
      this.bakis.x = this.sakkad.basX + (this.sakkad.sonX - this.sakkad.basX) * p;
      this.bakis.y = this.sakkad.basY + (this.sakkad.sonY - this.sakkad.basY) * p;
    } else if (simdi > this.sonrakiSakkad) {
      const sabit = this.tanim.bakis;
      const yay = sabit ? 0.22 : 0.85;
      const mx = sabit ? sabit[0] : 0;
      const my = sabit ? sabit[1] : 0;
      this.#sakkadBaslat(
        mx + (Math.random() - 0.5) * 2 * yay,
        my + (Math.random() - 0.5) * yay * 0.55,
      );
      this.#sakkadZamanla(simdi);
    }

    // Mikro titreme: goz sabitken bile tamamen durmuyor.
    const tit = Math.sin(gecen * 21) * 0.006 + Math.sin(gecen * 13.7) * 0.004;

    const hiz = Math.hypot(this.bakis.x - this.oncekiBakis.x,
                           this.bakis.y - this.oncekiBakis.y) / Math.max(dt, 0.001);
    this.oncekiBakis.x = this.bakis.x;
    this.oncekiBakis.y = this.bakis.y;
    this.gerilme += (Math.min(hiz * 0.035, 0.28) - this.gerilme) *
                    (1 - Math.pow(0.001, dt * (hiz > 0.5 ? 8 : 3)));

    let kirp = 0;
    if (this.kirpmaT >= 0) {
      this.kirpmaT += dt / this.kirpmaSure;
      kirp = this.#kirpmaMiktari(this.kirpmaT);
      if (this.kirpmaT >= 1) {
        this.kirpmaT = -1;
        if (this.ikinciKirpma) {
          this.ikinciKirpma = false;
          this.kirpmaT = 0;
          this.kirpmaSure = 0.16;
        } else {
          this.#kirpmaZamanla(simdi);
        }
      }
    } else if (simdi > this.sonrakiKirpma) {
      this.#kirpmaBaslat();
    }

    this.#ciz(gecen, kirp, tit);
    requestAnimationFrame((z) => this.#cerceve(z));
  }

  // Gercek kirpma simetrik degil: kapanma hizli, acilma yavas.
  #kirpmaMiktari(t) {
    if (t < 0) return 0;
    if (t < 0.34) return girisKolay(t / 0.34);
    if (t < 0.44) return 1;
    if (t < 1) return 1 - kolay((t - 0.44) / 0.56);
    return 0;
  }

  #ciz(gecen, kirp, tit = 0) {
    const c = this.c, G = this.g, Y = this.y;
    if (!G || !Y) return;
    c.clearRect(0, 0, G, Y);

    const birim = Math.min(G, Y * 1.6);
    const tabanG = birim * 0.235;
    const tabanY = birim * 0.30;
    const aralik = birim * 0.085;

    // Nefes: fark edilmiyor ama olmayinca robot "olu" goruniyor.
    const nefes = 1 + Math.sin(gecen * 1.45) * 0.018;
    const nefesY = Math.sin(gecen * 1.45) * birim * 0.006;
    const pop = 1 + this.vurgu;

    const merkezX = G / 2;
    const merkezY = Y / 2 + nefesY;

    c.save();
    c.translate(merkezX, merkezY);
    c.rotate(this.egim * 0.20);
    c.translate(-merkezX, -merkezY);

    for (let i = 0; i < 2; i++) {
      const e = this.simdi[i];
      const yon = i === 0 ? -1 : 1;

      const gen = tabanG * e.g * pop * nefes * (1 + this.gerilme * 0.5);
      const yuk = tabanY * e.y * pop * nefes * (1 - this.gerilme * 0.28)
                  * (1 - kirp * 0.94);

      const x = merkezX + yon * (tabanG / 2 + aralik / 2)
                + (this.bakis.x + tit) * birim * 0.075
                + e.kaydirX * birim;
      const y = merkezY + (this.bakis.y + tit * 0.5) * birim * 0.055
                + e.kaydirY * birim;

      this.#gozCiz(x, y, gen, yuk, e, birim, kirp, yon);
    }

    c.restore();
  }

  #gozCiz(x, y, gen, yuk, e, birim, kirp, yon) {
    const c = this.c;
    const sol = x - gen / 2, ust = y - yuk / 2;
    const yaricap = Math.min(gen * 0.32, yuk / 2);

    const ustY = ust + yuk * e.ustKapak;
    const altY = ust + yuk * (1 - e.altKapak);

    // IC / DIS kenar hesabi — ifadenin dogru okunmasi buna bagli.
    // Sol gozun IC tarafi SAG noktasi, sag gozun IC tarafi SOL noktasi.
    // egim > 0 ic kenari asagi indirir (kizgin), < 0 yukari (uzgun).
    const kenar = (miktar) => {
      const yari = miktar * yuk / 2;
      return yon < 0 ? [-yari, yari] : [yari, -yari];
    };
    const [dUst, dUst2] = kenar(e.egim);
    const [dAlt, dAlt2] = kenar(e.altEgim);

    const egimUst = (dUst2 - dUst) / gen;
    const egimAlt = (dAlt2 - dAlt) / gen;

    const solUst = ustY + dUst - egimUst * gen;
    const sagUst = ustY + dUst2 + egimUst * gen;
    const solAlt = altY + dAlt - egimAlt * gen;
    const sagAlt = altY + dAlt2 + egimAlt * gen;

    c.save();

    c.beginPath();
    c.moveTo(sol - gen, solUst);
    c.lineTo(sol + gen * 2, sagUst);
    c.lineTo(sol + gen * 2, sagAlt);
    c.lineTo(sol - gen, solAlt);
    c.closePath();
    c.clip();

    c.shadowColor = PARLAMA;
    c.shadowBlur = birim * 0.055;

    const egimRenk = c.createLinearGradient(0, ust, 0, ust + yuk);
    egimRenk.addColorStop(0, RENK_ACIK);
    egimRenk.addColorStop(1, RENK_KOYU);
    c.fillStyle = egimRenk;

    c.beginPath();
    c.roundRect(sol, ust, gen, Math.max(yuk, 1), yaricap);
    c.fill();

    // Cam parlamasi — bakisla hafifce kayiyor, isik gozun uzerinde
    // sabit duruyormus hissi veriyor.
    if (kirp < 0.5 && yuk > birim * 0.06) {
      c.shadowBlur = 0;
      c.fillStyle = RENK_PARLAK;
      const px = sol + gen * (0.14 - this.bakis.x * 0.04);
      c.beginPath();
      c.roundRect(px, ust + yuk * 0.10, gen * 0.26,
                  Math.max(yuk * 0.20, 2), yaricap * 0.5);
      c.fill();
    }

    c.restore();
  }
}
