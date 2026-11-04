// QrCodeGen: 精簡的 QR Code (Model 2) 編碼器，只用標準函式庫 (不依賴 Qt)，支援版本 1~40、ECC L/M/Q/H、
// 數字 / 英數 / 位元組 (UTF-8) 三種模式 (自動選擇)、自動挑選遮罩。
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace qrgen {

    struct Matrix {
        int size = 0;                       // 邊長 (模組數)
        std::vector<uint8_t> dark;          // size*size，1 = 黑
        bool at(int x, int y) const { return dark[size_t(y) * size + x] != 0; }
    };

    namespace detail {
        // 索引：[ecc 0=L 1=M 2=Q 3=H][version 0..40]
        static const int8_t kEccPerBlock[4][41] = {
            { -1, 7,10,15,20,26,18,20,24,30,18,20,24,26,30,22,24,28,30,28,28,28,28,30,30,26,28,30,30,30,30,30,30,30,30,30,30,30,30,30,30 },
            { -1,10,16,26,18,24,16,18,22,22,26,30,22,22,24,24,28,28,26,26,26,26,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28 },
            { -1,13,22,18,26,18,24,18,22,20,24,28,26,24,20,30,24,28,28,26,30,28,30,30,30,30,28,30,30,30,30,30,30,30,30,30,30,30,30,30,30 },
            { -1,17,28,22,16,22,28,26,26,24,28,24,28,22,24,24,30,28,28,26,28,30,24,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30 } };
        static const int8_t kNumBlocks[4][41] = {
            { -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9,10,12,12,12,13,14,15,16,17,18,19,19,20,21,22,24,25 },
            { -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9,10,10,11,13,14,16,17,17,18,20,21,23,25,26,28,29,31,33,35,37,38,40,43,45,47,49 },
            { -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8,10,12,16,12,17,16,18,21,20,23,23,25,27,29,34,34,35,38,40,43,45,48,51,53,56,59,62,65,68 },
            { -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8,11,11,16,16,18,16,19,21,25,25,25,34,30,32,35,37,40,42,45,48,51,54,57,60,63,66,70,74,77,81 } };

        inline int rawModules(int ver) {
            int r = (16 * ver + 128) * ver + 64;
            if (ver >= 2) { int a = ver / 7 + 2; r -= (25 * a - 10) * a - 55; if (ver >= 7) r -= 36; }
            return r;
        }
        inline int dataCodewords(int ver, int ecc) { return rawModules(ver) / 8 - kEccPerBlock[ecc][ver] * kNumBlocks[ecc][ver]; }

        inline uint8_t gfMul(uint8_t x, uint8_t y) {
            int z = 0;
            for (int i = 7; i >= 0; --i) { z = (z << 1) ^ ((z >> 7) * 0x11D); z ^= ((y >> i) & 1) * x; }
            return uint8_t(z);
        }
        inline std::vector<uint8_t> rsDivisor(int degree) {
            std::vector<uint8_t> r(degree, 0);
            r[degree - 1] = 1;
            uint8_t root = 1;
            for (int i = 0; i < degree; ++i) {
                for (int j = 0; j < degree; ++j) { r[j] = gfMul(r[j], root); if (j + 1 < degree) r[j] ^= r[j + 1]; }
                root = gfMul(root, 0x02);
            }
            return r;
        }
        inline std::vector<uint8_t> rsRemainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& div) {
            std::vector<uint8_t> r(div.size(), 0);
            for (uint8_t b : data) {
                const uint8_t f = b ^ r[0];
                r.erase(r.begin()); r.push_back(0);
                for (size_t i = 0; i < r.size(); ++i) r[i] ^= gfMul(div[i], f);
            }
            return r;
        }

        struct Bits {
            std::vector<uint8_t> v;
            void put(uint32_t val, int len) { for (int i = len - 1; i >= 0; --i) v.push_back((val >> i) & 1); }
        };

        inline int alnumValue(char c) {
            static const char* cs = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
            for (int i = 0; cs[i]; ++i) if (cs[i] == c) return i;
            return -1;
        }

        // 把資料轉成位元串 (不含終止碼 / 補齊)；回傳 false = 版本放不下
        inline void buildSegment(const std::string& s, int mode, int ver, Bits& b) {
            const int cc = ver <= 9 ? 0 : (ver <= 26 ? 1 : 2);
            if (mode == 0) {            // 數字
                b.put(1, 4); b.put(uint32_t(s.size()), (cc == 0 ? 10 : cc == 1 ? 12 : 14));
                for (size_t i = 0; i < s.size(); i += 3) {
                    const size_t n = std::min<size_t>(3, s.size() - i);
                    b.put(uint32_t(std::stoi(s.substr(i, n))), int(n) * 3 + 1);
                }
            }
            else if (mode == 1) {       // 英數
                b.put(2, 4); b.put(uint32_t(s.size()), (cc == 0 ? 9 : cc == 1 ? 11 : 13));
                for (size_t i = 0; i + 1 < s.size(); i += 2) b.put(uint32_t(alnumValue(s[i]) * 45 + alnumValue(s[i + 1])), 11);
                if (s.size() % 2) b.put(uint32_t(alnumValue(s.back())), 6);
            }
            else {                      // 位元組 (UTF-8)
                b.put(4, 4); b.put(uint32_t(s.size()), (cc == 0 ? 8 : 16));
                for (unsigned char c : s) b.put(c, 8);
            }
        }
    } // namespace detail

    // ecc：0=L 1=M 2=Q 3=H；utf8：已是 UTF-8 的位元組字串。資料太長放不進版本 40 時回傳 false。
    inline bool encode(const std::string& utf8, int ecc, Matrix& out) {
        using namespace detail;
        if (ecc < 0 || ecc > 3 || utf8.empty()) return false;
        int mode = 0;
        for (char c : utf8) { if (c < '0' || c > '9') { mode = 1; break; } }
        if (mode == 1) for (char c : utf8) if (alnumValue(c) < 0) { mode = 2; break; }

        int ver = 0; Bits bits;
        for (int v = 1; v <= 40; ++v) {
            Bits b; buildSegment(utf8, mode, v, b);
            if (int(b.v.size()) <= dataCodewords(v, ecc) * 8) { ver = v; bits = std::move(b); break; }
        }
        if (!ver) return false;
        const int cap = dataCodewords(ver, ecc) * 8;
        bits.put(0, std::min(4, cap - int(bits.v.size())));
        bits.put(0, (8 - int(bits.v.size()) % 8) % 8);
        for (uint8_t pad = 0xEC; int(bits.v.size()) < cap; pad ^= 0xEC ^ 0x11) bits.put(pad, 8);
        std::vector<uint8_t> data(bits.v.size() / 8, 0);
        for (size_t i = 0; i < bits.v.size(); ++i) data[i >> 3] |= bits.v[i] << (7 - (i & 7));

        // --- 加上 Reed-Solomon 並交錯 ---
        const int nb = kNumBlocks[ecc][ver], eccLen = kEccPerBlock[ecc][ver];
        const int raw = rawModules(ver) / 8, nShort = nb - raw % nb, shortLen = raw / nb;
        std::vector<std::vector<uint8_t>> blocks;
        const std::vector<uint8_t> div = rsDivisor(eccLen);
        for (int i = 0, k = 0; i < nb; ++i) {
            std::vector<uint8_t> dat(data.begin() + k, data.begin() + k + shortLen - eccLen + (i < nShort ? 0 : 1));
            k += int(dat.size());
            const std::vector<uint8_t> e = rsRemainder(dat, div);
            if (i < nShort) dat.push_back(0);
            dat.insert(dat.end(), e.begin(), e.end());
            blocks.push_back(dat);
        }
        std::vector<uint8_t> all;
        for (size_t i = 0; i < blocks[0].size(); ++i)
            for (size_t j = 0; j < blocks.size(); ++j)
                if (i != size_t(shortLen - eccLen) || j >= size_t(nShort)) all.push_back(blocks[j][i]);

        // --- 畫模組 ---
        const int n = ver * 4 + 17;
        std::vector<uint8_t> mod(size_t(n) * n, 0), fn(size_t(n) * n, 0);
        auto setF = [&](int x, int y, bool d) { mod[size_t(y) * n + x] = d; fn[size_t(y) * n + x] = 1; };
        for (int i = 0; i < n; ++i) { setF(6, i, i % 2 == 0); setF(i, 6, i % 2 == 0); }
        auto finder = [&](int cx, int cy) {
            for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) {
                const int d = std::max(std::abs(dx), std::abs(dy)), x = cx + dx, y = cy + dy;
                if (x >= 0 && x < n && y >= 0 && y < n) setF(x, y, d != 2 && d != 4);
            }
            };
        finder(3, 3); finder(n - 4, 3); finder(3, n - 4);
        if (ver > 1) {
            const int na = ver / 7 + 2;
            const int step = ver == 32 ? 26 : (ver * 4 + na * 2 + 1) / (na * 2 - 2) * 2;
            std::vector<int> pos;
            for (int i = 0, p = n - 7; i < na - 1; ++i, p -= step) pos.insert(pos.begin(), p);
            pos.insert(pos.begin(), 6);
            for (int i = 0; i < na; ++i) for (int j = 0; j < na; ++j) {
                if ((i == 0 && j == 0) || (i == 0 && j == na - 1) || (i == na - 1 && j == 0)) continue;
                for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
                    setF(pos[i] + dx, pos[j] + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
            }
        }
        const int fmtBits[4] = { 1, 0, 3, 2 };       // L M Q H
        auto drawFormat = [&](int mask) {
            const int d = fmtBits[ecc] << 3 | mask;
            int rem = d;
            for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
            const int bits = (d << 10 | rem) ^ 0x5412;
            auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };
            for (int i = 0; i <= 5; ++i) setF(8, i, bit(i));
            setF(8, 7, bit(6)); setF(8, 8, bit(7)); setF(7, 8, bit(8));
            for (int i = 9; i < 15; ++i) setF(14 - i, 8, bit(i));
            for (int i = 0; i < 8; ++i) setF(n - 1 - i, 8, bit(i));
            for (int i = 8; i < 15; ++i) setF(8, n - 15 + i, bit(i));
            setF(8, n - 8, true);
            };
        drawFormat(0);
        if (ver >= 7) {
            int rem = ver;
            for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
            const int bits = ver << 12 | rem;
            for (int i = 0; i < 18; ++i) {
                const bool bt = ((bits >> i) & 1) != 0; const int a = n - 11 + i % 3, b = i / 3;
                setF(a, b, bt); setF(b, a, bt);
            }
        }
        // 放資料
        size_t bi = 0;
        for (int right = n - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < n; ++vert) for (int j = 0; j < 2; ++j) {
                const int x = right - j; const bool up = ((right + 1) & 2) == 0; const int y = up ? n - 1 - vert : vert;
                if (!fn[size_t(y) * n + x] && bi < all.size() * 8) { mod[size_t(y) * n + x] = (all[bi >> 3] >> (7 - (bi & 7))) & 1; ++bi; }
            }
        }
        // 挑遮罩 (依評分最低)
        auto applyMask = [&](int m) {
            for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
                bool inv = false;
                switch (m) {
                case 0: inv = (x + y) % 2 == 0; break; case 1: inv = y % 2 == 0; break;
                case 2: inv = x % 3 == 0; break;       case 3: inv = (x + y) % 3 == 0; break;
                case 4: inv = (x / 3 + y / 2) % 2 == 0; break;
                case 5: inv = x * y % 2 + x * y % 3 == 0; break;
                case 6: inv = (x * y % 2 + x * y % 3) % 2 == 0; break;
                default: inv = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                }
                if (inv && !fn[size_t(y) * n + x]) mod[size_t(y) * n + x] ^= 1;
            }
            };
        auto penalty = [&]() {
            int p = 0;
            auto M = [&](int x, int y) { return mod[size_t(y) * n + x] != 0; };
            for (int pass = 0; pass < 2; ++pass) {
                for (int a = 0; a < n; ++a) {
                    int run = 1;
                    for (int b = 1; b < n; ++b) {
                        const bool same = pass ? M(a, b) == M(a, b - 1) : M(b, a) == M(b - 1, a);
                        if (same) { ++run; if (run == 5) p += 3; else if (run > 5) p += 1; }
                        else run = 1;
                    }
                    for (int b = 0; b + 11 <= n; ++b) {            // 類似定位圖形 1011101 + 4 白
                        static const int A[11] = { 1,0,1,1,1,0,1,0,0,0,0 }, B[11] = { 0,0,0,0,1,0,1,1,1,0,1 };
                        bool ma = true, mb = true;
                        for (int k = 0; k < 11 && (ma || mb); ++k) {
                            const int v = pass ? M(a, b + k) : M(b + k, a);
                            if (v != A[k]) ma = false;
                            if (v != B[k]) mb = false;
                        }
                        if (ma) p += 40;
                        if (mb) p += 40;
                    }
                }
            }
            for (int y = 0; y + 1 < n; ++y) for (int x = 0; x + 1 < n; ++x) {
                const bool c = M(x, y);
                if (c == M(x + 1, y) && c == M(x, y + 1) && c == M(x + 1, y + 1)) p += 3;
            }
            int dark = 0; for (uint8_t v : mod) dark += v;
            const int total = n * n;
            p += ((std::abs(dark * 20 - total * 10) + total - 1) / total - 1) * 10;
            return p;
            };
        int best = 0, bestP = 1 << 30;
        for (int m = 0; m < 8; ++m) {
            applyMask(m); drawFormat(m);
            const int p = penalty();
            if (p < bestP) { best = m; bestP = p; }
            applyMask(m);                                   // 再套一次 = 還原
        }
        applyMask(best); drawFormat(best);
        out.size = n; out.dark = std::move(mod);
        return true;
    }

} // namespace qrgen