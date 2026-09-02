// Pati — Asama 2 firmware'i, ilk adim
//
// BU DOSYA HENUZ SES TASIMIYOR. Su an tek isi:
//   1. Kartin gercekten ayaga kalktigini gostermek (PSRAM, flash, cekirdek)
//   2. stackchan'in Gemini Live istemcisinin BIZIM projede derlenip
//      baglandigini kanitlamak
//   3. Kablolamayi seri porta yazmak — lehimlerken ekrana bakip
//      dogrulayabilmek icin
//
// NEDEN BOYLE PARCA PARCA: donanim henuz kargoda. Derlenen kod ile
// CALISAN kod ayri seyler; burada "derlendi ve baglandi" diyebilecegimiz
// kadarini yapiyoruz. Ses hatti, kart elimize gectiginde adim adim
// eklenecek ve her adim olculecek.

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdlib>
#include <memory>

#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_psram.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <conversation/gemini_live_client.hpp>

#include "pati_ekran.hpp"
#include "pati_gozler.hpp"
#include "pati_guc.hpp"
#include "pati_hafiza.hpp"
#include "pati_olcum.hpp"
#include "pati_pinler.h"
#include "pati_ses.hpp"
#include "pati_sohbet.hpp"
#include "pati_tus.hpp"
#include "pati_ag.hpp"
#include "pati_anahtar.hpp"
#include "pati_ayar.hpp"
#include "pati_guncelleme.hpp"
#include "pati_kullanim.hpp"
#include "pati_panel.hpp"

namespace {

constexpr const char* ETIKET = "pati";

// Kac turda bir tam rapor basilsin.
//
// Kosu bitmesini beklemiyoruz: 20 dakikalik bir kosuda gecikmenin
// zamanla artip artmadigini gormek istiyoruz (PC'de baglam buyudukce
// +283 ms olculmustu).
constexpr std::uint32_t RAPOR_ARALIGI = 5;

// Olumcul hata: sebebi yukarida yazili, program burada duruyor.
//
// NEDEN CIKMIYORUZ: app_main donerse ESP32 yeniden baslar ve seri
// porttaki hata mesaji akip gider. Burada bekleyip hatanin okunmasini
// sagliyoruz.
void bekle_ve_dur()
{
    ESP_LOGE(ETIKET, "");
    ESP_LOGE(ETIKET, "Program durdu. Yukaridaki sebebi duzeltip");
    ESP_LOGE(ETIKET, "karti yeniden baslat (RESET dugmesi).");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// -------------------------------------------------------------------------
// GUC GOZCUSU — kaynagi izler, kipi uygular, dusuk pili haber verir
// -------------------------------------------------------------------------
//
// 🔴 NEDEN AYRI GOREV. Bu is eskiden ana dongudeydi ve ana dongu ancak
// SU UCU BITINCE basliyor: wifi baglanmasi, anahtarin girilmis olmasi,
// ilk Gemini oturumunun acilmasi. Yani acilistan itibaren saniyelerce
// guc kipi HIC uygulanmiyordu.
//
// 02.09.2026'da olculdu — brownout'tan yeni kalkmis bir cihazda:
//
//     19:16:04  cokme=1  goz_fps=20   <- yeniden basladi, gozler tam hizda
//     19:16:10  cokme=1  goz_fps=10   <- pil kipi ANCAK simdi girdi
//
// Alti saniye boyunca Pati en kirilgan aninda — pil zaten dusuk, telsiz
// baglaniyor (en yuksek akim), TLS el sikismasi CPU yiyor — ustune
// gozleri de tam hizda cizti. Wifi yavas baglansa bu pencere yirmi
// saniye olurdu.
//
// Ayri gorev bunu ORTADAN KALDIRIYOR: gozler acilir acilmaz basliyor ve
// hicbir seyi beklemiyor.
//
// Yigin 3072: I2C okumasi, birkac karsilastirma ve ESP_LOG. Olculen
// tepe kullanim buna rahat siginiyor.

// Yoklama araligi. 2 saniye, cunku pil yuzdesi 30 saniyelik pencerenin
// TEPESINDEN hesaplaniyor (pati_guc.cpp) ve 15 ornek istiyor.
constexpr int GOZCU_ARALIK_MS = 2000;

// Dusuk pil uyarisi ne siklikta gorunsun.
//
// Kullanicinin istegi: "pil %20'nin altina dusunce dakikada bir sarja
// tak diye bir sey gosterebiliriz". Dakikada bir, uc saniye — sohbeti
// bolmeyecek kadar seyrek, unutturmayacak kadar sik.
constexpr std::int64_t UYARI_ARALIK_US = 60LL * 1000000LL;

// Guc kipini uygular. Kaynak degisince ve acilista bir kez cagriliyor.
void guc_kipi_uygula(pati::GucKaynagi kaynak)
{
    const bool pilde = (kaynak != pati::GucKaynagi::Usb);

    // ---- ARKA ISIK: pilde kisik ----------------------------------------
    //
    // Sesi kismak yerine BURADAN tasarruf. Brownout'u hoparlorun anlik
    // tepe akimi yapiyor, ama o tepe taban akimin uzerine biniyor:
    // taban dusunce ray daha yuksekte durur ve tepe icin pay kalir.
    //
    // ⚠️ OLCULMUS DEGERLER DEGIL. 0.45 tek basina denendi ve cokmeyi
    // durdurmadi (bkz. PIL.md); yine de taban akimi dusurdugu icin
    // duruyordu. 02.09.2026'da kullanicinin istegiyle 0.35'e indi:
    // "sorun barizse guc, biraz daha kisalim."
    //
    // 0.15 alt sinir ve orasi "kisik ekran" degil "kapali ekran" gibi
    // gorunuyor (pati_ekran.hpp). 0.35 hala rahat okunuyor.
    pati::ekran_parlaklik_ayarla(pilde ? 0.35f : 1.00f);

    // ---- WIFI VERICI GUCU: DENENDI VE GERI ALINDI ------------------------
    //
    // 02.09.2026'da pilde 20 dBm'den 15 dBm'e indirildi. Gerekcesi
    // saglamdi: telsiz gonderirken 250-350 mA cekiyor ve Pati surekli
    // gonderiyor.
    //
    // ❌ BEDELI HEMEN GORULDU: kullanici "modemin dibinde bile zor
    // cekiyor" dedi ve panel 3/4'ten 1/4'e dustu. 3 dB'lik dusus bu
    // kartin anteni icin fazlaymis.
    //
    // Zayif wifi ayrica hedefin TERSINE calisiyor: baglanti kotulesince
    // telsiz daha cok yeniden gonderiyor, yani ortalama akim ARTABILIR.
    //
    // Tekrar denenecekse 18 dBm gibi daha kucuk bir adim mantikli, ve
    // once wifi kapsamasi duzeltilmeli — Pati'nin sinyali zaten dusuk
    // ve bu ayri bir sorun (TESHIS.md'de 10 saniyelik TCP takilmalari).

    // ---- GOZLER: pilde yavas, konusurken daha yavas ---------------------
    //
    // Kullanicinin oncelik sirasi: once cokmeme, sonra SES, sonra
    // gorunum. Ses tavanina DOKUNULMUYOR (0.70'te sabit); pilde feda
    // edilebilir tek yer gozler.
    //
    // 02.09.2026 olcumu: 20 -> 10 fps, cokme arasini ~40 saniyeden
    // ~4,5 dakikaya cikardi. Gerekce ve sayilar pati_gozler.cpp'de.
    pati::gozler_pil_kipi(pilde);

    ESP_LOGW(ETIKET, "GUC KAYNAGI: %s · pil %d mV (%%%d) · VIN %d mV · "
                     "ses tavani %.2f",
             kaynak == pati::GucKaynagi::Usb   ? "USB/5VIN"
             : kaynak == pati::GucKaynagi::Pil ? "PIL"
                                               : "BILINMIYOR",
             pati::pil_mv(), pati::pil_yuzde(), pati::vin_mv(),
             static_cast<double>(
                 pilde ? std::min(pati::ses_seviyesi(), pati::SES_PIL_TAVANI)
                       : pati::ses_seviyesi()));
}

// Tusa basildi.
//
// TUS GOREVINDEN cagriliyor, yani BLOKLAMAMALI. Yaptigi tek sey bir
// atomik bayrak birakmak; sayfayi goz gorevi ciziyor.
//
// ⚠️ IKI TUS DA AYNI ISI YAPIYOR. Ekranin sagindaki mavi dugmenin G11
// mi G12 mi oldugu olculmedi (pati_tus.hpp). Ikisini de baglamak
// dogru davranisi bugun veriyor; hangisi oldugu seri porttan
// ogrenilince burasi ayrilabilir.
void tusa_basildi(int, bool uzun)
{
    if (uzun) {
        // Uzun basis = "kapat". Geri donmuyor.
        //
        // ⚠️ GERCEK KAPANMA DEGIL, derin uyku — gerekcesi ve bedeli
        // pati_guc.hpp'de. Ozeti: ESP32 kapaliyken hicbir yazilim
        // calismadigi icin cihaz kendini ACAMAZ, o yuzden gercek
        // kapanma yalnizca yan dugmeden (cift tik) yapilabiliyor.
        // Derin uyku ayni tusla geri gelebilmeyi mumkun kiliyor.
        pati::guc_derin_uyku();
    }
    // Guncelleme perdesi aciksa mavi tus BASKA BIR IS yapiyor.
    //
    // Ayni tus, ekranda ne yaziyorsa onu yapmali: ekranda "MAVI TUSA
    // BAS" yaziyorken bilgi sayfasi acmak, cocuga tusun bozuk oldugunu
    // dusundururdu.
    if (pati::gozler_guncelleme_acik()) {
        // Dusuk pilde baslatmiyoruz — ekranda zaten "once sarja tak"
        // yaziyor ve basmanin bir sey yapmamasi o yazinin karsiligi.
        // Gerekce pati_gozler.cpp'de GUNC_EN_AZ_PIL.
        const int y = pati::pil_yuzde();
        const bool pil_zayif = pati::guc_kaynak() != pati::GucKaynagi::Usb
                               && y >= 0 && y < 40;
        if (!pil_zayif) {
            ESP_LOGW(ETIKET, "guncelleme cocugun istegiyle basliyor");
            pati::guncelleme_indir();
        } else {
            ESP_LOGW(ETIKET, "guncelleme istendi ama pil %%%d — reddedildi", y);
        }
        return;
    }

    pati::gozler_bilgi_degistir();
}

void guc_gozcusu(void*)
{
    pati::GucKaynagi onceki = pati::GucKaynagi::Bilinmiyor;
    bool ilk = true;
    std::int64_t son_uyari_us = 0;

    while (true) {
        pati::pil_ornekle();

        const pati::GucKaynagi kaynak = pati::guc_kaynak();

        // ---- GOZ GOREVINE DURUM BILDIR --------------------------------
        //
        // 🔴 GOZ GOREVI ARTIK HICBIR SEY SORMUYOR. Perde kararlari icin
        // guncelleme, ag ve guc durumuna bakiyordu; ikisi KILIT aliyor
        // (ayni kilidi panel de aliyor), biri I2C yapiyor.
        //
        // 02.09.2026'da iki kez goruldu: gozler dondu ve donuk kaldi,
        // sonunda cihaz coktu. Cizim gorevi bekleyebilecegi hicbir sey
        // icermemeli.
        //
        // Burasi zaten 2 saniyede bir donuyor ve bu sorulari sormanin
        // dogru yeri: bu gorev bekleyebilir, goz gorevi bekleyemez.
        {
            const pati::AgDurumu ag = pati::ag_durumu();
            pati::gozler_durum_bildir(
                static_cast<int>(pati::guncelleme_durumu()),
                pati::guncelleme_yuzde(),
                pati::guncelleme_yeni_surum(),
                ag == pati::AgDurumu::Bagli,
                ag == pati::AgDurumu::Kurulum,
                pati::pil_yuzde(),
                kaynak == pati::GucKaynagi::Usb);
        }
        if (ilk || kaynak != onceki) {
            ilk = false;
            onceki = kaynak;
            guc_kipi_uygula(kaynak);
        }

        // ---- DUSUK PIL UYARISI ------------------------------------------
        //
        // NEDEN GEREKLI: cocuk Pati'yi her zaman dolu pille kullanmayacak
        // ve "sarja tak" diye bir sey soylenmezse pilin bittigini ancak
        // Pati sustugunda anlayacak. Uyari, sohbetin ortasinda sessizce
        // olmekten iyi.
        //
        // pil_dusuk() histerezisli ve USB'de her zaman false — yani
        // sarj olurken uyari cikmiyor.
        if (pati::pil_dusuk()) {
            const std::int64_t simdi = esp_timer_get_time();
            if (son_uyari_us == 0 || simdi - son_uyari_us >= UYARI_ARALIK_US) {
                son_uyari_us = simdi;
                pati::gozler_pil_uyarisi(pati::pil_yuzde());
                ESP_LOGW(ETIKET, "DUSUK PIL: %%%d (%d mV) — uyari gosterildi",
                         pati::pil_yuzde(), pati::pil_mv());
            }
        } else {
            // Sarja takildi ya da esigin uzerine cikti: bir dahaki
            // dususte uyari HEMEN ciksin, dakikayi bekletme.
            son_uyari_us = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(GOZCU_ARALIK_MS));
    }
}

// -------------------------------------------------------------------------
// Kartin gercekte ne oldugunu yaz
//
// NEDEN: StickS3'un N8R8 oldugunu biliyoruz ama KARTIN kendisi ne diyor?
// PSRAM gercekten 8 MB mi, gercekten oktal mi? Tahmin etmek yerine
// cipe soruyoruz. Yanlis varyant gelmisse ilk saniyede anlasilir.
// -------------------------------------------------------------------------
void karti_yaz()
{
    esp_chip_info_t bilgi{};
    esp_chip_info(&bilgi);

    ESP_LOGI(ETIKET, "cekirdek sayisi : %d", bilgi.cores);
    ESP_LOGI(ETIKET, "silikon surumu  : %d", bilgi.revision);

    if (esp_psram_is_initialized()) {
        const size_t psram = esp_psram_get_size();
        ESP_LOGI(ETIKET, "PSRAM           : %u bayt (%.1f MB)",
                 static_cast<unsigned>(psram),
                 static_cast<double>(psram) / (1024.0 * 1024.0));
    } else {
        // Bu bir HATA, uyari degil. PSRAM olmadan TLS tamponlari ve ses
        // kuyruklari dahili SRAM'e sigmiyor.
        ESP_LOGE(ETIKET, "PSRAM YOK! sdkconfig'de CONFIG_SPIRAM acik mi?");
    }

    ESP_LOGI(ETIKET, "bos dahili SRAM : %u bayt",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
}

// -------------------------------------------------------------------------
// Gemini Live istemcisi gercekten baglandi mi?
//
// Bu bir CALISMA testi degil, BAGLANMA (link) testi. Nesneyi kuruyoruz
// ve durumunu soruyoruz; ag baglantisi kurulmuyor. Amac: 49 KB'lik
// stackchan bileseni bizim derlememizde yerini aldi mi?
//
// Baglanti kurulmasi ve olcum, ses hatti eklendiginde gelecek.
// -------------------------------------------------------------------------
void istemciyi_dene()
{
    using stackchan::conversation::ConversationState;
    using stackchan::conversation::GeminiLiveClient;

    auto istemci = std::make_unique<GeminiLiveClient>("");
    const auto durum = istemci->state();

    ESP_LOGI(ETIKET, "Gemini Live istemcisi kuruldu, durum=%d (Idle=%d)",
             static_cast<int>(durum),
             static_cast<int>(ConversationState::Idle));
    ESP_LOGI(ETIKET, "  -> stackchan conversation bileseni baglandi");
    ESP_LOGW(ETIKET, "  -> ama HENUZ BAGLANMIYOR: wifi ve olcum yazilmadi");
}

// -------------------------------------------------------------------------
// Ses hatti sinamasi — DONANIM GELMEDEN calisan kadari
//
// I2S kanallarini gercekten aciyoruz. Mikrofon takili degilse okuma ya
// zaman asimina duser ya sifir doner — ikisi de beklenen, cokme olmamali.
// Amac: kanal kurulumunun (saat, yuva bicimi, pin eslemesi) kabul
// edildigini gormek. Sesin GERCEKTEN geldigi ancak mikrofon takilinca
// anlasilir.
// -------------------------------------------------------------------------
void ses_hattini_dene()
{
    if (pati::mikrofon_baslat() != ESP_OK) {
        ESP_LOGE(ETIKET, "mikrofon kurulamadi — pin catismasi olabilir");
        return;
    }
    if (pati::hoparlor_baslat() != ESP_OK) {
        ESP_LOGE(ETIKET, "hoparlor kurulamadi — pin catismasi olabilir");
        return;
    }

    // Bir parca okumayi dene. Mikrofon yoksa 0 ya da gurultu doner.
    std::array<std::int16_t, pati::PATI_OKUMA_ORNEK> parca{};
    const size_t okunan = pati::mikrofon_oku(parca, 200);

    if (okunan == 0) {
        ESP_LOGW(ETIKET, "mikrofondan veri gelmedi (%u ornek)",
                 static_cast<unsigned>(okunan));
        ESP_LOGW(ETIKET, "  mikrofon takili degilse NORMAL");
        ESP_LOGW(ETIKET, "  takiliysa: L/R pini GND'ye bagli mi?");
    } else {
        // En yuksek genligi yaz. Sessiz odada bile kucuk bir deger
        // olmali; tam sifir gelirse veri hatti bagli degil demektir.
        std::int32_t enb = 0;
        for (size_t i = 0; i < okunan; ++i) {
            enb = std::max<std::int32_t>(enb, std::abs(parca[i]));
        }
        ESP_LOGI(ETIKET, "mikrofon %u ornek verdi, en yuksek genlik %ld",
                 static_cast<unsigned>(okunan), static_cast<long>(enb));
        if (enb == 0) {
            ESP_LOGW(ETIKET, "  genlik TAM SIFIR — SD pini bagli mi?");
        }
    }

    ESP_LOGI(ETIKET, "ses seviyesi baslangic: %.0f%%",
             static_cast<double>(pati::ses_seviyesi()) * 100.0);
}

// -------------------------------------------------------------------------
// Donanim kilidi — yanlis karta inen yazilim kendini geri alir
// -------------------------------------------------------------------------
//
// SORUN. Guncelleme manifestinde (surum.json) donanim alani YOK; icinde
// yalnizca surum numarasi ve indirme adresi var. Onceki Pati (ESP32-S3
// DevKit + INMP441 + MAX98357) ile bu kart AYNI yongayi kullaniyor
// (esp32s3), yani eski Pati bu yazilimi indirir, imza dogrulamasindan
// gecirir ve calistirir.
//
// Ve calistirdiginda kendi kendine GERI DONEMEZ. Onyukleyicinin geri
// alma mekanizmasi yeni yapinin kendini "saglam" isaretlememesine
// bakiyor; oysa guncelleme_onayla() ag ile panel kalkinca cagriliyor ve
// ikisi de ses kodeginden bagimsiz — ikisi de yanlis kartta da calisir.
// Yani yazilim sessizce kendini saglam ilan eder, geri alma HIC devreye
// girmez ve eski Pati kalici olarak susar. Caresi kutuyu acip USB
// takmak olurdu.
//
// COZUM. Kart kimligini yazilimin kendisi soruyor: ES8311 I2C'de cevap
// veriyor mu? Onceki kartta o yongadan hic yok.
//
// NEDEN BURADA, guncelleme_onayla()'DAN ONCE: onay app_main'in cok
// asagisinda, ag ve panel kalktiktan sonra veriliyor. Oraya varmadan
// donmemiz gerekiyor.
//
// 🔴 KABLOYLA YUKLENDIYSE YENIDEN BASLATMIYORUZ. Geri donulecek bir
// yapi yoksa esp_restart() sonsuz bir acilis dongusu olurdu — kart
// surekli yeniden baslar, seri porta bakan biri sebebi goremez ve
// yeniden yukleme icin indirme moduna girmek bile zorlasir. Yalnizca
// OTA ile gelmis ve HENUZ ONAYLANMAMIS bir yapi geri alinabilir.
void donanimi_dogrula()
{
    if (pati::donanim_dogru()) {
        return;
    }

    ESP_LOGE(ETIKET, "");
    ESP_LOGE(ETIKET, "===== YANLIS DONANIM =====");
    ESP_LOGE(ETIKET, "ES8311 ses kodegi I2C'de (0x%02X) cevap vermiyor.",
             PATI_ADR_ES8311);
    ESP_LOGE(ETIKET, "Bu yazilim M5Stack StickS3 icin derlendi.");

    const esp_partition_t* p = esp_ota_get_running_partition();
    esp_ota_img_states_t hal = ESP_OTA_IMG_UNDEFINED;

    if (p != nullptr && esp_ota_get_state_partition(p, &hal) == ESP_OK &&
        hal == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGE(ETIKET, "Bu yapi OTA ile geldi ve henuz onaylanmadi —");
        ESP_LOGE(ETIKET, "onaylamadan yeniden baslatiyoruz, onyukleyici");
        ESP_LOGE(ETIKET, "onceki surume DONECEK.");
        ESP_LOGE(ETIKET, "==========================");
        // Gunlugun seri porttan cikmasi icin kisa bir an.
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    ESP_LOGE(ETIKET, "Bu yapi KABLOYLA yuklenmis: geri donulecek bir");
    ESP_LOGE(ETIKET, "surum yok, yeniden baslatmiyoruz (acilis dongusu");
    ESP_LOGE(ETIKET, "olurdu). Dogru yazilimi USB'den yukleyin.");
    ESP_LOGE(ETIKET, "==========================");
    ESP_LOGE(ETIKET, "");
}

}  // namespace

extern "C" void app_main()
{
    // NVS ilk isimiz: wifi bilgisi ve Pati'nin hafizasi burada duracak.
    // Bozuksa silip yeniden kuruyoruz — cocuk fisi cekince olusan
    // yarim yazmalar burada birikir.
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(ETIKET, "NVS bozuk ya da eski, silinip yeniden kuruluyor");
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    // ---- GUC VE I2C -----------------------------------------------------
    //
    // BUNDAN SONRAKI HER SEYDEN ONCE. Mikrofon, hoparlor ve ekranin arka
    // isigi M5PM1'in "L3B" katmanindan besleniyor ve o katman acilista
    // KENDILIGINDEN GELMIYOR — acmazsak ucu birden olu kalir ve hicbiri
    // hata vermez. Gerekce pati_pinler.h'nin basinda.
    if (pati::guc_baslat() != ESP_OK) {
        ESP_LOGE(ETIKET, "guc katmani kurulamadi — mikrofon, hoparlor ve "
                         "ekran arka isigi calismayacak");
    }

    // ---- DONANIM KILIDI -------------------------------------------------
    donanimi_dogrula();

    // Gemini anahtari KENDI NVS bolumunde (bkz. partitions.csv). Ayri
    // durmasinin sebebi tam da yukaridaki satir: bozuk NVS silinip
    // yeniden kuruluyor ve anahtar o silmeden etkilenmemeli.
    //
    // Basarisiz olursa DEVAM EDIYORUZ: sebebi neredeyse kesin olarak
    // eski bolum tablosu ve bunu gorebilmenin tek yolu panelin acilmasi.
    pati::anahtar_baslat();
    pati::guncelleme_baslat();

    ESP_LOGI(ETIKET, "");
    ESP_LOGI(ETIKET, "===== PATI %s =====", pati::guncelleme_surumu());
    ESP_LOGI(ETIKET, "Amac: gercek kartta 'cocuk sustu -> ilk ses' olcumu");
    ESP_LOGI(ETIKET, "Kriter: <=1500 ms gecer (PLAN.md)");
    ESP_LOGI(ETIKET, "");

    karti_yaz();

    // Olcum matematigi PC ile ayni mi? Bunu HER ACILISTA siniyoruz.
    //
    // Neden acilista: Asama 2'nin butun amaci bir sayi uretmek ve o sayiyi
    // PC'de olculen 1325 ms ile karsilastirmak. Matematik farkliysa
    // karsilastirma anlamsiz olur — ve bu, sayilara bakip "gecti" dedikten
    // SONRA fark edilecek bir hata olurdu.
    pati::olcum_baslat();
    pati::oz_test();

    istemciyi_dene();
    ses_hattini_dene();

    // Wifi. Baglanamazsa DEVAM ETMIYORUZ: Asama 2'nin tek isi gecikme
    // olcmek ve baglanti olmadan olculecek bir sey yok. Sessizce devam
    // etmek, sonra "neden veri gelmiyor" diye aramaya yol aciyor.
    // HAFIZA sohbetten ONCE okunmali: sistem promptu onu iceriyor.
    // Sonra okunursa robot ilk oturumda cocugu tanimiyor gibi davranir.
    if (pati::hafiza_baslat() != ESP_OK) {
        ESP_LOGW(ETIKET, "hafiza okunamadi — Pati cocugu bastan taniyacak");
    }
    pati::hafiza_oturum_sayaci();

    // GOZLER wifi'den ONCE aciliyor.
    //
    // Wifi'ye baglanmak saniyeler suruyor. Ekran o sure boyunca siyah
    // kalirsa "acilmadi" saniiliyor — cocuk icin de fisi takar takmaz
    // bir hayat belirtisi olmasi gerekiyor.
    //
    // Basarisiz olsa bile DEVAM EDIYORUZ: ekransiz Pati konusabilir.
    // Yuzu olmayan robot, sessiz robottan iyidir.
    //
    // ⚠️ ONCEDEN BURADA BIR AYAR VARDI (PATI_EKRAN_VAR) VE KALDIRILDI.
    //
    // Onceki kartta ekran disaridan lehimlenen bir moduldu ve VARLIGI
    // KODDAN ANLASILAMIYORDU: SPI yolu tek yonlu kuruluyor, panel hic
    // bagli olmasa bile esp_lcd hatasiz aciliyor ve gozler_baslat()
    // ESP_OK donuyordu. Olu bir modulle saatlerce tam olarak boyle
    // gorundu; o yuzden "ekran takili mi" diye SORULMASI gerekiyordu.
    //
    // StickS3'te ekran govdenin icinde ve sokulemiyor. Soru anlamsiz
    // hale geldi, ayar da kaldirildi — cevabi degismeyecek bir soruyu
    // ayar olarak birakmak, bir gun yanlis cevaplanmasini beklemektir.
    //
    // Bedeli hala gercek (23.08.2026, onceki kartta olculdu: kare
    // ortancasi 114 ms, 50 ms'lik butce) ama artik bir secim degil:
    // ekran orada ve cocuk yuze bakiyor.
    if (pati::gozler_baslat() != ESP_OK) {
        ESP_LOGW(ETIKET, "ekran acilmadi — Pati yuzsuz devam ediyor");
    } else {
        pati::gozler_bos();
    }

    // GUC GOZCUSU — TAM BURADA baslamali.
    //
    // Gozler acildi (yani kismasi anlamli oldu) ve asagidaki ag beklemesi
    // HENUZ BASLAMADI. Bir satir asagi konsa, wifi baglanana kadar gecen
    // saniyelerde pil kipi yine uygulanmamis olurdu — duzeltmeye
    // calistigimiz kusurun ta kendisi (gerekce gorevin basinda).
    if (xTaskCreate(guc_gozcusu, "pati_guc", 3072, nullptr, 3, nullptr)
        != pdPASS) {
        // Olumcul degil: guc kipi uygulanmaz, Pati calisir. Ama pilde
        // daha sik coker, o yuzden sessiz gecilmiyor.
        ESP_LOGE(ETIKET, "guc gozcusu baslatilamadi — pil kipi ve dusuk "
                         "pil uyarisi CALISMAYACAK");
    }

    // TUSLAR. Gozlerden sonra, agdan once: bilgi sayfasi wifi
    // baglanmasini beklemek zorunda degil — baglanti YOKKEN de acilip
    // "WiFi yok" gostermesi dogru davranis, cunku cocugun sordugu soru
    // tam da bu olabilir.
    if (pati::tus_baslat(tusa_basildi) != ESP_OK) {
        // Olumcul degil: tussuz Pati konusabiliyor, sadece bilgi
        // sayfasi acilamaz.
        ESP_LOGW(ETIKET, "tuslar kurulamadi — bilgi sayfasi acilamayacak");
    }

    // AG — artik gomulu wifi yok (§4.3). Kayitli ag varsa baglaniyor,
    // yoksa kendi agini yayinlayip captive portal aciyor.
    //
    // BLOKLAMIYOR: baglanma denemeleri saniyeler suruyor ve o sure
    // boyunca gozler olu kalmamali.
    pati::ayar_baslat();

    // Calma hizi ancak AYARLAR OKUNDUKTAN sonra kurulabiliyor:
    // hoparlor_baslat() bundan once kosuyor ve NVS'teki degeri henuz
    // bilmiyor. Pati'nin afacan sesi buna bagli — bkz. pati_ses.hpp.
    pati::hoparlor_hiz_ayarla(pati::ayar_hiz());

    pati::kullanim_baslat();
    if (pati::ag_baslat() != ESP_OK) {
        ESP_LOGE(ETIKET, "ag katmani baslatilamadi. Sebep yukarida.");
        bekle_ve_dur();
        return;
    }

    // PANEL her iki modda da aciliyor: kurulum modunda ebeveyn wifi
    // bilgisini girmek icin, bagliyken ayarlar icin.
    if (pati::panel_baslat() != ESP_OK) {
        ESP_LOGW(ETIKET, "panel acilmadi — ayarlar telefondan yapilamaz");
    }

    // ---- GERI ALMAYI IPTAL ET -------------------------------------------
    //
    // Yeni bir yapi OTA ile geldiyse "denemede" (PENDING_VERIFY) aciliyor
    // ve burasi cagrilmadan yeniden baslarsa onyukleyici ESKI yapiya
    // donuyor (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
    //
    // NEDEN TAM BURADA:
    //
    //   ONCESI OLMAZ. Buraya kadar gelmek NVS, ekran, ses hatti, ag
    //   katmani ve HTTP sunucusunun tamaminin ayaga kalktigini
    //   kanitliyor. Daha erken onaylamak, bu yollardan birini kiran bir
    //   yapiyi saglam ilan etmek olurdu.
    //
    //   SONRASI DA OLMAZ. Bir sonraki adim wifi bekleme dongusu ve orada
    //   SURESIZ kalinabiliyor: robot kurulum modundaysa anne wifi
    //   bilgisini girene kadar bekliyor. Onayi oraya koysaydik, kurulum
    //   sirasinda fisi cekilen robot bir onceki surume donerdi — ve
    //   fisin cekilmesi bu robotun BEKLENEN kapanma bicimi.
    //
    // Sohbetin baslamasi onaya dahil DEGIL: anahtar yanlissa sohbet
    // acilmiyor ama bu firmware'in degil, girilen anahtarin sorunu.
    // Geri almak onu duzeltmezdi.
    pati::guncelleme_onayla();

    // ---- DURUM OZETI ----------------------------------------------------
    //
    // 🔴 ACILISIN ILK SANIYESI BU KARTTA GORULEMIYOR.
    //
    // Konsol USB uzerinden geliyor (UART koprusu yok) ve her sifirlamada
    // USB yeniden numaralaniyor — bilgisayarin portu birkac saniye
    // kayboluyor. 23.08.2026'da kart ilk kez takildiginda tam olarak bu
    // yasandi: guc ve ses satirlari her denemede kacti, cihaz "sessiz"
    // sanildi ve teshis dakikalar aldi.
    //
    // Bu yuzden ozet BURADA, ag ve panel kalktiktan sonra bir kez daha
    // basiliyor. Buraya gelindiginde monitor coktan baglanmis oluyor.
    ESP_LOGW(ETIKET, "");
    ESP_LOGW(ETIKET, "===== DURUM =====");
    ESP_LOGW(ETIKET, "  guc / L3B : %s",
             pati::guc_hazir() ? "acik" : "KAPALI — mik/hoparlor/arka isik olu");
    ESP_LOGW(ETIKET, "  ES8311    : %s",
             pati::donanim_dogru() ? "cevap veriyor" : "YOK");
    ESP_LOGW(ETIKET, "  ses hatti : %s",
             pati::ses_hazir() ? "hazir" : "KURULAMADI");
    ESP_LOGW(ETIKET, "  ekran     : %s",
             pati::ekran_hazir() ? "hazir" : "KURULAMADI");

    // Guc kaynagi — ses tavaninin dayanagi (SES_PIL_TAVANI).
    {
        const pati::GucKaynagi kaynak = pati::guc_kaynak();
        const int mv = pati::pil_mv();
        ESP_LOGW(ETIKET, "  guc kayn. : %s%s · pil %d mV · VIN %d mV",
                 kaynak == pati::GucKaynagi::Usb   ? "USB/5VIN"
                 : kaynak == pati::GucKaynagi::Pil ? "PIL"
                                                   : "BILINMIYOR",
                 kaynak == pati::GucKaynagi::Usb ? "" : " — ses sinirli",
                 mv, pati::vin_mv());
        ESP_LOGW(ETIKET, "  sicaklik  : %.1f C",
                 static_cast<double>(pati::yonga_sicakligi()));
    }

    // ---- NEDEN ACILDIK --------------------------------------------------
    //
    // 🔴 SES SEVIYESININ TEK DURUST OLCUTU BU SATIR.
    //
    // 01.09.2026, gercek kartta olculdu: ses seviyesi 1.00'DE ve kart
    // USB'ye TAKILIYKEN, bir oturumda dort kez
    //
    //     E BOD: Brownout detector was triggered
    //     rst:0x3 (RTC_SW_SYS_RST)
    //
    // Hoparlor akim cekince (AW8737 + 8 ohm) ray cokuyor ve yonga
    // kendini sifirliyor. Brownout esigi zaten en musamahakar kademede
    // (CONFIG_ESP_BROWNOUT_DET_LVL 7), yani yazilimdan gevsetilecek yer
    // yok — gerilim gercekten dusuyor.
    //
    // DISARIDAN NASIL GORUNUYOR: Pati cumlenin ortasinda susuyor, ~5
    // saniye sonra geri geliyor ve konusulani hatirlamiyor. Tipki
    // baglantinin kopmasi gibi gorunuyor ve sebep agda aranir. Ikisini
    // ayiran tek sey bu satir.
    //
    // KULLANIMI: ses seviyesini panelden artirdiktan sonra buraya bak.
    // "brownout" cikiyorsa seviye o donanim icin YUKSEK, bir kademe geri
    // al. M5Stack'in kendi belgesi de pilde %75'in altini soyluyor
    // (pati_ses.hpp'deki gerekce).
    const esp_reset_reason_t sebep = esp_reset_reason();
    ESP_LOGW(ETIKET, "  acilis    : %s",
             sebep == ESP_RST_BROWNOUT
                 ? "BROWNOUT — ses seviyesi bu donanim icin yuksek"
             : sebep == ESP_RST_POWERON    ? "guc verildi"
             : sebep == ESP_RST_SW         ? "yazilim (guncelleme/yeniden baslat)"
             : sebep == ESP_RST_PANIC      ? "COKME (panic) — yigit izine bak"
             : sebep == ESP_RST_TASK_WDT   ? "GOREV BEKCISI — bir gorev takildi"
             : sebep == ESP_RST_INT_WDT    ? "KESME BEKCISI"
             : sebep == ESP_RST_DEEPSLEEP  ? "derin uykudan"
             // Kabloyla yukleme sonrasi: esptool RTS ile sifirliyor.
             // Ayirt edilmezse "diger" cikiyor ve satir ise yaramiyor.
             : sebep == ESP_RST_EXT        ? "dis sifirlama (yukleme/RESET)"
                                           : "diger");
    ESP_LOGW(ETIKET, "=================");
    ESP_LOGW(ETIKET, "");

    // Baglanti kurulana kadar bekle. Kurulum modundaysa BEKLEMIYORUZ:
    // ebeveyn bilgiyi girene kadar burada durmak, gozleri ve paneli
    // olu birakmak olurdu — oysa panel tam o an gerekli.
    // Kurulum modunda goz sayaclarini da basiyoruz.
    //
    // NEDEN BURADA: periyodik rapor (asagida) ancak robot AGA BAGLANINCA
    // calisiyor. 31.07.2026'da kart ilk kez acildiginda bekci kopegi
    // kurulum modunda havladi ve elimizde tek bir kare suresi yoktu —
    // sadece backtrace vardi. Cizim butcesi tam da burada, hicbir sey
    // bagli degilken olculebilir olmali.
    int kurulum_tik = 0;
    while (pati::ag_durumu() != pati::AgDurumu::Bagli) {
        if (pati::ag_durumu() == pati::AgDurumu::Kurulum) {
            pati::gozler_durum("uykulu");   // "beni ayarla" hali
            if (++kurulum_tik % 5 == 0) {
                ESP_LOGI(ETIKET,
                         "gozler: kare %u = cizim %u (sekil %u + renk %u) "
                         "+ gonderim %u (butce %u) | alan %u px, "
                         "harman %u px | atlanan %u / %u",
                         static_cast<unsigned>(pati::gozler_kare_us()),
                         static_cast<unsigned>(pati::gozler_ciz_us()),
                         static_cast<unsigned>(pati::gozler_sekil_us()),
                         static_cast<unsigned>(pati::gozler_renk_us()),
                         static_cast<unsigned>(pati::gozler_gonder_us()),
                         static_cast<unsigned>(1000000 / pati::gozler_hedef_fps()),
                         static_cast<unsigned>(pati::gozler_alan()),
                         static_cast<unsigned>(pati::gozler_piksel()),
                         static_cast<unsigned>(pati::gozler_atlanan_kare()),
                         static_cast<unsigned>(pati::gozler_kare()));
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    pati::panel_kurulum_bitti();
    pati::kullanim_saat_ayarla();
    ESP_LOGI(ETIKET, "ag hazir (%s)", pati::ag_ip());

    // ---- ACILISTA GUNCELLEME YOKLAMASI ----------------------------------
    //
    // NEDEN BURADA: ag yeni hazir oldu ve sohbet henuz baslamadi, yani
    // manifest cekmenin kimseyi bekletmedigi tek an burasi.
    //
    // Eskiden yoklama YALNIZCA panelden tetikleniyordu: ebeveyn panele
    // girip "guncellemeleri kontrol et" demedikce Pati eskimis surumde
    // kaliyordu. Cocuk paneli acmiyor, ebeveyn her gun acmiyor.
    //
    // Sonuc ekrana dusuyor: yeni surum varsa gozlerin yerine bir sayfa
    // gelip mavi tusu isaret ediyor (pati_gozler.cpp, guncelleme
    // perdesi). BLOKLAMIYOR — kendi gorevinde kosuyor.
    pati::guncelleme_kontrol_et();

    // 🔴 BURADA ANAHTAR SINAMASI VARDI, KALDIRILDI. Geri eklemeyin.
    //
    // 31.07.2026, gercek kartta olculdu. Acilista `anahtar_dogrula()`
    // cagriliyordu ve HER SEFERINDE basarisiz donuyordu:
    //
    //   E esp-tls-mbedtls: mbedtls_ssl_handshake returned -0x0050
    //   W anahtar: dogrulama istegi gonderilemedi: ESP_ERR_HTTP_CONNECT
    //
    // -0x0050 = NET_CONN_RESET. Anahtar saglamdi: yirmi saniye sonra
    // ayni anahtarla Gemini oturumu sorunsuz aciliyordu. Sebep ag da
    // degildi, saat de degil (saat_hazir=true).
    //
    // Sebep BU FONKSIYONUN NEREDE KOSTUGU. app_main CPU 0'da ve gozler
    // gorevi de CPU 0'da; o gorev cizim butcesini asinca IDLE0 hic
    // calisamiyor ve bekci kopegi havliyor (ayni loglarda goruluyor,
    // PLAN.md §"Cihazda ogrenilenler" 2). TLS el sikismasi o acligin
    // ortasinda zaman asimina dusup karsi tarafca kapatiliyor.
    //
    // Panel de bunun uzerine "Google'a ulasilamiyor" yaziyordu — robot
    // gayet konusurken. Tam olarak kacinmaya calistigimiz sey.
    //
    // Sinamaya GEREK DE YOK: acilan bir Gemini oturumu anahtarin
    // calistiginin kanitidir ve `sohbet_baslat` bunu kendisi bildiriyor.
    // Oturum acilamazsa sebebi zaten `anahtar_baglanti_hatasi()`
    // soruyor. Yani bu istek hem yanlis cevap veriyordu hem fazlaydi.

    pati::kullanim_oturum_basladi();

    // ---- SOHBET — anahtar gelene kadar bekliyoruz -----------------------
    //
    // 🔴 ESKIDEN BURADA `bekle_ve_dur()` VARDI. Anahtar Kconfig'den
    // geldigi surece dogruydu: bos anahtar "yanlis derledin" demekti ve
    // durup sebebi seri porta yazmak dogru davranisti.
    //
    // Artik anahtar panelden giriliyor. Kutudan yeni cikmis robotta
    // anahtarin OLMAMASI normal bir hal — durmak, anne anahtari
    // yazdiktan sonra bile robotun sessiz kalmasi demek olurdu. Ustelik
    // durdugu yerden geri donusu yok, fis cekilmesi gerekirdi.
    //
    // Onun yerine bekliyoruz. Panel bu sirada calisiyor, durumu
    // gosteriyor ve anne anahtari yazar yazmaz asagidaki dongu onu
    // goruyor.
    int deneme = 0;
    while (true) {
        if (!pati::anahtar_var()) {
            // "Beni ayarla" hali — wifi kurulumunda kullanilan gozlerin
            // aynisi. Ikisi de ayni seyi soyluyor: panelde bir isin var.
            pati::gozler_durum("uykulu");
            vTaskDelay(pdMS_TO_TICKS(2000));
            deneme = 0;
            continue;
        }

        if (pati::sohbet_baslat() == ESP_OK) break;

        // Anahtar var ama baglanti kurulamadi. Sebebi anahtar katmani
        // biliyor (sohbet_baslat dogrulama istegi attirdi) ve panel
        // yaziyor.
        //
        // BEKLEME SUREYI SEBEBE GORE DEGISIYOR:
        //
        //   Gecersiz/Kota  Google'in kararini yeniden denemek DEGISTIRMEZ.
        //                  Insan mudahalesi bekleniyor (yeni anahtar ya
        //                  da bakiye), o yuzden seyrek deniyoruz. Sik
        //                  denemek 429 yiyen bir anahtari daha da
        //                  batirmak olurdu.
        //   otekiler       Ag dalgalanmasi olabilir; birazdan duzelir.
        const auto d = pati::anahtar_durumu();
        const bool insan_bekleniyor = (d == pati::AnahtarDurumu::Gecersiz
                                       || d == pati::AnahtarDurumu::Kota);
        if (++deneme <= 3 || !insan_bekleniyor) {
            ESP_LOGW(ETIKET, "sohbet baslamadi, 15 sn sonra tekrar");
            vTaskDelay(pdMS_TO_TICKS(15000));
        } else {
            ESP_LOGW(ETIKET, "sohbet baslamadi ve sebep anahtar — "
                             "panelden duzeltilmesi bekleniyor");
            pati::gozler_durum("uykulu");
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }

    ESP_LOGI(ETIKET, "");
    ESP_LOGI(ETIKET, "=========================================");
    ESP_LOGI(ETIKET, " PATI HAZIR — konusabilirsin.");
    ESP_LOGI(ETIKET, "");
    ESP_LOGI(ETIKET, " Olculecek uc sey:");
    ESP_LOGI(ETIKET, "  1. Normal sohbet et — her tur gecikmeyi yaziyor");
    ESP_LOGI(ETIKET, "  2. Pati konusurken UZERINE KONUS (sozunu kesme)");
    ESP_LOGI(ETIKET, "  3. %d dakika devam et (15 dk oturum siniri)", 20);
    ESP_LOGI(ETIKET, "");
    ESP_LOGI(ETIKET, " Rapor her %d turda bir basiliyor.", RAPOR_ARALIGI);
    ESP_LOGI(ETIKET, "=========================================");
    ESP_LOGI(ETIKET, "");

    // Ana gorev bundan sonra sadece izliyor. Is mikrofon ve ses
    // gorevlerinde; buradan tur sayisina bakip periyodik rapor basiyoruz.
    std::uint32_t basilan = 0;
    const int64_t t0 = esp_timer_get_time();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        const std::uint32_t tur = pati::sohbet_tur_sayisi();
        const std::uint32_t dusen = pati::sohbet_dusen_olay();

        if (dusen > 0) {
            // Bu SIFIR olmali. Sifirdan buyukse ses gorevi yetismiyor:
            // hem ses kesilir hem olcum eksilir.
            ESP_LOGW(ETIKET, "DUSEN OLAY: %u — ses gorevi yetismiyor",
                     static_cast<unsigned>(dusen));
        }

        // Mikrofon duyuyor mu — her raporda, kosulsuz.
        //
        // Sessiz odada bile birkac yuz olmali. TAM SIFIR gelirse Gemini'ye
        // sessizlik gidiyor: robot cevap vermez ve sebebi ag ya da anahtar
        // sanilir. 01.09.2026'da gercek kartta tam bu oldu.
        const std::uint32_t mik = pati::sohbet_mik_tepe();
        std::uint32_t mik_okuma = 0, mik_bos = 0;
        pati::sohbet_mik_okuma(mik_okuma, mik_bos);
        if (mik == 0) {
            ESP_LOGW(ETIKET, "MIKROFON SESSIZ: tepe 0 · %u okuma, %u bos "
                             "(%s)",
                     static_cast<unsigned>(mik_okuma),
                     static_cast<unsigned>(mik_bos),
                     (mik_okuma > 0 && mik_bos == mik_okuma)
                         ? "I2S hic veri vermiyor"
                         : (mik_okuma == 0 ? "sohbet dongusu hic okumuyor"
                                           : "veri geliyor ama sessiz"));
        } else if (mik >= 32767) {
            // KIRPMA. Sessiz kalmasi yanlis olurdu: "32768 / 32767"
            // satiri okuyana saglikli bir seviye gibi bile gorunebilir
            // ("sonuna kadar duyuyor"), oysa ADC doymus ve dalga
            // tepesinden kesilmis demek.
            //
            // 01.09.2026'da tam bunun bedeli olculdu: Gemini kirpilmis
            // sesi yanlis dokumluyor ve Turkce konusulurken Portekizce
            // cumle donuyordu. Sebep model sanildi.
            //
            // Ara sira olmasi normaldir (cocuk bagirmistir); HER
            // raporda cikiyorsa mikrofon kazanci yuksek demektir —
            // pati_ses.cpp set_mic_gain, 6 dB'lik basamaklar.
            ESP_LOGW(ETIKET, "MIKROFON KIRPIYOR: tepe %u (tavan 32767) — "
                             "her raporda cikiyorsa kazanc yuksek",
                     static_cast<unsigned>(mik));
        } else {
            ESP_LOGI(ETIKET, "mikrofon tepe genlik: %u / 32767",
                     static_cast<unsigned>(mik));
        }

        const std::uint32_t gonderilemeyen = pati::sohbet_gonderilemeyen();
        if (gonderilemeyen > 0) {
            // Bu da SIFIR olmali. Sifirdan buyukse mikrofon sesi
            // Gemini'ye ULASMIYOR — robot cevap vermiyorsa sebebi bu.
            ESP_LOGW(ETIKET, "GONDERILEMEYEN SES PARCASI: %u",
                     static_cast<unsigned>(gonderilemeyen));
        }

        // 🔴 GUC KAYNAGI BLOGU BURADAN ALINDI — geri koymayin.
        //
        // Eskiden guc kipi (ekran parlakligi + goz kare hizi) bu
        // dongude uygulaniyordu. Sorun: bu dongu ancak wifi baglanip
        // ilk Gemini oturumu acildiktan SONRA basliyor, yani acilistan
        // itibaren saniyelerce guc kipi hic uygulanmiyordu. Olculdu ve
        // gerekcesi `guc_gozcusu` gorevinin basinda yaziyor.
        //
        // Artik o gorev yapiyor ve gozler acilir acilmaz calisiyor.

        // Cokme sayaci — cihazda duruyor, A/B olcumunun zemini.
        {
            static std::uint32_t onceki_cokme = 0;
            static bool cokme_basildi = false;
            const std::uint32_t c = pati::cokme_sayisi();
            if (!cokme_basildi || c != onceki_cokme) {
                onceki_cokme = c;
                cokme_basildi = true;
                ESP_LOGW(ETIKET, "COKME SAYACI (acilisi atlatan): %u",
                         static_cast<unsigned>(c));
            }
        }

        // Baglanti kac kez koptu.
        //
        // Bu sayi SIFIRDAN BUYUK OLABILIR ve tek basina hata degil: wifi
        // dalgalanir, sunucu oturumu kapatir, toparlanma calisir ve cocuk
        // bir-iki saniyelik bir bosluk disinda bir sey fark etmez.
        //
        // Anlami DEGISIMINDE: dakikalar icinde artiyorsa sebep aranmali
        // (zayif sinyal, kota, oturum siniri). Sifir kalmasi da bilgi —
        // "ses gitti" sikayeti geldiginde kopma OLMADIGINI soyluyor ve
        // aramayi ses yoluna cevirir.
        static std::uint32_t onceki_kopma = 0;
        const std::uint32_t kopma = pati::sohbet_kopma_sayisi();
        if (kopma != onceki_kopma) {
            ESP_LOGW(ETIKET, "BAGLANTI KOPMASI: %u (bu aralikta %u)",
                     static_cast<unsigned>(kopma),
                     static_cast<unsigned>(kopma - onceki_kopma));
            onceki_kopma = kopma;
        }

        // Goz sayaclari. Ikisi de SIFIR olmali:
        //   bilinmeyen -> model listede olmayan ifade uyduruyor ya da
        //                 biz yanlis ad gonderiyoruz
        //   atlanan    -> kare butcesi asiliyor, gozler kekeliyor
        const std::uint32_t goz_bilinmeyen = pati::gozler_bilinmeyen_durum();
        const std::uint32_t goz_atlanan = pati::gozler_atlanan_kare();
        if (goz_bilinmeyen > 0) {
            ESP_LOGW(ETIKET, "BILINMEYEN IFADE: %u kez",
                     static_cast<unsigned>(goz_bilinmeyen));
        }
        // 🔴 SAYAC KUMULATIF — FARKA BAKIYORUZ.
        //
        // 01.09.2026'da gercek kartta gorundu: wifi baglanirken 895 kare
        // atlandi (o sirada dogruydu, islemci doluydu), sonra her sey
        // duzeldi ama uyari 5 saniyede bir basilmaya DEVAM ETTI —
        // ustelik "butce asiliyor" diyerek, kare 14 ms iken ve butce
        // 50 ms iken.
        //
        // Boyle bir uyari sorunu gorunur yapmiyor, GERCEK sorunlari
        // gizliyor: surekli yanan bir ikaz isigina bakan kimse kalmaz.
        // Bir daha bakan (insan ya da sonraki oturum) olmayan bir
        // sorunun pesine duser.
        //
        // Simdi yalnizca SON ARALIKTA atlanan kare varsa yaziyor.
        static std::uint32_t onceki_atlanan = 0;
        const std::uint32_t yeni_atlanan = goz_atlanan - onceki_atlanan;
        onceki_atlanan = goz_atlanan;

        if (yeni_atlanan > 0) {
            // Dokumu de yaziyoruz. Eskiden yalnizca "butce asiliyor"
            // diyordu ve bu, sorunu GORUNUR yapip TESHIS EDILEMEZ
            // birakiyordu: sekil mi, renk mi, SPI mi belli olmuyordu.
            ESP_LOGW(ETIKET, "ATLANAN KARE: %u (toplam %u) — cizim butcesi "
                             "asiliyor (son kare %u us / butce %u us)",
                     static_cast<unsigned>(yeni_atlanan),
                     static_cast<unsigned>(goz_atlanan),
                     static_cast<unsigned>(pati::gozler_kare_us()),
                     static_cast<unsigned>(1000000 / pati::gozler_hedef_fps()));
            ESP_LOGW(ETIKET, "  cizim %u us (sekil %u + renk %u) + gonderim %u us"
                             " | alan %u px, harman %u px",
                     static_cast<unsigned>(pati::gozler_ciz_us()),
                     static_cast<unsigned>(pati::gozler_sekil_us()),
                     static_cast<unsigned>(pati::gozler_renk_us()),
                     static_cast<unsigned>(pati::gozler_gonder_us()),
                     static_cast<unsigned>(pati::gozler_alan()),
                     static_cast<unsigned>(pati::gozler_piksel()));
        }

        // Her RAPOR_ARALIGI turda tam rapor. Beklemeye gerek yok; kosu
        // ortasinda da sayilari gormek istiyoruz.
        if (tur >= basilan + RAPOR_ARALIGI) {
            basilan = tur;
            pati::rapor_yaz();
        }

        // Uzun kosuda hayatta oldugunu 60 saniyede bir soyle.
        const int64_t sn = (esp_timer_get_time() - t0) / 1000000;
        if (sn % 60 < 5) {
            ESP_LOGI(ETIKET,
                     "%lld dk %lld sn · %u tur · %s · %u kare (%u us/%u px) "
                     "· bos yigit: %u bayt",
                     sn / 60, sn % 60, static_cast<unsigned>(tur),
                     pati::gozler_su_anki(),
                     static_cast<unsigned>(pati::gozler_kare()),
                     static_cast<unsigned>(pati::gozler_kare_us()),
                     static_cast<unsigned>(pati::gozler_piksel()),
                     static_cast<unsigned>(
                         heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));

            // ---- DAHILI SRAM AYRINTISI ----------------------------------
            //
            // 🔴 TEK BASINA "bos yigit" YETMIYOR. 01.09.2026'da gercek
            // kartta su gorildi:
            //
            //     bos yigit: 28895 bayt
            //     E Dynamic Impl: alloc(4437 bytes) failed
            //
            // 28 KB bos, 4,4 KB istek — ve basarisiz. Yani sorun yer
            // olmamasi degil, PARCALANMA olabilir: bos alan var ama
            // ardisik degil. Iki durum bambaska cozum istiyor (tampon
            // kucultmek / ayirma sirasini degistirmek) ve tek sayiyla
            // ayirt edilemiyor.
            //
            // EN DUSUK: acilistan beri gorulen en dip nokta. Anlik deger
            // rahat gorunse bile burasi sifira yaklasiyorsa sistem
            // kiyida calisiyor demektir.
            //
            // EN BUYUK BLOK: mbedtls'in isteyebilecegi tek parca. TLS el
            // sikismasi bunun altina duserse baglanti kurulamiyor ve
            // disaridan "Pati cevap vermiyor" diye gorunuyor.
            ESP_LOGI(ETIKET,
                     "  dahili SRAM: en dusuk %u · en buyuk blok %u bayt",
                     static_cast<unsigned>(heap_caps_get_minimum_free_size(
                         MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(
                         MALLOC_CAP_INTERNAL)));
        }
    }
}
