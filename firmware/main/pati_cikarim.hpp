// Konusmadan kalici bilgi cikarma — cihazda.
//
// ===========================================================================
// NEDEN KONUSMA SIRASINDA DEGIL, SONRA
// ===========================================================================
//
// Modele `hatirla()` diye bir arac verip konusma sirasinda cagirtmak da
// mumkundu. YAPMIYORUZ, cunku olculdu: yuz ifadesi aracinda her cagri
// cevabin onune fazladan bir gidis-donus koydu ve medyan gecikme
// ~790 ms'den ~1.428 ms'ye cikti (prototype/hafiza.py bas yorumu).
//
// Hafiza icin ayni bedeli odemeye gerek yok — konusma BITTIKTEN sonra,
// cocugu bekletmeden yapilabilir.
//
// ===========================================================================
// NE ZAMAN CALISIYOR: UYKUYA GECERKEN
// ===========================================================================
//
// PC'de cikarim "durdur" dugmesine basilinca calisiyordu ve gercek
// kullanimda o an HIC gelmiyor — cocuk konusmayi birakip gidiyor,
// robotta ise fis cekiliyor. Bu yuzden PC tarafinda da uyku kancasi
// eklendi; burada bastan oyle.
//
// Uyku, konusmanin bittiginin dogal isareti: N dakika sessizlikten
// sonra geliyor.
//
// ===========================================================================
// DOKUM HER CIKARIMDAN SONRA TEMIZLENIYOR — PC'den farkli
// ===========================================================================
//
// PC tarafi konusmanin ILK 12.000 karakterini aliyor. Orada oturumlar
// kisa (test kosusu) ve bu sorun degil.
//
// Robotta oturum saatlerce surebiliyor: cocuk butun ogleden sonra
// oynuyor. Ilk 8 KB'yi tutsak sadece gunun ilk bes dakikasindan bilgi
// cikarirdik. Onun yerine her uykuda cikarip dokumu TEMIZLIYORUZ —
// yani gun boyunca parca parca ogreniliyor.
//
// Maliyet: uyku basina ~$0,00065 (ucuz cikarim modeli). Gunde on kez
// uyusa ayda ~20 sent.

#pragma once

#include <cstdint>
#include <string>

#include <esp_err.h>

namespace pati {

// Dokume bir satir ekler. Sinir dolduysa YENI metin atiliyor (eski
// korunuyor) — cunku konusmanin basinda tanisma geciyor.
void cikarim_dokum_ekle(const char* kim, const std::string& metin);

bool cikarim_dokum_var();
size_t cikarim_dokum_boyu();

// Cikarimi calistirir: modele sorar, donen bilgileri hafizaya yazar,
// dokumu temizler.
//
// BLOKLAR (~2-10 sn, ag hizina bagli). Ses gorevinden CAGRILMAMALI;
// uyku gorevinden cagriliyor.
//
// Doner: eklenen bilgi sayisi. Hata durumunda -1 ve sebep gunluge
// yaziliyor — dokum SILINMIYOR ki bir sonraki uykuda tekrar denensin.
int cikarim_calistir(const char* api_anahtari);

}  // namespace pati
