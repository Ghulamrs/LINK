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
    : entry(), optref(true), optref_said(false), opticf(true), opticf_said(false), nodefaultlib(false), fixed(false), dynamicbase(true),
      timestamp(0), have_timestamp(false), subsystem(3),
      image_base(0x140000000ull), section_align(0x1000), file_align(0x200),
      stack_reserve(0x100000), stack_commit(0x1000), heap_reserve(0x100000), heap_commit(0x1000),
      debug(false), verbose(false) {}

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

/*  Where a library named without a directory is looked for: as given, then /libpath: in
 *  order, then LIB's directories in order - link.exe's rule. A name without an extension is
 *  a .lib: /DEFAULTLIB:libcpmt is how the CRT's objects spell it. */
std::string find_library(const Options &o, const std::string &name)
{
    std::string n = name;
    if (n.find('.') == std::string::npos) n += ".lib";
    FILE *f = fopen(n.c_str(), "rb");
    if (f) { fclose(f); return n; }
    if (n.find_first_of("/\\") != std::string::npos) return std::string();
    for (size_t i = 0; i < o.libpath.size(); i++) {
        std::string t = o.libpath[i];
        if (!t.empty() && t[t.size() - 1] != '/' && t[t.size() - 1] != '\\') {
            /*  The separator the directory itself uses, and `/` when it says nothing: a
             *  backslash is not one on the Mac, where this linker is also built, and every
             *  search there failed before its name could be opened. Windows takes `/` too. */
            t += (t.find('\\') != std::string::npos && t.find('/') == std::string::npos) ? '\\' : '/';
        }
        t += n;
        f = fopen(t.c_str(), "rb");
        if (f) { fclose(f); return t; }
    }
    return std::string();
}

namespace {

/* case-folded, for the names the CRT spells /DEFAULTLIB and /defaultlib alike */
std::string lower(const std::string &s)
{
    std::string o = s;
    for (size_t i = 0; i < o.size(); i++) if (o[i] >= 'A' && o[i] <= 'Z') o[i] = (char)(o[i] - 'A' + 'a');
    return o;
}

std::string leaf_lower(const std::string &path)
{
    size_t sl = path.find_last_of("/\\");
    std::string l = sl == std::string::npos ? path : path.substr(sl + 1);
    if (l.find('.') == std::string::npos) l += ".lib";
    return lower(l);
}

struct World {
    Link *lk;
    std::vector<Input> *ins;
    std::map<std::string, std::pair<int, int> > defined;   /* name -> module, symbol */
    std::vector<std::string> undef;                        /* still wanted, in first-asked order */
    std::map<std::string, bool> asked;
    std::map<std::string, int> asker;                      /* verbose: the module that first asked for a name */
    int taking;
    std::map<std::string, bool> nosearch;                  /* weak externals that must not pull members */
    std::map<std::string, std::string> alternate;          /* /ALTERNATENAME:X=Y: X -> Y */
    std::map<std::string, std::pair<int, int> > weak;      /* name -> the module and aux symbol to fall back on */
    std::map<std::string, std::pair<u32, int> > common;    /* name -> largest size, and the module that asked */
    std::vector<std::string> defaultlibs;                  /* /DEFAULTLIB names, in the order met */
    std::map<std::string, bool> refused;                   /* /nodefaultlib:name, lowered */
    std::map<std::string, std::pair<int, int> > comdat;    /* COMDAT symbol -> the contribution that holds it */
    std::vector<std::pair<int, int> > pending;             /* live sections whose references are still to be followed */
    std::map<std::string, bool> optional;                  /* names the linker asks for on its own account: no error when nothing has them */
};

/*  A name ending in _$fo$ is link.exe's function-override hook (the CRT's memcpy_$fo$,
 *  _guard_dispatch_icall_$fo$): p12 showed it neither searched for nor given its
 *  /alternatename, but resolved to zero - a call to it lands on the image base. The members
 *  that define such names (overrides.obj, cfg_fo.obj) are never pulled, and in the CRT no
 *  relocation refers to the name anyway. So: no search, and absolute zero at the end. */
bool is_override_hook(const std::string &nm)
{
    return nm.size() >= 5 && nm.compare(nm.size() - 5, 5, "_$fo$") == 0;
}

void want(World &w, const std::string &name)
{
    if (is_override_hook(name)) w.nosearch[name] = true;
    if (!w.asked[name]) { w.asked[name] = true; w.undef.push_back(name); w.asker[name] = w.taking; }
}

/*  What the image keeps is decided by reference once everything is loaded - link.exe's
 *  /OPT:REF, on unless /OPT:NOREF or /DEBUG says otherwise. A section is live when it is not
 *  a COMDAT, when it defines a name the linker itself asked for (the entry, an /include:,
 *  _load_config_used), when a live section refers to a symbol in it, or when it is
 *  ASSOCIATIVE to a live one; a COMDAT nothing reaches is left out. The archives are searched
 *  first, for every undefined name of every module loaded, whether or not the section that
 *  asks survives: the corpus said so - link.exe's hello carries undname.obj's string literals
 *  and tables though __unDName is named only from a COMDAT that ends up dead, and
 *  get_qualified_locale.obj's copy of a pooled string won its ANY selection there, so that
 *  member was pulled, from a reference that never reached the image. Asking only for what
 *  live sections name gave an image 157 members where the oracle has 169. */
void mark_live(World &w, int mi, int si)
{
    Module &m = w.lk->mods[mi];
    if (si < 0 || (size_t)si >= m.secs.size()) return;
    Contrib &c = m.secs[si];
    if (c.dropped || c.live) return;
    c.live = true;
    w.pending.push_back(std::make_pair(mi, si));
    for (size_t k = 0; k < m.secs.size(); k++)
        if (m.secs[k].select == COMDAT_ASSOCIATIVE && m.secs[k].assoc == si + 1) mark_live(w, mi, (int)k);
}

/* a definition's section, live */
void mark_defined_live(World &w, const std::pair<int, int> &def)
{
    const Symbol &s = w.lk->mods[def.first].syms[def.second];
    if (s.section > 0) mark_live(w, def.first, s.section - 1);
}

/*  Every relocation out of the sections marked since the last drain: a symbol in a section of
 *  the same module makes that section live; an external is the definition that won, live, or
 *  a name to ask for. A static in a section that lost is not followed - nothing outside its
 *  own COMDAT group refers to one. */
void drain(World &w)
{
    while (!w.pending.empty()) {
        std::pair<int, int> at = w.pending.back();
        w.pending.pop_back();
        for (size_t r = 0; r < w.lk->mods[at.first].secs[at.second].relocs.size(); r++) {
            const Module &m = w.lk->mods[at.first];
            const Reloc &rl = m.secs[at.second].relocs[r];
            if (rl.sym >= m.syms.size()) continue;
            const Symbol &s = m.syms[rl.sym];
            if (s.section > 0) {
                if (!m.secs[s.section - 1].dropped) { mark_live(w, at.first, s.section - 1); continue; }
            } else if (s.section != 0) {
                continue;                                   /* absolute, debug, the image base */
            }
            if (s.storage != SYM_EXTERNAL && s.storage != SYM_WEAK_EXTERNAL) continue;
            if (s.name.empty()) continue;
            std::map<std::string, std::pair<int, int> >::iterator d = w.defined.find(s.name);
            if (d != w.defined.end()) mark_defined_live(w, d->second);
        }
    }
}

/*  .drectve: the options an object hands the linker. /DEFAULTLIB and /ALTERNATENAME decide
 *  something here; /FAILIFMISMATCH, /MERGE, /DISALLOWLIB, /GUARDSYM, /THROWINGNEW and the rest
 *  are read and left alone. A name may be quoted. */
void read_directives(World &w, const Contrib &c)
{
    std::string t(c.data.begin(), c.data.end());
    size_t at = 0;
    while (at < t.size()) {
        while (at < t.size() && (t[at] == ' ' || t[at] == '\t' || t[at] == '\r' || t[at] == '\n' || t[at] == 0)) at++;
        if (at >= t.size()) break;
        size_t e = at;
        bool quoted = false;
        while (e < t.size() && (quoted || (t[e] != ' ' && t[e] != '\t' && t[e] != '\r' && t[e] != '\n' && t[e] != 0))) {
            if (t[e] == '"') quoted = !quoted;
            e++;
        }
        std::string opt = t.substr(at, e - at);
        at = e;
        if (opt.empty() || (opt[0] != '/' && opt[0] != '-')) continue;
        size_t colon = opt.find(':');
        std::string key = lower(opt.substr(1, colon == std::string::npos ? std::string::npos : colon - 1));
        std::string val = colon == std::string::npos ? std::string() : opt.substr(colon + 1);
        std::string bare;
        for (size_t i = 0; i < val.size(); i++) if (val[i] != '"') bare += val[i];
        if (key == "defaultlib" && !bare.empty()) {
            std::string l = leaf_lower(bare);
            bool seen = false;
            for (size_t i = 0; i < w.defaultlibs.size() && !seen; i++) seen = leaf_lower(w.defaultlibs[i]) == l;
            if (!seen) w.defaultlibs.push_back(bare);
        } else if (key == "include" && !bare.empty()) {
            want(w, bare);                                  /* a reference the module makes without a relocation */
        } else if (key == "alternatename") {
            size_t eq = bare.find('=');
            if (eq != std::string::npos && eq > 0 && eq + 1 < bare.size() &&
                w.alternate.find(bare.substr(0, eq)) == w.alternate.end())
                w.alternate[bare.substr(0, eq)] = bare.substr(eq + 1);
        }
    }
}

/*  A second definition of a COMDAT symbol: the section's selection says which one stays.
 *  ANY keeps the first; NODUPLICATES refuses; SAME_SIZE and EXACT_MATCH keep the first
 *  when the two agree and refuse otherwise; LARGEST keeps the bigger. The loser is dropped
 *  with every section ASSOCIATIVE to it, in its module. */
void drop_with_associates(Module &m, int sec)
{
    m.secs[sec].dropped = true;
    for (size_t k = 0; k < m.secs.size(); k++)
        if (!m.secs[k].dropped && m.secs[k].select == COMDAT_ASSOCIATIVE && m.secs[k].assoc == sec + 1)
            drop_with_associates(m, (int)k);
}

bool settle_comdat(World &w, int mi, int si, std::string &err)
{
    Module &m = w.lk->mods[mi];
    Contrib &c = m.secs[si];
    if (c.comdat_sym < 0) return true;                  /* nothing to settle by */
    const std::string &name = m.syms[c.comdat_sym].name;
    std::map<std::string, std::pair<int, int> >::iterator it = w.comdat.find(name);
    if (it == w.comdat.end()) { w.comdat[name] = std::make_pair(mi, si); return true; }
    Module &om = w.lk->mods[it->second.first];
    Contrib &oc = om.secs[it->second.second];
    bool keep_old = true;
    switch (c.select) {
    case COMDAT_ANY: break;
    case COMDAT_NODUPLICATES:
        err = name + " already defined in " + om.name + "; also in " + m.name; return false;
    case COMDAT_SAME_SIZE:
        if (oc.size != c.size) { err = name + ": COMDAT sections of different sizes in " + om.name + " and " + m.name; return false; }
        break;
    case COMDAT_EXACT_MATCH:
        if (oc.size != c.size || oc.checksum != c.checksum) {
            err = name + ": COMDAT sections that do not match in " + om.name + " and " + m.name; return false;
        }
        break;
    case COMDAT_LARGEST:
        keep_old = oc.size >= c.size;
        break;
    default: break;
    }
    if (keep_old) { drop_with_associates(m, si); return true; }
    bool was_live = oc.live;
    drop_with_associates(om, it->second.second);
    it->second = std::make_pair(mi, si);
    if (was_live) mark_live(w, mi, si);
    /* the symbol now belongs to the new section */
    std::map<std::string, std::pair<int, int> >::iterator d = w.defined.find(name);
    if (d != w.defined.end()) d->second = std::make_pair(mi, c.comdat_sym);
    return true;
}

bool take_module(World &w, const Module &m0, std::string &err)
{
    int mi = (int)w.lk->mods.size();
    w.lk->mods.push_back(m0);
    Module &m = w.lk->mods[mi];
    w.taking = mi;

    for (size_t si = 0; si < m.secs.size(); si++)
        if (m.secs[si].name == ".drectve") read_directives(w, m.secs[si]);

    /* COMDAT sections first, so that a symbol in a section that loses is not a definition */
    for (size_t si = 0; si < m.secs.size(); si++) {
        Contrib &c = m.secs[si];
        if (c.dropped || !(c.flags & SCN_LNK_COMDAT) || c.select == COMDAT_ASSOCIATIVE) continue;
        if (!settle_comdat(w, mi, (int)si, err)) return false;
    }
    /* an ASSOCIATIVE section whose parent was dropped goes with it; one whose parent is
       not a COMDAT at all is simply kept */
    for (size_t si = 0; si < m.secs.size(); si++) {
        Contrib &c = m.secs[si];
        if (c.dropped || c.select != COMDAT_ASSOCIATIVE) continue;
        if (c.assoc > 0 && (size_t)c.assoc <= m.secs.size() && m.secs[c.assoc - 1].dropped) c.dropped = true;
    }

    for (size_t i = 0; i < m.syms.size(); i++) {
        const Symbol &s = m.syms[i];
        if (s.name.empty()) continue;
        if (s.storage == SYM_WEAK_EXTERNAL) {
            /*  A weak external, once a live section refers to it, asks for its name; when
             *  nothing defines it, the aux symbol's definition stands in. NOLIBRARY says the
             *  archives are not to be searched for it - the CRT's optional hooks are spelled
             *  that way, and pulling members for them would drag in what the program did
             *  not ask for. */
            if (w.weak.find(s.name) == w.weak.end() && s.aux_tag >= 0)
                w.weak[s.name] = std::make_pair(mi, s.aux_tag);
            if (s.weak_kind == WEAK_NOLIBRARY) w.nosearch[s.name] = true;
            want(w, s.name);
            continue;
        }
        if (s.storage != SYM_EXTERNAL) continue;
        if (s.section > 0) {
            if ((size_t)s.section <= m.secs.size() && m.secs[s.section - 1].dropped) continue;
            std::map<std::string, std::pair<int, int> >::iterator d = w.defined.find(s.name);
            if (d == w.defined.end()) { w.defined[s.name] = std::make_pair(mi, (int)i); continue; }
            /* defined twice: a COMDAT symbol was settled above and its loser dropped, so a
               second definition that reaches here is a real duplicate */
            const Module &om = w.lk->mods[d->second.first];
            const Symbol &os = om.syms[d->second.second];
            bool old_comdat = os.section > 0 && (size_t)os.section <= om.secs.size() &&
                              (om.secs[os.section - 1].flags & SCN_LNK_COMDAT);
            bool new_comdat = (m.secs[s.section - 1].flags & SCN_LNK_COMDAT) != 0;
            if (old_comdat && new_comdat) continue;      /* both COMDAT: settled by selection */
            err = s.name + " already defined in " + om.name + "; also in " + m.name;
            return false;
        } else if (s.section == -1 || s.section == -3) {
            if (w.defined.find(s.name) == w.defined.end()) w.defined[s.name] = std::make_pair(mi, (int)i);
        } else if (s.section == 0 && s.value == 0) {
            want(w, s.name);
        } else if (s.section == 0) {
            /*  COMMON: undefined with a size; the largest wins, allocated at the end. It is a
             *  definition of a kind, so the archives are not searched for the name - p13:
             *  link.exe leaves the member that defines cval alone until something else pulls
             *  it, and then that definition wins (libcmt's _tls_index, __dyn_tls_init_callback
             *  and __scrt_ucrt_dll_is_in_use each have such a member, none of them in hello). */
            std::map<std::string, std::pair<u32, int> >::iterator it = w.common.find(s.name);
            if (it == w.common.end() || it->second.first < s.value) w.common[s.name] = std::make_pair(s.value, mi);
            w.nosearch[s.name] = true;
            want(w, s.name);
        }
    }
    return true;
}

/*  One pass over the archives: every member the current undefined set names is taken, and
 *  the members taken may ask for more. An archive is searched again for what its own
 *  members just asked before the next archive is opened - link.exe's order, and the one
 *  that decides which of two libraries' guard_support.obj an image gets: loadcfg.obj asks
 *  for __guard_check_icall_fptr, and libcmt, where loadcfg.obj came from, answers before
 *  libucrt is looked at. Returns whether anything was taken. */
bool pull_pass(World &w, std::string &err, bool &took)
{
    took = false;
    std::vector<Input> &ins = *w.ins;
    for (size_t a = 0; a < ins.size(); a++) {
        if (!ins[a].is_archive) continue;
        Archive &ar = ins[a].arch;
      for (;;) {
        std::vector<u32> pull;
        for (size_t u = 0; u < w.undef.size(); u++) {
            const std::string &nm = w.undef[u];
            if (w.defined.find(nm) != w.defined.end()) continue;
            if (w.nosearch.find(nm) != w.nosearch.end()) continue;
            for (size_t k = 0; k < ar.index.size(); k++) {
                if (ar.index[k].first != nm) continue;
                u32 off = ar.index[k].second;
                if (std::find(ar.taken.begin(), ar.taken.end(), off) != ar.taken.end()) break;
                if (std::find(pull.begin(), pull.end(), off) == pull.end()) {
                    pull.push_back(off);
                    if (w.lk->opt.verbose) fprintf(stderr, "pull %s <- %s%s%s asked by %s\n", ar.name.c_str(), nm.c_str(),
                        w.alternate.count(nm) ? " [alternate]" : "", w.weak.count(nm) ? " [weak]" : "",
                        w.asker[nm] >= 0 ? w.lk->mods[w.asker[nm]].name.c_str() : "?");
                }
                break;
            }
        }
        if (pull.empty()) break;
        for (size_t k = 0; k < pull.size(); k++) {
            Module m;
            if (!ar.member(pull[k], ar.name, m, err)) return false;
            if (w.lk->opt.verbose) fprintf(stderr, "took %s\n", m.name.c_str());
            m.lib = (int)a;
            ar.taken.push_back(pull[k]);
            if (!take_module(w, m, err)) return false;
            took = true;
        }
      }
    }
    return true;
}

bool pull_until_settled(World &w, std::string &err)
{
    bool took = true;
    while (took) if (!pull_pass(w, err, took)) return false;
    return true;
}

/*  The names still undefined after the archives: an alternate name takes the other's
 *  definition (and may ask for it), a weak external takes its aux symbol's. Either may
 *  bring in more, so the archives are searched again until nothing moves. */
bool settle_fallbacks(World &w, std::string &err)
{
    for (;;) {
        bool changed = false;
        for (size_t u = 0; u < w.undef.size(); u++) {
            const std::string nm = w.undef[u];
            if (w.defined.find(nm) != w.defined.end()) continue;
            std::map<std::string, std::string>::iterator a = w.alternate.find(nm);
            if (a != w.alternate.end()) {
                std::map<std::string, std::pair<int, int> >::iterator d = w.defined.find(a->second);
                if (d != w.defined.end()) { w.defined[nm] = d->second; changed = true; continue; }
                if (!w.asked[a->second]) { want(w, a->second); changed = true; }
                continue;
            }
            std::map<std::string, std::pair<int, int> >::iterator k = w.weak.find(nm);
            if (k != w.weak.end()) {
                const Symbol &t = w.lk->mods[k->second.first].syms[k->second.second];
                if (t.section != 0) { w.defined[nm] = k->second; changed = true; continue; }
                std::map<std::string, std::pair<int, int> >::iterator d = w.defined.find(t.name);
                if (d != w.defined.end()) { w.defined[nm] = d->second; changed = true; continue; }
                if (!w.asked[t.name]) { want(w, t.name); changed = true; }
            }
        }
        if (!changed) return true;
        if (!pull_until_settled(w, err)) return false;
    }
}

/*  The libraries the objects asked for through /DEFAULTLIB, opened after the ones the
 *  command line named and searched in the order met - unless /NODEFAULTLIB said no to all
 *  of them, or /NODEFAULTLIB:name to one. Returns whether any were opened. */
bool open_default_libraries(World &w, std::string &err, bool &opened)
{
    opened = false;
    if (w.lk->opt.nodefaultlib) return true;
    std::vector<Input> &ins = *w.ins;
    std::vector<Input> extra;
    for (size_t k = 0; k < w.defaultlibs.size(); k++) {
        std::string l = leaf_lower(w.defaultlibs[k]);
        if (w.refused.find(l) != w.refused.end()) continue;
        bool already = false;
        for (size_t i = 0; i < ins.size() && !already; i++)
            already = ins[i].is_archive && leaf_lower(ins[i].path) == l;
        for (size_t i = 0; i < extra.size() && !already; i++) already = leaf_lower(extra[i].path) == l;
        if (already) continue;
        std::string f = find_library(w.lk->opt, w.defaultlibs[k]);
        if (f.empty()) { err = w.defaultlibs[k] + ": cannot open (asked for by /DEFAULTLIB)"; return false; }
        Input in; in.path = f; in.ok = false; in.is_archive = false;
        extra.push_back(in);
    }
    for (size_t k = 0; k < extra.size(); k++) {
        read_one(&extra[k]);
        if (!extra[k].ok) { err = extra[k].err; return false; }
        ins.push_back(extra[k]);
        opened = true;
    }
    return true;
}

} /* namespace */

bool Link::read_inputs()
{
    std::vector<Input> ins;
    ins.reserve(opt.inputs.size() + 32);      /* the default libraries join later */
    ins.resize(opt.inputs.size());
    for (size_t i = 0; i < opt.inputs.size(); i++) {
        std::string p = opt.inputs[i];
        std::string found = find_library(opt, p);
        ins[i].path = found.empty() ? p : found;
        ins[i].ok = false; ins[i].is_archive = false;
    }

    std::vector<std::thread> th;
    th.reserve(ins.size());
    for (size_t i = 0; i < ins.size(); i++) th.push_back(std::thread(read_one, &ins[i]));
    for (size_t i = 0; i < th.size(); i++) th[i].join();

    for (size_t i = 0; i < ins.size(); i++)
        if (!ins[i].ok) { err = ins[i].err; return false; }

    World w; w.lk = this; w.ins = &ins; w.taking = -1;
    for (size_t i = 0; i < opt.nodefaultlibs.size(); i++) w.refused[leaf_lower(opt.nodefaultlibs[i])] = true;
    for (size_t i = 0; i < opt.defaultlibs.size(); i++) w.defaultlibs.push_back(opt.defaultlibs[i]);

    /* the objects first, in command-line order: that order is the layout order */
    for (size_t i = 0; i < ins.size(); i++)
        if (!ins[i].is_archive && !take_module(w, ins[i].mod, err)) return false;

    /*  The entry point. Without /entry: link.exe asks for the CRT's, by the subsystem and by
     *  which main the objects defined: mainCRTStartup for a console program with main,
     *  wmainCRTStartup for one with wmain, WinMainCRTStartup / wWinMainCRTStartup for a
     *  windows one. Asked for before the archives are searched, so the CRT's start-up
     *  member is pulled with everything it needs. */
    if (opt.entry.empty()) {
        bool wide = w.defined.find("wmain") != w.defined.end() || w.defined.find("wWinMain") != w.defined.end();
        opt.entry = opt.subsystem == 2 ? (wide ? "wWinMainCRTStartup" : "WinMainCRTStartup")
                                       : (wide ? "wmainCRTStartup" : "mainCRTStartup");
    }
    /*  The linker's own references, which are also what keeps a COMDAT alive with nothing
     *  else pointing at it: the entry, every /include:, and _load_config_used - asked for by
     *  link.exe itself (libcmt's loadcfg.obj is in every corpus image and nothing in the
     *  objects names it), and asked for quietly: the probes link /nodefaultlib against
     *  kernel32.lib alone, and nothing is said when no library has it. */
    std::vector<std::string> roots;
    roots.push_back(opt.entry);
    for (size_t i = 0; i < opt.includes.size(); i++) roots.push_back(opt.includes[i]);
    roots.push_back("_load_config_used");
    w.optional["_load_config_used"] = true;
    for (size_t i = 0; i < roots.size(); i++) want(w, roots[i]);

    /*  __ImageBase is the linker's: the image base itself, RVA 0. An ADDR64 to it gets the
     *  base, an ADDR32NB 0 - which is what section -3 means to sym_rva. The control-flow-guard
     *  names libcmt's loadcfg.obj fills the load-config record from are the linker's too, and
     *  absolute: with no /guard:cf every table and count is zero and __guard_flags is 0x100
     *  (CF_INSTRUMENTED, which link.exe sets regardless) - the corpus's oracle records, read
     *  back field by field. An ADDR64 to an absolute is the value alone, with no base and no
     *  base relocation. */
    {
        Module m; m.name = "*imagebase*"; m.compid = 0xFFFFFFFFu; m.from_archive = false; m.lib = -1;
        Symbol s; s.name = "__ImageBase"; s.value = 0; s.section = -3; s.storage = SYM_EXTERNAL;
        s.type = 0; s.naux = 0; s.aux_tag = -1; s.weak_kind = 0;
        m.syms.push_back(s);
        static const char *const guard[] = {
            "__guard_fids_table", "__guard_fids_count", "__guard_flags", "__guard_iat_table",
            "__guard_iat_count", "__guard_longjmp_table", "__guard_longjmp_count", "__enclave_config",
            "__guard_eh_cont_table", "__guard_eh_cont_count", 0
        };
        for (int i = 0; guard[i]; i++) {
            s.name = guard[i]; s.section = -1; s.value = strcmp(guard[i], "__guard_flags") == 0 ? 0x100u : 0u;
            m.syms.push_back(s);
        }
        if (!take_module(w, m, err)) return false;
    }

    /*  Then the archives, in passes; then the libraries the objects asked for, which may
     *  ask for more; and only when no library is left to open, the fallbacks - which may
     *  take members that ask for more libraries, so round until nothing opens.
     *
     *  **The fallbacks last, and it is not a matter of taste.** libcmt's startup calls
     *  __acrt_initialize and __vcrt_initialize as weak externals whose fallbacks are the
     *  empty stubs in libcmt(ucrt_stubs.obj); the real ones are in libucrt and libvcruntime,
     *  which LIBCMT's own /DEFAULTLIB opens. Settled before those were open, the stubs won,
     *  the CRT was never initialised, and the program died in ntdll before main - every shalimar
     *  program, whose link names only its object and shmrt-x86_64-windows.lib and gets the
     *  CRT by directive. c90 and cpp11 name the CRT libraries and never saw it. */
    for (;;) {
        if (!pull_until_settled(w, err)) return false;
        bool opened = false;
        if (!open_default_libraries(w, err, opened)) return false;
        if (opened) continue;
        if (!settle_fallbacks(w, err)) return false;
        if (!open_default_libraries(w, err, opened)) return false;
        if (!opened) break;
    }

    /*  The COMMON symbols nothing defined: one uninitialised contribution of the linker's,
     *  largest size each, aligned by size up to sixteen, placed at the end of .data's
     *  uninitialised run where link.exe puts them. */
    {
        Module m; m.name = "*common*"; m.compid = 0xFFFFFFFFu; m.from_archive = false; m.lib = -1;
        Contrib c;
        c.name = ".bss"; c.flags = (u32)(SCN_CNT_UNINIT | SCN_MEM_READ | SCN_MEM_WRITE) | 0x00500000u;
        c.size = 0; c.module = -1; c.serial = 0; c.dropped = false; c.out = -1; c.rva = 0; c.fileoff = 0;
        c.select = COMDAT_NONE; c.assoc = 0; c.checksum = 0; c.comdat_sym = -1;
        for (std::map<std::string, std::pair<u32, int> >::iterator it = w.common.begin(); it != w.common.end(); ++it) {
            if (w.defined.find(it->first) != w.defined.end()) continue;
            u32 sz = it->second.first;
            u32 al = sz >= 16 ? 16 : sz >= 8 ? 8 : sz >= 4 ? 4 : sz >= 2 ? 2 : 1;
            c.size = align_up(c.size, al);
            Symbol s; s.name = it->first; s.value = c.size; s.section = 1; s.storage = SYM_EXTERNAL;
            s.type = 0; s.naux = 0; s.aux_tag = -1; s.weak_kind = 0;
            m.syms.push_back(s);
            c.size += sz;
        }
        if (c.size) { m.secs.push_back(c); if (!take_module(w, m, err)) return false; }
    }

    /* the function-override hooks (is_override_hook): absolute zero, as link.exe leaves them */
    {
        Module m; m.name = "*override*"; m.compid = 0xFFFFFFFFu; m.from_archive = false; m.lib = -1;
        for (size_t u = 0; u < w.undef.size(); u++) {
            const std::string &nm = w.undef[u];
            if (w.defined.find(nm) != w.defined.end() || !is_override_hook(nm)) continue;
            Symbol s; s.name = nm; s.value = 0; s.section = -1; s.storage = SYM_EXTERNAL;
            s.type = 0; s.naux = 0; s.aux_tag = -1; s.weak_kind = 0;
            m.syms.push_back(s);
        }
        if (!m.syms.empty() && !take_module(w, m, err)) return false;
    }

    /*  Liveness, now that everything is in: the roots are every section that is not a
     *  COMDAT - every section, when nothing is to be left out - and the definitions of the
     *  linker's own names; everything those reach follows; a COMDAT nothing reached is left
     *  out with what is ASSOCIATIVE to it. */
    for (size_t mi = 0; mi < mods.size(); mi++)
        for (size_t si = 0; si < mods[mi].secs.size(); si++) {
            Contrib &c = mods[mi].secs[si];
            if (!c.dropped && (!(c.flags & SCN_LNK_COMDAT) || !opt.optref)) mark_live(w, (int)mi, (int)si);
        }
    for (size_t i = 0; i < roots.size(); i++) {
        std::map<std::string, std::pair<int, int> >::iterator d = w.defined.find(roots[i]);
        if (d != w.defined.end()) mark_defined_live(w, d->second);
    }
    drain(w);
    if (opt.optref)
        for (size_t mi = 0; mi < mods.size(); mi++)
            for (size_t si = 0; si < mods[mi].secs.size(); si++) {
                Contrib &c = mods[mi].secs[si];
                if (!c.dropped && !c.live && (c.flags & SCN_LNK_COMDAT)) drop_with_associates(mods[mi], (int)si);
            }

    if (opt.verbose)
        for (size_t mi = 0; mi < mods.size(); mi++)
            for (size_t si = 0; si < mods[mi].secs.size(); si++)
                if (mods[mi].secs[si].dropped && !mods[mi].secs[si].live && (mods[mi].secs[si].flags & SCN_LNK_COMDAT))
                    fprintf(stderr, "dead %s #%d %s\n", mods[mi].name.c_str(), (int)si + 1, mods[mi].secs[si].name.c_str());

    std::vector<std::string> missing;
    for (size_t u = 0; u < w.undef.size(); u++)
        if (w.defined.find(w.undef[u]) == w.defined.end() && !w.optional[w.undef[u]]) missing.push_back(w.undef[u]);
    if (!missing.empty()) {
        /* every one, as link.exe reports them, so a program is understood in one run */
        err = "unresolved external symbol";
        if (missing.size() > 1) { char b[32]; snprintf(b, sizeof b, "s (%d)", (int)missing.size()); err += b; }
        err += ":";
        for (size_t i = 0; i < missing.size(); i++) err += "\n  " + printable(missing[i]);
        return false;
    }

    resolved.swap(w.defined);

    /*  The kept-static map, for a reloc to a static in a COMDAT that lost. Built after the
     *  dead COMDATs are dropped, over the sections that remain, so a name resolves to the
     *  copy that stays. An external already sits in `resolved`; this adds the statics. */
    for (size_t mi = 0; mi < mods.size(); mi++)
        for (size_t k = 0; k < mods[mi].syms.size(); k++) {
            const Symbol &s = mods[mi].syms[k];
            if (s.section <= 0 || (size_t)s.section > mods[mi].secs.size() || s.name.empty()) continue;
            if (mods[mi].secs[s.section - 1].dropped) continue;
            if (kept.find(s.name) == kept.end()) kept[s.name] = std::make_pair((int)mi, (int)k);
        }
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
        base == ".edata"  || base == ".CRT"  || base == ".00cfg" ||
        base == ".rtc"    || base == "_RDATA")
        return ".rdata";
    if (base == ".data"  || base == ".bss" || base == ".tls") return ".data";
    if (base == ".pdata")  return ".pdata";
    return base;
}

/*  Inside .rdata the corpus maps show link.exe's order plainly: .idata$5 first (the IAT
 *  directory points at exactly it), then every other name in ASCII order - .00cfg, the
 *  .CRT$X* initialisers, .rdata and its suffixes, .rtc$*, .xdata - then .edata, then the
 *  import directory's .idata$2, $3, $4 and $6, in that order. */
static int rank_of(const std::string &out, const std::string &name)
{
    if (out == ".rdata") {
        if (name == ".idata$5") return 0;
        if (name.compare(0, 6, ".edata") == 0) return 2;
        if (name.compare(0, 6, ".idata") == 0) return 3;
        return 1;
    }
    if (out == ".data") return name.compare(0, 4, ".bss") == 0 ? 1 : 0;
    return 0;
}

namespace {

/*  Inside a run of import-table contributions the order is per library: each library's
 *  words are contiguous, and the library's null word - the NULL_THUNK_DATA member's, the
 *  one with no relocation - comes last in its run, or the loader takes it for the end of
 *  the table (the review's L11: every corpus image crashed on it). Libraries in the order
 *  they were named or asked for. */
struct Placed {
    Contrib *c;
    int out, rank;
    int lib;            /* the library the contribution came from, -1 for an object */
    bool last;          /* an import-table word with no relocation: the terminator */
    bool operator<(const Placed &o) const {
        if (out  != o.out)  return out < o.out;
        if (rank != o.rank) return rank < o.rank;
        if (c->name != o.c->name) return c->name < o.c->name;
        if (lib  != o.lib)  return lib < o.lib;
        if (last != o.last) return !last;
        return c->serial < o.c->serial;
    }
};

u32 out_flags(const std::string &n)
{
    /* cast each: to cl an or of enumerators that fits in an int is an int (C4245) */
    if (n == ".text")  return (u32)(SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ);
    if (n == ".data")  return (u32)(SCN_CNT_INITDATA | SCN_MEM_READ | SCN_MEM_WRITE);
    if (n == ".reloc") return (u32)(SCN_CNT_INITDATA | SCN_MEM_DISCARD | SCN_MEM_READ);
    return (u32)(SCN_CNT_INITDATA | SCN_MEM_READ);
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
                /* an empty contribution is placed too, at the current address: a label in it
                   is a real label (the review's L19) */
                if (c.dropped) continue;
                Placed p;
                p.c = &c;
                p.out = out_order(out_of(c.name));
                p.rank = rank_of(out_of(c.name), c.name);
                p.lib = mods[mi].lib;
                p.last = c.name.compare(0, 7, ".idata$") == 0 && c.relocs.empty() &&
                         (c.name == ".idata$4" || c.name == ".idata$5");
                ps.push_back(p);
            }
        }
        std::stable_sort(ps.begin(), ps.end());

        /* an output section made of nothing but empty contributions is not made at all:
           link.exe writes no section for p01's empty .data */
        std::map<std::string, u32> total;
        for (size_t i = 0; i < ps.size(); i++) total[out_of(ps[i].c->name)] += ps[i].c->size;

        std::map<std::string, int> seen;
        for (size_t i = 0; i < ps.size(); i++) {
            std::string on = out_of(ps[i].c->name);
            if (total[on] == 0) continue;
            std::map<std::string, int>::iterator it = seen.find(on);
            if (it == seen.end()) {
                it = seen.insert(std::make_pair(on, (int)outs.size())).first;
                OutSection s;
                s.name = on; s.flags = out_flags(on);
                /* a section of a name this linker does not know - .fptable, kept by link.exe
                   as its own - carries the characteristics its first contribution had */
                if (on != ".text" && on != ".rdata" && on != ".data" && on != ".pdata" && on != ".reloc")
                    s.flags = ps[i].c->flags & (u32)(SCN_CNT_CODE | SCN_CNT_INITDATA | SCN_CNT_UNINIT |
                                                   SCN_MEM_EXECUTE | SCN_MEM_READ | SCN_MEM_WRITE | SCN_MEM_DISCARD);
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
                if (all[i]->size == 0 || all[i]->name == last) continue;
                last = all[i]->name;
                if (opt.verbose) {
                    /* the run's total, the way the map file lists it, for holding against link.exe's */
                    u32 run = 0;
                    for (size_t k = i; k < all.size() && all[k]->name == last; k++) run += all[k]->size;
                    fprintf(stderr, "run %-28s %x\n", last.c_str(), run);
                }
                n += 8 + ((last.size() + 1 + 3) & ~(size_t)3);
            }
            /*  Sixteen zero bytes follow the record - but only in an image with no .data
             *  section. p04 and p06 carry the same eleven names and the same record and
             *  differ in exactly this; all thirteen reference images agree on it. What the
             *  sixteen are for is still unread; that they are not there once .data exists
             *  is what the bed says. */
            bool has_data = false;
            for (size_t i = 0; i < outs.size(); i++) if (outs[i].name == ".data") has_data = true;
            if (!has_data) n += 16;
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
 *  Every module carries a @comp.id saying which tool wrote it, or counts as zero when it
 *  does not - an object with none is 0x00000000 and only a short import member is 0x00010000,
 *  which p10 settled. link.exe writes one entry per distinct id with the number of modules
 *  that had it, and its own id last. The order is what nine reference images show: ascending by id,
 *  then each adjacent pair swapped. The space it reserves is one slot per marked module plus
 *  one for all the unmarked together plus one for itself - not one per entry - which is why
 *  p03, whose three short imports collapse into a single unmarked entry, still starts its PE
 *  header where p02 does. Both of those are read off the bed, not understood; docs has them.
 */
u32 rich_count(const Link &lk, std::vector<std::pair<u32, u32> > &ents)
{
    std::map<u32, u32> count;
    /* the linker's own symbol modules (compid all-ones) are not objects and are not counted */
    for (size_t i = 0; i < lk.mods.size(); i++)
        if (lk.mods[i].compid != 0xFFFFFFFFu) count[lk.mods[i].compid]++;
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
            /* an empty contribution takes the current address and pads nothing: the maps
               show .gehcont$y (0 bytes) and the .rdata after it at one address */
            if (c->size) cur = align_up(cur, scn_align(c->flags));
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
    if (s.section == -3) { rva = 0; return true; }             /* the image base itself */
    if (s.section <= 0) { err = "undefined symbol: " + s.name; return false; }
    /*  A folded section holds no bytes of its own; its symbols answer at the
     *  survivor's address, which is the whole point of the fold. */
    int fm = mod, fs = s.section - 1;
    fold_rep(fm, fs);
    const Contrib &c = mods[fm].secs[fs];
    if (c.out < 0 && c.size == 0 && !c.dropped) { rva = 0; return true; }   /* a label in a section of nothing */
    if (c.out < 0) { err = "symbol in a section that was left out: " + s.name; return false; }
    rva = c.rva + s.value;
    return true;
}

/* ---------------------------------------------------------- relocation */

/*  .pdata holds one RUNTIME_FUNCTION per function, and the loader finds a function's by
 *  binary search - so the table is sorted by BeginAddress across every module, which the
 *  contributions' placement order is not once cpp11's .text$x funclets sort after every
 *  module's .text$mn (the review's L13: 713 of 1,815 entries out of order). The entries
 *  are gathered from the placed .pdata contributions after fix-up, sorted, and put back in
 *  the same slots. */
void Link::sort_pdata()
{
    int pi = out_index(".pdata");
    if (pi < 0) return;
    std::vector<std::pair<u32, std::string> > entries;
    std::vector<Contrib*> parts;
    for (size_t k = 0; k < outs[pi].parts.size(); k++) {
        Contrib *c = all[outs[pi].parts[k]];
        if (c->data.empty()) continue;
        parts.push_back(c);
        for (size_t at = 0; at + 12 <= c->data.size(); at += 12)
            entries.push_back(std::make_pair(rd32(&c->data[at]), std::string(c->data.begin() + at, c->data.begin() + at + 12)));
    }
    std::stable_sort(entries.begin(), entries.end());
    size_t e = 0;
    for (size_t k = 0; k < parts.size(); k++) {
        Contrib *c = parts[k];
        for (size_t at = 0; at + 12 <= c->data.size() && e < entries.size(); at += 12, e++)
            memcpy(&c->data[at], entries[e].second.data(), 12);
    }
}

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

            u64 S = 0;
            bool absolute = false;                 /* the value is not an address: no base, no base relocation */
            if (s.section == 0 && s.storage == SYM_SECTION) {
                /*  A section-class reference - the import descriptor's, to its library's
                 *  .idata$4, $5 and $6: the first contribution of that name that came from
                 *  the same library, which the per-library placement makes the start of that
                 *  library's run. */
                bool found = false;
                for (size_t k = 0; k < all.size() && !found; k++)
                    if (all[k]->name == s.name && mods[all[k]->module].lib == m.lib) { S = all[k]->rva; found = true; }
                for (size_t k = 0; k < all.size() && !found; k++)
                    if (all[k]->name == s.name) { S = all[k]->rva; found = true; }
                if (!found) { err = "no contribution named " + s.name + " for " + m.name; return false; }
            } else if (s.section == 0 ||
                       (s.section > 0 && (size_t)s.section <= m.secs.size() && m.secs[s.section - 1].dropped)) {
                /*  An external the module did not define - or defined in a COMDAT section that
                 *  lost, which is the same thing: the name reaches the definition that won.
                 *  A module's own inline __local_stdio_printf_options is the usual case. */
                std::map<std::string, std::pair<int, int> >::const_iterator it = resolved.find(s.name);
                /*  A static whose own COMDAT section lost falls back on the winning copy, kept
                 *  by name in `kept` - the exception funclets $catch$N, $unwind$, $pdata$ are
                 *  the case the corpus hit (07-vector3's std::string operator+, defined in two
                 *  objects, each with its own $catch$0). */
                bool have = it != resolved.end();
                std::pair<int, int> def = have ? it->second : std::make_pair(0, 0);
                if (!have) {
                    std::map<std::string, std::pair<int, int> >::const_iterator kt = kept.find(s.name);
                    if (kt == kept.end()) { err = "unresolved external symbol: " + s.name; return false; }
                    def = kt->second;
                }
                if (!sym_rva(def.first, def.second, S)) return false;
                absolute = mods[def.first].syms[def.second].section == -1;
            } else {
                if (!sym_rva(c->module, rl.sym, S)) return false;
                absolute = s.section == -1;
            }

            u8 *p = &c->data[rl.offset];
            u32 P = c->rva + rl.offset;
            switch (rl.type) {
            case REL_ABSOLUTE: break;
            case REL_ADDR64:
                if (absolute) { wr64(p, S + rd64(p)); break; }
                wr64(p, opt.image_base + S + rd64(p));
                base_relocs.push_back(std::make_pair(P, (u16)0xA));    /* DIR64 */
                break;
            case REL_ADDR32:
                if (absolute) { wr32(p, (u32)(S + rd32(p))); break; }
                wr32(p, (u32)(opt.image_base + S + rd32(p)));
                base_relocs.push_back(std::make_pair(P, (u16)0x3));    /* HIGHLOW */
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
                char b[64]; snprintf(b, sizeof b, "relocation type 0x%02x is not handled", rl.type);
                err = c->name + ": " + b; return false;
            }
            }
        }
    }

    sort_pdata();

    /*  .reloc: one block to a 4K page, each fixup a type in the top four bits and an offset in
     *  the low twelve, and an ABS entry of padding when a block ends on an odd word. */
    if (opt.fixed || base_relocs.empty()) return true;
    std::sort(base_relocs.begin(), base_relocs.end());
    size_t i = 0;
    while (i < base_relocs.size()) {
        u32 page = base_relocs[i].first & ~0xFFFu;
        size_t j = i;
        while (j < base_relocs.size() && (base_relocs[j].first & ~0xFFFu) == page) j++;
        size_t n = j - i;
        size_t block = 8 + ((n + 1) & ~(size_t)1) * 2;
        size_t at = reloc_data.size();
        reloc_data.resize(at + block, 0);
        wr32(&reloc_data[at], page);
        wr32(&reloc_data[at + 4], (u32)block);
        for (size_t k = 0; k < n; k++)
            wr16(&reloc_data[at + 8 + k * 2],
                 (u16)((u16)(base_relocs[i + k].second << 12) | (u16)(base_relocs[i + k].first - page)));
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


/* ------------------------------------------------------------------- ICF */

/*  The surviving contribution a fold chain ends at. The chain is built one
 *  link long - a section folds straight into a survivor that is itself not
 *  folded - but it is walked defensively, since a later round may fold a
 *  survivor into an earlier one. */
void Link::fold_rep(int &mod, int &sec) const
{
    for (int guard = 0; guard < 64; guard++) {
        if (mod < 0 || sec < 0) return;
        const Contrib &c = mods[mod].secs[sec];
        if (c.fold_mod < 0) return;
        mod = c.fold_mod;
        sec = c.fold_sec;
    }
}

/*  What a relocation points at, written so that two relocations in different
 *  sections compare equal exactly when they will reach the same address. A
 *  target inside a contribution is named by that contribution's *survivor*,
 *  so a pair that has already folded makes its referrers identical in turn. */
std::string Link::target_key(int mod, const Reloc &r) const
{
    const Module &m = mods[mod];
    if (r.sym >= m.syms.size()) return "!";
    const Symbol &s = m.syms[r.sym];

    /*  A section-class reference - the import descriptor's .idata$4 and the
     *  like - names a section rather than a symbol; its own name is the key. */
    if (s.section == 0 && s.storage == SYM_SECTION) return "S" + s.name;

    int tm = mod, ts = -1;
    long long val = 0;
    if (s.section > 0 && (size_t)s.section <= m.secs.size() && !m.secs[s.section - 1].dropped) {
        ts = s.section - 1;
        val = s.value;
    } else {
        std::map<std::string, std::pair<int, int> >::const_iterator it = resolved.find(s.name);
        if (it == resolved.end()) return "U" + s.name;
        const Symbol &d = mods[it->second.first].syms[it->second.second];
        if (d.section == -3) return "B";                       /* the image base */
        if (d.section == -1) {                                 /* an absolute */
            char b[32]; snprintf(b, sizeof b, "A%llx", (unsigned long long)d.value);
            return b;
        }
        if (d.section <= 0 || (size_t)d.section > mods[it->second.first].secs.size())
            return "U" + s.name;
        tm = it->second.first;
        ts = d.section - 1;
        val = d.value;
    }
    fold_rep(tm, ts);
    char b[64];
    snprintf(b, sizeof b, "C%d:%d:%lld", tm, ts, val);
    return b;
}

/*  Everything about a contribution that has to match for a fold to be safe:
 *  the section it would be placed in and its name, its size, the
 *  characteristics that decide its placement, its bytes, and every relocation
 *  by offset, type and target. Two contributions with the same key produce the
 *  same bytes at the same address, so one of them is not needed. */
std::string Link::fold_key(int mod, const Contrib &c) const
{
    std::string k = c.name;
    char b[64];
    snprintf(b, sizeof b, "|%x|%x|%u|", c.size, c.flags, (unsigned)c.relocs.size());
    k += b;
    if (!c.data.empty()) k.append((const char *)&c.data[0], c.data.size());
    for (size_t i = 0; i < c.relocs.size(); i++) {
        snprintf(b, sizeof b, "|%u,%u,", c.relocs[i].offset, (unsigned)c.relocs[i].type);
        k += b;
        k += target_key(mod, c.relocs[i]);
    }
    return k;
}

/*  /OPT:ICF. A COMDAT is the compiler's word that a definition may appear more
 *  than once and the linker may keep one; two whose bytes and targets agree are
 *  the same function, however they were named, and one copy will do.
 *
 *  This is where link.exe and gcc part company: gcc folds in the compiler
 *  (-fipa-icf), Microsoft in the linker, and this follows Microsoft. It is
 *  also, strictly, non-conforming - two functions end up sharing an address,
 *  so `&f == &g` where C++ says they differ - which is exactly why /OPT:NOICF
 *  exists and why /DEBUG turns it off.
 *
 *  ASSOCIATIVE children go with their parent - the .pdata and .xdata describing
 *  a function that no longer has bytes would only duplicate the survivor's -
 *  and each is folded onto the survivor's own child rather than merely
 *  dropped, so that a symbol defined in one still answers. */
bool Link::fold_identical()
{
    if (!opt.opticf) return true;

    size_t folded = 0;
    for (int round = 0; round < 8; round++) {
        std::map<std::string, std::pair<int, int> > first;
        size_t here = 0;

        for (size_t mi = 0; mi < mods.size(); mi++) {
            for (size_t si = 0; si < mods[mi].secs.size(); si++) {
                Contrib &c = mods[mi].secs[si];
                if (c.dropped || !c.live) continue;
                if (!(c.flags & SCN_LNK_COMDAT)) continue;
                if (c.select == COMDAT_ASSOCIATIVE) continue;   /* follows its parent */
                if (c.data.empty() || c.size == 0) continue;

                /*  **Only what is never written at run time.** Folding is
                 *  sound for bytes nobody changes - code, and read-only data.
                 *  A writable section is storage, and two pieces of storage
                 *  that happen to start out alike are still two. */
                if (c.flags & SCN_MEM_WRITE) continue;

                /*  The import tables are the case that proves it. The loader
                 *  writes each .idata$5 word as it binds, and the short import
                 *  members are COMDAT here so /OPT:REF can drop the unused, so
                 *  without this they fold together and every import after the
                 *  first reads one address. p03 and p09 said so. */
                if (c.name.compare(0, 6, ".idata") == 0) continue;

                std::string k = fold_key((int)mi, c);
                std::map<std::string, std::pair<int, int> >::iterator it = first.find(k);
                if (it == first.end()) {
                    first.insert(std::make_pair(k, std::make_pair((int)mi, (int)si)));
                    continue;
                }
                if (it->second.first == (int)mi && it->second.second == (int)si) continue;

                const int sm = it->second.first, ss = it->second.second;
                c.fold_mod = sm;
                c.fold_sec = ss;
                c.dropped = true;

                /*  **A child follows its parent to the survivor, not to
                 *  nothing.** Dropping it is only half the job: a symbol
                 *  defined in it must still answer, so it needs the survivor's
                 *  matching child as its fold target the way the parent has
                 *  the survivor. The .pdata and .xdata that first motivated
                 *  this hold no symbol anyone references, which is why leaving
                 *  fold_mod unset survived 6,500 links - until a catch
                 *  funclet, which is code in a `.text$x` of its own with a
                 *  `$catch$0` symbol the parent's .xdata points at, folded and
                 *  left that symbol in a section that was left out.
                 *
                 *  Children are paired by name and by their order among the
                 *  children of that name: two catch blocks in one function
                 *  give two `.text$x`, and the first must answer to the first.
                 *  A child with no counterpart is left standing - the survivor
                 *  cannot speak for what it does not have. */
                for (size_t k2 = 0; k2 < mods[mi].secs.size(); k2++) {
                    Contrib &a = mods[mi].secs[k2];
                    if (a.dropped || a.select != COMDAT_ASSOCIATIVE) continue;
                    if (a.assoc != (int)si + 1) continue;

                    int nth = 0;                       /* which child of this name it is */
                    for (size_t k3 = 0; k3 < k2; k3++) {
                        const Contrib &b = mods[mi].secs[k3];
                        if (b.select == COMDAT_ASSOCIATIVE && b.assoc == (int)si + 1 && b.name == a.name)
                            nth++;
                    }

                    int mate = -1, seen = 0;
                    for (size_t k3 = 0; k3 < mods[sm].secs.size(); k3++) {
                        const Contrib &b = mods[sm].secs[k3];
                        if (b.dropped || b.select != COMDAT_ASSOCIATIVE) continue;
                        if (b.assoc != ss + 1 || b.name != a.name) continue;
                        if (seen++ == nth) { mate = (int)k3; break; }
                    }
                    if (mate < 0) continue;            /* nothing to answer for it: keep it */

                    a.fold_mod = sm;
                    a.fold_sec = mate;
                    a.dropped = true;
                }
                here++;
                if (opt.verbose)
                    fprintf(stderr, "fold %s #%d into %s #%d\n", mods[mi].name.c_str(), (int)si + 1,
                            mods[it->second.first].name.c_str(), it->second.second + 1);
            }
        }
        folded += here;
        if (here == 0) break;
    }

    if (opt.verbose) fprintf(stderr, "icf: %u contribution(s) folded\n", (unsigned)folded);
    return true;
}
