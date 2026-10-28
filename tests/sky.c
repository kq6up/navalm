/* SPDX-License-Identifier: MIT */
#include "navsky.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define R (3.14159265358979323846/180.)
static double difference(double a,double b){return fabs(remainder(a-b,360));}
static void horizontal(double gha,double dec,double lat,double lon,double altitude,double azimuth,int defined){
    double h,z;int valid;
    navsky_horizontal(gha,dec,lat,lon,&h,&z,&valid);
    assert(fabs(h-altitude)<1e-6 && valid==defined);
    if(defined) assert(difference(z,azimuth)<1e-6);
}
int main(void){
    double jd;char error[512],date[32],time[32];
    int engine,trial;
    horizontal(90,0,0,0,0,270,1); horizontal(270,0,0,0,0,90,1);
    horizontal(0,30,0,0,60,0,1); horizontal(0,-30,0,0,60,180,1);
    horizontal(0,0,0,90,0,270,1); horizontal(0,0,0,0,90,0,0);
    horizontal(0,0,0,180,-90,0,0);
    assert(navsky_jd(1700,2,29,0,0,0,&jd));
    assert(navsky_jd(2026,1,1,0,0,NAN,&jd));
    assert(navsky_jd(2026,1,1,0,0,60,&jd));
    for(engine=1600;engine<=2399;engine+=7){
        assert(!navsky_jd(engine,12,31,23,59,59,&jd));
        navsky_utc(jd,date,sizeof date,time,sizeof time);
        char expected[32];snprintf(expected,sizeof expected,"%04d-12-31",engine);
        assert(!strcmp(date,expected) && !strcmp(time,"23:59:59"));
    }
    assert(!navsky_jd(2000,2,28,23,59,59.9,&jd));
    navsky_utc(jd,date,sizeof date,time,sizeof time);
    assert(!strcmp(date,"2000-02-29") && !strcmp(time,"00:00:00"));
    for(engine=0;engine<2;engine++){
        nav_engine_select(engine?"classic":"precise");assert(!nav_engine_init(error,sizeof error));
        for(trial=0;trial<120;trial++){
            NavSky sky;NavSkyOptions o;
            int selected=0,eligible=0;size_t i;
            o.jd=nav_julian_date(2026,1+trial%12,1+trial%27,trial%24,trial%60,0);
            o.latitude=-89+178.*rand()/RAND_MAX;o.longitude=-180+360.*rand()/RAND_MAX;
            o.min_altitude=15;o.max_altitude=75;o.max_magnitude=trial%2?3:1.5;o.requested=trial%2?3:4;
            assert(!navsky_compute(&o,&sky,error,sizeof error));assert(sky.count==58);
            for(i=0;i<sky.count;i++){
                NavStarAlmanac a;double h,z,c;
                NavSkyStar *s=&sky.stars[i];assert(!nav_star_almanac(i,o.jd,&a));
                h=asin(sin(o.latitude*R)*sin(a.dec_deg*R)+cos(o.latitude*R)*cos(a.dec_deg*R)*cos((a.gha_deg+o.longitude)*R))/R;
                assert(fabs(s->altitude-h)<1e-7);
                c=(sin(a.dec_deg*R)-sin(h*R)*sin(o.latitude*R))/(cos(h*R)*cos(o.latitude*R));
                c=fmax(-1,fmin(1,c));z=acos(c)/R;
                if(sin((a.gha_deg+o.longitude)*R)>0) z=360-z;
                assert(difference(s->azimuth,z)<1e-6);
                if(s->eligible) eligible++;
                if(s->selected){assert(s->eligible);selected++;}
            }
            assert(eligible==sky.eligible && selected==sky.selected);
            assert(selected==(eligible<o.requested?eligible:o.requested));
            assert(sky.geometry>=0 && sky.geometry<=1 && sky.score>=0 && sky.score<=1);
        }
        {
            NavSky sky;NavSkyOptions o={nav_julian_date(2026,10,27,12,0,0),34,-117,15,75,3,3};
            double next, altitude, az;int defined,morning;
            for(morning=0;morning<2;morning++){
                NavAlmanac a;
                assert(!navsky_twilight(o.jd,34,-117,morning,&next,error,sizeof error));
                assert(next>o.jd && next<o.jd+2);
                assert(!nav_almanac(NAV_SUN,next,&a));
                navsky_horizontal(a.gha_deg,a.dec_deg,34,-117,&altitude,&az,&defined);
                assert(fabs(altitude+8)<1e-5);
                assert(!nav_almanac(NAV_SUN,next+1./1440,&a));
                navsky_horizontal(a.gha_deg,a.dec_deg,34,-117,&altitude,&az,&defined);
                assert(morning?altitude> -8:altitude< -8);
            }
            assert(navsky_twilight(nav_julian_date(2026,6,21,0,0,0),90,0,1,&next,error,sizeof error));
            assert(strstr(error,"48 hours"));
            o.latitude=NAN;assert(navsky_compute(&o,&sky,error,sizeof error));o.latitude=34;
            o.min_altitude=80;assert(navsky_compute(&o,&sky,error,sizeof error));o.min_altitude=15;
            o.max_magnitude=-2;assert(!navsky_compute(&o,&sky,error,sizeof error));assert(sky.selected==0);
            { NavStarAlmanac a;assert(!nav_star_almanac(0,o.jd,&a));
              o.latitude=a.dec_deg;o.longitude=remainder(-a.gha_deg,360);o.max_altitude=90;o.max_magnitude=6;
              assert(!navsky_compute(&o,&sky,error,sizeof error));
              assert(!sky.stars[0].azimuth_defined && !sky.stars[0].selected); }
        }
    }
    puts("Sky: cardinal/zenith cases, UTC round trips, 13,920 independent star-coordinate checks,");
    puts("filters, 3/4-star sets, twilight directions, polar no-event and invalid inputs passed.");
    nav_engine_shutdown();return 0;
}
