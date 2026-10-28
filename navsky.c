/* SPDX-License-Identifier: MIT */
#include "navsky.h"
#include "libastro/astro.h"
#undef lat /* libastro observer macro; this module uses ordinary coordinate arguments */
#include "navsky_data.inc"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define SKY_RAD (3.14159265358979323846/180.)
static double sky_clamp(double x,double lo,double hi){ return x<lo?lo:x>hi?hi:x; }
static int sky_error(char *out,size_t n,const char *message){
    if(out && n) snprintf(out,n,"%s",message);
    return 1;
}
void navsky_horizontal(double gha,double dec,double lat,double lon,
                       double *alt,double *az,int *defined){
    double h=(gha+lon)*SKY_RAD, d=dec*SKY_RAD, p=lat*SKY_RAD;
    double e=-cos(d)*sin(h), north=sin(d)*cos(p)-cos(d)*cos(h)*sin(p);
    double up=sin(d)*sin(p)+cos(d)*cos(h)*cos(p);
    *alt=asin(sky_clamp(up,-1,1))/SKY_RAD;
    *defined=hypot(e,north)>1e-12;
    *az=*defined?atan2(e,north)/SKY_RAD:0;
    if(*az<0) *az+=360;
}
int navsky_jd(int y,int m,int d,int h,int minute,double s,double *jd){
    int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(y<1600 || y>2399 || m<1 || m>12 || h<0 || h>23 || minute<0 || minute>59 ||
       !isfinite(s) || s<0 || s>=60) return 1;
    if(y%4==0 && (y%100!=0 || y%400==0)) days[1]=29;
    if(d<1 || d>days[m-1]) return 1;
    *jd=nav_julian_date(y,m,d,h,minute,s);
    return 0;
}
void navsky_utc(double jd,char *date,size_t dn,char *time,size_t tn){
    double shifted=jd+.5+.5/86400., z=floor(shifted), alpha=floor((z-1867216.25)/36524.25);
    double a=z+1+alpha-floor(alpha/4), b=a+1524, c=floor((b-122.1)/365.25);
    double dd=floor(365.25*c), e=floor((b-dd)/30.6001);
    int day=(int)(b-dd-floor(30.6001*e)), month=(int)(e<14?e-1:e-13);
    int year=(int)(month>2?c-4716:c-4715), sec=(int)floor((shifted-z)*86400.);
    snprintf(date,dn,"%04d-%02d-%02d",year,month,day);
    snprintf(time,tn,"%02d:%02d:%02d",sec/3600,(sec/60)%60,sec%60);
}
typedef struct {
    int ids[NAVSKY_MAX_STARS], choices[4], best[4], n, wanted;
    double east[NAVSKY_MAX_STARS], north[NAVSKY_MAX_STARS], ease[NAVSKY_MAX_STARS];
    double score, geometry;
} SkySearch;
static void sky_search(SkySearch *s,int start,int depth,double ee,double nn,double en,
                       double e,double n,double ease){
    int i;
    if(depth==s->wanted){
        double count=s->wanted;
        double geometry=sky_clamp(4*(ee*nn-en*en)/(count*count),0,1);
        double balance=1-hypot(e,n)/count;
        double score=.65*geometry+.20*balance+.15*ease/count;
        if(score>s->score+1e-12){
            s->score=score; s->geometry=geometry;
            memcpy(s->best,s->choices,sizeof s->best);
        }
        return;
    }
    for(i=start;i<=s->n-(s->wanted-depth);i++){
        double x=s->east[i], y=s->north[i];
        s->choices[depth]=s->ids[i];
        sky_search(s,i+1,depth+1,ee+x*x,nn+y*y,en+x*y,e+x,n+y,ease+s->ease[i]);
    }
}
static int sky_sun(double jd,double lat,double lon,double *alt){
    NavAlmanac a;
    double az;
    int defined;
    if(!nav_engine_covers(jd) || nav_almanac(NAV_SUN,jd,&a)) return 1;
    navsky_horizontal(a.gha_deg,a.dec_deg,lat,lon,alt,&az,&defined);
    return 0;
}
const NavSkyStar *navsky_object(const NavSky *sky,size_t index){
    if(index<sky->count) return &sky->stars[index];
    index-=sky->count;
    if(index<sky->body_count) return &sky->bodies[index];
    index-=sky->body_count;
    return index<sky->context_count?&sky->context[index]:NULL;
}
static const char *sky_constellation(const NavStarCat *star){
    int id=cns_pick(star->ra2000_deg*SKY_RAD,star->dec2000_deg*SKY_RAD,J2000);
    return id>=0?cns_name(id)+5:"";
}
static void sky_vector(const NavSkyStar *s,double v[3]){
    double h=s->altitude*SKY_RAD,z=s->azimuth*SKY_RAD;
    v[0]=cos(h)*sin(z);v[1]=cos(h)*cos(z);v[2]=sin(h);
}
static void sky_direction(const double v[3],double *h,double *z){
    *h=atan2(v[2],hypot(v[0],v[1]))/SKY_RAD;
    *z=atan2(v[0],v[1])/SKY_RAD;if(*z<0)*z+=360;
}
/* Clip on the celestial sphere before projecting. A straight chord between
 * below-horizon endpoints would otherwise cut across the visible chart. */
static int sky_arc(NavSky *sky,const NavSkyStar *first,const NavSkyStar *last,int figure){
    double a[3],b[3],dot,angle,den;int j,n,k;
    if(first->altitude<=0 && last->altitude<=0)return 0;
    sky_vector(first,a);sky_vector(last,b);
    if(a[2]<0 || b[2]<0){
        double t=a[2]/(a[2]-b[2]),v[3],length;
        for(j=0;j<3;j++)v[j]=a[j]+t*(b[j]-a[j]);
        length=hypot(hypot(v[0],v[1]),v[2]);if(length<1e-12)return 0;
        double *clipped=a[2]<0?a:b;
        for(j=0;j<3;j++)clipped[j]=v[j]/length;
    }
    dot=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    angle=acos(sky_clamp(dot,-1,1));den=sin(angle);
    if(angle<1e-10 || fabs(den)<1e-12)return 0;
    n=(int)ceil(angle/(3*SKY_RAD));if(n<1)n=1;
    if(sky->path_count>=NAVSKY_MAX_PATHS || sky->point_count+n+1>NAVSKY_MAX_POINTS)return 1;
    NavSkyPath *path=&sky->paths[sky->path_count++];
    path->start=(unsigned short)sky->point_count;path->count=(unsigned short)(n+1);path->figure=(unsigned short)figure;
    for(k=0;k<=n;k++){
        double t=(double)k/n,v[3],sa=sin((1-t)*angle)/den,sb=sin(t*angle)/den;
        NavSkyPoint *p=&sky->points[sky->point_count++];
        for(j=0;j<3;j++)v[j]=sa*a[j]+sb*b[j];
        sky_direction(v,&p->altitude,&p->azimuth);
        p->altitude=fmax(0,p->altitude);
    }
    return 0;
}
static int sky_context(const NavSkyOptions *o,NavSky *sky,char *error,size_t capacity){
    size_t i;double centres[NAVSKY_MAX_FIGURES][3]={{0}};
    sky->context_count=sizeof sky_context_catalogue/sizeof sky_context_catalogue[0];
    sky->figure_count=sizeof sky_figure_names/sizeof sky_figure_names[0];
    if(sky->context_count>NAVSKY_MAX_CONTEXT || sky->figure_count>NAVSKY_MAX_FIGURES)
        return sky_error(error,capacity,"Constellation catalogue exceeds display capacity.");
    for(i=0;i<sky->context_count;i++){
        NavStarAlmanac a;NavSkyStar *s=&sky->context[i];
        if(nav_catalog_star_almanac(&sky_context_catalogue[i],o->jd,&a))
            return sky_error(error,capacity,nav_engine_last_error());
        s->name=a.name;s->magnitude=a.magnitude;s->is_context=1;
        s->constellation=sky_constellation(&sky_context_catalogue[i]);
        navsky_horizontal(a.gha_deg,a.dec_deg,o->latitude,o->longitude,&s->altitude,&s->azimuth,&s->azimuth_defined);
    }
    for(i=0;i<sizeof sky_figure_edges/sizeof sky_figure_edges[0];i++){
        int f=sky_figure_edges[i].figure,j,k;
        const NavSkyStar *ends[]={navsky_object(sky,sky_figure_edges[i].a),navsky_object(sky,sky_figure_edges[i].b)};
        if(!ends[0] || !ends[1])return sky_error(error,capacity,"Invalid constellation vertex.");
        if(sky_arc(sky,ends[0],ends[1],f))return sky_error(error,capacity,"Constellation path exceeds display capacity.");
        for(j=0;j<2;j++)if(ends[j]->altitude>0){
            double v[3];sky_vector(ends[j],v);
            for(k=0;k<3;k++)centres[f][k]+=v[k];
            sky->figures[f].visible=1;
        }
    }
    for(i=0;i<sky->figure_count;i++){
        sky->figures[i].name=sky_figure_names[i];
        sky_direction(centres[i],&sky->figures[i].altitude,&sky->figures[i].azimuth);
    }
    return 0;
}
int navsky_compute(const NavSkyOptions *o,NavSky *out,char *error,size_t capacity){
    size_t i;
    SkySearch search;
    memset(out,0,sizeof *out); memset(&search,0,sizeof search); search.score=-1;
    if(!isfinite(o->jd) || o->jd<nav_julian_date(1600,1,1,0,0,0) || o->jd>=nav_julian_date(2400,1,1,0,0,0) ||
       !isfinite(o->latitude) || fabs(o->latitude)>90 ||
       !isfinite(o->longitude) || fabs(o->longitude)>180)
        return sky_error(error,capacity,"Enter a valid UTC date and DR latitude/longitude.");
    if(!isfinite(o->min_altitude) || !isfinite(o->max_altitude) ||
       o->min_altitude<0 || o->max_altitude>90 || o->min_altitude>=o->max_altitude ||
       !isfinite(o->max_magnitude) || o->max_magnitude< -2 || o->max_magnitude>6 ||
       (o->requested!=3 && o->requested!=4))
        return sky_error(error,capacity,"Use 0 <= minimum altitude < maximum <= 90, magnitude -2 to 6, and 3 or 4 stars.");
    if(nav_engine_init(error,capacity)) return 1;
    if(!nav_engine_covers(o->jd)) return sky_error(error,capacity,
        "This date is outside the loaded ephemeris. Choose Classic or another ephemeris.");
    out->count=nav_star_count_get();
    if(out->count>NAVSKY_MAX_STARS) return sky_error(error,capacity,"Star catalogue exceeds the planner capacity.");
    if(sky_sun(o->jd,o->latitude,o->longitude,&out->sun_altitude))
        return sky_error(error,capacity,nav_engine_last_error());
    out->body_count=NAVSKY_BODY_COUNT;
    for(i=0;i<out->body_count;i++){
        NavAlmanac a;
        NavSkyStar *s=&out->bodies[i];
        s->body=(NavBody)i; s->is_body=1; s->name=nav_body_name(s->body);
        if(nav_almanac(s->body,o->jd,&a)) return sky_error(error,capacity,nav_engine_last_error());
        s->magnitude=a.magnitude;
        navsky_horizontal(a.gha_deg,a.dec_deg,o->latitude,o->longitude,
                          &s->altitude,&s->azimuth,&s->azimuth_defined);
    }
    out->light=out->sun_altitude>=0?"Daylight":out->sun_altitude>=-6?"Civil twilight":
        out->sun_altitude>=-12?"Nautical twilight":out->sun_altitude>=-18?"Astronomical twilight":"Night";
    for(i=0;i<out->count;i++){
        NavStarAlmanac a;
        NavSkyStar *s=&out->stars[i];
        if(nav_star_almanac(i,o->jd,&a)) return sky_error(error,capacity,nav_engine_last_error());
        s->name=a.name; s->magnitude=a.magnitude;
        s->constellation=sky_constellation(&nav_star_cat[i]);
        navsky_horizontal(a.gha_deg,a.dec_deg,o->latitude,o->longitude,
                          &s->altitude,&s->azimuth,&s->azimuth_defined);
        s->eligible=s->azimuth_defined && s->altitude>0 && s->altitude>=o->min_altitude &&
            s->altitude<=o->max_altitude && s->magnitude<=o->max_magnitude;
        if(s->eligible){
            int j=search.n++;
            search.ids[j]=(int)i; search.east[j]=sin(s->azimuth*SKY_RAD);
            search.north[j]=cos(s->azimuth*SKY_RAD);
            search.ease[j]=.6*sky_clamp((3-s->magnitude)/4.5,0,1)+
                .4*sky_clamp(1-fabs(s->altitude-45)/45,0,1);
        }
    }
    if(sky_context(o,out,error,capacity)) return 1;
    out->eligible=search.n; search.wanted=search.n<o->requested?search.n:o->requested;
    if(search.wanted){
        sky_search(&search,0,0,0,0,0,0,0,0);
        out->selected=search.wanted; out->score=search.score; out->geometry=search.geometry;
        for(i=0;i<(size_t)search.wanted;i++) out->stars[search.best[i]].selected=1;
    }
    return 0;
}
int navsky_twilight(double jd,double lat,double lon,int morning,double *result,char *error,size_t capacity){
    double prev, a=jd, b, current;
    int i,j;
    if(!isfinite(jd) || !isfinite(lat) || fabs(lat)>90 || !isfinite(lon) || fabs(lon)>180)
        return sky_error(error,capacity,"Enter a valid UTC date and observer position first.");
    if(sky_sun(a,lat,lon,&prev)) return sky_error(error,capacity,"Sun position is outside the available ephemeris.");
    for(i=1;i<=288;i++){
        b=jd+i/144.; /* ten-minute brackets over 48 hours */
        if(sky_sun(b,lat,lon,&current)) return sky_error(error,capacity,"Search extends beyond the available ephemeris.");
        if(morning?(prev< -8 && current>=-8):(prev> -8 && current<=-8)){
            for(j=0;j<25;j++){
                double mid=(a+b)/2, alt;
                if(sky_sun(mid,lat,lon,&alt)) return sky_error(error,capacity,"Could not calculate twilight.");
                if(morning?(alt< -8):(alt> -8)) a=mid; else b=mid;
            }
            *result=(a+b)/2; return 0;
        }
        a=b; prev=current;
    }
    return sky_error(error,capacity,"No matching Sun -8 degree crossing in the next 48 hours; try another date or position.");
}
