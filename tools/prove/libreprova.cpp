/* =============================================================================
 * tools/prove/libreprova.cpp — the third-party libraries, inside EX-OS
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     libreprova           one line per check, then "LIBRE fine <failures>"
 *
 * zlib, bzip2, xz (liblzma), lz4, zstd, PCRE2 and {fmt} as ported by
 * tools/<library>-exos/costruisci.sh, linked static from the cross sysroot:
 *
 *   x86_64-exos-g++ -O2 libreprova.cpp -o libreprova -lfmt -lpcre2-posix \
 *       -lpcre2-8 -lzstd -llz4 -llzma -lbz2 -lz
 *
 * Each compressor packs 64 KB of text and gets it back byte for byte; the
 * size it packed to is printed (a compressor that "works" by copying would
 * pass the round trip and fail there). tools/prova_librerie.sh runs it.
 *   LIBRE ok <what>   /   LIBRE NO <what>
 * ============================================================================= */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include <zlib.h>
#include <bzlib.h>
#include <lzma.h>
#include <lz4.h>
#include <lz4frame.h>
#include <zstd.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <pcre2posix.h>
#include <fmt/format.h>

static int g_male;

static void prova(const std::string &cosa, bool giusto)
{
    std::printf("LIBRE %s %s\n", giusto ? "ok" : "NO", cosa.c_str());
    if (!giusto) g_male++;
}

static std::vector<unsigned char> testo()
{
    std::string s;
    for (int i = 0; s.size() < 65536; i++)
        s += fmt::format("riga {:5d}: EX-OS porta le librerie vere, {} e {}.\n",
                         i, i * 7 % 13, "ancora");
    s.resize(65536);
    return std::vector<unsigned char>(s.begin(), s.end());
}

static bool uguale(const std::vector<unsigned char> &a, const unsigned char *b, size_t n)
{
    return a.size() == n && std::memcmp(a.data(), b, n) == 0;
}

int main()
{
    std::vector<unsigned char> t = testo();
    std::vector<unsigned char> c(t.size() * 2 + 1024), d(t.size() + 1024);

    /* zlib */
    {
        uLongf nc = c.size(), nd = d.size();
        int r1 = compress2(c.data(), &nc, t.data(), t.size(), 9);
        int r2 = uncompress(d.data(), &nd, c.data(), nc);
        prova(fmt::format("zlib {}: 65536 -> {} byte e ritorno", zlibVersion(), nc),
              r1 == Z_OK && r2 == Z_OK && nc < 20000 && uguale(t, d.data(), nd));
    }
    /* bzip2 */
    {
        unsigned int nc = c.size(), nd = d.size();
        int r1 = BZ2_bzBuffToBuffCompress((char *)c.data(), &nc, (char *)t.data(), t.size(), 9, 0, 0);
        int r2 = BZ2_bzBuffToBuffDecompress((char *)d.data(), &nd, (char *)c.data(), nc, 0, 0);
        prova(fmt::format("bzip2 {}: 65536 -> {} byte e ritorno", BZ2_bzlibVersion(), nc),
              r1 == BZ_OK && r2 == BZ_OK && nc < 20000 && uguale(t, d.data(), nd));
    }
    /* xz / lzma */
    {
        size_t nc = 0, nd = 0, in = 0;
        uint64_t limite = UINT64_MAX;
        lzma_ret r1 = lzma_easy_buffer_encode(6, LZMA_CHECK_CRC64, NULL, t.data(), t.size(),
                                              c.data(), &nc, c.size());
        lzma_ret r2 = lzma_stream_buffer_decode(&limite, 0, NULL, c.data(), &in, nc,
                                                d.data(), &nd, d.size());
        prova(fmt::format("xz {}: 65536 -> {} byte e ritorno", lzma_version_string(), nc),
              r1 == LZMA_OK && r2 == LZMA_OK && nc < 20000 && uguale(t, d.data(), nd));
    }
    /* lz4: the block API and the frame format of the lz4 program */
    {
        int nc = LZ4_compress_default((const char *)t.data(), (char *)c.data(), t.size(), c.size());
        int nd = LZ4_decompress_safe((const char *)c.data(), (char *)d.data(), nc, d.size());
        size_t fc = LZ4F_compressFrame(c.data(), c.size(), t.data(), t.size(), NULL);
        prova(fmt::format("lz4 {}: 65536 -> {} byte e ritorno (frame {})", LZ4_versionString(), nc, fc),
              nc > 0 && nc < 40000 && uguale(t, d.data(), nd) && !LZ4F_isError(fc));
    }
    /* zstd */
    {
        size_t nc = ZSTD_compress(c.data(), c.size(), t.data(), t.size(), 19);
        size_t nd = ZSTD_decompress(d.data(), d.size(), c.data(), nc);
        prova(fmt::format("zstd {}: 65536 -> {} byte e ritorno", ZSTD_versionString(), nc),
              !ZSTD_isError(nc) && !ZSTD_isError(nd) && nc < 20000 && uguale(t, d.data(), nd));
    }
    /* PCRE2: a Perl pattern with a named group, and the POSIX layer */
    {
        int err;
        PCRE2_SIZE off;
        pcre2_code *re = pcre2_compile((PCRE2_SPTR) "(?<anno>\\d{4})-(\\d\\d)-(\\d\\d)",
                                       PCRE2_ZERO_TERMINATED, 0, &err, &off, NULL);
        const char *s = "il kernel 0.279 e' del 2026-10-25, il 0.278 del 2026-10-24";
        pcre2_match_data *md = re ? pcre2_match_data_create_from_pattern(re, NULL) : NULL;
        int n = re ? pcre2_match(re, (PCRE2_SPTR)s, strlen(s), 0, 0, md, NULL) : -1;
        std::string anno;
        if (n > 0) {
            PCRE2_UCHAR buf[16];
            PCRE2_SIZE l = sizeof buf;
            if (pcre2_substring_copy_byname(md, (PCRE2_SPTR) "anno", buf, &l) == 0)
                anno.assign((const char *)buf, l);
        }
        unsigned char ver[32] = "";
        pcre2_config(PCRE2_CONFIG_VERSION, ver);
        prova(fmt::format("pcre2 {}: (?<anno>...) trova {}", (const char *)ver, anno),
              n == 4 && anno == "2026");
        regex_t rx;
        regmatch_t m[2];
        bool posix = regcomp(&rx, "kernel ([0-9.]+)", REG_EXTENDED) == 0 &&
                     regexec(&rx, s, 2, m, 0) == 0 &&
                     std::string(s + m[1].rm_so, m[1].rm_eo - m[1].rm_so) == "0.279";
        prova("pcre2-posix: regcomp/regexec", posix);
        if (md) pcre2_match_data_free(md);
        if (re) pcre2_code_free(re);
    }
    /* {fmt} */
    {
        std::string s = fmt::format("{:>8.3f}|{:#x}|{:<5}|{}", 3.14159, 255, "ab", 1e100);
        prova(fmt::format("fmt {}.{}.{}: \"{}\"", FMT_VERSION / 10000, FMT_VERSION / 100 % 100,
                          FMT_VERSION % 100, s),
              s == "   3.142|0xff|ab   |1e+100");
    }
    std::printf("LIBRE fine %d\n", g_male);
    return g_male != 0;
}
