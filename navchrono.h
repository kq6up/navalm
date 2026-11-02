/* SPDX-License-Identifier: MIT
 * Clock error = watch - UTC. Times are Unix seconds on the UTC civil scale.
 * The model is fitted against reference UTC, then inverted for watch readings.
 */
#ifndef NAVCHRONO_H
#define NAVCHRONO_H
#include "navfile.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace chrono_nav {
struct Clock { int id=0,series=1;std::string name; };
struct Reading {
    int clock=0,series=1;double utc=0,watch=0;bool enabled=true;
    std::string source="Manual WWV",note;
};
struct Model {
    size_t count=0;double epoch=0,error=0,rate=0,rms=0,first=0,last=0;
    double error_at(double utc) const {return error+rate*((utc-epoch)/86400.);}
    double utc_from_watch(double watch) const {
        if(!count)throw std::runtime_error("Record a clock comparison before applying Chrono.");
        if(!std::isfinite(watch))throw std::runtime_error("Invalid watch time.");
        return epoch+((watch-epoch)-error)/(1+rate/86400.);
    }
};
inline void text_ok(const std::string &s,size_t limit,bool required=false) {
    if(s.size()>limit || (required && s.find_first_not_of(" \t")==std::string::npos) ||
       std::any_of(s.begin(),s.end(),[](unsigned char c){return c<32;}))
        throw std::runtime_error("Use a short name or note without control characters.");
}
inline Model fit(const std::vector<Reading> &rows,int clock,int series) {
    Model m;double sum_t=0,sum_e=0,origin=0;
    for(const auto &r:rows)if(r.clock==clock && r.series==series && r.enabled){
        if(!m.count){origin=r.utc;m.first=m.last=r.utc;}
        ++m.count;sum_t+=(r.utc-origin)/86400.;sum_e+=r.watch-r.utc;
        m.first=std::min(m.first,r.utc);m.last=std::max(m.last,r.utc);
    }
    if(!m.count)return m;
    m.epoch=origin+sum_t/m.count*86400.;m.error=sum_e/m.count;
    double xx=0,xy=0;
    for(const auto &r:rows)if(r.clock==clock && r.series==series && r.enabled){
        double x=(r.utc-m.epoch)/86400.,y=(r.watch-r.utc)-m.error;xx+=x*x;xy+=x*y;
    }
    if(xx>1e-18)m.rate=xy/xx;
    if(!std::isfinite(m.rate) || std::abs(m.rate)>3600)
        throw std::runtime_error("Clock drift exceeds one hour/day; check dates or start a new series after a reset.");
    double ss=0;
    for(const auto &r:rows)if(r.clock==clock && r.series==series && r.enabled){
        double residual=(r.watch-r.utc)-m.error_at(r.utc);ss+=residual*residual;
    }
    m.rms=std::sqrt(ss/m.count);return m;
}
struct Log {
    std::vector<Clock> clocks;std::vector<Reading> readings;int selected=0;bool apply_new=false;
    const Clock &clock(int id) const {
        auto i=std::find_if(clocks.begin(),clocks.end(),[&](const Clock &c){return c.id==id;});
        if(i==clocks.end())throw std::runtime_error("Select a Chrono clock first.");return *i;
    }
    Clock &clock(int id){return const_cast<Clock&>(static_cast<const Log*>(this)->clock(id));}
    int add_clock(const std::string &name) {
        text_ok(name,31,true);
        if(name.find_first_of("\"\\")!=std::string::npos)throw std::runtime_error("Clock names cannot contain quotes or backslashes.");
        for(const auto &c:clocks)if(c.name==name)throw std::runtime_error("A clock already has that name.");
        int id=1;for(const auto &c:clocks)id=std::max(id,c.id+1);
        if(clocks.size()>=100)throw std::runtime_error("The clock log is limited to 100 clocks.");
        clocks.push_back({id,1,name});selected=id;return id;
    }
    void add(Reading r) {
        const auto &c=clock(r.clock);r.series=c.series;
        text_ok(r.source,40,true);text_ok(r.note,160);
        if(!std::isfinite(r.utc)||!std::isfinite(r.watch)||r.utc < -11676096000. || r.utc > 13569465600. ||
           r.watch < -11676096000. || r.watch > 13569465600. || std::abs(r.watch-r.utc)>604800.)
            throw std::runtime_error("Check the reference and watch dates; their difference must be within seven days.");
        for(const auto &old:readings)if(old.enabled && old.clock==r.clock && old.series==r.series && std::abs(old.utc-r.utc)<.0005)
            throw std::runtime_error("This reference instant is already logged. Exclude the old reading before entering its replacement.");
        if(readings.size()>=20000)throw std::runtime_error("The clock log is full (20,000 comparisons).");
        readings.push_back(r);
    }
    Model model(int id) const {const auto &c=clock(id);return fit(readings,id,c.series);}
    void set_enabled(size_t index,bool enabled) {
        auto &r=readings.at(index);
        if(enabled)for(size_t i=0;i<readings.size();++i){
            const auto &other=readings[i];
            if(i!=index && other.enabled && other.clock==r.clock && other.series==r.series && std::abs(other.utc-r.utc)<.0005)
                throw std::runtime_error("Exclude the replacement comparison before restoring this reading.");
        }
        r.enabled=enabled;
    }
    void save(const std::filesystem::path &path) const {
        std::filesystem::create_directories(path.parent_path());
        auto tmp=path;tmp+=".tmp";std::ofstream out(tmp,std::ios::binary);
        out<<"NAVALM_CHRONO 1\nsettings "<<selected<<' '<<apply_new<<'\n'<<std::setprecision(17);
        for(const auto &c:clocks)out<<"clock "<<c.id<<' '<<c.series<<' '<<std::quoted(c.name)<<'\n';
        for(const auto &r:readings)out<<"reading "<<r.clock<<' '<<r.series<<' '<<r.utc<<' '<<r.watch<<' '<<r.enabled<<' '<<std::quoted(r.source)<<' '<<std::quoted(r.note)<<'\n';
        out.flush();if(!out)throw std::runtime_error("Cannot write the Chrono log.");out.close();
        if(nav_replace_file(tmp.u8string().c_str(),path.u8string().c_str()))throw std::runtime_error("Cannot replace the Chrono log.");
    }
    static Log load(const std::filesystem::path &path) {
        std::ifstream in(path);if(!in){if(std::filesystem::exists(path))throw std::runtime_error("Cannot read Chrono log.");return {};}
        Log log;std::string line;if(!std::getline(in,line)||line!="NAVALM_CHRONO 1")throw std::runtime_error("Unrecognized Chrono log; original file preserved.");
        bool settings=false;
        while(std::getline(in,line)){
            if(line.empty())continue;std::istringstream row(line);std::string kind;row>>kind;
            if(kind=="settings") {if(settings || !(row>>log.selected>>log.apply_new))throw std::runtime_error("Invalid Chrono settings.");settings=true;}
            else if(kind=="clock"){
                Clock c;if(!(row>>c.id>>c.series>>std::quoted(c.name)) || c.id<1 || c.series<1 || c.series>100000 || log.clocks.size()>=100)throw std::runtime_error("Invalid clock record.");
                text_ok(c.name,31,true);if(c.name.find_first_of("\"\\")!=std::string::npos)throw std::runtime_error("Invalid clock name.");
                for(const auto &old:log.clocks)if(old.id==c.id||old.name==c.name)throw std::runtime_error("Duplicate clock record.");log.clocks.push_back(c);
            } else if(kind=="reading"){
                Reading r;if(!(row>>r.clock>>r.series>>r.utc>>r.watch>>r.enabled>>std::quoted(r.source)>>std::quoted(r.note)))throw std::runtime_error("Invalid clock comparison.");
                const auto &c=log.clock(r.clock);if(r.series<1||r.series>c.series)throw std::runtime_error("Invalid clock series.");
                text_ok(r.source,40,true);text_ok(r.note,160);
                if(!std::isfinite(r.utc)||!std::isfinite(r.watch)||std::abs(r.watch-r.utc)>604800. || r.utc < -11676096000. || r.utc > 13569465600. || r.watch < -11676096000. || r.watch > 13569465600. || log.readings.size()>=20000)throw std::runtime_error("Invalid comparison date/time.");
                log.readings.push_back(r);
            } else throw std::runtime_error("Unrecognized Chrono record.");
            row>>std::ws;if(!row.eof())throw std::runtime_error("Extra data in Chrono record.");
        }
        if(!in.eof() || !settings)throw std::runtime_error("Incomplete Chrono log.");
        if(log.selected)log.clock(log.selected);return log;
    }
};
}
#endif
