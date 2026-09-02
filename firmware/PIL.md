# Pil modu — Pati pilde çökmemeli

Bu belge **devam eden bir işin** çalışma notu. `TESHIS.md` çözülmüş
sorunları anlatır; burası henüz çözülmemiş olanı.

---

## Amaç

**Pati pille çalışırken çökmemeli.**

Çocuk Pati'yi çoğunlukla pilde kullanacak. "Şarja tak" bir cevap değil —
çocuğu sürekli kabloya bağlı kalmaya zorlayamayız. Harici besleme
(ileride motorlar için gelecek olan) bu sorunu çözer ama **pil modunu
ortadan kaldırmaz**; ikisi ayrı iş.

USB'de Pati sorunsuz çalışıyor. Sorun yalnızca pilde.

---

## 🔴 Kullanıcının öncelik sırası — pazarlık burada biter

Feda etme sırası, en önemliden en feda edilebilire:

1. **Çökmeme** — her şeyin üstünde
2. **Ses seviyesi** — `SES_PIL_TAVANI` **0.70'te sabit, DAHA AŞAĞI
   İNİLMİYOR.** Kullanıcı bunu iki kez açıkça söyledi. Pilde kısık ses
   gerçek bir kayıp: çocuk duyamıyorsa robot işe yaramıyor.
3. **Ekran parlaklığı** — pilde %45. Gerekirse biraz daha kısılabilir,
   ama önce diğerleri denenmeli.
4. **Gözler** — pilde feda edilebilir tek yer. İfadeler güzel ama
   akıcılık, sohbetin kendisinden önce gelmez.

---

## Belirti

Pilde, Pati **konuşmaya başladığı anda** brownout:

```
00:13:03  ifade=dusunuyor    <- cevap uretiliyor
00:13:05  COKTU
00:13:10  geri geldi -> acilis=brownout
```

Öncesinde pil geriliminde sarkma görülüyor (3816 tabanından 3696'ya).
Sıklık: **20-60 saniyede bir**, pil 3,8-3,9 V civarındayken.

Pati 5 saniyede kendine geliyor — bekçi işini yapıyor. Ama çocuk için
cümle ortasında kaybolmak kabul edilemez.

---

## Ölçülmüş gerçekler

- **Pil sağlam.** 4,13 V'tan başlıyor; çökmeler 3,8-3,9 V'ta da,
  4,0 V'ta da oluyor. "Pil bitmiş" değil.
- **İki kart da aynı.** İkinci kart kutudan yeni çıktı, aynı davranıyor.
  **Donanım kusuru değil**, garanti konusu değil.
- **Tetikleyici konuşma.** Her çökme `dusunuyor`/`konusuyor` anında.
  Yani amfi + telsiz + PSRAM aynı anda akım çekerken.
- **Sıcaklık etken değil.** 55-64 °C ölçüldü; bu yonga için normal,
  PMIC'in ısıl koruması bu sıcaklıkta devreye girmez.
- **Göz çizici en büyük sürekli CPU müşterisi.** Kare başına çizim
  24-30 ms, bütçe 50 ms — bir çekirdeğin yarısından fazlası kesintisiz.
  İşin %85'i şekil rasterlemesinde.

---

## Denenenler

| Deneme | Gerekçe | Sonuç |
|---|---|---|
| Ses 1.00 → 0.70 | amfi akımı azalsın | ❌ çökme sürdü |
| Ekran %100 → %45 | taban akım düşsün, tepeye pay kalsın | ❌ çökme sürdü |
| PSRAM 80 → 40 MHz | veri yolu kilitleniyordu | ✅ ama **başka bir sınıfı** çözdü (bkz. TESHIS.md), brownout'u değil |
| Gözler 20 → 10 fps (pilde) | CPU doluluğu yarıya insin | 🔄 **şu an sınanıyor** |

⚠️ İlk ikisi **sağlam akıl yürütmeye** dayanıyordu ve tutmadı. Ders:
akım üzerine akıl yürütmek bu projede işe yaramıyor, çünkü **hiç akım
ölçülmedi.** İşe yarayan tek değişiklik (PSRAM), mekanizması çözümlenmiş
program sayacıyla görülen tekti.

---

## Denenmemiş adaylar

**Wifi verici gücü** — en ciddi şüpheli. ESP32-S3 telsizi gönderirken
250-350 mA çekiyor; bu muhtemelen hoparlör amfisinden **daha büyük** bir
tepe. `esp_wifi_set_max_tx_power()` ile kısılabilir. Bedeli menzil, ve
wifi zaten 1-3/4 olduğu için bedava değil.

**CPU 240 → 160 MHz (pilde).** Kayda değer akım kazancı. Ama 20 fps'te
kare bütçesi zaten dardı (160 MHz'de kare 45-49 ms / bütçe 50 ms);
gözler zaten 10 fps'e indiyse bu artık mümkün olabilir.

**Wifi güç tasarrufu (pilde).** Ortalama akımı ciddi düşürür. ⚠️ Ama
`WIFI_PS_NONE` **bilerek** seçilmişti: radyo DTIM aralıklarında uyuyunca
ses tamponu kuruyor ve konuşma kesiliyor. Kullanıcının kırmızı çizgisi
tam da bu.

**Akıllı göz optimizasyonu** — kare hızını düşürmek kaba bir çözüm.
Gerçek kazanç şurada:
- *Sadece değişeni çiz.* Gözler kırpma aralarında sabit; o karelerde iş
  yapılmamalı.
- *Boştayken yavaşla, hareket olunca hızlan.*
- *Konuşma başlarken yükü bırak.* En hedefli olanı: brownout tam o anda
  oluyor.

**Pil gerilimine bağlı kademeli geri çekilme + uyarı.** Kullanıcının
fikri: %20 altında "şarja tak" uyarısı. Doğru ama tek başına yetmez —
çocuk uyarıyı okumaz. Pati pil düştükçe **kendiliğinden** yük bırakmalı
(önce gözler, sonra ekran) ve uyarıyı da vermeli. Eşiklerin hangi
gerilimde başlayacağını ölçüm söyleyecek.

---

## Nasıl ölçülüyor

🔴 **USB'yi çekince seri kablo da gidiyor.** Pilde tek pencere panel.

```powershell
(Invoke-WebRequest http://pati.local/api/durum -UseBasicParsing).Content
```

`guc` alanı: `kaynak`, `pil_mv`, `vin_mv`, `sicaklik_c`, `ses_tavani`,
`goz_fps`, `acilis`, **`cokme`**.

**`cokme` sayacı cihazda, NVS'te durur** ve açılışı atlatır. Yalnızca
arıza sayılır (brownout, panic, bekçi); düğmeye basmak ve kabloyla
yükleme sayılmaz. Dün gece çökmeler ancak bilgisayardan ağ üzerinden
sayılabiliyordu; çocuğun evinde öyle bir imkân olmayacak, ve yapılacak
işin tamamı "şu değişiklik çökmeyi azalttı mı" karşılaştırması.

**A/B yöntemi:** sayacı not al → USB'yi çek → 5-10 dakika normal konuş
(sallamadan; sallama ayrı bir değişken) → sayaca tekrar bak.

Zemin: **dün gece pilde 20-60 saniyede bir çökme.**

---

## Karar kuralı

Bir değişiklik çökme sıklığını **belirgin şekilde** azaltmıyorsa **geri
alınır.** Görünümü ya da sesi bedava bozmuyoruz. Bu gece iki değişiklik
tam bu yüzden geri alındı ya da sorgulandı.
