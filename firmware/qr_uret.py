"""Sabit panel adresleri için QR üretir; çıkan pikselleri bağımsız çözücüyle doğrular.

Gerekenler: qrcode, pillow, zxing-cpp. Çalıştırma: python firmware/qr_uret.py
"""
from pathlib import Path
import qrcode
import zxingcpp
from PIL import Image

root = Path(__file__).resolve().parent
lines = ['// qr_uret.py üretir; elle düzenlenmez. Sessiz kenar dört modüldür.',
         '#pragma once', '#include <cstdint>', 'namespace pati {']
for name, url in [('KURULUM', 'http://192.168.4.1/'), ('PANEL', 'http://pati.local/')]:
    qr = qrcode.QRCode(version=2, error_correction=qrcode.constants.ERROR_CORRECT_M,
                       box_size=3, border=4)
    qr.add_data(url)
    qr.make(fit=False)
    matrix = qr.get_matrix()
    assert len(matrix) == 33
    pixels = Image.new('L', (99, 99), 255)
    for y in range(99):
        for x in range(99):
            pixels.putpixel((x, y), 0 if matrix[y // 3][x // 3] else 255)
    decoded = zxingcpp.read_barcode(pixels)
    assert decoded and decoded.text == url
    lines.append(f'inline constexpr std::uint8_t QR_{name}[33][33] = {{')
    lines.extend('    {' + ','.join(str(int(v)) for v in row) + '},' for row in matrix)
    lines.append('};')
lines.append('} // namespace pati')
target = root / 'main/pati_qr_uretilmis.h'
content = '\n'.join(lines) + '\n'
target.write_bytes(content.replace('\n', '\r\n').encode('utf-8'))
assert target.read_text(encoding='utf-8') == content
print('İki QR 99x99 pikselde çözüldü; üretilen başlık geri okunup doğrulandı.')
