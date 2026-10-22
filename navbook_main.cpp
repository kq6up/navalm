/* SPDX-License-Identifier: MIT */
#include "navbook.h"
extern "C" {
#include "nav_engine.h"
}
#include <cstdio>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
    try {
        navbook::Options o;
        std::string output;
        for(int i=1;i<argc;++i) {
            std::string a=argv[i];
            auto value=[&]() { if(++i>=argc) throw std::runtime_error("Missing value for "+a); return std::string(argv[i]); };
            if(a=="--help") {
                std::puts("NAVALM annual practice-book builder (5.1 development)\n"
                  "Usage: navalm-book --year YYYY --output PATH [options]\n"
                  "  --format pdf|latex  (latex writes editable .tex source)\n"
                  "  --shade-rows  (light gray alternating hourly and reference rows)\n"
                  "  --engine precise|classic   --ephemeris FILE\n"
                  "  --no-sun-moon --no-planets --no-stars --no-events\n"
                  "  --no-increments --no-corrections\n"
                  "  --reduction both|ageton|formulas|none\n"
                  "  --ageton-only  (standalone compact table booklet)\n"
                  "The concise formula guide is not the NAO Concise lookup tables."); return 0;
            } else if(a=="--year") {
                auto s=value(); size_t n=0; o.year=std::stoi(s,&n);
                if(n!=s.size()) throw std::runtime_error("Year must be a whole number.");
            } else if(a=="--output") output=value();
            else if(a=="--shade-rows") o.shade_rows=true;
            else if(a=="--format") {
                auto f=value();
                if(f=="pdf") o.format=navbook::Format::Pdf;
                else if(f=="latex") o.format=navbook::Format::Latex;
                else throw std::runtime_error("Choose pdf or latex format.");
            }
            else if(a=="--engine") { if(nav_engine_select(value().c_str())) throw std::runtime_error("Unknown engine."); }
            else if(a=="--ephemeris") nav_engine_set_ephemeris_path(value().c_str());
            else if(a=="--no-sun-moon") o.sun_moon=false;
            else if(a=="--no-planets") o.planets=false;
            else if(a=="--no-stars") o.stars=false;
            else if(a=="--no-events") o.events=false;
            else if(a=="--no-increments") o.increments=false;
            else if(a=="--no-corrections") o.corrections=false;
            else if(a=="--ageton-only") {
                o.sun_moon=o.planets=o.stars=o.events=o.increments=o.corrections=false;
                o.reduction=navbook::Reduction::Ageton;
            } else if(a=="--reduction") {
                auto v=value();
                if(v=="both") o.reduction=navbook::Reduction::Both;
                else if(v=="ageton") o.reduction=navbook::Reduction::Ageton;
                else if(v=="formulas") o.reduction=navbook::Reduction::Formulas;
                else if(v=="none") o.reduction=navbook::Reduction::None;
                else throw std::runtime_error("Choose both, ageton, formulas, or none.");
            } else throw std::runtime_error("Unknown option: "+a);
        }
        if(output.empty()) throw std::runtime_error("Specify --output book.pdf; use --help for options.");
        int last=-1;
        auto pdf=navbook::generate(o,[&last](int pct,const std::string &part) {
            if(pct/10!=last/10 || last<0 || pct==100) {
                std::fprintf(stderr,"%3d%% %s\n",pct,part.c_str()); last=pct;
            }
            return true;
        });
        navbook::save(output,pdf);
        std::fprintf(stderr,"Saved %s (%zu bytes)\n",output.c_str(),pdf.size());
        nav_engine_shutdown(); return 0;
    } catch(const std::exception &e) {
        std::fprintf(stderr,"Book: %s\n",e.what()); nav_engine_shutdown(); return 2;
    }
}
