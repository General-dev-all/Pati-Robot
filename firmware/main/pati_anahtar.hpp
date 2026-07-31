// Gemini API anahtari — panelden giriliyor, kendi NVS bolumunde duruyor.
//
// ===========================================================================
// NEDEN DERLEMEDE DEGIL
// ===========================================================================
//
// Anahtar Asama 2'de Kconfig'deydi (CONFIG_PATI_GEMINI_API_KEY) ve
// firmware'in ICINE gomuluyordu. O asamada dogruydu: tek is gecikme
// olcmekti ve binary kimseye verilmiyordu.
//
// Panelden guncelleme gelince bu tutulamaz oldu. Guncelleme demek
// `pati.bin`'i GitHub'a koymak demek; gomulu anahtar da onunla birlikte
// herkese acilir ve faturayi anahtarin sahibi oder. Kconfig girdisi bu
// yuzden KALDIRILDI — birakilsaydi bir gun biri doldurur, derler,
// yayimlar ve kimse fark etmezdi. Sessizce yanlis olan bir sey, hic
// olmayan bir seyden tehlikelidir.
//
// Simdi anahtar cihazda: anne panele bir kez yaziyor, NVS'te kaliyor.
// Firmware'de anahtar YOK, yani `pati.bin` herkese acik durabilir.
//
// ===========================================================================
// NEDEN AYRI BIR NVS BOLUMU ("anahtar")
// ===========================================================================
//
// Iki senaryo icin, ikisi de gercek:
//
//   FABRIKA AYARLARI. Panel wifi'yi, hafizayi ve ayarlari siliyor.
//   Anahtar bunlarla birlikte silinirse anne robotu sifirladigi her
//   seferde Google hesabina gidip anahtari yeniden almak zorunda kalir.
//   Anahtar cocuga ait bir veri degil, kurulum bilgisi.
//
//   NVS BOZULMASI. Bu robotun beklenen kapanma bicimi fisin cekilmesi.
//   Yarim yazma NVS'i bozabiliyor ve app_main bozuk NVS'i silip yeniden
//   kuruyor (nvs_flash_erase) — dogru davranis. Ama o silme "nvs"
//   bolumune bakiyor; anahtar baska bolumde oldugu icin kurtuluyor.
//
// Ayrinti partitions.csv'de.
//
// ===========================================================================
// DURUM — "calismiyor" yetmez, NEDEN calismadigi lazim
// ===========================================================================
//
// Panelde tek bir "Pati konusmuyor" yazisi anneye hicbir sey soylemiyor.
// Yapabilecegi sey sebebe gore DEGISIYOR:
//
//   Gecersiz     -> yeni anahtar yazacak
//   Kota         -> Google'a para yukleyecek (ya da yeni anahtar)
//   Ulasilamadi  -> hicbir sey yapmayacak, ag duzelince kendi gelecek
//
// Ucunu birbirine karistirmak en kotu sonucu veriyor: ag koptugu icin
// susan robota bakip "anahtarim bitti" deyip para yuklemek. O yuzden
// `Ulasilamadi` ayri bir durum ve ANAHTARIN SUCU SAYILMIYOR.

#pragma once

#include <string>

#include <esp_err.h>

namespace pati {

enum class AnahtarDurumu {
    Yok,          // hic girilmedi — kurulum tamamlanmamis
    Bilinmiyor,   // kayitli ama denenmedi (henuz aga cikilmadi)
    Gecerli,      // Google kabul etti
    Gecersiz,     // Google reddetti: anahtar yanlis, iptal ya da suresi gecmis
    Kota,         // Google 429 dondu: kota ya da bakiye bitti
    Ulasilamadi,  // istek Google'a hic ulasmadi — AG sorunu, anahtarin degil
};

// Ayri NVS bolumunu acar ve kayitli anahtari okur.
//
// Bolum yoksa (eski bolum tablosuyla yuklenmis bir kart) hata dondurur ve
// sebebini yazar. Cokmuyoruz: robot anahtarsiz da acilip paneli gostersin,
// yoksa sorun hic anlasilmaz.
esp_err_t anahtar_baslat();

bool anahtar_var();

// Kayitli anahtarin kopyasi. Yoksa bos dize.
//
// Kopya donuyor, isaretci degil: cagiran taraf bunu HTTP isteginde
// kullaniyor ve o sirada panelden yeni anahtar yazilabilir.
std::string anahtar_al();

// Kirpip dogrular ve NVS'e yazar. Kabul edilmezse false.
//
// Dogrulama BICIM denetimi: bosluk kirpiliyor, kopyala-yapistirda gelen
// tirnaklar atiliyor, uzunluk ve karakter kumesi bakiliyor. Anahtarin
// GERCEKTEN calisip calismadigi buradan anlasilmiyor — onu yalnizca
// Google soyleyebilir (anahtar_dogrula).
bool anahtar_yaz(const std::string& yeni);

void anahtar_sil();

AnahtarDurumu anahtar_durumu();

// Google'a en ucuz istegi atip anahtari siner. BLOKLUYOR (~1-3 sn).
//
// Model listesinden tek satir istiyor: govde kucuk, belirtec harcamiyor,
// ucretlendirilmiyor. Sohbet baglantisini denemek yerine bu kullaniliyor
// cunku WebSocket el sikismasi basarisiz oldugunda HTTP durum kodu
// gorunmuyor — "anahtar mi bozuk, ag mi kopuk" ayirt edilemiyor.
AnahtarDurumu anahtar_dogrula();

// Baska bir istekten donen HTTP kodunu durum olarak isler.
//
// Hafiza cikarimi zaten Google'a REST istegi atiyor ve kodu goruyor;
// ayrica bir dogrulama istegi atmak gereksiz olurdu.
void anahtar_kod_bildir(int http_kod);

// Sohbet baglantisi kurulamadi. Sebep bilinmiyor (WebSocket kod vermiyor),
// o yuzden bir dogrulama istegiyle soruyoruz.
//
// HEMEN DONER, isi ayri bir gorevde yapar. Cagiranlarin biri mikrofon
// gorevi (uyandirma) ve orada beklemek cocugun sesini kesmek olurdu;
// ustelik bloklu cagri gercek kartta YANLIS CEVAP verdi (ayrinti
// pati_anahtar.cpp §anahtar_baglanti_hatasi).
//
// Kendini sinirliyor: en fazla dakikada bir. Baglanti hatasi arka arkaya
// gelebiliyor ve her birinde Google'a istek atmak, kotasi zaten dolu olan
// bir anahtari daha da doldurmak olurdu.
void anahtar_baglanti_hatasi();

// Panelin gosterecegi hali (JSON parcasi, disi suslu parantez yok).
std::string anahtar_json();

}  // namespace pati
