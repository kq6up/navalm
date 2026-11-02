/* SPDX-License-Identifier: MIT */
#ifndef NAVCHRONO_SIGHT_H
#define NAVCHRONO_SIGHT_H
#include "navchrono.h"
#include "navfltk_model.h"
extern "C" {
#include "navsky.h"
}
namespace chrono_nav {
inline double epoch(const Sight &s){
    // Keep the fractional second out of the large Julian-date addition.
    return (nav_julian_date(s.y,s.mo,s.d,0,0,0)-2440587.5)*86400.+s.h*3600.+s.mi*60.+s.s;
}
inline void set_time(Sight &s,double utc){
    if(!std::isfinite(utc)||utc < -11676096000. || utc >= 13569465600.)throw std::runtime_error("Corrected UTC is outside 1600–2399.");
    auto ms=static_cast<long long>(std::llround(utc*1000.));
    auto day=static_cast<long long>(std::floor(double(ms)/86400000.));
    auto part=ms-day*86400000;
    char date[32],unused[32],time[32];navsky_utc(2440587.5+double(day),date,sizeof date,unused,sizeof unused);
    std::snprintf(time,sizeof time,"%02d:%02d:%06.3f",int(part/3600000),int((part/60000)%60),double(part%60000)/1000.);
    desktop::date_time(date,time,s);
}
inline std::string stamp(double t){
    Sight s{};set_time(s,t);char b[40];std::snprintf(b,sizeof b,"%04d-%02d-%02dT%02d:%02d:%06.3f",s.y,s.mo,s.d,s.h,s.mi,s.s);return b;
}
inline double parse(const std::string &date,const std::string &time){Sight s{};desktop::date_time(date,time,s);return epoch(s);}
inline void correct(Sight &s,const Clock &c,const Model &m){
    double watch=epoch(s),utc=m.utc_from_watch(watch);
    if(std::abs(utc-watch)>604800.)throw std::runtime_error("Clock correction exceeds seven days; check the dates and clock series.");
    auto raw=stamp(watch);set_time(s,utc);
    std::snprintf(s.clock_name,sizeof s.clock_name,"%s",c.name.c_str());
    std::snprintf(s.clock_watch,sizeof s.clock_watch,"%s",raw.c_str());
    s.clock_correction_sec=epoch(s)-watch;
}
}
#endif
