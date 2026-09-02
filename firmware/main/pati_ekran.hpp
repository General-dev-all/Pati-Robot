// Pati'nin ekrani — ST7789P3 240x135 (yatay), SPI.
//
// ===========================================================================
// NEDEN TAM CERCEVE TAMPONU YOK
// ===========================================================================
//
// Ilk tasarim tam bir cerceve tamponunu (onceki kartta 240x240x2 =
// 115 KB, burada 240x135x2 = 65 KB) PSRAM'de
// tutup ekrana basmakti. ESP-IDF dokumani (memory-types) bunu
// engelliyor:
//
//     "Most peripheral DMA controllers require sending/receiving
//      buffers to be placed in DRAM and be word-aligned."
//
// Yani ekrana gidecek veri PSRAM'de degil IC RAM'de olmali. 115 KB'yi
// ic RAM'de tutmak TLS tamponlariyla yarismak demek (512 KB'nin icinde
// zaten Gemini baglantisi var).
//
// COZUM — SERIT: cerceve tamponu HIC YOK. Gozler dogrudan kucuk bir
// DMA-uygun serit tamponuna ciziliyor ve her serit hemen ekrana
// basiliyor. 240 x 24 x 2 = 11,5 KB. Iki tane var (asagi bak).
//
// Bu tesadufen daha iyi cikti: alfa karistirmasi (parlama katmanlari,
// cam parlamasi) serit icinde yapiliyor, cunku bir satira degen butun
// katmanlar ayni gecişte ciziliyor. Cerceve tamponu olsa da ayni
// islemi yapacaktik.
//
// ===========================================================================
// IKI TAMPON, KUYRUK DERINLIGI 1
// ===========================================================================
//
// `esp_lcd_panel_draw_bitmap()` ISI KUYRUGA ATIP DONUYOR. Tek tampon
// olsa bir sonraki seridi cizerken hala gonderilmekte olan verinin
// uzerine yazardik ve ekranda cizgi cizgi bozulma olurdu — sebebi de
// "ara ara bozuluyor" diye aylarca aranirdi.
//
// Iki tampon + kuyruk derinligi 1:
//   serit N   -> A'ya ciz, gonder (kuyruga girdi)
//   serit N+1 -> B'ye ciz, gonder -> KUYRUK DOLU, N bitene kadar bekler
//   dondugunde A serbest, sirayla devam
//
// Boylece hem guvenli hem SPI gonderimi ile cizim ust uste biniyor.

#pragma once

#include <cstdint>

#include <esp_err.h>

namespace pati {

// Serit yuksekligi. 240 genislikte 24 satir = 11,5 KB tampon.
//
// Neden 24: tampon ic RAM'de duruyor ve iki tane var (23 KB toplam).
// Daha buyuk serit SPI islemi sayisini azaltir ama ic RAM'i yer;
// daha kucugu islem basi ek yuku artirir. 24 ikisinin arasi ve
// 240'i tam bolmuyor (240/24 = 10) — kalan satir hesabi zaten var.
constexpr int EKRAN_SERIT_YUKSEK = 24;

// Ekrani ayaga kaldirir: SPI veri yolu, panel, arka isik.
//
// Basarisiz olursa GERI DONUYOR, programi durdurmuyor. Sebep: ekran
// olmadan da Pati konusabiliyor (ses yolu ekrandan bagimsiz). Ekran
// bozuksa robot sessiz kalmamali, sadece yuzu olmamali.
esp_err_t ekran_baslat();

// Arka isik. BUNU ACMAZSAN EKRAN BOS GORUNUR.
//
// Su an sadece ac/kapa. Parlaklik ayari (LEDC PWM) bilerek yok:
// ekranla ilgili hicbir sey henuz olculmedi, once "yaniyor mu ve
// renkler dogru mu" sorusu cevaplanacak.
void ekran_arka_isik(bool ac);

// Arka isik parlakligi, 0.15 - 1.00.
//
// NEDEN VAR: arka isik bu kartin en buyuk SABIT akim musterisi.
// Brownout'u hoparlorun anlik tepesi yapiyor ama o tepe taban akimin
// uzerine biniyor — tabani dusurmek, sesi kismadan pay kazandiriyor.
//
// 0.15'in altina inilmiyor: orasi "kisik ekran" degil "kapali ekran"
// gibi gorunuyor ve cocuk Pati'yi bozuk sanar.
float ekran_parlaklik_ayarla(float yeni);
float ekran_parlaklik();

// Cizim yapilacak serit tamponu. Her `ekran_serit_bas()` cagrisindan
// sonra OTEKI tampona geciyor — donen isaretciyi saklamayin.
std::uint16_t* ekran_serit();

// Serit tamponundaki `gen x yuk` alani ekranin (x0, y0) noktasina basar.
//
// Tampon satir uzunlugu `gen` kabul ediliyor, yani cizim yapan taraf
// satirlari sikistirilmis yazmali.
esp_err_t ekran_serit_bas(int x0, int y0, int gen, int yuk);

// RGB'yi ekranin bekledigi 16 bitlik degere cevirir.
//
// BAYT SIRASI BURADA COZULUYOR ve bir VARSAYIM iceriyor (bkz. .cpp).
// Renkler ters gorunurse duzeltilecek tek yer bu fonksiyon.
std::uint16_t ekran_renk(std::uint8_t r, std::uint8_t g, std::uint8_t b);

// Ilk acilista bir kez cizilen renk deseni.
//
// NEDEN VAR: iki bilinmeyeni tek bakista cozuyor — bayt sirasi ve renk
// tersligi. Seri porta "ne gormen gerekiyor" yaziliyor; gordugun sey
// baskaysa hangi ayarin yanlis oldugu belli oluyor. Tahmin yerine
// gozlem.
void ekran_test_deseni();

// Butun ekrani tek renge boyar (gozler baslamadan once siyahlamak icin).
esp_err_t ekran_doldur(std::uint16_t renk);

bool ekran_hazir();

}  // namespace pati
