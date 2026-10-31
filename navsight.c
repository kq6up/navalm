/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* navalm 2.0 - sight correction, HO 229 reduction, LOP store, running fix.
 *
 * Formulas follow the explanation pages of the Nautical Almanac:
 *   dip        0.97' * sqrt(feet)   or  1.76' * sqrt(metres)
 *   refraction R0 = 0.0167 deg / tan(Ha + 7.32/(Ha + 4.32)), scaled by
 *              f  = 0.28 * P / (T + 273)     (P millibars, T Celsius)
 *   parallax   asin(sin HP * cos H)
 *   Moon SD    augmented by (1 + sin HP * sin H)
 *
 * The HO 229 block mimics the printed tables: tabulated Hc and d for the
 * whole degree of declination, the d correction for the declination
 * increment, and the residual (what the tables handle with the
 * double-second-difference column).
 */

#include "navfile.h"
#include "navsight.h"
#include "nav_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ARIES_RATE (15.0 + 2.464/60.0)   /* GHA Aries, degrees per hour */
#define MOON_RATE  (14.0 + 19.0/60.0)    /* Moon GHA base increment */

static double rad(double x){ return x*M_PI/180.0; }
static double deg(double x){ return x*180.0/M_PI; }
static double norm360(double x){ x=fmod(x,360.0); return x<0?x+360.0:x; }
static double clampd(double x,double lo,double hi){ return x<lo?lo:(x>hi?hi:x); }

/* ---------------------------------------------------------------- time */

double navsight_jd(const Sight *si){
    return nav_julian_date(si->y,si->mo,si->d,si->h,si->mi,si->s);
}

static void jd_to_cal(double jd,int *y,int *mo,int *d,int *h,int *mi,double *s){
    double z,f,a,b,c,dd,e,alpha,day;
    jd += 0.5;
    z = floor(jd); f = jd - z;
    if(z < 2299161.0) a = z;
    else { alpha = floor((z-1867216.25)/36524.25); a = z + 1 + alpha - floor(alpha/4); }
    b = a + 1524; c = floor((b-122.1)/365.25);
    dd = floor(365.25*c); e = floor((b-dd)/30.6001);
    day = b - dd - floor(30.6001*e) + f;
    *d  = (int)floor(day);
    *mo = (e < 14) ? (int)e - 1 : (int)e - 13;
    *y  = (*mo > 2) ? (int)c - 4716 : (int)c - 4715;
    {
        double frac = (day - *d) * 24.0;
        *h = (int)floor(frac);
        frac = (frac - *h) * 60.0;
        *mi = (int)floor(frac);
        *s = (frac - *mi) * 60.0;
        if(*s >= 59.9995){ *s = 0.0; (*mi)++; }
        if(*mi >= 60){ *mi -= 60; (*h)++; }
        if(*h >= 24){ *h -= 24; (*d)++; }   /* only used for display */
    }
}

/* -------------------------------------------------------------- format */

static const char *fmt_alt(double x,char *b,size_t n){
    double a=fabs(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m>=59.95){ m=0.0; d++; }
    snprintf(b,n,"%s%02d %04.1f'",x<0?"-":"",d,m); return b;
}
static const char *fmt_gha(double x,char *b,size_t n){
    double a=norm360(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m>=59.95){ m=0.0; d=(d+1)%360; }
    snprintf(b,n,"%03d %04.1f'",d,m); return b;
}
static const char *fmt_lat(double x,char *b,size_t n){
    double a=fabs(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m>=59.95){ m=0.0; d++; }
    snprintf(b,n,"%c %02d %04.1f'",x<0?'S':'N',d,m); return b;
}
static const char *fmt_lon(double x,char *b,size_t n){
    double a=fabs(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m>=59.95){ m=0.0; d++; }
    snprintf(b,n,"%c %03d %04.1f'",x<0?'W':'E',d,m); return b;
}
static const char *fmt_min(double x,char *b,size_t n){
    double r=(fabs(x)*10.0+0.5); r=floor(r)/10.0;
    if(r==0.0){ snprintf(b,n,"  0.0'"); return b; }
    snprintf(b,n,"%c%5.1f'",x<0?'-':'+',r); return b;
}

const char *navsight_fmt_lat(double d,char *b,size_t n){ return fmt_lat(d,b,n); }

/* hundredths of a minute: the engine really does carry this, so single-body
 * lookups show it.  Printed-style tables stay at tenths. */
const char *navsight_fmt_lat2(double x,char *b,size_t n){
    double a=fabs(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m>=59.995){ m=0.0; d++; }
    snprintf(b,n,"%c %02d %05.2f'",x<0?'S':'N',d,m); return b;
}
const char *navsight_fmt_ra(double x,char *b,size_t n){
    double a=norm360(x); int d,m; double s;
    d=(int)floor(a);
    m=(int)floor((a-d)*60.0);
    s=((a-d)*60.0-m)*60.0;
    if(s>=59.95){ s=0.0; m++; }
    if(m>=60){ m=0; d=(d+1)%360; }
    snprintf(b,n,"%03d %02d %04.1f\"",d,m,s); return b;
}

const char *navsight_fmt_gha2(double x,char *b,size_t n){
    double a=norm360(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m>=59.995){ m=0.0; d=(d+1)%360; }
    snprintf(b,n,"%03d %05.2f'",d,m); return b;
}
const char *navsight_fmt_lon(double d,char *b,size_t n){ return fmt_lon(d,b,n); }
const char *navsight_fmt_gha(double d,char *b,size_t n){ return fmt_gha(d,b,n); }

/* ------------------------------------------------------- almanac access */

/* lower-case letters and digits only, so "al nair", "Al Na'ir" and
 * "AL-NAIR" all normalize to the same thing */
static void star_normalize(const char *s,char *out,size_t n){
    size_t k = 0;
    if(!s){ out[0]=0; return; }
    for(; *s && k+1 < n; s++)
        if(isalnum((unsigned char)*s)) out[k++] = (char)tolower((unsigned char)*s);
    out[k] = 0;
}

size_t navsight_star_match(const char *query,int *idx,size_t max){
    char nq[64],nn[64];
    size_t i,count = 0,qlen,total = nav_star_count_get();
    int exact;

    if(!query || !*query) return 0;

    exact = nav_star_find(query);
    if(exact >= 0){
        if(max) idx[0] = exact;
        return 1;
    }

    star_normalize(query,nq,sizeof nq);
    qlen = strlen(nq);
    if(!qlen) return 0;

    for(i=0;i<total;i++){
        star_normalize(nav_star_name(i),nn,sizeof nn);
        if(!strncmp(nn,nq,qlen)){
            if(count < max) idx[count] = (int)i;
            count++;
        }
    }
    if(count) return count;

    for(i=0;i<total;i++){
        star_normalize(nav_star_name(i),nn,sizeof nn);
        if(strstr(nn,nq)){
            if(count < max) idx[count] = (int)i;
            count++;
        }
    }
    return count;
}

int navsight_body_index(const char *name){
    static const char *n[]={"sun","moon","mercury","venus","mars","jupiter","saturn","aries"};
    int i;
    for(i=0;i<8;i++) if(!strcmp(name,n[i])) return i;
    return -1;
}

static int body_at(int b,double jd,NavAlmanac *a){
    return nav_almanac((NavBody)b,jd,a);
}

/* ---------------------------------------------------- paper-fix almanac */

/* Standard-atmosphere refraction from a true altitude, following ICE's
 * F7 convention: two estimates of apparent altitude before the tangent
 * evaluation. This is deliberately separate from the observed-Ha model. */
static double paper_refraction(double true_alt){
    double ha=true_alt, r=0.0;
    if(ha < 4.0){ r=32.5-6.5*ha; ha=true_alt+r/60.0; }
    if(ha < 16.0){
        r=1.0/rad(ha+7.31/(ha+4.4));
        ha=true_alt+r/60.0;
    }
    if(ha < 87.0) r=1.0/tan(rad(ha+7.31/(ha+4.4)));
    return -r;
}

static void paper_position(NavPaperRow *r,double latitude,double longitude,
                           double hp_deg,double sd_deg,int sun,int moon){
    double phi=rad(latitude), dec=rad(r->dec), lha=rad(r->gha+longitude);
    double up=sin(phi)*sin(dec)+cos(phi)*cos(dec)*cos(lha);
    double east=-cos(dec)*sin(lha);
    double north=cos(phi)*sin(dec)-sin(phi)*cos(dec)*cos(lha);
    double hc=asin(clampd(up,-1.0,1.0)), pa;
    r->hc=deg(hc);
    r->have_azimuth=hypot(east,north)>1e-12;
    r->zn=r->have_azimuth ? norm360(deg(atan2(east,north))) : 0.0;
    if(r->hc <= -0.5) return;
    r->have_corrections=1;
    /* ICE evaluates parallax twice and includes its conventional 1.55
     * arcsecond solar-limb increment. Lunar SD is augmented at the DR. */
    pa=cos(hc)*rad(hp_deg);
    r->par_min=cos(hc-pa)*hp_deg*60.0;
    r->sd_min=sd_deg*60.0+(sun ? 1.55/60.0 : 0.0);
    if(moon) r->sd_min*=1.0+sin(hc)/60.27;
    r->refr_min=paper_refraction(r->hc-r->par_min/60.0-r->sd_min/60.0);
    r->sum_min=r->refr_min+r->sd_min+r->par_min;
}

int navsight_paper(const Sight *si,NavPaper *p,char *err,size_t errn){
    static const int month_days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    double jd;
    int b,limit;
    memset(p,0,sizeof *p);
    if(!isfinite(si->dr_lat) || !isfinite(si->dr_lon) ||
       fabs(si->dr_lat)>90.0 || fabs(si->dr_lon)>180.0){
        snprintf(err,errn,"DR must be within 90 degrees latitude and 180 degrees longitude");
        return 1;
    }
    if(si->mo<1 || si->mo>12){ snprintf(err,errn,"invalid UTC date"); return 1; }
    limit=month_days[si->mo-1];
    if(si->mo==2 && si->y%4==0 && (si->y%100!=0 || si->y%400==0)) limit++;
    if(si->d<1 || si->d>limit || si->h<0 || si->h>23 || si->mi<0 || si->mi>59 ||
       !isfinite(si->s) || si->s<0.0 || si->s>=60.0){
        snprintf(err,errn,"invalid UTC date or time"); return 1;
    }
    jd=navsight_jd(si);
    if(!nav_engine_covers(jd)){
        snprintf(err,errn,"%s",nav_engine_last_error()); return 1;
    }
    p->y=si->y; p->mo=si->mo; p->d=si->d; p->h=si->h; p->mi=si->mi; p->s=si->s;
    p->dr_lat=si->dr_lat; p->dr_lon=si->dr_lon;
    p->delta_t=nav_delta_t_seconds(jd);
    b=navsight_body_index(si->body);
    if(!strcmp(si->body,"star")){
        int matches[2];
        size_t n=navsight_star_match(si->star,matches,2);
        NavStarAlmanac a;
        NavPaperRow *r=&p->rows[p->n];
        if(n!=1){ snprintf(err,errn,"enter an unambiguous navigation-star name"); return 1; }
        if(nav_star_almanac((size_t)matches[0],jd,&a)) goto engine_error;
        snprintf(r->name,sizeof r->name,"%s",a.name);
        r->gha=a.gha_deg; r->dec=a.dec_deg;
        paper_position(r,si->dr_lat,si->dr_lon,0.0,0.0,0,0);
        p->n++;
    } else if(b<0){ snprintf(err,errn,"unknown body: %s",si->body); return 1; }
    if(b!=NAV_ARIES && strcmp(si->body,"star")){
        NavAlmanac a;
        NavPaperRow *r=&p->rows[p->n];
        if(nav_almanac((NavBody)b,jd,&a)) goto engine_error;
        snprintf(r->name,sizeof r->name,"%s",nav_body_name((NavBody)b));
        r->gha=a.gha_deg; r->dec=a.dec_deg;
        paper_position(r,si->dr_lat,si->dr_lon,a.hp_deg,a.sd_deg,b==NAV_SUN,b==NAV_MOON);
        p->n++;
    }
    if(b!=NAV_SUN){
        NavAlmanac a;
        NavPaperRow *r=&p->rows[p->n];
        if(nav_almanac(NAV_SUN,jd,&a)) goto engine_error;
        snprintf(r->name,sizeof r->name,"Sun"); r->gha=a.gha_deg; r->dec=a.dec_deg;
        paper_position(r,si->dr_lat,si->dr_lon,a.hp_deg,a.sd_deg,1,0);
        /* As in ICE, a supplementary Sun is omitted below civil/nautical
         * twilight; the requested object itself is always shown. */
        if(r->hc >= -12.0) p->n++;
    }
    {
        NavAlmanac a;
        NavPaperRow *r=&p->rows[p->n];
        memset(r,0,sizeof *r);
        if(nav_almanac(NAV_ARIES,jd,&a)) goto engine_error;
        snprintf(r->name,sizeof r->name,"Aries"); r->is_aries=1; r->gha=a.gha_deg; p->n++;
    }
    return 0;
engine_error:
    snprintf(err,errn,"%s",nav_engine_last_error()[0]?nav_engine_last_error():"almanac calculation failed");
    return 1;
}

void navsight_print_paper(FILE *out,const NavPaper *p,int width){
    size_t i;
    char la[32],lo[32],g[32],d[32],raw[32],h[40],z[16];
    fprintf(out,"Celestial Navigation Data for %04d-%02d-%02d %02d:%02d:%04.1f UTC\n",
            p->y,p->mo,p->d,p->h,p->mi,p->s);
    fprintf(out,"Delta T = %.1f seconds (DUT1=0 assumed)\n",p->delta_t);
    fprintf(out,"For assumed location: %s  %s\n\n",
            fmt_lat(p->dr_lat,la,sizeof la),fmt_lon(p->dr_lon,lo,sizeof lo));
    fprintf(out,"%-16s %10s %11s %11s %6s%s\n","Object","GHA","Dec","Hc","Zn",
            width>=100?" |   Refr     SD     PA    Sum":"");
    for(i=0;i<p->n;i++){
        const NavPaperRow *r=&p->rows[i];
        fmt_gha(r->gha,g,sizeof g);
        if(r->is_aries){ fprintf(out,"%-16s %10s\n",r->name,g); continue; }
        fmt_lat(r->dec,d,sizeof d); fmt_alt(r->hc,raw,sizeof raw);
        snprintf(h,sizeof h,"%s%s",r->hc>=0.0?"+":"",raw);
        if(r->have_azimuth) snprintf(z,sizeof z,"%5.1f",r->zn);
        else snprintf(z,sizeof z,"--");
        fprintf(out,"%-16s %10s %11s %11s %6s",r->name,g,d,h,z);
        if(r->have_corrections){
            if(width<100) fprintf(out,"\n  Refr / SD / PA / Sum ('):");
            else fprintf(out," |");
            fprintf(out," %6.1f %6.1f %6.1f %6.1f",r->refr_min,r->sd_min,r->par_min,r->sum_min);
        }
        fprintf(out,"\n");
    }
    fprintf(out,"\nCorrections in arcminutes; Sun/Moon lower limb; standard atmosphere.\n");
    fprintf(out,"Sum = Refr + SD + PA; apply dip and index error separately.\n");
}

/* --------------------------------------------------------- the reducer */

int navsight_validate(const Sight *si,char *err,size_t errn){
    int days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if(!si){ snprintf(err,errn,"no sight supplied"); return 1; }
    if(si->reduce_at_dr != 0 && si->reduce_at_dr != 1){
        snprintf(err,errn,"invalid reduction method"); return 1;
    }
    if(!memchr(si->body,0,sizeof si->body) || !memchr(si->star,0,sizeof si->star)){
        snprintf(err,errn,"invalid saved body or star name"); return 1;
    }
    if(!isfinite(si->dr_lat) || fabs(si->dr_lat)>90.0){
        snprintf(err,errn,"DR latitude is missing or invalid; enter a latitude from -90 to 90 degrees"); return 1;
    }
    if(!isfinite(si->dr_lon) || fabs(si->dr_lon)>180.0){
        snprintf(err,errn,"DR longitude is missing or invalid; enter a longitude from -180 to 180 degrees"); return 1;
    }
    if(si->y<1600 || si->y>2399 || si->mo<1 || si->mo>12){
        snprintf(err,errn,"invalid sight UTC date (use years 1600 through 2399)"); return 1;
    }
    if(si->y%4==0 && (si->y%100!=0 || si->y%400==0)) days[1]=29;
    if(si->d<1 || si->d>days[si->mo-1] || si->h<0 || si->h>23 || si->mi<0 || si->mi>59 ||
       !isfinite(si->s) || si->s<0 || si->s>=60){
        snprintf(err,errn,"invalid sight UTC date or time"); return 1;
    }
    if(!isfinite(si->hs_deg) || si->hs_deg<0 || si->hs_deg>(si->artificial?180.0:90.0)){
        snprintf(err,errn,"invalid sextant altitude; check Hs and the horizon setting"); return 1;
    }
    if(!isfinite(si->ie_min) || fabs(si->ie_min)>120 || !isfinite(si->he) || si->he<0 || si->he>10000 ||
       !isfinite(si->temp_c) || si->temp_c<-100 || si->temp_c>100 ||
       !isfinite(si->press_mb) || si->press_mb<0 || si->press_mb>1200){
        snprintf(err,errn,"invalid index error, height of eye, temperature or pressure"); return 1;
    }
    if(!isfinite(si->short_horizon_nm) || si->short_horizon_nm<0 || si->short_horizon_nm>1000){
        snprintf(err,errn,"invalid short-horizon waterline distance in NM");return 1;
    }
    if(si->short_horizon_nm>0){
        double feet=si->he_metres?si->he/0.3048:si->he;
        double horizon=sqrt(2*3440.1*(feet/6076.1)/0.8321);
        if(si->artificial || feet<=0){
            snprintf(err,errn,"short-horizon sights require a positive eye height and a waterline, not an artificial horizon");return 1;
        }
        if(si->short_horizon_nm>horizon){
            snprintf(err,errn,"waterline is beyond the visible horizon; select Sea horizon instead");return 1;
        }
    }
    if(si->has_motion && (!isfinite(si->course) || si->course<0 || si->course>360 ||
       !isfinite(si->speed) || si->speed<0 || si->speed>100 || !isfinite(si->set) ||
       si->set<0 || si->set>360 || !isfinite(si->drift) || si->drift<0 || si->drift>100)){
        snprintf(err,errn,"invalid vessel motion or current"); return 1;
    }
    return 0;
}

int navsight_reduce(const Sight *si,Reduction *r,char *err,size_t errn){
    double jd_exact,jd_hour,frac;
    int b=-1,star_index=-1;
    NavAlmanac a0,a1,ax;
    NavStarAlmanac sa;
    double h,tC,pMB;

    memset(r,0,sizeof *r);
    if(navsight_validate(si,err,errn)) return 1;

    if(!strcmp(si->body,"star")){
        int m[2];
        size_t nm = navsight_star_match(si->star,m,2);
        if(nm == 0){ snprintf(err,errn,"unknown star: %s",si->star); return 1; }
        if(nm > 1){ snprintf(err,errn,"\"%s\" matches %lu stars; use more letters",si->star,(unsigned long)nm); return 1; }
        star_index = m[0];
        r->is_star = 1;
    } else {
        b = navsight_body_index(si->body);
        if(b < 0 || b == NAV_ARIES){ snprintf(err,errn,"unknown body: %s",si->body); return 1; }
    }

    jd_exact = navsight_jd(si);
    jd_hour  = nav_julian_date(si->y,si->mo,si->d,si->h,0,0.0);
    frac     = (si->mi*60.0 + si->s)/3600.0;
    r->hour_h = si->h;
    r->frac_hour = frac;

    /* ---- Hs -> Ho ---- */
    r->hs = si->hs_deg;
    r->ic_min = si->ie_on_arc ? -fabs(si->ie_min) : fabs(si->ie_min);
    h = r->hs + r->ic_min/60.0;

    if(si->artificial){
        r->halved = h/2.0;
        h = r->halved;
    } else {
        double he = si->he < 0 ? 0 : si->he;
        r->dip_min = (si->he_metres ? 1.76 : 0.97) * sqrt(he);
        if(si->short_horizon_nm>0){
            /* Bowditch (2019), Vol II, Table 14: standard terrestrial refraction.
             * atan returns radians; convert to arcminutes. Distance is to the waterline. */
            double feet=si->he_metres?he/0.3048:he, distance=si->short_horizon_nm;
            r->dip_min=deg(atan(feet/(6076.1*distance)+0.8321*distance/(2*3440.1)))*60;
        }
        h -= r->dip_min/60.0;
    }
    r->ha = h;

    if(r->ha <= -1.0){ snprintf(err,errn,"apparent altitude below the horizon"); return 1; }
    if(r->ha > 90.0){
        snprintf(err,errn,"Ha is above 90 degrees; an artificial-horizon reading must be halved (--ah)");
        return 1;
    }

    tC = si->temp_c; pMB = si->press_mb;
    r->r0_min = 0.0167/tan(rad(r->ha + 7.32/(r->ha + 4.32))) * 60.0;
    r->f = 0.28*pMB/(tC + 273.0);
    r->r_min = r->f * r->r0_min;
    {
        double h_refr = r->ha - r->r_min/60.0;
        double ho = h_refr, hp_deg=0.0, sd_deg=0.0;

        if(r->is_star){
            if(nav_star_almanac((size_t)star_index,jd_exact,&sa)){
                snprintf(err,errn,"star almanac failed"); return 1;
            }
        } else {
            if(body_at(b,jd_exact,&ax)){ snprintf(err,errn,"almanac failed"); return 1; }
            hp_deg = ax.hp_deg; sd_deg = ax.sd_deg;
        }

        if(sd_deg > 0.0 && si->limb != 0){
            r->sd_raw_min = sd_deg*60.0;
            r->sd_min = r->sd_raw_min;
            if(b == NAV_MOON)
                r->sd_min *= (1.0 + sin(rad(hp_deg))*sin(rad(h_refr)));
            if(si->limb < 0) r->sd_min = -r->sd_min;
            ho += r->sd_min/60.0;
            r->used_sd = 1;
        }
        r->hp_min = hp_deg*60.0;
        if(hp_deg > 0.0){
            r->par_min = deg(asin(sin(rad(hp_deg))*cos(rad(h_refr)))) * 60.0;
            ho += r->par_min/60.0;
            r->used_par = 1;
        }
        r->ho = ho;
    }

    /* ---- almanac: hourly row + increments ---- */
    if(r->is_star){
        NavStarAlmanac sx;
        if(body_at(NAV_ARIES,jd_hour,&a0)){ snprintf(err,errn,"almanac failed"); return 1; }
        if(nav_star_almanac((size_t)star_index,jd_exact,&sx)){ snprintf(err,errn,"star almanac failed"); return 1; }
        r->gha_aries_hour = a0.gha_deg;
        r->inc_deg = ARIES_RATE*frac;
        r->gha_aries = norm360(a0.gha_deg + r->inc_deg);
        r->sha_deg = sx.sha_deg;
        r->gha = norm360(r->gha_aries + sx.sha_deg);
        r->dec = sx.dec_deg;
        r->dec_hour = sx.dec_deg;
        r->gha_exact = sx.gha_deg;
        r->dec_exact = sx.dec_deg;
    } else {
        double dgha,draw,base;
        if(body_at(b,jd_hour,&a0) || body_at(b,jd_hour+1.0/24.0,&a1)){
            snprintf(err,errn,"almanac failed"); return 1;
        }
        base = (b == NAV_MOON) ? MOON_RATE : 15.0;
        dgha = norm360(a1.gha_deg - a0.gha_deg);
        if(dgha > 180.0) dgha -= 360.0;
        r->v_min = (dgha - base)*60.0;
        r->has_v = (b != NAV_SUN);
        r->inc_deg = base*frac;
        r->vcorr_min = r->has_v ? r->v_min*frac : 0.0;
        r->gha_hour = a0.gha_deg;
        r->gha = norm360(a0.gha_deg + r->inc_deg + r->vcorr_min/60.0);

        draw = (a1.dec_deg - a0.dec_deg)*60.0;
        r->dec_hour = a0.dec_deg;
        r->dec = a0.dec_deg + draw*frac/60.0;
        r->d_min = (a0.dec_deg >= 0.0) ? draw : -draw;   /* + when |Dec| increasing */
        r->dcorr_min = r->d_min*frac;

        if(!body_at(b,jd_exact,&ax)){
            r->gha_exact = ax.gha_deg;
            r->dec_exact = ax.dec_deg;
        }
    }

    if(si->reduce_at_dr){
        double phi=rad(si->dr_lat), dec=rad(r->dec), h;
        double east, north, up;
        r->ap_lat=si->dr_lat;
        r->ap_lon=si->dr_lon;
        r->lha_deg=norm360(r->gha+si->dr_lon);
        h=rad(r->lha_deg);
        up=sin(phi)*sin(dec)+cos(phi)*cos(dec)*cos(h);
        east=-cos(dec)*sin(h);
        north=cos(phi)*sin(dec)-sin(phi)*cos(dec)*cos(h);
        if(hypot(east,north)<1e-12){
            snprintf(err,errn,"azimuth is undefined at the zenith or nadir; use another sight");
            return 1;
        }
        h=deg(asin(clampd(up,-1,1)));
        if(h < -1.0){
            snprintf(err,errn,"body is below the horizon at the DR position");
            return 1;
        }
        r->hc_min=floor(h*600.0+0.5)/10.0;
        r->zn=norm360(deg(atan2(east,north)));
    } else {
        /* ---- assumed position ---- */
        {
            double lha_whole,alon;
            r->ap_lat = floor(si->dr_lat + 0.5);
            lha_whole = floor(si->dr_lon + r->gha + 0.5);
            alon = lha_whole - r->gha;
            if(alon >  180.0) alon -= 360.0;
            if(alon < -180.0) alon += 360.0;
            r->ap_lon = alon;
            r->lha = ((int)floor(norm360(lha_whole)+0.5)) % 360;
            r->lha_deg = r->lha;
        }

        /* ---- HO 229 ---- */
        {
            double L = fabs(r->ap_lat);
            int lat_north = (r->ap_lat >= 0.0);
            double D = fabs(r->dec);
            double sgn;
            double hc_tab,hc_next,hc_exact,z;

            r->same_name = ((r->dec >= 0.0) == lat_north) || r->dec == 0.0;
            sgn = r->same_name ? 1.0 : -1.0;
            r->dec_deg_whole = (int)floor(D);
            r->dec_inc_min = (D - floor(D))*60.0;

            {
                double sl=sin(rad(L)), cl=cos(rad(L)), ch=cos(rad((double)r->lha));
                double dd;
                dd = sgn*floor(D);
                hc_tab  = deg(asin(clampd(sl*sin(rad(dd)) + cl*cos(rad(dd))*ch,-1,1)));
                dd = sgn*(floor(D)+1.0);
                hc_next = deg(asin(clampd(sl*sin(rad(dd)) + cl*cos(rad(dd))*ch,-1,1)));
                dd = sgn*D;
                hc_exact= deg(asin(clampd(sl*sin(rad(dd)) + cl*cos(rad(dd))*ch,-1,1)));

                {
                    double sh=sin(rad(hc_exact)), den=cl*cos(rad(hc_exact));
                    z = (fabs(den) > 1e-12)
                        ? deg(acos(clampd((sin(rad(dd)) - sl*sh)/den,-1,1)))
                        : 0.0;
                }
            }

            if(hc_exact < -1.0){
                snprintf(err,errn,"body is below the horizon at the assumed position");
                return 1;
            }

            r->tab_hc_min  = floor(hc_tab*60.0*10.0+0.5)/10.0;
            r->d_tab_min   = floor((hc_next-hc_tab)*60.0*10.0+ (hc_next>hc_tab?0.5:-0.5))/10.0;
            r->dcorr229_min= r->d_tab_min * r->dec_inc_min/60.0;
            r->hc_min      = floor(hc_exact*60.0*10.0+0.5)/10.0;
            r->secdiff_min = r->hc_min - (r->tab_hc_min + r->dcorr229_min);

            r->z = z;
            if(lat_north) r->zn = (r->lha > 180) ? z : 360.0 - z;
            else          r->zn = (r->lha > 180) ? 180.0 - z : 180.0 + z;
            r->zn = norm360(r->zn);
        }

    } /* reduction method */

    r->intercept_nm = (floor(r->ho*60.0*10.0+0.5)/10.0) - r->hc_min;
    return 0;
}

/* ------------------------------------------------------------ printing */

static const char *body_label(const Sight *si){
    return (!strcmp(si->body,"star")) ? si->star : si->body;
}

void navsight_print_correction(FILE *out,const Sight *si,const Reduction *r){
    char b[40];
    fprintf(out,"Sight     %s  %04d-%02d-%02d %02d:%02d:%05.2f UTC\n",
           body_label(si),si->y,si->mo,si->d,si->h,si->mi,si->s);
    fprintf(out,"\nHs -> Ho\n");
    fprintf(out,"  %-12s %10s\n","Hs",fmt_alt(r->hs,b,sizeof b));
    fprintf(out,"  %-12s %10s   index error %.1f' %s arc\n","IC",
           fmt_min(r->ic_min,b,sizeof b),fabs(si->ie_min),si->ie_on_arc?"on":"off");
    if(si->artificial){
        fprintf(out,"  %-12s %10s   double altitude (artificial horizon)\n","Hs + IC",
               fmt_alt(r->hs + r->ic_min/60.0,b,sizeof b));
        fprintf(out,"  %-12s %10s   no dip with an artificial horizon\n","/ 2",
               fmt_alt(r->halved,b,sizeof b));
    } else if(si->short_horizon_nm>0){
        fprintf(out,"  %-12s %10s   short of horizon; waterline %.4f NM, eye %.2f %s (Bowditch Table 14)\n","Dip",
                fmt_min(-r->dip_min,b,sizeof b),si->short_horizon_nm,si->he,si->he_metres?"m":"ft");
    } else {
        fprintf(out,"  %-12s %10s   %.2f' x sqrt(%.1f %s)\n","Dip",
               fmt_min(-r->dip_min,b,sizeof b),si->he_metres?1.76:0.97,
               si->he,si->he_metres?"m":"ft");
    }
    fprintf(out,"  %-12s %10s   apparent altitude\n","Ha",fmt_alt(r->ha,b,sizeof b));
    fprintf(out,"  %-12s %10s   0.0167 deg / tan(Ha + 7.32/(Ha + 4.32))\n","R0",
           fmt_min(-r->r0_min,b,sizeof b));
    fprintf(out,"  %-12s %10.4f   0.28 x %.1f mb / %.1f K\n","f",r->f,si->press_mb,si->temp_c+273.0);
    fprintf(out,"  %-12s %10s\n","R = f x R0",fmt_min(-r->r_min,b,sizeof b));
    if(r->used_sd){
        char lab[16];
        snprintf(lab,sizeof lab,"SD (%s)",si->limb>0?"LL":"UL");
        fprintf(out,"  %-12s %10s   ",lab,fmt_min(r->sd_min,b,sizeof b));
        if(!strcmp(si->body,"moon"))
            fprintf(out,"almanac %.2f' augmented to %.2f'\n",r->sd_raw_min,fabs(r->sd_min));
        else
            fprintf(out,"almanac SD %.2f'\n",r->sd_raw_min);
    }
    if(r->used_par)
        fprintf(out,"  %-12s %10s   asin(sin HP x cos H)\n","Parallax",fmt_min(r->par_min,b,sizeof b));
    fprintf(out,"  %-12s %10s   Ho - %s\n","Total",
           fmt_min((r->ho - (si->artificial ? r->hs/2.0 : r->hs))*60.0,b,sizeof b),
           si->artificial?"Hs/2":"Hs");
    fprintf(out,"  %-12s %10s   observed altitude\n","Ho",fmt_alt(r->ho,b,sizeof b));
}

void navsight_print_reduction(FILE *out,const Sight *si,const Reduction *r){
    char b[40],c2[40],lab[16];
    double tmin = si->mi + si->s/60.0;

    fprintf(out,"\nAlmanac\n");
    if(r->is_star){
        snprintf(lab,sizeof lab,"GHA Aries %02dh",r->hour_h);
        fprintf(out,"  %-14s %10s\n",lab,fmt_gha(r->gha_aries_hour,b,sizeof b));
        fprintf(out,"  %-14s %10s   %.2f min at 15 02.46'/h\n","Increment",
               fmt_gha(r->inc_deg,b,sizeof b),tmin);
        fprintf(out,"  %-14s %10s\n","GHA Aries",fmt_gha(r->gha_aries,b,sizeof b));
        fprintf(out,"  %-14s %10s   %s\n","SHA",fmt_gha(r->sha_deg,b,sizeof b),si->star);
        fprintf(out,"  %-14s %10s\n","GHA",fmt_gha(r->gha,b,sizeof b));
        fprintf(out,"  %-14s %10s\n","Dec",fmt_lat(r->dec,b,sizeof b));
    } else {
        snprintf(lab,sizeof lab,"GHA %02dh",r->hour_h);
        fprintf(out,"  %-14s %10s\n",lab,fmt_gha(r->gha_hour,b,sizeof b));
        fprintf(out,"  %-14s %10s   %.2f min at %s/h\n","Increment",fmt_gha(r->inc_deg,b,sizeof b),
               tmin,(!strcmp(si->body,"moon"))?"14 19.0'":"15 00.0'");
        if(r->has_v)
            fprintf(out,"  %-14s %10s   v %s x %.4f h\n","v corr",
                   fmt_min(r->vcorr_min,b,sizeof b),fmt_min(r->v_min,c2,sizeof c2),r->frac_hour);
        fprintf(out,"  %-14s %10s\n","GHA",fmt_gha(r->gha,b,sizeof b));
        snprintf(lab,sizeof lab,"Dec %02dh",r->hour_h);
        fprintf(out,"  %-14s %10s\n",lab,fmt_lat(r->dec_hour,b,sizeof b));
        fprintf(out,"  %-14s %10s   d %s x %.4f h\n","d corr",
               fmt_min(r->dcorr_min,b,sizeof b),fmt_min(r->d_min,c2,sizeof c2),r->frac_hour);
        fprintf(out,"  %-14s %10s\n","Dec",fmt_lat(r->dec,b,sizeof b));
        {
            double dg = (r->gha_exact - r->gha)*60.0;
            double dd = (r->dec_exact - r->dec)*60.0;
            while(dg >  180.0*60.0) dg -= 360.0*60.0;
            while(dg < -180.0*60.0) dg += 360.0*60.0;
            fprintf(out,"  %-14s %10s   engine at the exact time, Dec %s\n","check",
                   fmt_min(dg,b,sizeof b),fmt_min(dd,c2,sizeof c2));
        }
    }

    if(si->reduce_at_dr){
        fprintf(out,"\nDirect reduction at DR\n");
        fprintf(out,"  %-14s %10s\n","DR latitude",fmt_lat(r->ap_lat,b,sizeof b));
        fprintf(out,"  %-14s %10s\n","DR longitude",fmt_lon(r->ap_lon,b,sizeof b));
        fprintf(out,"  %-14s %10s   GHA + DR longitude, without rounding\n","LHA",fmt_alt(r->lha_deg,b,sizeof b));
        fprintf(out,"  Spherical calculation with unrounded DR and declination\n");
        fprintf(out,"  %-14s %10s\n","Hc",fmt_alt(r->hc_min/60.0,b,sizeof b));
        fprintf(out,"  %-14s %10.1f   degrees true\n","Zn",r->zn);
    } else {
        fprintf(out,"\nAssumed position\n");
        fprintf(out,"  %-14s %10s   DR latitude to the whole degree\n","a-Lat",fmt_lat(r->ap_lat,b,sizeof b));
        fprintf(out,"  %-14s %10s   chosen for a whole-degree LHA\n","a-Lon",fmt_lon(r->ap_lon,b,sizeof b));
        snprintf(b,sizeof b,"%03d",r->lha);
        fprintf(out,"  %-14s %10s\n","LHA",b);

        fprintf(out,"\nHO 229   LHA %03d, Lat %02d %c, Dec %02d %s name\n",
               r->lha,(int)fabs(r->ap_lat),r->ap_lat<0?'S':'N',
               r->dec_deg_whole,r->same_name?"Same":"Contrary");
        fprintf(out,"  %-14s %10s\n","Tab Hc",fmt_alt(r->tab_hc_min/60.0,b,sizeof b));
        fprintf(out,"  %-14s %10s\n","d",fmt_min(r->d_tab_min,b,sizeof b));
        fprintf(out,"  %-14s %9.1f'\n","Dec inc",r->dec_inc_min);
        fprintf(out,"  %-14s %10s   d x Dec inc / 60\n","d corr",fmt_min(r->dcorr229_min,b,sizeof b));
        if(fabs(r->secdiff_min) >= 0.05)
            fprintf(out,"  %-14s %10s   second-difference residual\n","2nd diff",fmt_min(r->secdiff_min,b,sizeof b));
        fprintf(out,"  %-14s %10s\n","Hc",fmt_alt(r->hc_min/60.0,b,sizeof b));
        snprintf(b,sizeof b,"%c %.1f %c",r->ap_lat<0?'S':'N',r->z,r->lha>180?'E':'W');
        fprintf(out,"  %-14s %10s\n","Z",b);
        snprintf(b,sizeof b,"%05.1f",r->zn);
        fprintf(out,"  %-14s %10s\n","Zn",b);

    }

    fprintf(out,"\nLine of position\n");
    fprintf(out,"  %-14s %10s\n","Ho",fmt_alt(r->ho,b,sizeof b));
    fprintf(out,"  %-14s %10s\n","Hc",fmt_alt(r->hc_min/60.0,b,sizeof b));
    fprintf(out,"  %-14s %8.1f NM %s\n","Intercept",fabs(r->intercept_nm),
           r->intercept_nm>=0?"Toward":"Away");
    fprintf(out,"  %-14s from %s %s %s, lay off %.1f NM %s %03.0f,\n"
           "                 then draw the LOP at right angles\n","Plot",si->reduce_at_dr?"DR":"AP",
           fmt_lat(r->ap_lat,b,sizeof b),fmt_lon(r->ap_lon,c2,sizeof c2),
           fabs(r->intercept_nm),r->intercept_nm>=0?"toward":"away from",r->zn);
}

void navsight_print_brief(FILE *out,const Sight *si,const Reduction *r){
    char ho[40],la[40],lo[40],id[16];
    if(si->id > 0) snprintf(id,sizeof id,"%3d",si->id);
    else           snprintf(id,sizeof id,"  -");
    fprintf(out,"%s  %-9s %04d-%02d-%02d %02d:%02d:%04.1f  Ho %s  a %5.1f %s  Zn %05.1f  %s %s %s%s%s\n",
           id,body_label(si),si->y,si->mo,si->d,si->h,si->mi,si->s,
           fmt_alt(r->ho,ho,sizeof ho),
           fabs(r->intercept_nm),r->intercept_nm>=0?"T":"A",r->zn,si->reduce_at_dr?"DR":"AP",
           fmt_lat(r->ap_lat,la,sizeof la),fmt_lon(r->ap_lon,lo,sizeof lo),
           si->label[0]?"  ":"",si->label);
    if(fabs(r->intercept_nm) > 60.0)
        fprintf(out,"     note: intercept over 60 NM; check the DR, the time and the limb\n");
}

/* --------------------------------------------------------------- store */

const char *navsight_store_path(void){
    static char path[1024];
    const char *env = getenv("NAVALM_SAVE");
    const char *home;
    if(env && *env) return env;
    home = getenv("HOME");
    if(!home || !*home) home = ".";
    snprintf(path,sizeof path,"%s/.naval.save",home);
    return path;
}

static void store_grow(SightStore *st){
    if(st->n < st->cap) return;
    st->cap = st->cap ? st->cap*2 : 16;
    st->v = (Sight*)realloc(st->v,st->cap*sizeof(Sight));
}

void navsight_store_free(SightStore *st){
    free(st->v); st->v=NULL; st->n=st->cap=0;
}

int navsight_store_add(SightStore *st,const Sight *si){
    size_t i; int maxid=0;
    for(i=0;i<st->n;i++) if(st->v[i].id > maxid) maxid = st->v[i].id;
    store_grow(st);
    if(!st->v) return 1;
    st->v[st->n] = *si;
    st->v[st->n].id = maxid+1;
    st->n++;
    return st->v[st->n-1].id;
}

Sight *navsight_store_find(SightStore *st,int id){
    size_t i;
    for(i=0;i<st->n;i++) if(st->v[i].id == id) return &st->v[i];
    return NULL;
}

int navsight_store_delete(SightStore *st,int id){
    size_t i;
    for(i=0;i<st->n;i++)
        if(st->v[i].id == id){
            memmove(&st->v[i],&st->v[i+1],(st->n-i-1)*sizeof(Sight));
            st->n--;
            return 0;
        }
    return 1;
}

/* key=value line parser; label may be quoted */
static int kv_next(char **p,char *key,size_t kn,char *val,size_t vn){
    char *s=*p; size_t i;
    while(*s==' '||*s=='\t') s++;
    if(!*s || *s=='\n') return 0;
    for(i=0;*s && *s!='=' && *s!=' ' && *s!='\n';s++) if(i+1<kn) key[i++]=*s;
    key[i]=0;
    if(*s!='='){ *p=s; return 0; }
    s++;
    i=0;
    if(*s=='"'){
        s++;
        for(;*s && *s!='"';s++) if(i+1<vn) val[i++]=*s;
        if(*s=='"') s++;
    } else {
        for(;*s && *s!=' ' && *s!='\n';s++) if(i+1<vn) val[i++]=*s;
    }
    val[i]=0;
    *p=s;
    return 1;
}

static void sight_defaults(Sight *si){
    memset(si,0,sizeof *si);
    strcpy(si->body,"sun");
    si->limb = 1;
    si->temp_c = 10.0;
    si->press_mb = 1010.0;
    si->he = 0.0;
}

int navsight_store_load(SightStore *st,char *err,size_t errn){
    FILE *f;
    char line[1024];
    const char *path = navsight_store_path();

    st->v=NULL; st->n=st->cap=0;
    f = fopen(path,"r");
    if(!f) return 0;                     /* no store yet is not an error */

    while(fgets(line,sizeof line,f)){
        char key[32],val[128],*p=line;
        Sight si;
        if(line[0]=='#' || line[0]=='\n') continue;
        if(strncmp(line,"lop ",4)) continue;
        sight_defaults(&si);
        p = line+4;
        while(kv_next(&p,key,sizeof key,val,sizeof val)){
            if(!strcmp(key,"id"))        si.id = atoi(val);
            else if(!strcmp(key,"body")) { strncpy(si.body,val,sizeof si.body-1); si.body[sizeof si.body-1]=0; }
            else if(!strcmp(key,"star")) { strncpy(si.star,val,sizeof si.star-1); si.star[sizeof si.star-1]=0; }
            else if(!strcmp(key,"time")) sscanf(val,"%d-%d-%dT%d:%d:%lf",&si.y,&si.mo,&si.d,&si.h,&si.mi,&si.s);
            else if(!strcmp(key,"hs"))       si.hs_deg = atof(val);
            else if(!strcmp(key,"ie"))       si.ie_min = atof(val);
            else if(!strcmp(key,"iearc"))    si.ie_on_arc = !strcmp(val,"on");
            else if(!strcmp(key,"horizon"))  si.artificial = !strcmp(val,"ah");
            else if(!strcmp(key,"he"))       si.he = atof(val);
            else if(!strcmp(key,"heunit"))   si.he_metres = !strcmp(val,"m");
            else if(!strcmp(key,"limb"))     si.limb = !strcmp(val,"ll") ? 1 : (!strcmp(val,"ul") ? -1 : 0);
            else if(!strcmp(key,"temp"))     si.temp_c = atof(val);
            else if(!strcmp(key,"press"))    si.press_mb = atof(val);
            else if(!strcmp(key,"short_horizon")) si.short_horizon_nm = atof(val);
            else if(!strcmp(key,"at_dr"))    si.reduce_at_dr = atoi(val);
            else if(!strcmp(key,"drlat"))    si.dr_lat = atof(val);
            else if(!strcmp(key,"drlon"))    si.dr_lon = atof(val);
            else if(!strcmp(key,"label"))    { strncpy(si.label,val,sizeof si.label-1); si.label[sizeof si.label-1]=0; }
            else if(!strcmp(key,"course"))   { si.course = atof(val); si.has_motion = 1; }
            else if(!strcmp(key,"speed"))    { si.speed  = atof(val); si.has_motion = 1; }
            else if(!strcmp(key,"set"))      { si.set    = atof(val); si.has_motion = 1; }
            else if(!strcmp(key,"drift"))    { si.drift  = atof(val); si.has_motion = 1; }
        }
        store_grow(st);
        if(!st->v){ fclose(f); snprintf(err,errn,"out of memory"); return 1; }
        st->v[st->n++] = si;
    }
    fclose(f);
    return 0;
}

int navsight_store_save(const SightStore *st,char *err,size_t errn){
    const char *path = navsight_store_path();
    char tmp[1100];
    FILE *f;
    size_t i;

    snprintf(tmp,sizeof tmp,"%s.tmp",path);
    f = fopen(tmp,"w");
    if(!f){ snprintf(err,errn,"cannot write %s",tmp); return 1; }

    fprintf(f,"# navalm LOP store, format 1\n");
    fprintf(f,"# one line per sight; the raw observation is stored and re-reduced on use\n");
    for(i=0;i<st->n;i++){
        const Sight *s = &st->v[i];
        fprintf(f,"lop id=%d body=%s",s->id,s->body);
        if(s->star[0]) fprintf(f," star=%s",s->star);
        fprintf(f," time=%04d-%02d-%02dT%02d:%02d:%06.3f",s->y,s->mo,s->d,s->h,s->mi,s->s);
        fprintf(f," hs=%.6f ie=%.2f iearc=%s",s->hs_deg,s->ie_min,s->ie_on_arc?"on":"off");
        fprintf(f," horizon=%s",s->artificial?"ah":"sea");
        if(!s->artificial) fprintf(f," he=%.2f heunit=%s",s->he,s->he_metres?"m":"ft");
        fprintf(f," limb=%s",s->limb>0?"ll":(s->limb<0?"ul":"center"));
        fprintf(f," temp=%.1f press=%.1f",s->temp_c,s->press_mb);
        fprintf(f," drlat=%.6f drlon=%.6f",s->dr_lat,s->dr_lon);
        if(s->short_horizon_nm>0) fprintf(f," short_horizon=%.9g",s->short_horizon_nm);
        if(s->reduce_at_dr) fprintf(f," at_dr=%d",s->reduce_at_dr);
        if(s->has_motion)
            fprintf(f," course=%.1f speed=%.2f set=%.1f drift=%.2f",
                    s->course,s->speed,s->set,s->drift);
        if(s->label[0]) fprintf(f," label=\"%s\"",s->label);
        fputc('\n',f);
    }
    fclose(f);
    if(nav_replace_file(tmp,path)){ snprintf(err,errn,"cannot rename %s to %s",tmp,path); return 1; }
    return 0;
}

/* ----------------------------------------------------------- running fix */

int navsight_fix(const Sight *sel,const Reduction *red,size_t n,
                 int have_at,int ay,int amo,int ad,int ah,int ami,double as,
                 FixResult *out,char *err,size_t errn)
{
    double jd_fix = 0.0, lat0=0, lon0=0, coslat;
    double A11=0,A12=0,A22=0,B1=0,B2=0,det;
    double bx[16],ux[16],uy[16];
    size_t i;

    if(n < 2 || n > 16){ snprintf(err,errn,"a fix needs 2 to 16 LOPs"); return 1; }
    if(!sel || !red || !out){ snprintf(err,errn,"missing fix data"); return 1; }
    for(i=0;i<n;i++){
        if(navsight_validate(&sel[i],err,errn)) return 1;
        if(!isfinite(red[i].ap_lat) || fabs(red[i].ap_lat)>90 ||
           !isfinite(red[i].ap_lon) || fabs(red[i].ap_lon)>180 ||
           !isfinite(red[i].zn) || !isfinite(red[i].intercept_nm)){
            snprintf(err,errn,"invalid reduced sight; check its DR and reduce it again"); return 1;
        }
    }


    memset(out,0,sizeof *out);

    if(have_at){
        Sight at = sel[0];
        at.y=ay; at.mo=amo; at.d=ad; at.h=ah; at.mi=ami; at.s=as;
        if(navsight_validate(&at,err,errn)) return 1;
        jd_fix = navsight_jd(&at);
    }
    else for(i=0;i<n;i++){
        double jd = navsight_jd(&sel[i]);
        if(jd > jd_fix) jd_fix = jd;
    }
    jd_to_cal(jd_fix,&out->fy,&out->fmo,&out->fd,&out->fh,&out->fmi,&out->fs);

    for(i=0;i<n;i++){ lat0 += red[i].ap_lat; lon0 += red[i].ap_lon; }
    lat0 /= (double)n; lon0 /= (double)n;
    coslat = cos(rad(lat0));
    if(fabs(coslat) < 1e-6){ snprintf(err,errn,"fix too close to the pole for a plane solution"); return 1; }

    for(i=0;i<n;i++){
        double dt = (jd_fix - navsight_jd(&sel[i]))*24.0;      /* hours */
        double north=0,east=0,lat,lon,x,y;

        if(sel[i].has_motion && dt != 0.0){
            north += sel[i].speed*dt*cos(rad(sel[i].course));
            east  += sel[i].speed*dt*sin(rad(sel[i].course));
            north += sel[i].drift*dt*cos(rad(sel[i].set));
            east  += sel[i].drift*dt*sin(rad(sel[i].set));
        }
        out->advanced_nm[i]  = sqrt(north*north + east*east);
        out->advanced_brg[i] = norm360(deg(atan2(east,north)));

        /* advanced AP, then a point on the LOP: intercept along Zn */
        lat = red[i].ap_lat + north/60.0;
        lon = red[i].ap_lon + east/(60.0*coslat);
        lat += red[i].intercept_nm*cos(rad(red[i].zn))/60.0;
        lon += red[i].intercept_nm*sin(rad(red[i].zn))/(60.0*coslat);

        x = (lon - lon0)*60.0*coslat;    /* east,  NM */
        y = (lat - lat0)*60.0;           /* north, NM */

        ux[i] = sin(rad(red[i].zn));
        uy[i] = cos(rad(red[i].zn));
        bx[i] = ux[i]*x + uy[i]*y;

        A11 += ux[i]*ux[i];
        A12 += ux[i]*uy[i];
        A22 += uy[i]*uy[i];
        B1  += ux[i]*bx[i];
        B2  += uy[i]*bx[i];
    }

    det = A11*A22 - A12*A12;
    if(fabs(det) < 1e-9){
        snprintf(err,errn,"LOPs are parallel or nearly so; no fix");
        return 1;
    }
    {
        double x = ( A22*B1 - A12*B2)/det;
        double y = (-A12*B1 + A11*B2)/det;
        double ss = 0.0;

        out->lat = lat0 + y/60.0;
        out->lon = lon0 + x/(60.0*coslat);

        for(i=0;i<n;i++){
            out->residual_nm[i] = ux[i]*x + uy[i]*y - bx[i];
            ss += out->residual_nm[i]*out->residual_nm[i];
        }
        out->rms_nm = sqrt(ss/(double)n);
        if(!isfinite(out->lat) || !isfinite(out->lon) || !isfinite(out->rms_nm)){
            snprintf(err,errn,"fix cannot be calculated from these sights; check the DR and motion"); return 1;
        }


        if(n >= 3){
            double s2 = ss/(double)(n-2);
            double cxx =  s2*A22/det, cyy = s2*A11/det, cxy = -s2*A12/det;
            double tr = cxx+cyy, dd = sqrt((cxx-cyy)*(cxx-cyy) + 4*cxy*cxy);
            double l1 = (tr+dd)/2, l2 = (tr-dd)/2;
            double ex,ey;
            if(l2 < 0) l2 = 0;
            if(fabs(cxy) > 1e-12){ ex = l1-cyy; ey = cxy; }
            else { ex = (cxx>=cyy)?1:0; ey = (cxx>=cyy)?0:1; }
            out->semi_major_nm = sqrt(l1);
            out->semi_minor_nm = sqrt(l2);
            out->ellipse_brg = norm360(deg(atan2(ex,ey)));
            out->have_ellipse = 1;
        }
    }
    return 0;
}
