#include "pati_tus.hpp"

#include <atomic>

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pati_guc.hpp"
#include "pati_pinler.h"

namespace pati {
namespace {

constexpr char ETIKET[] = "tus";

// Yoklama araligi. 20 ms hem sekmeyi (contact bounce) filtrelemeye
// yetiyor hem de en hizli basisi bile kacirmiyor: insan parmagi bir
// dugmeyi 20 ms'den kisa basili tutamiyor.
constexpr int YOKLAMA_MS = 20;

// Bir basisin sayilmasi icin kac ardisik yoklamada basili gorulmeli.
//
// 2 x 20 = 40 ms. Mekanik sekme tipik olarak 5-15 ms suruyor, yani
// tek bir yoklama sekmeye takilabilir ama ikisi ust uste takilmaz.
constexpr int ONAY = 2;

// Ayni basisin iki kez sayilmamasi icin en kisa ara.
//
// 🔴 BU SATIR OLMAZSA SAYFA ACILIP HEMEN KAPANIR. Bilgi sayfasi
// "degistir" mantigiyla calisiyor: tek basista aciliyor, tekrar basista
// kapaniyor. Sekme yuzunden tek basis iki kez sayilirsa sayfa acilip
// ayni anda kapanir ve cocuga hicbir sey olmamis gibi gorunur.
constexpr std::int64_t EN_KISA_ARA_US = 300000;   // 300 ms

// Uzun basis esigi.
//
// 1,2 saniye: kazara basili kalmayi kisa basistan ayirmaya yetiyor ama
// cocugun sabrini zorlamiyor. Yan taraftaki guc dugmesinin uzun basmasi
// (indirme modu) M5PM1'in isi ve BU TUSLARLA ILGISI YOK — karismiyor.
constexpr std::int64_t UZUN_US = 1200000;

TusIslevi g_islev = nullptr;
std::atomic<unsigned> g_sayi{0};

struct Durum {
    gpio_num_t pin;
    int        hangi;
    int        basili_sayac;
    bool       basiliydi;
    bool       uzun_bildirildi;   // bu basista uzun zaten haber verildi
    std::int64_t bas_us;          // basisin BASLADIGI an
    std::int64_t son_us;
};

void tus_gorevi(void*)
{
    // ⚠️ MANTIK TERS: dugmeye basilinca pin TOPRAGA cekiliyor, yani
    // okunan deger 0 oluyor. Cekme direnci L2 katmaninda ve acilisla
    // birlikte hazir (pati_pinler.h), o yuzden dahili pull-up yeterli
    // ama yine de aciliyor — pin yapilandirmasi bu dosyada gorunur
    // olsun.
    Durum tuslar[2] = {
        {PATI_TUS_1, 1, 0, false, false, 0, 0},
        {PATI_TUS_2, 2, 0, false, false, 0, 0},
    };

    // ---- yan guc dugmesi: TEK TIK = KAPAT ---------------------------------
    //
    // M5PM1'den I2C ile okunuyor, ESP32 pininden degil.
    //
    // NEDEN TEK TIK: M5PM1'in kendi kapatma hareketi CIFT TIK ve bunu bir
    // cocuk tutturamiyor. Basili tutma da denendi ve kullanici "tus sert,
    // cocuk yapamaz" dedi — dogru itiraz, bu dugme gercekten sert.
    // Geriye en basit hareket kaliyor: bir kez bas, kapansin.
    //
    // Calismasinin sarti guc_baslat()'ta M5PM1'in tek-tik-sifirlamasinin
    // kapatilmis olmasi; acik olsaydi cihaz biz okuyamadan yeniden
    // baslardi.
    //
    // 🔴 ONCE BIRAKILMASINI BEKLIYORUZ. Cihaz bu dugmeye basilarak
    // aciliyor ve yazilim ayaga kalktiginda parmak HALA USTUNDE
    // olabiliyor. Bunu saymaya baslasaydik Pati acilir acilmaz kendini
    // kapatirdi — hem de sebebi gorunmeden, "acilmiyor" diye.
    bool yan_hazir = false;
    int yan_sayac = 0;

    while (true) {
        // 🔴 500 ms'DE BIR — 100 ms DENENDI VE PAHALIYA MAL OLDU.
        //
        // Bu okuma I2C uzerinden gidiyor ve o hat ES8311 ile PAYLASILIYOR
        // (pati_pinler.h: tek hat, uc aygit). 100 ms'de bir yoklamak,
        // 02.09.2026'da pilde cokme arasini ~44 saniyeye dusuren
        // gerilemenin bir parcasiydi.
        //
        // Seyreklestirmek tik KACIRMIYOR, cunku artik anlik durum degil
        // BAYRAK okunuyor: M5PM1 basisi biriktiriyor ve okuma onu
        // siliyor.
        if (++yan_sayac >= 25) {
            yan_sayac = 0;
            const bool tik = yan_dugme_tiklandi();

            // Ilk okuma bayragi TEMIZLEMEK icin. Cihaz bu dugmeye
            // basilarak aciliyor ve o basis bayrakta duruyor; saymazsak
            // Pati acilir acilmaz kendini kapatirdi.
            if (!yan_hazir) {
                yan_hazir = true;
            } else if (tik) {
                ESP_LOGW(ETIKET, "YAN GUC DUGMESI — kapatiliyor");
                guc_kapat();   // geri donmuyor
            }
        }

        for (Durum& t : tuslar) {
            const bool su_an = (gpio_get_level(t.pin) == 0);

            if (!su_an) {
                // ---- birakildi ------------------------------------------
                //
                // KISA BASIS TAM BURADA doguyor: onaylanmis bir basis
                // vardi ve uzun esigi dolmadan birakildi.
                //
                // Neden basarken degil de birakirken: basma aninda bunun
                // kisa mi uzun mu olacagi HENUZ BILINMIYOR. Basarken
                // tetiklenseydi her uzun basis once sayfayi acar, sonra
                // cihazi uykuya sokardi — cocuk parmagini kaldirdiginda
                // ekranda bambaska bir sey gormus olurdu.
                if (t.basiliydi && !t.uzun_bildirildi) {
                    g_sayi.fetch_add(1, std::memory_order_relaxed);
                    // Hangi tusun basildigi YAZILIYOR: pati_tus.hpp'de
                    // anlatildigi gibi ekranin sagindaki mavi dugmenin
                    // G11 mi G12 mi oldugu olculmedi. Bu satir sorunun
                    // cevabi.
                    ESP_LOGI(ETIKET, "TUS %d (GPIO %d) kisa basildi",
                             t.hangi, static_cast<int>(t.pin));
                    if (g_islev != nullptr) g_islev(t.hangi, false);
                }
                t.basili_sayac = 0;
                t.basiliydi = false;
                t.uzun_bildirildi = false;
                continue;
            }

            if (t.basili_sayac < ONAY) ++t.basili_sayac;
            if (t.basili_sayac < ONAY) continue;

            const std::int64_t simdi = esp_timer_get_time();

            // ---- basis daha yeni onaylandi ------------------------------
            if (!t.basiliydi) {
                if (simdi - t.son_us < EN_KISA_ARA_US) continue;
                t.basiliydi = true;
                t.bas_us = simdi;
                t.son_us = simdi;
                continue;   // kisa/uzun karari HENUZ verilmedi
            }

            // ---- basili tutuluyor: uzun mu oldu -------------------------
            if (!t.uzun_bildirildi && simdi - t.bas_us >= UZUN_US) {
                t.uzun_bildirildi = true;
                g_sayi.fetch_add(1, std::memory_order_relaxed);
                ESP_LOGW(ETIKET, "TUS %d (GPIO %d) UZUN basildi", t.hangi,
                         static_cast<int>(t.pin));
                if (g_islev != nullptr) g_islev(t.hangi, true);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(YOKLAMA_MS));
    }
}

}  // namespace

esp_err_t tus_baslat(TusIslevi tiklandi)
{
    g_islev = tiklandi;

    gpio_config_t ayar = {};
    ayar.pin_bit_mask = (1ULL << PATI_TUS_1) | (1ULL << PATI_TUS_2);
    ayar.mode = GPIO_MODE_INPUT;
    ayar.pull_up_en = GPIO_PULLUP_ENABLE;
    ayar.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ayar.intr_type = GPIO_INTR_DISABLE;

    const esp_err_t hata = gpio_config(&ayar);
    if (hata != ESP_OK) {
        ESP_LOGE(ETIKET, "tus pinleri kurulamadi: %s", esp_err_to_name(hata));
        return hata;
    }

    // 🔴 YIGIN 4096 — 2048 DENENDI VE COKTU.
    //
    // 02.09.2026, gercek kartta, uzun basista:
    //
    //     W tus: TUS 1 (GPIO 11) UZUN basildi
    //     W pati.guc: derin uykuya giriliyor
    //     ***ERROR*** A stack overflow in task pati_tus has been detected.
    //     Rebooting...
    //
    // Ilk hesap "iki GPIO okumasi ve bir bayrak" diyordu ve KISA basis
    // icin dogruydu. Kacirilan sey UZUN basisin ne yaptigi:
    // `esp_deep_sleep_start()` bu gorevin YIGININDA kosuyor ve RTC
    // hazirligi, uyku kaynaklarinin kurulmasi, onbellek kapatilmasi
    // derin bir cagri zinciri.
    //
    // Disaridan gorunusu tam bir "kapanma" taklidiydi: ekran sonuyor,
    // cihaz yeniden basliyor. Yani derin uyku HIC calismadi, cokme
    // calisti — ve ikisi cocugun gozunde ayni gorunuyor. Seri log
    // olmadan ayirt edilemezdi.
    //
    // ⚠️ Bu goreve is eklerken yigin yeniden dusunulmeli. "Sadece bir
    // bayrak birakiyor" muhakemesi, bayragin arkasindaki isi saymadigi
    // icin yanlisti.
    //
    // Oncelik 4: goz gorevinden (3) yukarida. Tus yanitinin gecikmesi
    // dogrudan hissediliyor — basip da bir sey olmamasi bozuk gibi
    // gorunur — ama isi 20 ms'de bir iki okuma oldugu icin kimseyi
    // aclige dusurmuyor.
    if (xTaskCreate(tus_gorevi, "pati_tus", 4096, nullptr, 4, nullptr)
        != pdPASS) {
        ESP_LOGE(ETIKET, "tus gorevi baslatilamadi");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(ETIKET, "tuslar hazir: GPIO %d ve %d",
             static_cast<int>(PATI_TUS_1), static_cast<int>(PATI_TUS_2));
    return ESP_OK;
}

unsigned tus_sayisi() { return g_sayi.load(std::memory_order_relaxed); }

}  // namespace pati
