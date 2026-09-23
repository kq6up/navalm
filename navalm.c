/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
#include "nav_engine.h"
#include "nav_notice.h"
#include "navsight.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NAVALM_VERSION "3.3c"

static double rad(double x){ return x*M_PI/180.0; }
static double deg(double x){ return x*180.0/M_PI; }
static double norm360(double x){ x=fmod(x,360.0); return x<0?x+360.0:x; }

static void dm(double x,char *buf,size_t n){
    double a=fabs(x); int d=(int)floor(a); double m=(a-d)*60;
    snprintf(buf,n,"%c%03d %05.2f'",x<0?'-':'+',d,m);
}
static void latdm(double x,char *buf,size_t n){
    double a=fabs(x); int d=(int)floor(a); double m=(a-d)*60;
    snprintf(buf,n,"%c %02d %05.2f'",x<0?'S':'N',d,m);
}
static void londm(double x,char *buf,size_t n){
    double a=fabs(x); int d=(int)floor(a); double m=(a-d)*60;
    snprintf(buf,n,"%c %03d %05.2f'",x<0?'W':'E',d,m);
}
static int body(const char*s){
    const char*n[]={"sun","moon","mercury","venus","mars","jupiter","saturn","aries"};
    for(int i=0;i<8;i++) if(!strcmp(s,n[i])) return i;
    return -1;
}

/* Parse AP as: --ap LAT_DEG LAT_MIN N|S LON_DEG LON_MIN E|W
   Example: --ap 34 2.5 N 117 21.9 W */
static int parse_ap(int argc,char **argv,int start,double *lat,double *lon){
    if(start+6>=argc || strcmp(argv[start],"--ap")) return 0;
    char *e;
    double ld=strtod(argv[start+1],&e); if(*e) return -1;
    double lm=strtod(argv[start+2],&e); if(*e || lm<0.0 || lm>=60.0) return -1;
    char ns=(char)toupper((unsigned char)argv[start+3][0]);
    if(argv[start+3][1] || (ns!='N'&&ns!='S')) return -1;
    double od=strtod(argv[start+4],&e); if(*e) return -1;
    double om=strtod(argv[start+5],&e); if(*e || om<0.0 || om>=60.0) return -1;
    char ew=(char)toupper((unsigned char)argv[start+6][0]);
    if(argv[start+6][1] || (ew!='E'&&ew!='W')) return -1;
    ld=fabs(ld)+lm/60.0; od=fabs(od)+om/60.0;
    if(ld>90.0 || od>180.0) return -1;
    *lat=(ns=='S')?-ld:ld;
    *lon=(ew=='W')?-od:od; /* east-positive internally */
    return 7;
}

static void print_ap_solution(double gha,double dec,double lat,double lon){
    /* east-positive longitude: LHA = GHA + longitude */
    double lha=norm360(gha+lon);
    double ph=rad(lat), de=rad(dec), H=rad(lha);
    double s=sin(ph)*sin(de)+cos(ph)*cos(de)*cos(H);
    if(s>1) s=1;
    if(s<-1) s=-1;
    double hc=deg(asin(s));
    /* Initial bearing from observer to body's GP, measured clockwise from true north. */
    double y=-sin(H)*cos(de);
    double x=cos(ph)*sin(de)-sin(ph)*cos(de)*cos(H);
    double zn=norm360(deg(atan2(y,x)));
    char la[32],lo[32],lh[32],hh[32],zz[32];
    latdm(lat,la,sizeof la); londm(lon,lo,sizeof lo);
    dm(lha,lh,sizeof lh); dm(hc,hh,sizeof hh); dm(zn,zz,sizeof zz);
    printf("AP  %s  %s\n",la,lo);
    printf("LHA %s\nHc  %s\nZn  %s\n",lh,hh,zz);
}


static int leap_year(int y){
    return (y%4==0 && (y%100!=0 || y%400==0));
}

static int valid_date(int y,int m,int d){
    static const int mdays[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    int n;
    if(m<1 || m>12 || d<1) return 0;
    n=mdays[m-1]+((m==2 && leap_year(y))?1:0);
    return d<=n;
}

static int parse_date_arg(const char *s,int *y,int *m,int *d){
    char tail;
    if(sscanf(s,"%d-%d-%d%c",y,m,d,&tail)!=3) return 0;
    return valid_date(*y,*m,*d);
}

static void fmt_gha(double x,char *buf,size_t n){
    double a=norm360(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m>=59.95){ m=0.0; d=(d+1)%360; }
    snprintf(buf,n,"%03d %04.1f",d,m);
}

static void fmt_dec(double x,char *buf,size_t n){
    double a=fabs(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m>=59.95){ m=0.0; d++; }
    snprintf(buf,n,"%c%02d %04.1f",x<0?'S':'N',d,m);
}

/* single-body lookups keep the second decimal; the printed-style daily
 * tables stay at tenths, as the almanac pages do */
static void fmt_gha2(double x,char *buf,size_t n){
    double a=norm360(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m>=59.995){ m=0.0; d=(d+1)%360; }
    snprintf(buf,n,"%03d %05.2f",d,m);
}

static void fmt_dec2(double x,char *buf,size_t n){
    double a=fabs(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m>=59.995){ m=0.0; d++; }
    snprintf(buf,n,"%c%02d %05.2f",x<0?'S':'N',d,m);
}

/* right ascension as degrees, minutes and seconds: "177 12 58.4" */
static void fmt_ra_dms(double x,char *buf,size_t n){
    double a=norm360(x); int d,m; double s;
    d=(int)floor(a);
    m=(int)floor((a-d)*60.0);
    s=((a-d)*60.0-m)*60.0;
    if(s>=59.95){ s=0.0; m++; }
    if(m>=60){ m=0; d=(d+1)%360; }
    snprintf(buf,n,"%03d %02d %04.1f",d,m,s);
}

static void fmt_arcmin(double x,char *buf,size_t n){
    snprintf(buf,n,"%5.1f",fabs(x)*60.0);
}

static int get_body_at(int y,int m,int d,int h,NavBody b,NavAlmanac *a){
    double jd=nav_julian_date(y,m,d,h,0,0.0);
    return nav_almanac(b,jd,a);
}

/* Nautical Almanac interpolation quantities.
   Planet v: actual hourly GHA increment minus 15 degrees, in arcminutes.
   Moon   v: actual hourly GHA increment minus 14 degrees 19.0 minutes,
             in arcminutes (the base Moon increment used by the increment tables).
   d: absolute hourly change of declination, in arcminutes. */
static int calc_vd(int y,int m,int d,int h,NavBody b,double base_inc_deg,
                   double *v_arcmin,double *d_arcmin){
    NavAlmanac a0,a1;
    double jd0=nav_julian_date(y,m,d,h,0,0.0);
    double jd1=jd0+1.0/24.0;
    if(nav_almanac(b,jd0,&a0) || nav_almanac(b,jd1,&a1)) return 1;
    *v_arcmin=(norm360(a1.gha_deg-a0.gha_deg)-base_inc_deg)*60.0;
    *d_arcmin=fabs(a1.dec_deg-a0.dec_deg)*60.0;
    return 0;
}

/* ------------------------------------------------------------------
   Daily table generation.

   The table data is gathered once into a DailyData structure and then
   handed to one of three renderers:

     TBL_ASCII  fixed-width console table (default, -t)
     TBL_HTML   standalone HTML document  (-th)
     TBL_TEX    standalone LaTeX document (-tx)

   The ASCII rendering is byte-for-byte the same as earlier releases.
   ------------------------------------------------------------------ */

typedef enum { TBL_ASCII=0, TBL_HTML=1, TBL_TEX=2 } TableFormat;

#define NPLANET 4
static const NavBody table_planets[NPLANET]={NAV_VENUS,NAV_MARS,NAV_JUPITER,NAV_SATURN};
static const char *const table_planet_names[NPLANET]={"VENUS","MARS","JUPITER","SATURN"};

typedef struct { int ok; double v; double d; } VDPair;

typedef struct {
    int hour;
    char aries[16];
    struct { int ok; char gha[16]; char dec[16]; } pl[NPLANET];
} PlanetRow;

typedef struct {
    int hour;
    char sun_gha[16], sun_dec[16], sun_sd[16];
    char moon_gha[16], moon_dec[16], moon_hp[16], moon_sd[16];
    int vd_ok; double moon_v, moon_d;
} SunMoonRow;

typedef struct { char name[32]; char sha[16]; char dec[16]; } StarRow;

typedef struct {
    int y,m,d;
    PlanetRow  prow[24];  int nprow;
    VDPair     pvd[NPLANET];
    SunMoonRow smrow[24]; int nsmrow;
    VDPair     sun_vd;
    StarRow   *stars;     size_t nstars;
} DailyData;

static void free_daily(DailyData *t){ free(t->stars); t->stars=NULL; t->nstars=0; }

static int collect_daily(int y,int m,int d,DailyData *t){
    int h,i;
    size_t s;
    double jd0;

    memset(t,0,sizeof *t);
    t->y=y; t->m=m; t->d=d;

    for(h=0;h<24;h++){
        NavAlmanac a;
        PlanetRow *r=&t->prow[t->nprow];
        if(get_body_at(y,m,d,h,NAV_ARIES,&a)) continue;
        r->hour=h;
        fmt_gha(a.gha_deg,r->aries,sizeof r->aries);
        for(i=0;i<NPLANET;i++){
            if(get_body_at(y,m,d,h,table_planets[i],&a)){
                r->pl[i].ok=0;
            }else{
                r->pl[i].ok=1;
                fmt_gha(a.gha_deg,r->pl[i].gha,sizeof r->pl[i].gha);
                fmt_dec(a.dec_deg,r->pl[i].dec,sizeof r->pl[i].dec);
            }
        }
        t->nprow++;
    }

    /* Like the printed Nautical Almanac, the slowly varying planet v and d
       interpolation quantities sit at the foot of the planet columns.  For a
       one-day table they are evaluated for the 12:00-13:00 UTC interval. */
    for(i=0;i<NPLANET;i++){
        double vv=0.0,dd=0.0;
        t->pvd[i].ok = calc_vd(y,m,d,12,table_planets[i],15.0,&vv,&dd)?0:1;
        t->pvd[i].v=vv; t->pvd[i].d=dd;
    }

    for(h=0;h<24;h++){
        NavAlmanac sun,mn;
        SunMoonRow *r=&t->smrow[t->nsmrow];
        double mv=0.0,mdelta=0.0;
        if(get_body_at(y,m,d,h,NAV_SUN,&sun) || get_body_at(y,m,d,h,NAV_MOON,&mn)) continue;
        r->hour=h;
        fmt_gha(sun.gha_deg,r->sun_gha,sizeof r->sun_gha);
        fmt_dec(sun.dec_deg,r->sun_dec,sizeof r->sun_dec);
        fmt_arcmin(sun.sd_deg,r->sun_sd,sizeof r->sun_sd);
        fmt_gha(mn.gha_deg,r->moon_gha,sizeof r->moon_gha);
        fmt_dec(mn.dec_deg,r->moon_dec,sizeof r->moon_dec);
        fmt_arcmin(mn.hp_deg,r->moon_hp,sizeof r->moon_hp);
        fmt_arcmin(mn.sd_deg,r->moon_sd,sizeof r->moon_sd);
        if(calc_vd(y,m,d,h,NAV_MOON,14.0+19.0/60.0,&mv,&mdelta)){
            r->vd_ok=0;
        }else{
            r->vd_ok=1; r->moon_v=mv; r->moon_d=mdelta;
        }
        t->nsmrow++;
    }

    {
        double sv=0.0,sdlt=0.0;
        t->sun_vd.ok = calc_vd(y,m,d,12,NAV_SUN,15.0,&sv,&sdlt)?0:1;
        t->sun_vd.v=sv; t->sun_vd.d=sdlt;
    }

    t->nstars=nav_star_count_get();
    if(t->nstars){
        t->stars=(StarRow*)calloc(t->nstars,sizeof *t->stars);
        if(!t->stars){ t->nstars=0; return 1; }
    }
    jd0=nav_julian_date(y,m,d,0,0,0.0);
    for(s=0;s<t->nstars;s++){
        NavStarAlmanac a;
        nav_star_almanac(s,jd0,&a);
        snprintf(t->stars[s].name,sizeof t->stars[s].name,"%s",a.name?a.name:"");
        fmt_gha(a.sha_deg,t->stars[s].sha,sizeof t->stars[s].sha);
        fmt_dec(a.dec_deg,t->stars[s].dec,sizeof t->stars[s].dec);
    }
    return 0;
}

/* ---------------- ASCII ---------------- */

static void render_ascii(const DailyData *t){
    static const char *rule_pl =
      "+-----+----------+--------------------+--------------------+--------------------+--------------------+\n";
    static const char *rule_sm =
      "+-----+----------------------------------+---------------------------------------------------------+\n";
    static const char *rule_st =
      "+----------------+----------+----------+   +----------------+----------+----------+\n";
    int i,r;
    size_t half,n;

    puts("Informational use only. No warranty. Do not rely on this for safety-critical decisions.");
    printf("NAVALM %s   DAILY NAUTICAL ALMANAC   %04d-%02d-%02d UTC\n",NAVALM_VERSION,t->y,t->m,t->d);
    printf("Values are apparent geocentric quantities at whole UTC hours.\n");
    printf("GHA/SHA and Dec are degrees and minutes; v/d/HP/SD are arcminutes.\n\n");

    fputs(rule_pl,stdout);
    printf("| UTC |  ARIES   |       VENUS        |        MARS        |      JUPITER       |       SATURN       |\n");
    printf("|  h  |   GHA    |   GHA       Dec    |   GHA       Dec    |   GHA       Dec    |   GHA       Dec    |\n");
    fputs(rule_pl,stdout);
    for(r=0;r<t->nprow;r++){
        const PlanetRow *p=&t->prow[r];
        printf("| %02d  | %8s |",p->hour,p->aries);
        for(i=0;i<NPLANET;i++){
            if(!p->pl[i].ok) printf(" %-18s |","ERR");
            else             printf(" %8s %9s |",p->pl[i].gha,p->pl[i].dec);
        }
        putchar('\n');
    }
    fputs(rule_pl,stdout);
    printf("|     |          |");
    for(i=0;i<NPLANET;i++){
        if(!t->pvd[i].ok) printf(" %-18s |","v ERR  d ERR");
        else              printf(" v %+5.1f'  d %4.1f'  |",t->pvd[i].v,t->pvd[i].d);
    }
    putchar('\n');
    fputs(rule_pl,stdout);
    putchar('\n');

    fputs(rule_sm,stdout);
    printf("| UTC |               SUN                |                          MOON                           |\n");
    printf("|  h  |   GHA       Dec       SD         |   GHA      v      Dec      d      HP      SD            |\n");
    fputs(rule_sm,stdout);
    for(r=0;r<t->nsmrow;r++){
        const SunMoonRow *s=&t->smrow[r];
        if(!s->vd_ok)
            printf("| %02d  | %8s %9s %5s'       | %8s   ERR  %9s   ERR  %5s' %5s'         |\n",
                   s->hour,s->sun_gha,s->sun_dec,s->sun_sd,s->moon_gha,s->moon_dec,s->moon_hp,s->moon_sd);
        else
            printf("| %02d  | %8s %9s %5s'       | %8s %5.1f' %9s %5.1f' %5s' %5s'         |\n",
                   s->hour,s->sun_gha,s->sun_dec,s->sun_sd,s->moon_gha,s->moon_v,s->moon_dec,s->moon_d,s->moon_hp,s->moon_sd);
    }
    fputs(rule_sm,stdout);
    printf("|     |                    d %4.1f'       |                                                         |\n",
           t->sun_vd.ok?t->sun_vd.d:0.0);
    fputs(rule_sm,stdout);
    putchar('\n');

    printf("NAVIGATION STARS AT 00:00 UTC\n");
    fputs(rule_st,stdout);
    printf("| Star           |   SHA    |   Dec    |   | Star           |   SHA    |   Dec    |\n");
    fputs(rule_st,stdout);
    n=t->nstars; half=(n+1)/2;
    for(r=0;(size_t)r<half;r++){
        size_t j=(size_t)r+half;
        const StarRow *a=&t->stars[r];
        printf("| %-14.14s | %8s | %8s |",a->name,a->sha,a->dec);
        if(j<n){
            const StarRow *b=&t->stars[j];
            printf("   | %-14.14s | %8s | %8s |",b->name,b->sha,b->dec);
        }
        putchar('\n');
    }
    fputs(rule_st,stdout);
}

/* ---------------- HTML ---------------- */

static void html_puts(FILE *out,const char *s){
    for(;*s;s++){
        switch(*s){
            case '&': fputs("&amp;",out); break;
            case '<': fputs("&lt;",out);  break;
            case '>': fputs("&gt;",out);  break;
            case '"': fputs("&quot;",out);break;
            default:  fputc(*s,out);
        }
    }
}

static void render_html(FILE *out,const DailyData *t){
    int i,r;
    size_t half,n;

    fprintf(out,"<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n");
    fprintf(out,"<title>NAVALM daily nautical almanac %04d-%02d-%02d UTC</title>\n",t->y,t->m,t->d);
    fprintf(out,
"<style>\n"
"body { font-family: Georgia, 'Times New Roman', serif; margin: 2em auto; max-width: 62em; color: #111; }\n"
"h1 { font-size: 1.3em; margin-bottom: 0.2em; }\n"
"h2 { font-size: 1.05em; margin-top: 1.8em; }\n"
"p.note { color: #444; font-size: 0.9em; margin: 0.2em 0; }\n"
"table { border-collapse: collapse; margin-top: 0.8em; font-family: 'DejaVu Sans Mono', Menlo, Consolas, monospace; font-size: 0.85em; }\n"
"th, td { border: 1px solid #999; padding: 2px 8px; text-align: right; white-space: nowrap; }\n"
"th { background: #eee; text-align: center; }\n"
"td.h, th.h { text-align: center; }\n"
"td.name { text-align: left; }\n"
"tbody tr:nth-child(even) { background: #f7f7f7; }\n"
"tfoot td { background: #eee; text-align: center; }\n"
"table.stars { display: inline-table; vertical-align: top; margin-right: 2em; }\n"
"</style>\n</head>\n<body>\n");

    fprintf(out,"<h1>NAVALM %s &mdash; daily nautical almanac &mdash; %04d-%02d-%02d UTC</h1>\n",
           NAVALM_VERSION,t->y,t->m,t->d);
    fprintf(out,"<p class=\"note\">Values are apparent geocentric quantities at whole UTC hours.</p>\n");
    fprintf(out,"<p class=\"note\">GHA/SHA and Dec are degrees and minutes; v/d/HP/SD are arcminutes.</p>\n");

    fprintf(out,"<p class=\"note\">Informational use only. No warranty. Do not rely on this for safety-critical decisions. See SAFETY.md.</p>\n");
    fprintf(out,"<h2>Aries and planets</h2>\n<table>\n<thead>\n<tr><th rowspan=\"2\">UTC h</th><th rowspan=\"2\">Aries GHA</th>");
    for(i=0;i<NPLANET;i++){
        fputs("<th colspan=\"2\">",out); html_puts(out,table_planet_names[i]); fputs("</th>",out);
    }
    fprintf(out,"</tr>\n<tr>");
    for(i=0;i<NPLANET;i++) fprintf(out,"<th>GHA</th><th>Dec</th>");
    fprintf(out,"</tr>\n</thead>\n<tbody>\n");
    for(r=0;r<t->nprow;r++){
        const PlanetRow *p=&t->prow[r];
        fprintf(out,"<tr><td class=\"h\">%02d</td><td>%s</td>",p->hour,p->aries);
        for(i=0;i<NPLANET;i++){
            if(!p->pl[i].ok) fprintf(out,"<td colspan=\"2\" class=\"h\">ERR</td>");
            else             fprintf(out,"<td>%s</td><td>%s</td>",p->pl[i].gha,p->pl[i].dec);
        }
        fprintf(out,"</tr>\n");
    }
    fprintf(out,"</tbody>\n<tfoot>\n<tr><td colspan=\"2\">&nbsp;</td>");
    for(i=0;i<NPLANET;i++){
        if(!t->pvd[i].ok) fprintf(out,"<td colspan=\"2\">v ERR &nbsp; d ERR</td>");
        else fprintf(out,"<td colspan=\"2\">v %+.1f&prime; &nbsp; d %.1f&prime;</td>",t->pvd[i].v,t->pvd[i].d);
    }
    fprintf(out,"</tr>\n</tfoot>\n</table>\n");

    fprintf(out,"<h2>Sun and Moon</h2>\n<table>\n<thead>\n"
           "<tr><th rowspan=\"2\">UTC h</th><th colspan=\"3\">SUN</th><th colspan=\"6\">MOON</th></tr>\n"
           "<tr><th>GHA</th><th>Dec</th><th>SD</th>"
           "<th>GHA</th><th>v</th><th>Dec</th><th>d</th><th>HP</th><th>SD</th></tr>\n"
           "</thead>\n<tbody>\n");
    for(r=0;r<t->nsmrow;r++){
        const SunMoonRow *s=&t->smrow[r];
        fprintf(out,"<tr><td class=\"h\">%02d</td><td>%s</td><td>%s</td><td>%s&prime;</td>",
               s->hour,s->sun_gha,s->sun_dec,s->sun_sd);
        if(!s->vd_ok)
            fprintf(out,"<td>%s</td><td>ERR</td><td>%s</td><td>ERR</td>",s->moon_gha,s->moon_dec);
        else
            fprintf(out,"<td>%s</td><td>%.1f&prime;</td><td>%s</td><td>%.1f&prime;</td>",
                   s->moon_gha,s->moon_v,s->moon_dec,s->moon_d);
        fprintf(out,"<td>%s&prime;</td><td>%s&prime;</td></tr>\n",s->moon_hp,s->moon_sd);
    }
    fprintf(out,"</tbody>\n<tfoot>\n<tr><td>&nbsp;</td><td colspan=\"3\">d %.1f&prime;</td>"
           "<td colspan=\"6\">&nbsp;</td></tr>\n</tfoot>\n</table>\n",
           t->sun_vd.ok?t->sun_vd.d:0.0);

    fprintf(out,"<h2>Navigation stars at 00:00 UTC</h2>\n");
    n=t->nstars; half=(n+1)/2;
    {
        size_t block;
        for(block=0;block<2;block++){
            size_t start=block?half:0, stop=block?n:half;
            if(start>=stop) continue;
            fprintf(out,"<table class=\"stars\">\n<thead>\n<tr><th>Star</th><th>SHA</th><th>Dec</th></tr>\n</thead>\n<tbody>\n");
            for(r=(int)start;(size_t)r<stop;r++){
                fprintf(out,"<tr><td class=\"name\">");
                html_puts(out,t->stars[r].name);
                fprintf(out,"</td><td>%s</td><td>%s</td></tr>\n",t->stars[r].sha,t->stars[r].dec);
            }
            fprintf(out,"</tbody>\n</table>\n");
        }
    }

    fprintf(out,"</body>\n</html>\n");
}

/* ---------------- LaTeX ---------------- */

static void tex_puts(const char *s){
    for(;*s;s++){
        switch(*s){
            case '&': case '%': case '$': case '#': case '_': case '{': case '}':
                putchar('\\'); putchar(*s); break;
            case '~':  fputs("\\textasciitilde{}",stdout); break;
            case '^':  fputs("\\textasciicircum{}",stdout); break;
            case '\\': fputs("\\textbackslash{}",stdout);   break;
            default:   putchar(*s);
        }
    }
}

/* Each of the three sections is boxed, then scaled with \navfit so that it
   fills the landscape page: the box is enlarged until it hits either the text
   width or the height still free below that page's heading, whichever binds
   first.  Aspect ratio is preserved, so the columns keep their proportions. */
static void render_tex(const DailyData *t){
    int i,r;
    size_t half,n;

    printf("%% Generated by navalm %s.  Compile with pdflatex.\n",NAVALM_VERSION);
    printf("\\documentclass[10pt]{article}\n"
           "\\usepackage[landscape,margin=1.5cm]{geometry}\n"
           "\\usepackage{times}\n"
           "\\usepackage{graphicx}\n"
           "\\newcommand{\\am}{\\ensuremath{^{\\prime}}}\n"
           "\\pagestyle{empty}\n"
           "\\setlength{\\tabcolsep}{4pt}\n"
           "\\setlength{\\parindent}{0pt}\n"
           "\\newsavebox{\\navbox}\n"
           "\\newlength{\\navavail}\n"
           "\\newlength{\\navheight}\n"
           "%% \\navfit{<free height>}{<content>}: scale content up to fill the\n"
           "%% page, limited by \\textwidth or by the free height, keeping aspect.\n"
           "\\newcommand{\\navfit}[2]{%%\n"
           "  \\sbox{\\navbox}{#2}%%\n"
           "  \\setlength{\\navavail}{#1}%%\n"
           "  \\setlength{\\navheight}{\\dimexpr\\ht\\navbox+\\dp\\navbox\\relax}%%\n"
           "  \\ifnum\\numexpr(\\number\\wd\\navbox/1000)*1000/(\\number\\textwidth/1000)\\relax>%%\n"
           "        \\numexpr(\\number\\navheight/1000)*1000/(\\number\\navavail/1000)\\relax\n"
           "    \\resizebox*{\\textwidth}{!}{\\usebox{\\navbox}}%%\n"
           "  \\else\n"
           "    \\resizebox*{!}{\\navavail}{\\usebox{\\navbox}}%%\n"
           "  \\fi\n"
           "}\n"
           "\\begin{document}\n\\ttfamily\\small\n");

    /* ---- page 1: Aries and planets ---- */
    printf("\\begin{center}\n\\rmfamily\\large\\textbf{NAVALM %s --- Daily Nautical Almanac --- %04d-%02d-%02d UTC}\\\\[2pt]\n",
           NAVALM_VERSION,t->y,t->m,t->d);
    printf("\\normalsize Values are apparent geocentric quantities at whole UTC hours.\\\\\n");
    printf("GHA/SHA and Dec are degrees and minutes; v/d/HP/SD are arcminutes.\\\\[6pt]\n");
    printf("\\small Informational use only. No warranty. No safety-critical reliance; see SAFETY.md.\\\\\n");
    printf("\\textbf{Aries and planets}\n\\end{center}\n\n");

    printf("\\begin{center}\n\\navfit{\\dimexpr\\textheight-4.2cm\\relax}{%%\n");
    printf("\\begin{tabular}{|c|c|rr|rr|rr|rr|}\n\\hline\n");
    printf("UTC & ARIES");
    for(i=0;i<NPLANET;i++){
        fputs(" & \\multicolumn{2}{c|}{",stdout); tex_puts(table_planet_names[i]); fputs("}",stdout);
    }
    printf(" \\\\\n h & GHA");
    for(i=0;i<NPLANET;i++) printf(" & GHA & Dec");
    printf(" \\\\\n\\hline\n");
    for(r=0;r<t->nprow;r++){
        const PlanetRow *p=&t->prow[r];
        printf("%02d & %s",p->hour,p->aries);
        for(i=0;i<NPLANET;i++){
            if(!p->pl[i].ok) printf(" & \\multicolumn{2}{c|}{ERR}");
            else             printf(" & %s & %s",p->pl[i].gha,p->pl[i].dec);
        }
        printf(" \\\\\n");
    }
    printf("\\hline\n & ");
    for(i=0;i<NPLANET;i++){
        if(!t->pvd[i].ok) printf(" & \\multicolumn{2}{c|}{v ERR \\ d ERR}");
        else printf(" & \\multicolumn{2}{c|}{v %+.1f\\am\\ \\ d %.1f\\am}",t->pvd[i].v,t->pvd[i].d);
    }
    printf(" \\\\\n\\hline\n\\end{tabular}}\n\\end{center}\n\\clearpage\n\n");

    /* ---- page 2: Sun and Moon ---- */
    printf("\\begin{center}\n\\rmfamily\\textbf{Sun and Moon --- %04d-%02d-%02d UTC}\n\\end{center}\n",
           t->y,t->m,t->d);
    printf("\\begin{center}\n\\navfit{\\dimexpr\\textheight-2.0cm\\relax}{%%\n");
    printf("\\begin{tabular}{|c|rrr|rrrrrr|}\n\\hline\n");
    printf("UTC & \\multicolumn{3}{c|}{SUN} & \\multicolumn{6}{c|}{MOON} \\\\\n");
    printf(" h & GHA & Dec & SD & GHA & v & Dec & d & HP & SD \\\\\n\\hline\n");
    for(r=0;r<t->nsmrow;r++){
        const SunMoonRow *s=&t->smrow[r];
        printf("%02d & %s & %s & %s\\am",s->hour,s->sun_gha,s->sun_dec,s->sun_sd);
        if(!s->vd_ok)
            printf(" & %s & ERR & %s & ERR",s->moon_gha,s->moon_dec);
        else
            printf(" & %s & %.1f\\am & %s & %.1f\\am",s->moon_gha,s->moon_v,s->moon_dec,s->moon_d);
        printf(" & %s\\am & %s\\am \\\\\n",s->moon_hp,s->moon_sd);
    }
    printf("\\hline\n & \\multicolumn{3}{c|}{d %.1f\\am} & \\multicolumn{6}{c|}{} \\\\\n",
           t->sun_vd.ok?t->sun_vd.d:0.0);
    printf("\\hline\n\\end{tabular}}\n\\end{center}\n\\clearpage\n\n");

    /* ---- page 3: navigation stars, two blocks side by side ---- */
    printf("\\begin{center}\n\\rmfamily\\textbf{Navigation stars at 00:00 UTC --- %04d-%02d-%02d}\n\\end{center}\n",
           t->y,t->m,t->d);
    printf("\\begin{center}\n\\navfit{\\dimexpr\\textheight-2.0cm\\relax}{%%\n\\ttfamily\\small\n");
    n=t->nstars; half=(n+1)/2;
    {
        size_t block;
        for(block=0;block<2;block++){
            size_t start=block?half:0, stop=block?n:half;
            if(start>=stop) continue;
            if(block) printf("\\hspace{1.5em}%%\n");
            printf("\\begin{tabular}[t]{|l|r|r|}\n\\hline\nStar & SHA & Dec \\\\\n\\hline\n");
            for(r=(int)start;(size_t)r<stop;r++){
                tex_puts(t->stars[r].name);
                printf(" & %s & %s \\\\\n",t->stars[r].sha,t->stars[r].dec);
            }
            printf("\\hline\n\\end{tabular}%%\n");
        }
    }
    printf("}\n\\end{center}\n\n\\end{document}\n");
}

int navalm_print_daily(int y,int m,int d,int fmt);   /* used by the GUI */

static int print_daily_table(int y,int m,int d,TableFormat fmt){
    DailyData t;
    if(collect_daily(y,m,d,&t)){
        free_daily(&t);
        fprintf(stderr,"out of memory building daily table\n");
        return 1;
    }
    switch(fmt){
        case TBL_HTML: render_html(stdout,&t); break;
        case TBL_TEX:  render_tex(&t);  break;
        default:       render_ascii(&t); break;
    }
    free_daily(&t);
    return 0;
}

int navalm_print_daily(int y,int m,int d,int fmt){
    return print_daily_table(y,m,d,(TableFormat)fmt);
}

int navalm_write_daily_html(FILE *out,int y,int m,int d){
    DailyData t;
    if(!out) return 1;
    if(collect_daily(y,m,d,&t)){ free_daily(&t); return 1; }
    render_html(out,&t);
    free_daily(&t);
    return 0;
}

static void usage(const char *p){
 fprintf(stderr,
  "Informational use only; no safety-critical reliance. Use implies acceptance\n"
  "of the risk and liability terms; see SAFETY.md or --notice. MIT: see LICENSE.\n"
  "usage:\n"
  "  %s BODY YYYY MM DD HH MM SS [--ap LATdeg LATmin N|S LONdeg LONmin E|W]\n"
  "  %s star NAME YYYY MM DD HH MM SS [--ap LATdeg LATmin N|S LONdeg LONmin E|W]\n"
  "  %s list-stars\n"
  "  %s -v\n"
  "  %s -t  YYYY-MM-DD | YYYY MM DD   plain-text daily table\n"
  "  %s -th YYYY-MM-DD | YYYY MM DD   same table as an HTML document\n"
  "  %s -tx YYYY-MM-DD | YYYY MM DD   same table as a LaTeX document\n"
  "\n"
  "  %s sight BODY|star NAME YYYY MM DD HH MM SS --hs DEG MIN [options]\n"
  "        options: --ie MIN [on|off]   --ah | --he VAL [ft|m]   --limb ll|ul|center\n"
  "                 --temp C | --tempf F   --press MB | --inhg IN\n"
  "                 --dr LATdeg LATmin N|S LONdeg LONmin E|W\n"
  "                 --course DEG --speed KT [--set DEG --drift KT]\n"
  "                 --label TEXT   --save   --brief\n"
  "  %s lop list | show ID | edit ID [options] | delete ID | clear --yes\n"
  "  %s lop advance ID --course DEG --speed KT [--set DEG --drift KT] | --clear\n"
  "  %s fix ID ID [ID...] [--at YYYY MM DD HH MM SS]\n"
  "        --update-dr     adopt the fix as the DR in ~/.navalmrc\n"
  "        --update-lops   adopt the fix as the DR of the LOPs used, and re-reduce\n"
  "  %s setup [same options]           show or change the defaults in ~/.navalmrc\n"
  "  %s --gui                          full-screen ncurses interface\n"
  "  %s -d [PORT] [--listen ADDR]      web interface (default 127.0.0.1:8080)\n"
  "examples:\n"
  "  %s moon 2026 9 1 13 36 47 --ap 34 2.5 N 117 21.9 W\n"
  "  %s star Sirius 2026 9 3 12 0 0\n"
  "  %s -th 2026-09-08 > almanac.html\n"
  "  %s -tx 2026-09-08 > almanac.tex && pdflatex almanac.tex\n"
  "\n"
  "  artificial-horizon Sun sight, lower limb, worked and saved:\n"
  "  %s sight sun 2026 9 19 21 38 05 --hs 94 37.2 --ah --limb ll \\\n"
  "        --ie 0 --temp 30 --press 1013 --dr 34 2.5 N 117 21.9 W \\\n"
  "        --label \"LAN\" --save\n"
  "\n"
  "  star sight over a sea horizon, one-line summary:\n"
  "  %s sight star Vega 2026 9 20 3 15 30 --hs 79 51.1 --he 10 ft \\\n"
  "        --ie 1.2 on --dr 34 2.5 N 117 21.9 W --brief\n"
  "\n"
  "  stored LOPs, editing and re-reducing:\n"
  "  %s lop list\n"
  "  %s lop show 2\n"
  "  %s lop edit 2 --hs 94 37.4 --temp 28\n"
  "  %s lop delete 3\n"
  "\n"
  "  running fix: advance LOP 1 at 6 kt on 270 with a 1.2 kt set to 090,\n"
  "  then cross it with two later sights\n"
  "  %s lop advance 1 --course 270 --speed 6 --set 90 --drift 1.2\n"
  "  %s fix 1 2 3\n"
  "  %s fix 1 2 3 --at 2026 9 20 3 19 0\n"
  "\n"
  "  the LOP store is ~/.naval.save, or $NAVALM_SAVE if that is set\n",
  p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p);
}

#ifdef NAVALM_GUI
int navgui_run(void);
#endif
int navweb_run(const char *addr,int port);

int main(int argc,char**argv){
 if(argc==2 && !strcmp(argv[1],"--notice")){ puts(navalm_safety_notice); return 0; }
 if(argc>=2 && (!strcmp(argv[1],"-d") || !strcmp(argv[1],"--daemon"))){
   const char *addr="127.0.0.1"; int port=8080,i;
   for(i=2;i<argc;i++){
     if(!strcmp(argv[i],"--listen") && i+1<argc) addr=argv[++i];
     else if(!strcmp(argv[i],"--port") && i+1<argc) port=atoi(argv[++i]);
     else if(argv[i][0]!='-') port=atoi(argv[i]);
   }
   if(port<1 || port>65535){ fprintf(stderr,"bad port\n"); return 2; }
   return navweb_run(addr,port);
 }
 if(argc==2 && !strcmp(argv[1],"--gui")){
#ifdef NAVALM_GUI
   return navgui_run();
#else
   fprintf(stderr,"this navalm was built without the ncurses interface (make gui)\n");
   return 2;
#endif
 }
 if(argc>=2 && (!strcmp(argv[1],"sight") || !strcmp(argv[1],"lop") || !strcmp(argv[1],"fix") || !strcmp(argv[1],"setup"))){
   int rc = navsight_command(argc,argv);
   if(rc >= 0) return rc;
 }
 if(argc==2 && (!strcmp(argv[1],"-v") || !strcmp(argv[1],"--version"))){
   printf("navalm version %s\n",NAVALM_VERSION);
   return 0;
 }
 if(argc>=2 && (!strcmp(argv[1],"-t") || !strcmp(argv[1],"-th") || !strcmp(argv[1],"-tx"))){
   int y=0,m=0,d=0,ok=0;
   TableFormat fmt = (argv[1][2]=='h') ? TBL_HTML : (argv[1][2]=='x') ? TBL_TEX : TBL_ASCII;
   if(argc==3) ok=parse_date_arg(argv[2],&y,&m,&d);
   else if(argc==5){ y=atoi(argv[2]); m=atoi(argv[3]); d=atoi(argv[4]); ok=valid_date(y,m,d); }
   if(!ok){ fprintf(stderr,"invalid date; use %s YYYY-MM-DD or %s YYYY MM DD\n",argv[1],argv[1]); return 2; }
   return print_daily_table(y,m,d,fmt);
 }
 if(argc==2 && !strcmp(argv[1],"list-stars")){
   for(size_t i=0;i<nav_star_count_get();i++) puts(nav_star_name(i));
   return 0;
 }
 if(argc>=2 && !strcmp(argv[1],"star")){
   if(argc!=9 && argc!=16){usage(argv[0]);return 2;}
   double alat=0,alon=0; int have_ap=0;
   if(argc==16){int n=parse_ap(argc,argv,9,&alat,&alon); if(n<0){fprintf(stderr,"invalid --ap; expected LATdeg LATmin N|S LONdeg LONmin E|W\n");return 2;} have_ap=1;}
   int mm[16]; size_t nm=navsight_star_match(argv[2],mm,16); int si;
   if(nm==0){fprintf(stderr,"unknown star: %s\n",argv[2]);return 2;}
   if(nm>1){size_t k;fprintf(stderr,"\"%s\" matches %lu stars:\n",argv[2],(unsigned long)nm);
     for(k=0;k<nm&&k<16;k++)fprintf(stderr,"  %lu  %s\n",(unsigned long)k+1,nav_star_name((size_t)mm[k]));
     fprintf(stderr,"use more letters\n");return 2;}
   si=mm[0];
   double jd=nav_julian_date(atoi(argv[3]),atoi(argv[4]),atoi(argv[5]),atoi(argv[6]),atoi(argv[7]),atof(argv[8]));
   NavStarAlmanac a; if(nav_star_almanac((size_t)si,jd,&a)) return 1;
   char g[32],d[32],sha[32]; fmt_gha2(a.gha_deg,g,sizeof g);fmt_dec2(a.dec_deg,d,sizeof d);fmt_gha2(a.sha_deg,sha,sizeof sha);
   if(atoi(argv[3])>=1972) {
     printf("%s  JD(UT)=%.8f  TT-UTC=%.3fs (DUT1=0 assumed)\n",a.name,jd,nav_delta_t_seconds(jd));
   } else {
     printf("%s  JD(UT)=%.8f  DeltaT=%.3fs\n",a.name,jd,nav_delta_t_seconds(jd));
   }
   { char ra[32]; fmt_ra_dms(a.ra_deg,ra,sizeof ra);
     printf("GHA %s\nSHA %s\nDec %s\nRA  %s\nMag %.2f\n",g,sha,d,ra,a.magnitude); }
   if(have_ap) print_ap_solution(a.gha_deg,a.dec_deg,alat,alon);
   return 0;
 }
 if(argc!=8 && argc!=15){usage(argv[0]);return 2;}
 double alat=0,alon=0; int have_ap=0;
 if(argc==15){int n=parse_ap(argc,argv,8,&alat,&alon); if(n<0){fprintf(stderr,"invalid --ap; expected LATdeg LATmin N|S LONdeg LONmin E|W\n");return 2;} have_ap=1;}
 int b=body(argv[1]);if(b<0){fprintf(stderr,"unknown body\n");return 2;}
 double jd=nav_julian_date(atoi(argv[2]),atoi(argv[3]),atoi(argv[4]),atoi(argv[5]),atoi(argv[6]),atof(argv[7]));
 NavAlmanac a;if(nav_almanac((NavBody)b,jd,&a))return 1;
 char g[32],d[32],hp[32],sd[32];fmt_gha2(a.gha_deg,g,sizeof g);fmt_dec2(a.dec_deg,d,sizeof d);fmt_arcmin(a.hp_deg,hp,sizeof hp);fmt_arcmin(a.sd_deg,sd,sizeof sd);
 if(atoi(argv[2])>=1972) {
   printf("%s  JD(UT)=%.8f  TT-UTC=%.3fs (DUT1=0 assumed)\n",nav_body_name((NavBody)b),jd,nav_delta_t_seconds(jd));
 } else {
   printf("%s  JD(UT)=%.8f  DeltaT=%.3fs\n",nav_body_name((NavBody)b),jd,nav_delta_t_seconds(jd));
 }
 printf("GHA %s\n",g);
 if(b!=NAV_ARIES){ char ra[32]; fmt_ra_dms(a.ra_deg,ra,sizeof ra);
   printf("Dec %s\nRA  %s\n",d,ra); }
 if(a.hp_deg != 0.0)printf("HP  %s'\n",hp);
 if(a.sd_deg != 0.0)printf("SD  %s'\n",sd);
 if(b!=NAV_ARIES)printf("Distance %.9g %s\n",a.distance,b==NAV_MOON?"km":"AU");
 if(have_ap){
   if(b==NAV_ARIES) fprintf(stderr,"note: --ap on Aries gives LHA only; Hc/Zn are undefined for Aries itself\n");
   else print_ap_solution(a.gha_deg,a.dec_deg,alat,alon);
 }
 return 0;
}
