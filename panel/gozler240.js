// -*- coding: utf-8 -*-
//
// Pati'nin gozleri — 240x135 ST7789P3 icin (M5Stack StickS3).
//
// ⚠️ DOSYA ADINDAKI "240" GENISLIGI ANLATIYOR, KARE EKRANI DEGIL.
// Onceki kartta ekran 240x240'ti; StickS3'te panel 135x240 (dikey) ama
// Pati onu YATAY kullaniyor, yani 240x135. Genislik degismedi, o yuzden
// gozlerin yatay olculeri de degismedi — yalnizca dikey daraldi.
// Ad korundu cunku firmware onu gomulu dosya olarak adiyla tanıyor.
//
// ===========================================================================
// BU DOSYA NEDEN VAR — prototype/arayuz/gozler.js zaten calisiyordu
// ===========================================================================
//
// Iki sebep, ikisi de "begenilen sey teslim edilemez" riskini kesiyor:
//
// 1. ESKI DOSYA GENIS TUVAL VARSAYIYOR.
//    Orada olcu birimi `Math.min(G, Y * 1.6)` idi — yani yatay bir
//    pencere icin ayarlanmis. 240x240 KARE ekranda ayni sayilar gozleri
//    ekranin ortasinda kucuk bir seride birakiyor, ust ve alt ucta
//    kocaman siyah bosluk kaliyor. Ayni mantik, yanlis kadraj.
//
// 2. TARAYICI, ESP32'NIN YAPAMADIGI SEYLERI BEDAVA YAPIYOR.
//    Eski dosya `shadowBlur` (gercek bulanik parlama), `clip()`,
//    `createLinearGradient` ve `rotate()` kullaniyor. ST7789'a cerceve
//    tamponu yazarken bunlarin hicbiri yok. Orada gordugunu begenip
//    sonra ekranda daha kotusunu vermek, v1'in tam olarak battigi
//    hataydi (PLAN.md: "hizli makinede olculdu, hedef cihazda hic
//    denenmedi").
//
// COZUM: bu dosya tarayicinin cizim yeteneklerini KULLANMIYOR.
// 240x240 piksel tamponuna kendi elimizle yaziyor — satir satir,
// tamsayi piksel, kendi alfa karistirmasi. Yani burada gorulen sey
// C++'in yapmasi gereken isin birebir taslagi. Tuval sadece hazir
// tamponu ekrana basmak icin (`putImageData`) kullaniliyor.
//
// ===========================================================================
// ESKI DOSYADAN NE KORUNDU
// ===========================================================================
//
// ANIMASYON MANTIGI VE SAYILARI aynen korundu, cunku onlar zaten
// tarayiciya bagli degildi ve dogru duruyordu:
//   - sakkad (goz kaymaz, SICRAR: 80-140 ms atlama sonra bekleme)
//   - asimetrik kirpma (kapanma hizli ~90 ms, acilma yavas)
//   - nefes, mikro titreme, squash & stretch
//   - DURUMLAR tablosu: v1'de deneyerek bulunmus ifadeler
//
// Ifade listesi prototype/yuz.py IFADELER ile ayni olmali — model oradan
// secim yapiyor. Burada fazlasi var (akis durumlari: bos/dinliyor/
// dusunuyor/konusuyor); onlari model degil biz suruyoruz.

// ---------------------------------------------------------------------------
// EKRAN — gercek sayilar, tahmin degil
// ---------------------------------------------------------------------------
//
// 1.14" kosegen panel, 135x240 piksel. Kosegen 28,96 mm; en-boy
// oraniyla kisa kenar 14,2 mm, uzun kenar 25,2 mm.
//
// Pati YATAY kullaniyor: uzun kenar genislik oluyor.
export const EKRAN = {
  g: 240,
  y: 135,
  mm: 25.2,           // aktif alanin genisligi (uzun kenar)
  spiMhz: 40,         // ST7789 guvenli hizi; 80'e cikarilabiliyor
};

export const PIKSEL_MM = EKRAN.g / EKRAN.mm;   // ~10,3 px/mm

// ---------------------------------------------------------------------------
// AYAR — stuudyodaki kaydiricilar bunu degistiriyor
// ---------------------------------------------------------------------------
//
// Hepsi PIKSEL. Sebep: C++'a birebir gececek. Oran kullansak
// ("ekranin %31'i") ESP32'de her karede carpma yapmak gerekirdi ve
// yuvarlama farki iki taraf arasinda goruntu kaymasi uretirdi.
// ---------------------------------------------------------------------------
// 🔴 BU SAYILAR 240x135 ICIN YENIDEN OLCULDU (23.08.2026)
// ---------------------------------------------------------------------------
//
// Onceki kart 240x240'ti ve degerler soyleydi:
//   gozG 76 · gozY 96 · aralik 26 · yaricap 26 · merkezY 120
//   bakisY 10 · parlamaKalinlik 5
//
// Ekran 240x135 olunca YATAY hicbir sey degismek zorunda degildi ama
// DIKEY yer neredeyse yariya indi. Sadece merkezY'yi 67 yapmak
// yetmiyor, cunku ifadeler gozu OLCEKLIYOR: en buyugu `saskin`, taban
// yuksekligi 1,50 ile carpiyor. Ustune parlama katmanlari biniyor.
//
// Degerler tahminle degil OLCULEREK secildi: 16 ifadenin hepsi
// cizilip en ust ve en alt dolu piksel bulundu, ustune en buyuk bakis
// kaymasi eklendi. Secilen takim, hicbir ifadenin kenara DEGMEDIGI en
// buyuk goz:
//
//   gozY 76 -> 6 piksel tasiyor
//   gozY 72 -> 3 piksel tasiyor
//   gozY 68 -> tam kenarda (pay 0)
//   gozY 64 -> pay 3/4 piksel   <-- secilen
//
// Gozler ekranin %68'ini (164/240) kapliyor, dinlenme halinde
// yuksekligin %47'sini.
//
// ⚠️ Bunlar GORUNUM kararlari ve tek dogrusu yok. panel/studyo.html
// kaydiricilarla canli deneme icin duruyor; begenilen degerler buraya
// yazilip `node goz_uret.mjs` calistirilinca firmware'e geciyor.
export const AYAR = {
  gozG: 68,           // goz taban genisligi
  gozY: 64,           // goz taban yuksekligi
  aralik: 28,         // iki goz arasi bosluk
  yaricap: 22,        // kose yaricapi
  merkezY: 67,        // gozlerin dikey merkezi (135 / 2)

  bakisX: 14,         // en fazla yatay bakis kaymasi
  bakisY: 7,          // en fazla dikey bakis kaymasi (240x240'ta 10'du)

  // Parlama: gercek bulanik golge ESP32'de YOK. Onun yerine ana
  // seklin biraz buyugu, dusuk alfayla, birkac kat halinde ciziliyor.
  // Bedeli katman basina ~birkac bin piksel — olculebilir, karsilanabilir.
  //
  // Kalinlik 5'ten 3'e indi. Sebep once yer: 5'te parlama her yonde 15
  // piksel tasiyor ve 135 piksellik bir ekranda bu, yuksekligin
  // %11'i — gozun kendisinden calinan yer. Yan faydasi islemci:
  // harmanlanan piksel sayisi belirgin dusuyor ve o islemci ses
  // cozumune kaliyor.
  parlamaKat: 3,
  parlamaKalinlik: 3,
  parlamaAlfa: 0.16,

  camParlamasi: true, // gozun ust solundaki isik lekesi
  egimRenk: true,     // dikey renk gecisi (satir basina bir renk: bedava)
  kenarYumusatma: true,
  rgb565: true,       // ekranin gercek renk derinligi
  kafaEgimi: 1.0,     // goz basina dikey kaydirma carpani (bkz. asagi)
};

// Turkuaz — prototype/arayuz/gozler.js ile ayni iki uc.
const ACIK   = [0x5d, 0xf2, 0xf2];
const KOYU   = [0x17, 0xc4, 0xc4];
const PARLAK = [0xe1, 0xff, 0xff];

// C++ uretecinin okumasi icin disa aciliyor (firmware/goz_uret.mjs).
// Elle kopyalanmalari yasak: iki tarafta iki farkli turkuaz olursa
// tarayicida begenilen renk ekranda baska cikar ve sebebi aranmaz.
export const RENKLER = { ACIK, KOYU, PARLAK };

// ---------------------------------------------------------------------------
// DURUMLAR — v1'den, degistirilmedi
// ---------------------------------------------------------------------------

const NOTR = {
  g: 1, y: 1, ustKapak: 0, altKapak: 0,
  egim: 0, altEgim: 0, kaydirX: 0, kaydirY: 0,
};

const D = (a, b) => [{ ...NOTR, ...a }, { ...NOTR, ...(b || a) }];

export const DURUMLAR = {
  bos:        { goz: D({}), kirpmaHizi: 1.0, hareketlilik: 1.0, egimKafa: 0 },
  notr:       { goz: D({}), kirpmaHizi: 1.0, hareketlilik: 1.0, egimKafa: 0 },
  dinliyor:   { goz: D({ g: 1.05, y: 1.20 }), kirpmaHizi: 0.7, hareketlilik: 0.5, egimKafa: 0 },
  dusunuyor:  { goz: D({ y: 0.84, ustKapak: 0.16 }),
                kirpmaHizi: 0.6, hareketlilik: 0.3, egimKafa: 0.05, bakis: [-0.55, -0.5] },
  konusuyor:  { goz: D({ y: 1.02 }), kirpmaHizi: 1.2, hareketlilik: 1.3, egimKafa: 0 },

  mutlu:      { goz: D({ y: 1.05, altKapak: 0.46 }),
                kirpmaHizi: 1.5, hareketlilik: 1.4, egimKafa: 0 },
  cok_mutlu:  { goz: D({ g: 1.10, y: 1.16, altKapak: 0.56 }),
                kirpmaHizi: 2.0, hareketlilik: 1.8, egimKafa: 0 },
  saskin:     { goz: D({ g: 1.12, y: 1.50 }),
                kirpmaHizi: 0.4, hareketlilik: 0.4, egimKafa: 0 },
  uzgun:      { goz: D({ y: 0.78, ustKapak: 0.34, egim: -0.55, altKapak: 0.10, kaydirY: 0.12 }),
                kirpmaHizi: 0.6, hareketlilik: 0.35, egimKafa: 0, bakis: [0, 0.3] },

  // v1 notu: ilk surumde egim 0.75 idi, ifade "tehditkar" oluyordu.
  // Cocuk robotunda fazla sert. Goz buyuk kaliyor, kaslar az catik.
  kizgin:     { goz: D({ y: 0.88, ustKapak: 0.24, egim: 0.48, altEgim: 0.10 }),
                kirpmaHizi: 0.9, hareketlilik: 0.7, egimKafa: 0 },

  // v1'in en degerli bulgusu: bir kizginligi TATLI yapan sey gozu
  // kucultmek degil, ALT kapagi (yanagi) yukari kaldirmak. Ust kapak
  // kasten cok dusuk; kapanma ALTTAN geliyor, goz kizgindan bile daha
  // BUYUK kaliyor. Sevimliligi koruyan sey bu.
  somurtkan:  { goz: D({ g: 1.04, y: 1.06, ustKapak: 0.06, altKapak: 0.28,
                         egim: 0.30, altEgim: -0.10 }),
                kirpmaHizi: 1.7, hareketlilik: 1.9, egimKafa: 0.09, bakis: [-0.3, 0.12] },

  // 🔴 31.07.2026'da DEGISTI. Once { y: 0.64, ustKapak: 0.58 } idi ve
  // telefondan bakan ebeveyn "uykudan cok sikilmis gibi" dedi —
  // hakliydi: yarim ay sekli acik bir goze benziyor, kapali goze degil.
  //
  // Dort aday cizdirilip karsilastirildi (tahminle degil, goruntuyle):
  //   yari kapali  -> hala acik goz, "sikilmis"
  //   daha kapali  -> kucuk ama yine acik goz
  //   ince yarik   -> SECILEN: kapali goz gibi okunuyor
  //   asagi kavis  -> iki goz ters egilince "kurnaz/sinirli" oluyor
  //
  // altKapak da eklendi: sadece ustten kapatmak sekli asagi itiyor,
  // iki taraftan sikmak onu ORTADA ince bir bant yapiyor.
  //
  // Ilk denemede 0.46/0.84/0.10 kondu ve gercek ekranda "biraz fazla
  // kapanik" geldi — sac teli gibi bir cizgi. Dort kademe daha
  // cizdirilip bakildi; buradaki bir tik acigi. Sonraki kademeler
  // (0.54 ve 0.58) yeniden eski yarim aya, yani ACIK goze donuyor.
  uykulu:     { goz: D({ y: 0.50, ustKapak: 0.79, altKapak: 0.08 }),
                kirpmaHizi: 0.2, hareketlilik: 0.1, egimKafa: 0.05, bakis: [0, 0.5] },
  'meraklı':  { goz: D({ y: 1.22 }, { y: 0.86, ustKapak: 0.20, egim: -0.18 }),
                kirpmaHizi: 1.1, hareketlilik: 1.5, egimKafa: 0.13 },
  anlamadim:  { goz: D({ y: 0.90, ustKapak: 0.20, egim: 0.20 },
                       { y: 1.14, ustKapak: 0.02, egim: -0.22 }),
                kirpmaHizi: 1.3, hareketlilik: 1.2, egimKafa: 0.15 },
  afacan:     { goz: D({ y: 0.70, altKapak: 0.44, egim: 0.26 }, { y: 1.10, ustKapak: 0.02 }),
                kirpmaHizi: 1.8, hareketlilik: 2.0, egimKafa: 0.10, bakis: [0.35, 0] },
  haylaz:     { goz: D({ g: 1.04, y: 0.46, ustKapak: 0.30, altKapak: 0.16, egim: 0.30 }),
                kirpmaHizi: 1.6, hareketlilik: 2.2, egimKafa: 0.06 },
};

// Bir duruma girerken atilan kisa "vurgu": goz once biraz abartip
// sonra yerine oturuyor. Gercek yuzler de boyle yapiyor.
export const GIRIS_VURGUSU = {
  cok_mutlu: 0.22, saskin: 0.30, afacan: 0.18, haylaz: 0.20,
  somurtkan: 0.26, kizgin: 0.14, 'meraklı': 0.14, mutlu: 0.12,
};

// Listede olmayan durumlar icin varsayilan vurgu. C++ tarafinin da ayni
// sayiyi kullanmasi gerekiyor, o yuzden sabit burada duruyor.
export const GIRIS_VURGUSU_VARSAYILAN = 0.08;

const kolay      = (t) => t * t * (3 - 2 * t);
const cikisKolay = (t) => 1 - (1 - t) * (1 - t);
const girisKolay = (t) => t * t;


// ===========================================================================
// TAMPON — ESP32'de ne varsa o
// ===========================================================================
//
// 240 x 240 x 2 bayt = 115 KB. PSRAM'de (8 MB) rahat duruyor; ic RAM'de
// (512 KB) durabilir ama TLS tamponlariyla yarisirdi.
//
// Burada RGBA8888 tutuluyor cunku tarayici boyle basiyor. RGB565'e
// cevirme SON adimda, tam ekranin uzerinden bir kez geciliyor —
// gercekte de oyle olacak (ya da dogrudan 565 yazilacak).
export class Tampon {
  constructor(g = EKRAN.g, y = EKRAN.y) {
    this.g = g;
    this.y = y;
    this.veri = new ImageData(g, y);
    this.p = this.veri.data;
    // Her karede kac piksele DOKUNDUK — ESP32 butcesinin tek dogru
    // olcusu bu. Milisaniye tarayiciya ait, piksel sayisi tasinabilir.
    this.dokunulan = 0;
  }

  // Ekranin bos hali GERCEKTEN siyah — seffaf degil. Onemli: fume
  // akrilik panelsi bu siyahin uzerine biniyor, yani akriligin
  // gercekten ne kadar isi kurtardigi ancak boyle gorulur.
  temizle() {
    this.p.fill(0);
    for (let i = 3; i < this.p.length; i += 4) this.p[i] = 255;
    this.dokunulan = 0;
  }

  karistir(x, y, r, g, b, a) {
    const i = (y * this.g + x) * 4;
    const p = this.p;
    if (a >= 1) {
      p[i] = r; p[i + 1] = g; p[i + 2] = b;
    } else {
      const t = 1 - a;
      p[i]     = r * a + p[i]     * t;
      p[i + 1] = g * a + p[i + 1] * t;
      p[i + 2] = b * a + p[i + 2] * t;
    }
    this.dokunulan++;
  }

  // -------------------------------------------------------------------------
  // Ana cizim: goz kapaklariyla kirpilmis yuvarlatilmis dikdortgen
  // -------------------------------------------------------------------------
  //
  // Kapaklar EGIK cizgiler (kizgin/uzgun ifadesi buradan geliyor):
  //     ust kapak  -> piksel gorunur ise  y >= ustA + ustB * x
  //     alt kapak  -> piksel gorunur ise  y <= altA + altB * x
  //
  // Egik cizgiyi piksel piksel test etmek yerine, her SATIR icin x
  // sinirina cevriliyor. Boylece satir basina sabit is: bir karekok
  // (kose) + iki bolme (kapaklar). C++'da sabit noktali aritmetikle
  // aynen yazilabilir.
  yuvarlakDoldur(x0, y0, gen, yuk, r, kapak, renkAl, alfa) {
    if (gen <= 0 || yuk <= 0) return;
    r = Math.min(r, gen / 2, yuk / 2);

    const y1 = y0 + yuk;
    const ustR = y0 + r;
    const altR = y1 - r;

    const bas = Math.max(0, Math.floor(y0));
    const son = Math.min(this.y - 1, Math.ceil(y1) - 1);

    for (let y = bas; y <= son; y++) {
      const ym = y + 0.5;

      // --- kose girintisi
      let girinti = 0;
      if (ym < ustR) {
        const d = ustR - ym;
        girinti = r - Math.sqrt(Math.max(0, r * r - d * d));
      } else if (ym > altR) {
        const d = ym - altR;
        girinti = r - Math.sqrt(Math.max(0, r * r - d * d));
      }

      let L = x0 + girinti;
      let R = x0 + gen - girinti;

      // --- ust kapak:  ym >= ustA + ustB * x
      if (kapak.ustB === 0) {
        if (ym < kapak.ustA) continue;
      } else {
        const x = (ym - kapak.ustA) / kapak.ustB;
        if (kapak.ustB > 0) R = Math.min(R, x); else L = Math.max(L, x);
      }

      // --- alt kapak:  ym <= altA + altB * x
      if (kapak.altB === 0) {
        if (ym > kapak.altA) continue;
      } else {
        const x = (ym - kapak.altA) / kapak.altB;
        if (kapak.altB > 0) L = Math.max(L, x); else R = Math.min(R, x);
      }

      if (R <= L) continue;

      const [rr, gg, bb] = renkAl(y);

      const xb = Math.max(0, Math.floor(L));
      const xs = Math.min(this.g - 1, Math.ceil(R) - 1);

      for (let x = xb; x <= xs; x++) {
        let k = 1;
        if (AYAR.kenarYumusatma) {
          // Kapsama: pikselin ne kadari sekil icinde. Satir basina
          // sadece iki kenar pikseli 1'den kucuk cikiyor — ESP32'de
          // bunu yapmak icin de zaten sadece o iki pikseli
          // karistirmak yeterli, ortadakiler duz yaziliyor.
          k = Math.min(R, x + 1) - Math.max(L, x);
          if (k <= 0) continue;
          if (k > 1) k = 1;
        } else if (x + 0.5 < L || x + 0.5 > R) {
          continue;
        }
        this.karistir(x, y, rr, gg, bb, alfa * k);
      }
    }
  }

  // RGB565: 5 bit kirmizi, 6 bit yesil, 5 bit mavi.
  //
  // NEDEN ONIZLEMEDE DE UYGULANIYOR: turkuaz gecisimiz 24 bitte
  // puruzsuz, 565'te BANTLANIYOR. Bant gorulmeden "gecis guzel"
  // denirse, ekran gelince surpriz olur. Surprizi simdi yasamak
  // bedava.
  rgb565Uygula() {
    const p = this.p;
    for (let i = 0; i < p.length; i += 4) {
      p[i]     = (p[i]     & 0xf8) | (p[i]     >> 5);
      p[i + 1] = (p[i + 1] & 0xfc) | (p[i + 1] >> 6);
      p[i + 2] = (p[i + 2] & 0xf8) | (p[i + 2] >> 5);
    }
  }

  bas(ctx) {
    if (AYAR.rgb565) this.rgb565Uygula();
    ctx.putImageData(this.veri, 0, 0);
  }
}


// ===========================================================================
// GOZLER
// ===========================================================================

export class Gozler240 {
  constructor(tuval) {
    this.tuval = tuval;
    tuval.width = EKRAN.g;
    tuval.height = EKRAN.y;
    this.ctx = tuval.getContext('2d', { willReadFrequently: true });
    this.tampon = new Tampon();

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

    // Rapor: son karede kac piksele dokunuldu, kac kare/sn.
    this.olcum = { piksel: 0, fps: 0, ms: 0 };
    this._kare = 0;
    this._fpsZaman = this.baslangic;

    this.calisiyor = true;
    requestAnimationFrame((z) => this.#dongu(z));
  }

  ayarla(durum) {
    const yeni = DURUMLAR[durum];
    if (!yeni || durum === this.durum) return;

    this.durum = durum;
    this.tanim = yeni;
    this.hedef = yeni.goz.map((e) => ({ ...e }));
    this.hedefEgim = yeni.egimKafa || 0;
    this.vurgu = GIRIS_VURGUSU[durum] || GIRIS_VURGUSU_VARSAYILAN;

    const b = yeni.bakis || [0, 0];
    this.#sakkadBaslat(b[0], b[1], 0.13);
    this.sonrakiSakkad = performance.now() / 1000 + 0.9;

    // Insanlar da yuz ifadesi degistirirken kirpar.
    if (Math.random() < 0.55) this.#kirpmaBaslat();
  }

  dur() { this.calisiyor = false; }

  // TEST SEAMI — firmware portu ayni pikselleri mi uretiyor?
  //
  // firmware/test/goz_karsilastir.cpp bu kareleri C++ motorunun
  // urettikleriyle piksel piksel karsilastiriyor. "Portladim" ile
  // "dogru portladim" arasindaki fark gozle gorulmuyor: bir isaret
  // hatasi ya da sabitte basamak kaymasi "calisiyor gibi" duruyor ama
  // ifade baska bir sey oluyor.
  //
  // Belirlenimci olmasi icin animasyon TAMAMEN atlaniyor: durum hedefe
  // oturtuluyor, gecen/kirpma/titreme sifir. Karsilastirilan sey
  // CIZIM, animasyon degil.
  testKaresi(durumAdi) {
    const d = DURUMLAR[durumAdi];
    if (!d) return null;

    this.durum = durumAdi;
    this.tanim = d;
    this.hedef = d.goz.map((e) => ({ ...e }));
    this.simdi = d.goz.map((e) => ({ ...e }));
    this.hedefEgim = d.egimKafa || 0;
    this.egim = this.hedefEgim;
    this.vurgu = 0;
    this.gerilme = 0;

    const b = d.bakis || [0, 0];
    this.bakis = { x: b[0], y: b[1] };
    this.oncekiBakis = { x: b[0], y: b[1] };
    this.sakkad.t = 1;
    this.kirpmaT = -1;

    this.#ciz(0, 0, 0);
    // Tampon nesnesi degil ImageData donuyor: cagiran taraf `.data`
    // ile standart sekilde okusun.
    return this.tampon.veri;
  }

  // ------------------------------------------------------------------ ic

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

  #dongu(zaman) {
    if (!this.calisiyor) return;

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

    const t0 = performance.now();
    this.#ciz(gecen, kirp, tit);
    this.olcum.ms = performance.now() - t0;
    this.olcum.piksel = this.tampon.dokunulan;

    this._kare++;
    if (zaman - this._fpsZaman > 500) {
      this.olcum.fps = this._kare * 1000 / (zaman - this._fpsZaman);
      this._kare = 0;
      this._fpsZaman = zaman;
    }

    requestAnimationFrame((z) => this.#dongu(z));
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
    const T = this.tampon;
    T.temizle();

    // Nefes: fark edilmiyor ama olmayinca robot "olu" goruniyor.
    const nefes = 1 + Math.sin(gecen * 1.45) * 0.018;
    const nefesY = Math.sin(gecen * 1.45) * 1.5;
    const pop = 1 + this.vurgu;

    const merkezX = EKRAN.g / 2;
    const merkezY = AYAR.merkezY + nefesY;

    // KAFA EGIMI — ESP32 gercegi.
    //
    // Eski dosya butun sahneyi `ctx.rotate()` ile donduruyordu. Cerceve
    // tamponunu dondurmek her piksel icin ters donusum demek; ucuz
    // degil. Bu boyutta egim zaten 2 dereceyi gecmiyor, yani gorsel
    // etkisi "bir goz biraz yukari, oteki biraz asagi"dan ibaret.
    // Onu dogrudan yapiyoruz: bedava ve ayni okunuyor.
    const egimKaydir = Math.sin(this.egim * 0.20) *
                       (AYAR.gozG + AYAR.aralik) / 2 * AYAR.kafaEgimi;

    for (let i = 0; i < 2; i++) {
      const e = this.simdi[i];
      const yon = i === 0 ? -1 : 1;

      const gen = AYAR.gozG * e.g * pop * nefes * (1 + this.gerilme * 0.5);
      const yuk = AYAR.gozY * e.y * pop * nefes * (1 - this.gerilme * 0.28)
                  * (1 - kirp * 0.94);

      const x = merkezX + yon * (AYAR.gozG / 2 + AYAR.aralik / 2)
                + (this.bakis.x + tit) * AYAR.bakisX
                + e.kaydirX * EKRAN.g;
      const y = merkezY + (this.bakis.y + tit * 0.5) * AYAR.bakisY
                + e.kaydirY * EKRAN.y
                + yon * egimKaydir;

      this.#gozCiz(T, x, y, gen, yuk, e, kirp, yon);
    }

    T.bas(this.ctx);
  }

  #gozCiz(T, x, y, gen, yuk, e, kirp, yon) {
    const sol = x - gen / 2;
    const ust = y - yuk / 2;
    const yaricap = Math.min(AYAR.yaricap, gen * 0.42, yuk / 2);

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

    // Kapak cizgileri: y = A + B * x
    const ustB = (dUst2 - dUst) / Math.max(gen, 1);
    const altB = (dAlt2 - dAlt) / Math.max(gen, 1);
    const kapak = {
      ustA: ustY + dUst - ustB * sol,
      ustB,
      altA: altY + dAlt - altB * sol,
      altB,
    };

    // Renk: satir basina bir deger. ESP32'de bu bir tablo aramasi
    // ya da tek carpma; gercek bir renk gecisi motoru gerekmiyor.
    const g0 = ust;
    const g1 = ust + Math.max(yuk, 1);
    const renkAl = AYAR.egimRenk
      ? (sy) => {
          let t = (sy + 0.5 - g0) / (g1 - g0);
          t = t < 0 ? 0 : t > 1 ? 1 : t;
          return [
            ACIK[0] + (KOYU[0] - ACIK[0]) * t,
            ACIK[1] + (KOYU[1] - ACIK[1]) * t,
            ACIK[2] + (KOYU[2] - ACIK[2]) * t,
          ];
        }
      : () => KOYU;

    // --- parlama: ana seklin buyugu, dusuk alfa, birkac kat
    for (let k = AYAR.parlamaKat; k >= 1; k--) {
      const b = k * AYAR.parlamaKalinlik;
      T.yuvarlakDoldur(
        sol - b, ust - b, gen + b * 2, Math.max(yuk, 1) + b * 2,
        yaricap + b,
        { ustA: kapak.ustA - b, ustB: kapak.ustB,
          altA: kapak.altA + b, altB: kapak.altB },
        () => KOYU,
        AYAR.parlamaAlfa / k,
      );
    }

    // --- gozun kendisi
    T.yuvarlakDoldur(sol, ust, gen, Math.max(yuk, 1), yaricap,
                     kapak, renkAl, 1);

    // --- cam parlamasi: bakisla hafifce kayiyor, isik gozun uzerinde
    //     sabit duruyormus hissi veriyor
    if (AYAR.camParlamasi && kirp < 0.5 && yuk > 14) {
      const px = sol + gen * (0.14 - this.bakis.x * 0.04);
      T.yuvarlakDoldur(
        px, ust + yuk * 0.10, gen * 0.26, Math.max(yuk * 0.20, 3),
        yaricap * 0.5, kapak, () => PARLAK, 0.85,
      );
    }
  }
}


// ===========================================================================
// ESP32 BUTCESI — olcum degil, ARITMETIK
// ===========================================================================
//
// Buradaki sayilar hesaplanmis, cihazda dogrulanmamis. Amaci "bu
// gorunumu tasiyabilir miyiz" sorusuna kaba bir cevap vermek; ornegin
// parlama katmanini 3'ten 6'ya cikarmanin bedelini gormek.
//
// Iki ayri darbogaz var ve ikisi de gercek:
//
//  1. CIZIM: piksel basina yaklasik 20 cevrim (kapsama + karistirma +
//     renk). 240 MHz'de kare butcesi = 240e6 / fps cevrim.
//  2. SPI TASIMA: 240*240*16 bit = 921.600 bit. 40 MHz'de 23 ms.
//     Yani TAM ekran tazeleme, cizim bedava olsa bile saniyede
//     ~43 kareyle sinirli. 30 fps hedeflenirse SPI'nin %70'i gidiyor.
//     Ders: butun ekrani degil, DEGISEN dikdortgeni gonder.
export function butce(piksel, fps = 30) {
  const cevrimPiksel = 20;
  const cpu = 240e6;

  const cizimCevrim = piksel * cevrimPiksel;
  const kareButce = cpu / fps;

  const bit = EKRAN.g * EKRAN.y * 16;
  const spiMs = bit / (EKRAN.spiMhz * 1e6) * 1000;

  return {
    piksel,
    cizimYuzde: 100 * cizimCevrim / kareButce,
    spiMs,
    spiYuzde: 100 * spiMs / (1000 / fps),
    enFazlaFps: 1000 / spiMs,
  };
}
