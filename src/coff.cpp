/*  coff.cpp - reading one MS COFF object.
 *
 *  Nothing here interprets: it turns the file into contributions, relocations and symbols and
 *  leaves every decision to layout.cpp. The sections this linker will not carry into an image
 *  are marked `dropped` rather than discarded, so that the Rich header and the diagnostics can
 *  still say what the object contained.
 */
#include "link.h"
#include <cstring>
#include <cstdio>

bool coff_is_object(const u8 *p, size_t n)
{
    if (n < 20) return false;
    if (rd16(p) == 0x8664 && rd16(p + 2) != 0xFFFF) return true;   /* ordinary object */
    return false;
}

/* a name field is eight bytes: the name itself, or /nnn into the string table */
static std::string scn_name(const u8 *f, const u8 *strtab, u32 strsize)
{
    if (f[0] == '/') {
        char buf[9];
        memcpy(buf, f + 1, 7); buf[7] = 0;
        u32 off = (u32)strtoul(buf, 0, 10);
        if (strtab && off < strsize) return std::string((const char *)strtab + off);
        return std::string();
    }
    size_t n = 0;
    while (n < 8 && f[n]) n++;
    return std::string((const char *)f, n);
}

static std::string sym_name(const u8 *f, const u8 *strtab, u32 strsize)
{
    if (rd32(f) == 0) {
        u32 off = rd32(f + 4);
        if (strtab && off < strsize) return std::string((const char *)strtab + off);
        return std::string();
    }
    size_t n = 0;
    while (n < 8 && f[n]) n++;
    return std::string((const char *)f, n);
}

/*  A section this linker will not place. `.debug$*` goes because no /debug was asked for,
 *  `.drectve` because it is a message to the linker rather than an image, and LNK_REMOVE
 *  because the assembler said so. */
static bool is_dropped(const std::string &name, u32 flags)
{
    if (flags & (SCN_LNK_INFO | SCN_LNK_REMOVE)) return true;
    if (name.compare(0, 7, ".debug$") == 0) return true;
    return false;
}

bool coff_read(const u8 *p, size_t n, const std::string &name, Module &m, std::string &err)
{
    if (n < 20) { err = name + ": too short for a COFF header"; return false; }
    u16 machine = rd16(p);
    if (machine != 0x8664) {
        char buf[64]; sprintf(buf, ": machine 0x%04x, not x86-64", machine);
        err = name + buf; return false;
    }
    u16 nsec   = rd16(p + 2);
    u32 symoff = rd32(p + 8);
    u32 nsym   = rd32(p + 12);
    u16 optsz  = rd16(p + 16);

    const u8 *strtab = 0; u32 strsize = 0;
    if (symoff && nsym) {
        u32 stroff = symoff + nsym * 18;
        if (stroff + 4 <= n) { strtab = p + stroff; strsize = rd32(p + stroff); }
        if (strsize > n - stroff) strsize = (u32)(n - stroff);
    }

    m.name = name;
    m.compid = 0x00010000u;          /* unmarked until an @comp.id says otherwise */
    m.secs.resize(nsec);

    const u8 *sh = p + 20 + optsz;
    for (u16 i = 0; i < nsec; i++, sh += 40) {
        if ((size_t)(sh + 40 - p) > n) { err = name + ": section table runs past the file"; return false; }
        Contrib &c = m.secs[i];
        c.name    = scn_name(sh, strtab, strsize);
        c.size    = rd32(sh + 16);
        c.flags   = rd32(sh + 36);
        c.module  = -1;
        c.serial  = 0;
        c.out     = -1;
        c.rva     = 0;
        c.fileoff = 0;
        c.dropped = is_dropped(c.name, c.flags);

        u32 rawptr = rd32(sh + 20);
        if (!(c.flags & SCN_CNT_UNINIT) && rawptr && c.size) {
            if (rawptr + c.size > n) { err = name + ": " + c.name + " runs past the file"; return false; }
            c.data.assign(p + rawptr, p + rawptr + c.size);
        }
        u32 relptr = rd32(sh + 24);
        u32 nrel   = rd16(sh + 32);
        /* 0xFFFF relocations is the flag for "the real count is in the first entry" */
        if (nrel == 0xFFFF && relptr + 10 <= n) { nrel = rd32(p + relptr) - 1; relptr += 10; }
        for (u32 r = 0; r < nrel; r++) {
            const u8 *rp = p + relptr + r * 10;
            if ((size_t)(rp + 10 - p) > n) { err = name + ": relocations run past the file"; return false; }
            Reloc rel;
            rel.offset = rd32(rp);
            rel.sym    = rd32(rp + 4);
            rel.type   = rd16(rp + 8);
            c.relocs.push_back(rel);
        }
    }

    m.syms.resize(nsym);
    for (u32 i = 0; i < nsym; ) {
        const u8 *sp = p + symoff + i * 18;
        if ((size_t)(sp + 18 - p) > n) { err = name + ": symbol table runs past the file"; return false; }
        Symbol &s = m.syms[i];
        s.name    = sym_name(sp, strtab, strsize);
        s.value   = rd32(sp + 8);
        s.section = (short)rd16(sp + 12);
        s.storage = sp[16];
        s.naux    = sp[17];
        s.aux_tag = -1;
        if (s.storage == SYM_WEAK_EXTERNAL && s.naux && (size_t)(sp + 36 - p) <= n)
            s.aux_tag = (int)rd32(sp + 18);
        if (s.name == "@comp.id" && s.section == -1) m.compid = s.value;
        for (u8 a = 1; a <= s.naux && i + a < nsym; a++) {
            Symbol &x = m.syms[i + a];
            x.name = std::string(); x.value = 0; x.section = 0;
            x.storage = 0; x.naux = 0; x.aux_tag = -1;
        }
        i += 1u + s.naux;
    }
    return true;
}
