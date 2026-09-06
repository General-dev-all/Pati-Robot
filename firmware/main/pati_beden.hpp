// Pati'nin bedeni — iki tekerlek, iki kol
//
// StickS3 bir gövdeye takilabiliyor. Gövdede iki DC motor (L9110S
// surucu), iki mini servo ve kendi 4'lu AA pil yuvasi var. Pin haritasi
// ve "AA'nin artisi bu karta gelmiyor" kurali pati_pinler.h'de.
//
// ===========================================================================
// 🔴 PATI KENDI KARARIYLA DONER, KENDI KARARIYLA ILERLEMEZ
// ===========================================================================
//
// Kollar Pati konusurken kendiliginden hareket ediyor. Tekerlekler de
// ediyor — ama YALNIZCA YERINDE DONEREK (06.09.2026, kullanicinin
// karari; oncesinde tekerlek tamamen panele bagliydi).
//
// Karar degisti, GEREKCESI DEGISMEDI. Eski kural "tekerlekler ozerk
// degil" diyordu ve sebebi sensor eksikligi degil, YER DEGISTIRMEYDI:
// ucurum sensoru olmayan bir robot ilerledigi surece eninde sonunda
// masadan duser, ve "az hareket etsin" bunu geciktirir, engellemez.
//
// O yuzden ozerkligin tamami TEK BIR SAYIYLA anlatiliyor:
//
//     sol = +teker        sag = -teker
//
// Iki tekerlek her zaman ters yonde, yani Pati yerinde doner ve yer
// DEGISTIRMEZ. Bu bir yorumdaki uyari degil, TIPIN KENDISI: jest
// tablosunda ileri giden bir kare yazilamiyor
// (pati_beden_matematik.hpp · Kare, jest_donus). Kurali bilmeyen biri
// de bozamiyor, ve konak testi her girdide sol + sag == 0 oldugunu
// tariyor.
//
// ILERLEMEK HALA YALNIZCA PANELDEN, cocugun parmagi altinda ve olu adam
// zamanlayicisiyla. Sesli komut ilerletmiyor; Pati "yuruyemem"
// diyor ve paneli gosteriyor (prototype/yuz.py · BEDEN_PROMPT_EKI).
//
// ⚠ SESLI KOMUTUN ASIL RISKI YANLIS ANLAMA DEGIL, DOGRU ANLAMA.
// "Ileri git" DOGRU anlasilirsa da Pati masadan duser; ustelik
// joystick'in aksine sesli komutun ustunde parmak yok, yani olu adam
// zamanlayicisi onu koruyamaz — onay surekli degil, tek seferlik.
//
// Bir gun uzaklik ya da ucurum sensoru eklenirse ilerleme de acilabilir;
// o zamana kadar acilmamali.
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
// Surus — ILERLEMEK YALNIZCA BURADAN
// ---------------------------------------------------------------------------
//
// Pati'yi gercekten YERINDEN OYNATAN tek yol bu fonksiyon, ve tek
// cagirani panel. Ozerk hareket ve sesli komut buraya hic ugramiyor;
// onlar yerinde donusle sinirli (bkz. beden_jest).
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
// Cagiranlar: panelin jest dugmeleri VE modelin arac cagrisi
// (pati_sohbet.cpp — cocuk "dans et" dedi).
//
// Adlar ve tablo pati_beden_matematik.hpp'de:
//   yalnizca kol : dinlen, selam, iki_kol, alkis, dusun
//   tekerlekli   : sevin, titre, hayir, bak_etrafina, dans
//
// 🔴 TEKERLEKLI JESTLERIN HEPSI YERINDE DONUYOR. Yer degistirebilen
// bir jest tabloya yazilamiyor.
//
// Ebeveynin "konusurken kipirdasin" anahtari BURADA bakilmiyor, beden
// gorevindeki tek bogazda bakiliyor: yeni bir cagiran eklenince
// unutulacak ikinci bir kontrol olmasin diye.
bool beden_jest(const char* ad);

// ---------------------------------------------------------------------------
// Konusma akisindan ITILIYOR
// ---------------------------------------------------------------------------
//
// pati_sohbet.cpp'de gozler_konusuyor() diyen yerin yaninda cagriliyor.
// Beden konusma durumunu SORMUYOR.
//
// Pati konusurken rastgele jest yapiyor, arada rastgele 3-7 saniye
// bekliyor. Aralik sabit olsaydi iki cumlede fark edilir ve mekanik
// gorunurdu.
//
// Jestlerin ucte biri tekerlekli (yerinde donus) ve iki tekerlekli
// jest arasinda en az 12 saniye var. Seyreklik bir sus degil:
// yerinde donus yer degistirmiyor ama tekerlek kaymasi her donuste
// birkac milimetrelik ikinci dereceden bir surunme birakiyor.
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
