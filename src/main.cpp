/*  main.cpp - the command line, in link.exe's spelling, and the order of the passes.
 *
 *  Only the switches the probe bed uses are acted on; the rest are accepted and ignored so
 *  that a command line written for link.exe does not have to be rewritten to try this one.
 *  /timestamp: is this linker's own, and exists for the tests: link.exe stamps an image with
 *  the time of day, so holding an image to a reference byte for byte means being able to say
 *  which second to claim.
 */
#include "link.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

static bool eat(const std::string &a, const char *sw, std::string &val)
{
    size_t n = strlen(sw);
    if (a.size() < n + 1) return false;
    for (size_t i = 0; i < n; i++) {
        char c = a[i + 1], s = sw[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != s) return false;
    }
    val = a.substr(n + 1);
    return true;
}

static bool is_switch(const std::string &a) { return !a.empty() && (a[0] == '/' || a[0] == '-'); }

bool Link::run()
{
    if (!read_inputs()) return false;
    if (!lay_out())     return false;
    if (!address())     return false;
    if (!fix_up())      return false;
    return write_image();
}

int main(int argc, char **argv)
{
    Link lk;
    std::string v;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (!is_switch(a)) { lk.opt.inputs.push_back(a); continue; }
        if (eat(a, "out:", v))        { lk.opt.out = v; continue; }
        if (eat(a, "entry:", v))      { lk.opt.entry = v; continue; }
        if (eat(a, "timestamp:", v))  { lk.opt.timestamp = (u32)strtoul(v.c_str(), 0, 16);
                                        lk.opt.have_timestamp = true; continue; }
        if (eat(a, "subsystem:", v))  { lk.opt.subsystem = (v == "windows" || v == "WINDOWS") ? 2 : 3; continue; }
        if (eat(a, "base:", v))       { lk.opt.image_base = strtoull(v.c_str(), 0, 0); continue; }
        if (eat(a, "align:", v))      { lk.opt.section_align = (u32)strtoul(v.c_str(), 0, 0); continue; }
        if (eat(a, "filealign:", v))  { lk.opt.file_align = (u32)strtoul(v.c_str(), 0, 0); continue; }
        if (eat(a, "nodefaultlib", v)) { lk.opt.nodefaultlib = true; continue; }
        if (eat(a, "fixed", v))        { lk.opt.fixed = true; continue; }
        if (eat(a, "dynamicbase", v))  { lk.opt.dynamicbase = true; continue; }
        if (eat(a, "verbose", v))      { lk.opt.verbose = true; continue; }
        if (eat(a, "nologo", v) || eat(a, "map", v) || eat(a, "incremental", v) ||
            eat(a, "debug", v) || eat(a, "machine:", v) || eat(a, "opt:", v) ||
            eat(a, "release", v) || eat(a, "manifest", v)) continue;
        fprintf(stderr, "link: unknown switch %s\n", argv[i]);
        return 2;
    }

    if (lk.opt.inputs.empty()) {
        fprintf(stderr,
            "usage: link /out:image.exe [/entry:sym] [/nodefaultlib] [/fixed]\n"
            "            [/subsystem:console|windows] [/timestamp:hex] object... library...\n");
        return 2;
    }
    if (lk.opt.out.empty()) {
        std::string s = lk.opt.inputs[0];
        size_t d = s.rfind('.');
        lk.opt.out = (d == std::string::npos ? s : s.substr(0, d)) + ".exe";
    }
    if (!lk.opt.have_timestamp) lk.opt.timestamp = (u32)time(0);

    if (!lk.run()) {
        fprintf(stderr, "link: %s\n", lk.err.c_str());
        return 1;
    }
    if (lk.opt.verbose) {
        for (size_t i = 0; i < lk.outs.size(); i++)
            printf("%-8s rva %08x  virtual %6x  raw %6x at %6x\n",
                   lk.outs[i].name.c_str(), lk.outs[i].rva, lk.outs[i].virt_size,
                   lk.outs[i].raw_size, lk.outs[i].fileoff);
    }
    return 0;
}
