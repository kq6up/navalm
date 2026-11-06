#include "navsight.h"
#include "nav_engine.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void near(double value,double expected,double tolerance){
    if(!isfinite(value) || fabs(value-expected)>tolerance){
        fprintf(stderr,"got %.9f, expected %.9f +/- %.9f\n",value,expected,tolerance);
        assert(0);
    }
}

int main(void){
    Sight s={0};
    NavPaper p;
    NavAlmanac a;
    char err[200];
    assert(!nav_engine_init(err,sizeof err));
    strcpy(s.body,"sun"); s.y=1988; s.mo=7; s.d=1; s.dr_lat=40; s.dr_lon=-75;
    assert(!navsight_paper(&s,&p,err,sizeof err));
    assert(p.n==2 && p.rows[1].is_aries);
    /* Rounded reference values transcribed from the user's ICE screen. */
    near(p.rows[0].gha,179+3.5/60,0.05/60);
    near(p.rows[0].dec,23+6.7/60,0.05/60);
    near(p.rows[0].hc,4+39.3/60,0.05/60);
    near(p.rows[0].zn,296.5,0.05);
    near(p.rows[0].refr_min,-10.6,0.05);
    near(p.rows[0].sd_min,15.8,0.05);
    near(p.rows[0].par_min,0.1,0.05);
    near(p.rows[0].sum_min,5.3,0.05);
    near(p.rows[1].gha,279+15.9/60,0.05/60);
    near(p.delta_t,56.184,1e-9); /* Current NAVALM time model, not ICE's 56.1. */
    assert(p.local_y==1988 && p.local_mo==6 && p.local_d==30);
    navsight_print_paper(stdout,&p,120);

    strcpy(s.body,"moon");
    assert(!navsight_paper(&s,&p,err,sizeof err));
    assert(p.n==3 && !strcmp(p.rows[1].name,"Sun") && p.rows[2].is_aries);
    assert(p.rows[0].hc < -0.5 && !p.rows[0].have_corrections);
    assert(nav_almanac(NAV_MOON,navsight_jd(&s),&a)==0);
    if(p.rows[0].have_corrections){
        near(p.rows[0].sum_min,p.rows[0].refr_min+p.rows[0].sd_min+p.rows[0].par_min,1e-12);
        assert(p.rows[0].sd_min>0 && p.rows[0].par_min>0);
    }
    s.dr_lat=-25; s.dr_lon=0;
    assert(!navsight_paper(&s,&p,err,sizeof err));
    assert(p.rows[0].have_corrections);
    assert(p.rows[0].sd_min>a.sd_deg*60.0 && p.rows[0].par_min>0.0);
    near(p.rows[0].sum_min,p.rows[0].refr_min+p.rows[0].sd_min+p.rows[0].par_min,1e-12);
    s.dr_lat=40; s.dr_lon=-75;

    strcpy(s.body,"star"); strcpy(s.star,"Vega");
    assert(!navsight_paper(&s,&p,err,sizeof err));
    assert(p.n==3 && !strcmp(p.rows[0].name,"Vega"));
    near(p.rows[0].sd_min,0,0); near(p.rows[0].par_min,0,0);
    strcpy(s.star,"not a star"); assert(navsight_paper(&s,&p,err,sizeof err));

    strcpy(s.body,"sun");
    assert(!nav_almanac(NAV_SUN,navsight_jd(&s),&a));
    s.dr_lat=a.dec_deg; s.dr_lon=-a.gha_deg;
    assert(!navsight_paper(&s,&p,err,sizeof err));
    near(p.rows[0].hc,90,1e-6); assert(!p.rows[0].have_azimuth);
    near(p.rows[0].refr_min,0,0);
    s.dr_lat=-a.dec_deg; s.dr_lon=180-a.gha_deg;
    assert(!navsight_paper(&s,&p,err,sizeof err));
    near(p.rows[0].hc,-90,1e-6); assert(!p.rows[0].have_corrections);
    s.dr_lat=0; s.dr_lon=0;
    assert(!navsight_paper(&s,&p,err,sizeof err)); /* Zero is a valid DR. */
    s.dr_lat=91; assert(navsight_paper(&s,&p,err,sizeof err));
    s.dr_lat=NAN; assert(navsight_paper(&s,&p,err,sizeof err));
    s.dr_lat=40; s.mo=2; s.d=30; assert(navsight_paper(&s,&p,err,sizeof err));
    s.mo=7; s.d=1; s.y=2200; assert(navsight_paper(&s,&p,err,sizeof err));

    /* Independent rounded USNO API reference, retrieved 2026-11-06.
     * https://aa.usno.navy.mil/api/rstt/oneday?date=2026-11-03&coords=51.4779,0&tz=0
     * Sun 06:57 / 11:44 / 16:30; Moon -- / 07:15 / 14:18.
     * API output is rounded to minutes: allow one minute, not exact equality. */
    s.y=2026; s.mo=11; s.d=3; s.h=12; s.dr_lat=51.4779; s.dr_lon=0;
    strcpy(s.body,"moon");
    assert(!navsight_paper(&s,&p,err,sizeof err));
    assert(p.rows[0].events.available && p.rows[0].events.count[0]==0);
    assert(p.rows[0].events.count[1]==1 && p.rows[0].events.count[2]==1);
    near(p.rows[0].events.hours[1][0],7+15./60,1./60);
    near(p.rows[0].events.hours[2][0],14+18./60,1./60);
    for(int k=0;k<3;k++){
        const double reference[]={6+57./60,11+44./60,16+30./60};
        assert(p.rows[1].events.count[k]==1);
        near(p.rows[1].events.hours[k][0],reference[k],1./60);
    }
    /* Same service, coords=34,-117&tz=-8. Civil standard times
     * shifted to LMT by +12 minutes, not by the computer timezone.
     * Moon 00:36 / 07:19 / 13:52; Sun 06:09 / 11:32 / 16:53. */
    s.dr_lat=34; s.dr_lon=-117; s.h=20;
    assert(!navsight_paper(&s,&p,err,sizeof err));
    for(int k=0;k<3;k++){
        const double moon[]={36./60,7+19./60,13+52./60};
        const double sun[]={6+9./60,11+32./60,16+53./60};
        assert(p.rows[0].events.count[k]==1 && p.rows[1].events.count[k]==1);
        near(p.rows[0].events.hours[k][0],moon[k]+8+s.dr_lon/15,1./60);
        near(p.rows[1].events.hours[k][0],sun[k]+8+s.dr_lon/15,1./60);
    }
    s.h=1;
    assert(!navsight_paper(&s,&p,err,sizeof err));
    assert(p.local_y==2026 && p.local_mo==11 && p.local_d==2);
    s.y=2026; s.mo=12; s.d=31; s.h=23; s.dr_lon=179;
    assert(!navsight_paper(&s,&p,err,sizeof err));
    assert(p.local_y==2027 && p.local_mo==1 && p.local_d==1);
    /* Circumpolar, never-rising, stars, planets and Aries-only selection. */
    s.mo=6; s.d=21; s.h=12; s.dr_lon=0; s.dr_lat=80; strcpy(s.body,"sun");
    assert(!navsight_paper(&s,&p,err,sizeof err));
    assert(p.rows[0].events.always_above && p.rows[0].events.count[1]==1);
    s.mo=12;
    assert(!navsight_paper(&s,&p,err,sizeof err));
    assert(p.rows[0].events.always_below && p.rows[0].events.count[1]==1);
    s.dr_lat=34; s.mo=11; s.d=3; strcpy(s.body,"star"); strcpy(s.star,"Altair");
    assert(!navsight_paper(&s,&p,err,sizeof err));
    for(int k=0;k<3;k++) assert(p.rows[0].events.count[k]>=1);
    s.mo=7; s.d=20; s.dr_lon=0;
    assert(!navsight_paper(&s,&p,err,sizeof err));
    assert(p.rows[0].events.count[1]==2); /* Sidereal day is shorter than 24 h. */
    assert(p.rows[0].events.hours[1][0]<0.05 && p.rows[0].events.hours[1][1]>23.9);
    s.mo=11; s.d=3;
    strcpy(s.body,"jupiter");
    assert(!navsight_paper(&s,&p,err,sizeof err));
    for(int k=0;k<3;k++) assert(p.rows[0].events.count[k]>=1);
    strcpy(s.body,"aries");
    assert(!navsight_paper(&s,&p,err,sizeof err));
    assert(!strcmp(p.rows[0].name,"Sun") && p.rows[0].events.count[1]==1);
    /* A valid lookup at the ephemeris boundary must still work if its
     * full local day falls outside coverage. */
    s.y=1849; s.mo=12; s.d=26; s.h=1; s.dr_lon=179;
    strcpy(s.body,"moon");
    if(nav_engine_covers(navsight_jd(&s))){
        assert(!navsight_paper(&s,&p,err,sizeof err));
        assert(!p.rows[0].events.available);
    }
    {
        NavDailyEvents e={0}; char text[40];
        e.available=1; e.count[1]=2; e.hours[1][0]=0.01; e.hours[1][1]=23.999;
        assert(!strcmp(navsight_fmt_event(&e,1,text,sizeof text),"00:01 / 24:00"));
    }
    nav_engine_shutdown();
    puts("Paper almanac checks passed.");
    return 0;
}
