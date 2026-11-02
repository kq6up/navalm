/* SPDX-License-Identifier: MIT */
#ifndef NAVFLTK_CHRONO_H
#define NAVFLTK_CHRONO_H
#include "navchrono_sight.h"
#include <FL/Fl.H>
#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>
#include <functional>

class ChronoPlot : public Fl_Widget {
  public:
    struct Point {size_t index;double utc,error;bool enabled;};
    std::vector<Point> points;
    chrono_nav::Model model;
    int series=1;
    long selected=-1;
    double first=0,last=1,low=-1,high=1;
    std::function<void(size_t)> select;
    ChronoPlot(int x,int y,int w,int h):Fl_Widget(x,y,w,h){}
    void set_data(const chrono_nav::Log &log,int clock,int s,long index=-1) {
        points.clear();series=s;selected=index;model={};
        for(size_t i=0;i<log.readings.size();++i){const auto &r=log.readings[i];
            if(r.clock==clock && r.series==s)points.push_back({i,r.utc,r.watch-r.utc,r.enabled});
        }
        if(points.empty()){redraw();return;}
        first=last=points.front().utc;low=high=points.front().error;
        for(const auto &p:points){first=std::min(first,p.utc);last=std::max(last,p.utc);low=std::min(low,p.error);high=std::max(high,p.error);}
        try{model=chrono_nav::fit(log.readings,clock,s);}catch(const std::exception &){/* Keep bad points visible even when the fit is unusable. */}
        if(model.count){low=std::min({low,model.error_at(first),model.error_at(last)});high=std::max({high,model.error_at(first),model.error_at(last)});}
        double pad=std::max(.1,(high-low)*.12);low-=pad;high+=pad;
        if(last==first){first-=43200.;last+=43200.;}
        else {double pad_t=(last-first)*.04;first-=pad_t;last+=pad_t;}
        redraw();
    }
    double px(double utc) const {return x()+64+(utc-first)/(last-first)*(w()-84);}
    double py(double error) const {return y()+h()-48-(error-low)/(high-low)*(h()-98);}
    long hit(double x,double y) const {
        long found=-1;double nearest=12*12;
        for(const auto &p:points){double dx=px(p.utc)-x,dy=py(p.error)-y,d=dx*dx+dy*dy;
            if(d<=nearest){found=long(p.index);nearest=d;}}
        return found;
    }
    int handle(int event) override {
        if(event==FL_PUSH && Fl::event_button()==FL_LEFT_MOUSE){auto i=hit(Fl::event_x(),Fl::event_y());if(i>=0 && select)select(size_t(i));return 1;}
        return Fl_Widget::handle(event);
    }
    void draw() override {
        fl_push_clip(x(),y(),w(),h());fl_color(FL_WHITE);fl_rectf(x(),y(),w(),h());
        fl_color(fl_rgb_color(188,201,204));fl_rect(x(),y(),w(),h());
        fl_color(fl_rgb_color(21,43,57));fl_font(FL_HELVETICA_BOLD,12);
        auto title="Watch − UTC (s) · series "+std::to_string(series);
        fl_draw(title.c_str(),x()+12,y()+19);
        fl_font(FL_HELVETICA,11);
        fl_draw(model.count?"● included   × excluded   — fitted drift":"● included   × excluded   (no usable fit)",x()+12,y()+36);
        if(points.empty()){fl_draw("No comparisons in this series.",x()+22,y()+h()/2);fl_pop_clip();return;}
        for(int i=0;i<3;++i){double v=low+(high-low)*i/2;int yy=int(std::round(py(v)));
            fl_color(fl_rgb_color(227,234,236));fl_line(x()+64,yy,x()+w()-20,yy);
            char b[40];std::snprintf(b,sizeof b,"%.2f",v);fl_color(fl_rgb_color(65,83,93));fl_draw(b,x()+3,yy-7,56,14,FL_ALIGN_RIGHT);
        }
        fl_color(fl_rgb_color(65,83,93));fl_line(x()+64,y()+50,x()+64,y()+h()-48);fl_line(x()+64,y()+h()-48,x()+w()-20,y()+h()-48);
        // The axis origin is an actual observation, not the padded viewport edge.
        double origin=points.front().utc,finish=origin;for(const auto &p:points){origin=std::min(origin,p.utc);finish=std::max(finish,p.utc);}
        for(int i=0;i<3;++i){double utc=finish>origin?origin+(finish-origin)*i/2:first+(last-first)*i/2;char b[40];std::snprintf(b,sizeof b,"%.2f",(utc-origin)/86400.);
            int xx=int(std::round(px(utc)));fl_draw(b,xx-28,y()+h()-44,56,16,FL_ALIGN_CENTER);
        }
        auto axis="Reference UTC · days from "+chrono_nav::stamp(origin);
        fl_draw(axis.c_str(),x()+64,y()+h()-10);
        fl_push_clip(x()+59,y()+45,w()-74,h()-88);
        if(model.count){fl_color(fl_rgb_color(193,112,28));fl_line_style(FL_SOLID,2);
            fl_line(int(px(first)),int(py(model.error_at(first))),int(px(last)),int(py(model.error_at(last))));fl_line_style(FL_SOLID);}
        for(const auto &p:points){int xx=int(std::round(px(p.utc))),yy=int(std::round(py(p.error)));
            fl_color(p.enabled?fl_rgb_color(0,119,128):fl_rgb_color(116,124,132));
            if(p.enabled)fl_pie(xx-4,yy-4,8,8,0,360);
            else {fl_line_style(FL_SOLID,2);fl_line(xx-4,yy-4,xx+4,yy+4);fl_line(xx-4,yy+4,xx+4,yy-4);fl_line_style(FL_SOLID);}
            if(long(p.index)==selected){fl_color(fl_rgb_color(202,94,20));fl_line_style(FL_SOLID,2);fl_arc(xx-8,yy-8,16,16,0,360);fl_line_style(FL_SOLID);}
        }
        fl_pop_clip();fl_pop_clip();
    }
};
#endif
