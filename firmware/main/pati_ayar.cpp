#include "pati_ayar.hpp"

#include "pati_beden_matematik.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>     // std::abs(float) — seviye karsilastirmasi
#include <cstdio>
#include <cstring>
#include <utility>

#include <esp_log.h>
#include <nvs.h>

#include "pati_anahtar.hpp"
#include "pati_ekran.hpp"
#include "pati_ses.hpp"

namespace pati {
namespace {

constexpr const char* ETIKET = "ayar";
constexpr const char* NVS_ALAN = "pati";

// Sinirlar — panelin gonderdigine GUVENMIYORUZ. Tarayici tarafinda
// kaydiricinin min/max'i var ama istek elle de atilabilir; sinir
// firmware'de olmali.
constexpr float HIZ_EN_AZ = 0.85f;
constexpr float HIZ_EN_FAZLA = 1.45f;
constexpr int UYKU_EN_AZ = 1;
constexpr int UYKU_EN_FAZLA = 15;
constexpr int VAD_EN_AZ = 200;
constexpr int VAD_EN_FAZLA = 2000;

// Bedenin surus hizi tavani, yuzde. Sinirlar
// pati_beden_matematik.hpp'de — tek kaynak, cunku karistirma da ayni
// araligi kirpiyor.
//
// EN AZ 40: altinda tekerlek donmuyor, yalnizca otuyor (06.09.2026'da
// gercek kartta olculdu). Kaydiriciya o araligi koymak, hicbir sey
// yapmayan bir yer birakmak olurdu.
//
// ⚠️ PANEL BU SAYIYI GOSTERMIYOR. Panel 0-100 gosteriyor ve donusumu
// kendisi yapiyor (pati.js · gosterilenden_gercege): gosterilen 0 ->
// 40, gosterilen 100 -> 100. Yani varsayilan 70, panelde %50 diye
// gorunuyor.
//
// Varsayilan bilincli olarak tam guc DEGIL: masada oynanacak bir robot
// icin tam guc fazla. Ebeveyn isterse yukseltiyor.
constexpr int PARLAKLIK_EN_AZ = 5;
constexpr int PARLAKLIK_EN_FAZLA = 100;

constexpr int BEDEN_HIZ_EN_AZ = HIZ_TAVAN_EN_AZ;
constexpr int BEDEN_HIZ_EN_FAZLA = HIZ_TAVAN_EN_COK;

std::string g_ses_adi;
float g_hiz = 1.30f;
int g_uyku_dk = 4;

// 🔴 EKRAN PARLAKLIGI — 13.09.2026'DA AYAR OLDU.
//
// Oncesinde app_main'de sabit bir sayiydi ve kullanici uc kez "kistik
// ama fark etmiyorum" dedi. Sorunun yazilimda olmadigi A/B testiyle
// kanitlandi (%4 ile %100 yuklenip karsilastirildi, fark bariz):
// arka isik PWM'i calisiyor, gozun ayirt edemedigi sey 0,45 -> 0,35 ->
// 0,25 adimlarinin KUCUKLUGUYDU.
//
// Bundan sonra deger tahmin edilmiyor, ebeveyn seciyor. Ayni zamanda
// bir teshis araci: kaydiriciyi ucdan uca gezdirmek arka isigin
// calisip calismadigini bes saniyede gosteriyor.
//
// ⚠️ Taban %5, cunku 13.09.2026'da olculdu: %4'te gozler HALA
// goruluyor (kullanicinin gozlemi). Eski taban 0,15'ti ve tahmine
// dayaniyordu. Yine de sifir degil — sonuk ekran cocuga "bozuldu"
// diye okunuyor.
int g_parlaklik = 25;
bool g_soz_kesme = false;
// Kablo takilinca acik kalsin mi. Varsayilan HAYIR: kullanicinin
// istegi "kapaliyken sarja takinca acilmasin" (12.09.2026).
int g_vad_ms = 0;
bool g_yuz = true;
int g_beden_hiz = 70;   // panelde %50

// 🔴 IKISI DE VARSAYILAN ACIK — kullanicinin acik istegi (06.09.2026):
// "default olarak acik gelsin".
//
// Acik olmasi guvenli, cunku ozerk hareket YAPISAL olarak yer
// degistiremiyor (pati_beden_matematik.hpp · jest_donus). Eski
// `sevinc` anahtari varsayilan KAPALIYDI ve o dogruydu: o zaman
// tekerlegin Pati'ye acilmasi denenmemis bir seydi.
int g_tekerlek_kip = KIP_ACIK;

// Kol araliklari. RAM'de tutuluyor cunku beden gorevi her 20 ms'lik
// tikte okuyor — kalici depo flash ve sicak dongude yeri yok.
int g_kol_az[2]  = {KOL_SOL_VARSAYILAN_AZ,  KOL_SAG_VARSAYILAN_AZ};
int g_kol_cok[2] = {KOL_SOL_VARSAYILAN_COK, KOL_SAG_VARSAYILAN_COK};

// Kalici depodaki adlar. Kisa: NVS anahtari en fazla 15 karakter.
bool g_kol_ters = false;
const char* const KOL_AD_TERS = "kol_ters";

// 🔴 SURUS YONU VE HIZ SINIRI DA KALICI BOLUMDE.
//
// Kullanicinin istegi (13.09.2026): "bu ayar kalici olsun, fabrika
// ayarlarina don'e basinca da kalsin."
//
// `surus_ters` kural geregi zaten oraya ait: kol_ters gibi bir TERCIH
// degil, BU GOVDENIN NASIL KABLOLANDIGINI anlatan bir olcum. Kaybolursa
// cocuk cubugu ileri itip Pati'yi geri surer — masa kenarinda
// duzeltilemez bir surpriz.
//
// ⚠️ `beden_hiz` ICIN AYNI SEY SOYLENEMEZ ve bu bilincli bir
// istisna. O bir donanim olcumu degil, ebeveynin tercihi; "yalnizca
// kaybolmasi donanima zarar veren sayilar" kuralinin disinda kaliyor.
// Kullanici acikca istedi, bedeli de kucuk ve TEK YONLU: fabrika
// sifirlamasi artik hiz sinirini varsayilana (70, panelde %50)
// DONDURMUYOR. Yani robotu baska bir cocuga verirken sifirlamak, hiz
// sinirini de sifirlamiyor — panelden elle bakilmali.
const char* const AD_SURUS_TERS = "surus_ters";
const char* const AD_DONUS_TERS = "donus_ters";
const char* const AD_BEDEN_HIZ  = "beden_hiz";

// 🔴 SES SEVIYESI VE PARLAKLIK DA KALICI BOLUMDE — 13.09.2026,
// kullanicinin acik istegi: "fabrika ayarlarina don'e bassam da
// yaptigim ayar kalsin."
//
// ⚠️ IKISI DE `beden_hiz` ILE AYNI SINIFTAN: donanim olcumu DEGIL,
// ebeveynin tercihi. Yani "yalnizca kaybolmasi donanima zarar veren
// sayilar" kuralinin disindalar ve kurali bir kez daha deliyorlar.
// Bedeli tek yonlu ve panelde yazili: fabrika sifirlamasi artik sesi
// ve parlakligi varsayilana DONDURMUYOR.
//
// 🔴 Sinir su: bu bolum "kaybolmasi pahaliya patlayan sayilar"
// icin, "kullanicinin sevdigi sayilar" icin DEGIL. Buraya yeni bir
// ayar koymadan once sorulacak soru hala ayni — kaybolursa ne olur?
// Cevap "ebeveyn tekrar ayarlar" ise oraya ait degildir; yoksa
// sifirlamanin hicbir anlami kalmaz.
const char* const AD_SES_SEVIYE = "ses_seviye";   // BINDE (0.65 -> 650)
const char* const AD_PARLAKLIK  = "parlaklik";
bool g_surus_ters = false;
bool g_donus_ters = false;

const char* const KOL_AD_AZ[2]  = {"kol_sol_az",  "kol_sag_az"};
const char* const KOL_AD_COK[2] = {"kol_sol_cok", "kol_sag_cok"};
int g_kol_kip = KIP_ACIK;

std::atomic<bool> g_yenileme{false};

nvs_handle_t ac(nvs_open_mode_t mod)
{
    nvs_handle_t h = 0;
    if (nvs_open(NVS_ALAN, mod, &h) != ESP_OK) return 0;
    return h;
}

void i32_yaz(const char* anahtar, std::int32_t v)
{
    const nvs_handle_t h = ac(NVS_READWRITE);
    if (h == 0) return;
    nvs_set_i32(h, anahtar, v);
    nvs_commit(h);
    nvs_close(h);
}

void str_yaz(const char* anahtar, const std::string& v)
{
    const nvs_handle_t h = ac(NVS_READWRITE);
    if (h == 0) return;
    nvs_set_str(h, anahtar, v.c_str());
    nvs_commit(h);
    nvs_close(h);
}

}  // namespace

// ---------------------------------------------------------------------------

esp_err_t ayar_baslat()
{
    // Varsayilanlar Kconfig'den: kayit yoksa Asama 2'deki davranisin
    // aynisi calisiyor, yani eski olcumlerle karsilastirilabilirlik
    // bozulmuyor.
    g_ses_adi = CONFIG_PATI_SES;
    g_vad_ms = CONFIG_PATI_VAD_SESSIZLIK_MS;
#ifdef CONFIG_PATI_YARIM_DUPLEKS
    // Kconfig'deki kol "yarim dupleks ACIK" demek; soz kesme onun TERSI.
    //
    // Eskiden iki dal da `false` veriyordu, yani kol hicbir sey
    // yapmiyordu. Daha kotusu: yanki korumasi ayri bir DERLEME ZAMANI
    // `#if`ine bagliydi ve `ayar_soz_kesme()` hicbir yerden
    // okunmuyordu — panelin "Pati'nin sozu kesilebilsin" dugmesi
    // NVS'e yaziyor ama davranisa dokunmuyordu. Panelin yalan
    // soylemesi bu projede ucuncu kez cikti (PLAN.md).
    g_soz_kesme = false;
#else
    g_soz_kesme = true;
#endif

    const nvs_handle_t h = ac(NVS_READONLY);
    if (h == 0) {
        ESP_LOGI(ETIKET, "kayit yok, varsayilanlar kullaniliyor");
        return ESP_OK;
    }

    char t[33];
    size_t n = sizeof(t);
    if (nvs_get_str(h, "ses_adi", t, &n) == ESP_OK && t[0] != '\0') {
        g_ses_adi = t;
    }
    std::int32_t v = 0;
    if (nvs_get_i32(h, "hiz_yuz", &v) == ESP_OK) {
        // Yuzde olarak saklaniyor: NVS'te float yok ve i32 tasinabilir.
        g_hiz = std::clamp(static_cast<float>(v) / 100.0f,
                           HIZ_EN_AZ, HIZ_EN_FAZLA);
    }
    // ⚠️ PARLAKLIK ARTIK BURAYA YAZILMIYOR, yalnizca ESKI kayitlar
    // icin okunuyor. Asil yeri kalici bolum (asagida); bu satirlar
    // 3.5.30 ve oncesinden gelen cihazlarin ayarini tasimak icin var.
    bool parlaklik_eski_kayit = false;
    if (nvs_get_i32(h, "parlaklik", &v) == ESP_OK) {
        g_parlaklik = std::clamp(static_cast<int>(v), PARLAKLIK_EN_AZ,
                                 PARLAKLIK_EN_FAZLA);
        parlaklik_eski_kayit = true;
    }
    if (nvs_get_i32(h, "uyku_dk", &v) == ESP_OK) {
        g_uyku_dk = std::clamp(static_cast<int>(v), UYKU_EN_AZ, UYKU_EN_FAZLA);
    }
    if (nvs_get_i32(h, "soz_kesme", &v) == ESP_OK) g_soz_kesme = (v != 0);
    if (nvs_get_i32(h, "vad_ms", &v) == ESP_OK) {
        g_vad_ms = (v == 0) ? 0 : std::clamp(static_cast<int>(v),
                                             VAD_EN_AZ, VAD_EN_FAZLA);
    }
    if (nvs_get_i32(h, "yuz", &v) == ESP_OK) g_yuz = (v != 0);
    if (nvs_get_i32(h, "tekerlek", &v) == ESP_OK) {
        g_tekerlek_kip = std::clamp(static_cast<int>(v), KIP_KAPALI, KIP_ACIK);
    } else if (nvs_get_i32(h, "hareket", &v) == ESP_OK) {
        // ESKI ANAHTARDAN GECIS. 3.2.x'te tek bir acma-kapama vardi:
        // "konusurken kipirdasin". Kapali demek "tekerlek yalnizca
        // joystick'ten donsun" demekti — yani tam olarak KIP_KUMANDA.
        //
        // Gecis olmasaydi anahtari kapatmis bir ebeveyn guncellemeden
        // sonra Pati'yi yine kipirdar bulurdu ve ayarinin sessizce
        // kayboldugunu fark etmezdi.
        g_tekerlek_kip = (v != 0) ? KIP_ACIK : KIP_KUMANDA;
        ESP_LOGI(ETIKET, "eski 'hareket' anahtari tasindi: tekerlek=%d",
                 g_tekerlek_kip);
    }
    if (nvs_get_i32(h, "kol", &v) == ESP_OK) {
        g_kol_kip = std::clamp(static_cast<int>(v), KIP_KAPALI, KIP_ACIK);
    }

    // 🔴 KOL ARALIKLARI BASKA BOLUMDEN OKUNUYOR — bu handle'dan degil.
    // Ayni yerde olsalardi ayar_sifirla() onlari da goturebilirdi.
    for (int i = 0; i < 2; ++i) {
        int t = 0;
        if (kalici_sayi_oku(KOL_AD_AZ[i], t))  g_kol_az[i]  = std::clamp(t, 0, 100);
        if (kalici_sayi_oku(KOL_AD_COK[i], t)) g_kol_cok[i] = std::clamp(t, 0, 100);
        if (g_kol_az[i] > g_kol_cok[i]) std::swap(g_kol_az[i], g_kol_cok[i]);
    }
    {
        // Kol yonu de AYNI kalici bolumde — gerekcesi pati_ayar.hpp'de,
        // ayar_kol_ters()'in yaninda: kaybolursa kol ters yone gider ve
        // bir yere carpar.
        int t = 0;
        if (kalici_sayi_oku(KOL_AD_TERS, t)) g_kol_ters = (t != 0);
        if (kalici_sayi_oku(AD_SURUS_TERS, t)) g_surus_ters = (t != 0);
        if (kalici_sayi_oku(AD_DONUS_TERS, t)) g_donus_ters = (t != 0);
        if (kalici_sayi_oku(AD_BEDEN_HIZ, t)) {
            g_beden_hiz = std::clamp(t, BEDEN_HIZ_EN_AZ, BEDEN_HIZ_EN_FAZLA);
        }

        // 🔴 SES SEVIYESI. Burada yalnizca okunmuyor, UYGULANIYOR
        // da: seviyenin saklandigi yer zaten ses katmanindaki global
        // (pati_ses.cpp · g_seviye), ayri bir uygulama adimi yok.
        //
        // ⚠️ Donanim sirasi onemsiz: ses_seviyesi_ayarla() yalnizca
        // bir float kirpip yaziyor, I2S'e ya da kodege dokunmuyor.
        // Kirpma da orada — flash'tan bozuk bir sayi gelse bile
        // sinirin disina cikamiyor.
        if (kalici_sayi_oku(AD_SES_SEVIYE, t)) {
            ses_seviyesi_ayarla(static_cast<float>(t) / 1000.0f);
        }

        // 🔴 PARLAKLIK — ve ESKI KAYITTAN GECIS.
        //
        // 3.5.30'a kadar normal NVS'teydi, yani ayar_sifirla() onu
        // goturuyordu. Guncellemeden sonra ebeveynin sectigi deger
        // sessizce %25'e donseydi, tam da duzeltmeye calistigimiz sey
        // kullaniciya bir kez daha yasanirdi.
        //
        // Eski kayit bir kez tasiniyor ve KAYNAGINDAN SILINIYOR:
        // degerin iki evi olursa hangisinin gecerli oldugu ileride
        // kimse icin belli olmaz.
        if (kalici_sayi_oku(AD_PARLAKLIK, t)) {
            g_parlaklik = std::clamp(t, PARLAKLIK_EN_AZ, PARLAKLIK_EN_FAZLA);
        } else if (parlaklik_eski_kayit) {
            kalici_sayi_yaz(AD_PARLAKLIK, g_parlaklik);
            const nvs_handle_t hy = ac(NVS_READWRITE);
            if (hy != 0) {
                nvs_erase_key(hy, "parlaklik");
                nvs_commit(hy);
                nvs_close(hy);
            }
            ESP_LOGI(ETIKET, "parlaklik kalici bolume tasindi: %%%d",
                     g_parlaklik);
        }
    }
    nvs_close(h);

    // ⚠️ SEVIYE VE PARLAKLIK DA BU SATIRDA. app_main daha once
    // "ses seviyesi baslangic" diye bir satir basiyor ama o, ayarlar
    // OKUNMADAN once kosuyor — yani derleme varsayilanini yaziyor.
    // Ebeveynin sectigi degeri gosteren tek satir bu.
    ESP_LOGI(ETIKET, "ses=%s seviye=%.2f hiz=%.2f uyku=%d dk soz_kesme=%d "
                     "vad=%d yuz=%d parlaklik=%d beden_hiz=%d tekerlek=%d "
                     "kol=%d",
             g_ses_adi.c_str(), static_cast<double>(ses_seviyesi()), g_hiz,
             g_uyku_dk, g_soz_kesme ? 1 : 0, g_vad_ms, g_yuz ? 1 : 0,
             g_parlaklik, g_beden_hiz, g_tekerlek_kip, g_kol_kip);
    return ESP_OK;
}

const std::string& ayar_ses_adi() { return g_ses_adi; }
float ayar_hiz() { return g_hiz; }
int ayar_uyku_dk() { return g_uyku_dk; }
int ayar_parlaklik() { return g_parlaklik; }
bool ayar_soz_kesme() { return g_soz_kesme; }
int ayar_vad_ms() { return g_vad_ms; }
bool ayar_yuz_araci() { return g_yuz; }

// 🔴 RAM'DEN OKUNUYOR, NVS'TEN DEGIL. beden_surus() bunu her komutta
// (saniyede ~7 kez) cagiriyor; NVS'e gitseydi surus yolunda flash
// erisimi olurdu — CLAUDE.md'deki sicak dongu tuzaginin ta kendisi.
int ayar_beden_hiz() { return g_beden_hiz; }
int ayar_tekerlek_kip() { return g_tekerlek_kip; }

bool ayar_kol_ters() { return g_kol_ters; }
bool ayar_surus_ters() { return g_surus_ters; }
bool ayar_donus_ters() { return g_donus_ters; }

void ayar_surus_ters_yaz(bool ters)
{
    if (ters == g_surus_ters) return;
    g_surus_ters = ters;
    // KALICI bolume: fabrika sifirlamasi bunu silmiyor. Gerekcesi
    // pati_ayar.hpp'de, ayar_surus_ters()'in yaninda.
    kalici_sayi_yaz(AD_SURUS_TERS, ters ? 1 : 0);
    ESP_LOGI(ETIKET, "surus yonu: %s", ters ? "TERS" : "normal");
}

void ayar_donus_ters_yaz(bool ters)
{
    if (ters == g_donus_ters) return;
    g_donus_ters = ters;
    // surus_ters ile AYNI kalici bolumde: ikisi de bu govdenin nasil
    // kablolandigini anlatiyor, bir tercih degil.
    kalici_sayi_yaz(AD_DONUS_TERS, ters ? 1 : 0);
    ESP_LOGI(ETIKET, "donus yonu: %s", ters ? "TERS" : "normal");
}

void ayar_kol_ters_yaz(bool ters)
{
    if (ters == g_kol_ters) return;
    g_kol_ters = ters;
    // KALICI DEPOYA — ayar_sifirla() buraya dokunmuyor. Gerekcesi
    // pati_ayar.hpp'de ayar_kol_ters()'in yaninda.
    kalici_sayi_yaz(KOL_AD_TERS, ters ? 1 : 0);
    ESP_LOGI(ETIKET, "kol yonu: %s", ters ? "TERS" : "normal");
}

int ayar_kol_en_az(int taraf)
{
    return g_kol_az[(taraf == 1) ? 1 : 0];
}

int ayar_kol_en_cok(int taraf)
{
    return g_kol_cok[(taraf == 1) ? 1 : 0];
}

void ayar_kol_araligi_yaz(int taraf, int en_az, int en_cok)
{
    const int i = (taraf == 1) ? 1 : 0;
    int a = std::clamp(en_az, 0, 100);
    int b = std::clamp(en_cok, 0, 100);
    // Ters aralik gelirse duzeltiliyor, reddedilmiyor: panelde iki ayri
    // cubuk var ve ebeveyn once tabani tavanin ustune itebilir. Istegi
    // yok saymak, cubugun takildigini dusundururdu.
    if (a > b) std::swap(a, b);
    if (a == g_kol_az[i] && b == g_kol_cok[i]) return;
    g_kol_az[i]  = a;
    g_kol_cok[i] = b;
    // KALICI DEPOYA — ayar_sifirla() buraya dokunmuyor.
    kalici_sayi_yaz(KOL_AD_AZ[i], a);
    kalici_sayi_yaz(KOL_AD_COK[i], b);
    ESP_LOGI(ETIKET, "%s kol araligi: %%%d - %%%d (kalici)",
             (i == 1) ? "sag" : "sol", a, b);
}
int ayar_kol_kip() { return g_kol_kip; }

void ayar_ses_adi_yaz(const std::string& ad)
{
    // Bos ad gelirse DOKUNMUYORUZ: Gemini bos voiceName ile setup'i
    // reddediyor ve robot hic konusmuyor.
    if (ad.empty() || ad == g_ses_adi) return;
    g_ses_adi = ad;
    str_yaz("ses_adi", ad);
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "ses: %s (tur sonunda gecerli)", ad.c_str());
}

void ayar_hiz_yaz(float hiz)
{
    const float y = std::clamp(hiz, HIZ_EN_AZ, HIZ_EN_FAZLA);
    if (std::abs(y - g_hiz) < 0.005f) return;
    g_hiz = y;
    i32_yaz("hiz_yuz", static_cast<std::int32_t>(y * 100.0f + 0.5f));
    // I2S saati sabit. Yazılım çarpanı tur sonunda değişir ki
    // aynı cümlenin ortasında Pati'nin tınısı değişmesin.
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "tizlik: %.2fx (tur sonunda gecerli)", y);
}

void ayar_ses_seviye_yaz(float seviye)
{
    const float onceki = ses_seviyesi();
    const float y = ses_seviyesi_ayarla(seviye);   // sinirlar ses katmaninda

    // ⚠️ DEGISMEDIYSE YAZMIYORUZ. Panel kaydiriciyi surukleme
    // boyunca gonderiyor (250 ms geciktirmeli — pati.js ·
    // gonderSeviye) ve ayni degeri birden cok kez gonderebiliyor;
    // her birini flash'a yazmak bedava degil. Esik yarim adim:
    // kaydiricinin adimi 0.05.
    if (std::abs(y - onceki) < 0.025f) return;

    kalici_sayi_yaz(AD_SES_SEVIYE, static_cast<int>(y * 1000.0f + 0.5f));
    ESP_LOGI(ETIKET, "ses seviyesi: %.2f", static_cast<double>(y));
}

void ayar_parlaklik_yaz(int yuzde)
{
    g_parlaklik = std::clamp(yuzde, PARLAKLIK_EN_AZ, PARLAKLIK_EN_FAZLA);
    // ⚠️ KALICI bolume — fabrika sifirlamasi bunu artik silmiyor.
    // Gerekcesi ve bedeli AD_PARLAKLIK'in taniminda yazili.
    kalici_sayi_yaz(AD_PARLAKLIK, g_parlaklik);
    // HEMEN uygula: yalnizca kaydetmek, ebeveynin kaydirdigi cubugun
    // hicbir sey yapmamasi demek olurdu — ki bu isin tamami zaten
    // "degisiyor mu, degismiyor mu" sorusundan cikti.
    ekran_parlaklik_ayarla(static_cast<float>(g_parlaklik) / 100.0f);
    ESP_LOGI(ETIKET, "parlaklik: %%%d", g_parlaklik);
}

void ayar_uyku_yaz(int dakika)
{
    g_uyku_dk = std::clamp(dakika, UYKU_EN_AZ, UYKU_EN_FAZLA);
    i32_yaz("uyku_dk", g_uyku_dk);
    ESP_LOGI(ETIKET, "uyku: %d dk", g_uyku_dk);
}

void ayar_soz_kesme_yaz(bool acik)
{
    g_soz_kesme = acik;
    i32_yaz("soz_kesme", acik ? 1 : 0);
    ESP_LOGI(ETIKET, "soz kesme: %s", acik ? "ACIK (kulaklik gerekiyor)"
                                           : "kapali");
}

void ayar_vad_yaz(int ms)
{
    const int y = (ms == 0) ? 0 : std::clamp(ms, VAD_EN_AZ, VAD_EN_FAZLA);
    if (y == g_vad_ms) return;
    g_vad_ms = y;
    i32_yaz("vad_ms", y);
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "sustu karari: %d ms (tur sonunda gecerli)", y);
}

void ayar_yuz_yaz(bool acik)
{
    if (acik == g_yuz) return;
    g_yuz = acik;
    i32_yaz("yuz", acik ? 1 : 0);
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "gozlerle duygu: %s (tur sonunda gecerli)",
             acik ? "acik" : "kapali");
}

void ayar_beden_hiz_yaz(int yuzde)
{
    const int y = std::clamp(yuzde, BEDEN_HIZ_EN_AZ, BEDEN_HIZ_EN_FAZLA);
    if (y == g_beden_hiz) return;
    g_beden_hiz = y;
    // ⚠️ KALICI bolume yaziliyor, normal ayarlarin yanina degil.
    // Kullanicinin istegi; bedeli ve gerekcesi AD_BEDEN_HIZ'in
    // taniminda yazili.
    kalici_sayi_yaz(AD_BEDEN_HIZ, y);
    // ANINDA gecerli: bir sonraki surus komutu yeni tavani kullaniyor.
    // Oturum yenilemesi gerekmiyor, bu ayar Gemini'ye gitmiyor.
}

namespace {

const char* kip_adi(int k)
{
    return (k == KIP_KAPALI) ? "kapali"
           : (k == KIP_KUMANDA) ? "sadece kumandadan" : "acik";
}

}  // namespace

void ayar_tekerlek_kip_yaz(int kip)
{
    const int k = std::clamp(kip, KIP_KAPALI, KIP_ACIK);
    if (k == g_tekerlek_kip) return;
    g_tekerlek_kip = k;
    i32_yaz("tekerlek", k);
    // Donanim tarafi ANINDA gecerli (beden gorevindeki tek bogaz).
    // Ama MODELE de soylenmesi gerekiyor: kip acik degilse donen
    // hareketleri hic secmemeli, yoksa "dans ediyorum!" der ve hicbir
    // sey donmez. O bilgi setup mesajinda gidiyor, yani tur sonu.
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "tekerlekler: %s (donanim aninda, model tur sonunda)",
             kip_adi(k));
}

void ayar_kol_kip_yaz(int kip)
{
    const int k = std::clamp(kip, KIP_KAPALI, KIP_ACIK);
    if (k == g_kol_kip) return;
    g_kol_kip = k;
    i32_yaz("kol", k);
    g_yenileme.store(true);
    ESP_LOGI(ETIKET, "kollar: %s (donanim aninda, model tur sonunda)",
             kip_adi(k));
}

void ayar_sifirla()
{
    const nvs_handle_t h = ac(NVS_READWRITE);
    if (h != 0) {
        // 🔴 "parlaklik" BU LISTEDE YOK ve olmamali: artik kalici
        // bolumde duruyor (AD_PARLAKLIK). Adini burada birakmak,
        // silinmeyen bir seyin silindigini dusundururdu — eski kayit
        // zaten acilistaki gecis sirasinda temizleniyor.
        for (const char* a : {"ses_adi", "hiz_yuz", "uyku_dk", "soz_kesme",

                              "vad_ms", "yuz", "tekerlek",
                              "kol", "hareket", "sevinc"}) {
            nvs_erase_key(h, a);
        }
        nvs_commit(h);
        nvs_close(h);
    }
    // ⚠️ KOL ARALIKLARI VE KOL YONU BILEREK SILINMIYOR. Baska
    // bolumdeler ve oraya hic dokunmuyoruz: kaybolmalarinin bedeli
    // servonun bir yere carpip bozulmasi (pati_ayar.hpp ·
    // ayar_kol_en_az ve ayar_kol_ters).
    //
    // Iki bagimsiz koruma var ve ikisi de bilincli: yukaridaki dongu
    // adi sayilan anahtarlari siliyor (kol_ters orada YOK), ve o
    // anahtarlar zaten baska bir flash bolumunde duruyor.
    ESP_LOGW(ETIKET, "ayarlar sifirlandi (kol araliklari ve yonu, surus "
                     "yonu, hiz siniri, ses seviyesi ve parlaklik "
                     "korundu)");
}

bool ayar_yenileme_gerekli() { return g_yenileme.load(); }
void ayar_yenileme_iste()
{
    // Govde TEK BIR ATOMIK STORE. Gerekcesi baslikta: beden algilama
    // sicak dongusunden cagriliyor.
    g_yenileme.store(true);
}

void ayar_yenileme_temizle() { g_yenileme.store(false); }

std::string ayar_json()
{
    char b[420];
    std::snprintf(b, sizeof(b),
                  "\"ses\":{\"seviye\":%.3f,\"en_az\":%.2f,\"en_fazla\":%.2f,"
                  "\"hiz\":%.2f,\"ses_adi\":\"%s\"},"
                  "\"uyku\":%d,\"parlaklik\":%d,"
                  "\"konusma\":{\"soz_kesme\":%s,\"vad\":%d,\"yuz\":%s},"
                  "\"kumanda\":{\"hiz\":%d,\"tekerlek\":%d,\"kol\":%d,"
                  "\"kol_ters\":%s,\"surus_ters\":%s,\"donus_ters\":%s,"
                  "\"kol_sol_az\":%d,\"kol_sol_cok\":%d,"
                  "\"kol_sag_az\":%d,\"kol_sag_cok\":%d}",
                  ses_seviyesi(), SES_SEVIYESI_EN_AZ, SES_SEVIYESI_EN_FAZLA,
                  g_hiz, g_ses_adi.c_str(), g_uyku_dk, g_parlaklik,
                  g_soz_kesme ? "true" : "false", g_vad_ms,
                  g_yuz ? "true" : "false",
                  g_beden_hiz, g_tekerlek_kip, g_kol_kip,
                  g_kol_ters ? "true" : "false",
                  g_surus_ters ? "true" : "false",
                  g_donus_ters ? "true" : "false",
                  g_kol_az[0], g_kol_cok[0], g_kol_az[1], g_kol_cok[1]);
    return b;
}

}  // namespace pati
