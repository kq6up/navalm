/* SPDX-License-Identifier: MIT */
#ifndef NAVFLTK_CLUSTER_H
#define NAVFLTK_CLUSTER_H

#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

class SightClusterPlot : public Fl_Widget {
  public:
    struct Point { double sec, ho, fit; int id; bool kept; };
    std::vector<Point> points;
    double ref_sec=0,ref_ho=0;
    std::string caption,epoch;
    SightClusterPlot(int x,int y,int w,int h):Fl_Widget(x,y,w,h){}
    void draw() override {
        fl_push_clip(x(),y(),w(),h());
        fl_color(FL_WHITE); fl_rectf(x(),y(),w(),h());
        fl_color(fl_rgb_color(185,198,204)); fl_rect(x(),y(),w(),h());
        fl_color(fl_rgb_color(26,50,63)); fl_font(FL_HELVETICA_BOLD,14);
        fl_draw(caption.c_str(),x()+16,y()+24);
        fl_font(FL_HELVETICA,12);
        fl_draw("● Used     × Excluded     — Fitted altitude     ◇ Representative",x()+16,y()+47);
        if(points.empty()){fl_pop_clip();return;}
        double low=ref_ho,high=ref_ho;
        for(const auto &p:points){
            low=std::min({low,p.ho,p.fit}); high=std::max({high,p.ho,p.fit});
        }
        const double pad=std::max(.02,(high-low)*.09);
        low-=pad; high+=pad;
        const double span=points.back().sec;
        const int left=x()+78,right=x()+w()-30,top=y()+67,bottom=y()+h()-69;
        auto px=[&](double t){return int(std::lround(span>0?left+12+t/span*(right-left-24):(left+right)/2.0));};
        auto py=[&](double a){return int(std::lround(bottom-(a-low)/(high-low)*(bottom-top)));};
        fl_font(FL_HELVETICA,11);
        for(int i=0;i<=5;i++){
            double value=low+(high-low)*i/5;
            int yy=py(value),min10=int(std::round(std::fabs(value)*600));
            char b[40]; std::snprintf(b,sizeof b,"%s%d° %02d.%d′",value<0?"−":"",min10/600,(min10/10)%60,min10%10);
            fl_color(fl_rgb_color(228,235,239)); fl_line(left,yy,right,yy);
            fl_color(fl_rgb_color(56,75,88)); fl_draw(b,x()+4,yy-7,68,14,FL_ALIGN_RIGHT);
        }
        fl_color(fl_rgb_color(56,75,88)); fl_line(left,top,left,bottom); fl_line(left,bottom,right,bottom);
        for(int i=0;i<=(span>0?4:0);i++){
            double t=span*i/4; int xx=px(t);
            char b[32]; std::snprintf(b,sizeof b,"%.1f",t);
            fl_line(xx,bottom,xx,bottom+4);
            fl_draw(b,xx-32,bottom+6,64,15,FL_ALIGN_CENTER);
        }
        std::string axis="Seconds from "+epoch+" UTC";
        fl_draw(axis.c_str(),left,bottom+28,right-left,18,FL_ALIGN_CENTER);
        fl_color(fl_rgb_color(173,105,26)); fl_line_style(FL_SOLID,2);
        for(size_t i=1;i<points.size();i++)
            fl_line(px(points[i-1].sec),py(points[i-1].fit),px(points[i].sec),py(points[i].fit));
        fl_line_style(FL_SOLID);
        for(const auto &p:points){
            int xx=px(p.sec),yy=py(p.ho);
            if(p.kept){
                fl_color(fl_rgb_color(0,111,126)); fl_pie(xx-4,yy-4,8,8,0,360);
            } else {
                fl_color(fl_rgb_color(175,62,52)); fl_line_style(FL_SOLID,2);
                fl_line(xx-5,yy-5,xx+5,yy+5); fl_line(xx-5,yy+5,xx+5,yy-5);
                fl_line_style(FL_SOLID);
                std::string id="#"+std::to_string(p.id);
                fl_draw(id.c_str(),xx+7,yy-6);
            }
        }
        int xx=px(ref_sec),yy=py(ref_ho);
        fl_color(fl_rgb_color(173,105,26)); fl_line_style(FL_SOLID,2);
        fl_loop(xx-7,yy,xx,yy-7,xx+7,yy,xx,yy+7);
        fl_line_style(FL_SOLID);
        fl_pop_clip();
    }
};
#endif
