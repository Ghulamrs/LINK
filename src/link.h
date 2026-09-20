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

/* COMDAT selection, from the section symbol's aux record */
enum {
    COMDAT_NONE = 0, COMDAT_NODUPLICATES = 1, COMDAT_ANY = 2, COMDAT_SAME_SIZE = 3,
    COMDAT_EXACT_MATCH = 4, COMDAT_ASSOCIATIVE = 5, COMDAT_LARGEST = 6
};

/* a weak external's characteristics: whether the libraries are searched for its name */
enum { WEAK_NOLIBRARY = 1, WEAK_LIBRARY = 2, WEAK_ALIAS = 3 };

struct Symbol {
    std::string name;
    u32  value;
    int  section;   /* 1-based COFF section number; 0 undefined, -1 absolute, -2 debug,
                       -3 the image base (the linker's own __ImageBase) */
    u16  type;      /* COFF type: 0x20 marks a function, which the map file shows as `f` */
    u8   storage;
    u8   naux;
    int  aux_tag;   /* weak external: the symbol index to fall back on, else -1 */
    u32  weak_kind; /* weak external: WEAK_* */
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
    bool dropped;             /* .debug$*, .drectve, LNK_REMOVE, a COMDAT that lost - read, then left behind */
    bool live;                /* reached from the entry, a non-COMDAT section or an /include: what /OPT:REF keeps */
    u8   select;              /* COMDAT_*: how a second definition of its symbol is settled */
    int  assoc;               /* COMDAT_ASSOCIATIVE: the 1-based section this one follows */
    u32  checksum;            /* the aux record's, for EXACT_MATCH */
    int  comdat_sym;          /* the COMDAT symbol's index, -1 when the section has none */
    int  out;                 /* output section index, -1 until placed */
    u32  rva;
    u32  fileoff;             /* 0 for a contribution with no bytes in the file */
    /*  Every field settled here rather than at each of the four places a contribution is
     *  made: the linker's own records were left with an unset `select` and their module with
     *  an unset `lib`, and the placement order read that. */
    Contrib() : flags(0), size(0), module(-1), serial(0), dropped(false), live(false), select(COMDAT_NONE),
                assoc(0), checksum(0), comdat_sym(-1), out(-1), rva(0), fileoff(0) {}
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
    int  lib;                    /* the input the member came from, in search order; -1 for an object */
    Module() : compid(0), from_archive(false), lib(-1) {}
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
    std::string map;                     /* /map[:file]: link.exe's map, for holding a link against the oracle's */
    std::string entry;
    std::vector<std::string> inputs;     /* objects and archives, in command-line order */
    std::vector<std::string> libpath;    /* /libpath: then LIB, the directories a bare .lib is looked for in */
    std::vector<std::string> defaultlibs;    /* /defaultlib: from the command line */
    std::vector<std::string> nodefaultlibs;  /* /nodefaultlib:name - those names, ignored wherever met */
    std::vector<std::string> includes;       /* /include:name - a reference the command line makes */
    bool optref;                         /* /opt:ref - unreferenced COMDATs left out, and their references never searched for;
                                            /opt:noref, or /debug without /opt:ref, keeps everything */
    bool optref_said;                    /* /opt:ref or /opt:noref was spelled, so /debug does not decide it */
    bool nodefaultlib;
    bool fixed;                          /* /fixed: no .reloc, no dynamic base */
    bool dynamicbase;
    u32  timestamp;                      /* /timestamp:hex, so the bed can pin it */
    bool have_timestamp;
    int  subsystem;
    u64  image_base;
    u32  section_align;
    u32  file_align;
    u64  stack_reserve, stack_commit;    /* /stack:reserve[,commit] */
    u64  heap_reserve, heap_commit;      /* /heap:reserve[,commit] */
    bool debug;                          /* /debug: accepted, and said to be ignored */
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
    /*  Every symbol - external or static - defined in a section that survived, by name. A
     *  reloc to a static whose own COMDAT section lost (an exception funclet's $catch$N,
     *  $unwind$, $pdata$) reaches the winning copy through this: the associated statics
     *  travel with the parent COMDAT, so the same name is defined in the member that won. */
    std::map<std::string, std::pair<int, int> > kept;
    std::vector<std::pair<u32, u16> > base_relocs;   /* RVA and type: DIR64 for an ADDR64, HIGHLOW for an ADDR32 */
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
    bool write_map();
    bool sym_rva(int mod, int sym, u64 &rva);
    void sort_pdata();
    int  out_index(const std::string &name) const;
};

/* layout.cpp */
u32 rich_count(const Link &lk, std::vector<std::pair<u32, u32> > &ents);
u32 rich_lfanew(const Link &lk);
std::string find_library(const Options &o, const std::string &name);

/* coff.cpp */
/*  A name out of an input file, fit to put in a message: a broken object's section name is
 *  whatever bytes were there, and printing them raw turns a diagnostic into line noise
 *  (the review's L20 - a 65,535-section object was refused as "3\xef\xbf\xbd runs past the file"). */
std::string printable(const std::string &s);
bool coff_read(const u8 *p, size_t n, const std::string &name, Module &m, std::string &err);
bool coff_is_object(const u8 *p, size_t n);

/* archive.cpp */
struct Archive {
    std::string name;
    std::vector<u8> bytes;
    std::vector<std::pair<std::string, u32> > index;   /* symbol -> member offset */
    std::vector<u32> taken;                            /* member offsets already pulled */
    size_t longnames_at;                               /* the `//` member's data, 0 when there is none */
    Archive() : longnames_at(0) {}
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
