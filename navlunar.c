/* SPDX-License-Identifier: MIT
 * Forward model: NOVAS/DE440s topocentric directions, refracted lunar/solar
 * circumference, raw sextant angles. Synthetic altitudes are not constraints.
 * All real shots retain their recorded epochs and share UTC-minus-watch.
 */
#include "navlunar.h"
#include "nav_engine.h"
#include "navsky.h"
#include "nav_version.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#ifndef NAVALM_NO_PRECISE
#include "novas.h"
#endif
#define R (0.017453292519943295769)
#define AU 149597870.7
typedef struct {double x,y,z;} Vec;
typedef struct {Vec v;double alt,az,sd;NavAlmanac geo;} Place;
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 1;}
static double clamp(double v){return fmax(-1,fmin(1,v));}
static Vec direction(double az,double alt){Vec v={cos(alt*R)*cos(az*R),cos(alt*R)*sin(az*R),sin(alt*R)};return v;}
static double dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static Vec cross(Vec a,Vec b){Vec v={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};return v;}
static Vec unit(Vec a){double n=sqrt(dot(a,a));Vec v={a.x/n,a.y/n,a.z/n};return v;}
static double angle(Vec a,Vec b){Vec v=cross(a,b);return atan2(sqrt(dot(v,v)),dot(a,b))/R;}
static double jd(const Sight *s){return nav_julian_date(s->y,s->mo,s->d,s->h,s->mi,s->s);}
static double wrap(double x){x=fmod(x+180,360);return (x<0?x+360:x)-180;}
static int body_number(const char *b){
    const char *names[]={"sun","moon","mercury","venus","mars","jupiter","saturn"};
    for(int i=0;i<7;i++)if(!strcmp(b,names[i]))return i;
    return !strcmp(b,"star")?7:-1;
}
static void position(const NavLunarRequest *q,double lat,double lon,double days,double *la,double *lo){
    *la=lat;*lo=lon;
    if(!q->distance.has_motion)return;
    double t=days*24/60, c=q->distance.course*R,s=q->distance.set*R;
    double north=t*(q->distance.speed*cos(c)+q->distance.drift*cos(s));
    double east=t*(q->distance.speed*sin(c)+q->distance.drift*sin(s));
    *la=lat+north;double co=cos((lat+*la)/2*R);
    *lo=wrap(lon+east/co);
}
static double apparent(double h,const Sight *s){
    double a=h;
    if(s->press_mb==0)return h;
    for(int i=0;i<12;i++)a=h+(s->press_mb/1010)*(283/(273+s->temp_c))/60/tan((a+7.31/(a+4.4))*R);
    return a;
}
static Vec refracted(Vec v,const Sight *s){return direction(atan2(v.y,v.x)/R,apparent(atan2(v.z,hypot(v.x,v.y))/R,s));}
static Vec limb(Vec m,Vec u,Vec v,double sd,double t){
    Vec l={m.x*cos(sd*R)+(u.x*cos(t)+v.x*sin(t))*sin(sd*R),
           m.y*cos(sd*R)+(u.y*cos(t)+v.y*sin(t))*sin(sd*R),
           m.z*cos(sd*R)+(u.z*cos(t)+v.z*sin(t))*sin(sd*R)};return l;
}
/* Angular radius of the refracted disc in the apparent contact direction.
 * Extremize the full circumference rather than adding a circular SD after
 * refraction. Sequential extrema are sufficient at the documented 0.1' scale. */
static Vec contact(const Place *p,Vec target,int near,const Sight *s){
    if(!near||p->sd==0)return refracted(p->v,s);
    Vec z={0,0,1},u=cross(p->v,z);if(dot(u,u)<1e-8){z.x=1;z.z=0;u=cross(p->v,z);}u=unit(u);
    Vec v=unit(cross(p->v,u));double best=near>0?1e9:-1,theta=0,step=6.283185307179586/36;
    for(int i=0;i<36;i++){
        double a=angle(refracted(limb(p->v,u,v,p->sd,i*step),s),target);
        if((near>0&&a<best)||(near<0&&a>best)){best=a;theta=i*step;}
    }
    double lo=theta-step,hi=theta+step;
    for(int i=0;i<28;i++){
        double a=lo+(hi-lo)/3,b=hi-(hi-lo)/3;
        double da=angle(refracted(limb(p->v,u,v,p->sd,a),s),target),db=angle(refracted(limb(p->v,u,v,p->sd,b),s),target);
        if((near>0&&da<db)||(near<0&&da>db))hi=b;else lo=a;
    }
    return refracted(limb(p->v,u,v,p->sd,(lo+hi)/2),s);
}
static int lunar_place(const char *body,const char *star,double when,double lat,double lon,double height,
                 const Sight *atmos,Place *p,char *e,size_t n){
#ifdef NAVALM_NO_PRECISE
    (void)body;(void)star;(void)when;(void)lat;(void)lon;(void)height;(void)atmos;(void)p;
    return fail(e,n,"Lunars require a build with NOVAS and the JPL ephemeris.");
#else
    double ra,dec,dis,zd,rar,decr,dt=nav_delta_t_seconds(when);int b=body_number(body);
    on_surface obs;make_on_surface(lat,lon,height,atmos->temp_c,atmos->press_mb,&obs);
    memset(p,0,sizeof *p);
    if(!nav_engine_covers(when))return fail(e,n,"JPL ephemeris does not cover a trial UTC; narrow the search or supply the correct ephemeris.");
    if(b==7){
        int i=nav_star_find(star);if(i<0)return fail(e,n,"Choose a named navigation star.");
        const NavStarCat *sc=&nav_star_cat[i];cat_entry cat;char name[SIZE_OF_OBJ_NAME],catalog[SIZE_OF_CAT_NAME];
        snprintf(name,sizeof name,"%s",sc->name);snprintf(catalog,sizeof catalog,"NAV");
        if(make_cat_entry(name,catalog,1,sc->ra2000_deg/15,sc->dec2000_deg,sc->pm_ra_mas,sc->pm_dec_mas,0,0,&cat)||
           topo_star(when+dt/86400,dt,&cat,&obs,0,&ra,&dec))return fail(e,n,"NOVAS could not calculate the selected star.");
        NavStarAlmanac a;if(nav_star_almanac(i,when,&a))return fail(e,n,nav_engine_last_error());
        p->geo.ra_deg=a.ra_deg;p->geo.dec_deg=a.dec_deg;
    }else{
        static const short nums[]={10,11,1,2,4,5,6};static const double radii[]={696000,1738.1,2439.7,6051.8,3396.19,71492,60268};
        object o;char name[SIZE_OF_OBJ_NAME];snprintf(name,sizeof name,"%s",body);
        if(b<0||make_object(0,nums[b],name,NULL,&o)||topo_planet(when+dt/86400,&o,dt,&obs,0,&ra,&dec,&dis))
            return fail(e,n,"NOVAS could not calculate a topocentric body position.");
        p->sd=asin(radii[b]/(dis*AU))/R;
        if(nav_almanac((NavBody)b,when,&p->geo))return fail(e,n,nav_engine_last_error());
    }
    equ2hor(when,dt,0,0,0,&obs,ra,dec,0,&zd,&p->az,&rar,&decr);
    p->alt=90-zd;p->v=direction(p->az,p->alt);
    if(!isfinite(p->alt)||p->alt-p->sd<5||p->alt+p->sd>89.8)
        return fail(e,n,"Lunar model requires both complete discs between 5 and 89.8 degrees altitude at every shot epoch.");
    return 0;
#endif
}
static double raw_hs(const Place *p,const Sight *s){
    double altitude=apparent(p->alt-s->limb*p->sd,s),dip=0;
    if(!s->artificial){
        dip=(s->he_metres?1.76:.97)*sqrt(s->he)/60;
        if(s->short_horizon_nm>0){double feet=s->he_metres?s->he/.3048:s->he,d=s->short_horizon_nm;
            dip=atan(feet/(6076.1*d)+.8321*d/(2*3440.1))/R;}
    }
    return (altitude+dip)*(s->artificial?2:1)+(s->ie_on_arc?1:-1)*fabs(s->ie_min)/60;
}
void navlunar_init(NavLunarRequest *q){
    memset(q,0,sizeof *q);strcpy(q->distance.body,"jupiter");q->distance.y=2026;q->distance.mo=q->distance.d=1;
    q->distance.temp_c=10;q->distance.press_mb=1010;q->moon.sight=q->other.sight=q->distance;
    strcpy(q->moon.sight.body,"moon");q->moon.sight.limb=1;q->moon_contact=1;
    q->search_seconds=600;q->distance_sigma_min=.2;q->altitude_sigma_min=1;
}
int navlunar_validate(const NavLunarRequest *q,char *e,size_t n){
    if(!q)return fail(e,n,"Missing lunar observation.");
    Sight d=q->distance;double distance=d.hs_deg;d.hs_deg=45;d.artificial=0;d.short_horizon_nm=0;
    if(navsight_validate(&d,e,n))return 1;
    int b=body_number(q->distance.body);
    if(b<0||b==1||(b==7&&nav_star_find(q->distance.star)<0))return fail(e,n,"Select Sun, a planet or a named navigation star as the other body.");
    if(!isfinite(distance)||distance<1||distance>150)return fail(e,n,"Enter a raw lunar distance from 1 to 150 degrees.");
    if(q->moon_contact < -1||q->moon_contact>1||q->other_contact < -1||q->other_contact>1)
        return fail(e,n,"Invalid near/centre/far distance contact.");
    if(b==7&&q->other_contact)return fail(e,n,"A star uses a centre contact.");
    if(!isfinite(q->initial_correction_sec)||fabs(q->initial_correction_sec)>604800||
       !isfinite(q->search_seconds)||q->search_seconds<1||q->search_seconds>21600)
        return fail(e,n,"Use a starting correction within seven days and a search half span from 1 to 21600 seconds.");
    if(!isfinite(q->observer_height_m)||q->observer_height_m < -500||q->observer_height_m>10000||fabs(d.dr_lat)>85)
        return fail(e,n,"Use observer elevation -500 to 10000 m and latitude within 85 degrees.");
    if(!isfinite(q->distance_sigma_min)||q->distance_sigma_min<=0||q->distance_sigma_min>60||
       !isfinite(q->altitude_sigma_min)||q->altitude_sigma_min<=0||q->altitude_sigma_min>60)
        return fail(e,n,"Angle uncertainties must be greater than zero and no more than 60 minutes.");
    if((q->refine_position!=0&&q->refine_position!=1)||(d.has_motion!=0&&d.has_motion!=1)||
       (d.ie_on_arc!=0&&d.ie_on_arc!=1))return fail(e,n,"Invalid position, motion or index switch.");
    if(q->refine_position&&(!q->moon.measured||!q->other.measured))
        return fail(e,n,"Refining position needs two real altitude shots. Keep AP fixed for synthetic or mixed inputs.");
    const NavLunarAltitude *a[]={&q->moon,&q->other};
    for(int i=0;i<2;i++){
        Sight s=a[i]->sight;if(!a[i]->measured){s.y=q->distance.y;s.mo=q->distance.mo;s.d=q->distance.d;
            s.h=q->distance.h;s.mi=q->distance.mi;s.s=q->distance.s;s.hs_deg=45;}
        s.dr_lat=d.dr_lat;s.dr_lon=d.dr_lon;
        strcpy(s.body,i?q->distance.body:"moon");snprintf(s.star,sizeof s.star,"%s",q->distance.star);
        if(navsight_validate(&s,e,n))return 1;
        if(a[i]->sight.limb < -1||a[i]->sight.limb>1)return fail(e,n,"Altitude limb must be lower, centre or upper.");
        if((a[i]->measured!=0&&a[i]->measured!=1)||(s.artificial!=0&&s.artificial!=1)||
           (s.ie_on_arc!=0&&s.ie_on_arc!=1)||(s.he_metres!=0&&s.he_metres!=1))return fail(e,n,"Invalid altitude source or instrument switch.");
        if(a[i]->measured&&fabs(jd(&s)-jd(&q->distance))>.5)return fail(e,n,"Real altitude epochs must be within 12 hours of the lunar distance; enter the full date across midnight.");
    }
    if(nav_engine_current()!=NAV_ENGINE_PRECISE)return fail(e,n,"Lunars use the Precise NOVAS + JPL engine; select Precise first.");
    return nav_engine_init(e,n);
}
int navlunar_predict(const NavLunarRequest *q,double correction,double lat,double lon,NavLunarSolution *r,char *e,size_t n){
    if(!isfinite(correction)||!isfinite(lat)||!isfinite(lon)||fabs(lat)>85)return fail(e,n,"Invalid trial position or clock correction.");
    double when=jd(&q->distance)+correction/86400;Place m,b;memset(r,0,sizeof *r);
    if(lunar_place("moon","",when,lat,lon,q->observer_height_m,&q->distance,&m,e,n)||
       lunar_place(q->distance.body,q->distance.star,when,lat,lon,q->observer_height_m,&q->distance,&b,e,n))return 1;
    Vec mb=refracted(m.v,&q->distance),bb=refracted(b.v,&q->distance),mc=mb,bc=bb;
    for(int i=0;i<3;i++){mc=contact(&m,bc,q->moon_contact,&q->distance);bc=contact(&b,mc,q->other_contact,&q->distance);}
    r->correction_sec=correction;r->additional_sec=correction-q->initial_correction_sec;r->utc_jd=when;r->lat=lat;r->lon=wrap(lon);
    r->raw_distance_deg=angle(mc,bc)+(q->distance.ie_on_arc?1:-1)*fabs(q->distance.ie_min)/60;
    r->topocentric_deg=angle(m.v,b.v);r->apparent_deg=angle(mb,bb);r->moon_sd_min=m.sd*60;
    r->geocentric_deg=angle(direction(m.geo.ra_deg,m.geo.dec_deg),direction(b.geo.ra_deg,b.geo.dec_deg));
    r->centre_alt_deg[0]=apparent(m.alt,&q->distance);r->centre_alt_deg[1]=apparent(b.alt,&q->distance);
    r->azimuth_deg[0]=m.az;r->azimuth_deg[1]=b.az;
    r->residual_min[0]=(r->raw_distance_deg-q->distance.hs_deg)*60;
    const NavLunarAltitude *a[]={&q->moon,&q->other};
    for(int i=0;i<2;i++){
        double epoch=a[i]->measured?jd(&a[i]->sight)+correction/86400:when,la,lo;Place p;
        position(q,lat,lon,epoch-when,&la,&lo);
        if(fabs(la)>85)return fail(e,n,"Motion carries an altitude position outside the supported latitude range.");
        const Sight *settings=&a[i]->sight;
        if(lunar_place(i?q->distance.body:"moon",q->distance.star,epoch,la,lo,q->observer_height_m,settings,&p,e,n))return 1;
        r->altitude_jd[i]=epoch;r->predicted_hs[i]=raw_hs(&p,settings);
        r->residual_min[i+1]=a[i]->measured?(r->predicted_hs[i]-a[i]->sight.hs_deg)*60:0;
    }
    return 0;
}
static double proximity(double lat,double lon,const Sight *s){
    return acos(clamp(sin(lat*R)*sin(s->dr_lat*R)+cos(lat*R)*cos(s->dr_lat*R)*cos((lon-s->dr_lon)*R)))/R;
}
static int linear3(double a[3][3],double b[3],double x[3]){
    double m[3][4];for(int i=0;i<3;i++){for(int j=0;j<3;j++)m[i][j]=a[i][j];m[i][3]=b[i];}
    for(int i=0;i<3;i++){
        int p=i;for(int j=i+1;j<3;j++)if(fabs(m[j][i])>fabs(m[p][i]))p=j;
        if(fabs(m[p][i])<1e-10)return 1;
        for(int j=0;j<4;j++){double t=m[p][j];m[p][j]=m[i][j];m[i][j]=t;}
        double scale=m[i][i];for(int j=0;j<4;j++)m[i][j]/=scale;
        for(int k=0;k<3;k++)if(k!=i){scale=m[k][i];for(int j=0;j<4;j++)m[k][j]-=scale*m[i][j];}
    }
    for(int i=0;i<3;i++)x[i]=m[i][3];return 0;
}
static double norm(const NavLunarSolution *r){double n=0;for(int i=0;i<3;i++)n+=r->residual_min[i]*r->residual_min[i];return n;}
static int jacobian(const NavLunarRequest *q,const double x[3],double j[3][3]){
    const double step[]={2,.001,.001};char e[256];
    for(int k=0;k<3;k++){
        double a[3]={x[0],x[1],x[2]},b[3]={x[0],x[1],x[2]};a[k]+=step[k];b[k]-=step[k];NavLunarSolution ra,rb;
        if(navlunar_predict(q,a[0],a[1],a[2],&ra,e,sizeof e)||navlunar_predict(q,b[0],b[1],b[2],&rb,e,sizeof e))return 1;
        for(int i=0;i<3;i++)j[i][k]=(ra.residual_min[i]-rb.residual_min[i])/(2*step[k]);
    }return 0;
}
static int joint(const NavLunarRequest *q,double x[3],NavLunarSolution *r){
    char e[256];double low=q->initial_correction_sec-q->search_seconds,high=q->initial_correction_sec+q->search_seconds;
    for(int it=0;it<24;it++){
        if(navlunar_predict(q,x[0],x[1],x[2],r,e,sizeof e))return 1;
        double old=norm(r);if(old<1e-9)return 0;
        double j[3][3],rhs[3],dx[3];if(jacobian(q,x,j))return 1;
        for(int i=0;i<3;i++)rhs[i]=-r->residual_min[i];if(linear3(j,rhs,dx))return 1;
        int accepted=0;double factor=1;
        for(int k=0;k<12;k++,factor*=.5){
            double y[3]={x[0]+factor*dx[0],x[1]+factor*dx[1],wrap(x[2]+factor*dx[2])};NavLunarSolution trial;
            if(y[0]<low||y[0]>high||fabs(y[1])>85||navlunar_predict(q,y[0],y[1],y[2],&trial,e,sizeof e))continue;
            if(norm(&trial)<old){memcpy(x,y,sizeof y);accepted=1;break;}
        }if(!accepted)return 1;
    }return 1;
}
static void append(const NavLunarRequest *q,NavLunarResult *out,NavLunarSolution r){
    for(int i=0;i<out->count;i++)if(fabs(out->solutions[i].correction_sec-r.correction_sec)<.1&&
       proximity(r.lat,r.lon,&(Sight){.dr_lat=out->solutions[i].lat,.dr_lon=out->solutions[i].lon})<.001)return;
    if(out->count>=NAVLUNAR_MAX_SOLUTIONS)return;
    NavLunarSolution a,b;char e[256];
    if(!navlunar_predict(q,r.correction_sec-2,r.lat,r.lon,&a,e,sizeof e)&&!navlunar_predict(q,r.correction_sec+2,r.lat,r.lon,&b,e,sizeof e)){
        r.rate_min_per_minute=(b.raw_distance_deg-a.raw_distance_deg)*900;
        if(fabs(r.rate_min_per_minute)>1e-6)r.sigma_time_sec=q->distance_sigma_min*60/fabs(r.rate_min_per_minute);
    }
    if(q->refine_position){
        double x[]={r.correction_sec,r.lat,r.lon},j[3][3],variance=0;
        if(!jacobian(q,x,j))for(int k=0;k<3;k++){
            double rhs[]={0,0,0},dx[3];rhs[k]=k?q->altitude_sigma_min:q->distance_sigma_min;
            if(linear3(j,rhs,dx)){variance=-1;break;}variance+=dx[0]*dx[0];
        }
        if(variance>0)r.sigma_time_sec=sqrt(variance);else r.sigma_time_sec=0;
    }
    r.weak=r.sigma_time_sec==0||r.sigma_time_sec>q->search_seconds||fabs(r.rate_min_per_minute)<.05;
    int at=out->count++;while(at>0&&proximity(out->solutions[at-1].lat,out->solutions[at-1].lon,&q->distance)>proximity(r.lat,r.lon,&q->distance)){
        out->solutions[at]=out->solutions[at-1];at--;}
    out->solutions[at]=r;
}
int navlunar_solve(const NavLunarRequest *q,NavLunarResult *out,char *e,size_t n){
    memset(out,0,sizeof *out);if(navlunar_validate(q,e,n))return 1;
    if(q->refine_position){
        /* DR plus an all-longitude/latitude seed grid locates the two usual
         * altitude-circle branches. Convergence is checked in raw angles.
         * This bounded local solver does not claim to find all global roots. */
        double x[]={q->initial_correction_sec,q->distance.dr_lat,q->distance.dr_lon};NavLunarSolution r;
        if(!joint(q,x,&r))append(q,out,r);out->scanned++;
        for(int la=-60;la<=60;la+=30)for(int lo=-180;lo<180;lo+=45){
            x[0]=q->initial_correction_sec;x[1]=la;x[2]=lo;
            if(!joint(q,x,&r))append(q,out,r);out->scanned++;
        }
    }else{
        double lo=q->initial_correction_sec-q->search_seconds,step=fmin(60,q->search_seconds/20),last=0,last_t=0;int valid=0;
        for(double t=lo;t<=q->initial_correction_sec+q->search_seconds+step/2;t+=step){
            NavLunarSolution r;char ignored[256];out->scanned++;
            if(navlunar_predict(q,t,q->distance.dr_lat,q->distance.dr_lon,&r,ignored,sizeof ignored)){valid=0;continue;}
            double f=r.residual_min[0];
            if(fabs(f)<1e-8)append(q,out,r);
            else if(valid&&last*f<0){
                double a=last_t,b=t,fa=last;int usable=1;
                for(int i=0;i<32;i++){
                    double m=(a+b)/2;
                    if(navlunar_predict(q,m,q->distance.dr_lat,q->distance.dr_lon,&r,ignored,sizeof ignored)){usable=0;break;}
                    double fm=r.residual_min[0];if(fabs(fm)<1e-8||b-a<.0002)break;
                    if(fa*fm<=0)b=m;else{a=m;fa=fm;}
                }if(usable&&fabs(r.residual_min[0])<.0001)append(q,out,r);
            }
            valid=1;last=f;last_t=t;
        }
    }
    if(!out->count)return fail(e,n,"No usable lunar solution in the search span. Check body/contact, dates, AP and real Hs; try a wider span. Both discs must stay above 5 degrees. A joint fix may need a better DR seed.");
    return 0;
}
static void stamp(double when,char *out,size_t n){
    double ms=round((when-2440587.5)*86400000.),day=floor(ms/86400000.);
    long part=(long)(ms-day*86400000.);char date[32],unused[32];
    navsky_utc(2440587.5+day,date,sizeof date,unused,sizeof unused);
    snprintf(out,n,"%s %02ld:%02ld:%06.3f",date,part/3600000,(part/60000)%60,(part%60000)/1000.);
}
void navlunar_report(FILE *f,const NavLunarRequest *q,const NavLunarResult *r){
    char original[80],utc[80];
    snprintf(original,sizeof original,"%04d-%02d-%02d %02d:%02d:%09.6f",q->distance.y,q->distance.mo,q->distance.d,q->distance.h,q->distance.mi,q->distance.s);
    fprintf(f,"NavAlm %s — LUNAR DISTANCE / UTC CORRECTION\n%s\n\n",NAVALM_VERSION,nav_engine_detail());
    fprintf(f,"Original watch epoch: %s (UTC scale, unmodified)\nOther body: %s %s\nRaw distance: %.8f deg; index error %.3f' %s arc\n",
        original,q->distance.body,q->distance.star,q->distance.hs_deg,q->distance.ie_min,q->distance.ie_on_arc?"ON":"OFF");
    fprintf(f,"Distance contacts: Moon %s; other %s\n",q->moon_contact>0?"NEAR":q->moon_contact<0?"FAR":"CENTRE",q->other_contact>0?"NEAR":q->other_contact<0?"FAR":"CENTRE");
    fprintf(f,"AP: %.7f latitude, %.7f east-positive longitude\nMode: %s\nInitial UTC-minus-watch correction: %+.3f s; search +/- %.1f s\n",
        q->distance.dr_lat,q->distance.dr_lon,q->refine_position?"two REAL altitudes; refine position and time":"fixed AP; solve lunar time",q->initial_correction_sec,q->search_seconds);
    if(!q->refine_position&&(q->moon.measured||q->other.measured))fprintf(f,"Real Hs residuals are independent checks in fixed-AP mode, not fitted position constraints.\n");
    fprintf(f,"Atmosphere: %.1f hPa, %.1f C; ellipsoidal observer elevation %.1f m\n",q->distance.press_mb,q->distance.temp_c,q->observer_height_m);
    for(int i=0;i<2;i++){
        const NavLunarAltitude *a=i?&q->other:&q->moon;
        if(a->measured)snprintf(original,sizeof original,"%04d-%02d-%02d %02d:%02d:%09.6f",a->sight.y,a->sight.mo,a->sight.d,a->sight.h,a->sight.mi,a->sight.s);
        fprintf(f,"%s altitude: %s",i?"Other":"Moon",a->measured?"MEASURED":"SYNTHETIC (not a position observation)");
        if(a->measured)fprintf(f,"; watch %s; raw Hs %.8f deg",original,a->sight.hs_deg);
        fprintf(f,"; limb %d; %s; eye %.2f %s; IE %.3f' %s; P %.1f hPa T %.1f C\n",a->sight.limb,a->sight.artificial?"artificial horizon":a->sight.short_horizon_nm>0?"short waterline":"sea horizon",
            a->sight.he,a->sight.he_metres?"m":"ft",a->sight.ie_min,a->sight.ie_on_arc?"on":"off",a->sight.press_mb,a->sight.temp_c);
    }
    fprintf(f,"Motion: %s; course %.2f deg / %.2f kn; set %.2f deg / %.2f kn\n",q->distance.has_motion?"applied between shot epochs":"stationary",q->distance.course,q->distance.speed,q->distance.set,q->distance.drift);
    for(int i=0;i<r->count;i++){
        const NavLunarSolution *s=&r->solutions[i];stamp(s->utc_jd,utc,sizeof utc);
        fprintf(f,"\nSOLUTION %d%s\nUTC - WATCH = %+.2f seconds (additional %+.2f s)\nDerived UTC: %s\nPosition: %.7f, %.7f E-positive\n",i+1,i==0&&q->refine_position?" (nearest AP)":"",s->correction_sec,s->additional_sec,utc,s->lat,s->lon);
        fprintf(f,"Geocentric centres %.8f deg\nTopocentric centres %.8f deg\nRefracted centres %.8f deg\nAugmented lunar SD %.5f'\nPredicted raw distance %.8f deg; residual %+.5f'\n",
            s->geocentric_deg,s->topocentric_deg,s->apparent_deg,s->moon_sd_min,s->raw_distance_deg,s->residual_min[0]);
        for(int k=0;k<2;k++){stamp(s->altitude_jd[k],utc,sizeof utc);
            fprintf(f,"%s shot: UTC %s; predicted raw Hs %.8f deg; residual %+.5f'%s\n",k?"Other":"Moon",utc,s->predicted_hs[k],s->residual_min[k+1],(k?q->other.measured:q->moon.measured)?"":" (synthetic; not fitted)");}
        fprintf(f,"At lunar epoch: apparent centre altitudes %.6f / %.6f deg; azimuths %.6f / %.6f deg\n",s->centre_alt_deg[0],s->centre_alt_deg[1],s->azimuth_deg[0],s->azimuth_deg[1]);
        fprintf(f,"Distance rate %+.5f'/minute of time\nFormal time uncertainty %.1f s from angle sigma %.2f' / altitude %.2f'%s\n",s->rate_min_per_minute,s->sigma_time_sec,q->distance_sigma_min,q->altitude_sigma_min,s->weak?" — WEAK GEOMETRY":"");
    }
    fprintf(f,"\nModel: NOVAS topocentric astrometry; WGS84 observer; Bennett refraction; smooth\nmean limbs; Jupiter/planet edges use equatorial radii, not an oblate-limb model.\nUTC=UT1 assumed. Discs supported from 5 to 89.8 deg altitude. No limb terrain.\nFormal uncertainty excludes AP, atmosphere and instrument/limb systematics.\nJoint search is bounded and does not guarantee discovery of every global root.\nClock correction is UTC minus watch, NOT astronomical delta T (TT minus UT1).\nA synthetic Hs supplies geometry only. Original watch readings remain unchanged.\n");
}
typedef struct {const char *key;size_t offset;int integer;} Field;
#define SF(k,i) {#k,offsetof(Sight,k),i}
static const Field sight_fields[]={SF(hs_deg,0),SF(ie_min,0),SF(ie_on_arc,1),SF(artificial,1),SF(he,0),SF(he_metres,1),
    SF(limb,1),SF(temp_c,0),SF(press_mb,0),SF(dr_lat,0),SF(dr_lon,0),SF(short_horizon_nm,0),SF(has_motion,1),
    SF(course,0),SF(speed,0),SF(set,0),SF(drift,0)};
#define QF(k,i) {#k,offsetof(NavLunarRequest,k),i}
static const Field request_fields[]={QF(moon_contact,1),QF(other_contact,1),QF(refine_position,1),
    QF(initial_correction_sec,0),QF(search_seconds,0),QF(observer_height_m,0),QF(distance_sigma_min,0),QF(altitude_sigma_min,0)};
static int setfield(void *obj,const Field *fields,size_t count,const char *key,const char *value){
    for(size_t i=0;i<count;i++)if(!strcmp(key,fields[i].key)){
        char *end;double x=strtod(value,&end);if(end==value||*end||!isfinite(x))return -1;
        if(fields[i].integer){if(x!=floor(x)||fabs(x)>1000000)return -1;int v=(int)x;memcpy((char*)obj+fields[i].offset,&v,sizeof v);}
        else memcpy((char*)obj+fields[i].offset,&x,sizeof x);return 1;
    }
    return 0;
}
static void writefields(FILE *f,const char *prefix,const void *obj,const Field *fields,size_t count){
    for(size_t i=0;i<count;i++){
        if(fields[i].integer){int v;memcpy(&v,(const char*)obj+fields[i].offset,sizeof v);fprintf(f,"%s%s=%d\n",prefix,fields[i].key,v);}
        else{double v;memcpy(&v,(const char*)obj+fields[i].offset,sizeof v);fprintf(f,"%s%s=%.15g\n",prefix,fields[i].key,v);}
    }
}
int navlunar_write(FILE *f,const NavLunarRequest *q){
    fprintf(f,"# NavAlm lunar observation: raw watch timestamps, degrees, east-positive longitude.\n# measured=0 means synthetic geometry; its stored Hs/watch fields are ignored.\nnavalm_lunar=1\n");
    writefields(f,"",q,request_fields,sizeof request_fields/sizeof *request_fields);
    const char *names[]={"distance.","moon.","other."};const Sight *s[]={&q->distance,&q->moon.sight,&q->other.sight};
    for(int i=0;i<3;i++){
        fprintf(f,"%sbody=%s\n%sstar=%s\n%swatch=%04d-%02d-%02dT%02d:%02d:%09.6f\n",names[i],s[i]->body,names[i],s[i]->star,names[i],s[i]->y,s[i]->mo,s[i]->d,s[i]->h,s[i]->mi,s[i]->s);
        writefields(f,names[i],s[i],sight_fields,sizeof sight_fields/sizeof *sight_fields);
        if(i)fprintf(f,"%smeasured=%d\n",names[i],i==1?q->moon.measured:q->other.measured);
    }return ferror(f)?1:0;
}
static int watch_parse(const char *v,Sight *s){
    char end;double when;
    if(sscanf(v,"%d-%d-%dT%d:%d:%lf%c",&s->y,&s->mo,&s->d,&s->h,&s->mi,&s->s,&end)!=6)return 1;
    return navsky_jd(s->y,s->mo,s->d,s->h,s->mi,s->s,&when);
}
static char *trim(char *s){while(isspace((unsigned char)*s))s++;char *end=s+strlen(s);while(end>s&&isspace((unsigned char)end[-1]))*--end=0;return s;}
int navlunar_load(const char *path,NavLunarRequest *out,char *e,size_t n){
    FILE *f=fopen(path,"r");if(!f)return fail(e,n,"Cannot open the lunar observation file.");
    NavLunarRequest q;navlunar_init(&q);char line[512],seen[128][80];int count=0,version=0,required=0,bad=0;
    while(fgets(line,sizeof line,f)){
        if(!strchr(line,'\n')&&!feof(f)){bad=1;break;}
        char *key=trim(line);if(!*key||*key=='#')continue;
        char *eq=strchr(key,'=');if(!eq){bad=1;break;}*eq++=0;key=trim(key);char *v=trim(eq);
        if(strlen(key)>=80||count>=128){bad=1;break;}
        for(int i=0;i<count;i++)if(!strcmp(seen[i],key))bad=1;
        if(bad)break;snprintf(seen[count++],80,"%s",key);
        if(!strcmp(key,"navalm_lunar")){version=!strcmp(v,"1");if(!version)bad=1;continue;}
        int matched=setfield(&q,request_fields,sizeof request_fields/sizeof *request_fields,key,v);
        if(matched){if(matched<0)bad=1;if(bad)break;continue;}
        Sight *s=NULL;int which=-1;
        if(!strncmp(key,"distance.",9)){s=&q.distance;key+=9;which=0;}
        else if(!strncmp(key,"moon.",5)){s=&q.moon.sight;key+=5;which=1;}
        else if(!strncmp(key,"other.",6)){s=&q.other.sight;key+=6;which=2;}
        if(!s){bad=1;break;}
        if(!strcmp(key,"body")||!strcmp(key,"star")){
            if(strlen(v)>=NAVSIGHT_NAME){bad=1;break;}snprintf(!strcmp(key,"body")?s->body:s->star,NAVSIGHT_NAME,"%s",v);
            if(which==0&&!strcmp(key,"body"))required|=1;
        }else if(!strcmp(key,"watch")){if(watch_parse(v,s)){bad=1;break;}required|=2<<(which*2);}
        else if(which>0&&!strcmp(key,"measured")){
            if(strcmp(v,"0")&&strcmp(v,"1")){bad=1;break;}
            if(which==1)q.moon.measured=atoi(v);else q.other.measured=atoi(v);
        }else{
            matched=setfield(s,sight_fields,sizeof sight_fields/sizeof *sight_fields,key,v);
            if(matched!=1){bad=1;break;}
            if(!strcmp(key,"hs_deg"))required|=4<<(which*2);
            if(which==0&&!strcmp(key,"dr_lat"))required|=128;
            if(which==0&&!strcmp(key,"dr_lon"))required|=256;
        }
    }
    if(ferror(f))bad=1;fclose(f);
    int mask=1|2|4|128|256;if(q.moon.measured)mask|=8|16;if(q.other.measured)mask|=32|64;
    if(bad||!version||(required&mask)!=mask)return fail(e,n,"Invalid/incomplete lunar file: use version 1, unique known keys, explicit body/distance/AP/watch epoch and Hs/watch epochs for real shots.");
    if(navlunar_validate(&q,e,n))return 1;*out=q;return 0;
}
int navlunar_command(int argc,char **argv){
    if(argc==3&&!strcmp(argv[2],"--example")){
        NavLunarRequest q;navlunar_init(&q);q.distance.mo=11;q.distance.d=3;q.distance.h=13;q.distance.mi=20;
        q.moon_contact=-1;/* Fictional far-limb example, not the new-observation default. */
        q.distance.hs_deg=9.02;q.distance.dr_lat=34;q.distance.dr_lon=-117;q.moon.sight=q.other.sight=q.distance;
        strcpy(q.moon.sight.body,"moon");q.moon.sight.limb=1;
        return navlunar_write(stdout,&q);
    }
    if(argc!=3){fprintf(stderr,"usage: %s lunar OBSERVATION.lunar\n       %s lunar --example > example.lunar\n",argv[0],argv[0]);return 2;}
    NavLunarRequest q;NavLunarResult r;char e[512];
    if(navlunar_load(argv[2],&q,e,sizeof e)||navlunar_solve(&q,&r,e,sizeof e)){fprintf(stderr,"Lunar: %s\n",e);return 2;}
    navlunar_report(stdout,&q,&r);return 0;
}
