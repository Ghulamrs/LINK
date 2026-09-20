/*  archive.cpp - reading a .lib, and expanding the short members an import library is made of.
 *
 *  Two kinds of member arrive here. An ordinary COFF object goes straight to coff_read: in an
 *  import library those are the three descriptor members, and p07 showed an archive of two
 *  plain objects where only the member that resolves something is pulled. The other kind is
 *  the short import member, twenty bytes of header and two strings, which is not an object at
 *  all; this file turns it into the contributions link.exe would have taken from a long one -
 *  the ILT word, the IAT word, the hint/name blob, and the six-byte jump thunk that lets a
 *  `call` in the object reach the imported routine.
 */
#include "link.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

static bool slurp(const std::string &path, std::vector<u8> &out, std::string &err)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { err = path + ": cannot open"; return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize((size_t)(n > 0 ? n : 0));
    if (n > 0 && fread(&out[0], 1, (size_t)n, f) != (size_t)n) { fclose(f); err = path + ": short read"; return false; }
    fclose(f);
    return true;
}

static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

bool Archive::load(const std::string &path, std::string &err)
{
    name = path;
    if (!slurp(path, bytes, err)) return false;
    if (bytes.size() < 8 || memcmp(&bytes[0], "!<arch>\n", 8) != 0) { err = path + ": not an archive"; return false; }

    /* the first linker member: a big-endian count, its offsets, then the names */
    size_t p = 8;
    if (p + 60 > bytes.size()) { err = path + ": no members"; return false; }
    const u8 *h = &bytes[p];
    char szbuf[11]; memcpy(szbuf, h + 48, 10); szbuf[10] = 0;
    u32 msize = (u32)strtoul(szbuf, 0, 10);
    if (h[0] != '/' || h[1] != ' ') { err = path + ": no symbol index"; return false; }
    const u8 *d = &bytes[p + 60];
    if (p + 60 + msize > bytes.size() || msize < 4) { err = path + ": broken symbol index"; return false; }
    u32 nsym = be32(d);
    const u8 *offs = d + 4;
    const char *names = (const char *)(offs + 4 * nsym);
    const char *end = (const char *)(d + msize);
    for (u32 i = 0; i < nsym && names < end; i++) {
        std::string s(names);
        index.push_back(std::make_pair(s, be32(offs + 4 * i)));
        names += s.size() + 1;
    }
    /* the longnames member, `//`, when there is one: after the second linker member */
    longnames_at = 0;
    size_t q = p + 60 + msize; if (q & 1) q++;
    for (int k = 0; k < 2 && q + 60 <= bytes.size(); k++) {
        const u8 *mh = &bytes[q];
        char sz[11]; memcpy(sz, mh + 48, 10); sz[10] = 0;
        u32 ms = (u32)strtoul(sz, 0, 10);
        if (mh[0] == '/' && mh[1] == '/') { longnames_at = q + 60; break; }
        q += 60 + ms; if (q & 1) q++;
    }
    return true;
}

/*  Build the module a short import member stands for. The names of the sections are the ones
 *  the import library would have used, because those names are what puts each word where
 *  link.exe puts it: $4 the ILT, $5 the IAT, $6 the hint and the name. */
static void short_import(const u8 *d, u32 dsize, const std::string &archname, Module &m)
{
    u16 hint = rd16(d + 16);
    u16 flags = rd16(d + 18);
    int type = flags & 3;
    const char *sym = (const char *)d + 20;
    const char *dll = sym + strlen(sym) + 1;
    if ((u32)(dll - (const char *)d) >= dsize) dll = "";

    m.name = archname + "(" + dll + ")";     /* the DLL, as link.exe's map names an import: kernel32:KERNEL32.dll */
    m.compid = 0x00010000u;          /* a short member carries no @comp.id: unmarked */
    m.from_archive = true;
    m.lib = -1;                      /* the caller says which library */

    bool code = (type == 0);
    m.secs.resize(code ? 4 : 3);

    /* $6: the hint, the name, a NUL, and a NUL of padding when that comes out odd */
    Contrib &hn = m.secs[2];
    hn.name = ".idata$6";
    hn.flags = SCN_CNT_INITDATA | SCN_MEM_READ | 0x00200000u;   /* 2-byte align */
    size_t n = 2 + strlen(sym) + 1; if (n & 1) n++;
    hn.data.assign(n, 0);
    wr16(&hn.data[0], hint);
    memcpy(&hn.data[2], sym, strlen(sym));
    hn.size = (u32)n;

    /* $4 and $5: one eight-byte word each, both pointing at the hint/name blob until the
       loader overwrites the IAT one with the address it found */
    for (int i = 0; i < 2; i++) {
        Contrib &c = m.secs[i];
        c.name  = (i == 0) ? ".idata$4" : ".idata$5";
        c.flags = SCN_CNT_INITDATA | SCN_MEM_READ | SCN_MEM_WRITE | 0x00400000u;  /* 8-byte align */
        c.data.assign(8, 0);
        c.size = 8;
        c.dropped = false; c.module = -1; c.serial = 0; c.out = -1; c.rva = 0; c.fileoff = 0;
        c.select = COMDAT_NONE; c.assoc = 0; c.checksum = 0; c.comdat_sym = -1;
        Reloc r; r.offset = 0; r.sym = 2; r.type = REL_ADDR32NB;   /* syms[2] labels $6 */
        c.relocs.push_back(r);
    }
    hn.dropped = false; hn.module = -1; hn.serial = 0; hn.out = -1; hn.rva = 0; hn.fileoff = 0;
    hn.select = COMDAT_NONE; hn.assoc = 0; hn.checksum = 0; hn.comdat_sym = -1;

    /*  An import is in the image only while something reaches it: link.exe's hello has 76
     *  IAT words where the members pulled would give 92, the sixteen being names referred to
     *  from COMDATs that were left out. So the member's pieces are marked as a COMDAT group
     *  is - the IAT word (and the thunk) stand or fall by reference, and the ILT word and
     *  the hint/name blob go with the IAT word. Nothing here is settled against another
     *  definition: the flag alone is what the liveness sweep reads. */
    for (size_t i = 0; i < m.secs.size(); i++) m.secs[i].flags |= SCN_LNK_COMDAT;
    m.secs[1].select = COMDAT_ANY;
    m.secs[0].select = COMDAT_ASSOCIATIVE; m.secs[0].assoc = 2;
    m.secs[2].select = COMDAT_ASSOCIATIVE; m.secs[2].assoc = 2;

    /*  The thunk. Six bytes of `jmp qword ptr [rip+d]` reaching the IAT word, which is what
     *  the bed found link.exe appending to .text for every imported routine a call names. */
    if (code) {
        Contrib &t = m.secs[3];
        t.name  = ".text$mn";
        t.flags = SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ | 0x00100000u | SCN_LNK_COMDAT;  /* 1-byte align */
        t.data.assign(6, 0);
        t.data[0] = 0xFF; t.data[1] = 0x25;
        t.size = 6;
        t.dropped = false; t.module = -1; t.serial = 0; t.out = -1; t.rva = 0; t.fileoff = 0;
        t.select = COMDAT_ANY; t.assoc = 0; t.checksum = 0; t.comdat_sym = -1;   /* by reference, as above */
        Reloc r; r.offset = 2; r.sym = 0; r.type = REL_REL32;      /* syms[0] is __imp_<sym> */
        t.relocs.push_back(r);
    }

    Symbol s;
    s.value = 0; s.type = 0; s.storage = SYM_EXTERNAL; s.naux = 0; s.aux_tag = -1; s.weak_kind = 0;
    s.name = std::string("__imp_") + sym; s.section = 2;   /* the $5 word */
    m.syms.push_back(s);
    s.name = std::string("__IMPORT_DESCRIPTOR_") + std::string(dll, strcspn(dll, "."));
    s.section = 0;                                          /* undefined: it pulls the descriptor */
    m.syms.push_back(s);
    s.name = ".idata$6"; s.section = 3; s.storage = SYM_STATIC;
    m.syms.push_back(s);
    if (code) {
        s.name = sym; s.section = 4; s.storage = SYM_EXTERNAL;
        m.syms.push_back(s);
    }
}

bool Archive::member(u32 off, const std::string &archname, Module &m, std::string &err) const
{
    if (off + 60 > bytes.size()) { err = archname + ": member past the end"; return false; }
    const u8 *h = &bytes[off];
    char szbuf[11]; memcpy(szbuf, h + 48, 10); szbuf[10] = 0;
    u32 msize = (u32)strtoul(szbuf, 0, 10);
    const u8 *d = h + 60;
    if (off + 60 + msize > bytes.size()) { err = archname + ": member runs past the end"; return false; }

    if (msize >= 20 && rd16(d) == 0 && rd16(d + 2) == 0xFFFF) {
        short_import(d, msize, archname, m);
        return true;
    }
    /* an ordinary object: its member name is only for diagnostics. A name of the form /nnn
       is an offset into the longnames member `//`, which the CRT libraries use throughout. */
    char nm[17]; memcpy(nm, h, 16); nm[16] = 0;
    std::string leaf;
    if (nm[0] == '/' && nm[1] >= '0' && nm[1] <= '9') {
        u32 at = (u32)strtoul(nm + 1, 0, 10);
        if (longnames_at && longnames_at + at < bytes.size()) {
            const char *s = (const char *)&bytes[longnames_at + at];
            size_t n = 0;
            while (longnames_at + at + n < bytes.size() && s[n] && s[n] != '/' && s[n] != '\n') n++;
            leaf.assign(s, n);
        }
    }
    if (leaf.empty()) {
        char *sl = strchr(nm, '/'); if (sl) *sl = 0;
        leaf = nm;
    }
    std::string mname = archname + "(" + leaf + ")";
    if (!coff_read(d, msize, mname, m, err)) return false;
    m.from_archive = true;
    return true;
}
