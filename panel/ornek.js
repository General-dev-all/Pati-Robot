// -*- coding: utf-8 -*-
//
// Panelin ornek verisi — robot gelmeden sayfayi gorebilmek icin.
//
// ===========================================================================
// BU DOSYA GECICI VE BILINCLI OLARAK TEK YERDE
// ===========================================================================
//
// Robot geldiginde panelin GERISI degismeyecek; sadece bu dosya
// ESP32'nin uc noktalarina baglanacak:
//
//     DURUM   -> Python'un yayinladigi 'hafiza'/'kullanim'/'sesler'
//     tara()  -> GET /api/aglar  (robot gelince; simdilik ornek liste)
//
// Ornek veriyi sayfanin icine dagitmadim cunku o zaman gercege
// baglarken on kere ayni degisiklik yapilir ve biri atlanir.
//
// ===========================================================================
// DORT VERI TAKIMI — ve neden dordu de gerekli
// ===========================================================================
//
// Duzenli veriyle bakip "guzel gorunuyor" demek yaniltiyor. Yerlesim
// duzenli veride bozulmaz; UZUN isimde, 24 satirlik hafizada, BOS
// listede bozulur. Bunlar tahmin degil, gercekten olacak durumlar:
//
//   normal  Ilk gun sonrasi, olagan hal
//   zor     En kotu hal: uzun ad, uzun wifi adi, 24 hafiza satiri,
//           dort haneli dakika. Yerlesim burada kirilirsa kirilir.
//   ilk     Robot kutudan yeni cikti: hafiza bos, ad bos, wifi yok.
//           Anne paneli ILK bu halde gorecek — en cok onemsenecek hal.
//   kopuk   Robot ag'da degil. Panel ne diyor?
//
// Secim adres satirindan:  panel/?veri=zor
// Studyodaki dugmeler de bunu yapiyor.

const TAKIMLAR = {

  // ------------------------------------------------------------- normal
  normal: {
    bagli: true,
    konusuyor: false,
    surum: 'Pati 2.0 · aşama 2',
    wifi: { ad: 'Ev-Wifi', guc: 3 },
    ses: { seviye: 0.85, hiz: 1.30, sesAdi: 'Puck' },
    uyku: 4,
    cocuk: { ad: 'Bulut', yas: 6 },
    ekBilgi: 'Kedisinin adı Pamuk. Brokoli sevmiyor. Kardeşi yok.',
    hafiza: [
      { metin: 'Adı Bulut, 6 yaşında', kez: 14 },
      { metin: 'Kedisinin adı Pamuk', kez: 9 },
      { metin: 'Dinozorları çok seviyor, en sevdiği triseratops', kez: 7 },
      { metin: 'Karanlıktan biraz korkuyor', kez: 3 },
      { metin: 'Anaokuluna gidiyor, öğretmeni Sevgi', kez: 2 },
    ],
    kota: { bugunDk: 18, ayDk: 214, tahminUsd: 3.9 },
  },

  // ---------------------------------------------------------------- zor
  //
  // Buradaki uzunluklar uydurma degil: Turk Telekom modemleri
  // gercekten bu bicimde uzun SSID uretiyor, cocuklarin uc adi
  // olabiliyor, ve hafiza haftalar icinde birikiyor.
  zor: {
    bagli: true,
    konusuyor: true,
    surum: 'Pati 2.0 · aşama 2 · yapı 4181',
    wifi: { ad: 'TurkTelekom_ZTE_A1B2C3_5GHz_Misafir', guc: 1 },
    ses: { seviye: 1.00, hiz: 1.45, sesAdi: 'Fenrir' },
    uyku: 15,
    cocuk: { ad: 'Muhammed Emir Kağan', yas: 4 },
    ekBilgi: 'Kedisinin adı Pamuk, köpeğinin adı Karabaş. Brokoli ve ' +
             'ıspanak sevmiyor ama karnabahar seviyor. Akşam sekizde ' +
             'dişlerini fırçalaması gerekiyor, hatırlatırsan iyi olur. ' +
             'Anneannesinin adı Hatice, her cumartesi ona gidiyoruz. ' +
             'Kuzeni Elif ile aynı okula gidiyor.',
    hafiza: Array.from({ length: 24 }, (_, i) => ([
      { metin: 'Adı Muhammed Emir Kağan, 4 yaşında', kez: 31 },
      { metin: 'Kedisinin adı Pamuk, köpeğinin adı Karabaş', kez: 22 },
      { metin: 'En sevdiği oyuncak kırmızı itfaiye arabası, adını ' +
               'Kıvılcım koymuş', kez: 18 },
      { metin: 'Anaokulunda en yakın arkadaşı Elif', kez: 14 },
      { metin: 'Gök gürültüsünden korkuyor, fırtınalı gecelerde ' +
               'annesinin yanında yatıyor', kez: 11 },
    ][i % 5])).map((h, i) => ({ ...h, kez: Math.max(1, h.kez - i) })),
    kota: { bugunDk: 187, ayDk: 2840, tahminUsd: 41.70 },
  },

  // ---------------------------------------------------------------- ilk
  //
  // Annenin paneli ILK gordugu hal. Bos bir sayfa "bozuk" gibi
  // gorunmemeli; her kutu ne yapilmasi gerektigini soylemeli.
  ilk: {
    bagli: false,
    konusuyor: false,
    surum: 'Pati 2.0 · ilk açılış',
    wifi: { ad: 'Ağ seçilmedi', guc: 0 },
    ses: { seviye: 0.85, hiz: 1.30, sesAdi: 'Puck' },
    uyku: 4,
    cocuk: { ad: '', yas: '' },
    ekBilgi: '',
    hafiza: [],
    kota: { bugunDk: 0, ayDk: 0, tahminUsd: 0 },
  },

  // -------------------------------------------------------------- kopuk
  kopuk: {
    bagli: false,
    konusuyor: false,
    surum: 'Pati 2.0 · aşama 2',
    wifi: { ad: 'Ev-Wifi (bağlanamıyor)', guc: 0 },
    ses: { seviye: 0.85, hiz: 1.30, sesAdi: 'Puck' },
    uyku: 4,
    cocuk: { ad: 'Bulut', yas: 6 },
    ekBilgi: 'Kedisinin adı Pamuk.',
    hafiza: [
      { metin: 'Adı Bulut, 6 yaşında', kez: 14 },
      { metin: 'Kedisinin adı Pamuk', kez: 9 },
    ],
    kota: { bugunDk: 0, ayDk: 214, tahminUsd: 3.9 },
  },
};

const secim = new URLSearchParams(location.search).get('veri');
export const TAKIM_ADI = TAKIMLAR[secim] ? secim : 'normal';
export const DURUM = structuredClone(TAKIMLAR[TAKIM_ADI]);

// Degerlerin sinirlari UYDURMA DEGIL — prototype/ayarlar.py'den:
//   SES_SEVIYESI = 0.85 · EN_AZ 0.15 · EN_FAZLA 1.00 · ADIM 0.15
// Tizlik/hiz araligi Asama 1 panelinden: 0.85 - 1.45, secilen 1.30.
export const SINIR = {
  seviyeEnAz: 0.15,
  seviyeEnFazla: 1.00,
  hizEnAz: 0.85,
  hizEnFazla: 1.45,
  uykuEnAz: 1,
  uykuEnFazla: 15,
};

// Ses listesi robotun kendisinden gelecek — Gemini'nin hazir sesleri.
// Burada sadece secim kutusu bos gorunmesin diye birkaci var; Asama
// 1'de dinleyerek "Puck" secildi (29.07.2026).
export const SESLER = ['Puck', 'Charon', 'Kore', 'Fenrir', 'Aoede'];

// Robot gelince: fetch('/api/aglar')
export async function tara() {
  await bekle(1200);
  return [
    { ad: 'Ev-Wifi', guc: 3, kilit: true },
    { ad: 'TurkTelekom_ZTE_A1B2C3_5GHz_Misafir', guc: 2, kilit: true },
    { ad: 'Ev-Wifi-5G', guc: 2, kilit: true },
    { ad: 'Komsu', guc: 1, kilit: true },
    { ad: 'Misafir', guc: 1, kilit: false },
  ];
}

// SADECE ONIZLEME ICIN. Canli sayfada kayit WebSocket uzerinden
// gidiyor (pati.js -> Python -> hafiza/ayarlar), bu fonksiyon
// kullanilmiyor. Robot gelince ESP32'nin /api/ayar ucu ayni yeri
// dolduracak.
export async function yaz(alan, deger) {
  await bekle(320);
  return { tamam: true, alan, deger };
}

const bekle = (ms) => new Promise((c) => setTimeout(c, ms));
