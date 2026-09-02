// Pati'nin tuslari — StickS3'un iki programlanabilir dugmesi
//
// ===========================================================================
// NEDEN YOKLAMA, KESME DEGIL
// ===========================================================================
//
// GPIO kesmesi daha "dogru" gorunuyor ama burada bir seyi cozmuyor:
// mekanik dugme her basista onlarca kez sekiyor (contact bounce), yani
// kesme de tek basina yetmiyor ve yine bir zamanlayici gerekiyor.
// Ustelik kesme ISR baglami demek, oradan ekran isi tetiklemek icin
// ayrica bir kuyruk kurmak gerekirdi.
//
// 20 ms'de bir yoklama hem sekmeyi filtreliyor hem de tek bir dongude
// bitiyor. Bedeli olculebilir degil: iki GPIO okumasi.
//
// ===========================================================================
// HANGI TUS HANGISI — HENUZ BILINMIYOR
// ===========================================================================
//
// 🔴 pati_pinler.h iki tus tanimliyor (G11, G12) ama HANGISININ ekranin
// sagindaki mavi dugme oldugu olculmedi. M5Stack'in tablosu bu iki pini
// "programlanabilir" diye geciyor, fiziksel yerini yazmiyor.
//
// O yuzden IKISI DE ayni isi yapiyor ve basilan tus gunluge yaziliyor.
// Seri porttan hangisinin dustugu gorulunce buraya yazilacak; o zamana
// kadar davranis dogru, sadece bilgi eksik.

#pragma once

#include <esp_err.h>

namespace pati {

// Basista cagrilan islev. TUS GOREVINDEN cagriliyor.
//
// `hangi` 1 ya da 2 (PATI_TUS_1 / PATI_TUS_2).
// `uzun`  true ise dugme UZUN SURE basili tutuldu.
//
// ⚠️ KISA BASIS BLOKLAMAMALI. Uzun basis blokLAYABILIR — o yol Pati'yi
// derin uykuya sokuyor ve zaten geri donmuyor.
//
// UZUN BASIS ERKEN BILDIRILIYOR: dugme birakilmayi beklemeden, esik
// dolar dolmaz. Sebebi kullanici geri bildirimi — parmagini bir
// saniyeden fazla tuttugunda bir sey OLMALI, yoksa insan tusun
// calismadigini dusunup birakiyor. Ayni basis ayrica kisa basis olarak
// SAYILMIYOR.
using TusIslevi = void (*)(int hangi, bool uzun);

// Tus gorevini baslatir.
//
// Basarisiz olursa PROGRAM DURMAMALI: tussuz Pati konusabiliyor, sadece
// bilgi sayfasi acilamaz.
esp_err_t tus_baslat(TusIslevi tiklandi);

// Kac kez basildi — teshis icin. Tus hic calismiyor mu, yoksa
// calisiyor da ekran mi acilmiyor: ayirt eden sey bu sayi.
unsigned tus_sayisi();

}  // namespace pati
