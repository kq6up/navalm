/* SPDX-License-Identifier: MIT — real forward/inverse tests, no personal data. */
#include "navlunar.h"
#include "nav_engine.h"
#include "navsky.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static char error[512];
static void ok(int rc){if(rc){fprintf(stderr,"LUNAR TEST: %s\n",error);exit(1);}}
static NavLunarRequest example(void){
    NavLunarRequest q;navlunar_init(&q);q.distance.mo=11;q.distance.d=3;q.distance.h=13;q.distance.mi=20;
    q.moon_contact=-1;q.distance.dr_lat=34;q.distance.dr_lon=-117;q.distance.hs_deg=9;
    q.moon.sight=q.other.sight=q.distance;strcpy(q.moon.sight.body,"moon");q.moon.sight.limb=1;return q;
}
static void epoch(Sight *s,double when){char date[32],time[32];navsky_utc(when,date,sizeof date,time,sizeof time);
    sscanf(date,"%d-%d-%d",&s->y,&s->mo,&s->d);sscanf(time,"%d:%d:%lf",&s->h,&s->mi,&s->s);}
static NavLunarSolution generated(NavLunarRequest *q,double correction,double lat,double lon){
    NavLunarSolution s;ok(navlunar_validate(q,error,sizeof error));ok(navlunar_predict(q,correction,lat,lon,&s,error,sizeof error));
    q->distance.hs_deg=s.raw_distance_deg;
    if(q->moon.measured)q->moon.sight.hs_deg=s.predicted_hs[0];if(q->other.measured)q->other.sight.hs_deg=s.predicted_hs[1];return s;
}
static void recovered(NavLunarRequest *q,double truth){
    NavLunarRequest original=*q;NavLunarResult r;ok(navlunar_solve(q,&r,error,sizeof error));
    assert(!memcmp(q,&original,sizeof *q));assert(r.count>0);double best=1e9;
    for(int i=0;i<r.count;i++)best=fmin(best,fabs(r.solutions[i].correction_sec-truth));
    assert(best<.04);
}
int main(int argc,char **argv){
    ok(nav_engine_init(error,sizeof error));
    NavLunarRequest defaults;navlunar_init(&defaults);assert(defaults.moon_contact==1);
    NavLunarRequest q=example();generated(&q,187,q.distance.dr_lat,q.distance.dr_lon);recovered(&q,187);
    /* Prior correction is a search centre; it must not be applied twice. */
    q.initial_correction_sec=160;q.search_seconds=100;recovered(&q,187);
    /* Index sign and contact conventions, including a resolved planetary limb. */
    q=example();q.distance.ie_min=2.5;q.distance.ie_on_arc=1;q.moon_contact=1;q.other_contact=-1;
    generated(&q,-73,34,-117);recovered(&q,-73);
    /* Independent Horizons centre-direction comparison. Public round AP;
     * separate fixture checked outside the forward/inverse identity below. */
    q=example();q.distance.ie_min=0;NavLunarSolution nominal;ok(navlunar_predict(&q,0,34,-117,&nominal,error,sizeof error));
    /* NASA/JPL Horizons 2026-11-03 13:20 UTC, airless WGS84 observer
     * -117E,34N,0km: Moon az/el 125.621217007/53.863377070;
     * Jupiter 130.840614253/62.171922069. See tests/lunar-horizons.md. */
    assert(fabs(nominal.topocentric_deg-8.749512888734568)<.00005);
    assert(nominal.topocentric_deg>nominal.geocentric_deg);
    assert(nominal.apparent_deg<nominal.topocentric_deg);
    /* Without atmosphere, far and near limb spacing equals two augmented
     * semidiameters; raw index signs and AH doubling remain physical. */
    q=example();q.distance.press_mb=q.moon.sight.press_mb=q.other.sight.press_mb=0;
    NavLunarSolution far,near,centre;
    q.moon_contact=-1;ok(navlunar_predict(&q,0,34,-117,&far,error,sizeof error));
    q.moon_contact=1;ok(navlunar_predict(&q,0,34,-117,&near,error,sizeof error));
    assert(fabs((far.raw_distance_deg-near.raw_distance_deg)*60-2*far.moon_sd_min)<1e-6);
    q.moon_contact=0;q.moon.sight.limb=0;q.moon.sight.artificial=1;
    ok(navlunar_predict(&q,0,34,-117,&centre,error,sizeof error));
    assert(fabs(centre.predicted_hs[0]/2-far.centre_alt_deg[0])<1e-8);
    q.distance.ie_min=2;q.distance.ie_on_arc=0;ok(navlunar_predict(&q,0,34,-117,&near,error,sizeof error));
    q.distance.ie_on_arc=1;ok(navlunar_predict(&q,0,34,-117,&far,error,sizeof error));
    assert(fabs((far.raw_distance_deg-near.raw_distance_deg)*60-4)<1e-7);
    /* Daylight Sun lunars and a deliberately wrong time span. */
    q=example();strcpy(q.distance.body,"sun");q.distance.h=17;q.other_contact=1;
    generated(&q,-42,34,-117);recovered(&q,-42);
    q.initial_correction_sec=1000;q.search_seconds=5;NavLunarResult empty;
    assert(navlunar_solve(&q,&empty,error,sizeof error)&&!empty.count);
    /* Different watch dates straddling midnight, AH, eye height, instrument
     * errors and DR motion all receive the same (large) UTC correction. */
    q=example();q.distance.h=23;q.distance.mi=59;q.initial_correction_sec=-38400;q.search_seconds=180;
    q.distance.has_motion=1;q.distance.course=90;q.distance.speed=12;q.distance.set=180;q.distance.drift=1.5;
    q.moon.measured=q.other.measured=1;q.moon.sight=q.other.sight=q.distance;
    double w=nav_julian_date(2026,11,3,23,59,0);epoch(&q.moon.sight,w+120/86400.);epoch(&q.other.sight,w-180/86400.);
    q.moon.sight.artificial=1;q.moon.sight.limb=-1;q.moon.sight.ie_min=1.7;q.moon.sight.ie_on_arc=1;
    q.other.sight.he=10;q.other.sight.he_metres=1;q.other.sight.ie_min=.6;q.other.sight.limb=0;
    generated(&q,-38330,34,-117);recovered(&q,-38330);q.refine_position=1;
    q.distance.dr_lat=34.03;q.distance.dr_lon=-117.04;recovered(&q,-38330);
    NavLunarResult r;ok(navlunar_solve(&q,&r,error,sizeof error));assert(fabs(r.solutions[0].lat-34)<.001&&fabs(r.solutions[0].lon+117)<.001);
    /* A synthetic altitude is not an observation and cannot move the AP. */
    q=example();generated(&q,100,34,-117);q.moon.sight.hs_deg=1;q.other.sight.hs_deg=170;
    ok(navlunar_solve(&q,&r,error,sizeof error));assert(fabs(r.solutions[0].lat-34)<1e-12);recovered(&q,100);
    q.refine_position=1;assert(navlunar_validate(&q,error,sizeof error));q.refine_position=0;
    q.moon.measured=1;q.moon.sight.hs_deg=45;ok(navlunar_validate(&q,error,sizeof error));q.refine_position=1;assert(navlunar_validate(&q,error,sizeof error));
    /* Named navigation-star lunars use NOVAS star proper motion/topocentric place. */
    q=example();strcpy(q.distance.body,"star");strcpy(q.distance.star,"Regulus");q.other_contact=0;
    generated(&q,31,34,-117);recovered(&q,31);
    /* Strict profile round trip, explicit epochs and rejection without overwriting output. */
    const char *path=argc>1?argv[1]:"/private/tmp/navalm-lunar-test.lunar";FILE *f=fopen(path,"w");assert(f);assert(!navlunar_write(f,&q));assert(!fclose(f));
    NavLunarRequest loaded;ok(navlunar_load(path,&loaded,error,sizeof error));recovered(&loaded,31);
    f=fopen(path,"a");assert(f);fputs("distance.hs_deg=nan\n",f);fclose(f);NavLunarRequest before=loaded;
    assert(navlunar_load(path,&loaded,error,sizeof error));assert(!memcmp(&before,&loaded,sizeof loaded));remove(path);
    q=example();q.distance.hs_deg=NAN;assert(navlunar_validate(&q,error,sizeof error));q=example();q.distance.d=31;q.distance.mo=2;assert(navlunar_validate(&q,error,sizeof error));
    q=example();q.moon.sight.temp_c=NAN;assert(navlunar_validate(&q,error,sizeof error));
    nav_engine_select("classic");q=example();assert(navlunar_validate(&q,error,sizeof error));
    puts("PASS lunar: fixed AP, signed index/contact, prior once, separate epochs/midnight, AH, motion, joint real fix, mixed/underdetermined guards, stars, profiles and invalid inputs");
    nav_engine_shutdown();return 0;
}
