/* SPDX-License-Identifier: MIT */
#include "navbook.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <vector>
static void require(bool p,const char *s) { if(!p) throw std::runtime_error(s); }
int main() {
    try {
        navbook::Ageton t;
        require(t.a(30)==301030,"Six-place scale at 30 degrees");
        require(t.a(0)>1e10 && t.b(90)>1e10,"Singular endpoints");
        require(t.a(25)==t.b(65),"Complement identity");
        require(t.a(120)==t.a(60),"Obtuse magnitudes");
        std::mt19937_64 rng(2112027);
        std::uniform_real_distribution<double> lat(-70,70),dec(-30,30),lha(0,360);
        constexpr double rad=3.14159265358979323846/180;
        for(bool full_sky:{false,true}) for(bool interp:{false,true}) {
            std::vector<double> errors,azerrors; size_t excluded=0;
            double maxe=0,wl=0,wd=0,wh=0;
            rng.seed(2112027);
            for(int i=0;i<200000;++i) {
                double l=lat(rng),d=dec(rng),h=lha(rng);
                if(full_sky) {l=l/70*89;d=d/30*89;}
                auto r=t.reduce(l,d,h,interp);
                if(!r.usable) { ++excluded; continue; }
                double u=std::sin(l*rad)*std::sin(d*rad)+std::cos(l*rad)*std::cos(d*rad)*std::cos(h*rad);
                double n=std::cos(l*rad)*std::sin(d*rad)-std::sin(l*rad)*std::cos(d*rad)*std::cos(h*rad);
                double e=-std::cos(d*rad)*std::sin(h*rad);
                double hc=std::atan2(u,std::hypot(n,e))/rad;
                double zn=std::atan2(e,n)/rad; if(zn<0) zn+=360;
                double err=std::abs(r.altitude-hc)*60;
                double az=std::abs(std::remainder(r.azimuth-zn,360.))*60;
                if(err>maxe) { maxe=err;wl=l;wd=d;wh=h; }
                errors.push_back(err);azerrors.push_back(az);
            }
            std::sort(errors.begin(),errors.end()); std::sort(azerrors.begin(),azerrors.end());
            std::printf("Ageton %s %s: accepted %zu/200000; excluded %zu; Hc p50 %.3f p99 %.3f max %.3f arcmin; Zn max %.3f arcmin\n",
                full_sky?"full-sky":"solar-range",interp?"interpolated":"nearest row",errors.size(),excluded,errors[errors.size()/2],errors[errors.size()*99/100],errors.back(),azerrors.back());
            std::printf("Worst Hc: lat %.8f dec %.8f LHA %.8f\n",wl,wd,wh);
            require(errors.size()>70000,"Sufficient accepted test domain");
            require(errors.back()<(interp?.35:1.1),"Altitude error regression");
            require(azerrors.back()<(interp?4:8),"Azimuth quadrant regression");
        }
        for(auto c:{std::pair<double,double>{60,300},{120,240}}) {
            auto a=t.reduce(-20,15,c.first,true),b=t.reduce(-20,15,c.second,true);
            if(a.usable&&b.usable) { require(std::abs(a.altitude-b.altitude)<1e-7,"East/west symmetry");
                require(std::abs(a.azimuth+b.azimuth-360)<1e-7,"East/west azimuth"); }
        }
        navbook::Options o; o.year=2027;
        o.sun_moon=o.planets=o.stars=o.events=o.increments=o.corrections=false;
        o.reduction=navbook::Reduction::Ageton;
        auto pdf=navbook::generate(o);
        require(pdf.find("%PDF-1.4")==0,"Native PDF header");
        require(pdf.find("/Count 28 ")!=std::string::npos,"Two opening pages, 25 Ageton pages and index");
        o.format=navbook::Format::Latex;
        auto tex=navbook::generate(o);
        require(tex.find("\\documentclass[10pt]{article}")!=std::string::npos &&
                tex.find("\\end{document}")!=std::string::npos,"Standalone LaTeX document");
        size_t pictures=0,position=0;
        while((position=tex.find("\\begin{picture}",position))!=std::string::npos) {++pictures;++position;}
        require(pictures==28,"LaTeX preserves compact Ageton page count");
        require(tex.find("\\_")!=std::string::npos && tex.find("{-}{-}")!=std::string::npos,
                "LaTeX worksheet underscores and literal hyphens escaped");
        require(tex.find("\\pdfbookmark[0]")!=std::string::npos,"LaTeX section bookmarks");
        o.format=navbook::Format::Pdf;
        bool cancelled=false;
        try { navbook::generate(o,[](int,const std::string &){return false;}); }
        catch(const std::exception&) {cancelled=true;}
        require(cancelled,"Cancellation");
        for(int year:{1599,2201}) {
            bool rejected=false; o.year=year;
            try { navbook::generate(o); } catch(const std::exception &) {rejected=true;}
            require(rejected,"Unsupported year rejected");
        }
        o.year=2027; o.reduction=navbook::Reduction::None;
        bool rejected=false;
        try { navbook::generate(o); } catch(const std::exception &) {rejected=true;}
        require(rejected,"Empty section selection rejected");
        // Reference-only books must work without loading an ephemeris.
        auto reference=o; reference.increments=true; reference.shade_rows=true;
        auto combined=navbook::generate(reference);
        require(combined.find("/Count 63 ")!=std::string::npos,
                "Sixty combined minute pages plus introduction and index");
        reference.format=navbook::Format::Latex;
        auto combined_tex=navbook::generate(reference);
        require(combined_tex.find("SECONDS\\ CORRECTION")!=std::string::npos,
                "Combined LaTeX export includes seconds lookup");
        auto dir=std::filesystem::temp_directory_path()/std::filesystem::u8path("navalm-book-test-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(dir);
        try {
            auto path=dir/std::filesystem::u8path(u8"almanac-é.pdf");
            navbook::save(path.u8string(),"previous destination");
            navbook::save(path.u8string(),pdf);
            std::ifstream in(path,std::ios::binary);
            std::string saved((std::istreambuf_iterator<char>(in)),{});
            require(saved==pdf,"Atomic replacement and Unicode destination");
            in.close(); rejected=false;
            try {navbook::save(dir.u8string(),pdf);} catch(const std::exception &) {rejected=true;}
            require(rejected && std::filesystem::is_directory(dir),"Failed install preserves destination");
            require(std::distance(std::filesystem::directory_iterator(dir),std::filesystem::directory_iterator{})==1,
                    "Failed install removes temporary file");
        } catch(...) {std::filesystem::remove_all(dir);throw;}
        std::filesystem::remove_all(dir);
        std::puts("Annual book numerical, PDF and LaTeX structure checks passed.");
    } catch(const std::exception &e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
