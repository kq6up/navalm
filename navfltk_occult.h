/* SPDX-License-Identifier: MIT — lunar occultation planning and time recovery. */
#ifndef NAVFLTK_OCCULT_H
#define NAVFLTK_OCCULT_H
#include "navchrono_sight.h"
extern "C" {
#include "navoccult.h"
}
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Text_Display.H>
#include <FL/fl_draw.H>
#include <fstream>
#include <functional>
#include <memory>
#include <locale>

namespace occult_ui {
inline std::string num(double x,int places=3){std::ostringstream s;s.imbue(std::locale::classic());s<<std::fixed<<std::setprecision(places)<<x;return s.str();}
inline std::string stamp(double jd){char s[48];navoccult_stamp(jd,s,sizeof s);return s;}
inline double epoch(const char *date,const char *time){Sight s{};desktop::date_time(date,time,s);return nav_julian_date(s.y,s.mo,s.d,s.h,s.mi,s.s);}
inline std::string text_report(const std::function<void(FILE*)> &write){
    std::unique_ptr<FILE,decltype(&std::fclose)> f(std::tmpfile(),&std::fclose);if(!f)throw std::runtime_error("Cannot create report.");write(f.get());std::rewind(f.get());
    std::string text;char b[4096];size_t n;while((n=std::fread(b,1,sizeof b,f.get())))text.append(b,n);return text;
}
inline std::string choose(bool save,const char *title,const char *name,const char *filter){
    Fl_Native_File_Chooser f;f.title(title);f.filter(filter);f.type(save?Fl_Native_File_Chooser::BROWSE_SAVE_FILE:Fl_Native_File_Chooser::BROWSE_FILE);
    if(save){f.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);f.preset_file(name);}if(f.show())return "";return f.filename();
}
inline void write(const std::string &path,const std::string &text){std::ofstream f(std::filesystem::u8path(path),std::ios::binary);if(!f||!(f<<text)||!f.flush())throw std::runtime_error("Cannot write "+path);}
class Controls {
public:
    std::function<void(const std::string&,bool)> notify;
    std::vector<std::unique_ptr<std::function<void()>>> callbacks;
    void bind(Fl_Widget *w,std::function<void()> fn){callbacks.push_back(std::make_unique<std::function<void()>>([this,fn]{try{fn();}catch(const std::exception &e){if(notify)notify(e.what(),true);}}));w->callback([](Fl_Widget*,void *p){(*static_cast<std::function<void()>*>(p))();},callbacks.back().get());}
    Fl_Input *entry(int x,int y,int width,const char *label,std::function<void()> changed){auto *w=new Fl_Input(x,y,width,30,label);w->align(FL_ALIGN_TOP_LEFT);w->labelsize(11);w->textsize(13);w->when(FL_WHEN_CHANGED);bind(w,changed);return w;}
    Fl_Choice *pick(int x,int y,int width,const char *label,const char *items,std::function<void()> changed){auto *w=new Fl_Choice(x,y,width,30,label);w->align(FL_ALIGN_TOP_LEFT);w->labelsize(11);w->textsize(12);w->add(items);w->value(0);bind(w,changed);return w;}
    Fl_Button *button(int x,int y,int width,const char *label,std::function<void()> action){auto *w=new Fl_Button(x,y,width,32,label);w->labelsize(12);bind(w,action);return w;}
    static void put_position(Fl_Input *lat,Fl_Choice *ns,Fl_Input *lon,Fl_Choice *ew,const Sight &s){lat->value(desktop::angle_entry(s.dr_lat,2).c_str());ns->value(s.dr_lat<0);lon->value(desktop::angle_entry(s.dr_lon).c_str());ew->value(s.dr_lon<0);}
    static void read_position(Fl_Input *lat,Fl_Choice *ns,Fl_Input *lon,Fl_Choice *ew,NavOccultRequest &q){q.lat=desktop::angle(lat->value(),"Latitude",89)*(ns->value()?-1:1);q.lon=desktop::angle(lon->value(),"Longitude",180)*(ew->value()?-1:1);}
};
class Diagram : public Fl_Widget {
public:
    NavOccultEvent event{};bool ready=false;
    Diagram(int x,int y,int w,int h):Fl_Widget(x,y,w,h){}
    void draw()override{
        fl_push_clip(x(),y(),w(),h());fl_color(fl_rgb_color(15,22,34));fl_rectf(x(),y(),w(),h());
        if(ready){double radius=std::min(w()*.27,h()*.37),cx=x()+w()*.5,cy=y()+h()*.50;
            fl_color(fl_rgb_color(135,148,166));fl_pie(int(cx-radius),int(cy-radius),int(radius*2),int(radius*2),0,360);
            double b=event.impact;double yy=cy-std::min(b,1.3)*radius;
            fl_color(FL_YELLOW);fl_line_style(FL_SOLID,2);fl_line(int(cx-radius*1.4),int(yy),int(cx+radius*1.4),int(yy));fl_line_style(FL_SOLID);
            if(event.has_contacts){double dx=radius*std::sqrt(std::max(0.,1-b*b));fl_color(FL_RED);fl_pie(int(cx-dx-4),int(yy-4),8,8,0,360);fl_color(FL_GREEN);fl_pie(int(cx+dx-4),int(yy-4),8,8,0,360);}
            fl_color(FL_WHITE);fl_font(FL_HELVETICA,12);fl_draw("Schematic track; not sky orientation",x()+8,y()+h()-12);
        }else{fl_color(FL_WHITE);fl_font(FL_HELVETICA,12);fl_draw("Select an event to see its geometry",x()+8,y()+h()/2);}
        fl_pop_clip();
    }
};
}

class OccultationUTCPanel : public Fl_Group,public occult_ui::Controls {
public:
    Fl_Input *star,*near_date,*near_time,*date,*time,*lat,*lon,*height,*dut1,*reaction,*limb_offset,*timing_sigma,*limb_sigma,*position_sigma;
    Fl_Choice *ns,*ew,*contact;Fl_Check_Button *review;Fl_Button *send,*export_button;
    Fl_Text_Buffer buffer;Fl_Text_Display *report;Fl_Box *summary;
    NavOccultRequest saved{};NavOccultEvent event{};NavOccultObservation observation{};NavOccultRecovery result{};
    bool ready=false;
    std::function<void(double,double,const std::string&)> chrono_transfer;
    std::function<Sight()> current_position;
    OccultationUTCPanel():Fl_Group(16,130,1148,672,"Occultation UTC"){
        begin();auto changed=[this]{invalidate();};
        star=entry(36,178,220,"Catalogue star · name or exact alias",changed);
        near_date=entry(272,178,150,"Event UTC date · YYYY-MM-DD",changed);near_time=entry(438,178,142,"Near event UTC · HH:MM:SS",changed);
        contact=pick(596,178,210,"Recorded contact","Disappearance (D)|Reappearance (R)",changed);
        button(822,178,148,"Load saved DR",[this]{load_dr(false);});button(984,178,148,"Use sight DR",[this]{load_dr(true);});
        lat=entry(36,244,132,"AP latitude · DD MM.m",changed);ns=pick(176,244,54,"","N|S",changed);
        lon=entry(246,244,136,"Longitude · DDD MM.m",changed);ew=pick(390,244,54,"","E|W",changed);ew->value(1);
        height=entry(460,244,120,"Elevation · metres",changed);dut1=entry(596,244,104,"DUT1 · seconds",changed);
        auto *help=new Fl_Box(718,218,414,66,"Use your position AT the contact. One contact recovers clock error only; it does not solve a fix.");help->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);help->labelsize(12);
        date=entry(36,312,182,"Recorded watch date · UTC scale",changed);time=entry(234,312,182,"Raw watch time · HH:MM:SS.sss",changed);
        reaction=entry(432,312,152,"Reaction delay · seconds",changed);reaction->tooltip("Positive means you recorded the contact late. UTC minus watch includes this correction.");
        limb_offset=entry(600,312,152,"Limb offset · arcseconds",changed);limb_offset->tooltip("Positive increases the model Moon radius. Leave zero without independent limb information.");
        timing_sigma=entry(768,312,104,"Timing σ · s",changed);limb_sigma=entry(888,312,112,"Limb/catalog σ · arcsec",changed);position_sigma=entry(1016,312,116,"AP σ · NM / axis",changed);
        button(36,374,160,"Recover UTC",[this]{solve();});button(210,374,120,"Load inputs…",[this]{load();});button(344,374,130,"Save inputs…",[this]{save();});
        export_button=button(488,374,152,"Export report…",[this]{export_report();});
        review=new Fl_Check_Button(656,374,476,30,"I reviewed the contact, AP, limb and timing uncertainty");review->labelsize(11);bind(review,[this]{update_send();});
        send=button(36,424,252,"Prepare Chrono comparison",[this]{transfer();});
        summary=new Fl_Box(306,414,826,56,"");summary->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);summary->labelsize(13);
        report=new Fl_Text_Display(36,488,1096,298);report->buffer(&buffer);report->textfont(FL_COURIER);report->textsize(12);
        resizable(report);end();
        auto now=chrono_nav::stamp(std::time(nullptr));near_date->value(now.substr(0,10).c_str());near_time->value("00:00:00");date->value(now.substr(0,10).c_str());time->value("");
        height->value("0");dut1->value("0");reaction->value("0");limb_offset->value("0");timing_sigma->value("0.2");limb_sigma->value("2");position_sigma->value("0.1");
        NavDefaults defaults;navrc_load(&defaults);if(defaults.have_dr){Sight s{};navrc_apply(&defaults,&s);put_position(lat,ns,lon,ew,s);}
        invalidate();buffer.text("Choose a predicted event on Occultations, or enter a catalogue star and an approximate event UTC.\nEnter the actual watch reading and identify disappearance or reappearance.\nThe watch date/time must use the UTC calendar/scale, not local civil time.\n\nNo sextant index correction, dip, Hs, or synthetic altitude is needed. At contact the star and\nthe occulting limb have the same incoming direction; common atmospheric refraction does not\nchange that coincidence. Lunar parallax and observer elevation ARE included by NOVAS.\n\nThe smooth Moon has no mountains or valleys. Near-limb events and double stars need special care.\nUncertainty uses YOUR assumed timing, limb/catalog and position sigmas; see OCCULTATIONS.md.\nSaving/exporting or preparing Chrono does not replace the original watch reading.");
    }
    ~OccultationUTCPanel()override{report->buffer(nullptr);}
    void update_send(){if(ready&&review->value())send->activate();else send->deactivate();}
    void invalidate(){ready=false;review->value(0);send->deactivate();export_button->deactivate();summary->copy_label("Enter a recorded contact, then Recover UTC. Original watch reading is retained.");}
    void load_dr(bool current){Sight s{};if(current){if(!current_position)throw std::runtime_error("No current sight DR.");s=current_position();}else{NavDefaults d;navrc_load(&d);if(!d.have_dr)throw std::runtime_error("Save a DR on Defaults first.");navrc_apply(&d,&s);}put_position(lat,ns,lon,ew,s);invalidate();}
    void select_event(const NavOccultRequest &q,const NavOccultEvent &e,int c){
        star->value(navoccult_star(e.star)->name);auto event_epoch=occult_ui::stamp(e.closest_jd);near_date->value(event_epoch.substr(0,10).c_str());near_time->value(event_epoch.substr(11,12).c_str());
        Sight s{};s.dr_lat=q.lat;s.dr_lon=q.lon;put_position(lat,ns,lon,ew,s);height->value(occult_ui::num(q.height_m).c_str());dut1->value(occult_ui::num(q.dut1_sec).c_str());contact->value(c);
        invalidate();buffer.text("Prediction copied. Enter your actual recorded watch date/time, then Recover UTC.\nNo observation was generated from the prediction. Watch fields were left unchanged.");
    }
    void read(NavOccultRequest &q,NavOccultObservation &o,int &index,double &event_epoch){
        navoccult_init(&q);event_epoch=occult_ui::epoch(near_date->value(),near_time->value());q.start_jd=event_epoch-.25;q.days=.5;q.night_only=0;q.min_alt_deg=0;
        read_position(lat,ns,lon,ew,q);q.height_m=desktop::number(height->value(),"Observer elevation",-500,10000);q.dut1_sec=desktop::number(dut1->value(),"DUT1",-.9,.9);
        index=navoccult_star_find(star->value());if(index<0)throw std::runtime_error("Star not in the bundled magnitude-6.5 catalogue. Use its exact name or an alias from the planner report.");
        o={};o.watch_jd=occult_ui::epoch(date->value(),time->value());o.contact=contact->value();o.reaction_sec=desktop::number(reaction->value(),"Reaction delay",-10,10);
        o.limb_offset_arcsec=desktop::number(limb_offset->value(),"Limb offset",-120,120);o.timing_sigma_sec=desktop::number(timing_sigma->value(),"Timing sigma",.001,60);
        o.limb_sigma_arcsec=desktop::number(limb_sigma->value(),"Limb/catalog sigma",.01,120);o.position_sigma_nm=desktop::number(position_sigma->value(),"AP sigma",0,60);
        char e[512];if(navoccult_validate(&q,e,sizeof e))throw std::runtime_error(e);
    }
    void solve(){invalidate();NavOccultRequest q;NavOccultObservation o;int index;double event_epoch;read(q,o,index,event_epoch);
        NavOccultEvent e;NavOccultRecovery r;char err[512];if(navoccult_event(&q,index,event_epoch,0,&e,err,sizeof err)||navoccult_recover(&q,&e,&o,&r,err,sizeof err))throw std::runtime_error(err);
        saved=q;event=e;observation=o;result=r;ready=true;
        buffer.text(occult_ui::text_report([&](FILE *f){navoccult_recovery_report(f,&saved,&event,&observation,&result);}).c_str());
        auto label="UTC − watch = "+occult_ui::num(r.correction_sec)+" s; assumed σ "+occult_ui::num(r.sigma_time_sec)+" s"+(r.weak?" · CAUTION: review geometry / star":"");summary->copy_label(label.c_str());export_button->activate();update_send();
    }
    void transfer(){if(!ready||!review->value()||!chrono_transfer)throw std::runtime_error("Recover UTC and review the result first.");
        // The raw watch reading includes the observer's delay; shift its paired
        // reference UTC by the same delay, rather than silently editing watch.
        double watch=(observation.watch_jd-2440587.5)*86400;
        std::string note=std::string("Occultation ")+navoccult_star(event.star)->name+(observation.contact?" R":" D")+"; assumed sigma "+occult_ui::num(result.sigma_time_sec,2)+" s; smooth Moon"+(result.weak?"; CAUTION":"");
        chrono_transfer(watch,watch+result.correction_sec,note);
    }
    void export_report(){if(!ready)throw std::runtime_error("Recover UTC again after editing inputs.");auto p=occult_ui::choose(true,"Export occultation recovery","NavAlm-occultation-UTC.txt","Text\t*.txt");if(p.empty())return;
        std::unique_ptr<char,decltype(&std::free)> text(buffer.text(),&std::free);occult_ui::write(p,text.get());}
    std::string input_text(){NavOccultRequest q;NavOccultObservation o;int index;double event_epoch;read(q,o,index,event_epoch);
        std::ostringstream f;f.imbue(std::locale::classic());f<<std::setprecision(17)<<"NAVALM_OCCULTATION 1\n"<<std::quoted(navoccult_star(index)->name)<<'\n'<<event_epoch<<' '<<q.lat<<' '<<q.lon<<' '<<q.height_m<<' '<<q.dut1_sec<<'\n'<<o.watch_jd<<' '<<o.contact<<' '<<o.reaction_sec<<' '<<o.limb_offset_arcsec<<' '<<o.timing_sigma_sec<<' '<<o.limb_sigma_arcsec<<' '<<o.position_sigma_nm<<'\n';return f.str();}
    void load_text(const std::string &text){
        std::istringstream f(text);f.imbue(std::locale::classic());std::string magic,name;int version,index;double event_epoch;NavOccultRequest q;navoccult_init(&q);NavOccultObservation o{};
        if(!(f>>magic>>version)||magic!="NAVALM_OCCULTATION"||version!=1||!(f>>std::quoted(name)>>event_epoch>>q.lat>>q.lon>>q.height_m>>q.dut1_sec>>o.watch_jd>>o.contact>>o.reaction_sec>>o.limb_offset_arcsec>>o.timing_sigma_sec>>o.limb_sigma_arcsec>>o.position_sigma_nm))throw std::runtime_error("Invalid occultation input file; current inputs were kept.");
        f>>std::ws;if(!f.eof())throw std::runtime_error("Unexpected data after occultation inputs.");
        q.start_jd=event_epoch-.25;q.days=.5;q.night_only=0;q.min_alt_deg=0;index=navoccult_star_find(name.c_str());
        NavOccultEvent e;NavOccultRecovery r;char err[512];if(index<0||navoccult_event(&q,index,event_epoch,0,&e,err,sizeof err)||navoccult_recover(&q,&e,&o,&r,err,sizeof err))throw std::runtime_error(index<0?"Unknown catalogue star in input file.":err);
        select_event(q,e,o.contact);auto watch=occult_ui::stamp(o.watch_jd);date->value(watch.substr(0,10).c_str());time->value(watch.substr(11,12).c_str());
        reaction->value(occult_ui::num(o.reaction_sec,6).c_str());limb_offset->value(occult_ui::num(o.limb_offset_arcsec,6).c_str());timing_sigma->value(occult_ui::num(o.timing_sigma_sec,6).c_str());limb_sigma->value(occult_ui::num(o.limb_sigma_arcsec,6).c_str());position_sigma->value(occult_ui::num(o.position_sigma_nm,6).c_str());
        buffer.text("Raw occultation inputs loaded. Recover UTC to recompute; no clock comparison was saved.");
    }
    void save(){auto text=input_text();auto p=occult_ui::choose(true,"Save occultation inputs","NavAlm-contact.occ","Occultation inputs\t*.occ");if(!p.empty())occult_ui::write(p,text);}
    void load(){auto p=occult_ui::choose(false,"Load occultation inputs","","Occultation inputs\t*.occ");if(p.empty())return;std::ifstream f(std::filesystem::u8path(p),std::ios::binary);if(!f)throw std::runtime_error("Cannot read occultation file.");std::string s((std::istreambuf_iterator<char>(f)),{});load_text(s);}
};

class OccultationPanel : public Fl_Group,public occult_ui::Controls {
public:
    Fl_Input *date,*time,*days,*lat,*lon,*height,*dut1,*magnitude,*altitude,*sun_alt,*margin;
    Fl_Choice *ns,*ew,*sort,*filter,*contact;Fl_Check_Button *night;
    Fl_Hold_Browser *rows;Fl_Text_Buffer buffer;Fl_Text_Display *details;Fl_Box *summary;
    Fl_Button *send,*export_button;occult_ui::Diagram *diagram;
    NavOccultRequest saved{};std::unique_ptr<NavOccultResult> result=std::make_unique<NavOccultResult>();
    std::vector<size_t> row_ids;bool ready=false,testing=false;
    std::function<Sight()> current_position;
    std::function<void(const NavOccultRequest&,const NavOccultEvent&,int)> transfer;
    OccultationPanel():Fl_Group(16,130,1148,672,"Occultations"){
        begin();auto changed=[this]{invalidate();};
        date=entry(36,176,150,"Start UTC date · YYYY-MM-DD",changed);time=entry(202,176,128,"UTC · HH:MM:SS",changed);days=entry(346,176,64,"Days · 1 to 31",changed);
        magnitude=entry(426,176,94,"Faintest mag · ≤6.5",changed);altitude=entry(536,176,96,"Min altitude · deg",changed);height=entry(648,176,100,"Elevation · m",changed);dut1=entry(764,176,88,"DUT1 · s",changed);
        button(868,176,124,"Load saved DR",[this]{load_dr(false);});button(1006,176,126,"Use sight DR",[this]{load_dr(true);});
        lat=entry(36,242,132,"AP latitude · DD MM.m",changed);ns=pick(176,242,54,"","N|S",changed);
        lon=entry(246,242,136,"Longitude · DDD MM.m",changed);ew=pick(390,242,54,"","E|W",changed);ew->value(1);
        night=new Fl_Check_Button(460,242,110,30,"Night only");night->value(1);night->labelsize(12);bind(night,[this]{state();invalidate();});
        sun_alt=entry(584,242,108,"Max Sun alt · deg",changed);margin=entry(708,242,100,"Near-miss · arcsec",changed);
        sort=pick(824,242,148,"Sort by","Time|Magnitude|Most direct|Closest to grazing",[this]{rebuild();});
        filter=pick(988,242,144,"Show","All events|Direct only|Near-limb / misses",[this]{rebuild();});
        button(36,298,152,"Find occultations",[this]{search();});
        contact=pick(202,298,194,"Contact to time","Disappearance (D)|Reappearance (R)",[this]{selection();});
        send=button(410,298,210,"Use for time recovery",[this]{prepare();});
        export_button=button(634,298,146,"Export list…",[this]{export_report();});
        summary=new Fl_Box(798,286,334,52,"");summary->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);summary->labelsize(12);
        auto *legend=new Fl_Box(36,340,1096,24,"UTC disappearance / reappearance · magnitude · impact b (0 = central, 1 = tangent) · * = visible in search interval");legend->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);legend->labelsize(11);
        rows=new Fl_Hold_Browser(36,370,1096,184);rows->textfont(FL_COURIER);rows->textsize(12);rows->format_char(0);bind(rows,[this]{selection();});
        details=new Fl_Text_Display(36,570,740,176);details->buffer(&buffer);details->textfont(FL_COURIER);details->textsize(11);
        diagram=new occult_ui::Diagram(792,570,340,176);
        auto *warning=new Fl_Box(36,754,1096,36,"Smooth Moon, no terrain profile. Near-limb (b ≥ 0.90) is a ranking, not a prediction of mountain/valley contacts.\nPosition, double stars and lunar relief can dominate time recovery. All 8,881 bundled stars are available offline; see OCCULTATIONS.md.");warning->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);warning->labelsize(11);
        resizable(rows);end();auto now=chrono_nav::stamp(std::time(nullptr));date->value(now.substr(0,10).c_str());time->value("00:00:00");days->value("7");magnitude->value("6.5");altitude->value("5");height->value("0");dut1->value("0");sun_alt->value("-6");margin->value("30");
        NavDefaults defaults;navrc_load(&defaults);if(defaults.have_dr){Sight s{};navrc_apply(&defaults,&s);put_position(lat,ns,lon,ew,s);}state();invalidate();
    }
    ~OccultationPanel()override{details->buffer(nullptr);}
    void state(){if(night->value())sun_alt->activate();else sun_alt->deactivate();}
    void invalidate(){ready=false;result->count=0;row_ids.clear();rows->clear();diagram->ready=false;diagram->redraw();send->deactivate();export_button->deactivate();summary->copy_label("Set AP and interval, then Find occultations.");buffer.text("Local lunar occultations of catalogue stars. No online service is needed.\nBoth contacts are calculated even if one is outside the interval or visibility limits.\nOnly marked contacts can be sent directly for time recovery.\nDUT1 is UT1 minus UTC; leave zero if unknown, and allow for that uncertainty.\nElevation is observer height above the ellipsoid, not sextant eye-height dip.");}
    void load_dr(bool current){Sight s{};if(current){if(!current_position)throw std::runtime_error("No current sight DR.");s=current_position();}else{NavDefaults d;navrc_load(&d);if(!d.have_dr)throw std::runtime_error("Save a DR on Defaults first.");navrc_apply(&d,&s);}put_position(lat,ns,lon,ew,s);invalidate();}
    NavOccultRequest read(){NavOccultRequest q;navoccult_init(&q);q.start_jd=occult_ui::epoch(date->value(),time->value());
        q.days=desktop::number(days->value(),"Days",.001,31);read_position(lat,ns,lon,ew,q);q.height_m=desktop::number(height->value(),"Observer elevation",-500,10000);q.dut1_sec=desktop::number(dut1->value(),"DUT1",-.9,.9);
        q.max_magnitude=desktop::number(magnitude->value(),"Magnitude",-2,6.5);q.min_alt_deg=desktop::number(altitude->value(),"Minimum altitude",0,90);q.max_sun_alt_deg=desktop::number(sun_alt->value(),"Sun altitude",-90,90);q.margin_arcsec=desktop::number(margin->value(),"Near-miss margin",0,120);q.night_only=night->value();char e[512];if(navoccult_validate(&q,e,sizeof e))throw std::runtime_error(e);return q;}
    void search(){
        auto q=read();invalidate();char err[512];int rc;
        if(testing)rc=navoccult_search(&q,result.get(),nullptr,nullptr,err,sizeof err);
        else{
            Fl_Double_Window dialog(460,154,"Finding lunar occultations");Fl_Progress progress(24,30,412,28);progress.minimum(0);progress.maximum(1);Fl_Button cancel(168,92,124,32,"Cancel");bool stop=false;
            cancel.callback([](Fl_Widget*,void *p){*static_cast<bool*>(p)=true;},&stop);dialog.callback([](Fl_Widget*,void *p){*static_cast<bool*>(p)=true;},&stop);dialog.end();dialog.set_modal();dialog.show();Fl::check();
            struct Context{Fl_Progress *p;bool *stop;} context{&progress,&stop};
            rc=navoccult_search(&q,result.get(),[](double f,void *raw){auto *c=static_cast<Context*>(raw);c->p->value(f);Fl::check();return *c->stop?1:0;},&context,err,sizeof err);dialog.hide();
        }
        if(rc)throw std::runtime_error(err);saved=q;ready=true;export_button->activate();rebuild();if(notify)notify("Occultations calculated for the entered AP. Select a visible contact for time recovery.",false);
    }
    void rebuild(){if(!ready)return;rows->clear();row_ids.clear();navoccult_sort(result.get(),sort->value());
        for(size_t i=0;i<result->count;i++){auto &e=result->events[i];if(filter->value()==1&&e.grazing)continue;if(filter->value()==2&&!e.grazing)continue;row_ids.push_back(i);
            auto a=occult_ui::stamp(e.has_contacts?e.contact_jd[0]:e.closest_jd),b=e.has_contacts?occult_ui::stamp(e.contact_jd[1]):std::string("—");
            std::string line=a.substr(0,19)+(e.visible[0]?"*":" ")+"  "+(e.has_contacts?b.substr(11,8)+(a.substr(0,10)!=b.substr(0,10)?" +1d":""):"no contact")+(e.visible[1]?"*":" ")+"  "+navoccult_star(e.star)->name+"  m="+occult_ui::num(navoccult_star(e.star)->magnitude,2)+"  b="+occult_ui::num(e.impact,3)+"  "+(e.has_contacts?(e.grazing?"near-limb":"direct"):"NEAR MISS")+(e.double_star?" [double]":"");rows->add(line.c_str());}
        auto msg=std::to_string(row_ids.size())+" shown / "+std::to_string(result->count)+" events. Times UTC.";summary->copy_label(msg.c_str());if(!row_ids.empty())rows->value(1);selection();
    }
    const NavOccultEvent *selected()const{int r=rows->value();return ready&&r>0&&size_t(r)<=row_ids.size()?&result->events[row_ids[size_t(r-1)]]:nullptr;}
    void selection(){auto *e=selected();send->deactivate();diagram->ready=e;diagram->redraw();if(!e){buffer.text(ready?"No events match these filters. Try more days, a fainter magnitude, or include daylight.":"Run Find occultations.");return;}
        diagram->event=*e;auto one=std::make_unique<NavOccultResult>();one->count=1;one->events[0]=*e;auto text=occult_ui::text_report([&](FILE *f){navoccult_report(f,&saved,one.get());}); auto first=text.find("1. "); buffer.text((first==std::string::npos?text:text.substr(first)).c_str());
        if(e->has_contacts&&e->visible[contact->value()])send->activate();
    }
    void prepare(){auto *e=selected();if(!e||!e->has_contacts||!e->visible[contact->value()]||!transfer)throw std::runtime_error("Choose a visible disappearance or reappearance. A near miss has no smooth-limb contact.");transfer(saved,*e,contact->value());}
    void export_report(){if(!ready)throw std::runtime_error("Find occultations again after changing inputs.");auto p=occult_ui::choose(true,"Export occultation predictions","NavAlm-occultations.txt","Text\t*.txt");if(p.empty())return;
        auto shown=std::make_unique<NavOccultResult>();for(size_t i:row_ids)shown->events[shown->count++]=result->events[i];occult_ui::write(p,occult_ui::text_report([&](FILE *f){navoccult_report(f,&saved,shown.get());}));}
};
#endif
