/*  layout.cpp - reading the inputs, deciding what is in the image, and where.
 *
 *  The order of a link, in the order this file does it:
 *
 *    read      every object named on the command line, a thread to a file
 *    resolve   the undefined names, pulling archive members in passes - a pass takes every
 *              member the current undefined set asks for, which is the order the bed showed
 *              the hint/name blobs arriving in
 *    place     contributions into output sections: the name before `$` picks the section, and
 *              inside it a rank and then the `$` suffix pick the order. docs/pe-observed.md
 *              records why .idata$5 is lifted to the front and the import descriptors sink to
 *              the back, and why the linker's own three records sit where they do
 *    address   RVAs on the section grain, file offsets on the file grain, bss last so it
 *              costs virtual size and no bytes
 *    fix up    every relocation, collecting the ADDR64s that .reloc will have to carry
 */
#include "link.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <thread>

Options::Options()
    : entry(), nodefaultlib(false), fixed(false), dynamicbase(true),
      timestamp(0), have_timestamp(false), subsystem(3),
      image_base(0x140000000ull), section_align(0x1000), file_align(0x200), verbose(false) {}

/* ---------------------------------------------------------------- reading */

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

namespace {

struct Input {
    std::string path;
    bool is_archive;
    std::vector<u8> bytes;
    Module mod;
    Archive arch;
    std::string err;
    bool ok;
};

/* one of these per input file: the thread body */
void read_one(Input *in)
{
    in->ok = false;
    if (!slurp(in->path, in->bytes, in->err)) return;
    if (in->bytes.size() >= 8 && memcmp(&in->bytes[0], "!<arch>\n", 8) == 0) {
        in->is_archive = true;
        in->ok = in->arch.load(in->path, in->err);
        return;
    }
    in->is_archive = false;
    in->ok = coff_read(&in->bytes[0], in->bytes.size(), in->path, in->mod, in->err);
}

} /* namespace */

/* ------------------------------------------------------- symbols and pulls */

namespace {

struct World {
    Link *lk;
    std::map<std::string, std::pair<int, int> > defined;   /* name -> module, symbol */
    std::vector<std::string> undef;                        /* still wanted, in first-asked order */
    std::map<std::string, bool> asked;
};

void take_module(World &w, const Module &m)
{
    int mi = (int)w.lk->mods.size();
    w.lk->mods.push_back(m);
    const Module &mm = w.lk->mods[mi];
    for (size_t i = 0; i < mm.syms.size(); i++) {
        const Symbol &s = mm.syms[i];
        if (s.name.empty()) continue;
        if (s.storage != SYM_EXTERNAL && s.storage != SYM_WEAK_EXTERNAL) continue;
        if (s.section != 0) {
            if (w.defined.find(s.name) == w.defined.end())
                w.defined[s.name] = std::make_pair(mi, (int)i);
        } else if (s.storage == SYM_EXTERNAL && s.value == 0) {
            if (!w.asked[s.name]) { w.asked[s.name] = true; w.undef.push_back(s.name); }
        }
    }
}

} /* namespace */

bool Link::read_inputs()
{
    std::vector<Input> ins(opt.inputs.size());
    for (size_t i = 0; i < opt.inputs.size(); i++) { ins[i].path = opt.inputs[i]; ins[i].ok = false; }

    std::vector<std::thread> th;
    th.reserve(ins.size());
    for (size_t i = 0; i < ins.size(); i++) th.push_back(std::thread(read_one, &ins[i]));
    for (size_t i = 0; i < th.size(); i++) th[i].join();

    for (size_t i = 0; i < ins.size(); i++)
        if (!ins[i].ok) { err = ins[i].err; return false; }

    World w; w.lk = this;

    /* the objects first, in command-line order: that order is the layout order */
    for (size_t i = 0; i < ins.size(); i++)
        if (!ins[i].is_archive) take_module(w, ins[i].mod);

    /*  then the archives, in passes. One pass takes every member the current undefined set
     *  names; the members it takes may ask for more, which the next pass answers. */
    bool progress = true;
    while (progress) {
        progress = false;
        for (size_t a = 0; a < ins.size(); a++) {
            if (!ins[a].is_archive) continue;
            Archive &ar = ins[a].arch;
            std::vector<u32> pull;
            for (size_t u = 0; u < w.undef.size(); u++) {
                const std::string &nm = w.undef[u];
                if (w.defined.find(nm) != w.defined.end()) continue;
                for (size_t k = 0; k < ar.index.size(); k++) {
                    if (ar.index[k].first != nm) continue;
                    u32 off = ar.index[k].second;
                    if (std::find(ar.taken.begin(), ar.taken.end(), off) != ar.taken.end()) break;
                    if (std::find(pull.begin(), pull.end(), off) == pull.end()) pull.push_back(off);
                    break;
                }
            }
            for (size_t k = 0; k < pull.size(); k++) {
                Module m; m.compid = 0x00010000u; m.from_archive = true;
                if (!ar.member(pull[k], ar.name, m, err)) return false;
                ar.taken.push_back(pull[k]);
                take_module(w, m);
                progress = true;
            }
        }
    }

    for (size_t u = 0; u < w.undef.size(); u++)
        if (w.defined.find(w.undef[u]) == w.defined.end())
            { err = "unresolved external symbol: " + w.undef[u]; return false; }

    resolved.swap(w.defined);
    return true;
}

/* -------------------------------------------------------------- placement */

/*  The output section a contribution merges into, and its rank inside it. The ranks are the
 *  bed's, not the specification's: .idata$5 starts .rdata because the IAT directory has to
 *  point at exactly it, and the import descriptors follow everything else the section holds. */
static std::string out_of(const std::string &name)
{
    std::string base = name.substr(0, name.find('$'));
    if (base == ".text")   return ".text";
    if (base == ".rdata" || base == ".idata" || base == ".xdata" ||
        base == ".edata"  || base == ".CRT")   return ".rdata";
    if (base == ".data"  || base == ".bss" || base == ".tls") return ".data";
    if (base == ".pdata")  return ".pdata";
    return base;
}

static int rank_of(const std::string &out, const std::string &name)
{
    if (out == ".rdata") {
        if (name == ".idata$5") return 0;
        if (name.compare(0, 6, ".rdata") == 0) return 1;
        if (name.compare(0, 6, ".xdata") == 0) return 2;
        if (name.compare(0, 6, ".idata") == 0) return 3;
        return 4;
    }
    if (out == ".data") return name.compare(0, 4, ".bss") == 0 ? 1 : 0;
    return 0;
}

namespace {

struct Placed {
    Contrib *c;
    int out, rank;
    bool operator<(const Placed &o) const {
        if (out  != o.out)  return out < o.out;
        if (rank != o.rank) return rank < o.rank;
        if (c->name != o.c->name) return c->name < o.c->name;
        return c->serial < o.c->serial;
    }
};

u32 out_flags(const std::string &n)
{
    if (n == ".text")  return SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ;
    if (n == ".data")  return SCN_CNT_INITDATA | SCN_MEM_READ | SCN_MEM_WRITE;
    if (n == ".reloc") return SCN_CNT_INITDATA | SCN_MEM_DISCARD | SCN_MEM_READ;
    return SCN_CNT_INITDATA | SCN_MEM_READ;
}

int out_order(const std::string &n)
{
    if (n == ".text")  return 0;
    if (n == ".rdata") return 1;
    if (n == ".data")  return 2;
    if (n == ".pdata") return 3;
    if (n == ".reloc") return 5;
    return 4;
}

} /* namespace */

/*  The linker's own module: the three records the bed found link.exe writing whatever the
 *  input said - the debug directory, the volatile-metadata word, and the coffgrp map of every
 *  contribution in the image. Their bytes are filled in once the addresses are known; here
 *  only their sizes have to be right, because the sizes decide the addresses. */
static void add_linker_module(Link &lk, size_t ncontrib_names)
{
    Module m;
    m.name = "*linker*";
    m.compid = 0x0102899Cu;        /* link 14.44.35228, as the reference images record it */
    m.from_archive = false;
    m.secs.resize(3);
    for (int i = 0; i < 3; i++) {
        Contrib &c = m.secs[i];
        c.flags = SCN_CNT_INITDATA | SCN_MEM_READ | 0x00300000u;   /* 4-byte align */
        c.dropped = false; c.out = -1; c.rva = 0; c.fileoff = 0; c.module = -1; c.serial = 0;
    }
    m.secs[0].name = ".rdata";        m.secs[0].size = 28; m.secs[0].data.assign(28, 0);
    m.secs[1].name = ".rdata$voltmd"; m.secs[1].size = 24; m.secs[1].data.assign(24, 0);
    m.secs[2].name = ".rdata$zzzdbg"; m.secs[2].size = (u32)ncontrib_names;
    m.secs[2].data.assign(m.secs[2].size, 0);
    lk.mods.push_back(m);
}

bool Link::lay_out()
{
    /*  The coffgrp record has to name every run of contributions in the image, including its
     *  own - so the placement is done twice: once to learn the names, once with the record
     *  sized. Names do not depend on sizes, so the second placement is the same order. */
    add_linker_module(*this, 16);
    Contrib &grp = mods.back().secs[2];

    for (int pass = 0; pass < 2; pass++) {
        outs.clear();
        all.clear();

        std::vector<Placed> ps;
        for (size_t mi = 0; mi < mods.size(); mi++) {
            for (size_t si = 0; si < mods[mi].secs.size(); si++) {
                Contrib &c = mods[mi].secs[si];
                c.module = (int)mi;
                c.serial = (int)(mi * 4096 + si);
                c.out = -1;
                if (c.dropped || c.size == 0) continue;
                Placed p;
                p.c = &c;
                p.out = out_order(out_of(c.name));
                p.rank = rank_of(out_of(c.name), c.name);
                ps.push_back(p);
            }
        }
        std::stable_sort(ps.begin(), ps.end());

        std::map<std::string, int> seen;
        for (size_t i = 0; i < ps.size(); i++) {
            std::string on = out_of(ps[i].c->name);
            std::map<std::string, int>::iterator it = seen.find(on);
            if (it == seen.end()) {
                it = seen.insert(std::make_pair(on, (int)outs.size())).first;
                OutSection s;
                s.name = on; s.flags = out_flags(on);
                s.rva = s.virt_size = s.raw_size = s.fileoff = 0;
                outs.push_back(s);
            }
            ps[i].c->out = it->second;
            outs[it->second].parts.push_back((int)all.size());
            all.push_back(ps[i].c);
        }

        if (pass == 0) {
            size_t n = 4;
            std::string last;
            for (size_t i = 0; i < all.size(); i++) {
                if (all[i]->name == last) continue;
                last = all[i]->name;
                n += 8 + ((last.size() + 1 + 3) & ~(size_t)3);
            }
            n += 16;
            grp.size = (u32)n;
            grp.data.assign(n, 0);
        }
    }

    /*  .reloc is the linker's own too, but it is placed before its bytes exist: whether the
     *  image has one is a question the relocations answer, and only its length waits. */
    bool need_reloc = false;
    for (size_t i = 0; i < all.size() && !need_reloc; i++)
        for (size_t r = 0; r < all[i]->relocs.size(); r++)
            if (all[i]->relocs[r].type == REL_ADDR64 || all[i]->relocs[r].type == REL_ADDR32)
                { need_reloc = true; break; }
    if (need_reloc && !opt.fixed) {
        OutSection s;
        s.name = ".reloc"; s.flags = out_flags(".reloc");
        s.rva = s.virt_size = s.raw_size = s.fileoff = 0;
        outs.push_back(s);
    }

    lfanew = rich_lfanew(*this);
    return true;
}

/*  The Rich header, and the one number it decides.
 *
 *  Every module carries a @comp.id saying which tool wrote it, or counts as unmarked when it
 *  does not; link.exe writes one entry per distinct id with the number of modules that had
 *  it, and its own id last. The order is what nine reference images show: ascending by id,
 *  then each adjacent pair swapped. The space it reserves is one slot per marked module plus
 *  one for all the unmarked together plus one for itself - not one per entry - which is why
 *  p03, whose three short imports collapse into a single unmarked entry, still starts its PE
 *  header where p02 does. Both of those are read off the bed, not understood; docs has them.
 */
u32 rich_count(const Link &lk, std::vector<std::pair<u32, u32> > &ents)
{
    std::map<u32, u32> count;
    for (size_t i = 0; i < lk.mods.size(); i++) count[lk.mods[i].compid]++;
    u32 reserve = 0;
    for (std::map<u32, u32>::iterator it = count.begin(); it != count.end(); ++it) {
        ents.push_back(std::make_pair(it->first, it->second));
        reserve += (it->first == 0x00010000u) ? 1u : it->second;
    }
    for (size_t i = 0; i + 1 < ents.size(); i += 2) std::swap(ents[i], ents[i + 1]);
    return reserve;
}

u32 rich_lfanew(const Link &lk)
{
    std::vector<std::pair<u32, u32> > ents;
    u32 reserve = rich_count(lk, ents);
    return 0x98u + 8u * reserve;
}

int Link::out_index(const std::string &name) const
{
    for (size_t i = 0; i < outs.size(); i++) if (outs[i].name == name) return (int)i;
    return -1;
}

/* ---------------------------------------------------------- addresses */

/*  RVAs on the section grain, file offsets on the file grain, and uninitialised bytes only at
 *  the end of the section that swallowed them: a section's raw size stops at its last byte
 *  that is in the file, which is how p04's .data comes to be 0x1018 long and 0x200 on disk. */
bool Link::address()
{
    u32 hdr = lfanew + 24 + 240 + (u32)outs.size() * 40;
    size_of_headers = align_up(hdr, opt.file_align);

    u32 rva = opt.section_align;
    u32 off = size_of_headers;
    for (size_t i = 0; i < outs.size(); i++) {
        OutSection &s = outs[i];
        s.rva = rva;
        u32 cur = 0, raw = 0;
        for (size_t k = 0; k < s.parts.size(); k++) {
            Contrib *c = all[s.parts[k]];
            cur = align_up(cur, scn_align(c->flags));
            c->rva = s.rva + cur;
            if (!c->data.empty()) { c->fileoff = off + cur; raw = cur + c->size; }
            cur += c->size;
        }
        s.virt_size = cur;
        s.raw_size = align_up(raw, opt.file_align);
        s.fileoff = off;
        off += s.raw_size;
        rva = align_up(rva + s.virt_size, opt.section_align);
    }
    size_of_image = rva;

    if (!opt.entry.empty()) {
        std::map<std::string, std::pair<int, int> >::const_iterator it = resolved.find(opt.entry);
        if (it == resolved.end()) { err = "entry point not found: " + opt.entry; return false; }
        u64 r;
        if (!sym_rva(it->second.first, it->second.second, r)) return false;
        entry_rva = (u32)r;
    }
    return true;
}

bool Link::sym_rva(int mod, int sym, u64 &rva)
{
    const Symbol &s = mods[mod].syms[sym];
    if (s.section == -1) { rva = s.value; return true; }       /* absolute */
    if (s.section <= 0) { err = "undefined symbol: " + s.name; return false; }
    const Contrib &c = mods[mod].secs[s.section - 1];
    if (c.out < 0) { err = "symbol in a section that was left out: " + s.name; return false; }
    rva = c.rva + s.value;
    return true;
}

/* ---------------------------------------------------------- relocation */

bool Link::fix_up()
{
    for (size_t i = 0; i < all.size(); i++) {
        Contrib *c = all[i];
        if (c->data.empty()) continue;
        for (size_t r = 0; r < c->relocs.size(); r++) {
            const Reloc &rl = c->relocs[r];
            if (rl.offset + 2 > c->data.size()) { err = "relocation past the end of " + c->name; return false; }
            const Module &m = mods[c->module];
            if (rl.sym >= m.syms.size()) { err = "relocation names a symbol that is not there"; return false; }
            const Symbol &s = m.syms[rl.sym];

            u64 S;
            if (s.section == 0) {
                /* an external the module did not define: it was resolved elsewhere */
                std::map<std::string, std::pair<int, int> >::const_iterator it = resolved.find(s.name);
                if (it == resolved.end()) { err = "unresolved external symbol: " + s.name; return false; }
                if (!sym_rva(it->second.first, it->second.second, S)) return false;
            } else {
                if (!sym_rva(c->module, rl.sym, S)) return false;
            }

            u8 *p = &c->data[rl.offset];
            u32 P = c->rva + rl.offset;
            switch (rl.type) {
            case REL_ABSOLUTE: break;
            case REL_ADDR64:
                wr64(p, opt.image_base + S + rd64(p));
                base_relocs.push_back(P);
                break;
            case REL_ADDR32:
                wr32(p, (u32)(opt.image_base + S + rd32(p)));
                base_relocs.push_back(P);
                break;
            case REL_ADDR32NB:
                wr32(p, (u32)(S + rd32(p)));
                break;
            case REL_REL32: case REL_REL32_1: case REL_REL32_2:
            case REL_REL32_3: case REL_REL32_4: case REL_REL32_5:
                wr32(p, (u32)(S + rd32(p) - (P + 4 + (rl.type - REL_REL32))));
                break;
            case REL_SECREL:
                wr32(p, (u32)(S + rd32(p) - outs[c->out].rva));
                break;
            case REL_SECTION: {
                int oi = -1;
                if (s.section > 0) oi = mods[c->module].secs[s.section - 1].out;
                wr16(p, (u16)(oi + 1));
                break;
            }
            default: {
                char b[64]; sprintf(b, "relocation type 0x%02x is not handled", rl.type);
                err = c->name + ": " + b; return false;
            }
            }
        }
    }

    /*  .reloc: one block to a 4K page, each fixup a type in the top four bits and an offset in
     *  the low twelve, and an ABS entry of padding when a block ends on an odd word. */
    if (opt.fixed || base_relocs.empty()) return true;
    std::sort(base_relocs.begin(), base_relocs.end());
    size_t i = 0;
    while (i < base_relocs.size()) {
        u32 page = base_relocs[i] & ~0xFFFu;
        size_t j = i;
        while (j < base_relocs.size() && (base_relocs[j] & ~0xFFFu) == page) j++;
        size_t n = j - i;
        size_t block = 8 + ((n + 1) & ~(size_t)1) * 2;
        size_t at = reloc_data.size();
        reloc_data.resize(at + block, 0);
        wr32(&reloc_data[at], page);
        wr32(&reloc_data[at + 4], (u32)block);
        for (size_t k = 0; k < n; k++)
            wr16(&reloc_data[at + 8 + k * 2], (u16)(0xA000u | (base_relocs[i + k] - page)));
        i = j;
    }

    int ri = out_index(".reloc");
    if (ri >= 0) {
        OutSection &s = outs[ri];
        s.virt_size = (u32)reloc_data.size();
        s.raw_size  = align_up(s.virt_size, opt.file_align);
        reloc_data.resize(s.raw_size, 0);
        size_of_image = align_up(s.rva + s.virt_size, opt.section_align);
    }
    return true;
}
