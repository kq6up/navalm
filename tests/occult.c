/* SPDX-License-Identifier: MIT — science, boundary, cancellation and clock checks. */
#include "navoccult.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static int cancel(double f,void *p){(*(int*)p)++;return f>.1;}
static double sec(double a,double b){return (a-b)*86400;}
int main(void){
    char e[512];NavOccultRequest q;navoccult_init(&q);nav_engine_select("precise");assert(!nav_engine_init(e,sizeof e));
    assert(navoccult_star_count()==8881);int star=navoccult_star_find("Gem Kappa-77");assert(star>=0);
    assert(star==navoccult_star_find("HD 62345"));assert(navoccult_star_find("not a star")==-1);assert(!navoccult_star(99999));
    q.start_jd=nav_julian_date(2026,3,27,0,0,0);q.days=1;q.lat=39.8;q.lon=-105;q.max_magnitude=4;q.night_only=0;
    NavOccultResult *r=calloc(1,sizeof *r);assert(r);assert(!navoccult_search(&q,r,NULL,NULL,e,sizeof e));assert(r->count==1);
    NavOccultEvent event=r->events[0];assert(event.star==star&&event.has_contacts&&!event.grazing&&event.double_star);
    /* Independent published check: IOTA / RASC 2026 lunar26.pdf, Denver,
     * kappa Gem disappearance 2026-03-27 04:05.3 UT, rounded to 0.1 minute.
     * Mean spherical limb and zero DUT1: allow 5 s for rounding/limb/catalog. */
    assert(fabs(sec(event.contact_jd[0],nav_julian_date(2026,3,27,4,5,18)))<5);
    assert(event.rate_arcsec_sec[0]<0&&event.rate_arcsec_sec[1]>0);
    assert(!event.bright_limb[0]&&event.bright_limb[1]);assert(event.visible[0]&&event.visible[1]);
    NavOccultEvent refined;assert(!navoccult_event(&q,star,event.closest_jd+900./86400,0,&refined,e,sizeof e));
    assert(fabs(sec(refined.contact_jd[0],event.contact_jd[0]))<.002);
    NavOccultObservation o={event.contact_jd[0]-60./86400+.25/86400,.25,0,.2,2,.1,0};NavOccultRecovery recovered;
    assert(!navoccult_recover(&q,&event,&o,&recovered,e,sizeof e));assert(fabs(recovered.correction_sec-60)<.002);
    assert(recovered.sigma_time_sec>9&&recovered.sigma_time_sec<10&&recovered.weak);
    assert(fabs(recovered.longitude_sigma_nm-recovered.sigma_time_sec*.25*cos(q.lat*.0174532925199433))<1e-9);
    double sigma=recovered.sigma_time_sec;o.position_sigma_nm=2;assert(!navoccult_recover(&q,&event,&o,&recovered,e,sizeof e));assert(recovered.sigma_time_sec>sigma);
    o.position_sigma_nm=.1;o.contact=1;o.reaction_sec=0;o.watch_jd=event.contact_jd[1]+35./86400;
    assert(!navoccult_recover(&q,&event,&o,&recovered,e,sizeof e));assert(fabs(recovered.correction_sec+35)<.002);
    o.limb_offset_arcsec=1;assert(!navoccult_recover(&q,&event,&o,&recovered,e,sizeof e));assert(recovered.utc_jd>event.contact_jd[1]);
    q.dut1_sec=.6;assert(!navoccult_event(&q,star,event.closest_jd,0,&refined,e,sizeof e));assert(fabs(sec(refined.contact_jd[0],event.contact_jd[0]))>.001);q.dut1_sec=0;
    /* Contact outside range retained, but never marked as selectable. */
    q.start_jd=event.contact_jd[0]+30./86400;q.days=.1;
    assert(!navoccult_search(&q,r,NULL,NULL,e,sizeof e));assert(r->count==1&&!r->events[0].visible[0]&&r->events[0].visible[1]);
    q.start_jd=nav_julian_date(2026,11,26,0,0,0);q.days=7;q.lat=34.0216667;q.lon=-117.3133333;q.max_magnitude=6.5;q.night_only=1;
    assert(!navoccult_search(&q,r,NULL,NULL,e,sizeof e));assert(r->count>=5);
    int misses=0,ordinary=0;
    for(size_t i=0;i<r->count;i++){
        if(i)assert(r->events[i].closest_jd>r->events[i-1].closest_jd);
        if(!r->events[i].has_contacts){misses++;assert(r->events[i].clearance_arcsec>0);o.contact=0;o.limb_offset_arcsec=0;o.watch_jd=r->events[i].closest_jd;assert(navoccult_recover(&q,&r->events[i],&o,&recovered,e,sizeof e));assert(strstr(e,"No contact"));}
        if(r->events[i].has_contacts&&!r->events[i].double_star){ordinary++;o.limb_offset_arcsec=0;o.contact=r->events[i].visible[0]?0:1;o.watch_jd=r->events[i].contact_jd[o.contact]-15./86400;
            assert(!navoccult_recover(&q,&r->events[i],&o,&recovered,e,sizeof e));assert(!recovered.weak);assert(fabs(recovered.correction_sec-15)<.002);}
    }assert(misses&&ordinary);
    navoccult_sort(r,1);for(size_t i=1;i<r->count;i++)assert(navoccult_star(r->events[i-1].star)->magnitude<=navoccult_star(r->events[i].star)->magnitude);
    navoccult_sort(r,2);for(size_t i=1;i<r->count;i++)assert(r->events[i-1].impact<=r->events[i].impact);
    navoccult_sort(r,3);for(size_t i=1;i<r->count;i++)assert(fabs(r->events[i-1].impact-1)<=fabs(r->events[i].impact-1));
    /* Construct a near-tangent site and ensure the 10-minute screening still
     * discovers contacts only tens of seconds apart. */
    q.start_jd=nav_julian_date(2026,3,27,0,0,0);q.days=1;q.max_magnitude=4;q.night_only=0;q.min_alt_deg=0;q.lon=-105;
    double low=35,high=45;NavOccultEvent a,b;assert(!navoccult_event(&q,star,event.closest_jd,0,&a,e,sizeof e));
    q.lat=low;assert(!navoccult_event(&q,star,event.closest_jd,0,&a,e,sizeof e));q.lat=high;assert(!navoccult_event(&q,star,event.closest_jd,0,&b,e,sizeof e));
    /* Southern limit is bracketed by latitudes 35 and 45 here. */
    if(a.clearance_arcsec*b.clearance_arcsec<0){
        for(int i=0;i<24;i++){q.lat=(low+high)/2;assert(!navoccult_event(&q,star,event.closest_jd,0,&refined,e,sizeof e));if((refined.clearance_arcsec+.02>0)==(a.clearance_arcsec+.02>0))low=q.lat;else high=q.lat;}
        assert(!navoccult_event(&q,star,event.closest_jd,0,&refined,e,sizeof e));assert(refined.grazing&&refined.has_contacts);
        assert(sec(refined.contact_jd[1],refined.contact_jd[0])<120);assert(!navoccult_search(&q,r,NULL,NULL,e,sizeof e));assert(r->count>=1);
    }else assert(!"Expected a near-tangent bracket");
    int calls=0;assert(navoccult_search(&q,r,cancel,&calls,e,sizeof e));assert(calls&&r->count==0&&strstr(e,"cancelled"));
    q.days=NAN;assert(navoccult_validate(&q,e,sizeof e));q.days=32;assert(navoccult_validate(&q,e,sizeof e));
    navoccult_init(&q);q.dut1_sec=2;assert(navoccult_validate(&q,e,sizeof e));q.dut1_sec=0;q.start_jd=nav_julian_date(2400,1,1,0,0,0);assert(navoccult_validate(&q,e,sizeof e));
    navoccult_init(&q);nav_engine_select("classic");assert(navoccult_validate(&q,e,sizeof e));assert(strstr(e,"Precise"));
    char stamp[48];navoccult_stamp(nav_julian_date(2026,12,31,23,59,59.9998),stamp,sizeof stamp);assert(!strcmp(stamp,"2027-01-01T00:00:00.000Z"));
    free(r);nav_engine_shutdown();puts("Occultations: published Denver event, direct/near-miss/short graze, UTC signs, reaction, DUT1, uncertainty, sorting, boundaries and cancellation passed.");return 0;
}
