# Teşhis — kesilme, donma, çökme

Pati "cevap vermiyor", "ses kesiliyor", "kendiliğinden yeniden başlıyor"
dendiğinde bakılacak yer burası.

Bu belge 01.09.2026'da yaşanan uzun bir teşhis oturumundan çıktı. O gün
**dört ayrı belirti tek bir kök nedene** bağlandı ve yolda birkaç yanlış
teşhis yapıldı. Yanlış teşhisler de yazılı: aynı tuzağa tekrar düşülmesin.

---

## Önce: nereden bakılır

İki pencere var ve **ikisi de gerekli.**

### Seri port (USB)

```powershell
. "$env:IDF_PATH\export.ps1"
cd firmware
python -m esp_idf_monitor --port COM6 --baud 115200 --no-reset build\pati.elf
```

⚠️ `--no-reset` şart. DTR/RTS'e dokunmak kartı indirme moduna sokuyor
(`boot:0x0 DOWNLOAD`, yeşil ışık yanıp söner) ve uygulama hiç açılmıyor.

⚠️ Monitörden önce kalan python süreçlerini öldür, yoksa port "erişim
engellendi" veriyor.

Backtrace çözümlenmesi için `export.ps1` ÖNCE çalıştırılmalı — yoksa
`xtensa-esp32-elf-addr2line` bulunamıyor ve çökme adresleri ham sayı
olarak kalıyor.

### Panel (ağ) — pilde TEK pencere

```powershell
(Invoke-WebRequest http://pati.local/api/durum -UseBasicParsing).Content
```

🔴 **USB'yi çekince seri kablo da gidiyor.** Yani "pilde ne oluyor"
sorusunun seri porttan cevabı YOK. 01.09.2026'da tam bu duvara çarpıldı:
pilde çökme yaşandı ve ses tavanının devreye girip girmediği
ölçülemedi. Panel `guc` alanı bu yüzden eklendi.

`api/durum` → `guc`: `kaynak` (usb/pil), `pil_mv`, `vin_mv`,
`sicaklik_c`, `ses_tavani`, `acilis`.

Uzun gözlem için 2 saniyede bir örnekleyip dosyaya yazmak işe yarıyor;
çökme anları `ULASILAMIYOR` satırı olarak görünüyor ve `acilis` alanı
sebebini söylüyor.

---

## Sayılar ve sağlıklı değerleri

Beş saniyede bir basılan rapordan:

| Satır | Sağlıklı | Bozuksa ne demek |
|---|---|---|
| `mikrofon tepe genlik` | sessiz 400-2000, konuşma 8000-27000 | 0 → mikrofon ölü. 32768 → **kırpıyor**, kazanç yüksek |
| `dahili SRAM: en dusuk` | > 40.000 | < 10.000 → TLS ayırması çökecek |
| `dahili SRAM: en buyuk blok` | > 25.000 | < 10.000 → **parçalanma**, boş alan bol olsa bile |
| `ATLANAN KARE` | 0-2 | sürekli → çizim bütçesi aşılıyor |
| `GONDERILEMEYEN SES PARCASI` | 0 | > 0 → ses Gemini'ye hiç gitmiyor |
| `BAGLANTI KOPMASI` | sabit | dakikada artıyorsa ağ zayıf |
| `wifi_guc` (panelde `ag.guc`) | 3-4 / 4 | 1-2 → TCP takılmaları başlıyor |
| `acilis` | `guc` / `dis` | `brownout`, `cokme`, `*_bekcisi` → aşağıya bak |

`acilis` değerleri ve anlamları:

- `brownout` — besleme gerilimi çöktü. Ses seviyesi ya da güç kontağı.
- `cokme` — yazılım paniği. Seri portta backtrace vardır.
- `gorev_bekcisi` / `kesme_bekcisi` — bir görev takıldı.
- `guc` — düğmeyle açıldı. `dis` — kabloyla yükleme sonrası.

---

## 🔴 01.09.2026 — dört belirti, tek kök neden

Belirtiler birbirine hiç benzemiyordu:

1. Pati konuşurken **cihaz sallanınca** çöküyordu
2. Bazen kendiliğinden **cevap vermez oluyordu**, bir süre sonra düzeliyordu
3. Hem pilde hem USB'de oluyordu
4. Bazen "burdayım Mert Mert Mert" gibi **ses takılması** duyuluyordu

Zincir şu çıktı:

```
zayıf wifi (1/4)
  → TCP gönderim tamponu boşalmıyor
  → websocket yazması 10 sn bekleyip zaman aşımına düşüyor
     (network_timeout_ms = 10000, gemini_live_client.cpp)
  → bağlantı kopuyor, yeniden bağlanılıyor
  → HER TLS oturumu dahili SRAM'de delik bırakıyor
  → dokuz dakikada en büyük blok 29696 → 10240 bayt
  → mbedTLS 4437 bayt isteyip bulamıyor
  → E Dynamic Impl: alloc(4437 bytes) failed
  → bağlantı ölüyor / panic
```

Sallamak **doğrudan** çöktürmüyordu: el, küçük PCB antenin önünde
zayıflama yapıp zaten sınırda olan bağlantının kopma sıklığını
artırıyor, yani belleği daha hızlı tüketiyordu.

### Ölçülen düzelme

| | Başlangıç | wifi/lwIP → PSRAM | mbedTLS → PSRAM |
|---|---|---|---|
| Boş dahili SRAM | 27.447 | 46.227 | **54.087** |
| **Dip nokta** | **1.903** | 30.475 → 2.611 | **49.067** |
| En büyük blok | 7.680 | 10.240'a düşüyordu | **29.696** |

Dip nokta 1903 bayttı — yani sistem tamamen tükenmenin 2 KB uzağında
çalışıyordu.

Devreye alınan ayarlar (`sdkconfig.defaults`, gerekçeleri orada):
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`,
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`,
`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`.

---

## Aynı gün bulunan diğer üç şey

**Kopan oturumu toparlayan kod yoktu.** `gemini_live_client.cpp`
`disable_auto_reconnect = true` diyor — taşıma katmanı bilerek
toparlanmıyor, işi uygulamaya bırakıyor. `pati_sohbet.cpp`'de
`case Error:` sadece log yazıyordu. Sonuç zombi robot: mikrofon
çalışıyor, ses ölü sokete gidiyor, **kullanım dakikası işlemeye devam
ediyor** (14,0 → 17,6 dk ölçüldü) ve yalnızca fişi çekmek düzeltiyor.
Toparlanma + sessiz-sunucu bekçisi eklendi.

**Kurulum modu tek yönlü bir kapıydı.** Girince kayıtlı ağ bir daha hiç
denenmiyordu (`portMAX_DELAY`). Bir brownout sonrası Pati kurulum
moduna düşüp orada kaldı; ağ oradaydı, şifre doğruydu. Artık 3 dakikada
bir yeniden yokluyor.

**Mikrofon görevi yığıtı taşıyordu.** Uyandırma bu görevden çağrılıyor
ve 5400 karakterlik promptu kurup oturum açıyor; yığıt 3 KB'ydi.
6 KB'ye çıkarıldı. Belirtisi yanıltıcıydı: çocuk konuştuğu an çöküyordu,
yani "Pati'ye seslenince kapanıyor" gibi görünüyordu.

**Mikrofon kırpıyordu.** Kazanç 30 dB'ydi ve tahmine dayanıyordu; tepe
genlik 32768'e oturuyordu. Gemini bozuk sesi yanlış döküyordu — Türkçe
konuşulurken Portekizce ve İtalyanca cümleler geliyordu. 18 dB'de sıfır
kırpma.

---

## Yolda yapılan yanlış teşhisler

Hepsi o an makul görünüyordu; tekrarlanmasın diye yazılı.

**"Pilde çöküyor, USB'de çökmüyor."** Yanlış. Sonra USB'de de çöktü.
Kullanıcı doğrusunu buldu: *hareket + akım çekimi birlikte*. O da tam
doğru değildi — asıl sebep bellekti.

**"Pil kontağı mekanik olarak kesintili."** Dolu pil (4,0 V), kısılmış
ses ve "sallayınca çöküyor" gözlemine dayanıyordu. Belirti gerçekti,
sebep değil.

**"Isınma olabilir."** Ölçüldü: 64,5 °C. Bu yonga için normal, PMIC'in
ısıl koruması bu sıcaklıkta devreye girmez. Sensör bu yüzden eklendi —
soru tekrar sorulursa cevap sayı olsun.

**"Yeniden bağlanma çalışmıyor."** Yanlış; grep satırları kaçırmıştı.
Çalışıyordu, 25-26 ms'de toparlıyordu.

---

## Bu belgeyi yazarken düşülen tuzaklar

**`sdkconfig` varsa `sdkconfig.defaults` OKUNMAZ.** `idf.py reconfigure`
de yetmiyor — dosyayı SİLMEK gerekiyor. `CONFIG_IDF_TARGET` zaten bu
yüzden defaults'ta duruyor, silmek güvenli.

**`CONFIG_MBEDTLS_DYNAMIC_BUFFER` "PSRAM'den al" demek DEĞİL.** O ayar
tamponları kullanılmadığında serbest bırakıyor, nereden alındığını
değiştirmiyor. PSRAM için `MBEDTLS_EXTERNAL_MEM_ALLOC` gerekiyor.
Yorumda yıllarca "PSRAM'den alınsın" yazıyordu ve alınmıyordu.

**Sıcaklık sensörünün aralığı keyfi seçilemiyor.** ESP32-S3 yalnızca
belirli aralıkları destekliyor (-10~80, -30~50, 20~100, 50~125);
eşleşmeyen aralık sessizce reddediliyor. `(-10, 110)` yazılmıştı ve
sensör hiç kurulmuyordu.

**Hata kodu basılmayan uyarı işe yaramıyor.** "sicaklik sensoru
kurulamadi" tek başına sebebi göstermiyordu; üstelik tek sefer basıldığı
için sonradan bakıldığında günlükte hiç yoktu.

**M5PM1'in sürücü başlığı bu yongada birebir tutmuyor.** `VBAT` üst
baytı için "high 4 bits" yazıyor; maskelenince pil 4130 yerine 34 mV
çıkıyor. İki bayt da tam kullanılıyor. `PWR_SRC` (0x04) şeması da
tutmuyor — USB takılıyken 0x05 okunuyor. Bu yüzden güç kaynağı kararı
**VIN gerilimine** bakılarak veriliyor: ölçülebilir ve anlamı kendinden
belli.
