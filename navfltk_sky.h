/* SPDX-License-Identifier: MIT */
#ifndef NAVFLTK_SKY_H
#define NAVFLTK_SKY_H
#include "navsky.h"
#include <FL/Fl_Widget.H>
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

class SkyCanvas : public Fl_Widget {
    double radius() const { return std::max(30.,std::min(w(),h())*.5-32); }
    void point(const NavSkyStar &s,double &px,double &py) const {
        double r=(90-s.altitude)/90*radius(), a=s.azimuth*3.14159265358979323846/180;
        px=x()+w()*.5-r*std::sin(a); py=y()+h()*.5-r*std::cos(a);
    }
  public:
    NavSky sky{};
    bool ready=false, names=true;
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
        std::vector<int> order;
        for(size_t i=0;i<sky.count;i++) if(sky.stars[i].altitude>0) order.push_back(int(i));
        std::stable_sort(order.begin(),order.end(),[&](int a,int b){
            if(sky.stars[a].selected!=sky.stars[b].selected) return sky.stars[a].selected>sky.stars[b].selected;
            return sky.stars[a].magnitude<sky.stars[b].magnitude;
        });
        struct Rect {double x,y,w,h;};
        std::vector<Rect> labels;
        for(int altitude : {0,30,60}){
            double ring=(90-altitude)/90.*r;
            labels.push_back({cx+3,cy-ring+2,40,15});
        }
        fl_font(FL_HELVETICA,11);
        for(int index:order){
            const auto &s=sky.stars[index]; double px,py; point(s,px,py);
            auto color=s.selected?fl_rgb_color(255,218,120):s.eligible?
                fl_rgb_color(225,234,247):fl_rgb_color(130,145,169);
            double size=std::max(2.,5-s.magnitude*.65);
            if(s.selected || index==selected){
                double ring=index==selected?11:8;
                fl_color(index==selected?fl_rgb_color(123,216,230):color);
                fl_arc(px-ring,py-ring,ring*2,ring*2,0,360);
            }
            fl_color(color); fl_pie(px-size,py-size,size*2,size*2,0,360);
            if(!names) continue;
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
                    labels.push_back(label); fl_draw(s.name,int(label.x),int(label.y+11)); break;
                }
            }
        }
        fl_pop_clip();
    }
    int handle(int event) override {
        if(event==FL_PUSH && ready){
            int nearest=-1; double distance=16;
            for(size_t i=0;i<sky.count;i++) if(sky.stars[i].altitude>0){
                double px,py; point(sky.stars[i],px,py);
                double d=std::hypot(px-Fl::event_x(),py-Fl::event_y());
                if(d<distance){nearest=int(i);distance=d;}
            }
            if(nearest>=0){selected=nearest;redraw();if(choose) choose(nearest);return 1;}
        }
        return Fl_Widget::handle(event);
    }
};
#endif
