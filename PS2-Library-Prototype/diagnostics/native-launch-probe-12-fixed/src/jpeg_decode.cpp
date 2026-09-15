// Minimal baseline + progressive JPEG decoder to RGBA8.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implements ITU-T T.81 sequential DCT (SOF0) and progressive DCT (SOF2),
// 8-bit, 1 or 3 components, integer sampling factors, restart intervals, and
// any scan script. Arithmetic coding, 12-bit, and 4-component CMYK are
// rejected so the UI shows a placeholder instead of garbage.
#include "jpeg_decode.hpp"

#include <cstring>

namespace jpeg_probe {
namespace {

constexpr int M_SOF0 = 0xC0;
constexpr int M_SOF1 = 0xC1;
constexpr int M_SOF2 = 0xC2;
constexpr int M_DHT = 0xC4;
constexpr int M_SOI = 0xD8;
constexpr int M_EOI = 0xD9;
constexpr int M_SOS = 0xDA;
constexpr int M_DQT = 0xDB;
constexpr int M_DRI = 0xDD;

constexpr int zigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

constexpr int max_blocks = ((max_width + 7) / 8 + 1) * ((max_height + 7) / 8 + 1);

struct Huff
{
    std::uint8_t bits[17]{};
    std::uint8_t vals[256]{};
    int mincode[17]{};
    int maxcode[18]{};
    int valptr[17]{};
};

struct Component
{
    int id = 0, h = 1, v = 1, tq = 0, td = 0, ta = 0;
    int block_cols = 0, block_rows = 0;
    std::uint8_t *plane = nullptr;
    int stride = 0;
};

struct Decoder
{
    const std::uint8_t *p = nullptr;
    const std::uint8_t *end = nullptr;
    std::uint32_t buf = 0;
    int cnt = 0;
    bool eof = false;

    std::uint16_t quant[4][64]{};
    Huff dc[4], ac[4];
    Component comp[4];
    int ncomp = 0;
    int width = 0, height = 0, max_h = 1, max_v = 1;
    int restart_interval = 0;
    bool progressive_image = false;

    std::uint8_t planes[3][plane_capacity]{};
    float row_out[64]{};
};

Decoder g;
std::int16_t g_block[3][static_cast<std::size_t>(max_blocks) * 64];

// C(u) * cos((2x+1)u*pi/16), precomputed so the decoder adds no libm import.
constexpr float g_ct[8][8] = {
    {0.707106781f, 0.707106781f, 0.707106781f, 0.707106781f, 0.707106781f, 0.707106781f, 0.707106781f, 0.707106781f},
    {0.980785280f, 0.831469612f, 0.555570233f, 0.195090322f, -0.195090322f, -0.555570233f, -0.831469612f, -0.980785280f},
    {0.923879533f, 0.382683432f, -0.382683432f, -0.923879533f, -0.923879533f, -0.382683432f, 0.382683432f, 0.923879533f},
    {0.831469612f, -0.195090322f, -0.980785280f, -0.555570233f, 0.555570233f, 0.980785280f, 0.195090322f, -0.831469612f},
    {0.707106781f, -0.707106781f, -0.707106781f, 0.707106781f, 0.707106781f, -0.707106781f, -0.707106781f, 0.707106781f},
    {0.555570233f, -0.980785280f, 0.195090322f, 0.831469612f, -0.831469612f, -0.195090322f, 0.980785280f, -0.555570233f},
    {0.382683432f, -0.923879533f, 0.923879533f, -0.382683432f, -0.382683432f, 0.923879533f, -0.923879533f, 0.382683432f},
    {0.195090322f, -0.555570233f, 0.831469612f, -0.980785280f, 0.980785280f, -0.831469612f, 0.555570233f, -0.195090322f},
};

void idct_block(const int coeff[64], std::uint8_t *dst, int stride) noexcept
{
    float tmp[64];
    for (int v = 0; v < 8; ++v)
        for (int x = 0; x < 8; ++x)
        {
            float s = 0.0f;
            for (int u = 0; u < 8; ++u)
                s += g_ct[u][x] * static_cast<float>(coeff[v * 8 + u]);
            tmp[v * 8 + x] = s * 0.5f;
        }
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
        {
            float s = 0.0f;
            for (int v = 0; v < 8; ++v)
                s += g_ct[v][y] * tmp[v * 8 + x];
            int i = static_cast<int>(s * 0.5f + 128.5f);
            if (i < 0)
                i = 0;
            else if (i > 255)
                i = 255;
            dst[y * stride + x] = static_cast<std::uint8_t>(i);
        }
}

int get_bit() noexcept
{
    if (g.cnt == 0)
    {
        if (g.eof)
            return 0;
        if (g.p >= g.end)
        {
            g.eof = true;
            return 0;
        }
        int b = *g.p++;
        if (b == 0xFF)
        {
            if (g.p < g.end && *g.p == 0x00)
                ++g.p;
            else
            {
                --g.p;
                g.eof = true;
                return 0;
            }
        }
        g.buf = static_cast<std::uint32_t>(b);
        g.cnt = 8;
    }
    --g.cnt;
    return static_cast<int>((g.buf >> g.cnt) & 1U);
}

int get_bits(int n) noexcept
{
    int v = 0;
    for (int i = 0; i < n; ++i)
        v = (v << 1) | get_bit();
    return v;
}

int huff_decode(const Huff &h) noexcept
{
    int code = 0;
    for (int l = 1; l <= 16; ++l)
    {
        code = (code << 1) | get_bit();
        if (h.maxcode[l] >= 0 && code <= h.maxcode[l])
        {
            const int idx = h.valptr[l] + code - h.mincode[l];
            return (idx < 0 || idx >= 256) ? -1 : h.vals[idx];
        }
    }
    return -1;
}

int extend(int v, int s) noexcept
{
    if (s == 0)
        return 0;
    const int half = 1 << (s - 1);
    return (v < half) ? (v - (1 << s) + 1) : v;
}

void build_huff(Huff &h) noexcept
{
    int code = 0, k = 0;
    for (int l = 1; l <= 16; ++l)
    {
        if (h.bits[l] > 0)
        {
            h.valptr[l] = k;
            h.mincode[l] = code;
            k += h.bits[l];
            code += h.bits[l];
            h.maxcode[l] = code - 1;
        }
        else
            h.maxcode[l] = -1;
        code <<= 1;
    }
    h.maxcode[17] = 0x7FFFFFFF;
}

bool decode_dc_coefficient(int ci, int &pred_in) noexcept
{
    const int t = huff_decode(g.dc[g.comp[ci].td]);
    if (t < 0)
        return false;
    pred_in += extend(get_bits(t), t);
    return true;
}

bool decode_ac_first(int ci, std::int16_t *block, int ss, int se, int al, int &eob_run) noexcept
{
    const Huff &ha = g.ac[g.comp[ci].ta];
    const int band_end = se > 63 ? 63 : se;
    if (eob_run > 0)
    {
        --eob_run;
        return true;
    }
    int k = ss;
    while (k <= band_end)
    {
        const int rs = huff_decode(ha);
        if (rs < 0)
            return false;
        const int r = rs >> 4, s = rs & 15;
        if (s == 0)
        {
            if (r != 15)
            {
                eob_run = 1 << r;
                if (r > 0)
                    eob_run += get_bits(r);
                --eob_run;
                break;
            }
            k += 16;
            continue;
        }
        k += r;
        if (k > band_end)
            break;
        block[zigzag[k]] = static_cast<std::int16_t>(extend(get_bits(s), s) << al);
        ++k;
    }
    return true;
}

bool decode_ac_refine(int ci, std::int16_t *block, int ss, int se, int al, int &eob_run) noexcept
{
    const Huff &ha = g.ac[g.comp[ci].ta];
    const int p1 = 1 << al;
    const int band_end = se > 63 ? 63 : se;
    int k = ss;

    if (eob_run == 0)
    {
        while (k <= band_end)
        {
            const int rs = huff_decode(ha);
            if (rs < 0)
                return false;
            const int r = rs >> 4, s = rs & 15;
            int newval = 0;
            if (s != 0)
            {
                if (s != 1)
                    return false;
                newval = get_bit() ? p1 : -p1;
            }
            else if (r != 15)
            {
                eob_run = 1 << r;
                if (r > 0)
                    eob_run += get_bits(r);
                break;
            }
            // Advance over `r` zero-history coefficients, refreshing the
            // correction bit of every nonzero coefficient on the way.
            int remaining = r;
            while (k <= band_end)
            {
                std::int16_t &coef = block[zigzag[k]];
                if (coef != 0)
                {
                    if (get_bit() && (coef & p1) == 0)
                        coef = static_cast<std::int16_t>(coef > 0 ? coef + p1 : coef - p1);
                }
                else
                {
                    if (remaining == 0)
                        break;
                    --remaining;
                }
                ++k;
            }
            if (newval != 0 && k <= band_end)
                block[zigzag[k]] = static_cast<std::int16_t>(newval);
            ++k;
        }
    }

    if (eob_run > 0)
    {
        for (; k <= band_end; ++k)
        {
            std::int16_t &coef = block[zigzag[k]];
            if (coef != 0 && get_bit() && (coef & p1) == 0)
                coef = static_cast<std::int16_t>(coef > 0 ? coef + p1 : coef - p1);
        }
        --eob_run;
    }
    return true;
}

bool decode_block(int ci, int bx, int by, int ss, int se, int ah, int al,
                  int &pred_in, int &eob_run) noexcept
{
    Component &c = g.comp[ci];
    std::int16_t *block = g_block[ci] + (static_cast<std::size_t>(by) * c.block_cols + bx) * 64;

    if (!g.progressive_image)
    {
        for (int i = 0; i < 64; ++i)
            block[i] = 0;
        if (!decode_dc_coefficient(ci, pred_in))
            return false;
        block[0] = static_cast<std::int16_t>(pred_in);
        const Huff &ha = g.ac[c.ta];
        int k = 1;
        while (k < 64)
        {
            const int rs = huff_decode(ha);
            if (rs < 0)
                return false;
            const int r = rs >> 4, s = rs & 15;
            if (s == 0)
            {
                if (r == 15)
                {
                    k += 16;
                    continue;
                }
                break;
            }
            k += r;
            if (k > 63)
                break;
            block[zigzag[k]] = static_cast<std::int16_t>(extend(get_bits(s), s));
            ++k;
        }
        return true;
    }

    if (ss == 0)
    {
        if (ah == 0)
        {
            if (!decode_dc_coefficient(ci, pred_in))
                return false;
            block[0] = static_cast<std::int16_t>(pred_in << al);
        }
        else if (get_bit())
        {
            block[0] = static_cast<std::int16_t>(block[0] | (1 << al));
        }
        return true;
    }
    if (ah == 0)
        return decode_ac_first(ci, block, ss, se, al, eob_run);
    return decode_ac_refine(ci, block, ss, se, al, eob_run);
}

void seek_marker() noexcept
{
    g.cnt = 0;
    g.buf = 0;
    g.eof = false;
    while (g.p < g.end)
    {
        if (*g.p == 0xFF)
        {
            if (g.p + 1 >= g.end)
            {
                g.p = g.end;
                return;
            }
            if (g.p[1] == 0x00 || g.p[1] == 0xFF)
            {
                ++g.p;
                continue;
            }
            return;
        }
        ++g.p;
    }
}

void seek_restart() noexcept
{
    g.cnt = 0;
    g.buf = 0;
    g.eof = false;
    while (g.p < g.end)
    {
        if (*g.p == 0xFF)
        {
            if (g.p + 1 >= g.end)
            {
                g.eof = true;
                return;
            }
            const int m = g.p[1];
            if (m >= 0xD0 && m <= 0xD7)
            {
                g.p += 2;
                return;
            }
            if (m == 0x00 || m == 0xFF)
            {
                ++g.p;
                continue;
            }
            g.eof = true;
            return;
        }
        ++g.p;
    }
    g.eof = true;
}

bool read_u16(const std::uint8_t *&p, const std::uint8_t *end, int &out) noexcept
{
    if (p + 2 > end)
        return false;
    out = (p[0] << 8) | p[1];
    p += 2;
    return true;
}

bool decode_scan(const int scan_comps[], int nscan, int ss, int se, int ah, int al) noexcept
{
    int pred[4] = {0, 0, 0, 0};
    int eob_run = 0;
    const bool interleaved = nscan > 1;

    int mcus_x, mcus_y;
    if (interleaved)
    {
        mcus_x = (g.width + 8 * g.max_h - 1) / (8 * g.max_h);
        mcus_y = (g.height + 8 * g.max_v - 1) / (8 * g.max_v);
    }
    else
    {
        mcus_x = g.comp[scan_comps[0]].block_cols;
        mcus_y = g.comp[scan_comps[0]].block_rows;
    }

    int mcu = 0;
    for (int my = 0; my < mcus_y; ++my)
    {
        for (int mx = 0; mx < mcus_x; ++mx)
        {
            if (g.restart_interval > 0 && mcu > 0 && (mcu % g.restart_interval) == 0)
            {
                seek_restart();
                for (int i = 0; i < 4; ++i)
                    pred[i] = 0;
                eob_run = 0;
            }
            if (interleaved)
            {
                for (int si = 0; si < nscan; ++si)
                {
                    const int ci = scan_comps[si];
                    Component &c = g.comp[ci];
                    for (int by = 0; by < c.v; ++by)
                        for (int bx = 0; bx < c.h; ++bx)
                            if (!decode_block(ci, mx * c.h + bx, my * c.v + by, ss, se, ah, al,
                                              pred[ci], eob_run))
                                return false;
                }
            }
            else
            {
                const int ci = scan_comps[0];
                if (!decode_block(ci, mx, my, ss, se, ah, al, pred[ci], eob_run))
                    return false;
            }
            ++mcu;
        }
    }
    return true;
}

} // namespace

Result decode(std::span<const std::uint8_t> jpeg, std::span<std::uint8_t> rgba, Image &out) noexcept
{
    if (jpeg.size() < 4)
        return Result::too_small;
    if (jpeg[0] != 0xFF || jpeg[1] != M_SOI)
        return Result::bad_magic;
    if (rgba.empty())
        return Result::out_of_bounds;

    g = Decoder{};

    const std::uint8_t *p = jpeg.data() + 2;
    const std::uint8_t *const end = jpeg.data() + jpeg.size();
    bool have_sof = false;

    while (p + 1 < end)
    {
        if (*p != 0xFF)
        {
            ++p;
            continue;
        }
        const int marker = p[1];
        if (marker == 0x00 || marker == 0xFF)
        {
            ++p;
            continue;
        }
        p += 2;
        if (marker == M_EOI)
            break;
        if (marker == M_SOI || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
            continue;
        int len = 0;
        if (!read_u16(p, end, len) || len < 2)
            return Result::corrupt;
        const std::uint8_t *seg = p;
        const std::uint8_t *seg_end = p + (len - 2);
        if (seg_end > end)
            return Result::corrupt;

        if (marker == M_DQT)
        {
            const std::uint8_t *q = seg;
            while (q < seg_end)
            {
                const int pq_tq = *q++;
                const int pq = pq_tq >> 4, tq = pq_tq & 15;
                if (tq > 3)
                    return Result::unsupported;
                for (int i = 0; i < 64; ++i)
                {
                    if (pq == 0)
                    {
                        if (q >= seg_end)
                            return Result::corrupt;
                        g.quant[tq][zigzag[i]] = *q++;
                    }
                    else
                    {
                        if (q + 1 >= seg_end)
                            return Result::corrupt;
                        g.quant[tq][zigzag[i]] = static_cast<std::uint16_t>((q[0] << 8) | q[1]);
                        q += 2;
                    }
                }
            }
        }
        else if (marker == M_DHT)
        {
            const std::uint8_t *h = seg;
            while (h < seg_end)
            {
                const int tc_th = *h++;
                const int tc = tc_th >> 4, th = tc_th & 15;
                if (th > 3 || tc > 1)
                    return Result::unsupported;
                Huff &tbl = (tc == 0) ? g.dc[th] : g.ac[th];
                int total = 0;
                for (int l = 1; l <= 16; ++l)
                {
                    if (h >= seg_end)
                        return Result::corrupt;
                    tbl.bits[l] = *h++;
                    total += tbl.bits[l];
                }
                if (total > 256)
                    return Result::corrupt;
                for (int i = 0; i < total; ++i)
                {
                    if (h >= seg_end)
                        return Result::corrupt;
                    tbl.vals[i] = *h++;
                }
                build_huff(tbl);
            }
        }
        else if (marker == M_DRI)
        {
            if (seg + 2 > seg_end)
                return Result::corrupt;
            g.restart_interval = (seg[0] << 8) | seg[1];
        }
        else if (marker == M_SOF0 || marker == M_SOF1 || marker == M_SOF2)
        {
            if (marker == M_SOF1)
                return Result::unsupported;
            if (seg + 6 > seg_end)
                return Result::corrupt;
            if (seg[0] != 8)
                return Result::unsupported;
            g.height = (seg[1] << 8) | seg[2];
            g.width = (seg[3] << 8) | seg[4];
            g.ncomp = seg[5];
            g.progressive_image = (marker == M_SOF2);
            if (g.ncomp < 1 || g.ncomp > 3)
                return Result::unsupported;
            if (g.width < 1 || g.height < 1)
                return Result::corrupt;
            if (g.width > max_width || g.height > max_height)
                return Result::out_of_bounds;
            const std::uint8_t *c = seg + 6;
            for (int i = 0; i < g.ncomp; ++i)
            {
                if (c + 3 > seg_end)
                    return Result::corrupt;
                Component &comp = g.comp[i];
                comp.id = c[0];
                comp.h = c[1] >> 4;
                comp.v = c[1] & 15;
                comp.tq = c[2];
                if (comp.h < 1 || comp.v < 1 || comp.tq > 3 || comp.h > 4 || comp.v > 4)
                    return Result::unsupported;
                if (comp.h > g.max_h)
                    g.max_h = comp.h;
                if (comp.v > g.max_v)
                    g.max_v = comp.v;
                c += 3;
            }
            const int mcus_x = (g.width + 8 * g.max_h - 1) / (8 * g.max_h);
            const int mcus_y = (g.height + 8 * g.max_v - 1) / (8 * g.max_v);
            for (int i = 0; i < g.ncomp; ++i)
            {
                Component &comp = g.comp[i];
                comp.block_cols = mcus_x * comp.h;
                comp.block_rows = mcus_y * comp.v;
                comp.stride = comp.block_cols * 8;
                comp.plane = g.planes[i];
                const std::size_t blocks =
                    static_cast<std::size_t>(comp.block_cols) * comp.block_rows;
                if (blocks > static_cast<std::size_t>(max_blocks))
                    return Result::out_of_bounds;
                std::memset(g_block[i], 0, blocks * 64 * sizeof(std::int16_t));
                const std::size_t need = static_cast<std::size_t>(comp.stride) * comp.block_rows;
                if (need > plane_capacity)
                    return Result::out_of_bounds;
                std::memset(comp.plane, 0, need);
            }
            have_sof = true;
        }
        else if (marker == M_SOS)
        {
            if (!have_sof)
                return Result::corrupt;
            if (seg + 1 > seg_end)
                return Result::corrupt;
            const int ns = seg[0];
            if (ns < 1 || ns > g.ncomp)
                return Result::unsupported;
            int scan_comps[4] = {0, 0, 0, 0};
            const std::uint8_t *c = seg + 1;
            for (int i = 0; i < ns; ++i)
            {
                if (c + 2 > seg_end)
                    return Result::corrupt;
                const int cid = c[0], tables = c[1];
                int found = -1;
                for (int j = 0; j < g.ncomp; ++j)
                    if (g.comp[j].id == cid)
                        found = j;
                if (found < 0)
                    return Result::corrupt;
                scan_comps[i] = found;
                g.comp[found].td = tables >> 4;
                g.comp[found].ta = tables & 15;
                if (g.comp[found].td > 3 || g.comp[found].ta > 3)
                    return Result::unsupported;
                c += 2;
            }
            if (c + 3 > seg_end)
                return Result::corrupt;
            const int ss = c[0], se = c[1], ahal = c[2];
            const int ah = ahal >> 4, al = ahal & 15;
            if (!g.progressive_image && (ss != 0 || se != 63 || ah != 0 || al != 0))
                return Result::corrupt;
            if (al > 13 || ah > 13)
                return Result::unsupported;

            g.p = seg_end;
            g.end = end;
            g.cnt = 0;
            g.buf = 0;
            g.eof = false;

            if (!decode_scan(scan_comps, ns, ss, se, ah, al))
                return Result::corrupt;
            seek_marker();
            p = g.p;
            continue;
        }

        p = seg_end;
    }

    if (!have_sof)
        return Result::corrupt;

    for (int i = 0; i < g.ncomp; ++i)
    {
        Component &comp = g.comp[i];
        int coeff[64];
        for (int by = 0; by < comp.block_rows; ++by)
        {
            for (int bx = 0; bx < comp.block_cols; ++bx)
            {
                const std::int16_t *block =
                    g_block[i] + (static_cast<std::size_t>(by) * comp.block_cols + bx) * 64;
                bool any = false;
                for (int k = 0; k < 64; ++k)
                {
                    const int v = block[k] * g.quant[comp.tq][k];
                    coeff[k] = v;
                    if (v)
                        any = true;
                }
                std::uint8_t *dst = comp.plane + (by * 8) * comp.stride + (bx * 8);
                if (!any)
                {
                    for (int y = 0; y < 8; ++y)
                        for (int x = 0; x < 8; ++x)
                            dst[y * comp.stride + x] = 128;
                }
                else
                    idct_block(coeff, dst, comp.stride);
            }
        }
    }

    if (rgba.size() < static_cast<std::size_t>(g.width) * g.height * 4)
        return Result::out_of_bounds;

    out.width = g.width;
    out.height = g.height;
    std::uint8_t *d = rgba.data();

    if (g.ncomp == 1)
    {
        const Component &y = g.comp[0];
        for (int row = 0; row < g.height; ++row)
        {
            const std::uint8_t *row_ptr = y.plane + (row * y.v / g.max_v) * y.stride;
            for (int col = 0; col < g.width; ++col)
            {
                const std::uint8_t v = row_ptr[col * y.h / g.max_h];
                *d++ = v;
                *d++ = v;
                *d++ = v;
                *d++ = 255;
            }
        }
        return Result::ok;
    }

    const Component &cy = g.comp[0], &cb = g.comp[1], &cr = g.comp[2];
    for (int row = 0; row < g.height; ++row)
    {
        const std::uint8_t *yrow = cy.plane + (row * cy.v / g.max_v) * cy.stride;
        const std::uint8_t *cbrow = cb.plane + (row * cb.v / g.max_v) * cb.stride;
        const std::uint8_t *crrow = cr.plane + (row * cr.v / g.max_v) * cr.stride;
        for (int col = 0; col < g.width; ++col)
        {
            const int y = yrow[col * cy.h / g.max_h];
            const int u = cbrow[col * cb.h / g.max_h] - 128;
            const int v = crrow[col * cr.h / g.max_h] - 128;
            auto clamp8 = [](float f) noexcept -> std::uint8_t {
                int i = static_cast<int>(f + 0.5f);
                if (i < 0)
                    i = 0;
                else if (i > 255)
                    i = 255;
                return static_cast<std::uint8_t>(i);
            };
            *d++ = clamp8(static_cast<float>(y) + 1.4020f * static_cast<float>(v));
            *d++ = clamp8(static_cast<float>(y) - 0.344136f * static_cast<float>(u) -
                          0.714136f * static_cast<float>(v));
            *d++ = clamp8(static_cast<float>(y) + 1.7720f * static_cast<float>(u));
            *d++ = 255;
        }
    }
    return Result::ok;
}

const char *summary(Result result) noexcept
{
    switch (result)
    {
    case Result::ok:
        return "cover decoded";
    case Result::too_small:
        return "cover too small";
    case Result::bad_magic:
        return "cover not a jpeg";
    case Result::unsupported:
        return "cover mode unsupported";
    case Result::corrupt:
        return "cover corrupt";
    case Result::out_of_bounds:
        return "cover dimensions unsupported";
    }
    return "cover error";
}

} // namespace jpeg_probe
