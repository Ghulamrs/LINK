/*  link.h - what the parts of this linker say to each other.
 *
 *  The shape follows what the probe bed showed link.exe doing, not what the PE specification
 *  says it may do: an input section is a *contribution*, contributions merge into output
 *  sections by their name up to and including the `$` suffix, and the linker's own records -
 *  the debug directory, the volatile-metadata word and the coffgrp map - arrive as three more
 *  contributions from a module of the linker's own making. docs/pe-observed.md holds the
 *  numbers this file is built on.
 */
#ifndef LINK_H
#define LINK_H

#include <map>
#include <string>
#include <utility>
#include <vector>

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

/* COFF section characteristics - the few a linker has to read */
enum {
    SCN_CNT_CODE     = 0x00000020u,
    SCN_CNT_INITDATA = 0x00000040u,
    SCN_CNT_UNINIT   = 0x00000080u,
    SCN_LNK_INFO     = 0x00000200u,
    SCN_LNK_REMOVE   = 0x00000800u,
    SCN_LNK_COMDAT   = 0x00001000u,
    SCN_ALIGN_MASK   = 0x00F00000u,
    SCN_MEM_DISCARD  = 0x02000000u,
    SCN_MEM_EXECUTE  = 0x20000000u,
    SCN_MEM_READ     = 0x40000000u,
    SCN_MEM_WRITE    = 0x80000000u
};

/* x86-64 COFF relocation types. REL32_n is a RIP-relative displacement with n bytes of
   immediate after it, which is why the addend differs from REL32 by exactly n. */
enum {
    REL_ABSOLUTE = 0x00, REL_ADDR64 = 0x01, REL_ADDR32 = 0x02, REL_ADDR32NB = 0x03,
    REL_REL32    = 0x04, REL_REL32_1 = 0x05, REL_REL32_2 = 0x06, REL_REL32_3 = 0x07,
    REL_REL32_4  = 0x08, REL_REL32_5 = 0x09, REL_SECTION = 0x0A, REL_SECREL  = 0x0B
};

/* COFF symbol storage classes, again only the ones that decide anything here */
enum { SYM_EXTERNAL = 2, SYM_STATIC = 3, SYM_SECTION = 104, SYM_WEAK_EXTERNAL = 105 };

struct Reloc {
    u32 offset;     /* into the contribution */
    u32 sym;        /* index into the module's symbol table */
    u16 type;
};

struct Symbol {
    std::string name;
    u32  value;
    int  section;   /* 1-based COFF section number; 0 undefined, -1 absolute, -2 debug */
    u8   storage;
    u8   naux;
    int  aux_tag;   /* weak external: the symbol index to fall back on, else -1 */
};

/*  One input section. `name` keeps its `$` suffix: the suffix is what orders the
 *  contributions inside an output section, and the part before it is what picks the
 *  output section. */
struct Contrib {
    std::string name;
    u32  flags;
    u32  size;                /* virtual size: bytes whether or not they are in the file */
    std::vector<u8> data;     /* empty when the section is uninitialised */
    std::vector<Reloc> relocs;
    int  module;              /* index into Link::mods */
    int  serial;              /* the order this contribution was read in: the tie-break */
    bool dropped;             /* .debug$*, .drectve, LNK_REMOVE - read, then left behind */
    int  out;                 /* output section index, -1 until placed */
    u32  rva;
    u32  fileoff;             /* 0 for a contribution with no bytes in the file */
};

/*  One input file, or one member of an archive, or the linker itself. `compid` is the value
 *  of the object's @comp.id - the tool that wrote it - and is what the Rich header counts;
 *  a module with no @comp.id counts as 0x00010000, which is what link.exe calls unmarked. */
struct Module {
    std::string name;
    std::vector<Contrib> secs;   /* secs[i] is COFF section number i+1 */
    std::vector<Symbol>  syms;
    u32  compid;
    bool from_archive;
};

struct OutSection {
    std::string name;
    u32 flags;
    u32 rva;
    u32 virt_size;
    u32 raw_size;
    u32 fileoff;
    std::vector<int> parts;   /* indices into Link::all, in layout order */
};

/*  Where a resolved symbol ended up. `rva` is only good once layout has run. */
struct Resolved {
    int module;
    int sym;
};

struct Options {
    std::string out;
    std::string entry;
    std::vector<std::string> inputs;     /* objects and archives, in command-line order */
    bool nodefaultlib;
    bool fixed;                          /* /fixed: no .reloc, no dynamic base */
    bool dynamicbase;
    u32  timestamp;                      /* /timestamp:hex, so the bed can pin it */
    bool have_timestamp;
    int  subsystem;
    u64  image_base;
    u32  section_align;
    u32  file_align;
    bool verbose;
    Options();
};

/*  The whole link. One object, no globals: the README's rule, and what lets the readers run
 *  a thread to a file. */
struct Link {
    Options opt;
    std::vector<Module>   mods;
    std::vector<Contrib*> all;        /* every placed contribution, in layout order */
    std::vector<OutSection> outs;
    std::map<std::string, std::pair<int, int> > resolved;   /* name -> module, symbol */
    std::vector<u32> base_relocs;     /* the RVAs an ADDR64 left needing a DIR64 */
    std::vector<u8>  reloc_data;      /* .reloc, once those are known */
    u32 entry_rva;
    u32 size_of_image;
    u32 size_of_headers;
    u32 lfanew;
    std::string err;

    Link() : entry_rva(0), size_of_image(0), size_of_headers(0), lfanew(0) {}

    bool run();
    bool read_inputs();
    bool lay_out();
    bool address();
    bool fix_up();
    bool write_image();
    bool sym_rva(int mod, int sym, u64 &rva);
    int  out_index(const std::string &name) const;
};

/* layout.cpp */
u32 rich_count(const Link &lk, std::vector<std::pair<u32, u32> > &ents);
u32 rich_lfanew(const Link &lk);

/* coff.cpp */
bool coff_read(const u8 *p, size_t n, const std::string &name, Module &m, std::string &err);
bool coff_is_object(const u8 *p, size_t n);

/* archive.cpp */
struct Archive {
    std::string name;
    std::vector<u8> bytes;
    std::vector<std::pair<std::string, u32> > index;   /* symbol -> member offset */
    std::vector<u32> taken;                            /* member offsets already pulled */
    bool load(const std::string &path, std::string &err);
    bool member(u32 off, const std::string &archname, Module &m, std::string &err) const;
};

/* utility shared by the readers and the writer */
inline u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
inline u64 rd64(const u8 *p) { return (u64)rd32(p) | ((u64)rd32(p + 4) << 32); }
inline void wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
inline void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
inline void wr64(u8 *p, u64 v) { wr32(p, (u32)v); wr32(p + 4, (u32)(v >> 32)); }
inline u32 align_up(u32 v, u32 a) { return a ? ((v + a - 1) / a) * a : v; }

/* the alignment a section's characteristics ask for: 0 means the COFF default of 16 */
inline u32 scn_align(u32 flags) {
    u32 f = (flags & SCN_ALIGN_MASK) >> 20;
    return f ? (1u << (f - 1)) : 16u;
}

#endif
