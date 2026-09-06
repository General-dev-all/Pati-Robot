#include "pati_beden.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pati_ayar.hpp"
#include "pati_beden_matematik.hpp"
#include "pati_pinler.h"

namespace pati {
namespace {

constexpr char ETIKET[] = "beden";

// ---------------------------------------------------------------------------
// LEDC dagilimi — CAKISMA KONTROL EDILDI
// ---------------------------------------------------------------------------
//
// Arka isik zaten LEDC_TIMER_1 / LEDC_CHANNEL_1 kullaniyor
// (pati_ekran.cpp · ISIK_ZAMANLAYICI). ESP32-S3'te 4 zamanlayici ve 8
// kanal var (yalnizca dusuk hiz kipi; S3'te yuksek hiz kipi YOK).
//
//   TIMER_1 / CH1        arka isik    (var olan)
//   TIMER_2 / CH2, CH3   kollar       50 Hz
//   TIMER_3 / CH4..CH7   motorlar     20 kHz
//
// TIMER_0 ve CH0 bos kaliyor.
constexpr ledc_timer_t   KOL_ZAMANLAYICI   = LEDC_TIMER_2;
constexpr ledc_timer_t   MOTOR_ZAMANLAYICI = LEDC_TIMER_3;

// ---- kollar ---------------------------------------------------------------
//
// Aci ve darbe matematigi pati_beden_matematik.hpp'de: donanima
// dokunmadigi icin konak testinde siniyor (servo darbesinin araligin
// disina cikmasi orada yakalaniyor).
constexpr int SERVO_HZ   = 50;
constexpr int SERVO_BIT  = 14;                 // 16384 adim
constexpr int SERVO_ADIM = 1 << SERVO_BIT;     // 20 ms / 16384 = 1,22 us

// SG90 5 V'ta kabaca 600 derece/saniye. 400'de tutuluyor: hem kalkis
// akimi dusuyor hem hareket "firlamis" degil "canli" gorunuyor.
constexpr int KOL_HIZ_DERECE_SN = 400;

// 🔴 KOL HEDEFE VARINCA DARBE KESILIYOR — VIZILTI ICIN.
//
// Servo konumunu tutarken duyulur sekilde vizildiyor ve mikrofon 10 cm
// otede, surekli acik. Darbe kesilince servo tutma torku uygulamiyor ve
// SUSUYOR. Hafif plastik bir kol kendi agirligiyla dusmuyorsa bu bedava;
// dusuyorsa dinlenme acisi yercekimine yaslanacak sekilde secilmeli.
//
// Yan faydasi: bostaki akim sifira iniyor, AA pil uzun omurlu oluyor.
constexpr std::int64_t KOL_SUS_GECIKME_US = 250000;   // 250 ms

// ---- motorlar -------------------------------------------------------------
//
// 🔴 5 kHz — 20 kHz DENENDI VE MOTORLAR HIC DONMEDI (06.09.2026).
//
// Once 20 kHz yazilmisti, gerekcesi de yaziliydi: mikrofon 10 cm otede
// ve surekli acik, dusuk frekansli PWM civiltisi dogrudan Gemini'ye
// gidiyor. Gerekce dogruydu ama ONCELIK YANLISTI.
//
// Gercek kartta olculdu — beden takili, tekerlekler sokulu, AA pil dolu:
//
//     hiz tavani %60  -> panel `beden.sol=60`  ·  motor DONMEDI
//     hiz tavani %100 -> panel `beden.sol=100` ·  motor DONDU
//
// Yani komut sonuna kadar dogru gidiyordu (panel, karistirma, tavan,
// LEDC — hepsi olculdu ve saglamdi); is sinyalin L9110'un cikisina
// donusmesinde bitiyordu. %100'de duty 1023/1024, yani ANAHTARLAMA
// NEREDEYSE HIC YOK — cikis surekli DC. Kirpilmis her duty'de surucunun
// kenarlari yetismiyor.
//
// L9110 bipolar bir surucu; kenarlari yavas ve uzerinde ~1 V dusuyor.
// 1-10 kHz rahat calistigi aralik.
//
// ⚠️ BELIRTISI YANILTICIYDI: "servolar oynuyor, motorlar oynamiyor"
// tam bir KABLO hatasi gibi gorunuyor. Kabloya bakmadan once panelden
// `beden.sol/sag` okunmali — orada dogru sayi varsa sorun kabloda
// DEGIL, bu satirdadir.
//
// Duyulacak: evet, hafif bir civilti olacak. Ama motor donerken TT
// redukturunun mekanik sesi zaten ondan yuksek, ve motorlar yalnizca
// cocuk surerken doniyor.
//
// Cozunurluk, olu bolge ve duty hesabi pati_beden_matematik.hpp'de.
constexpr int MOTOR_HZ = 5000;

// 🔴 OLU ADAM ZAMANLAYICISI — PAZARLIKSIZ.
//
// Panel dokunma surerken 150 ms'de bir gonderiyor. Dort paket ust uste
// kaybolursa motorlar duruyor.
//
// Cocuk parmagini kaldirirsa, telefon kilitlenirse, wifi takilirsa,
// tarayici kapanirsa Pati DURUYOR. Panelin sifir gondermesine
// guvenmiyoruz: unutan bir panel, kacan bir robot demek.
constexpr std::int64_t OLU_ADAM_US = 600000;   // 600 ms

// ---- sevinc donusu --------------------------------------------------------
//
// Iki motor TERS yonde donuyor: robot yerinde doner, YER DEGISTIRMEZ.
// Yapi geregi masadan dusemez — tekerlekleri Pati'ye acmanin tek
// guvenli bicimi bu.
//
// Varsayilan KAPALI (ayar_sevinc). Suresi kodda sabit ve kisa; ne olursa
// olsun 2 x 150 ms sonra kendiliginden bitiyor.
constexpr int SEVINC_HIZ = 55;
constexpr std::int64_t SEVINC_FAZ_US = 150000;

// ---- zamanlama ------------------------------------------------------------
constexpr int DONGU_MESGUL_MS = 20;    // bir sey hareket ederken
constexpr int DONGU_BOS_MS    = 200;   // bosta

// Algilamanin kararli kalmasi gereken sure. Takip cikarirken kontak
// sekiyor; sekmeyi durum degisikligi saymak panelin kumanda kartini
// acip kapatmasi demek olurdu.
constexpr std::int64_t ALGILA_KARARLI_US = 1000000;   // 1 sn

// Konusurken jestler arasi bekleme, rastgele araligin ucu.
//
// SABIT ARALIK OLMAZ: iki cumlede fark ediliyor ve kol mekanik
// gorunuyor. Rastgelelik burada susleme degil, canlilik.
constexpr std::int64_t JEST_ARA_EN_AZ_US  = 3000000;
constexpr std::int64_t JEST_ARA_EN_COK_US = 7000000;

// ---------------------------------------------------------------------------
// Jest tablosu
// ---------------------------------------------------------------------------
//
// Kare = "iki kolu su yuzdeye getir, varinca su kadar bekle".
// -1 = "bu kolu degistirme". Yuzde 0 = asagi, 100 = yukari.

struct Kare {
    std::int8_t   sol;
    std::int8_t   sag;
    std::uint16_t bekle_ms;
};

constexpr Kare JEST_DINLEN[]  = {{0, 0, 0}};
constexpr Kare JEST_SELAM[]   = {{-1, 85, 90}, {-1, 55, 90},
                                 {-1, 85, 90}, {-1, 55, 90}, {0, 0, 0}};
constexpr Kare JEST_IKI_KOL[] = {{95, 95, 320}, {0, 0, 0}};
constexpr Kare JEST_ALKIS[]   = {{55, 55, 60}, {30, 30, 60}, {55, 55, 60},
                                 {30, 30, 60}, {55, 55, 60}, {0, 0, 0}};
constexpr Kare JEST_DUSUN[]   = {{50, 0, 420}, {0, 0, 0}};

struct Jest {
    const char*   ad;
    const Kare*   kare;
    std::uint8_t  adet;
    bool          kendiliginden;   // konusurken secilebilir mi
};

constexpr Jest JESTLER[] = {
    {"dinlen",  JEST_DINLEN,  1, false},
    {"selam",   JEST_SELAM,   5, true},
    {"iki_kol", JEST_IKI_KOL, 2, true},
    {"alkis",   JEST_ALKIS,   6, true},
    {"dusun",   JEST_DUSUN,   2, true},
};
constexpr int JEST_ADET = sizeof(JESTLER) / sizeof(JESTLER[0]);

// ---------------------------------------------------------------------------
// Paylasilan durum — HEPSI ATOMIK, hicbiri kilit istemiyor
// ---------------------------------------------------------------------------

std::atomic<bool> g_kuruldu{false};
std::atomic<bool> g_takili{false};
std::atomic<std::uint32_t> g_takma{0};

std::atomic<int> g_sol{0};            // -100..100
std::atomic<int> g_sag{0};
std::atomic<std::int64_t> g_surus_us{0};

std::atomic<int> g_kol_hedef[2] = {{0}, {0}};      // yuzde 0..100
std::atomic<std::uint32_t> g_kol_elle{0};          // elle mudahale sayaci
std::atomic<int> g_jest_istek{-1};
std::atomic<bool> g_konusuyor{false};
std::atomic<bool> g_sevinc_istek{false};

TaskHandle_t g_gorev = nullptr;

// Motor kanallari: 0 sol-ileri, 1 sol-geri, 2 sag-ileri, 3 sag-geri.
constexpr ledc_channel_t MOTOR_KANAL[4] = {
    LEDC_CHANNEL_4, LEDC_CHANNEL_5, LEDC_CHANNEL_6, LEDC_CHANNEL_7,
};
constexpr gpio_num_t MOTOR_PIN[4] = {
    PATI_BEDEN_SOL_ILERI, PATI_BEDEN_SOL_GERI,
    PATI_BEDEN_SAG_ILERI, PATI_BEDEN_SAG_GERI,
};
constexpr ledc_channel_t KOL_KANAL[2] = {LEDC_CHANNEL_2, LEDC_CHANNEL_3};
constexpr gpio_num_t KOL_PIN[2] = {PATI_BEDEN_KOL_SOL, PATI_BEDEN_KOL_SAG};

// ---------------------------------------------------------------------------
// Donanima yazan iki fonksiyon — dongunun tek donanim temasi
// ---------------------------------------------------------------------------

void kanal_duty(ledc_channel_t k, int duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, k, static_cast<std::uint32_t>(duty));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, k);
}

// Darbe genisligini (us) LEDC duty'sine cevirir: 20 ms'lik cerceve
// SERVO_ADIM adima bolunuyor.
int kol_duty(int derece10)
{
    return std::clamp(kol_darbe_us(derece10) * SERVO_ADIM / 20000,
                      0, SERVO_ADIM - 1);
}

// Bir tekerlegi surer. `hiz` -100..100.
//
// Yon "hangi girise darbe verildigi" ile seciliyor: ileri icin A pini
// darbeli B pini sifir, geri icin tersi. Ikisi birden yuksek olsaydi
// motor frenlerdi; bu durum burada hic uretilmiyor.
void tekerlek_sur(int ileri_kanal, int geri_kanal, int hiz)
{
    const int duty = motor_duty(hiz);

    if (hiz > 0) {
        kanal_duty(MOTOR_KANAL[geri_kanal], 0);
        kanal_duty(MOTOR_KANAL[ileri_kanal], duty);
    } else if (hiz < 0) {
        kanal_duty(MOTOR_KANAL[ileri_kanal], 0);
        kanal_duty(MOTOR_KANAL[geri_kanal], duty);
    } else {
        kanal_duty(MOTOR_KANAL[ileri_kanal], 0);
        kanal_duty(MOTOR_KANAL[geri_kanal], 0);
    }
}

std::int64_t rastgele_ara()
{
    const std::uint32_t r = esp_random();
    return JEST_ARA_EN_AZ_US
           + static_cast<std::int64_t>(r % (JEST_ARA_EN_COK_US
                                            - JEST_ARA_EN_AZ_US));
}

// ---------------------------------------------------------------------------
// Beden gorevi
// ---------------------------------------------------------------------------
//
// 🔴 ICINDE I2C, KILIT VE NVS YOK. Tek donanim temasi LEDC yazmaclari ve
// bir GPIO okumasi; geri kalani atomik okuma ve tam sayi aritmetigi.
// Gerekce pati_beden.hpp'nin basinda.
//
// 20 ms'lik dongu YALNIZCA bir sey hareket ederken calisiyor; bosta 200
// ms'de bir uyaniyor ve komut gelince bildirimle uyandiriliyor.

void beden_gorevi(void*)
{
    // Kollarin gercek konumu ve zamanlamasi GOREVE OZEL — paylasilmiyor,
    // dolayisiyla kilit de gerekmiyor.
    int su_an10[2] = {kol_derece10(0, false), kol_derece10(0, true)};
    std::int64_t vardi_us[2] = {0, 0};
    bool darbe_acik[2] = {false, false};

    // Akan jest.
    const Jest* akan = nullptr;
    int kare_no = 0;
    std::int64_t kare_bekle_bitis = 0;
    std::uint32_t elle_gorulen = g_kol_elle.load();

    // Konusma jesti zamanlamasi.
    std::int64_t sonraki_jest_us = 0;

    // Sevinc donusu.
    int sevinc_faz = -1;
    std::int64_t sevinc_faz_bitis = 0;

    // Uygulanan motor degerleri — degismedikce yazmaca dokunmuyoruz.
    int uygulanan_sol = 0, uygulanan_sag = 0;

    // Kalkis darbesi ne zaman bitiyor (0 = darbe yok).
    std::int64_t kalkis_bitis[2] = {0, 0};

    // Algilama.
    bool ham_son = (gpio_get_level(PATI_BEDEN_ALGILA) == 0);
    std::int64_t ham_us = 0;

    while (true) {
        const std::int64_t simdi = esp_timer_get_time();

        // ---- ALGILAMA ---------------------------------------------------
        const bool ham = (gpio_get_level(PATI_BEDEN_ALGILA) == 0);
        if (ham != ham_son) {
            ham_son = ham;
            ham_us = simdi;
        }
        const bool takili = g_takili.load(std::memory_order_relaxed);
        if (ham != takili && simdi - ham_us >= ALGILA_KARARLI_US) {
            g_takili.store(ham, std::memory_order_relaxed);
            if (ham) {
                g_takma.fetch_add(1, std::memory_order_relaxed);
                ESP_LOGW(ETIKET, "BEDEN TAKILDI (%u. kez)",
                         static_cast<unsigned>(g_takma.load()));
                // Pati bedeninin geldigini fark etsin: kollari dinlenme
                // konumuna al ve bir kez el salla.
                g_kol_hedef[0].store(0);
                g_kol_hedef[1].store(0);
                g_jest_istek.store(1);   // "selam"
                sonraki_jest_us = simdi + rastgele_ara();
            } else {
                ESP_LOGW(ETIKET, "beden cikarildi — her sey duruyor");
                // Cikarildi: HER SEY sifir. Pinler LEDC'de duty 0'da
                // kaliyor, yani asagi cekili.
                g_sol.store(0);
                g_sag.store(0);
                akan = nullptr;
                sevinc_faz = -1;
                g_jest_istek.store(-1);
                g_sevinc_istek.store(false);
            }
        }

        if (!g_takili.load(std::memory_order_relaxed)) {
            if (uygulanan_sol != 0 || uygulanan_sag != 0) {
                tekerlek_sur(0, 1, 0);
                tekerlek_sur(2, 3, 0);
                uygulanan_sol = uygulanan_sag = 0;
            }
            for (int i = 0; i < 2; ++i) {
                if (darbe_acik[i]) {
                    kanal_duty(KOL_KANAL[i], 0);
                    darbe_acik[i] = false;
                }
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DONGU_BOS_MS));
            continue;
        }

        // ---- OLU ADAM ---------------------------------------------------
        int istek_sol = g_sol.load(std::memory_order_relaxed);
        int istek_sag = g_sag.load(std::memory_order_relaxed);
        if ((istek_sol != 0 || istek_sag != 0)
            && simdi - g_surus_us.load(std::memory_order_relaxed)
                   > OLU_ADAM_US) {
            ESP_LOGW(ETIKET, "olu adam: komut kesildi, motorlar duruyor");
            g_sol.store(0);
            g_sag.store(0);
            istek_sol = istek_sag = 0;
        }

        // ---- SEVINC DONUSU ----------------------------------------------
        //
        // Yalnizca cocuk surmuyorken basliyor; surus istegi geldigi anda
        // iptal oluyor. Suresi yapisi geregi 2 x 150 ms.
        if (g_sevinc_istek.exchange(false)) {
            if (sevinc_faz < 0 && istek_sol == 0 && istek_sag == 0) {
                sevinc_faz = 0;
                sevinc_faz_bitis = simdi + SEVINC_FAZ_US;
            }
        }
        if (sevinc_faz >= 0 && (istek_sol != 0 || istek_sag != 0)) {
            sevinc_faz = -1;   // cocuk sürmeye basladi: sevinc iptal
        }
        if (sevinc_faz >= 0) {
            if (simdi >= sevinc_faz_bitis) {
                ++sevinc_faz;
                sevinc_faz_bitis = simdi + SEVINC_FAZ_US;
            }
            if (sevinc_faz == 0)      { istek_sol =  SEVINC_HIZ; istek_sag = -SEVINC_HIZ; }
            else if (sevinc_faz == 1) { istek_sol = -SEVINC_HIZ; istek_sag =  SEVINC_HIZ; }
            else                      { sevinc_faz = -1; }
        }

        // ---- KALKIS DARBESI + YUMUSAK RAMPA -----------------------------
        //
        // Motor DURURKEN kalkmak icin yuksek guc istiyor, ama donduginde
        // cok daha azi yetiyor. Ikisi tek sayiya baglandiginda robot ya
        // otuyor ya firliyor. Gerekce ve sayilar
        // pati_beden_matematik.hpp'de.
        //
        // Darbe YALNIZCA duruyorken harekete gecerken veriliyor ve
        // MOTOR_KALKIS_MS sonra kendiliginden bitiyor. Kendisi de
        // rampali — anlik siçrama yok.
        //
        // ⚠️ Sifira inis darbeden de rampadan da GECMIYOR: durmak
        // beklemez (olu adam, cocugun parmagini kaldirmasi).
        int hedef[2] = {istek_sol, istek_sag};
        const int mevcut[2] = {uygulanan_sol, uygulanan_sag};
        bool darbede[2] = {false, false};
        for (int i = 0; i < 2; ++i) {
            if (hedef[i] == 0) {
                kalkis_bitis[i] = 0;
                continue;
            }
            if (mevcut[i] == 0 && kalkis_bitis[i] == 0) {
                kalkis_bitis[i] = simdi + MOTOR_KALKIS_MS * 1000LL;
            }
            if (kalkis_bitis[i] != 0) {
                if (simdi < kalkis_bitis[i]) {
                    darbede[i] = true;
                    // Istek zaten darbeden buyukse istegi kullaniyoruz;
                    // darbe bir TABAN, tavan degil.
                    const int d = (hedef[i] > 0) ? MOTOR_KALKIS_DUTY
                                                 : -MOTOR_KALKIS_DUTY;
                    if (std::abs(hedef[i]) < MOTOR_KALKIS_DUTY) hedef[i] = d;
                } else {
                    kalkis_bitis[i] = 0;
                }
            }
        }
        const int yeni_sol = motor_rampa(hedef[0], uygulanan_sol, darbede[0]);
        const int yeni_sag = motor_rampa(hedef[1], uygulanan_sag, darbede[1]);
        if (yeni_sol != uygulanan_sol) {
            tekerlek_sur(0, 1, yeni_sol);
            uygulanan_sol = yeni_sol;
        }
        if (yeni_sag != uygulanan_sag) {
            tekerlek_sur(2, 3, yeni_sag);
            uygulanan_sag = yeni_sag;
        }
        // Rampa suruyorken de dongu 20 ms'de donmeli, yoksa kalkis
        // 200 ms'lik adimlara bolunur ve rampa anlamini yitirir.
        const bool suruyor = (istek_sol != 0 || istek_sag != 0
                              || uygulanan_sol != 0 || uygulanan_sag != 0);

        // ---- ELLE MUDAHALE JESTI IPTAL EDIYOR ---------------------------
        //
        // Cocuk paneldeki kol dugmesine bastiysa, akan jest onun ustune
        // yazmamali: dugmeye basip kolun geri gitmesi "bozuk" gorunur.
        const std::uint32_t elle = g_kol_elle.load(std::memory_order_relaxed);
        if (elle != elle_gorulen) {
            elle_gorulen = elle;
            akan = nullptr;
            sonraki_jest_us = simdi + rastgele_ara();
        }

        // ---- JEST BASLATMA ----------------------------------------------
        const int istek = g_jest_istek.exchange(-1, std::memory_order_relaxed);
        if (istek >= 0 && istek < JEST_ADET) {
            akan = &JESTLER[istek];
            kare_no = 0;
            kare_bekle_bitis = 0;
        } else if (akan == nullptr && g_konusuyor.load(std::memory_order_relaxed)
                   && !suruyor && simdi >= sonraki_jest_us) {
            // 🔴 TEKERLEKLER DONERKEN YENI JEST BASLAMIYOR.
            //
            // Ikisi de ayni AA hattindan besleniyor. Motor kalkisi
            // gerilimde cokuntu yapiyor; o anda servo hareket ederse
            // titriyor ya da sifirlaniyor. Kondansator gerektirmeyen
            // bedava bir onlem.
            int aday = -1;
            for (int deneme = 0; deneme < 8 && aday < 0; ++deneme) {
                const int s = static_cast<int>(esp_random() % JEST_ADET);
                if (JESTLER[s].kendiliginden && &JESTLER[s] != akan) aday = s;
            }
            if (aday >= 0) {
                akan = &JESTLER[aday];
                kare_no = 0;
                kare_bekle_bitis = 0;
            }
            sonraki_jest_us = simdi + rastgele_ara();
        }

        // ---- JEST ILERLETME ---------------------------------------------
        if (akan != nullptr) {
            const Kare& k = akan->kare[kare_no];
            if (k.sol >= 0) g_kol_hedef[0].store(k.sol, std::memory_order_relaxed);
            if (k.sag >= 0) g_kol_hedef[1].store(k.sag, std::memory_order_relaxed);

            const bool vardi =
                (su_an10[0] == kol_derece10(g_kol_hedef[0].load(), false))
                && (su_an10[1] == kol_derece10(g_kol_hedef[1].load(), true));

            if (vardi) {
                if (kare_bekle_bitis == 0) {
                    kare_bekle_bitis = simdi + k.bekle_ms * 1000LL;
                }
                if (simdi >= kare_bekle_bitis) {
                    kare_bekle_bitis = 0;
                    if (++kare_no >= akan->adet) akan = nullptr;
                }
            }
        }

        // ---- KOL HAREKETI -----------------------------------------------
        constexpr int ADIM10 = KOL_HIZ_DERECE_SN * DONGU_MESGUL_MS / 100;
        bool kol_oynuyor = false;
        for (int i = 0; i < 2; ++i) {
            const int hedef10 =
                kol_derece10(g_kol_hedef[i].load(std::memory_order_relaxed),
                               i == 1);
            if (su_an10[i] != hedef10) {
                const int fark = hedef10 - su_an10[i];
                su_an10[i] += std::clamp(fark, -ADIM10, ADIM10);
                vardi_us[i] = simdi;
                kol_oynuyor = true;
                if (!darbe_acik[i]) darbe_acik[i] = true;
                kanal_duty(KOL_KANAL[i], kol_duty(su_an10[i]));
            } else if (darbe_acik[i] && simdi - vardi_us[i] > KOL_SUS_GECIKME_US) {
                // Hedefe varildi ve bekleme doldu: DARBEYI KES.
                // Servo susuyor, akim sifira iniyor.
                kanal_duty(KOL_KANAL[i], 0);
                darbe_acik[i] = false;
            }
        }

        const bool mesgul = suruyor || kol_oynuyor || akan != nullptr
                            || darbe_acik[0] || darbe_acik[1];
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(mesgul ? DONGU_MESGUL_MS
                                                      : DONGU_BOS_MS));
    }
}

void uyandir()
{
    if (g_gorev != nullptr) xTaskNotifyGive(g_gorev);
}

}  // namespace

// ---------------------------------------------------------------------------

esp_err_t beden_baslat()
{
    // Ikinci cagri IKINCI BIR GOREV acardi ve iki gorev ayni LEDC
    // kanallarina yazardi — kol seyirir, sebebi "servo bozuk" sanilirdi.
    if (g_kuruldu.exchange(true)) return ESP_OK;

    // 🔴 ILK IS: MOTOR PINLERINI ASAGI CEK.
    //
    // ESP32 cikislari yazilim kurana kadar HAVADA kaliyor ve L9110'un
    // girisleri o sirada belirsiz. LEDC kanali duty 0 ile kurulunca pin
    // asagi cekiliyor, yani bu satirdan sonra motorlarin kendiliginden
    // donmesi mumkun degil.
    //
    // Beden takili OLMASA DA kuruluyor: bos havaya dusuk seviye surmenin
    // bedeli yok, ama takiliyken pinin bir an bile havada kalmamasinin
    // degeri var.
    ledc_timer_config_t mz{};
    mz.speed_mode      = LEDC_LOW_SPEED_MODE;
    mz.timer_num       = MOTOR_ZAMANLAYICI;
    mz.duty_resolution = static_cast<ledc_timer_bit_t>(MOTOR_BIT);
    mz.freq_hz         = MOTOR_HZ;
    mz.clk_cfg         = LEDC_AUTO_CLK;
    if (ledc_timer_config(&mz) != ESP_OK) {
        ESP_LOGE(ETIKET, "motor zamanlayicisi kurulamadi (%d Hz)", MOTOR_HZ);
        g_kuruldu.store(false);
        return ESP_FAIL;
    }
    for (int i = 0; i < 4; ++i) {
        ledc_channel_config_t k{};
        k.speed_mode = LEDC_LOW_SPEED_MODE;
        k.channel    = MOTOR_KANAL[i];
        k.timer_sel  = MOTOR_ZAMANLAYICI;
        k.gpio_num   = MOTOR_PIN[i];
        k.duty       = 0;
        k.hpoint     = 0;
        if (ledc_channel_config(&k) != ESP_OK) {
            ESP_LOGE(ETIKET, "motor kanali %d kurulamadi (GPIO %d)", i,
                     static_cast<int>(MOTOR_PIN[i]));
            g_kuruldu.store(false);
            return ESP_FAIL;
        }
    }

    // ---- kollar ---------------------------------------------------------
    ledc_timer_config_t kz{};
    kz.speed_mode      = LEDC_LOW_SPEED_MODE;
    kz.timer_num       = KOL_ZAMANLAYICI;
    kz.duty_resolution = static_cast<ledc_timer_bit_t>(SERVO_BIT);
    kz.freq_hz         = SERVO_HZ;
    kz.clk_cfg         = LEDC_AUTO_CLK;
    if (ledc_timer_config(&kz) != ESP_OK) {
        ESP_LOGE(ETIKET, "kol zamanlayicisi kurulamadi (%d Hz / %d bit)",
                 SERVO_HZ, SERVO_BIT);
        g_kuruldu.store(false);
        return ESP_FAIL;
    }
    for (int i = 0; i < 2; ++i) {
        ledc_channel_config_t k{};
        k.speed_mode = LEDC_LOW_SPEED_MODE;
        k.channel    = KOL_KANAL[i];
        k.timer_sel  = KOL_ZAMANLAYICI;
        k.gpio_num   = KOL_PIN[i];
        k.duty       = 0;          // darbe YOK — servo sessiz basliyor
        k.hpoint     = 0;
        if (ledc_channel_config(&k) != ESP_OK) {
            ESP_LOGE(ETIKET, "kol kanali %d kurulamadi (GPIO %d)", i,
                     static_cast<int>(KOL_PIN[i]));
            g_kuruldu.store(false);
            return ESP_FAIL;
        }
    }

    // ---- algilama -------------------------------------------------------
    gpio_config_t a{};
    a.pin_bit_mask = 1ULL << PATI_BEDEN_ALGILA;
    a.mode         = GPIO_MODE_INPUT;
    a.pull_up_en   = GPIO_PULLUP_ENABLE;
    a.pull_down_en = GPIO_PULLDOWN_DISABLE;
    a.intr_type    = GPIO_INTR_DISABLE;
    if (gpio_config(&a) != ESP_OK) {
        ESP_LOGE(ETIKET, "algilama pini kurulamadi (GPIO %d)",
                 static_cast<int>(PATI_BEDEN_ALGILA));
        g_kuruldu.store(false);
        return ESP_FAIL;
    }

    // Yigin 3072: atomik okumalar, tam sayi aritmetigi, LEDC yazmaci ve
    // ESP_LOG. Derin cagri zinciri yok — pati_tus'taki tuzak (bir bayragin
    // arkasinda esp_deep_sleep_start() olmasi) burada yok, ama bu goreve
    // is eklenirse yigin YENIDEN dusunulmeli.
    //
    // Oncelik 3: goz gorevi ile ayni. Kol hareketinin birkac milisaniye
    // gecikmesi gorunmuyor; ses gorevinin onune gecmesi ise duyulurdu.
    if (xTaskCreate(beden_gorevi, "pati_beden", 3072, nullptr, 3, &g_gorev)
        != pdPASS) {
        ESP_LOGE(ETIKET, "beden gorevi baslatilamadi");
        g_kuruldu.store(false);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(ETIKET, "beden katmani hazir · motor %d Hz · servo %d Hz · "
                     "algilama GPIO %d",
             MOTOR_HZ, SERVO_HZ, static_cast<int>(PATI_BEDEN_ALGILA));
    return ESP_OK;
}

bool beden_takili() { return g_takili.load(std::memory_order_relaxed); }

std::uint32_t beden_takma_sayisi()
{
    return g_takma.load(std::memory_order_relaxed);
}

void beden_surus(int x, int y)
{
    if (!g_takili.load(std::memory_order_relaxed)) return;

    // Karistirma ve hiz siniri CIHAZDA — panelde degil. Panel yalnizca
    // joystick'in nerede oldugunu soyluyor; iki tekerlegin ne yapacagina
    // ve tavanin ne oldugune burasi karar veriyor.
    //
    // 🔴 ayar_beden_hiz() RAM'den okunuyor (pati_ayar.cpp). Bu fonksiyon
    // surus sirasinda saniyede ~7 kez cagriliyor; NVS'e gitseydi surus
    // yolunda flash erisimi olurdu.
    const Surus m = surus_karistir(x, y, ayar_beden_hiz());
    g_sol.store(m.sol, std::memory_order_relaxed);
    g_sag.store(m.sag, std::memory_order_relaxed);
    g_surus_us.store(esp_timer_get_time(), std::memory_order_relaxed);
    uyandir();
}

void beden_kol(int sol_yuzde, int sag_yuzde)
{
    if (!g_takili.load(std::memory_order_relaxed)) return;
    if (sol_yuzde >= 0) {
        g_kol_hedef[0].store(std::clamp(sol_yuzde, 0, 100),
                             std::memory_order_relaxed);
    }
    if (sag_yuzde >= 0) {
        g_kol_hedef[1].store(std::clamp(sag_yuzde, 0, 100),
                             std::memory_order_relaxed);
    }
    g_kol_elle.fetch_add(1, std::memory_order_relaxed);
    uyandir();
}

bool beden_jest(const char* ad)
{
    if (ad == nullptr) return false;
    for (int i = 0; i < JEST_ADET; ++i) {
        if (std::strcmp(ad, JESTLER[i].ad) == 0) {
            if (!g_takili.load(std::memory_order_relaxed)) return true;
            g_jest_istek.store(i, std::memory_order_relaxed);
            uyandir();
            return true;
        }
    }
    // Bilinmeyen adi SESSIZCE YUTMUYORUZ: panel yeni bir jest gonderiyor
    // ve firmware tanimiyorsa, cocuk dugmeye basip hicbir sey olmadigini
    // gorur ve sebebini kimse bulamaz.
    ESP_LOGW(ETIKET, "bilinmeyen jest: %s", ad);
    return false;
}

void beden_konusma_bildir(bool konusuyor)
{
    const bool onceki = g_konusuyor.exchange(konusuyor,
                                             std::memory_order_relaxed);
    if (!g_takili.load(std::memory_order_relaxed)) return;

    if (konusuyor && !onceki) {
        // Konusma basladi: hemen bir jest ve (aciksa) sevinc donusu.
        g_jest_istek.store(1, std::memory_order_relaxed);   // "selam"
        if (ayar_sevinc()) g_sevinc_istek.store(true, std::memory_order_relaxed);
    } else if (!konusuyor && onceki) {
        g_kol_hedef[0].store(0, std::memory_order_relaxed);
        g_kol_hedef[1].store(0, std::memory_order_relaxed);
    }
    uyandir();
}

std::string beden_json()
{
    char t[160];
    std::snprintf(t, sizeof(t),
                  "\"beden\":{\"takili\":%s,\"kol_sol\":%d,\"kol_sag\":%d,"
                  "\"sol\":%d,\"sag\":%d,\"takma\":%u}",
                  beden_takili() ? "true" : "false",
                  g_kol_hedef[0].load(), g_kol_hedef[1].load(),
                  g_sol.load(), g_sag.load(),
                  static_cast<unsigned>(g_takma.load()));
    return t;
}

}  // namespace pati
