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

    m.name = archname + "(" + sym + ")";
    m.compid = 0x00010000u;          /* a short member carries no @comp.id: unmarked */
    m.from_archive = true;

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
        Reloc r; r.offset = 0; r.sym = 2; r.type = REL_ADDR32NB;   /* syms[2] labels $6 */
        c.relocs.push_back(r);
    }

    /*  The thunk. Six bytes of `jmp qword ptr [rip+d]` reaching the IAT word, which is what
     *  the bed found link.exe appending to .text for every imported routine a call names. */
    if (code) {
        Contrib &t = m.secs[3];
        t.name  = ".text$mn";
        t.flags = SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ | 0x00100000u;  /* 1-byte align */
        t.data.assign(6, 0);
        t.data[0] = 0xFF; t.data[1] = 0x25;
        t.size = 6;
        Reloc r; r.offset = 2; r.sym = 0; r.type = REL_REL32;      /* syms[0] is __imp_<sym> */
        t.relocs.push_back(r);
    }

    Symbol s;
    s.value = 0; s.storage = SYM_EXTERNAL; s.naux = 0; s.aux_tag = -1;
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
    /* an ordinary object: its member name is only for diagnostics */
    char nm[17]; memcpy(nm, h, 16); nm[16] = 0;
    char *sl = strchr(nm, '/'); if (sl) *sl = 0;
    std::string mname = archname + "(" + nm + ")";
    if (!coff_read(d, msize, mname, m, err)) return false;
    m.from_archive = true;
    return true;
}
