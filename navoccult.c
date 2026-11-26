/* SPDX-License-Identifier: MIT
 * Lunar stellar occultations: NOVAS full-accuracy topocentric places / DE440s.
 * Reference sphere, not a lunar topographic limb. See OCCULTATIONS.md.
 */
#include "navoccult.h"
#include "navsky.h"
#include "nav_version.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef NAVALM_NO_PRECISE
#include "novas.h"
#endif
#define RAD 0.01745329251994329577
#define DAY 86400.0
#define AU 149597870.7
typedef struct { NavStarCat star; const char *aliases; int double_star; } OccStar;
#include "navoccult_data.inc"
typedef struct {double v[3],ra,dec,sd,alt;} Place;
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 1;}
static void vector(double ra,double dec,double v[3]){v[0]=cos(dec*RAD)*cos(ra*RAD);v[1]=cos(dec*RAD)*sin(ra*RAD);v[2]=sin(dec*RAD);}
static double dot(const double a[3],const double b[3]){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static double separation(const double a[3],const double b[3]){
    double x=a[1]*b[2]-a[2]*b[1],y=a[2]*b[0]-a[0]*b[2],z=a[0]*b[1]-a[1]*b[0];
    return atan2(sqrt(x*x+y*y+z*z),dot(a,b))/RAD;
}
size_t navoccult_star_count(void){return sizeof catalog/sizeof catalog[0];}
const NavStarCat *navoccult_star(size_t i){return i<navoccult_star_count()?&catalog[i].star:NULL;}
const char *navoccult_aliases(size_t i){return i<navoccult_star_count()?catalog[i].aliases:"";}
static int equal(const char *a,size_t len,const char *b){
    if(strlen(b)!=len)return 0;
    for(size_t i=0;i<len;i++)if(tolower((unsigned char)a[i])!=tolower((unsigned char)b[i]))return 0;
    return 1;
}
int navoccult_star_find(const char *name){
    if(!name||!*name)return -1;
    for(size_t i=0;i<navoccult_star_count();i++){
        const char *s=catalog[i].aliases;while(*s){const char *end=strchr(s,'|');size_t len=end?(size_t)(end-s):strlen(s);
            if(equal(s,len,name))return (int)i;if(!end)break;s=end+1;}
    }return -1;
}
void navoccult_init(NavOccultRequest *q){
    memset(q,0,sizeof *q);q->start_jd=nav_julian_date(2026,1,1,0,0,0);q->days=7;
    q->max_magnitude=6.5;q->min_alt_deg=5;q->night_only=1;q->max_sun_alt_deg=-6;q->margin_arcsec=30;
}
int navoccult_validate(const NavOccultRequest *q,char *e,size_t n){
    if(!q)return fail(e,n,"Missing occultation request.");
    const double a[]={q->start_jd,q->days,q->lat,q->lon,q->height_m,q->dut1_sec,q->max_magnitude,q->min_alt_deg,q->max_sun_alt_deg,q->margin_arcsec};
    for(size_t i=0;i<sizeof a/sizeof a[0];i++)if(!isfinite(a[i]))return fail(e,n,"Occultation inputs must be finite numbers.");
    if(q->days<=0||q->days>31||fabs(q->lat)>89||fabs(q->lon)>180||q->height_m < -500||q->height_m>10000||fabs(q->dut1_sec)>.9)
        return fail(e,n,"Use 0 < days <= 31, latitude within 89 degrees, longitude within 180, elevation -500 to 10000 m and DUT1 within 0.9 s.");
    if(q->max_magnitude < -2||q->max_magnitude>6.5||q->min_alt_deg<0||q->min_alt_deg>90||q->max_sun_alt_deg < -90||q->max_sun_alt_deg>90||q->margin_arcsec<0||q->margin_arcsec>120||(q->night_only!=0&&q->night_only!=1))
        return fail(e,n,"Use magnitude -2 to 6.5, altitude 0 to 90 degrees, Sun altitude -90 to 90, and near-miss margin 0 to 120 arcseconds.");
    if(nav_engine_current()!=NAV_ENGINE_PRECISE)return fail(e,n,"Occultations require the Precise NOVAS + JPL engine.");
    if(nav_engine_init(e,n))return 1;
    if(!nav_engine_covers(q->start_jd-.25)||!nav_engine_covers(q->start_jd+q->days+.25))
        return fail(e,n,"The JPL ephemeris must cover the whole search plus six hours at either end.");
    return 0;
}
static int occ_place(const NavOccultRequest *q,int star,int sun,double utc,Place *p,char *e,size_t n){
#ifdef NAVALM_NO_PRECISE
    (void)q;(void)star;(void)sun;(void)utc;(void)p;
    return fail(e,n,"Occultations need NOVAS and DE440s.");
#else
    double dt=nav_delta_t_with_dut1_seconds(utc,q->dut1_sec),tt=utc+(dt+q->dut1_sec)/DAY;
    double dis,zd,az,rar,decr;on_surface obs;
    if(!nav_engine_covers(utc))return fail(e,n,"Trial contact is outside the JPL ephemeris.");
    make_on_surface(q->lat,q->lon,q->height_m,10,0,&obs);memset(p,0,sizeof *p);
    if(star>=0){
        const NavStarCat *s=navoccult_star((size_t)star);if(!s)return fail(e,n,"Unknown occultation star.");
        cat_entry cat;char name[SIZE_OF_OBJ_NAME],tag[SIZE_OF_CAT_NAME];
        snprintf(name,sizeof name,"%s",s->name);snprintf(tag,sizeof tag,"SKY");
        if(make_cat_entry(name,tag,star+1,s->ra2000_deg/15,s->dec2000_deg,s->pm_ra_mas,s->pm_dec_mas,0,0,&cat)||topo_star(tt,dt,&cat,&obs,0,&p->ra,&p->dec))
            return fail(e,n,"NOVAS could not calculate the star's topocentric place.");
    }else{
        object o;char name[SIZE_OF_OBJ_NAME];snprintf(name,sizeof name,"%s",sun?"Sun":"Moon");
        if(make_object(0,sun?10:11,name,NULL,&o)||topo_planet(tt,&o,dt,&obs,0,&p->ra,&p->dec,&dis))
            return fail(e,n,"NOVAS could not calculate a topocentric Moon/Sun position.");
        if(!sun)p->sd=asin(NAVOCCULT_RADIUS_KM/(dis*AU))/RAD;
    }
    equ2hor(utc+q->dut1_sec/DAY,dt,0,0,0,&obs,p->ra,p->dec,0,&zd,&az,&rar,&decr);
    p->alt=90-zd;p->ra*=15;vector(p->ra,p->dec,p->v);
    if(!isfinite(p->alt)||!isfinite(p->v[0])||(!sun&&star<0&&p->sd<=0))return fail(e,n,"Invalid NOVAS occultation geometry.");
    return 0;
#endif
}
static int geometry(const NavOccultRequest *q,int star,double t,double offset,Place *m,Place *s,double *f,char *e,size_t n){
    if(occ_place(q,-1,0,t,m,e,n)||occ_place(q,star,0,t,s,e,n))return 1;
    *f=separation(m->v,s->v)-m->sd-offset/3600;return 0;
}
static int value(const NavOccultRequest *q,int star,double t,double offset,double *f,char *e,size_t n){Place m,s;return geometry(q,star,t,offset,&m,&s,f,e,n);}
static int root(const NavOccultRequest *q,int star,double a,double b,double offset,double *t,char *e,size_t n){
    double fa,fb;if(value(q,star,a,offset,&fa,e,n)||value(q,star,b,offset,&fb,e,n))return 1;
    if(fa*fb>0)return fail(e,n,"Could not bracket the selected contact; check position and limb offset.");
    for(int i=0;i<38 && (b-a)*DAY>.0003;i++){
        double mid=(a+b)/2,f;if(value(q,star,mid,offset,&f,e,n))return 1;
        if((fa>0)==(f>0)){a=mid;fa=f;}else b=mid;
    }*t=(a+b)/2;return 0;
}
static int contact_details(const NavOccultRequest *q,NavOccultEvent *r,int i,double offset,char *e,size_t n){
    double t=r->contact_jd[i],f,fa,fb;Place m,s,sun;
    if(geometry(q,r->star,t,offset,&m,&s,&f,e,n)||occ_place(q,-1,1,t,&sun,e,n)||
       value(q,r->star,t-1/DAY,offset,&fa,e,n)||value(q,r->star,t+1/DAY,offset,&fb,e,n))return 1;
    r->altitude_deg[i]=s.alt;r->sun_alt_deg[i]=sun.alt;r->rate_arcsec_sec[i]=(fb-fa)*1800;
    double da=(s.ra-m.ra)*RAD;
    r->pa_deg[i]=fmod(atan2(sin(da),cos(m.dec*RAD)*tan(s.dec*RAD)-sin(m.dec*RAD)*cos(da))/RAD+360,360);
    /* Direction to star and Sun on Moon's tangent plane; positive dot is bright limb. */
    double north[3]={-sin(m.dec*RAD)*cos(m.ra*RAD),-sin(m.dec*RAD)*sin(m.ra*RAD),cos(m.dec*RAD)};
    double east[3]={-sin(m.ra*RAD),cos(m.ra*RAD),0};
    r->bright_limb[i]=dot(s.v,north)*dot(sun.v,north)+dot(s.v,east)*dot(sun.v,east)>0;
    r->visible[i]=s.alt>=q->min_alt_deg&&(!q->night_only||sun.alt<=q->max_sun_alt_deg);
    return 0;
}
int navoccult_event(const NavOccultRequest *q,int star,double near,double offset,NavOccultEvent *r,char *e,size_t n){
    if(!r)return fail(e,n,"Missing event output.");memset(r,0,sizeof *r);
    if(navoccult_validate(q,e,n))return 1;
    if(!navoccult_star((size_t)star)||!isfinite(near)||!isfinite(offset)||fabs(offset)>120)return fail(e,n,"Choose a catalogue star, finite epoch and limb offset within 120 arcseconds.");
    /* Find a coarse minimum, then refine a 10-minute bracket. Do not assume
     * the caller supplied the exact minimum; --near may be a rounded UTC. */
    double best=1e9,mid=near;
    for(int i=-18;i<=18;i++){double t=near+i*600/DAY,f;if(value(q,star,t,offset,&f,e,n))return 1;if(f<best){best=f;mid=t;}}
    if(fabs(mid-near)*DAY>=10799)return fail(e,n,"No local conjunction within three hours of the supplied event epoch.");
    double lo=mid-600/DAY,hi=mid+600/DAY;
    for(int i=0;i<38&&(hi-lo)*DAY>.002;i++){
        double a=lo+(hi-lo)/3,b=hi-(hi-lo)/3,fa,fb;
        if(value(q,star,a,offset,&fa,e,n)||value(q,star,b,offset,&fb,e,n))return 1;
        if(fa<fb)hi=b;else lo=a;
    }
    double t=(lo+hi)/2,f;Place m,s,sun;
    if(geometry(q,star,t,offset,&m,&s,&f,e,n)||occ_place(q,-1,1,t,&sun,e,n))return 1;
    r->star=star;r->double_star=catalog[star].double_star;r->closest_jd=t;r->separation_deg=separation(m.v,s.v);
    r->sd_deg=m.sd+offset/3600;r->impact=r->separation_deg/r->sd_deg;r->clearance_arcsec=f*3600;
    r->grazing=r->impact>=.9;r->closest_alt_deg=s.alt;r->closest_sun_alt_deg=sun.alt;
    if(f>=0)return 0; /* near miss / exact tangent: no invented pair of contacts */
    double outer[2];
    for(int i=0;i<2;i++){
        int found=0;for(int k=1;k<=48;k++){double a=t+(i?1:-1)*k*300/DAY,fa;
            if(value(q,star,a,offset,&fa,e,n))return 1;
            if(fa>0){outer[i]=a;found=1;break;}}
        if(!found)return fail(e,n,"Contact lies more than four hours from closest approach.");
        if(root(q,star,i?t:outer[i],i?outer[i]:t,offset,&r->contact_jd[i],e,n))return 1;
        if(contact_details(q,r,i,offset,e,n))return 1;
    }r->has_contacts=1;return 0;
}
int navoccult_search(const NavOccultRequest *q,NavOccultResult *out,NavOccultProgress progress,void *user,char *e,size_t n){
    if(!out)return fail(e,n,"Missing prediction output.");out->count=0;if(navoccult_validate(q,e,n))return 1;
    size_t count=navoccult_star_count();double (*v)[3]=calloc(count,sizeof *v);double *previous=malloc(count*sizeof *previous),*before=malloc(count*sizeof *before),*last=calloc(count,sizeof *last);
    if(!v||!previous||!before||!last){free(v);free(previous);free(before);free(last);return fail(e,n,"Not enough memory for occultation search.");}
    int error=0;double end=q->start_jd+q->days,step=600/DAY;
    /* Screening only: apparent geocentric midpoint catalogue directions. The
     * 0.8-degree cone includes the Moon, near-miss allowance, ten-minute sample
     * spacing and 31-day stellar motion. Every candidate uses exact topo places. */
    for(size_t i=0;i<count;i++){
        previous[i]=before[i]=-2;
        if(catalog[i].star.magnitude>q->max_magnitude)continue;
        NavStarAlmanac a;if(nav_catalog_star_almanac(&catalog[i].star,(q->start_jd+end)/2,&a)){error=fail(e,n,nav_engine_last_error());goto done;}
        vector(a.ra_deg,a.dec_deg,v[i]);
        if(i%128==0&&progress&&progress(.1*i/count,user)){error=fail(e,n,"Occultation search cancelled.");goto done;}
    }
    int steps=(int)ceil((q->days+.5)/step);double begin=q->start_jd-.25;
    for(int k=0;k<=steps;k++){
        double t=begin+k*step;Place m;if(occ_place(q,-1,0,t,&m,e,n)){error=1;goto done;}
        for(size_t i=0;i<count;i++){
            if(catalog[i].star.magnitude>q->max_magnitude)continue;
            double d=dot(m.v,v[i]);
            if(k>=2&&previous[i]>=before[i]&&previous[i]>d&&previous[i]>cos(.8*RAD)&&t-last[i]>.2){
                NavOccultEvent r;if(navoccult_event(q,(int)i,t-step,0,&r,e,n)){error=1;goto done;}last[i]=t;
                if(r.clearance_arcsec<=q->margin_arcsec){
                    int include=0;
                    if(r.has_contacts){for(int j=0;j<2;j++){r.visible[j]=r.visible[j]&&r.contact_jd[j]>=q->start_jd&&r.contact_jd[j]<end;include|=r.visible[j];}}
                    else include=r.closest_jd>=q->start_jd&&r.closest_jd<end&&r.closest_alt_deg>=q->min_alt_deg&&(!q->night_only||r.closest_sun_alt_deg<=q->max_sun_alt_deg);
                    if(include){if(out->count>=NAVOCCULT_MAX_EVENTS){error=fail(e,n,"Too many occultations; shorten the search interval.");goto done;}out->events[out->count++]=r;}
                }
            }before[i]=previous[i];previous[i]=d;
        }
        if(k%12==0&&progress&&progress(.1+.9*k/steps,user)){error=fail(e,n,"Occultation search cancelled.");goto done;}
    }
    navoccult_sort(out,0);
done:free(v);free(previous);free(before);free(last);if(error)out->count=0;return error;
}
static int time_compare(const void *a,const void *b){const NavOccultEvent *x=a,*y=b;double tx=x->has_contacts?x->contact_jd[x->visible[0]?0:1]:x->closest_jd,ty=y->has_contacts?y->contact_jd[y->visible[0]?0:1]:y->closest_jd;return (tx>ty)-(tx<ty);}
static int mag_compare(const void *a,const void *b){const NavOccultEvent *x=a,*y=b;double d=catalog[x->star].star.magnitude-catalog[y->star].star.magnitude;return d==0?time_compare(a,b):(d>0?1:-1);}
static int central_compare(const void *a,const void *b){const NavOccultEvent *x=a,*y=b;double d=x->impact-y->impact;return d==0?time_compare(a,b):(d>0?1:-1);}
static int graze_compare(const void *a,const void *b){const NavOccultEvent *x=a,*y=b;double d=fabs(x->impact-1)-fabs(y->impact-1);return d==0?time_compare(a,b):(d>0?1:-1);}
void navoccult_sort(NavOccultResult *r,int mode){if(r)qsort(r->events,r->count,sizeof r->events[0],mode==1?mag_compare:mode==2?central_compare:mode==3?graze_compare:time_compare);}
int navoccult_recover(const NavOccultRequest *q,const NavOccultEvent *event,const NavOccultObservation *o,NavOccultRecovery *r,char *e,size_t n){
    if(!q||!event||!o||!r)return fail(e,n,"Missing occultation recovery input.");memset(r,0,sizeof *r);
    const double a[]={o->watch_jd,o->reaction_sec,o->limb_offset_arcsec,o->timing_sigma_sec,o->limb_sigma_arcsec,o->position_sigma_nm};
    for(size_t i=0;i<sizeof a/sizeof a[0];i++)if(!isfinite(a[i]))return fail(e,n,"Recovery inputs must be finite numbers.");
    if(o->contact<0||o->contact>1||fabs(o->reaction_sec)>10||fabs(o->limb_offset_arcsec)>120||o->timing_sigma_sec<=0||o->timing_sigma_sec>60||o->limb_sigma_arcsec<.01||o->limb_sigma_arcsec>120||o->position_sigma_nm<0||o->position_sigma_nm>60)
        return fail(e,n,"Use D or R, reaction within 10 s, positive timing sigma <=60 s, limb/catalog sigma .01 to 120 arcseconds and position sigma 0 to 60 NM per axis.");
    NavOccultEvent fresh;if(navoccult_event(q,event->star,event->closest_jd,o->limb_offset_arcsec,&fresh,e,n))return 1;
    if(!fresh.has_contacts)return fail(e,n,"No contact at this AP: a near miss or tangent cannot recover UTC with the smooth-Moon model.");
    int c=o->contact;if(!fresh.visible[c])return fail(e,n,"Selected contact is below the altitude limit or excluded by the night filter.");
    double t=fresh.contact_jd[c],rate=fabs(fresh.rate_arcsec_sec[c]);
    if(rate<.001)return fail(e,n,"Contact is too nearly tangent to recover a stable clock correction.");
    double correction=(t-o->watch_jd)*DAY+o->reaction_sec;
    if(fabs(correction)>604800)return fail(e,n,"UTC minus watch exceeds seven days. Check observation date and the selected occultation.");
    double gradient[2];
    for(int axis=0;axis<2;axis++){
        NavOccultRequest qa=*q,qb=*q;double scale=axis?60*cos(q->lat*RAD):60;
        if(axis){qa.lon+=.01/scale;qb.lon-=.01/scale;}else{qa.lat+=.01/scale;qb.lat-=.01/scale;}
        if(qa.lon>180)qa.lon-=360;if(qb.lon < -180)qb.lon+=360;
        double fa,fb;if(value(&qa,event->star,t,o->limb_offset_arcsec,&fa,e,n)||value(&qb,event->star,t,o->limb_offset_arcsec,&fb,e,n))return 1;
        gradient[axis]=(fa-fb)*3600/.02;
    }
    r->utc_jd=t;r->correction_sec=correction;r->rate_arcsec_sec=fresh.rate_arcsec_sec[c];
    r->position_sigma_sec=o->position_sigma_nm*hypot(gradient[0],gradient[1])/rate;
    r->limb_sigma_sec=o->limb_sigma_arcsec/rate;
    r->sigma_time_sec=hypot(o->timing_sigma_sec,hypot(r->limb_sigma_sec,r->position_sigma_sec));
    r->longitude_sigma_nm=r->sigma_time_sec*.25*cos(q->lat*RAD);
    r->weak=fresh.grazing||rate<.05||fresh.altitude_deg[c]<5||fresh.double_star;
    return 0;
}
void navoccult_stamp(double jd,char *out,size_t n){
    /* Round before splitting date/time so midnight carries into the new date. */
    double midnight=floor(jd-.5)+.5,sec=round((jd-midnight)*DAY*1000)/1000;
    if(sec>=DAY){midnight+=1;sec=0;}char d[32],t[32];navsky_utc(midnight,d,sizeof d,t,sizeof t);
    snprintf(out,n,"%sT%02d:%02d:%06.3fZ",d,(int)(sec/3600),(int)(sec/60)%60,fmod(sec,60));
}
void navoccult_report(FILE *f,const NavOccultRequest *q,const NavOccultResult *r){
    char start[48];navoccult_stamp(q->start_jd,start,sizeof start);
    fprintf(f,"NavAlm %s — Lunar stellar occultations\nAP %.8f, %+.8f east; elevation %.1f m; DUT1 %+.3f s\nStart %s; %.3f days; magnitude <= %.2f; altitude >= %.1f deg; %s\n%zu events, %zu catalogue stars (XEphem SKY2000, magnitude 6.5).\n",NAVALM_VERSION,q->lat,q->lon,q->height_m,q->dut1_sec,start,q->days,q->max_magnitude,q->min_alt_deg,q->night_only?"night filter on":"daylight included",r->count,navoccult_star_count());
    fprintf(f,"Smooth Moon radius %.1f km; no terrain corrections. Near-limb means b >= 0.90, NOT a terrain graze prediction.\nD = disappearance; R = reappearance. b = closest separation / lunar radius; negative clearance passes inside.\nOnly visible contacts within the search interval are marked *. All times UTC.\n\n",NAVOCCULT_RADIUS_KM);
    for(size_t i=0;i<r->count;i++){
        const NavOccultEvent *s=&r->events[i];fprintf(f,"%zu. %s  mag %.2f  %s  b=%.4f  clearance=%+.2f arcsec%s\n",i+1,catalog[s->star].star.name,catalog[s->star].star.magnitude,s->has_contacts?(s->grazing?"NEAR-LIMB":"DIRECT"):"NEAR MISS",s->impact,s->clearance_arcsec,s->double_star?"  [catalogued double]":"");
        fprintf(f,"   Aliases: %s\n",catalog[s->star].aliases);
        if(s->has_contacts)for(int c=0;c<2;c++){char t[48];navoccult_stamp(s->contact_jd[c],t,sizeof t);fprintf(f,"   %c %s%s  alt %.2f deg  Sun %+.2f deg  PA %.1f deg  %s limb  rate %+.4f arcsec/s\n",c?'R':'D',t,s->visible[c]?" *":"",s->altitude_deg[c],s->sun_alt_deg[c],s->pa_deg[c],s->bright_limb[c]?"bright":"dark",s->rate_arcsec_sec[c]);}
        else{char t[48];navoccult_stamp(s->closest_jd,t,sizeof t);fprintf(f,"   Closest %s  alt %.2f deg  Sun %+.2f deg; no smooth-limb contact to time.\n",t,s->closest_alt_deg,s->closest_sun_alt_deg);}
    }
    fprintf(f,"\nPosition, catalog astrometry, double stars, lunar relief, DUT1 and observer reaction can dominate clock accuracy. See OCCULTATIONS.md.\n");
}
void navoccult_recovery_report(FILE *f,const NavOccultRequest *q,const NavOccultEvent *s,const NavOccultObservation *o,const NavOccultRecovery *r){
    char utc[48],watch[48];navoccult_stamp(r->utc_jd,utc,sizeof utc);navoccult_stamp(o->watch_jd,watch,sizeof watch);
    fprintf(f,"NavAlm %s — Occultation time recovery\n%s — %s; AP %.8f, %+.8f east; elevation %.1f m\nRecorded watch: %s (watch reading on UTC calendar/scale)\nPredicted contact UTC: %s\nUTC minus watch = %+.3f seconds\n",NAVALM_VERSION,catalog[s->star].star.name,o->contact?"reappearance":"disappearance",q->lat,q->lon,q->height_m,watch,utc,r->correction_sec);
    fprintf(f,"Reaction delay %+.3f s (positive = late recording); effective reference for raw watch includes this delay.\nLimb offset %+.3f arcsec; smooth Moon radius %.1f km; DUT1 %+.3f s.\nContact normal rate %+.4f arcsec/s\n",o->reaction_sec,o->limb_offset_arcsec,NAVOCCULT_RADIUS_KM,q->dut1_sec,r->rate_arcsec_sec);
    fprintf(f,"User-assumed 1-sigma budget: timing %.3f s; limb + catalog %.3f arcsec -> %.3f s; AP %.3f NM per independent N/E axis -> %.3f s\nCombined local linear sigma: %.3f s, equivalent clock-only longitude sigma %.3f NM at AP latitude (%.3f NM at equator).\n",o->timing_sigma_sec,o->limb_sigma_arcsec,r->limb_sigma_sec,o->position_sigma_nm,r->position_sigma_sec,r->sigma_time_sec,r->longitude_sigma_nm,r->sigma_time_sec*.25);
    fprintf(f,"%s\nNo terrain profile or catalog covariance is loaded. The assumed sigma is not a guaranteed accuracy, especially for grazing or double-star events.\nAP stays fixed; one occultation cannot solve position and clock independently. Motion is not inferred; enter position AT contact.\n",r->weak?"CAUTION: near-limb, low-altitude, slow contact or double star; do not use as an automatic clock reference.":"Review AP, contact identification, lunar limb and reaction delay before preparing a Chrono comparison.");
}
#include "navoccult_cmd.inc"
