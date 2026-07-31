# Bu klasör bizim kodumuz DEĞİL

Kaynak: **[ciniml/stackchan-idf](https://github.com/ciniml/stackchan-idf)**
`components/conversation/` · Telif: Kenta IDA (fuga@fugafuga.org)
Lisans: **Boost Software License 1.0** — tam metin yanındaki `LICENSE`
dosyasında. Alındığı tarih: **30.07.2026**, `main` dalı.

BSL-1.0 izin verici bir lisans: kullanmaya, çoğaltmaya, değiştirmeye ve
dağıtmaya izin veriyor. Tek şartı telif ve izin bildiriminin kopyalarda
bulunması — kaynak dosyalardaki `SPDX-FileCopyrightText` başlıkları ve bu
klasördeki `LICENSE` bunu karşılıyor.

## Neden aldık

Bu klasördeki `gemini_live_client.cpp` (49 KB) **ESP32'den doğrudan
Gemini Live API'sine bağlanan, çalışan bir istemci.** Sıfırdan yazmak
projenin en riskli parçasıydı; hazır ve izinli olması riski büyük ölçüde
kaldırdı. Şunlar zaten çözülmüş durumda:

- PCM16 **16 kHz giriş / 24 kHz çıkış** — Aşama 1'de ölçtüğümüzün aynısı
- Sunucu tarafı VAD, `vad_silence_ms` ayarlanabilir
- **`goAway` uyarısı + oturum devam anahtarının saklanıp yeniden
  gönderilmesi** — Python tarafında üç koşuda çözdüğümüz iş
- `cancel_response()` → sözünü kesme (barge-in)
- `emit_us` → mikrosaniye damgası, gecikme ölçümü için
- `Stats` toplayıcı (`metrics.hpp`)

## Neden HİÇ DEĞİŞTİRMEDİK

Olduğu gibi kopyalandı, tek satır bile düzenlenmedi. Sebep: çalıştığı
bilinen koda dokunmadan derlemek, sorunu kendi kodumuzda aramak. Bir
sonraki adımda gerekirse budanır.

İçinde **kullanmadığımız** iki dosya da var:
`openai_realtime_client.cpp` ve `xiaozhi_client.cpp`. Silinmediler çünkü
`CMakeLists.txt`'i düzenlemek gerekirdi ve ilk hedef yeşil ışıktı. Binary'de
%57 boş alan var, acelesi yok.

## Yukarı akıştan güncelleme gelirse

Klasör pristine olduğu için üstüne yeniden kopyalanabilir. Bizim
eklediğimiz tek şey bu dosya ve `LICENSE`.

## Bağımlılıkları

Kendi `CMakeLists.txt`inden: `tl_expected` (yanındaki klasör) ·
`esp_websocket_client ^1.2.3` · `esp_audio_codec ^2.5.0` · `json` ·
`mbedtls` · `esp-tls` · `esp_timer` · `esp_event` · `esp_hw_support` · C++20.

İlk ikisi ESP-IDF bileşen kayıt defterinden `idf.py build` sırasında
kendiliğinden iniyor (`managed_components/`, depoya girmiyor).
