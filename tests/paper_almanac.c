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
    nav_engine_shutdown();
    puts("Paper almanac checks passed.");
    return 0;
}
