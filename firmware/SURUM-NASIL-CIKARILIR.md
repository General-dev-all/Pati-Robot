# Yeni sürüm nasıl çıkarılır

**Push et. Bitti.**

VS Code'da **Sync** düğmesine basmak yeterli. Sürüm numarasıyla
uğraşman gerekmiyor — GitHub bir öncekinin yamasını bir artırıyor
(`2.3.4` → `2.3.5`), firmware'i kendi sunucusunda derliyor, Release
açıyor ve içine `pati.bin` ile `surum.json` koyuyor. Üç beş dakika.

Bitince anne panelden **"Güncellemeleri kontrol et"** deyince yeni
sürümü görüyor.

> Sync **önce çeker, sonra gönderir.** GitHub yayımdan sonra
> `surum.txt`'ye yeni numarayı yazıyor; bir sonraki Sync'te o satır
> kendiliğinden sana geliyor. Bir şey yapman gerekmiyor.

---

## Annenin okuyacağı not

Panelde güncelleme kutusunda **tek bir cümle** görünüyor. O cümle
`firmware/surum.txt`'nin ilk satırının altında duruyor:

```
2.3.4
Gözler daha akıcı, uyku sayacı düzeltildi.
```

İlk satır sürüm — **ona dokunmana gerek yok**, GitHub güncelliyor.
Altındaki satır anneye ait ve onu Claude her değişiklikte tazeliyor.

Not boşsa yayım **durur**: anneye gösterilecek tek metin o, boş
kalamaz.

---

## Sürümü elle yükseltmek istersen

Büyük bir değişiklikte numaranın anlamlı atlamasını istiyorsan ilk
satıra kendin yaz:

```
2.4.0
Pati artık şarkı söylüyor.
```

Yayımlanan sürümden büyükse GitHub ona saygı duyup onu kullanır.
Küçükse yok sayar ve yamayı artırmaya devam eder — yani yanlışlıkla
eski bir numara yazman bir şeyi bozmaz.

---

## Nasıl gittiğini nereden görürsün

Depo sayfasında **Actions** sekmesi. Yeşil tik = yayımlandı. Kırmızı
çarpı = bir şey ters gitti ve **hiçbir şey yayımlanmadı** — anne yanlış
bir şey görmez, eski sürümde kalır.

---

## Hangi push'lar yayımlıyor

Yalnızca `firmware/` ya da `panel/` altında bir şey değişmişse. İkisi de
robota giren şeyler — panel firmware'in içine gömülüyor.

README, PLAN gibi belgeleri push etmek Release açmıyor: ikili dosya
değişmediği için anneye aynı firmware'i yeniden indirtmekten başka bir
işe yaramazdı.

---

## Bir şey ters giderse

**Derleme kırmızı yandı.** Actions sekmesinde hangi adımda durduğu
yazıyor. En sık iki sebep:

- `surum.txt`'nin ilk satırı `2.3.4` biçiminde değil
- İlk satırın altında not yok — anneye gösterilecek metin boş kalamaz

**Anne güncelledi ama Pati açılmadı.** Kendiliğinden eski sürüme döner
(`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). Yeni yapı, ağ katmanı ve
panel ayağa kalkana kadar "deneme" sayılıyor; oraya varamayan bir yapı
bir sonraki açılışta geri alınıyor. Kablo gerekmez.

**İndirme yarıda kaldı.** Çalışan sürüme dokunulmuyor — indirme boştaki
bölüme yazılıyor. Pati eski sürümde çalışmaya devam eder, anne tekrar
deneyebilir.

---

## Neden `surum.json` artık depoda değil

Depodaydı ve sessiz bir kusuru vardı: push, manifesti **anında** yayına
sokuyor ama `pati.bin`'i oluşturmuyor. Derleme bitene kadar geçen
dakikalarda panel "güncelleme var" der, anne basar, indirme 404'e
düşerdi.

Şimdi ikisi de aynı Release'in içinde. Manifest, tarif ettiği dosyadan
**önce var olamıyor** — dikkatle değil, yapısı gereği.

---

## Kabloyla yükleme ne zaman gerekir

Neredeyse hiç. İki durum dışında:

1. **Bölüm tablosu değişirse** (`firmware/partitions.csv`). OTA yalnızca
   uygulama bölümüne yazıyor, tabloya dokunmuyor.
2. **Robot açılamayacak kadar bozulursa** — geri alma bunu zaten
   karşılıyor, ama geri alınacak sağlam bir sürüm de yoksa.

İkisi de `firmware/KARTA-YUKLE.bat` ile. `app-flash` yetmez, tam
`flash` gerekir.
