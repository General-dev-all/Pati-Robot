// Tam ekran uyari — su an yalnizca "pilim bitiyor"
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
// Tam ekran ayrica DAHA UCUZ: uyari cizilirken goz cizimi duruyor, yani
// o birkac saniye CPU ve SPI yuku dusuyor. Pilde bunu istiyoruz zaten.
//
// ===========================================================================
// NEDEN SUREKLI DEGIL
// ===========================================================================
//
// Uyari dakikada bir, birkac saniye gorunuyor. Surekli gosterilse iki
// sey birden bozulurdu: cocuk Pati'nin yuzunu goremezdi ve uyari
// "arka plan" haline gelip okunmaz olurdu.

#pragma once

#include <esp_err.h>

namespace pati {

// Dusuk pil uyarisini tam ekran cizer ve DONER — bekleme yapmiyor,
// ekranda ne kadar kalacagina cagiran karar veriyor.
//
// `yuzde` 0-100; 0'dan kucukse yuzde satiri cizilmiyor (bilinmiyor).
esp_err_t uyari_pil_ciz(int yuzde);

}  // namespace pati
