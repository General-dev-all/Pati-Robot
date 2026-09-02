// Perdeler — gozlerin YERINE cizilen tam ekran sayfalar
//
// Iki tane var:
//   1. Dusuk pil uyarisi — Pati kendisi karar verip gosteriyor
//   2. Bilgi sayfasi     — cocuk tusa basinca aciliyor
//
// ===========================================================================
// 🔴 BU DOSYAYI SADECE GOZ GOREVI CAGIRABILIR
// ===========================================================================
//
// Cizim `ekran_serit()` / `ekran_serit_bas()` uzerinden gidiyor ve o iki
// serit tamponu GOZ GOREVININ malı. Baska bir gorevden cagrilirsa iki
// gorev ayni tamponu ayni anda doldurur; ekranda cizgi cizgi bozulma
// olur ve sebebi "ara ara bozuluyor" diye aranir (ayni tuzak
// pati_ekran.hpp'de anlatiliyor).
//
// Cagri yeri: pati_gozler.cpp icindeki goz gorevi, kare cizmek yerine.
// Disaridan istek `gozler_pil_uyarisi()` / `gozler_bilgi_ac()` ile
// birakiliyor; ikisi de bloklamiyor.
//
// ===========================================================================
// NEDEN TAM EKRAN, gozlerin altina serit degil
// ===========================================================================
//
// Gozler ekranin y=19..115 araligini kullaniyor (goz 68x64, aralik 28,
// merkez_y 67, uzerine bakis kaymasi ±14/±7 ve 9 piksel parlama).
// Geriye altta 20 piksel kaliyor — bir pil sembolu ve okunabilir bir
// yazi icin dar, ustelik gozler squash & stretch ile o payi yiyebiliyor.
//
// Tam ekran ayrica DAHA UCUZ: perde cizilirken goz cizimi duruyor, yani
// o birkac saniye CPU ve SPI yuku dusuyor. Pilde bunu istiyoruz zaten.

#pragma once

#include <esp_err.h>

namespace pati {

// Dusuk pil uyarisi. Turuncu degil TURKUAZ — gerekce .cpp'de.
//
// `yuzde` 0-100; 0'dan kucukse yuzde satiri cizilmiyor (bilinmiyor).
esp_err_t perde_pil_uyarisi(int yuzde);

// Bilgi sayfasi: pil doluluk ve wifi.
//
// NEDEN BU IKISI: cocugun bilmek isteyecegi tek iki sey. Sicaklik,
// gerilim, cokme sayaci gibi seyler ebeveyn panelinde duruyor ve
// buraya konsa sayfayi okunmaz yapardi.
//
// Veriyi kendisi topluyor (pil_yuzde, ag_adi, ag_gucu). Cagiran taraf
// bir sey hazirlamiyor.
esp_err_t perde_bilgi();

}  // namespace pati
