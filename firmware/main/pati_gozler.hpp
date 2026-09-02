// Pati'nin gozleri — 240x135 ekranda.
//
// ===========================================================================
// BU DOSYA panel/gozler240.js'IN PORTU
// ===========================================================================
//
// Olculer ve duygu tablosu ELLE YAZILMADI: `pati_goz_uretilmis.h`
// dosyasi `firmware/goz_uret.mjs` tarafindan gozler240.js'ten uretiliyor.
// Yani tarayicida begenilen sey ile ekranda cizilen sey ayni sayilara
// dayaniyor. Sayiyi degistirmek icin gozler240.js'i degistir ve ureteci
// calistir; buraya elle sayi yazmak iki tarafi sessizce ayirir.
//
// ANIMASYON MANTIGI da ayni: sakkad (goz kaymaz, SICRAR), asimetrik
// kirpma (kapanma hizli ~90 ms, acilma yavas), nefes, squash & stretch,
// mikro titreme. Bu sayilar v1'de deneyerek bulundu; burada
// degistirilmiyor.
//
// ===========================================================================
// NEDEN GOREV OLARAK CALISIYOR
// ===========================================================================
//
// Cizim + SPI gonderimi bir karede ~10-25 ms tutuyor. Bunu sohbet
// dongusunun icinde yapmak ses hattini bloklar; olculen gecikmeye
// dogrudan eklenir. Ayri gorev, ayri onceligi olan bir is.
//
// Sohbet tarafi sadece "durum degisti" diyor (`gozler_durum`), gorev
// gerisini kendi yapiyor. Cagri BLOKLAMIYOR.
//
// ===========================================================================
// KIRLI DIKDORTGEN — bu bir tasarim karari, suslemek degil
// ===========================================================================
//
// Hesap (gozler_test.mjs ve gozler240.js'teki `butce`):
//   240 x 240 x 16 bit = 921.600 bit
//   40 MHz SPI'da       = 23,0 ms
//   yani cizim BEDAVA olsa bile en fazla 43 fps, 30 fps hedefinde
//   SPI'nin %69'u gidiyor.
//
// Gozler ekranin ortasinda duruyor ve degisen alan cok daha kucuk.
// O yuzden tam ekran degil, DEGISEN dikdortgen gonderiliyor: onceki
// karenin kapladigi alan ile simdiki karenin kapladigi alanin
// birlesimi. Erimeyen kenar kalmiyor cunku eski alan da temizleniyor.

#pragma once

#include <cstdint>

#include <esp_err.h>

namespace pati {

// Gozleri baslatir: ekrani kurar, cizim gorevini acar.
//
// `ekran_baslat()` basarisiz olursa bu da basarisiz doner — ama cagiran
// taraf programi DURDURMAMALI. Ekransiz Pati konusabiliyor; yuzu
// olmayan robot, sessiz robottan iyidir.
esp_err_t gozler_baslat();

// Ifadeyi degistirir. Ad `pati_goz_uretilmis.h` icindeki tabloda
// olmalı — yoksa cagri sessizce yok sayilmiyor, sayaca yaziliyor
// (`gozler_bilinmeyen_durum()`).
//
// BLOKLAMIYOR: sadece bir bayrak koyuyor, cizimi gorev yapiyor.
// Sohbet dongusunden ve olay geri cagirimlarindan guvenle cagrilabilir.
void gozler_durum(const char* ad);

// Konusma akisindan surulen durumlar. Modele SORULMUYOR cunku model
// "su an dinliyorum" demeyi beceremez — bunu biz zaten biliyoruz.
// (prototype/yuz.py §AKIS_DURUMLARI ile ayni dort ad.)
void gozler_bos();
void gozler_dinliyor();
void gozler_dusunuyor();
void gozler_konusuyor();

// --- olcum ve teshis ------------------------------------------------------
//
// Panelde gosterilmiyor, seri porta basiliyor. Sifir olmasi gereken
// sayilar sessizce birikirse "ara ara takiliyor" diye aranir.

// Cizilen kare sayisi.
std::uint32_t gozler_kare();

// Son karede DOKUNULAN piksel sayisi — tasinabilir maliyet olcusu.
// Milisaniye cihaza ozel, piksel sayisi degil.
std::uint32_t gozler_piksel();

// Hedeflenen kare hizi. Butce hesabi iki yerde yazilmasin diye burada:
// kaynak pati_gozler.cpp'deki HEDEF_FPS.
int gozler_hedef_fps();

// Pil kipi: goz kare hizini dusurur (20 -> 10 fps), ve Pati KONUSURKEN
// bir kademe daha (10 -> 5 fps).
//
// NEDEN: goz cizici en buyuk surekli CPU musterisi (kare basina 24-30 ms,
// butce 50 ms). Pilde brownout tam Pati konusmaya baslarken oluyor ve
// CPU'nun o anda bos olmasi pay birakiyor.
//
// 02.09.2026'da OLCULDU: 20 -> 10 fps, pilde cokme arasini ~40 saniyeden
// ~4,5 dakikaya cikardi (9 dakikada 2 cokme, oncesi 20-60 sn'de bir).
// Yani goz yuku gercek bir etken. Konusma kademesi bunun hedefli hali:
// cokmelerin HEPSI Pati konusurken oldu.
//
// Gerekcesi ve olculecek sey pati_gozler.cpp'de PIL_FPS'in yaninda.
void gozler_pil_kipi(bool pilde);

// Dusuk pil uyarisini TAM EKRAN gosterir: bir sonraki karede gozlerin
// yerine cizilir, birkac saniye durur, sonra gozler geri gelir.
//
// NEDEN BURADAN: cizim serit tamponlarini kullaniyor ve o tamponlar bu
// gorevin malı. Baska bir gorevden cizmek iki gorevin ayni tamponu ayni
// anda doldurmasi demek olurdu (bkz. pati_uyari.hpp).
//
// BLOKLAMIYOR — sadece istek birakiyor.
//
// `yuzde` 0-100; negatifse yuzde satiri cizilmiyor.
void gozler_pil_uyarisi(int yuzde);

// Son karenin cizim + gonderim suresi (mikrosaniye).
std::uint32_t gozler_kare_us();

// Ayni surenin ikiye bolunmus hali: piksel uretimi ve SPI'ye gonderim.
// Toplam sayi hangisinin sucu oldugunu soylemedigi icin ayri duruyorlar
// (31.07.2026, gercek kartta kare butcenin 1,7 kati cikti).
std::uint32_t gozler_ciz_us();
std::uint32_t gozler_gonder_us();

// Cizimin kendi ici: sekil rasterizasyonu ve 8 bit -> RGB565 cevrimi.
std::uint32_t gozler_sekil_us();
std::uint32_t gozler_renk_us();

// Son karede yeniden cizilen dikdortgenin alani (piksel).
std::uint32_t gozler_alan();

// Tabloda olmayan ifade istendi mi. Sifirdan buyukse ya model listede
// olmayan bir sey uyduruyor ya biz yanlis ad gonderiyoruz.
std::uint32_t gozler_bilinmeyen_durum();

// Kare atlandi mi (bir onceki kare hala cizilirken yeni istek geldi).
std::uint32_t gozler_atlanan_kare();

// Su anki ifadenin adi.
const char* gozler_su_anki();

}  // namespace pati
