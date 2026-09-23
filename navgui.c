/* navalm 2.1 - mono ncurses console interface (./navalm --gui)
 *
 * Numbered menu entries in the style of the TI-89, function keys to move
 * between screens, ESC to back out.  No colour: it reads the same on any
 * terminal and over ssh.
 *
 * Everything here is a front end over the same navsight_* functions the
 * command line uses, so the two stay in step by construction.
 */

#include "navsight.h"
#include "navrc.h"
#include "nav_engine.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

#define MAXLINE 160
#define MAXPANE 400

/* ------------------------------------------------------------ utilities */

/* truncating copy; keeps the compiler from worrying about snprintf("%s") */
static void copy_str(char *dst,size_t dn,const char *src){
    size_t n = strlen(src);
    if(n > dn-1) n = dn-1;
    memcpy(dst,src,n);
    dst[n] = 0;
}

static void bar(int row,const char *text){
    int x;
    move(row,0);
    for(x=0;x<COLS;x++) addch(' ');
    mvaddnstr(row,1,text,COLS-2);
}

static void title(const char *screen){
    char buf[MAXLINE];
    time_t t = time(NULL);
    struct tm *g = gmtime(&t);
    attron(A_REVERSE);
    snprintf(buf,sizeof buf,"NAVALM 2.8g  %-24s %04d-%02d-%02d %02d:%02d:%02d UTC",
             screen,g->tm_year+1900,g->tm_mon+1,g->tm_mday,g->tm_hour,g->tm_min,g->tm_sec);
    bar(0,buf);
    attroff(A_REVERSE);
}

static void footer(const char *keys){
    attron(A_REVERSE);
    bar(LINES-1,keys);
    attroff(A_REVERSE);
}

static void message(const char *msg){
    bar(LINES-2,msg);
}

static char gui_error[MAXLINE];

/* one-line prompt at the bottom; returns 0 if the user pressed ESC */
static int prompt(const char *label,char *buf,size_t n){
    char tmp[MAXLINE];
    int ok;
    echo();
    curs_set(1);
    snprintf(tmp,sizeof tmp,"%s",label);
    bar(LINES-2,tmp);
    move(LINES-2,(int)strlen(tmp)+2);
    ok = (getnstr(buf,(int)n-1) != ERR);
    noecho();
    curs_set(0);
    bar(LINES-2,"");
    if(!ok) return 0;
    return 1;
}

static int prompt_num(const char *label,double *v){
    char buf[64],*e;
    double x;
    if(!prompt(label,buf,sizeof buf)) return 0;
    if(!buf[0]) return 0;
    x = strtod(buf,&e);
    while(*e==' ') e++;
    if(*e){ return 0; }
    *v = x;
    return 1;
}

/* "DEG MIN" or "DEG" */
static int prompt_dm(const char *label,double *deg){
    char buf[64];
    double d=0,m=0;
    int k;
    if(!prompt(label,buf,sizeof buf)) return 0;
    if(!buf[0]) return 0;
    k = sscanf(buf,"%lf %lf",&d,&m);
    if(k < 1 || m < 0 || m >= 60) return 0;
    *deg = fabs(d) + m/60.0;
    return 1;
}

/* Tolerant position parser.  Accepts, in any mixture:
 *   34 2.5 N 117 21.9 W      34 2.5N 117 21.9W      N 34 2.5 W 117 21.9
 *   34.0415 N 117.3645 W        34.0415 -117.3645        34 02 29.5 N 117 21 52.2 W
 * Numbers are taken in order; hemisphere letters may sit anywhere.  Two
 * numbers mean decimal degrees, four mean degrees and minutes, six mean
 * degrees, minutes and seconds. */
static int parse_position_text(const char *s,double *lat,double *lon){
    double v[6];
    int nv = 0, ns = 0, ew = 0;
    const char *p = s;

    while(*p && nv < 6){
        if(isalpha((unsigned char)*p)){
            char c = (char)toupper((unsigned char)*p);
            if(c=='N') ns = +1;
            else if(c=='S') ns = -1;
            else if(c=='E') ew = +1;
            else if(c=='W') ew = -1;
            p++;
        } else if(isdigit((unsigned char)*p) || *p=='-' || *p=='+' || *p=='.'){
            char *e;
            double x = strtod(p,&e);
            if(e == p){ p++; continue; }
            v[nv++] = x;
            p = e;
        } else p++;
    }

    if(nv == 2){ *lat = v[0]; *lon = v[1]; }
    else if(nv == 4){
        if(v[1] < 0 || v[1] >= 60 || v[3] < 0 || v[3] >= 60) return 0;
        *lat = (v[0] < 0 ? -1 : 1) * (fabs(v[0]) + v[1]/60.0);
        *lon = (v[2] < 0 ? -1 : 1) * (fabs(v[2]) + v[3]/60.0);
    }
    else if(nv == 6){
        if(v[1]<0||v[1]>=60||v[2]<0||v[2]>=60||v[4]<0||v[4]>=60||v[5]<0||v[5]>=60) return 0;
        *lat = (v[0] < 0 ? -1 : 1) * (fabs(v[0]) + v[1]/60.0 + v[2]/3600.0);
        *lon = (v[3] < 0 ? -1 : 1) * (fabs(v[3]) + v[4]/60.0 + v[5]/3600.0);
    }
    else return 0;

    if(ns) *lat = ns * fabs(*lat);
    if(ew) *lon = ew * fabs(*lon);
    if(fabs(*lat) > 90.0 || fabs(*lon) > 180.0) return 0;
    return 1;
}

static int prompt_pos(const char *label,double *lat,double *lon){
    char buf[96];
    gui_error[0] = 0;
    if(!prompt(label,buf,sizeof buf)) return 0;
    if(!buf[0]) return 0;
    if(!parse_position_text(buf,lat,lon)){
        copy_str(gui_error,sizeof gui_error,
                 "could not read that position; try  34 2.5 N 117 21.9 W");
        return 0;
    }
    return 1;
}

/* Resolve a typed star name.  One match is taken; two to nine are offered
 * as a numbered list in the TI-89 style; more asks for another letter. */
static int pick_star(const char *query,char *out,size_t outn){
    int m[16];
    size_t nm = navsight_star_match(query,m,16);
    size_t k;
    int row,c;

    gui_error[0] = 0;
    if(nm == 0){
        snprintf(gui_error,sizeof gui_error,"no star matches \"%s\"",query);
        return 0;
    }
    if(nm == 1){
        copy_str(out,outn,nav_star_name((size_t)m[0]));
        return 1;
    }
    if(nm > 9){
        snprintf(gui_error,sizeof gui_error,
                 "\"%s\" matches %lu stars; type another letter",query,(unsigned long)nm);
        return 0;
    }

    row = LINES - 3 - (int)nm;
    if(row < 2) row = 2;
    for(k=0;k<nm;k++){
        int x;
        move(row+(int)k,2);
        for(x=2;x<COLS-2;x++) addch(' ');
        mvprintw(row+(int)k,4,"%lu  %s",(unsigned long)k+1,nav_star_name((size_t)m[k]));
    }
    message("select a star by number, ESC to cancel");
    refresh();

    c = getch();
    message("");
    if(c >= '1' && c <= '0'+(int)nm){
        copy_str(out,outn,nav_star_name((size_t)m[c-'1']));
        return 1;
    }
    return 0;
}

static int confirm(const char *question){
    int c;
    char buf[MAXLINE+16];
    snprintf(buf,sizeof buf,"%s  (y/n)",question);
    message(buf);
    refresh();
    c = getch();
    message("");
    return (c=='y' || c=='Y');
}

/* ------------------------------------------------- capture printed output */

typedef struct {
    char line[MAXPANE][MAXLINE];
    int  n, top;
} Pane;

static void pane_clear(Pane *p){ p->n = 0; p->top = 0; }

static void pane_capture(Pane *p,void (*fn)(FILE*,const Sight*,const Reduction*),
                         const Sight *si,const Reduction *r,int append)
{
    FILE *f = tmpfile();
    char buf[MAXLINE];
    if(!append) pane_clear(p);
    if(!f) return;
    fn(f,si,r);
    rewind(f);
    while(fgets(buf,sizeof buf,f) && p->n < MAXPANE){
        size_t k = strlen(buf);
        while(k && (buf[k-1]=='\n' || buf[k-1]=='\r')) buf[--k]=0;
        copy_str(p->line[p->n++],MAXLINE,buf);
    }
    fclose(f);
}

static void pane_add(Pane *p,const char *s){
    if(p->n < MAXPANE) copy_str(p->line[p->n++],MAXLINE,s);
}

static void pane_draw(const Pane *p,int y0,int x0,int h,int w){
    int i;
    for(i=0;i<h;i++){
        int row = p->top + i;
        move(y0+i,x0);
        {
            int x;
            for(x=0;x<w;x++) addch(' ');
        }
        if(row < p->n) mvaddnstr(y0+i,x0,p->line[row],w);
    }
    if(p->n > h){
        char tag[32];
        snprintf(tag,sizeof tag,"[%d-%d/%d]",p->top+1,
                 (p->top+h < p->n)?p->top+h:p->n,p->n);
        mvaddstr(y0+h-1,x0+w-(int)strlen(tag)-1,tag);
    }
}

static int pane_scroll(Pane *p,int c,int h){
    if(c == KEY_NPAGE){ p->top += h-1; }
    else if(c == KEY_PPAGE){ p->top -= h-1; }
    else if(c == KEY_DOWN){ p->top++; }
    else if(c == KEY_UP){ p->top--; }
    else return 0;
    if(p->top > p->n - h) p->top = p->n - h;
    if(p->top < 0) p->top = 0;
    return 1;
}

/* ---------------------------------------------------------- shared state */

static void utc_now(int *y,int *mo,int *d,int *h,int *mi,double *s){
    time_t t = time(NULL);
    struct tm *g = gmtime(&t);
    *y=g->tm_year+1900; *mo=g->tm_mon+1; *d=g->tm_mday;
    *h=g->tm_hour; *mi=g->tm_min; *s=g->tm_sec;
}

static const char *limb_name(int limb){
    return limb>0 ? "lower" : (limb<0 ? "upper" : "center");
}

static void sight_defaults_now(Sight *si){
    NavDefaults d;
    memset(si,0,sizeof *si);
    strcpy(si->body,"sun");
    navrc_load(&d);
    navrc_apply(&d,si);
    utc_now(&si->y,&si->mo,&si->d,&si->h,&si->mi,&si->s);
    si->s = floor(si->s);
}

/* body cycling: sun, moon, planets, then stars by name */
static const char *const gui_bodies[] =
    {"sun","moon","mercury","venus","mars","jupiter","saturn"};
#define NBODY ((int)(sizeof gui_bodies/sizeof gui_bodies[0]))

static void cycle_body(Sight *si){
    int i;
    if(!strcmp(si->body,"star")){ strcpy(si->body,"sun"); si->star[0]=0; return; }
    for(i=0;i<NBODY;i++)
        if(!strcmp(si->body,gui_bodies[i])){
            if(i+1 < NBODY) strcpy(si->body,gui_bodies[i+1]);
            else { strcpy(si->body,"star"); if(!si->star[0]) strcpy(si->star,"Vega"); }
            return;
        }
    strcpy(si->body,"sun");
}

/* ------------------------------------------------------------ sight entry */

static void edit_field(Sight *si,int key,Pane *msg){
    double v,lat,lon;
    char buf[96];
    (void)msg;
    switch(key){
        case '1':
            cycle_body(si);
            if(!strcmp(si->body,"star")){
                if(prompt("Star (name or first letters, e.g. pol):",buf,sizeof buf) && buf[0]){
                    char name[NAVSIGHT_NAME];
                    if(pick_star(buf,name,sizeof name)) copy_str(si->star,sizeof si->star,name);
                }
            }
            break;
        case '2':
            if(prompt("Date  YYYY MM DD:",buf,sizeof buf)){
                int y,mo,d;
                if(sscanf(buf,"%d %d %d",&y,&mo,&d)==3){ si->y=y; si->mo=mo; si->d=d; }
                else if(sscanf(buf,"%d-%d-%d",&y,&mo,&d)==3){ si->y=y; si->mo=mo; si->d=d; }
            }
            break;
        case '3':
            if(prompt("Time UTC  HH MM SS:",buf,sizeof buf)){
                int h,mi; double s=0;
                if(sscanf(buf,"%d %d %lf",&h,&mi,&s)>=2){ si->h=h; si->mi=mi; si->s=s; }
                else if(sscanf(buf,"%d:%d:%lf",&h,&mi,&s)>=2){ si->h=h; si->mi=mi; si->s=s; }
            }
            break;
        case '4':
            if(prompt_dm("Hs  DEG MIN:",&v)) si->hs_deg = v;
            break;
        case '5':
            si->limb = (si->limb==1) ? 0 : (si->limb==0 ? -1 : 1);
            break;
        case '6':
            if(prompt_num("Index error, arcminutes (negative = on arc):",&v)){
                si->ie_min = fabs(v);
                if(v < 0) si->ie_on_arc = 1;
            }
            break;
        case '7':
            if(si->artificial){
                si->artificial = 0;
                if(prompt_num("Height of eye (feet, or metres if set in Setup):",&v)) si->he = v;
            } else si->artificial = 1;
            break;
        case '8':
            if(prompt_num("Temperature, Celsius:",&v)) si->temp_c = v;
            if(prompt_num("Pressure, millibars:",&v)) si->press_mb = v;
            break;
        case '9':
            if(prompt_pos("DR  LATd LATm N|S LONd LONm E|W:",&lat,&lon)){
                si->dr_lat = lat; si->dr_lon = lon;
            }
            break;
        case '0':
            if(prompt("Label:",buf,sizeof buf)) copy_str(si->label,sizeof si->label,buf);
            break;
        default: break;
    }
}

static void draw_sight_fields(const Sight *si,int y0){
    char b[64],c[64];
    mvprintw(y0+0,2," 1 Body        %s%s%s",
             (!strcmp(si->body,"star"))?"star ":si->body,
             (!strcmp(si->body,"star"))?si->star:"", "");
    mvprintw(y0+1,2," 2 Date        %04d-%02d-%02d",si->y,si->mo,si->d);
    mvprintw(y0+2,2," 3 Time UTC    %02d:%02d:%04.1f",si->h,si->mi,si->s);
    mvprintw(y0+3,2," 4 Hs          %s",
             si->hs_deg>0 ? navsight_fmt_lat(si->hs_deg,b,sizeof b)+2 : "-- --.-'");
    mvprintw(y0+4,2," 5 Limb        %s",limb_name(si->limb));
    mvprintw(y0+5,2," 6 Index error %.1f' %s arc",si->ie_min,si->ie_on_arc?"on":"off");
    if(si->artificial)
        mvprintw(y0+6,2," 7 Horizon     artificial (halve, no dip)");
    else
        mvprintw(y0+6,2," 7 Horizon     sea, HE %.1f %s",si->he,si->he_metres?"m":"ft");
    mvprintw(y0+7,2," 8 Temp/Press  %.1f C  %.1f mb",si->temp_c,si->press_mb);
    mvprintw(y0+8,2," 9 DR          %s %s",
             navsight_fmt_lat(si->dr_lat,b,sizeof b),navsight_fmt_lon(si->dr_lon,c,sizeof c));
    mvprintw(y0+9,2," 0 Label       %s",si->label[0]?si->label:"(none)");
}

static void screen_sight(Sight *carry)
{
    Sight si = *carry;
    NavDefaults d0;
    Pane p;
    int c,paneh,panew,panex,have_dr;
    char err[160],status[MAXLINE];

    status[0] = 0;

    navrc_load(&d0);
    have_dr = d0.have_dr || si.dr_lat != 0.0 || si.dr_lon != 0.0;
    pane_clear(&p);
    for(;;){
        Reduction r;
        int ok;

        erase();
        title("SIGHT   Hs -> Ho -> HO 229");
        draw_sight_fields(&si,2);

        panex = 40; panew = COLS-panex-1; paneh = LINES-5;
        if(panew < 24){ panex = 2; panew = COLS-4; paneh = LINES-16; }

        ok = (si.hs_deg > 0.0) && (navsight_reduce(&si,&r,err,sizeof err) == 0);
        pane_clear(&p);
        if(si.hs_deg <= 0.0) pane_add(&p,"enter Hs (key 4) to see the worked sight");
        else if(!ok) pane_add(&p,err);
        else {
            pane_capture(&p,navsight_print_correction,&si,&r,0);
            if(have_dr) pane_capture(&p,navsight_print_reduction,&si,&r,1);
            else {
                pane_add(&p,"");
                pane_add(&p,"set the DR (key 9) to reduce this sight to an LOP");
            }
        }
        pane_draw(&p,2,panex,paneh,panew);
        if(status[0]) message(status);

        footer("1-0 edit   N now   S save   D defaults   PgUp/PgDn scroll   ESC back");
        refresh();

        c = getch();
        if(c == 27 || c == KEY_F(10)) break;
        if(pane_scroll(&p,c,paneh)) continue;
        if(c >= '0' && c <= '9'){
            status[0] = 0;
            gui_error[0] = 0;
            edit_field(&si,c,&p);
            if(gui_error[0]) copy_str(status,sizeof status,gui_error);
            else if(c == '9') have_dr = 1;
            continue;
        }
        if(c == 'n' || c == 'N'){ utc_now(&si.y,&si.mo,&si.d,&si.h,&si.mi,&si.s); si.s=floor(si.s); continue; }
        if(c == 'd' || c == 'D'){
            NavDefaults d;
            navrc_load(&d);
            navrc_apply(&d,&si);
            copy_str(status,sizeof status,"setup defaults reloaded");
            continue;
        }
        if(c == 's' || c == 'S'){
            SightStore st;
            int id;
            if(si.hs_deg <= 0.0){ copy_str(status,sizeof status,"nothing to save: Hs is empty"); continue; }
            if(navsight_store_load(&st,err,sizeof err)){ copy_str(status,sizeof status,err); continue; }
            id = navsight_store_add(&st,&si);
            if(id <= 0 || navsight_store_save(&st,err,sizeof err))
                copy_str(status,sizeof status,"save failed");
            else {
                char m[MAXLINE+64];
                snprintf(m,sizeof m,"saved as LOP %d in %s",id,navsight_store_path());
                copy_str(status,sizeof status,m);
            }
            navsight_store_free(&st);
        }
    }
    *carry = si;
}

/* ------------------------------------------------------------ LOP screen */

static void screen_lop_detail(const Sight *si)
{
    Pane p;
    Reduction r;
    char err[160];
    int c,h;

    pane_clear(&p);
    if(navsight_reduce(si,&r,err,sizeof err)) pane_add(&p,err);
    else {
        pane_capture(&p,navsight_print_correction,si,&r,0);
        pane_capture(&p,navsight_print_reduction,si,&r,1);
    }
    for(;;){
        erase();
        title("LOP DETAIL");
        h = LINES-4;
        pane_draw(&p,2,2,h,COLS-4);
        footer("PgUp/PgDn scroll   ESC back");
        refresh();
        c = getch();
        if(c == 27) break;
        pane_scroll(&p,c,h);
    }
}

static void screen_fix(SightStore *st,const int *marked)
{
    Sight sel[16];
    Reduction red[16];
    size_t used[16];
    FixResult fx;
    Pane p;
    char err[160],b1[48],b2[48],buf[MAXLINE+64];
    size_t n = 0,i;
    int c,h,have_fix = 0;

    pane_clear(&p);
    for(i=0;i<st->n && n<16;i++)
        if(marked[i]){
            Reduction r;
            if(navsight_reduce(&st->v[i],&r,err,sizeof err)){
                snprintf(buf,sizeof buf,"LOP %d: %s",st->v[i].id,err);
                pane_add(&p,buf);
                continue;
            }
            sel[n] = st->v[i]; red[n] = r; used[n] = i; n++;
        }

    if(n < 2) pane_add(&p,"mark at least two LOPs with SPACE, then press F");
    else if(navsight_fix(sel,red,n,0,0,0,0,0,0,0.0,&fx,err,sizeof err)) pane_add(&p,err);
    else {
        have_fix = 1;
        snprintf(buf,sizeof buf,"Fix at %04d-%02d-%02d %02d:%02d:%04.1f UTC from %lu LOPs",
                 fx.fy,fx.fmo,fx.fd,fx.fh,fx.fmi,fx.fs,(unsigned long)n);
        pane_add(&p,buf);
        pane_add(&p,"");
        for(i=0;i<n;i++){
            char adv[48];
            if(fx.advanced_nm[i] > 0.05)
                snprintf(adv,sizeof adv,"advanced %.1f NM %03.0f",fx.advanced_nm[i],fx.advanced_brg[i]);
            else
                snprintf(adv,sizeof adv,"not advanced");
            snprintf(buf,sizeof buf,"  LOP %-3d %-9s Zn %05.1f  a %5.1f %s  %-22s residual %+.1f NM",
                     sel[i].id,(!strcmp(sel[i].body,"star"))?sel[i].star:sel[i].body,
                     red[i].zn,fabs(red[i].intercept_nm),red[i].intercept_nm>=0?"T":"A",
                     adv,fx.residual_nm[i]);
            pane_add(&p,buf);
        }
        pane_add(&p,"");
        snprintf(buf,sizeof buf,"  Fix     %s  %s",
                 navsight_fmt_lat(fx.lat,b1,sizeof b1),navsight_fmt_lon(fx.lon,b2,sizeof b2));
        pane_add(&p,buf);
        snprintf(buf,sizeof buf,"  RMS     %.2f NM",fx.rms_nm);
        pane_add(&p,buf);
        if(fx.have_ellipse){
            snprintf(buf,sizeof buf,"  1-sigma ellipse  %.2f x %.2f NM, major axis %03.0f",
                     fx.semi_major_nm,fx.semi_minor_nm,fx.ellipse_brg);
            pane_add(&p,buf);
        } else
            pane_add(&p,"  (two LOPs: intersection only, no redundancy to estimate error)");
    }

    if(have_fix){
        pane_add(&p,"");
        pane_add(&p,"  U  use this fix as the DR in Setup (for the next sights)");
        pane_add(&p,"  L  use it as the DR of these LOPs and re-reduce them");
    }

    for(;;){
        erase();
        title("FIX");
        h = LINES-4;
        pane_draw(&p,2,2,h,COLS-4);
        footer(have_fix ? "U set Setup DR   L re-DR these LOPs   PgUp/PgDn scroll   ESC back"
                        : "PgUp/PgDn scroll   ESC back");
        refresh();
        c = getch();
        if(c == 27) break;
        if(have_fix && (c=='u' || c=='U')){
            NavDefaults d;
            navrc_load(&d);
            d.dr_lat = fx.lat; d.dr_lon = fx.lon; d.have_dr = 1;
            if(navrc_save(&d,err,sizeof err)) message(err);
            else {
                snprintf(buf,sizeof buf,"Setup DR is now %s %s",
                         navsight_fmt_lat(fx.lat,b1,sizeof b1),navsight_fmt_lon(fx.lon,b2,sizeof b2));
                message(buf);
            }
            refresh(); getch();
            continue;
        }
        if(have_fix && (c=='l' || c=='L')){
            if(confirm("set the DR of these LOPs to the fix and re-reduce?")){
                for(i=0;i<n;i++){
                    st->v[used[i]].dr_lat = fx.lat;
                    st->v[used[i]].dr_lon = fx.lon;
                }
                if(navsight_store_save(st,err,sizeof err)) message(err);
                else message("LOPs re-DRed; press ESC and F again to see the new fix");
                refresh(); getch();
            }
            continue;
        }
        pane_scroll(&p,c,h);
    }
}

static void screen_lops(void)
{
    SightStore st;
    int marked[256];
    char err[160];
    int sel = 0, c;
    size_t i;

    memset(marked,0,sizeof marked);
    if(navsight_store_load(&st,err,sizeof err)) return;

    for(;;){
        int rows = LINES-6;
        erase();
        title("LOPs");
        mvprintw(1,2,"  #  body      date/time UTC            Ho          a       Zn    AP");
        if(!st.n) mvprintw(3,4,"no LOPs stored in %s",navsight_store_path());

        for(i=0;i<st.n && (int)i<rows;i++){
            Reduction r;
            char line[MAXLINE+96],ho[40],la[40],lo[40];
            if(navsight_reduce(&st.v[i],&r,err,sizeof err)){
                snprintf(line,sizeof line,"%c%c %2d  %-9s  %s",
                         marked[i]?'*':' ',(int)i==sel?'>':' ',st.v[i].id,st.v[i].body,err);
            } else {
                snprintf(line,sizeof line,"%c%c %2d  %-9s %04d-%02d-%02d %02d:%02d:%04.1f  %s %5.1f %s  %05.1f  %s %s",
                         marked[i]?'*':' ',(int)i==sel?'>':' ',
                         st.v[i].id,(!strcmp(st.v[i].body,"star"))?st.v[i].star:st.v[i].body,
                         st.v[i].y,st.v[i].mo,st.v[i].d,st.v[i].h,st.v[i].mi,st.v[i].s,
                         navsight_fmt_lat(r.ho,ho,sizeof ho)+2,
                         fabs(r.intercept_nm),r.intercept_nm>=0?"T":"A",r.zn,
                         navsight_fmt_lat(r.ap_lat,la,sizeof la),
                         navsight_fmt_lon(r.ap_lon,lo,sizeof lo));
            }
            mvaddnstr(2+(int)i,1,line,COLS-2);
            if(st.v[i].has_motion && (int)i < rows-1)
                mvprintw(2+(int)i,COLS-18,"%03.0f/%.0fkt",st.v[i].course,st.v[i].speed);
        }

        footer("up/dn move  SPACE mark  ENTER detail  A advance  D delete  F fix  ESC back");
        refresh();

        c = getch();
        if(c == 27) break;
        if(c == KEY_UP && sel > 0) sel--;
        else if(c == KEY_DOWN && sel+1 < (int)st.n) sel++;
        else if(c == ' ' && st.n) marked[sel] = !marked[sel];
        else if((c == '\n' || c == KEY_ENTER) && st.n) screen_lop_detail(&st.v[sel]);
        else if((c == 'f' || c == 'F') && st.n) screen_fix(&st,marked);
        else if((c == 'd' || c == 'D') && st.n){
            char q[MAXLINE];
            snprintf(q,sizeof q,"delete LOP %d?",st.v[sel].id);
            if(confirm(q)){
                navsight_store_delete(&st,st.v[sel].id);
                navsight_store_save(&st,err,sizeof err);
                memset(marked,0,sizeof marked);
                if(sel >= (int)st.n && sel) sel--;
            }
        }
        else if((c == 'a' || c == 'A') && st.n){
            double v;
            Sight *s = &st.v[sel];
            if(prompt_num("Course, degrees true (blank to clear motion):",&v)){
                s->course = v; s->has_motion = 1;
                if(prompt_num("Speed, knots:",&v)) s->speed = v;
                if(prompt_num("Set, degrees true (0 for none):",&v)) s->set = v;
                if(prompt_num("Drift, knots:",&v)) s->drift = v;
            } else {
                s->has_motion = 0; s->course = s->speed = s->set = s->drift = 0.0;
            }
            navsight_store_save(&st,err,sizeof err);
        }
    }
    navsight_store_free(&st);
}

/* -------------------------------------------------------- almanac screen */

static void screen_almanac(void)
{
    Sight q;
    Pane p;
    int c,h;

    sight_defaults_now(&q);
    pane_clear(&p);

    for(;;){
        NavAlmanac a;
        double jd;
        char buf[MAXLINE+64],b[40];

        erase();
        title("ALMANAC");
        mvprintw(2,2," 1 Body        %s%s",
                 (!strcmp(q.body,"star"))?"star ":q.body,(!strcmp(q.body,"star"))?q.star:"");
        mvprintw(3,2," 2 Date        %04d-%02d-%02d",q.y,q.mo,q.d);
        mvprintw(4,2," 3 Time UTC    %02d:%02d:%04.1f",q.h,q.mi,q.s);

        pane_clear(&p);
        jd = nav_julian_date(q.y,q.mo,q.d,q.h,q.mi,q.s);
        if(!strcmp(q.body,"star")){
            NavStarAlmanac s;
            int idx = nav_star_find(q.star);
            if(idx < 0 || nav_star_almanac((size_t)idx,jd,&s)) pane_add(&p,"star not found");
            else {
                snprintf(buf,sizeof buf,"  GHA  %s",navsight_fmt_gha(s.gha_deg,b,sizeof b)); pane_add(&p,buf);
                snprintf(buf,sizeof buf,"  SHA  %s",navsight_fmt_gha(s.sha_deg,b,sizeof b)); pane_add(&p,buf);
                snprintf(buf,sizeof buf,"  Dec  %s",navsight_fmt_lat(s.dec_deg,b,sizeof b)); pane_add(&p,buf);
                snprintf(buf,sizeof buf,"  Mag  %.2f",s.magnitude); pane_add(&p,buf);
            }
        } else {
            int bi = navsight_body_index(q.body);
            if(bi < 0 || nav_almanac((NavBody)bi,jd,&a)) pane_add(&p,"almanac failed");
            else {
                snprintf(buf,sizeof buf,"  GHA  %s",navsight_fmt_gha(a.gha_deg,b,sizeof b)); pane_add(&p,buf);
                snprintf(buf,sizeof buf,"  Dec  %s",navsight_fmt_lat(a.dec_deg,b,sizeof b)); pane_add(&p,buf);
                if(a.sd_deg != 0.0){ snprintf(buf,sizeof buf,"  SD   %.2f'",a.sd_deg*60.0); pane_add(&p,buf); }
                if(a.hp_deg != 0.0){ snprintf(buf,sizeof buf,"  HP   %.2f'",a.hp_deg*60.0); pane_add(&p,buf); }
                snprintf(buf,sizeof buf,"  JD   %.8f",jd); pane_add(&p,buf);
            }
        }
        h = LINES-9;
        pane_draw(&p,7,2,h,COLS-4);

        footer("1-3 edit   N now   PgUp/PgDn scroll   ESC back");
        refresh();

        c = getch();
        if(c == 27) break;
        if(pane_scroll(&p,c,h)) continue;
        if(c >= '1' && c <= '3') edit_field(&q,c,&p);
        else if(c == 'n' || c == 'N'){ utc_now(&q.y,&q.mo,&q.d,&q.h,&q.mi,&q.s); q.s = floor(q.s); }
    }
}

/* ---------------------------------------------------------- table screen */

extern int navalm_print_daily(int y,int mo,int d,int fmt);   /* in navalm.c */

static void screen_table(void)
{
    char buf[64];
    int y,mo,d,fmt=0,c;
    double s;
    int hh,mi;

    utc_now(&y,&mo,&d,&hh,&mi,&s);
    erase();
    title("DAILY TABLE");
    mvprintw(2,2,"The daily table is wider than this window, so it is written to the");
    mvprintw(3,2,"terminal outside the interface.");
    mvprintw(5,2,"Date (YYYY MM DD, blank = today):");
    footer("ENTER accept   ESC back");
    refresh();
    if(prompt("Date:",buf,sizeof buf) && buf[0]){
        int a,b2,cc;
        if(sscanf(buf,"%d %d %d",&a,&b2,&cc)==3 || sscanf(buf,"%d-%d-%d",&a,&b2,&cc)==3){
            y=a; mo=b2; d=cc;
        }
    }
    message("format: 1 text  2 HTML  3 LaTeX");
    refresh();
    c = getch();
    if(c=='2') fmt=1; else if(c=='3') fmt=2;

    def_prog_mode();
    endwin();
    navalm_print_daily(y,mo,d,fmt);
    fputs("\n[press Enter to return to the interface]\n",stdout);
    fflush(stdout);
    while(getchar() != '\n'){ /* wait */ }
    reset_prog_mode();
    refresh();
}

/* ---------------------------------------------------------- setup screen */

static void screen_setup(void)
{
    NavDefaults d;
    Sight si;
    char err[160];
    int c;

    navrc_load(&d);
    memset(&si,0,sizeof si);
    strcpy(si.body,"sun");
    navrc_apply(&d,&si);

    for(;;){
        char b[48],c2[48];
        erase();
        title("SETUP   defaults for new sights");
        mvprintw(2,2," 5 Limb        %s",limb_name(si.limb));
        mvprintw(3,2," 6 Index error %.1f' %s arc",si.ie_min,si.ie_on_arc?"on":"off");
        if(si.artificial) mvprintw(4,2," 7 Horizon     artificial (halve, no dip)");
        else              mvprintw(4,2," 7 Horizon     sea, HE %.1f %s",si.he,si.he_metres?"m":"ft");
        mvprintw(5,2," 8 Temp/Press  %.1f C  %.1f mb",si.temp_c,si.press_mb);
        mvprintw(6,2," 9 DR          %s %s",
                 navsight_fmt_lat(si.dr_lat,b,sizeof b),navsight_fmt_lon(si.dr_lon,c2,sizeof c2));
        if(si.has_motion)
            mvprintw(7,2," M Motion      course %05.1f  %.1f kt   set %05.1f  drift %.1f kt",
                     si.course,si.speed,si.set,si.drift);
        else
            mvprintw(7,2," M Motion      none (stationary)");
        mvprintw(9,2,"U  toggle height-of-eye units (now %s)",si.he_metres?"metres":"feet");
        mvprintw(11,2,"Saved in %s",navrc_path());
        mvprintw(12,2,"The LOP store stays a separate file, so both remain readable by 2.0.");

        footer("5-9 edit   M motion   U units   S save   ESC back without saving");
        refresh();

        c = getch();
        if(c == 27) break;
        if(c >= '5' && c <= '9'){
            gui_error[0] = 0;
            edit_field(&si,c,NULL);
            if(gui_error[0]){ message(gui_error); refresh(); getch(); }
            continue;
        }
        if(c == 'u' || c == 'U'){ si.he_metres = !si.he_metres; continue; }
        if(c == 'm' || c == 'M'){
            double v;
            if(prompt_num("Course, degrees true (blank = stationary):",&v)){
                si.course = v; si.has_motion = 1;
                if(prompt_num("Speed, knots:",&v)) si.speed = v;
                if(prompt_num("Set, degrees true:",&v)) si.set = v;
                if(prompt_num("Drift, knots:",&v)) si.drift = v;
            } else {
                si.has_motion = 0; si.course = si.speed = si.set = si.drift = 0.0;
            }
            continue;
        }
        if(c == 's' || c == 'S'){
            navrc_from_sight(&d,&si);
            d.have_motion = si.has_motion;
            if(navrc_save(&d,err,sizeof err)) message(err);
            else message("defaults saved");
            refresh();
            getch();
            break;
        }
    }
}

/* ------------------------------------------------------------- main menu */

static void screen_menu(void)
{
    Sight carry;
    int c;

    sight_defaults_now(&carry);

    for(;;){
        erase();
        title("MAIN MENU");
        mvprintw(3,4," 1  Almanac          body, date, time -> GHA / Dec / SD / HP");
        mvprintw(4,4," 2  New sight        Hs -> Ho, HO 229 reduction, save as an LOP");
        mvprintw(5,4," 3  LOPs             list, detail, advance, delete, mark for a fix");
        mvprintw(6,4," 4  Fix              cross the marked LOPs (from the LOPs screen)");
        mvprintw(7,4," 5  Daily table      the -t / -th / -tx pages");
        mvprintw(8,4," 6  Setup            defaults: IE, horizon, T/P, DR, motion");
        mvprintw(10,4,"F1 Almanac   F2 Sight   F3 LOPs   F5 Table   F6 Setup   F10 Quit");
        mvprintw(12,4,"LOP store   %s",navsight_store_path());
        mvprintw(13,4,"Defaults    %s",navrc_path());
        footer("select a number, or use the function keys                    F10/Q quit");
        refresh();

        c = getch();
        switch(c){
            case '1': case KEY_F(1): screen_almanac(); break;
            case '2': case KEY_F(2): screen_sight(&carry); break;
            case '3': case KEY_F(3): screen_lops(); break;
            case '4': case KEY_F(4): screen_lops(); break;
            case '5': case KEY_F(5): screen_table(); break;
            case '6': case KEY_F(6): screen_setup(); break;
            case 'q': case 'Q': case KEY_F(10): return;
            default: break;
        }
    }
}

int navgui_run(void)
{
    initscr();
#ifdef NCURSES_VERSION
    set_escdelay(100);          /* give arrow/function key sequences time to arrive */
#endif
    cbreak();
    noecho();
    keypad(stdscr,TRUE);
    curs_set(0);
    if(LINES < 20 || COLS < 72){
        endwin();
        fprintf(stderr,"navalm --gui needs a terminal of at least 72x20\n");
        return 1;
    }
    screen_menu();
    endwin();
    return 0;
}
