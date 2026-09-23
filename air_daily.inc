/* ------------------------------------------------- Air Almanac daily page
 *
 * Laid out like the printed Air Almanac daily pages: a GREENWICH A.M. page
 * covering 00:00 to 11:50 and a P.M. page covering 12:00 to 23:50, both at
 * ten-minute intervals, with Sun and Aries GHA to 0.1' and the planets and
 * Moon to 1'.  Declination degrees and the hemisphere letter are printed on
 * the hour and again whenever the degrees or hemisphere change; in between only the
 * minutes appear, and an unchanged value is left as a dot, as on the page.
 *
 * The printed pages carry three planet columns and omit any planet too close
 * to the Sun to observe; the same rule is applied here, by elongation.
 */

#define AIR_MINELONG 20.0     /* degrees: below this the almanac omits a planet */

static const char *air_month(int m){
    static const char *n[] = {"JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE","JULY",
                              "AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"};
    return (m>=1 && m<=12) ? n[m-1] : "";
}

static const char *air_weekday(double jd_noon){
    static const char *n[] = {"MONDAY","TUESDAY","WEDNESDAY","THURSDAY",
                              "FRIDAY","SATURDAY","SUNDAY"};
    int k = (int)floor(jd_noon + 0.5);
    return n[((k % 7) + 7) % 7];
}

static int air_day_of_year(int y,int mo,int d){
    double j0 = nav_julian_date(y,1,1,0,0,0.0);
    double j  = nav_julian_date(y,mo,d,0,0,0.0);
    return (int)floor(j - j0 + 1.5);
}

/* degrees and tenths of a minute, as "179 10.1" */
static void air_gha1(double x,char *b,size_t n){
    double a = fmod(x,360.0); int d; double m;
    if(a < 0) a += 360.0;
    d = (int)floor(a); m = (a-d)*60.0;
    if(m >= 59.95){ m = 0.0; d = (d+1)%360; }
    snprintf(b,n,"%d %04.1f",d,m);
}

/* degrees and whole minutes, as "347 32" */
static void air_gha0(double x,char *b,size_t n){
    double a = fmod(x,360.0); int d; double m;
    if(a < 0) a += 360.0;
    d = (int)floor(a); m = (a-d)*60.0;
    if(m >= 59.5){ m = 0.0; d = (d+1)%360; }
    snprintf(b,n,"%d %02.0f",d,m);
}

/* Declination with the printed page's suppression.  *prev_deg holds signed
 * (degrees + 1), so N0 and S0 differ; force prints the full form (hour rows). */
static void air_dec(double x,int tenths,int force,int *prev_deg,int *prev_min,
                    char *b,size_t n)
{
    double a = fabs(x);
    int deg = (int)floor(a);
    double m = (a-deg)*60.0;
    int mi, deg_key;
    char hemi = (x < 0) ? 'S' : 'N';

    if(tenths){ if(m >= 59.95){ m = 0.0; deg++; } }
    else      { if(m >= 59.5 ){ m = 0.0; deg++; } }
    mi = (int)(tenths ? (m*10.0 + 0.5) : (m + 0.5));

    deg_key = (x < 0 ? -1 : 1) * (deg + 1);
    if(force || deg_key != *prev_deg){
        if(tenths) snprintf(b,n,"%c%d&nbsp;%04.1f",hemi,deg,m);
        else       snprintf(b,n,"%c%d&nbsp;%02.0f",hemi,deg,m);
    } else if(mi != *prev_min){
        if(tenths) snprintf(b,n,"%04.1f",m);
        else       snprintf(b,n,"%02.0f",m);
    } else {
        snprintf(b,n,"&middot;");      /* unchanged, as the page marks it */
    }
    *prev_deg = deg_key;
    *prev_min = mi;
}

static size_t api_daily_air(const char *qs,char *out,size_t cap)
{
    Json j;
    int y=qs_int(qs,"y",2026), mo=qs_int(qs,"mo",1), d=qs_int(qs,"d",1);
    static const NavBody cand[]={NAV_VENUS,NAV_MARS,NAV_JUPITER,NAV_SATURN};
    NavBody shown[4];
    double shown_mag[4];
    int nshown=0, dropped=0, k, half;
    double jd_noon;

    j.p = out; j.n = 0; j.cap = cap;
    jd_noon = nav_julian_date(y,mo,d,12,0,0.0);

    /* the planets the printed page would carry */
    for(k=0;k<4;k++){
        NavAlmanac a;
        if(nav_almanac(cand[k],jd_noon,&a)) continue;
        if(a.elong_deg < AIR_MINELONG){ dropped++; continue; }   /* too close to the Sun */
        if(nshown < 4){ shown[nshown]=cand[k]; shown_mag[nshown]=a.magnitude; nshown++; }
    }

    for(half=0; half<2; half++){
        int prev_sun_d=-999, prev_sun_m=-999;
        int prev_moon_d=-999, prev_moon_m=-999;
        int prev_pl_d[4], prev_pl_m[4];
        int mins, start = half*720;

        for(k=0;k<4;k++){ prev_pl_d[k]=-999; prev_pl_m[k]=-999; }

        jput(&j,"<div class=\"airpage\">");
        jput(&j,"<div class=\"airtitle\">(DAY %03d) &nbsp; GREENWICH %s &nbsp; %04d %s %d (%s)</div>",
             air_day_of_year(y,mo,d), half ? "P. M." : "A. M.",
             y, air_month(mo), d, air_weekday(jd_noon));

        jput(&j,"<table class=\"air\"><thead>");
        jput(&j,"<tr><th rowspan=\"2\">UT</th><th colspan=\"2\">SUN</th><th>ARIES</th>");
        for(k=0;k<nshown;k++)
            jput(&j,"<th colspan=\"2\">%s<span class=\"mag\">%+.1f</span></th>",
                 nav_body_name(shown[k]),shown_mag[k]);
        jput(&j,"<th colspan=\"2\">MOON</th></tr>");
        jput(&j,"<tr><th>GHA</th><th>Dec.</th><th>GHA</th>");
        for(k=0;k<nshown;k++) jput(&j,"<th>GHA</th><th>Dec.</th>");
        jput(&j,"<th>GHA</th><th>Dec.</th></tr></thead><tbody>");

        for(mins = start; mins < start+720; mins += 10){
            int hh = mins/60, mm = mins%60, hour_row = (mm == 0);
            double jd = nav_julian_date(y,mo,d,hh,mm,0.0);
            NavAlmanac a;
            char g[40],dc[40];

            jput(&j,"<tr class=\"%s\">",hour_row ? "hr" : (mm==30 ? "half" : ""));
            if(hour_row) jput(&j,"<td class=\"ut\">%02d %02d</td>",hh,mm);
            else         jput(&j,"<td class=\"ut\">%02d</td>",mm);

            if(!nav_almanac(NAV_SUN,jd,&a)){
                air_gha1(a.gha_deg,g,sizeof g);
                air_dec(a.dec_deg,1,hour_row,&prev_sun_d,&prev_sun_m,dc,sizeof dc);
                jput(&j,"<td>%s</td><td>%s</td>",g,dc);
            } else jput(&j,"<td colspan=\"2\">&mdash;</td>");

            if(!nav_almanac(NAV_ARIES,jd,&a)){
                air_gha1(a.gha_deg,g,sizeof g);
                jput(&j,"<td>%s</td>",g);
            } else jput(&j,"<td>&mdash;</td>");

            for(k=0;k<nshown;k++){
                if(!nav_almanac(shown[k],jd,&a)){
                    air_gha0(a.gha_deg,g,sizeof g);
                    air_dec(a.dec_deg,0,hour_row,&prev_pl_d[k],&prev_pl_m[k],dc,sizeof dc);
                    jput(&j,"<td>%s</td><td>%s</td>",g,dc);
                } else jput(&j,"<td colspan=\"2\">&mdash;</td>");
            }

            if(!nav_almanac(NAV_MOON,jd,&a)){
                air_gha0(a.gha_deg,g,sizeof g);
                air_dec(a.dec_deg,0,hour_row,&prev_moon_d,&prev_moon_m,dc,sizeof dc);
                jput(&j,"<td>%s</td><td>%s</td>",g,dc);
            } else jput(&j,"<td colspan=\"2\">&mdash;</td>");

            jput(&j,"</tr>");
        }
        jput(&j,"</tbody></table>");
        if(dropped)
            jput(&j,"<div class=\"airnote\">THE REMAINING NAVIGATIONAL PLANET%s "
                    "TOO CLOSE TO THE SUN FOR OBSERVATION</div>",
                 dropped > 1 ? "S ARE" : " IS");
        if(nshown > 3)
            jput(&j,"<div class=\"airnote\">The printed pages carry three planet columns; "
                    "all four navigational planets qualify here and all four are shown.</div>");
        jput(&j,"<div class=\"airfoot\">Sun and Aries to 0.1&prime;, planets and Moon to 1&prime;. "
                "Declination degrees are repeated on the hour and whenever they change; "
                "a dot means unchanged.</div>");
        jput(&j,"</div>");
    }
    return j.n;
}
