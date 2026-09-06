// Pati'nin bedeni — iki tekerlek, iki kol
//
// StickS3 bir gövdeye takilabiliyor. Gövdede iki DC motor (L9110S
// surucu), iki mini servo ve kendi 4'lu AA pil yuvasi var. Pin haritasi
// ve "AA'nin artisi bu karta gelmiyor" kurali pati_pinler.h'de.
//
// ===========================================================================
// KOLLAR OZERK, TEKERLEKLER DEGIL
// ===========================================================================
//
// Kollar Pati konusurken kendiliginden hareket ediyor. Tekerlekler
// YALNIZCA panelden, cocugun parmagi altinda doniyor.
//
// Ayrimin sebebi teknik: Pati'de ucurum sensoru YOK. Masanin kenarini
// gorebilecegi hicbir yol yok, dolayisiyla kendi kararıyla ilerleyen bir
// Pati eninde sonunda duser. "Az hareket etsin" bunu geciktirir,
// engellemez. Kollar bu riski hic tasimiyor.
//
// Bir gun uzaklik ya da ucurum sensoru eklenirse bu karar yeniden
// acilabilir; o zamana kadar acilmamali.
//
// ===========================================================================
// 🔴 BU KATMAN HICBIR SEY SORMUYOR — DURUM ONA ITILIYOR
// ===========================================================================
//
// Arayuz bilerek pati_gozler'in aynisi. Sebep CLAUDE.md'deki sicak
// dongu tuzagi: 02.09.2026'da goz gorevi her karede kilit alan iki soru
// soruyordu ve pilde cokme arasi 4,5 dakikadan 0,7 dakikaya dustu.
//
// Bir servo dongusu dogasi geregi 20-50 ms'de bir donmek istiyor, yani
// bu dosya o tuzaga en yakin duran yer. Kurallar:
//
//   - Beden gorevi I2C YAPMIYOR, KILIT ALMIYOR, NVS'e DOKUNMUYOR.
//   - Icindeki tek donanim erisimi LEDC yazmaclari (ledc_set_duty /
//     ledc_update_duty) ve tek bir GPIO okumasi.
//   - 20 ms'lik dongu YALNIZCA gercekten bir sey hareket ederken
//     calisiyor. Bosta gorev uyuyor ve komut gelince uyandiriliyor.
//
// Yani tuzaga dusecek dongu, zamanin cogunda hic var olmuyor.

#pragma once

#include <cstdint>
#include <string>

#include <esp_err.h>

namespace pati {

// Pinleri GUVENLI hale getirir, sonra beden gorevini acar.
//
// 🔴 MUMKUN OLAN EN ERKEN CAGRILMALI — guc_baslat()'in hemen ardindan.
//
// Sebebi: ESP32 cikislari yazilim kurana kadar HAVADA kaliyor ve
// L9110'un girisleri o sirada belirsiz. Motor pinlerini erken asagi
// cekmek, acilistaki "segirme" penceresini onyukleyici suresine
// indiriyor. Bir satir asagi konsa pencere saniyelere cikardi.
//
// Basarisiz olursa PROGRAM DURMAMALI: bedensiz Pati zaten calisiyor.
esp_err_t beden_baslat();

// Beden su an takili mi.
//
// Algilama pini 200 ms'de bir okunuyor ve BES ARDISIK ayni okuma
// isteniyor (1 saniye): takip cikarirken kontak sekiyor ve sekmeyi
// durum degisikligi saymak, panelin kumanda kartini acip kapatmasi
// demek olurdu.
bool beden_takili();

// ---------------------------------------------------------------------------
// Surus — YALNIZCA panelden
// ---------------------------------------------------------------------------
//
// `x` ve `y` JOYSTICK KONUMU, -100..+100. y ileri, x saga.
//
// Karistirma (hangi tekerlek ne yapacak) ve ebeveynin hiz siniri
// CIHAZDA uygulaniyor, panelde degil — panel gonderse bile sinirin
// ustune cikilamiyor. Matematigi pati_beden_matematik.hpp'de ve konak
// testinde siniyor: isaretin ters olmasi ancak robot masadayken fark
// edilirdi.
//
// 🔴 HER CAGRI OLU ADAM ZAMANLAYICISINI BESLIYOR. Komut gelmeden
// BEDEN_OLU_ADAM_MS gecerse motorlar KENDILIGINDEN duruyor.
//
// Bu tek kural, "masada dusebilir" endisesinin yazilim karsiligi: cocuk
// parmagini kaldirirsa, telefon kilitlenirse, wifi takilirsa ya da
// tarayici kapanirsa Pati duruyor. Panelin dogru davranmasina
// GUVENMIYORUZ — sifir gondermeyi unutan bir panel, kacan bir robot
// demek olurdu.
void beden_surus(int x, int y);

// ---------------------------------------------------------------------------
// Kollar
// ---------------------------------------------------------------------------
//
// Aci DEGIL, KALDIRMA YUZDESI: 0 = asagi (dinlenme), 100 = yukari.
// -1 = "bu kolu degistirme".
//
// NEDEN YUZDE: iki servo karsilikli monte edilmis, yani ayni fiziksel
// hareket icin sag kolun acisi aynalanmali. Yuzde kullanmak o geometriyi
// TEK YERDE tutuyor (pati_beden.cpp · KOL_SAG_AYNA). Panel ve jest
// tablosu servo geometrisini hic bilmiyor; montaj degisirse tek bir
// sabit degisiyor.
void beden_kol(int sol_yuzde, int sag_yuzde);

// Hazir jest oynatir. Bilinmeyen ad false donuyor (panel yaziyor).
//
// Adlar: "dinlen", "selam", "iki_kol", "alkis", "dusun"
bool beden_jest(const char* ad);

// ---------------------------------------------------------------------------
// Konusma akisindan ITILIYOR
// ---------------------------------------------------------------------------
//
// pati_sohbet.cpp'de gozler_konusuyor() diyen yerin yaninda cagriliyor.
// Beden konusma durumunu SORMUYOR.
//
// Pati konusurken kollar rastgele jest yapiyor, arada rastgele 3-7
// saniye bekliyor. Aralik sabit olsaydi iki cumlede fark edilir ve
// mekanik gorunurdu.
void beden_konusma_bildir(bool konusuyor);

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------

// /api/durum icin JSON parcasi: "beden":{...} (disi suslu parantez yok).
std::string beden_json();

// Kac kez takildi — teshis. Kablo temassizsa bu sayi hizla artiyor ve
// "beden ara ara kayboluyor" sikayeti sayiyla karsilanabiliyor.
std::uint32_t beden_takma_sayisi();

}  // namespace pati
