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
 *  `.drectve` because it is a message to the linker rather than an image (it is read first,
 *  in layout.cpp), LNK_REMOVE because the assembler said so - and the CRT's bookkeeping
 *  sections because link.exe consumes them and writes none of them into an image: the
 *  control-flow-guard tables (.gfids$y, .gehcont$y, .gsspr$y, .gssep$x, .00cfg is kept),
 *  the volatile-metadata table (.voltbl, the linker writes its own record), the checksum
 *  section (.chks64), the retpoline table (.retplne) and the incremental-link maps
 *  (*_$fo_bdd$, *_$fo_rvas$). The review's corpus maps are what says so (L14). */
static bool is_dropped(const std::string &name, u32 flags)
{
    if (flags & (SCN_LNK_INFO | SCN_LNK_REMOVE)) return true;
    if (name.compare(0, 7, ".debug$") == 0) return true;
    static const char *const consumed[] = {
        ".gfids", ".gehcont", ".gsspr", ".gssep", ".voltbl", ".chks64", ".retplne", 0
    };
    std::string base = name.substr(0, name.find('$'));
    for (int i = 0; consumed[i]; i++) if (base == consumed[i]) return true;
    if (name.size() > 8 && (name.find("_$fo_bdd$") != std::string::npos ||
                            name.find("_$fo_rvas$") != std::string::npos)) return true;
    return false;
}

bool coff_read(const u8 *p, size_t n, const std::string &name, Module &m, std::string &err)
{
    if (n < 20) { err = name + ": too short for a COFF header"; return false; }
    u16 machine = rd16(p);
    /* machine 0 is "any machine": the CRT's alias members carry it (one section, one weak
       external each), and link.exe takes them */
    if (machine != 0x8664 && machine != 0) {
        char buf[64]; snprintf(buf, sizeof buf, ": machine 0x%04x, not x86-64", machine);
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
    m.lib = -1;
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
        c.select = COMDAT_NONE; c.assoc = 0; c.checksum = 0; c.comdat_sym = -1;

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
        s.weak_kind = 0;
        if (s.storage == SYM_WEAK_EXTERNAL && s.naux && (size_t)(sp + 36 - p) <= n) {
            s.aux_tag = (int)rd32(sp + 18);
            s.weak_kind = rd32(sp + 22);
        }
        /*  The section symbol's aux record: length, relocation and line counts, the checksum,
         *  the associated section number and the COMDAT selection. Only a section marked
         *  LNK_COMDAT means any of it. */
        if (s.storage == SYM_STATIC && s.naux && s.value == 0 && s.section > 0 &&
            (size_t)s.section <= m.secs.size() && (size_t)(sp + 36 - p) <= n) {
            Contrib &c = m.secs[s.section - 1];
            if ((c.flags & SCN_LNK_COMDAT) && c.select == COMDAT_NONE) {
                c.checksum = rd32(sp + 26);
                c.assoc    = rd16(sp + 30);
                c.select   = sp[32];
            }
        }
        if (s.name == "@comp.id" && s.section == -1) m.compid = s.value;
        for (u8 a = 1; a <= s.naux && i + a < nsym; a++) {
            Symbol &x = m.syms[i + a];
            x.name = std::string(); x.value = 0; x.section = 0;
            x.storage = 0; x.naux = 0; x.aux_tag = -1;
        }
        i += 1u + s.naux;
    }
    /*  The COMDAT symbol: the first external symbol defined in the section. It is the name
     *  a second definition is settled by. A COMDAT whose symbol is static - the CRT's
     *  file-local helpers, cl's pooled string literals - belongs to its object alone, and
     *  is settled against nothing: libucrt's strcspn.obj and strpbrk.obj each carry a
     *  static fallbackMethod in a NODUPLICATES section, and link.exe keeps both. */
    for (u32 i = 0; i < nsym; i++) {
        const Symbol &s = m.syms[i];
        if (s.section <= 0 || (size_t)s.section > m.secs.size() || s.name.empty()) continue;
        if (s.storage != SYM_EXTERNAL) continue;
        Contrib &c = m.secs[s.section - 1];
        if (!(c.flags & SCN_LNK_COMDAT) || c.comdat_sym >= 0) continue;
        c.comdat_sym = (int)i;
    }
    return true;
}
