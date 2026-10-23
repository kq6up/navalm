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
// Execute the PRINTED traditional K/K~L instructions independently of reduce().
// All lookups use the same rounded integers that appear in the book.
struct PencilResult { double hc, zn, k, separation; bool ready, caution; };
static PencilResult pencil(const navbook::Ageton &tab,double lat,double dec,double lha,bool interp) {
    double t=lha<180?lha:360-lha;
    double entry=t>90?180-t:t;
    double ar=tab.a(entry,interp)+tab.b(dec,interp);
    double r=tab.inverse_a(ar,interp),br=tab.b(r,interp);
    double ak=tab.a(dec,interp)-br;
    double acute=ak>=150515?tab.inverse_a(ak,interp):
        tab.inverse_b(tab.b(dec,interp)+tab.b(entry,interp)-br,interp);
    double k=t<90?acute:180-acute;
    bool same=std::signbit(lat)==std::signbit(dec);
    double separation=same?std::abs(k-std::abs(lat)):k+std::abs(lat);
    require(separation<90,"Printed K~L horizon rule");
    double hc=tab.inverse_a(br+tab.b(separation,interp),interp),bh=tab.b(hc,interp);
    double az=ar-bh;
    double zacute=az>=150515?tab.inverse_a(az,interp):
        tab.inverse_b(br+tab.a(separation,interp)-bh,interp);
    double z=same&&k>std::abs(lat)?zacute:180-zacute;
    bool east=lha>180;
    double zn=lat<0?(east?180-z:180+z):(east?z:360-z);
    bool ready=std::isfinite(ar)&&std::isfinite(ak)&&std::isfinite(br)&&std::isfinite(bh);
    if(ak<150515) ready=ready&&std::isfinite(tab.b(entry,interp));
    if(az<150515) ready=ready&&std::isfinite(tab.a(separation,interp));
    bool caution=false;
    for(double a:{entry,std::abs(dec),r,acute,separation,hc,zacute}) {
        if(a>90) a=180-a;
        caution=caution || a<3 || a>87;
    }
    return {hc,zn,k,separation,ready,caution};
}
int main() {
    try {
        navbook::Ageton t;
        require(t.a(30)==301030,"Million-scale logarithm at 30 degrees");
        require(t.a(0)>1e10 && t.b(90)>1e10,"Singular endpoints");
        require(t.b(60)==t.a(30) && t.a(60)==t.b(30),"c-side reads opposite letter");
        require(t.a(25)==t.b(65),"Complement identity");
        require(t.a(120)==t.a(60),"Obtuse magnitudes");
        std::mt19937_64 rng(2112027);
        std::uniform_real_distribution<double> lat(-70,70),dec(-30,30),lha(0,360);
        constexpr double rad=3.14159265358979323846/180;
        for(bool full_sky:{false,true}) for(bool interp:{false,true}) {
            std::vector<double> errors,azerrors; size_t excluded=0;
            double maxe=0,wl=0,wd=0,wh=0;
            size_t branches[8]={},zone_count[2]={},paper_stops=0;
            double zone_hc[2]={},zone_az[2]={};
            rng.seed(2112027);
            for(int i=0;i<200000;++i) {
                double l=lat(rng),d=dec(rng),h=lha(rng);
                if(full_sky) {l=l/70*89;d=d/30*89;}
                auto r=t.reduce(l,d,h,interp);
                if(!r.usable) { ++excluded; continue; }
                auto written=pencil(t,l,d,h,interp);
                require(std::abs(written.hc-r.altitude)<1e-5,"Printed altitude agrees with engine");
                require(std::abs(std::remainder(written.zn-r.azimuth,360.))<1e-5,
                        "Printed elevated-pole quadrant rules agree with engine");
                if(!written.ready) ++paper_stops;
                ++branches[(l<0?4:0)+(h>180?2:0)+(std::min(h,360-h)>90?1:0)];
                double u=std::sin(l*rad)*std::sin(d*rad)+std::cos(l*rad)*std::cos(d*rad)*std::cos(h*rad);
                double n=std::cos(l*rad)*std::sin(d*rad)-std::sin(l*rad)*std::cos(d*rad)*std::cos(h*rad);
                double e=-std::cos(d*rad)*std::sin(h*rad);
                double hc=std::atan2(u,std::hypot(n,e))/rad;
                double zn=std::atan2(e,n)/rad; if(zn<0) zn+=360;
                double err=std::abs(r.altitude-hc)*60;
                double az=std::abs(std::remainder(r.azimuth-zn,360.))*60;
                if(written.ready) {
                    auto zone=written.caution?1:0;
                    ++zone_count[zone];zone_hc[zone]=std::max(zone_hc[zone],err);
                    zone_az[zone]=std::max(zone_az[zone],az);
                }
                if(err>maxe) { maxe=err;wl=l;wd=d;wh=h; }
                errors.push_back(err);azerrors.push_back(az);
            }
            std::sort(errors.begin(),errors.end()); std::sort(azerrors.begin(),azerrors.end());
            std::printf("Ageton %s %s: accepted %zu/200000; excluded %zu; Hc p50 %.3f p99 %.3f max %.3f arcmin; Zn max %.3f arcmin\n",
                full_sky?"full-sky":"solar-range",interp?"interpolated":"nearest row",errors.size(),excluded,errors[errors.size()/2],errors[errors.size()*99/100],errors.back(),azerrors.back());
            std::printf("Worst Hc: lat %.8f dec %.8f LHA %.8f\n",wl,wd,wh);
            std::printf("Printed caution audit: regular %zu max Hc %.3f / Zn %.3f arcmin; caution %zu max Hc %.3f / Zn %.3f; paper STOP %zu\n",
                zone_count[0],zone_hc[0],zone_az[0],zone_count[1],zone_hc[1],zone_az[1],paper_stops);
            require(zone_count[0]>0 && zone_count[1]>0,"Both regular and caution zones audited");
            for(auto n:branches) require(n>0,"Every printed hemisphere/EW/t branch exercised");
            require(errors.size()>70000,"Sufficient accepted test domain");
            require(errors.back()<(interp?.35:1.1),"Altitude error regression");
            require(azerrors.back()<(interp?4:8),"Azimuth quadrant regression");
        }
        for(auto c:{std::pair<double,double>{60,300},{120,240}}) {
            auto a=t.reduce(-20,15,c.first,true),b=t.reduce(-20,15,c.second,true);
            if(a.usable&&b.usable) { require(std::abs(a.altitude-b.altitude)<1e-7,"East/west symmetry");
                require(std::abs(a.azimuth+b.azimuth-360)<1e-7,"East/west azimuth"); }
        }
        for(double hemisphere:{-1.,1.}) for(double lha:{120.,240.}) {
            auto w=pencil(t,75*hemisphere,20*hemisphere,lha,true);
            require(w.k>90 && w.separation<90,"Obtuse K, positive K~L above-horizon example");
        }
        // Deliberately sample near danger-zone and singular boundaries in addition
        // to the broad random audit. A printed INF is a stop even if a software
        // limiting value could still be computed.
        size_t edge_ok=0,edge_stop=0;
        double edge_hc=0,edge_zn=0;
        for(bool interp:{false,true})
        for(double a:{.001,.004,.0082,.0084,.02,.05,1.,2.9,3.,3.1,86.9,87.,87.1,89.99,
                      90.01,92.9,93.,93.1,176.9,177.,177.1,179.99})
        for(double lat:{-80.,-60.,-30.,-5.,0.,5.,30.,60.,80.})
        for(double dec:{-60.,-23.,-3.,-.00834,.00834,3.,23.,60.})
        for(double h:{a,360-a}) {
            auto r=t.reduce(lat,dec,h,interp);
            if(!r.usable) {++edge_stop;continue;}
            auto w=pencil(t,lat,dec,h,interp);
            if(!w.ready) {++edge_stop;continue;}
            double hc=std::asin(std::sin(lat*rad)*std::sin(dec*rad)+
                std::cos(lat*rad)*std::cos(dec*rad)*std::cos(h*rad))/rad;
            double zn=std::atan2(-std::cos(dec*rad)*std::sin(h*rad),
                std::cos(lat*rad)*std::sin(dec*rad)-std::sin(lat*rad)*std::cos(dec*rad)*std::cos(h*rad))/rad;
            edge_hc=std::max(edge_hc,std::abs(w.hc-hc)*60);
            edge_zn=std::max(edge_zn,std::abs(std::remainder(w.zn-zn,360.))*60);
            ++edge_ok;
        }
        require(edge_ok>0 && edge_stop>0,"Boundary usable and stop cases exercised");
        std::printf("Targeted boundaries: %zu usable, %zu stopped; max Hc %.3f / Zn %.3f arcmin (sample, not a guarantee)\n",
            edge_ok,edge_stop,edge_hc,edge_zn);
        navbook::Options o; o.year=2027;
        o.sun_moon=o.planets=o.stars=o.events=o.increments=o.corrections=false;
        o.reduction=navbook::Reduction::Ageton;
        auto pdf=navbook::generate(o);
        require(pdf.find("%PDF-1.4")==0,"Native PDF header");
        require(pdf.find("/Count 29 ")!=std::string::npos,"Two opening pages, 26 Ageton pages and index");
        o.format=navbook::Format::Latex;
        auto tex=navbook::generate(o);
        require(tex.find("\\documentclass[10pt]{article}")!=std::string::npos &&
                tex.find("\\end{document}")!=std::string::npos,"Standalone LaTeX document");
        size_t pictures=0,position=0;
        while((position=tex.find("\\begin{picture}",position))!=std::string::npos) {++pictures;++position;}
        require(pictures==29,"LaTeX preserves compact Ageton page count");
        require(tex.find("{-}{-}")!=std::string::npos,
                "LaTeX literal hyphens escaped");
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
