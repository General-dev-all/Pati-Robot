// Pati'nin kendi paneli — ebeveynin telefonundan actigi sayfa.
//
// ===========================================================================
// AYNI DOSYALAR, IKI YER
// ===========================================================================
//
// `panel/` klasorundeki sayfa hem bilgisayarda (Python sunucusu) hem
// burada (ESP32) servis ediliyor. Dosyalar CMake `EMBED_FILES` ile
// firmware'e gomuluyor, yani ikisi ayni kaynaktan besleniyor ve
// ayrilmalari mumkun degil.
//
// Sayfa hangi tarafta oldugunu KENDISI anliyor: `/api/durum` cevap
// verirse robot modu, vermezse tezgah modu (WebSocket) — bkz. pati.js.
//
// ===========================================================================
// CAPTIVE PORTAL
// ===========================================================================
//
// Kurulum modunda (ag kayitli degil ya da baglanamadi) iki sey birden
// calisiyor:
//
//   1. DNS ELE GECIRME. UDP 53'te her A sorgusuna kendi IP'mizi
//      donduruyoruz. Telefon hangi adresi sorarsa sorsun bize geliyor.
//
//   2. ISLETIM SISTEMI YOKLAMALARI. Telefon aga baglanir baglanmaz
//      "internet var mi" diye bilinen bir adrese bakiyor:
//        Android  /generate_204
//        iOS      /hotspot-detect.html
//        Windows  /connecttest.txt · /ncsi.txt
//      Bunlara beklenen cevabi VERMEYIP yonlendirme dondurunce telefon
//      "oturum acmaniz gerekiyor" sayfasini kendiliginden aciyor.
//
// Ikisi birden gerekli: sadece DNS olsa telefon sayfayi kendiliginden
// acmiyor, sadece yoklama olsa adresler cozulemiyor.
//
// ===========================================================================
// SOHBET SIRASINDA
// ===========================================================================
//
// ⚠️ PLAN.md: panel gecikme yolunda yer almamali. HTTP sunucusu
// dinlemede duruyor (bos bir dinleyen soket neredeyse bedava) ama
// istekler AYRI GOREVDE isleniyor ve onceligi ses gorevinin ALTINDA.
// Ebeveyn tam konusma aninda paneli acarsa gecikmeye ne kadar
// eklendigi HENUZ OLCULMEDI — PLAN.md'da bilinmeyen olarak duruyor.

#pragma once

#include <esp_err.h>

namespace pati {

// HTTP sunucusunu baslatir. Kurulum modundaysa DNS ele gecirmeyi de
// acar.
//
// Ag hazir olmasa bile cagrilabilir: kurulum modunda AP uzerinden
// hizmet veriyor.
esp_err_t panel_baslat();

// Captive portal DNS'ini baslatir (yalnizca kurulum modunda is yapar).
//
// NEDEN AYRI: panel, ag daha "ariyor" halindeyken aciliyor; kurulum
// moduna GECIS ondan sonra oluyor. Panelin icinde kalsaydi kayitli ag
// olmayan cihazda captive portal hic calismazdi — gerekce .cpp'de.
//
// Birden fazla kez cagrilabilir, ikincisi hicbir sey yapmaz.
void panel_dns_baslat();

// Kurulum modundan cikildi (ag baglandi): DNS ele gecirme kapansin.
// Acik kalirsa ev agindaki butun DNS sorgularina cevap vermeye
// devam eder ve baska cihazlarin internetini bozar.
void panel_kurulum_bitti();

}  // namespace pati
