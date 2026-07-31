// Konusma dakikasi sayaci — panelde "bugun / bu ay".
//
// prototype/kullanim.py'nin portu.
//
// ===========================================================================
// NE SAYILIYOR: OTURUMUN ACIK KALDIGI SURE
// ===========================================================================
//
// Konusulan sure degil. Ucret dakikayla isliyor ve en buyuk kalem
// sohbetin kendisi degil BOSTA BEKLEMEK (ayarlar.py: gunde 12 saat
// bosta ~ $108/ay). Fatura "oturum acik miydi" sorusuna bakiyor, o
// yuzden sayilan sey de o.
//
// ===========================================================================
// SAAT SORUNU — ve neden SNTP eklendi
// ===========================================================================
//
// "Bugun" ve "bu ay" demek icin gercek tarih gerekiyor. ESP32'de pil
// destekli saat yok; acilista tarih 1970.
//
// SNTP olmadan iki secenek vardi:
//   a) Panelde "bugun/bu ay" yerine "acilistan beri" yazmak
//   b) Tarihi uydurmak
//
// (b) yasak. (a) ise paneli PC surumunden ayirirdi ve kullanici
// "robotu yaptigimizda her sey bu testlerde yaptigimiz gibi olsun"
// dedi. O yuzden SNTP eklendi (ag baglandiktan sonra, ~15 satir).
//
// Saat HENUZ ayarlanmamissa dakikalar "0000-00-00" hanesine yaziliyor
// ve panelde toplam olarak gorunuyor — kaybolmuyorlar.

#pragma once

#include <cstdint>
#include <string>

#include <esp_err.h>

namespace pati {

// NVS'ten okur. Ag hazir olmadan da cagrilabilir.
esp_err_t kullanim_baslat();

// Saati ag uzerinden ayarlar. Ag baglandiktan SONRA cagrilmali;
// bloklamiyor, arka planda tamamlaniyor.
void kullanim_saat_ayarla();

bool kullanim_saat_hazir();

// Oturum basladi / bitti. Sure NVS'e ancak bitince yaziliyor
// (her dakika yazmak flash omrunu tuketirdi), ama `kullanim_ozet`
// acik oturumu de sayiyor — yoksa panel konusma boyunca hic degismez
// ve "sayac bozuk" gorunur.
void kullanim_oturum_basladi();
void kullanim_oturum_bitti();

// UYKU — sayac duruyor, cunku uykuda ucret ISLEMIYOR.
//
// Uykuda WebSocket kapali: ses akmiyor, dakika yanmiyor. Panelin
// kendisi de "Uyurken ucret islemez" yaziyor. Sayac duvar saatiyle
// isleseydi panel yalan soylerdi — PC'de tam bu yasandi: 53,7
// dakikalik bir oturum 90 saniye sonra uyudu, panel 53,7 dakikanin
// tamamini ucretli gosterdi (prototype/kullanim.py §Oturum).
//
// AYRICA BURASI TEK FLASH NOKTASI. Robotun normal kapanisi "fisi
// cek"; `kullanim_oturum_bitti()` gercek kullanimda hic cagrilmiyor.
// Duraklatma dakikalari NVS'e yazdigi icin fis cekildiginde en fazla
// son uyanmadan bu yana gecen sure kayboluyor — uyku suresiyle
// sinirli. Yazmasaydik gunun tamami giderdi.
void kullanim_duraklat();
void kullanim_devam();

struct KullanimOzeti {
    double bugun_dk = 0;
    double ay_dk = 0;
    double tahmin_usd = 0;
    int gun_sayisi = 0;
};

KullanimOzeti kullanim_ozet();

void kullanim_sifirla();

}  // namespace pati
