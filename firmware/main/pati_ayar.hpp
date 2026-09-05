// Panelden degistirilebilen ayarlar — NVS'te.
//
// ===========================================================================
// NEDEN AYRI BIR MODUL
// ===========================================================================
//
// Bu ayarlar Kconfig'de (derleme zamani) duruyordu: CONFIG_PATI_SES,
// CONFIG_PATI_VAD_SESSIZLIK_MS, PATI_YARIM_DUPLEKS. Derleme zamani
// olmalari Asama 2'de dogruydu — o asamanin tek amaci gecikme olcmekti
// ve ayar degistirmek icin yeniden derlemek sorun degildi.
//
// Gercek robotta OLMAZ: ebeveyn panelde ses secimini degistirdiginde
// bir sey olmasi gerekiyor. Aksi halde panel YALAN SOYLUYOR — ve bu
// hatanin birebir aynisi PC tarafinda yasandi (panel "cumlesini
// bitirince gecerli olur" diyordu, kod ise sadece degiskeni
// degistiriyordu).
//
// ===========================================================================
// HANGISI NE ZAMAN GECERLI
// ===========================================================================
//
//   seviye      ANINDA   — her ses parcasinda yeniden uygulaniyor
//   uyku        ANINDA   — sadece bir sayac degeri
//   soz_kesme   ANINDA   — mikrofon kapisi
//   ses_adi     TUR SONU — setup mesajinda gidiyor, oturum yenilenmeli
//   hiz         TUR SONU — yeniden örnekleme çarpanı, cümle içinde değişmesin
//   vad_ms      TUR SONU — setup mesajinda
//   yuz_araci   TUR SONU — setup mesajinda (tools + prompt eki)
//
// "TUR SONU" olanlar `ayar_yenileme_gerekli()` bayragini kaldiriyor;
// sohbet dongusu ilk dogal boslukta oturumu tazeliyor (GoAway ile ayni
// yol: olculen 568 ms, hafiza korunuyor).

#pragma once

#include <cstdint>
#include <string>

#include <esp_err.h>

namespace pati {

// NVS'ten okur. Kayit yoksa Kconfig varsayilanlariyla baslar — yani
// eski davranis bozulmuyor.
esp_err_t ayar_baslat();

const std::string& ayar_ses_adi();
float ayar_hiz();
int ayar_uyku_dk();
bool ayar_soz_kesme();
int ayar_vad_ms();          // 0 = Google varsayilani
bool ayar_yuz_araci();

// Hepsi NVS'e isliyor. Tur sonu gerektirenler bayragi kaldiriyor.
void ayar_ses_adi_yaz(const std::string& ad);
void ayar_hiz_yaz(float hiz);
void ayar_uyku_yaz(int dakika);
void ayar_soz_kesme_yaz(bool acik);
void ayar_vad_yaz(int ms);
void ayar_yuz_yaz(bool acik);

void ayar_sifirla();

// Oturumun tazelenmesi gerekiyor mu? Sohbet dongusu bakiyor ve
// tazeledikten sonra temizliyor.
bool ayar_yenileme_gerekli();
void ayar_yenileme_temizle();

// Panelin gosterdigi degerler (JSON parcasi, disi suslu parantez yok).
std::string ayar_json();

}  // namespace pati
