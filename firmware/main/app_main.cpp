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
#include <esp_psram.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <conversation/gemini_live_client.hpp>

#include "pati_ekran.hpp"
#include "pati_gozler.hpp"
#include "pati_hafiza.hpp"
#include "pati_olcum.hpp"
#include "pati_pinler.h"
#include "pati_ses.hpp"
#include "pati_sohbet.hpp"
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
// Kartin gercekte ne oldugunu yaz
//
// NEDEN: N16R8 aldigimizi biliyoruz ama KARTIN kendisi ne diyor? PSRAM
// gercekten 8 MB mi, gercekten oktal mi? Bunu tahmin etmek yerine
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
// Kablolamayi seri porta yaz
//
// NEDEN: 13 kablo lehimlenecek ve yanlis yere giden bir tel ya parcayi
// yakiyor ya saatler yakiyor. Kartin kendisi hangi pini beklediğini
// soylerse, lehimlerken ekrana bakip dogrulanabiliyor. Kagida yazilan
// tablo eskiyor; kod eskimiyor.
// -------------------------------------------------------------------------
void kablolamayi_yaz()
{
    ESP_LOGI(ETIKET, "");
    ESP_LOGI(ETIKET, "===== KABLOLAMA =====");
    ESP_LOGI(ETIKET, "INMP441 mikrofon:");
    ESP_LOGI(ETIKET, "  VDD -> 3V3   <<< 5V VERILIRSE YANAR");
    ESP_LOGI(ETIKET, "  GND -> GND");
    ESP_LOGI(ETIKET, "  L/R -> GND   (bosta kalirsa veri gelmez)");
    ESP_LOGI(ETIKET, "  SCK -> GPIO%d", PATI_MIK_SCK);
    ESP_LOGI(ETIKET, "  WS  -> GPIO%d", PATI_MIK_WS);
    ESP_LOGI(ETIKET, "  SD  -> GPIO%d", PATI_MIK_SD);
    ESP_LOGI(ETIKET, "MAX98357 amfi:");
    ESP_LOGI(ETIKET, "  VIN  -> 5V");
    ESP_LOGI(ETIKET, "  GND  -> GND");
    ESP_LOGI(ETIKET, "  BCLK -> GPIO%d", PATI_AMFI_BCLK);
    ESP_LOGI(ETIKET, "  LRC  -> GPIO%d", PATI_AMFI_LRC);
    ESP_LOGI(ETIKET, "  DIN  -> GPIO%d", PATI_AMFI_DIN);
    ESP_LOGI(ETIKET, "  GAIN -> BOS  (9 dB varsayilan)");
    ESP_LOGI(ETIKET, "  SD   -> BOS");
    ESP_LOGI(ETIKET, "Hoparlor: amfinin cikisina. HICBIR UCU GND'YE GITMEZ.");
    ESP_LOGI(ETIKET, "");
    ESP_LOGI(ETIKET, "BAGLAMA: GPIO35/36/37 (oktal PSRAM hatti, olu)");
    ESP_LOGI(ETIKET, "         GPIO19/20 (USB), GPIO43/44 (UART)");
    ESP_LOGI(ETIKET, "=====================");
    ESP_LOGI(ETIKET, "");
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
    kablolamayi_yaz();

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
    if (pati::gozler_baslat() != ESP_OK) {
        ESP_LOGW(ETIKET, "ekran yok — Pati yuzsuz devam ediyor");
    } else {
        pati::gozler_bos();
    }

    // AG — artik gomulu wifi yok (§4.3). Kayitli ag varsa baglaniyor,
    // yoksa kendi agini yayinlayip captive portal aciyor.
    //
    // BLOKLAMIYOR: baglanma denemeleri saniyeler suruyor ve o sure
    // boyunca gozler olu kalmamali.
    pati::ayar_baslat();
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

    // ANAHTARI SIMDI SINA. Aga yeni baglandik; panel acilir acilmaz
    // dogru durumu gostersin.
    //
    // Bunu yapmasak panel "bilinmiyor" derdi ve anne sohbet
    // baslamayinca sebebini hicbir yerden okuyamazdi. Istek belirtec
    // harcamiyor (model listesi), yani her aciliste calistirmanin
    // faturaya etkisi yok.
    if (pati::anahtar_var()) {
        pati::anahtar_dogrula();
    }

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

        const std::uint32_t gonderilemeyen = pati::sohbet_gonderilemeyen();
        if (gonderilemeyen > 0) {
            // Bu da SIFIR olmali. Sifirdan buyukse mikrofon sesi
            // Gemini'ye ULASMIYOR — robot cevap vermiyorsa sebebi bu.
            ESP_LOGW(ETIKET, "GONDERILEMEYEN SES PARCASI: %u",
                     static_cast<unsigned>(gonderilemeyen));
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
        if (goz_atlanan > 0) {
            ESP_LOGW(ETIKET, "ATLANAN KARE: %u — cizim butcesi asiliyor "
                             "(son kare %u us, %u piksel)",
                     static_cast<unsigned>(goz_atlanan),
                     static_cast<unsigned>(pati::gozler_kare_us()),
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
        }
    }
}
