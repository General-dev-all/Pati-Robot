// Pati sohbet dongusu — parcalari birbirine baglayan yer
//
// Buraya kadar her sey ayri duruyordu: mikrofon okuyor, hoparlor yaziyor,
// wifi bagliyor, defter sayiyor. Bu dosya onlari Gemini Live istemcisine
// baglayip Asama 2'nin sayisini uretiyor.
//
// ---------------------------------------------------------------------------
// GOREV YAPISI — ve neden boyle
// ---------------------------------------------------------------------------
//
//   [mikrofon gorevi]  I2S oku -> istemci.push_audio()
//
//   [istemcinin kendi gorevi]  olay uretir -> KUYRUGA at, baska bir sey yapma
//
//   [ses gorevi]  kuyruktan al -> hoparlore yaz, damgala
//
// Uc gorev olmasinin sebebi conversation_service.hpp'de yazili:
//
//   "THREADING: the callback runs in the backend's internal task context
//    (for the OpenAI client, the esp_websocket_client event task). It must
//    be cheap and non-blocking — the recommended pattern is to marshal the
//    event onto a FreeRTOS queue and let an application task do the real
//    work."
//
// Hoparlore yazmak BLOKLAYAN bir is (I2S DMA kuyrugu dolduysa bekliyor).
// Onu geri cagirimin icinde yapmak websocket gorevini durdurur; sonuc
// ara ara kekeleyen ses olur ve sebebi gunlerce aranir.
//
// ---------------------------------------------------------------------------
// GOAWAY — Python'da uc kosu almisti, dersi buraya tasindi
// ---------------------------------------------------------------------------
//
// Sunucu kapanmadan ~50 sn once goAway gonderiyor. Kopmayi ENGELLEMIYORUZ
// (zaten olacak); NE ZAMAN olacagini seciyoruz.
//
//   1. GoingAway olayi gelince bayrak kalkiyor
//   2. Yenileme, konusmanin ilk dogal bosluguna kadar BEKLIYOR
//      (tur bitmis + hoparlor susmus)
//   3. O anda stop() + start(): oturum devam anahtari korunuyor
//
// Ve Python'da kendi duzeltmemin actigi acik: yenileme sirasinda
// push_audio() cagirilirsa kapali baglantiya yazilir. Burada bayrakla
// engelleniyor — mikrofon gorevi yenileme penceresinde ses GONDERMIYOR,
// sessizce dusuruyor. Cokme yok.

#pragma once

#include <cstdint>

#include <esp_err.h>

namespace pati {

// Sohbeti baslatir: istemciyi kurar, gorevleri acar, baglanir.
//
// wifi_baglan() BASARILI olduktan sonra cagrilmali.
esp_err_t sohbet_baslat();

// Kac tur tamamlandi (rapor icin).
std::uint32_t sohbet_tur_sayisi();

// Sohbet hala ayakta mi?
bool sohbet_calisiyor();

// Oturum sessizlikten dolayi kapandi mi.
//
// 🔴 PANELE GONDERILMESI SART. Panel uyku durumunu gozlerin ifadesinden
// TAHMIN ETMIYOR, ayri bir bayrak bekliyor. Bayrak gitmeyince robot
// uyurken panel "dinliyor" yaziyordu — gozler dogru, yazi yanlis
// (31.07.2026, telefonda goruldu).
bool sohbet_uyuyor();

// Kuyruk dolu oldugu icin DUSEN olay sayisi.
//
// NEDEN DISA ACILIYOR: sifir olmali. Sifirdan buyukse ses gorevi
// yetismiyor demektir ve o durumda hem ses kesilir hem olcum eksilir.
// Sessizce birikmesine izin vermek, sonra "neden bazi turlar kayit
// edilmemis" diye aramaya yol acar.
std::uint32_t sohbet_dusen_olay();

// push_audio() basarisiz olan parca sayisi.
//
// NEDEN DISA ACILIYOR: sessizce basarisiz olursa ses Gemini'ye HIC
// gitmez ve robot hicbir sey duymaz. Disardan "robot cevap vermiyor"
// diye gorunur, sebebi mikrofonda ya da agda aranir. Sayi burada.
std::uint32_t sohbet_gonderilemeyen();

// Son rapor araliginda mikrofondan gelen EN YUKSEK genlik (0-32767).
// Okuyunca sifirlaniyor.
//
// Sessiz odada bile birkac yuz olmali. TAM SIFIR gelirse mikrofon hic
// veri vermiyor demektir — Gemini'ye sessizlik gidiyor ve robot
// cevap vermiyor.
std::uint32_t sohbet_mik_tepe();

// Son aralikta mikrofon kac kez okundu ve kaci BOS dondu.
//
// bos == okuma  -> I2S/DMA hic veri vermiyor
// bos == 0      -> veri geliyor; sessizse sorun kodekte/ADC'de
void sohbet_mik_okuma(std::uint32_t& okuma, std::uint32_t& bos);

// Oturumu kapatir, ses ve mikrofon gorevlerini sonlandirir.
//
// Guncelleme icin var (pati_guncelleme): indirme sirasinda TLS tamponu
// ile Gemini'nin TLS'i ayni anda ayakta olunca PSRAM'de sikisiyoruz.
// Ustelik indirme bitince cihaz yeniden basliyor — cocugun cumlesinin
// ortasinda kesmektense once susmasi dogru.
//
// GERI DONUSU YOK: `sohbet_baslat()` yeniden cagrilmadikca sohbet geri
// gelmiyor. Guncellemeden sonra cihaz zaten yeniden basliyor.
void sohbet_durdur();

}  // namespace pati
