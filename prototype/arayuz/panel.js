// -*- coding: utf-8 -*-
//
// Panel — Python'un yayinladigi olaylari alir ve gosterir.
//
// ⚠ BU DOSYA SES ISLEMIYOR. Mikrofon da hoparlor de Python tarafinda.
//   Buraya sadece JSON olay geliyor. PLAN.md'nin "araya tarayici
//   koyma" kurali bu yuzden cignenmiyor: panel kapaliyken de olcum
//   birebir ayni calisiyor.

import { Gozler, DURUMLAR } from './gozler.js';
import { TarayiciSes } from './mikrofon.js';

const gozler = new Gozler(document.getElementById('gozler'));

const $ = (id) => document.getElementById(id);
const baglantiRozet = $('baglanti');
const dokumKutu = $('dokum');
const olayKutu = $('olaylar');

let ws = null;
let calisiyor = false;

// Tarayici mikrofonu — sadece "gosteri" modunda kuruluyor.
const tarayiciSes = new TarayiciSes((pcm) => {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(pcm);
});

// Robot konusurken mikrofon kapaninca gozler "konusuyor"da kalsin,
// kullanici da mikrofonun neden sessiz oldugunu gorsun.
tarayiciSes.susturmaGeri = (sussun) => {
  const s = document.getElementById('mikDurum');
  if (s) s.textContent = sussun ? '🔇 mikrofon kapalı (robot konuşuyor)'
                                : '🎤 dinliyor';
};

// PLAN.md esikleri — arayuzde de AYNI degerler.
const GECER = 1500;
const SINIR = 2500;

function karar(ms) {
  if (ms <= GECER) return 'gecer';
  if (ms <= SINIR) return 'sinir';
  return 'kalir';
}

// --------------------------------------------------------------- baglanti

function bagla() {
  // WS adresi SAYFANIN KENDI adresinden turetiliyor.
  //
  // Onceden sabit "ws://host:8757" idi ve iki sorun yaratiyordu:
  //   1. Sayfa ve WebSocket ayri porttaydi; tek adres tunellenince
  //      (Cloudflare tunnel) sayfa aciliyor ama canli veri gelmiyordu.
  //   2. https uzerinden acilinca tarayici "ws://" baglantisini
  //      guvensiz sayip engelliyordu.
  // Artik ayni port + ayni protokol: https ise wss, http ise ws.
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${proto}//${location.host}/ws`);

  ws.onopen = () => {
    baglantiRozet.textContent = 'panel bağlı';
    baglantiRozet.className = 'rozet bagli';
  };

  ws.onclose = () => {
    baglantiRozet.textContent = 'panel koptu';
    baglantiRozet.className = 'rozet koptu';
    // Python yeniden baslatilirsa kendiliginden geri baglansin
    setTimeout(bagla, 1500);
  };

  ws.binaryType = 'arraybuffer';
  ws.onmessage = (e) => {
    // Ikili cerceve = robotun sesi (24 kHz PCM). Metin = JSON olay.
    if (e.data instanceof ArrayBuffer) {
      tarayiciSes.cal(e.data);
      return;
    }
    let m;
    try { m = JSON.parse(e.data); } catch { return; }
    isle(m);
  };
}

function gonder(mesaj) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(mesaj));
  }
}

// ------------------------------------------------------------------ olaylar

function isle(m) {
  switch (m.tip) {
    case 'ifade':
    case 'durum': {
      const d = m.ifade || m.durum;
      gozler.ayarla(d);
      $('ifadeAdi').textContent = d;
      break;
    }

    case 'baglanti':
      baglantiRozet.textContent = m.metin || m.durum;
      baglantiRozet.className = 'rozet ' +
        (m.durum === 'bagli' ? 'bagli' : m.durum === 'koptu' ? 'koptu' : 'bekliyor');
      calisiyor = (m.durum === 'bagli');
      $('baslat').disabled = calisiyor;
      $('durdur').disabled = !calisiyor;
      $('yazi').disabled = !calisiyor;
      break;

    case 'tur':
      turEkle(m);
      break;

    case 'ozet':
      ozetGuncelle(m);
      break;

    case 'olay':
      olayEkle(m.metin, m.seviye);
      break;

    case 'hafiza':
      hafizaGoster(m.ozet);
      break;

    case 'ses_temizle':
      tarayiciSes.temizle();   // sozunu kesme
      break;

    case 'sesler':
      sesleriGoster(m);
      break;

    case 'ses_seviyesi':
      // Sunucu son sozu soyluyor. Cocuk "sesini kis" dediginde ya da
      // kaydirici sinira dayandiginda kaydiricinin GERCEK degeri
      // gostermesi icin sunucudan gelen deger yaziliyor.
      if (m.en_az != null) $('seviyeKaydirici').min = m.en_az;
      if (m.en_fazla != null) $('seviyeKaydirici').max = m.en_fazla;
      $('seviyeKaydirici').value = m.deger;
      $('seviyeDeger').textContent = `${Math.round(m.deger * 100)}%`;
      break;

    case 'gecikme_ayar':
      $('vadSecim').value = m.vad == null ? '' : String(m.vad);
      $('yuzAcik').checked = !!m.yuz;
      // Neyin GERCEKTEN uygulandigini yaz. Onceden ayarlar sadece
      // dugmeye basinca gidiyordu ve kullanici kutucugu degistirip
      // dugmeye basmayi unutunca olcum yanlis ayarla yapiliyordu —
      // bir kosu bu yuzden bosa gitti.
      $('gecikmeDurum').textContent =
        `✓ etkin: sustu kararı ${m.vad == null ? 'varsayılan' : m.vad + ' ms'}` +
        ` · yüz aracı ${m.yuz ? 'açık' : 'KAPALI'}`;
      break;

    case 'ses_durum':
      $('sesDinle').disabled = m.calisiyor;
      $('sesDinle').textContent = m.calisiyor ? 'çalıyor…' : 'Dinle';
      break;
  }
}

// Google'in 30 hazir sesi. Adaylar basta, digerleri altta.
function sesleriGoster(m) {
  const sec = $('sesListesi');
  sec.innerHTML = '';

  const grup = (etiket, liste) => {
    if (!liste.length) return;
    const g = document.createElement('optgroup');
    g.label = etiket;
    for (const s of liste) {
      const o = document.createElement('option');
      o.value = s.ad;
      o.textContent = `${s.ad} — ${s.tanim}`;
      if (s.ad === m.secili) o.selected = true;
      g.appendChild(o);
    }
    sec.appendChild(g);
  };

  grup('Çocuk robotu için adaylar', m.liste.filter((s) => s.aday));
  grup('Diğerleri', m.liste.filter((s) => !s.aday));

  $('hizKaydirici').value = m.hiz;
  $('hizDeger').textContent = `${Number(m.hiz).toFixed(2)}×`;
  $('sesDurum').textContent =
    `✓ etkin: ${m.secili} · tizlik ${Number(m.hiz).toFixed(2)}×`;
}

// Hafizayi gostermek v1'in ilkesi: cocuk robotun ne bildigini
// gorebilmeli ve istemedigini silebilmeli.
function hafizaGoster(o) {
  const ad = o.cocuk?.ad;
  const parcalar = [];
  parcalar.push(ad ? `Tanıdığı çocuk: <b>${kacir(ad)}</b>` : 'Çocuğu henüz tanımıyor');
  if (o.cocuk?.yas) parcalar.push(`${o.cocuk.yas} yaşında`);
  parcalar.push(`${o.bilgi_sayisi} bilgi (depolama sınırı ${o.depolama_siniri})`);
  parcalar.push(`prompta en çok ${o.prompt_siniri} tanesi giriyor`);
  $('hafizaOzet').innerHTML = parcalar.join(' · ');

  const liste = $('hafizaListe');
  liste.innerHTML = '';
  for (const b of o.bilgiler || []) {
    const d = document.createElement('div');
    d.className = 'bilgi';
    const kez = b.kez > 1 ? ` <span class="kez">×${b.kez}</span>` : '';
    d.innerHTML = `<span>${kacir(b.metin)}${kez}</span>`;
    const sil = document.createElement('button');
    sil.className = 'kucuk';
    sil.textContent = 'sil';
    sil.onclick = () => gonder({ tip: 'bilgi_sil', id: b.id });
    d.appendChild(sil);
    liste.appendChild(d);
  }
}

function turEkle(m) {
  const d = document.createElement('div');
  d.className = 'tur';

  let ic = '';
  if (m.cocuk) {
    ic += `<div class="kim">çocuk</div><div class="soz">${kacir(m.cocuk)}</div>`;
  }
  if (m.robot) {
    ic += `<div class="kim">Pati</div><div class="soz pati">${kacir(m.robot)}</div>`;
  }
  if (m.gecikme != null) {
    // "metin" kaynakli turlarda VAD devrede degil — kriter sayilmaz.
    const etiket = m.kaynak === 'metin'
      ? `${Math.round(m.gecikme)} ms · metin turu, KRITER DEGIL`
      : `${Math.round(m.gecikme)} ms (ağ ${Math.round(m.ag)} · yerel ${Math.round(m.yerel)})`;
    ic += `<div class="sure">⏱ ${etiket}</div>`;
  }
  d.innerHTML = ic;
  dokumKutu.appendChild(d);
  dokumKutu.scrollTop = dokumKutu.scrollHeight;

  if (m.gecikme != null && m.kaynak !== 'metin') {
    const el = $('sonGecikme');
    el.textContent = Math.round(m.gecikme);
    el.className = 'deger ' + karar(m.gecikme);
    cubukGuncelle(m.ag, m.yerel);
  }
}

function cubukGuncelle(ag, yerel) {
  const toplam = ag + yerel || 1;
  $('cubukAg').style.width = `${(ag / toplam) * 100}%`;
  $('cubukYerel').style.width = `${(yerel / toplam) * 100}%`;
  $('agMs').textContent = `${Math.round(ag)} ms`;
  $('yerelMs').textContent = `${Math.round(yerel)} ms`;
}

function ozetGuncelle(m) {
  $('turSayisi').textContent = m.adet ?? 0;
  if (m.ortalama != null) {
    const o = $('ortalama');
    o.textContent = Math.round(m.ortalama);
    o.className = 'deger ' + karar(m.ortalama);
  }
  if (m.en_kotu != null) {
    const k = $('enKotu');
    k.textContent = Math.round(m.en_kotu);
    k.className = 'deger ' + karar(m.en_kotu);
  }
}

function olayEkle(metin, seviye) {
  const d = document.createElement('div');
  d.className = seviye || '';
  d.textContent = metin;
  olayKutu.appendChild(d);
  olayKutu.scrollTop = olayKutu.scrollHeight;
}

function kacir(s) {
  return String(s).replace(/[<>&]/g, (c) =>
    ({ '<': '&lt;', '>': '&gt;', '&': '&amp;' }[c]));
}

// -------------------------------------------------------------- kontroller

$('baslat').onclick = async () => {
  // Dugmeyi HEMEN kilitle. Ikinci tiklama ikinci bir Gemini oturumu
  // aciyordu ve iki Pati ayni anda konusuyordu.
  if ($('baslat').disabled) return;
  $('baslat').disabled = true;

  const mod = document.querySelector('input[name=mod]:checked').value;

  // Gosteri modu: once mikrofon iznini al. Izin gelmezse hic
  // baslatmayalim, yoksa Gemini oturumu acilip bos bekler.
  if (mod === 'tarayici') {
    try {
      await tarayiciSes.ac();
      gonder({ tip: 'ses_kanali', ac: true });
    } catch (e) {
      olayEkle(`mikrofon izni alınamadı: ${e.message}`, 'hata');
      olayEkle('Tarayıcı adres çubuğundaki kilit simgesinden izin ver.', '');
      $('baslat').disabled = false;
      return;
    }
  }

  dokumKutu.innerHTML = '';
  olayKutu.innerHTML = '';
  gonder({ tip: 'baslat', mod });
  baglantiRozet.textContent = 'bağlanıyor…';
  baglantiRozet.className = 'rozet bekliyor';
};

$('durdur').onclick = () => {
  gonder({ tip: 'durdur' });
  gonder({ tip: 'ses_kanali', ac: false });
  tarayiciSes.kapat();
};

// Sifirlama geri alinamaz — v1'de de onay isteyen tek dugme buydu.
$('hafizaSifirla').onclick = () => {
  if (!confirm('Robotun öğrendiği HER ŞEY silinecek.\n' +
               'Çocuğu baştan tanıyacak. Emin misin?')) return;
  gonder({ tip: 'hafiza_sifirla' });
};

$('yazi').onkeydown = (e) => {
  if (e.key !== 'Enter') return;
  const yazi = e.target.value.trim();
  if (!yazi) return;
  gonder({ tip: 'metin', yazi });
  e.target.value = '';
};

// -- ses secimi
//
// AYARLAR DEGISTIGI ANDA UYGULANIYOR, dugme yok.
// Onceden "kullan" dugmesi vardi ve kullanici bir ayari degistirip
// dugmeye basmayi unutunca olcum yanlis ayarla yapiliyordu — bir kosu
// bu yuzden bosa gitti (yuz araci kapatilmis saniliyordu, acikti).
function sesGonder() {
  gonder({
    tip: 'ses_ayarla',
    ses: $('sesListesi').value,
    hiz: Number($('hizKaydirici').value),
  });
}

$('hizKaydirici').oninput = (e) => {
  $('hizDeger').textContent = `${Number(e.target.value).toFixed(2)}×`;
};
$('hizKaydirici').onchange = sesGonder;   // birakinca uygula
$('sesListesi').onchange = sesGonder;

// SES SEVIYESI — tizlikten farkli olarak ANINDA gonderiliyor (oninput),
// birakmayi beklemiyor. Sebep: seviye calisan oturuma da uygulaniyor,
// yani kaydirirken sonucu duyabiliyorsun. Tizlik ise oturum acilirken
// sabitlendigi icin ancak sonraki baslatmada gecerli.
$('seviyeKaydirici').oninput = (e) => {
  const v = Number(e.target.value);
  $('seviyeDeger').textContent = `${Math.round(v * 100)}%`;
  gonder({ tip: 'ses_seviyesi', deger: v });
};

$('sesDinle').onclick = () => {
  gonder({
    tip: 'ses_dene',
    ses: $('sesListesi').value,
    hiz: Number($('hizKaydirici').value),
  });
};

function gecikmeGonder() {
  const v = $('vadSecim').value;
  gonder({
    tip: 'gecikme_ayar',
    vad: v === '' ? null : Number(v),
    yuz: $('yuzAcik').checked,
  });
}
$('vadSecim').onchange = gecikmeGonder;
$('yuzAcik').onchange = gecikmeGonder;

// Ifade onizleme dugmeleri — sadece gorsel deneme, olcume girmez.
const kutu = $('ifadeDugmeleri');
for (const ad of Object.keys(DURUMLAR)) {
  const b = document.createElement('button');
  b.textContent = ad;
  b.onclick = () => { gozler.ayarla(ad); $('ifadeAdi').textContent = ad; };
  kutu.appendChild(b);
}

// ---------------------------------------------------------------------
// UZAKTAN BAGLANANI DOGRU MODA YONLENDIR
//
// Gercekten yasandi: tunel adresi telefondan acildi, varsayilan
// "Mikrofonla" secili geldi ve Python EV SAHIBI BILGISAYARIN
// mikrofonunu/hoparlorunu acmaya calisti. Uzaktaki biri icin o iki
// secenek anlamsiz — onun mikrofonu tarayicida.
//
// Sayfa 127.0.0.1 disindan aciliyorsa: tarayici modunu sec, otekileri
// kapat.
// ---------------------------------------------------------------------
const YEREL = ['127.0.0.1', 'localhost', '::1'].includes(location.hostname);

if (!YEREL) {
  const secim = document.querySelector('input[name=mod][value=tarayici]');
  if (secim) secim.checked = true;
  // Kulakliksiz kullanim secenegi sadece burada anlamli.
  $('dubleksSatir').hidden = false;
  $('dubleksNot').hidden = false;
  $('yarimDubleks').onchange = (e) => {
    tarayiciSes.yarimDubleks = e.target.checked;
    if (!e.target.checked) {
      tarayiciSes.susturuldu = false;
      const s = $('mikDurum');
      if (s) s.textContent = '🎤 dinliyor';
    }
  };
  for (const g of document.querySelectorAll('input[name=mod]')) {
    if (g.value !== 'tarayici') {
      g.disabled = true;
      g.closest('label').style.opacity = '0.4';
      g.closest('label').title =
        'Bu seçenek robotun bulunduğu bilgisayarın mikrofonunu kullanır, ' +
        'uzaktan işe yaramaz.';
    }
  }
  const not = document.createElement('div');
  not.className = 'ipucu';
  not.innerHTML =
    'Uzaktan bağlandın. <b>Kendi mikrofonunla</b> konuşacaksın — ' +
    '“Başlat”a basınca tarayıcı izin isteyecek.';
  document.querySelector('.mod').appendChild(not);
}

bagla();
