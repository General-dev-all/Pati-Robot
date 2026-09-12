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

// Bedenin surus hizi tavani (10-100). Panel degil FIRMWARE uyguluyor:
// sinir cihazda dursun, panel gonderse bile asilamasin.
int ayar_beden_hiz();

// ---------------------------------------------------------------------------
// 🔴 UZUVLARIN KIPI — tekerlekler ve kollar AYRI AYRI
// ---------------------------------------------------------------------------
//
// Kullanicinin istegi (06.09.2026): "dc motorlarin sesli komutlar dahil
// tamamen kapatacak bir ayar... ayri sekilde kollar yani servolar icin
// de koy. Belki cocuk motorlarin veya servolarin konusma boyunca hic
// hareket etmesini istemez."
//
// ACMA-KAPAMA YETMIYOR, CUNKU UC HAL VAR ve ucu de gercekten isteniyor:
//
//   KIP_ACIK     Pati kendi de kullaniyor — konusurken kipirdiyor,
//                cocugun sesli komutunu dinliyor, panel de calisiyor.
//                VARSAYILAN.
//
//   KIP_KUMANDA  Yalnizca panelden, cocugun parmagi altinda. Pati
//                kendi kararıyla oynatmiyor ve sesli komutu
//                dinlemiyor. "Kendi kendine kipirdamasin ama cocuk
//                oynatabilsin."
//
//   KIP_KAPALI   Hicbiri. Motor hic donmuyor, servo hic darbe
//                almiyor. Gurultu istemeyen, pil suresini uzatmak
//                isteyen ya da Pati'yi rafta tutan icin.
//
// Iki acma-kapama ile ayni sey anlatilamazdi: "kapali" ile "sadece
// kumandadan" arasindaki fark, cocugun elinden kumandayi alip almamak.
//
// 🔴 KIP MODELE DE SOYLENIYOR, yalnizca motora degil. Yalnizca donanimi
// durdurmak yetmiyor: Pati "dans ediyorum!" der, hicbir sey oynamaz ve
// cocuk robotun bozuldugunu dusunur. O yuzden kip degisimi oturum
// tazelemesi istiyor (setup mesaji).
// enum DEGIL int: adsiz bir enum std::clamp'in sablon cikarimini
// bozuyor (clamp(int, enum, enum) eslesmiyor) ve hata mesaji
// "no matching function" diye cikip sebebi gorunmuyor.
inline constexpr int KIP_KAPALI  = 0;
inline constexpr int KIP_KUMANDA = 1;
inline constexpr int KIP_ACIK    = 2;

int ayar_tekerlek_kip();
int ayar_kol_kip();

// Hepsi NVS'e isliyor. Tur sonu gerektirenler bayragi kaldiriyor.
void ayar_ses_adi_yaz(const std::string& ad);
void ayar_hiz_yaz(float hiz);
void ayar_uyku_yaz(int dakika);
void ayar_soz_kesme_yaz(bool acik);
void ayar_vad_yaz(int ms);
void ayar_yuz_yaz(bool acik);
void ayar_beden_hiz_yaz(int yuzde);
// ---------------------------------------------------------------------------
// 🔴 KOL ARALIKLARI — FABRIKA AYARLARINDAN KURTULUYOR
// ---------------------------------------------------------------------------
//
// Kolun gidebilecegi yuzde araligi, her kol icin ayri. Mekanik bir
// sinir: sol kol asagida tekerlege, yukarida ustteki kabloya carpiyor
// (gerekce pati_beden_matematik.hpp).
//
// ⚠️ BU IKISI `ayar_sifirla()` ILE SILINMIYOR ve bu bilincli. Ayri bir
// flash bolumunde duruyorlar (pati_anahtar.hpp · kalici_sayi_oku),
// yani ne fabrika ayarlari ne de NVS bozulmasi onlari goturuyor.
//
// Kullanicinin gerekcesi (09.09.2026): "bir kere ayarlandiktan sonra
// fabrika ayarlarini sifirla denilirse ve tekrar ayarlanmasi
// unutulursa bir yerlere carpip servo bozulabilir."
//
// `taraf`: 0 sol, 1 sag.
// 🔴 KOL YONU TERS MI — ikinci gövdede servolar ters takili.
//
// Kullanicinin sikayeti (12.09.2026): powerbank'li gövdede "kolunu
// kaldir" deyince kol ASAGI iniyor. Servolar ters monte edilmis.
//
// ⚠️ BU AYAR KALICI DEPODA ve `ayar_sifirla()` ONA DOKUNMUYOR — kol
// araliklariyla ayni gerekce, kullanicinin kendi sozleriyle: "cocuk
// fabrika ayarlarina don'e basarsa ve yonleri degistirmeyi unutsa
// kollar bir yere carpabilir." Kaybolmasi DONANIMA ZARAR VEREN bir
// sayi, o yuzden oraya konuldu.
//
// Varsayilan false = eski gövdenin davranisi, hicbir sey degismiyor.
bool ayar_kol_ters();

int ayar_kol_en_az(int taraf);
int ayar_kol_en_cok(int taraf);
void ayar_kol_araligi_yaz(int taraf, int en_az, int en_cok);
void ayar_kol_ters_yaz(bool ters);

void ayar_tekerlek_kip_yaz(int kip);
void ayar_kol_kip_yaz(int kip);

void ayar_sifirla();

// Oturumun tazelenmesi gerekiyor mu? Sohbet dongusu bakiyor ve
// tazeledikten sonra temizliyor.
bool ayar_yenileme_gerekli();
void ayar_yenileme_temizle();

// Ayar disi bir sebeple tazeleme iste.
//
// Tek cagirani beden algilama (pati_beden.cpp): beden takilip
// cikarildiginda modele giden arac semasi ve prompt eki degisiyor —
// `hareket` alani yalnizca beden takiliyken gonderiliyor, cunku
// olmayan bir bedeni anlatmak Pati'ye yapamayacagi bir sey vaat
// ettirirdi.
//
// 🔴 SICAK DONGUDEN CAGRILIYOR: govdesi tek bir atomik store olmali.
// NVS, kilit ya da I2C eklenirse CLAUDE.md'deki sicak dongu tuzagina
// dusulur (02.09.2026: cokme arasi 4,5 dk -> 0,7 dk).
void ayar_yenileme_iste();

// Panelin gosterdigi degerler (JSON parcasi, disi suslu parantez yok).
std::string ayar_json();

}  // namespace pati
