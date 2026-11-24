# MTK block format (Y in 16x32 tiles, CbCr interleaved in 16x16 tiles) -> I420, and PSNR vs a reference.
import sys, numpy as np
def untile(buf, w, h, tw, th):
    t = buf[:w*h].reshape(h//th, w//tw, th, tw)
    return t.transpose(0, 2, 1, 3).reshape(h, w)
def convert(raw, bw, bh, pw, ph):
    y = untile(raw, bw, bh, 16, 32)
    c = untile(raw[bw*bh:], bw, bh//2, 16, 16)
    u, v = c[:, 0::2], c[:, 1::2]
    return y[:ph, :pw], u[:ph//2, :pw//2], v[:ph//2, :pw//2]
def psnr(a, b):
    m = np.mean((a.astype(float) - b.astype(float))**2)
    return 99.0 if m == 0 else 10*np.log10(255**2/m)
if __name__ == '__main__':
    raw, ref, bw, bh, pw, ph = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:7])
    order = sys.argv[7] if len(sys.argv) > 7 else None
    fsz = bw*bh*3//2; rsz = pw*ph*3//2
    r = np.fromfile(raw, np.uint8); f = np.fromfile(ref, np.uint8)
    n = len(r)//fsz
    idx = list(map(int, order.split(','))) if order else range(n)
    out = open(raw + '.i420', 'wb')
    for i in range(n):
        y, u, v = convert(r[i*fsz:(i+1)*fsz], bw, bh, pw, ph)
        for p in (y, u, v): out.write(np.ascontiguousarray(p).tobytes())
        j = idx[i]
        g = f[j*rsz:(j+1)*rsz]
        print(f"frame {i} (ref {j}): PSNR Y {psnr(y, g[:pw*ph].reshape(ph,pw)):.1f} dB  U {psnr(u, g[pw*ph:pw*ph*5//4].reshape(ph//2,pw//2)):.1f}  V {psnr(v, g[pw*ph*5//4:rsz].reshape(ph//2,pw//2)):.1f}")
