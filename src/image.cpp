/*  image.cpp - writing the PE32+ file.
 *
 *  Every constant here was read out of the reference images rather than out of the
 *  specification, and docs/pe-observed.md says which image said what. The three records the
 *  linker writes on its own account - the debug directory, the volatile-metadata word and the
 *  coffgrp contribution map - are filled in here, because only here are all the addresses
 *  they describe settled.
 */
#include "link.h"
#include <cstdio>
#include <cstring>

/*  The DOS header and stub link.exe writes, byte for byte, with e_lfanew left at zero: it is
 *  the same in every image the bed produced, and the Rich checksum is taken over it. */
static const u8 dos_stub[0x80] = {
    0x4D,0x5A,0x90,0x00,0x03,0x00,0x00,0x00, 0x04,0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,
    0xB8,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0E,0x1F,0xBA,0x0E,0x00,0xB4,0x09,0xCD, 0x21,0xB8,0x01,0x4C,0xCD,0x21,0x54,0x68,
    0x69,0x73,0x20,0x70,0x72,0x6F,0x67,0x72, 0x61,0x6D,0x20,0x63,0x61,0x6E,0x6E,0x6F,
    0x74,0x20,0x62,0x65,0x20,0x72,0x75,0x6E, 0x20,0x69,0x6E,0x20,0x44,0x4F,0x53,0x20,
    0x6D,0x6F,0x64,0x65,0x2E,0x0D,0x0D,0x0A, 0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static u32 rol(u32 v, unsigned n) { n &= 31; return n ? ((v << n) | (v >> (32 - n))) : v; }

/*  The Rich key is a checksum: the DOS header rotated byte by byte with e_lfanew left out,
 *  started at the offset the block begins on, then each entry's id rotated by its count.
 *  Nine reference images agree with it exactly. */
static u32 rich_key(const std::vector<std::pair<u32, u32> > &ents)
{
    u32 c = 0x80;
    for (int i = 0; i < 0x80; i++) {
        if (i >= 0x3C && i < 0x40) continue;
        c += rol(dos_stub[i], (unsigned)i);
    }
    for (size_t i = 0; i < ents.size(); i++) c += rol(ents[i].first, ents[i].second);
    return c;
}

/* the RVA and total length of one run of contributions, found by name */
static void group(const Link &lk, const char *name, u32 &rva, u32 &size)
{
    rva = 0; size = 0;
    for (size_t i = 0; i < lk.all.size(); i++) {
        if (lk.all[i]->name != name) continue;
        if (!size) rva = lk.all[i]->rva;
        size += lk.all[i]->size;
    }
}

/*  The linker's own three records, now that the addresses exist. */
static void fill_own_records(Link &lk)
{
    Contrib *dbg = 0, *vol = 0, *grp = 0;
    Module &m = lk.mods.back();
    dbg = &m.secs[0]; vol = &m.secs[1]; grp = &m.secs[2];

    /* the coffgrp map: one entry per run of contributions that share a name */
    size_t at = 4;
    std::string last;
    for (size_t i = 0; i < lk.all.size(); i++) {
        Contrib *c = lk.all[i];
        if (c->name == last) continue;
        last = c->name;
        u32 rva = c->rva, size = 0;
        for (size_t j = i; j < lk.all.size() && lk.all[j]->name == last; j++)
            size = (lk.all[j]->rva + lk.all[j]->size) - rva;
        wr32(&grp->data[at], rva);
        wr32(&grp->data[at + 4], size);
        size_t n = last.size() + 1;
        memcpy(&grp->data[at + 8], last.data(), last.size());
        at += 8 + ((n + 3) & ~(size_t)3);
    }

    /* the volatile-metadata record: the same twenty-four bytes in every image the bed made */
    wr32(&vol->data[0], 0x18);
    wr32(&vol->data[4], 0x80008000u);

    /* the debug directory: one entry, type 13, pointing at the coffgrp record */
    wr32(&dbg->data[4],  lk.opt.timestamp);
    wr32(&dbg->data[12], 13);
    wr32(&dbg->data[16], grp->size - 16);      /* the record; the sixteen zeros after it are not in it */
    wr32(&dbg->data[20], grp->rva);
    wr32(&dbg->data[24], grp->fileoff);
}

bool Link::write_image()
{
    fill_own_records(*this);

    std::vector<std::pair<u32, u32> > ents;
    rich_count(*this, ents);
    u32 key = rich_key(ents);

    std::vector<u8> f(size_of_headers, 0);
    memcpy(&f[0], dos_stub, 0x80);
    wr32(&f[0x3C], lfanew);

    size_t p = 0x80;
    wr32(&f[p], 0x536E6144u ^ key); p += 4;                  /* "DanS" */
    for (int i = 0; i < 3; i++) { wr32(&f[p], key); p += 4; }
    for (size_t i = 0; i < ents.size(); i++) {
        wr32(&f[p], ents[i].first ^ key);
        wr32(&f[p + 4], ents[i].second ^ key);
        p += 8;
    }
    memcpy(&f[p], "Rich", 4); p += 4;
    wr32(&f[p], key); p += 4;
    /* the rest of the reserved room stays zero, which is what link.exe leaves there */

    u8 *h = &f[lfanew];
    memcpy(h, "PE\0\0", 4);
    wr16(h + 4, 0x8664);
    wr16(h + 6, (u16)outs.size());
    wr32(h + 8, opt.timestamp);
    wr32(h + 12, 0);
    wr32(h + 16, 0);
    wr16(h + 20, 240);
    wr16(h + 22, (u16)(0x0022u | (opt.fixed ? 0x0001u : 0u)));

    u8 *o = h + 24;
    u32 code = 0, init = 0, uninit = 0, base_of_code = 0;
    for (size_t i = 0; i < outs.size(); i++) {
        u32 sz = align_up(outs[i].virt_size, opt.file_align);
        if (outs[i].flags & SCN_CNT_CODE) { code += sz; if (!base_of_code) base_of_code = outs[i].rva; }
        else if (outs[i].flags & SCN_CNT_INITDATA) init += sz;
        else if (outs[i].flags & SCN_CNT_UNINIT) uninit += sz;
    }
    wr16(o, 0x020B);
    o[2] = 14; o[3] = 44;                     /* the linker version the reference images name */
    wr32(o + 4, code);
    wr32(o + 8, init);
    wr32(o + 12, uninit);
    wr32(o + 16, entry_rva);
    wr32(o + 20, base_of_code);
    wr64(o + 24, opt.image_base);
    wr32(o + 32, opt.section_align);
    wr32(o + 36, opt.file_align);
    wr16(o + 40, 6); wr16(o + 42, 0);         /* OS version 6.00 */
    wr16(o + 44, 0); wr16(o + 46, 0);         /* image version 0.00 */
    wr16(o + 48, 6); wr16(o + 50, 0);         /* subsystem version 6.00 */
    wr32(o + 52, 0);
    wr32(o + 56, size_of_image);
    wr32(o + 60, size_of_headers);
    wr32(o + 64, 0);                          /* checksum: link.exe leaves it zero for an exe */
    wr16(o + 68, (u16)opt.subsystem);
    wr16(o + 70, (u16)(opt.fixed ? 0x8120 : 0x8160));
    wr64(o + 72, 0x100000); wr64(o + 80, 0x1000);
    wr64(o + 88, 0x100000); wr64(o + 96, 0x1000);
    wr32(o + 104, 0);
    wr32(o + 108, 16);

    u8 *d = o + 112;
    memset(d, 0, 16 * 8);
    u32 rva, size, r2, s2;
    group(*this, ".idata$2", rva, size);
    group(*this, ".idata$3", r2, s2);
    if (size) { wr32(d + 1 * 8, rva); wr32(d + 1 * 8 + 4, size + s2); }
    int pi = out_index(".pdata");
    if (pi >= 0) { wr32(d + 3 * 8, outs[pi].rva); wr32(d + 3 * 8 + 4, outs[pi].virt_size); }
    int ri = out_index(".reloc");
    if (ri >= 0 && outs[ri].virt_size) { wr32(d + 5 * 8, outs[ri].rva); wr32(d + 5 * 8 + 4, outs[ri].virt_size); }
    wr32(d + 6 * 8, mods.back().secs[0].rva); wr32(d + 6 * 8 + 4, 28);
    group(*this, ".idata$5", rva, size);
    if (size) { wr32(d + 12 * 8, rva); wr32(d + 12 * 8 + 4, size); }

    u8 *sh = d + 16 * 8;
    for (size_t i = 0; i < outs.size(); i++, sh += 40) {
        memset(sh, 0, 40);
        memcpy(sh, outs[i].name.data(), outs[i].name.size() < 8 ? outs[i].name.size() : 8);
        wr32(sh + 8, outs[i].virt_size);
        wr32(sh + 12, outs[i].rva);
        wr32(sh + 16, outs[i].raw_size);
        wr32(sh + 20, outs[i].raw_size ? outs[i].fileoff : 0);
        wr32(sh + 36, outs[i].flags);
    }

    /* the sections themselves */
    u32 end = size_of_headers;
    for (size_t i = 0; i < outs.size(); i++) end += outs[i].raw_size;
    f.resize(end, 0);
    for (size_t i = 0; i < all.size(); i++) {
        Contrib *c = all[i];
        if (c->data.empty()) continue;
        if (c->fileoff + c->data.size() > f.size()) { err = "a contribution lands past the end of the file"; return false; }
        memcpy(&f[c->fileoff], &c->data[0], c->data.size());
    }
    if (ri >= 0 && !reloc_data.empty())
        memcpy(&f[outs[ri].fileoff], &reloc_data[0], reloc_data.size());

    FILE *out = fopen(opt.out.c_str(), "wb");
    if (!out) { err = opt.out + ": cannot create"; return false; }
    if (fwrite(&f[0], 1, f.size(), out) != f.size()) { fclose(out); err = opt.out + ": short write"; return false; }
    fclose(out);
    return true;
}
