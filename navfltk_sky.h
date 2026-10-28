/* SPDX-License-Identifier: MIT */
#ifndef NAVFLTK_SKY_H
#define NAVFLTK_SKY_H
#include "navsky.h"
#include <FL/Fl_Widget.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

class SkyWindow : public Fl_Double_Window {
  public:
    std::function<void()> toggle_fullscreen;
    using Fl_Double_Window::Fl_Double_Window;
    int handle(int event) override {
        if(event==FL_SHORTCUT && (Fl::event_key()==FL_F+11 ||
           (Fl::event_key()==FL_Escape && fullscreen_active()))){
            if(toggle_fullscreen)toggle_fullscreen();return 1;
        }
        return Fl_Double_Window::handle(event);
    }
};

class SkyCanvas : public Fl_Widget {
    double radius() const { return std::max(30.,std::min(w(),h())*.5-32); }
    void point(const NavSkyStar &s,double &px,double &py) const {
        double r=(90-s.altitude)/90*radius(), a=s.azimuth*3.14159265358979323846/180;
        px=x()+w()*.5-r*std::sin(a); py=y()+h()*.5-r*std::cos(a);
    }
  public:
    NavSky sky{};
    bool ready=false, names=true, lines=true, constellation_names=false, supporting=true;
    size_t object_count() const { return sky.count+sky.body_count; }
    const NavSkyStar &object(size_t i) const { return *navsky_object(&sky,i); }
    int selected=-1;
    std::function<void(int)> choose;
    SkyCanvas(int x,int y,int w,int h):Fl_Widget(x,y,w,h) {}
    void draw() override {
        fl_push_clip(x(),y(),w(),h());
        fl_color(fl_rgb_color(16,28,48)); fl_rectf(x(),y(),w(),h());
        double cx=x()+w()*.5, cy=y()+h()*.5, r=radius();
        fl_font(FL_HELVETICA,11);
        for(int altitude : {0,30,60}){
            double ring=(90-altitude)/90.*r;
            fl_color(fl_rgb_color(66,81,106)); fl_arc(cx-ring,cy-ring,ring*2,ring*2,0,360);
            fl_color(fl_rgb_color(167,181,203));
            fl_draw((std::to_string(altitude)+" deg").c_str(),int(cx+5),int(cy-ring+14));
        }
        fl_color(fl_rgb_color(42,57,81)); fl_line(int(cx-r),int(cy),int(cx+r),int(cy));
        fl_line(int(cx),int(cy-r),int(cx),int(cy+r));
        fl_font(FL_HELVETICA_BOLD,14); fl_color(fl_rgb_color(213,223,237));
        fl_draw("N",int(cx-5),int(cy-r-12)); fl_draw("S",int(cx-5),int(cy+r+22));
        fl_draw("E",int(cx-r-23),int(cy+5)); fl_draw("W",int(cx+r+10),int(cy+5));
        if(!ready){
            fl_font(FL_HELVETICA,13);
            fl_draw("Enter a DR and UTC time, then Update sky.",x(),y(),w(),h(),FL_ALIGN_CENTER);
            fl_pop_clip(); return;
        }
        // Identification layers never enter the selectable navigation catalogue.
        auto project=[&](double altitude,double azimuth,double &px,double &py){
            NavSkyStar at{};at.altitude=altitude;at.azimuth=azimuth;point(at,px,py);
        };
        if(lines){
            fl_color(fl_rgb_color(80,112,154));
            for(size_t i=0;i<sky.path_count;i++){
                const auto &path=sky.paths[i];
                fl_begin_line();
                for(size_t j=0;j<path.count;j++){
                    const auto &at=sky.points[path.start+j];double px,py;
                    project(at.altitude,at.azimuth,px,py);fl_vertex(px,py);
                }
                fl_end_line();
            }
        }
        if(supporting){
            fl_color(fl_rgb_color(151,173,201));
            for(size_t i=0;i<sky.context_count;i++) if(sky.context[i].altitude>0){
                double px,py;point(sky.context[i],px,py);fl_pie(px-1.5,py-1.5,3,3,0,360);
            }
        }
        std::vector<int> order;
        for(size_t i=0;i<object_count();i++) if(object(i).altitude>0) order.push_back(int(i));
        std::stable_sort(order.begin(),order.end(),[&](int a,int b){
            if(object(a).is_body!=object(b).is_body) return object(a).is_body>object(b).is_body;
            if(object(a).selected!=object(b).selected) return object(a).selected>object(b).selected;
            return object(a).magnitude<object(b).magnitude;
        });
        struct Rect {double x,y,w,h;};
        std::vector<Rect> labels;
        for(int altitude : {0,30,60}){
            double ring=(90-altitude)/90.*r;
            labels.push_back({cx+3,cy-ring+2,40,15});
        }
        fl_font(FL_HELVETICA,11);
        for(int index:order){
            const auto &s=object(index); double px,py; point(s,px,py);
            auto color=s.is_body?(s.body==NAV_SUN?fl_rgb_color(255,198,66):s.body==NAV_MOON?
                fl_rgb_color(204,220,245):fl_rgb_color(255,156,112)):s.selected?fl_rgb_color(255,218,120):s.eligible?
                fl_rgb_color(225,234,247):fl_rgb_color(130,145,169);
            double size=s.is_body?(s.body==NAV_SUN || s.body==NAV_MOON?6.:4.):std::max(2.,5-s.magnitude*.65);
            if(s.selected || index==selected){
                double ring=index==selected?11:8;
                fl_color(index==selected?fl_rgb_color(123,216,230):color);
                fl_arc(px-ring,py-ring,ring*2,ring*2,0,360);
            }
            fl_color(color); fl_pie(px-size,py-size,size*2,size*2,0,360);
            if(s.is_body){
                fl_color(fl_rgb_color(16,28,48));
                if(s.body==NAV_MOON){
                    fl_color(fl_rgb_color(130,145,169));
                    fl_pie(px-3,py-3,2,2,0,360); fl_pie(px+1,py+1,3,3,0,360);
                }
                else if(s.body!=NAV_SUN) fl_pie(px-1.5,py-1.5,3,3,0,360);
            }
            if(!names && !s.is_body) continue;
            double width=fl_width(s.name);
            const double offsets[][2]={{11,-9},{11,17},{-width-11,-9},{-width-11,17},{11,-25},{-width-11,33}};
            for(const auto &offset:offsets){
                Rect label{px+offset[0],py+offset[1]-11,width+4,15};
                if(label.x<x()+3 || label.x+width>x()+w()-3 || label.y<y()+10 || label.y+14>y()+h()-10) continue;
                bool overlap=false;
                for(const auto &other:labels)
                    if(label.x<other.x+other.w && label.x+label.w>other.x &&
                       label.y<other.y+other.h && label.y+label.h>other.y){overlap=true;break;}
                if(!overlap){
                    labels.push_back(label); fl_color(color); fl_draw(s.name,int(label.x),int(label.y+11)); break;
                }
            }
        }
        if(constellation_names){
            fl_font(FL_HELVETICA,12);
            for(size_t i=0;i<sky.figure_count;i++){
                const auto &figure=sky.figures[i];if(!figure.visible)continue;
                double px,py;project(figure.altitude,figure.azimuth,px,py);
                double width=fl_width(figure.name);
                for(int offset : {0,18,-18,36,-36}){
                    Rect label{px-width/2,py+offset-12,width+4,16};
                    if(label.x<x()+3 || label.x+width>x()+w()-3 || label.y<y()+12 || label.y+16>y()+h()-12)continue;
                    bool overlap=false;
                    for(const auto &other:labels)if(label.x<other.x+other.w && label.x+label.w>other.x &&
                        label.y<other.y+other.h && label.y+label.h>other.y){overlap=true;break;}
                    if(overlap)continue;
                    labels.push_back(label);
                    fl_color(fl_rgb_color(16,28,48));fl_rectf(int(label.x-2),int(label.y-1),int(width+5),17);
                    fl_color(fl_rgb_color(158,199,241));fl_draw(figure.name,int(label.x),int(label.y+12));break;
                }
            }
        }
        fl_pop_clip();
    }
    int handle(int event) override {
        if(event==FL_PUSH && ready){
            int nearest=-1; double distance=16;
            for(size_t i=0;i<object_count();i++) if(object(i).altitude>0){
                double px,py; point(object(i),px,py);
                double d=std::hypot(px-Fl::event_x(),py-Fl::event_y());
                if(d<distance){nearest=int(i);distance=d;}
            }
            if(nearest>=0){selected=nearest;redraw();if(choose) choose(nearest);return 1;}
        }
        return Fl_Widget::handle(event);
    }
};
#endif
