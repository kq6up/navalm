/* SPDX-License-Identifier: MIT — native Lunars panel; calculations in navlunar.c. */
#ifndef NAVFLTK_LUNAR_H
#define NAVFLTK_LUNAR_H
#include "navchrono_sight.h"
extern "C" {
#include "navlunar.h"
}
#include <FL/Fl_Group.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <functional>
#include <memory>
#include <fstream>

class LunarPanel : public Fl_Group {
public:
    struct Altitude {Fl_Check_Button *measured;Fl_Input *hs,*date,*time,*height;Fl_Choice *limb,*horizon,*units;Fl_Button *setup;Sight settings{};bool shared=true;} altitude[2];
    Fl_Input *date,*time,*distance,*lat,*lon,*ie,*temp,*pressure,*elevation,*waterline,*initial,*span,*sigma,*alt_sigma;
    Fl_Choice *body,*star,*moon_contact,*other_contact,*ns,*ew,*arc,*solution;
    Fl_Check_Button *refine,*motion;
    Fl_Button *send,*export_button;
    Fl_Box *summary;
    Fl_Text_Buffer buffer;Fl_Text_Display *report;
    NavLunarRequest saved{};NavLunarResult result{};bool ready=false,testing=false;
    std::function<void(const std::string&,bool)> notify;
    std::function<Sight()> current_position;
    std::function<double(double)> chrono_estimate;
    std::function<void(const NavLunarRequest&,const NavLunarSolution&)> chrono_transfer;
    std::vector<std::unique_ptr<std::function<void()>>> callbacks;
    Sight settings{};
    static std::string raw_stamp(const Sight &s){char b[64];std::snprintf(b,sizeof b,"%04d-%02d-%02dT%02d:%02d:%09.6f",s.y,s.mo,s.d,s.h,s.mi,s.s);return b;}
    static std::string num(double v,int decimals=6){std::ostringstream o;o<<std::fixed<<std::setprecision(decimals)<<v;return o.str();}
    void guard(const std::function<void()> &fn){try{fn();}catch(const std::exception &e){if(notify)notify(e.what(),true);}}
    void bind(Fl_Widget *w,std::function<void()> fn){
        callbacks.push_back(std::make_unique<std::function<void()>>([this,fn]{guard(fn);}));
        w->callback([](Fl_Widget*,void *p){(*static_cast<std::function<void()>*>(p))();},callbacks.back().get());
    }
    Fl_Input *entry(int x,int y,int w,const char *label){
        auto *p=new Fl_Input(x,y,w,30,label);p->align(FL_ALIGN_TOP_LEFT);p->labelsize(11);p->textsize(13);
        p->when(FL_WHEN_CHANGED);bind(p,[this]{invalidate();});return p;
    }
    Fl_Choice *pick(int x,int y,int w,const char *label,const char *items){
        auto *p=new Fl_Choice(x,y,w,30,label);p->align(FL_ALIGN_TOP_LEFT);p->labelsize(11);p->textsize(12);p->add(items);p->value(0);
        bind(p,[this]{state();invalidate();});return p;
    }
    Fl_Button *action(int x,int y,int w,const char *label,std::function<void()> fn){
        auto *p=new Fl_Button(x,y,w,32,label);p->labelsize(12);bind(p,fn);return p;
    }
    void invalidate(){ready=false;send->deactivate();export_button->deactivate();summary->copy_label("Inputs changed. Solve again; original watch readings are retained.");}
    void state(){
        if(body->value()==6){star->activate();other_contact->value(1);other_contact->deactivate();}else{star->deactivate();other_contact->activate();}
        for(auto &a:altitude){for(auto *w:{a.hs,a.date,a.time})if(a.measured->value())w->activate();else w->deactivate();
            if(a.horizon->value()==1){a.height->deactivate();a.units->deactivate();}else{a.height->activate();a.units->activate();}}
        if(altitude[0].measured->value()&&altitude[1].measured->value())refine->activate();else{refine->value(0);refine->deactivate();}
    }
    static std::string chooser(bool save,const char *title,const char *name,const char *filter){
        Fl_Native_File_Chooser f;f.title(title);f.filter(filter);f.type(save?Fl_Native_File_Chooser::BROWSE_SAVE_FILE:Fl_Native_File_Chooser::BROWSE_FILE);
        if(save){f.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);f.preset_file(name);}if(f.show())return "";return f.filename();
    }
    LunarPanel():Fl_Group(16,130,1148,672,"Lunars"){
        begin();
        date=entry(36,174,150,"Watch date · YYYY-MM-DD");time=entry(202,174,142,"Watch time · HH:MM:SS.s");
        distance=entry(360,174,132,"Raw distance · DDD MM.m");
        body=pick(508,174,150,"Other body","Sun|Mercury|Venus|Mars|Jupiter|Saturn|Navigation star");body->value(4);
        star=pick(674,174,190,"Star","");for(size_t i=0;i<nav_star_count_get();i++)star->add(nav_star_name(i));star->value(0);
        moon_contact=pick(880,174,118,"Moon contact","Near|Centre|Far");moon_contact->value(0);
        other_contact=pick(1014,174,118,"Other contact","Near|Centre|Far");other_contact->value(1);
        lat=entry(36,238,132,"AP latitude · DD MM.m");ns=pick(176,238,54,"","N|S");
        lon=entry(246,238,136,"Longitude · DDD MM.m");ew=pick(390,238,54,"","E|W");ew->value(1);
        action(460,238,132,"Load saved DR",[this]{load_dr(false);});action(608,238,132,"Use sight DR",[this]{load_dr(true);});
        refine=new Fl_Check_Button(756,238,376,30,"Use two measured altitudes to refine AP");refine->labelsize(12);bind(refine,[this]{invalidate();});
        for(int i=0;i<2;i++){
            auto &a=altitude[i];int y=302+i*62;
            a.measured=new Fl_Check_Button(36,y,130,30,i?"Other measured":"Moon measured");a.measured->labelsize(12);
            bind(a.measured,[this]{state();invalidate();});
            a.setup=action(169,y,41,"…",[this,i]{altitude_setup(i);});a.setup->tooltip("Altitude index error, weather and short-waterline distance; normally shared with the lunar distance.");
            a.hs=entry(218,y,122,"Hs · DDD MM.m");a.date=entry(356,y,148,"Shot watch date");a.time=entry(520,y,136,"Shot watch time");
            a.limb=pick(672,y,100,"Altitude limb","Lower|Centre|Upper");a.limb->value(i?1:0);
            a.horizon=pick(788,y,150,"Horizon","Sea|Artificial|Short waterline");
            a.height=entry(954,y,82,"Eye height");a.units=pick(1052,y,80,"Units","ft|m");
            a.measured->tooltip("Unchecked: calculate geometry at the lunar epoch; the stored Hs/watch fields are ignored, not overwritten.");
        }
        ie=entry(36,426,76,"IE · minutes");arc=pick(120,426,76,"Arc","Off|On");
        temp=entry(212,426,64,"Temp · C");pressure=entry(292,426,78,"P · hPa");elevation=entry(386,426,92,"Elevation · m");
        waterline=entry(494,426,82,"Waterline · NM");initial=entry(592,426,102,"UTC − watch · s");span=entry(710,426,88,"Search ± s");
        sigma=entry(814,426,86,"Distance σ · ′");alt_sigma=entry(916,426,78,"Altitude σ · ′");
        motion=new Fl_Check_Button(1004,426,128,30,"Use DR motion");motion->labelsize(11);bind(motion,[this]{invalidate();});
        action(36,482,128,"Solve lunar UTC",[this]{solve();});
        action(178,482,162,"Predict at start",[this]{preview();});
        action(354,482,108,"Load…",[this]{load();});action(476,482,108,"Save inputs…",[this]{save();});
        export_button=action(598,482,134,"Export report…",[this]{export_report();});
        action(746,482,164,"Use Chrono estimate",[this]{
            if(!chrono_estimate)throw std::runtime_error("Select a Chrono clock first.");
            double watch=chrono_nav::parse(date->value(),time->value());initial->value(num(chrono_estimate(watch),3).c_str());invalidate();
        });
        send=action(924,482,208,"Prepare Chrono comparison",[this]{
            if(!ready||!chrono_transfer)throw std::runtime_error("Solve a lunar first.");
            if(result.solutions[solution->value()].weak)throw std::runtime_error("Weak lunar geometry: review this result before using it as a clock reference.");
            chrono_transfer(saved,result.solutions[solution->value()]);
        });
        solution=pick(36,544,350,"Solution · closest AP first","");bind(solution,[this]{selection();});
        summary=new Fl_Box(404,532,592,46,"");summary->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);summary->labelsize(12);
        action(1014,538,118,"Example",[this]{example();});
        report=new Fl_Text_Display(36,592,1096,194);report->buffer(&buffer);report->textfont(FL_COURIER);report->textsize(12);
        resizable(report);end();defaults();state();send->deactivate();export_button->deactivate();
        buffer.text("Lunars: enter the raw limb-to-body distance and a watch date/time on the UTC scale.\n"
            "Unchecked Measured Hs = synthetic geometry at the iterated lunar UTC; the AP stays fixed.\n"
            "Real shots retain their own watch dates/times. Two real altitudes can refine position.\n"
            "Use the same watch for all shots. Clock correction means UTC minus watch, not TT minus UT1.\n"
            "Predict at start is a preview; it does not turn synthetic altitudes into observations.\n"
            "Sea/short-waterline dip applies to altitudes only, never the lunar distance. See LUNARS.md.");
    }
    ~LunarPanel()override{report->buffer(nullptr);}
    void put_position(const Sight &s){lat->value(desktop::angle_entry(s.dr_lat,2).c_str());ns->value(s.dr_lat<0);
        lon->value(desktop::angle_entry(s.dr_lon).c_str());ew->value(s.dr_lon<0);}
    void defaults(){
        NavDefaults d;navrc_load(&d);navrc_apply(&d,&settings);NavLunarRequest q;navlunar_init(&q);
        q.distance=settings;std::strcpy(q.distance.body,"jupiter");q.distance.hs_deg=0;
        auto now=chrono_nav::stamp(std::time(nullptr));desktop::date_time(now.substr(0,10),now.substr(11),q.distance);
        q.moon.sight=q.other.sight=q.distance;q.moon.sight.limb=1;q.other.sight.limb=0;fill(q);
        distance->value("");if(!d.have_dr){lat->value("");lon->value("");}
    }
    void load_dr(bool current){
        Sight s{};if(current){if(!current_position)throw std::runtime_error("No sight DR available.");s=current_position();}
        else{NavDefaults d;navrc_load(&d);if(!d.have_dr)throw std::runtime_error("Save a DR on Defaults first.");navrc_apply(&d,&s);}
        put_position(s);settings.course=s.course;settings.speed=s.speed;settings.set=s.set;settings.drift=s.drift;
        motion->value(s.has_motion);invalidate();
    }
    void fill(const NavLunarRequest &q){
        settings=q.distance;auto stamp=raw_stamp(q.distance);date->value(stamp.substr(0,10).c_str());time->value(stamp.substr(11).c_str());
        distance->value(desktop::angle_entry(q.distance.hs_deg).c_str());put_position(q.distance);
        const char *names[]={"sun","mercury","venus","mars","jupiter","saturn","star"};for(int i=0;i<7;i++)if(!std::strcmp(names[i],q.distance.body))body->value(i);
        int st=nav_star_find(q.distance.star);if(st>=0)star->value(st);
        moon_contact->value(1-q.moon_contact);other_contact->value(1-q.other_contact);refine->value(q.refine_position);
        ie->value(num(q.distance.ie_min).c_str());arc->value(q.distance.ie_on_arc);temp->value(num(q.distance.temp_c,2).c_str());pressure->value(num(q.distance.press_mb,2).c_str());
        elevation->value(num(q.observer_height_m,2).c_str());waterline->value(num(std::max(q.moon.sight.short_horizon_nm,q.other.sight.short_horizon_nm)).c_str());
        initial->value(num(q.initial_correction_sec,3).c_str());span->value(num(q.search_seconds,1).c_str());sigma->value(num(q.distance_sigma_min,2).c_str());alt_sigma->value(num(q.altitude_sigma_min,2).c_str());motion->value(q.distance.has_motion);
        for(int i=0;i<2;i++){
            auto &a=altitude[i];auto &raw=i?q.other:q.moon;auto &s=raw.sight;
            a.settings=s;a.shared=s.ie_min==q.distance.ie_min&&s.ie_on_arc==q.distance.ie_on_arc&&s.temp_c==q.distance.temp_c&&s.press_mb==q.distance.press_mb&&
                (s.short_horizon_nm==0||s.short_horizon_nm==std::max(q.moon.sight.short_horizon_nm,q.other.sight.short_horizon_nm));
            a.setup->copy_label(a.shared?"…":"Own");
            a.measured->value(raw.measured);a.hs->value(!raw.measured&&s.hs_deg==0?"":desktop::angle_entry(s.hs_deg).c_str());auto t=raw_stamp(s);
            a.date->value(t.substr(0,10).c_str());a.time->value(t.substr(11).c_str());a.limb->value(1-s.limb);
            a.horizon->value(s.artificial?1:s.short_horizon_nm>0?2:0);a.height->value(num(s.he).c_str());a.units->value(s.he_metres);
        }state();invalidate();
    }
    void altitude_setup(int i){
        auto &a=altitude[i];Fl_Double_Window dialog(450,310,i?"Other altitude settings":"Moon altitude settings");
        Fl_Check_Button shared(20,18,410,28,"Use main IE, weather and waterline settings");shared.value(a.shared);
        Fl_Input error(160,65,100,28,"IE (minutes)");error.value(a.shared?ie->value():num(a.settings.ie_min).c_str());
        Fl_Choice side(280,65,140,28);side.add("Off arc|On arc");side.value(a.shared?arc->value():a.settings.ie_on_arc);
        Fl_Input t(160,110,100,28,"Temperature C");t.value(a.shared?temp->value():num(a.settings.temp_c).c_str());
        Fl_Input p(160,155,100,28,"Pressure hPa");p.value(a.shared?pressure->value():num(a.settings.press_mb).c_str());
        Fl_Input water(160,200,100,28,"Waterline NM");water.value(a.shared?waterline->value():num(a.settings.short_horizon_nm).c_str());
        Fl_Button apply(180,258,110,32,"Apply"),cancel(310,258,110,32,"Cancel");int choice=0;
        apply.callback([](Fl_Widget*,void *p){*static_cast<int*>(p)=1;},&choice);
        cancel.callback([](Fl_Widget*,void *p){*static_cast<int*>(p)=2;},&choice);
        dialog.end();dialog.set_modal();dialog.show();while(dialog.shown()&&!choice)Fl::wait();dialog.hide();if(choice!=1)return;
        Sight s=a.settings;if(!shared.value()){
            s.ie_min=desktop::number(error.value(),"Altitude index error",0,120);s.ie_on_arc=side.value();
            s.temp_c=desktop::number(t.value(),"Altitude temperature",-100,100);s.press_mb=desktop::number(p.value(),"Altitude pressure",0,1200);
            s.short_horizon_nm=desktop::number(water.value(),"Altitude waterline distance",0,1000);
            if(a.horizon->value()==2&&s.short_horizon_nm<=0)throw std::runtime_error("A short-waterline altitude needs a positive distance.");
        }a.settings=s;a.shared=shared.value();a.setup->copy_label(a.shared?"…":"Own");invalidate();
    }
    NavLunarRequest read(){
        NavLunarRequest q;navlunar_init(&q);q.distance=settings;
        desktop::date_time(date->value(),time->value(),q.distance);
        q.distance.hs_deg=desktop::angle(distance->value(),"Lunar distance",150);
        q.distance.dr_lat=desktop::angle(lat->value(),"AP latitude",85)*(ns->value()?-1:1);
        q.distance.dr_lon=desktop::angle(lon->value(),"AP longitude",180)*(ew->value()?-1:1);
        const char *names[]={"sun","mercury","venus","mars","jupiter","saturn","star"};std::snprintf(q.distance.body,sizeof q.distance.body,"%s",names[body->value()]);
        std::snprintf(q.distance.star,sizeof q.distance.star,"%s",body->value()==6?nav_star_name(star->value()):"");
        q.distance.ie_min=desktop::number(ie->value(),"Index error",0,120);q.distance.ie_on_arc=arc->value();
        q.distance.temp_c=desktop::number(temp->value(),"Temperature",-100,100);q.distance.press_mb=desktop::number(pressure->value(),"Pressure",0,1200);
        q.distance.has_motion=motion->value();q.distance.artificial=0;q.distance.short_horizon_nm=0;
        q.observer_height_m=desktop::number(elevation->value(),"Observer elevation",-500,10000);
        q.initial_correction_sec=desktop::number(initial->value(),"Initial UTC minus watch",-604800,604800);
        q.search_seconds=desktop::number(span->value(),"Search half span",1,21600);
        q.distance_sigma_min=desktop::number(sigma->value(),"Distance uncertainty",.001,60);q.altitude_sigma_min=desktop::number(alt_sigma->value(),"Altitude uncertainty",.001,60);
        q.moon_contact=1-moon_contact->value();q.other_contact=1-other_contact->value();q.refine_position=refine->value();
        for(int i=0;i<2;i++){
            auto &a=altitude[i];auto &raw=i?q.other:q.moon;raw.measured=a.measured->value();raw.sight=q.distance;
            std::snprintf(raw.sight.body,sizeof raw.sight.body,"%s",i?q.distance.body:"moon");
            // Disabled synthetic fields retain their entered values for a later switch to real.
            if(std::strlen(a.date->value())&&std::strlen(a.time->value())){
                if(raw.measured)desktop::date_time(a.date->value(),a.time->value(),raw.sight);
                else try{Sight stored=raw.sight;desktop::date_time(a.date->value(),a.time->value(),stored);raw.sight=stored;}catch(const std::exception&){}
            }
            else if(raw.measured)throw std::runtime_error("A real altitude needs its own watch date and time.");
            if(raw.measured)raw.sight.hs_deg=desktop::angle(a.hs->value(),"Measured Hs",180);
            else{try{raw.sight.hs_deg=desktop::angle(a.hs->value(),"Stored Hs",180);}catch(...){raw.sight.hs_deg=0;}}
            raw.sight.limb=1-a.limb->value();raw.sight.artificial=a.horizon->value()==1;
            raw.sight.he=desktop::number(a.height->value(),"Eye height",0,10000);raw.sight.he_metres=a.units->value();
            if(!a.shared){raw.sight.ie_min=a.settings.ie_min;raw.sight.ie_on_arc=a.settings.ie_on_arc;
                raw.sight.temp_c=a.settings.temp_c;raw.sight.press_mb=a.settings.press_mb;}
            raw.sight.short_horizon_nm=a.horizon->value()==2?(a.shared?desktop::number(waterline->value(),"Waterline distance",.000001,1000):a.settings.short_horizon_nm):0;
        }
        char e[512];if(navlunar_validate(&q,e,sizeof e))throw std::runtime_error(e);return q;
    }
    static std::string report_text(const NavLunarRequest &q,const NavLunarResult &r){
        FILE *f=std::tmpfile();if(!f)throw std::runtime_error("Cannot create a lunar report.");navlunar_report(f,&q,&r);std::rewind(f);
        char b[4096];std::string text;size_t n;while((n=std::fread(b,1,sizeof b,f)))text.append(b,n);std::fclose(f);return text;
    }
    void selection(){
        if(!ready||solution->value()<0||solution->value()>=result.count)return;
        const auto &s=result.solutions[solution->value()];
        std::string label="UTC − watch = "+num(s.correction_sec,1)+" s; formal σ "+num(s.sigma_time_sec,1)+" s";
        if(s.weak)label+=" · WEAK GEOMETRY";else if(saved.refine_position)label+=" · review position branch";
        summary->copy_label(label.c_str());send->activate();
    }
    void solve(){
        invalidate();auto q=read();NavLunarResult r;char e[512];Fl::wait(0);
        if(navlunar_solve(&q,&r,e,sizeof e))throw std::runtime_error(e);
        saved=q;result=r;ready=true;solution->clear();
        for(int i=0;i<r.count;i++){auto &s=r.solutions[i];std::string item=num(s.correction_sec,1)+" s · "+num(s.lat,3)+", "+num(s.lon,3);solution->add(item.c_str());}
        solution->value(0);buffer.text(report_text(saved,result).c_str());selection();export_button->activate();
        if(notify)notify("Lunar solved. Review the model, position and formal uncertainty before preparing a Chrono comparison.",false);
    }
    void preview(){
        auto q=read();NavLunarResult r{};r.count=1;char e[512];if(navlunar_predict(&q,q.initial_correction_sec,q.distance.dr_lat,q.distance.dr_lon,&r.solutions[0],e,sizeof e))throw std::runtime_error(e);
        invalidate();buffer.text(("PREDICTION AT START — not a recovered UTC solution\n\n"+report_text(q,r)).c_str());summary->copy_label("Prediction only. Stored Hs/watch readings were not replaced.");
    }
    void example(){
        NavLunarRequest q;navlunar_init(&q);q.distance.mo=11;q.distance.d=3;q.distance.h=13;q.distance.mi=20;
        q.moon_contact=-1;/* This worked example explicitly uses the far limb. */
        q.distance.dr_lat=34;q.distance.dr_lon=-117;q.distance.hs_deg=9.02;q.moon.sight=q.other.sight=q.distance;
        q.moon.sight.hs_deg=q.other.sight.hs_deg=0;q.moon.sight.limb=1;std::strcpy(q.moon.sight.body,"moon");fill(q);buffer.text("Fictional example loaded. Use Solve lunar UTC. No saved observations were changed.");
    }
    void save(){auto q=read();auto p=chooser(true,"Save raw lunar inputs","NavAlm-observation.lunar","Lunar observation\t*.lunar");if(p.empty())return;
        FILE *f=std::fopen(p.c_str(),"w");if(!f)throw std::runtime_error("Cannot save lunar inputs.");int error=navlunar_write(f,&q);error|=std::fclose(f)!=0;if(error)throw std::runtime_error("Error writing lunar inputs.");if(notify)notify("Raw lunar observation saved: "+p,false);}
    void load(){auto p=chooser(false,"Load lunar observation","","Lunar observation\t*.lunar");if(p.empty())return;
        NavLunarRequest q;char e[512];if(navlunar_load(p.c_str(),&q,e,sizeof e))throw std::runtime_error(e);fill(q);buffer.text("Lunar inputs loaded. Solve to derive UTC; original watch epochs are preserved.");}
    void export_report(){if(!ready)throw std::runtime_error("Solve again after changing inputs.");auto p=chooser(true,"Export lunar calculation","NavAlm-lunar-report.txt","Text\t*.txt");if(p.empty())return;
        std::unique_ptr<char,decltype(&std::free)> text(buffer.text(),&std::free);std::ofstream f(std::filesystem::u8path(p),std::ios::binary);if(!f||!(f<<text.get())||!f.flush())throw std::runtime_error("Cannot write lunar report.");}
};
#endif
