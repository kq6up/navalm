/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* navalm 2.8g - ./navalm -d : a small embedded web interface.
 *
 * Single-threaded HTTP/1.1, no dependencies, no framework.  The server is
 * stateless: it reduces sights and solves fixes on request and keeps nothing.
 * The browser holds the sight list in its own localStorage, so the web
 * interface never touches ~/.naval.save or ~/.navalmrc.
 *
 * Endpoints
 *   GET /                    the page (embedded below)
 *   GET /api/version
 *   GET /api/stars?q=pol     fuzzy star match
 *   GET /api/almanac?...      GHA/Dec/HP/SD for a body and UTC
 *   GET /daily/naval?...      exact standalone NAVAL HTML daily page
 *   GET /api/daily?...        AIR 10-minute or compact NAVAL HTML table
 *   GET /api/reduce?...      one sight: Hs -> Ho, almanac, HO 229, intercept
 *   GET /api/fix?n=3&...     a fix from indexed sight parameters
 *
 * Network care: binds 127.0.0.1 unless --listen says otherwise, caps the
 * request size and the header count, and never shells out.
 */

#include "navsight.h"
#include "navrc.h"
#include "nav_engine.h"
#include "navweb_page.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define REQ_MAX     16384
#define HDR_MAX       64
#define JSON_MAX   32768
#define MAXFIX        16

extern int navalm_write_daily_html(FILE *out,int y,int m,int d);

/* ------------------------------------------------------------ query string */

static int hexval(int c){
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(const char *in,char *out,size_t n){
    size_t k = 0;
    while(*in && k+1 < n){
        if(*in == '%' && in[1] && in[2]){
            int a = hexval(in[1]), b = hexval(in[2]);
            if(a >= 0 && b >= 0){ out[k++] = (char)(a*16+b); in += 3; continue; }
        }
        if(*in == '+'){ out[k++] = ' '; in++; continue; }
        out[k++] = *in++;
    }
    out[k] = 0;
}

/* value of key in a query string; returns buf or NULL */
static const char *qs_get(const char *qs,const char *key,char *buf,size_t n){
    size_t klen = strlen(key);
    const char *p = qs;
    if(!qs) return NULL;
    while(*p){
        const char *amp, *eq;
        while(*p == '&') p++;
        if(!*p) break;
        amp = strchr(p,'&');
        eq  = strchr(p,'=');
        if(eq && (!amp || eq < amp) && (size_t)(eq-p) == klen && !strncmp(p,key,klen)){
            char raw[512];
            size_t vlen = amp ? (size_t)(amp-eq-1) : strlen(eq+1);
            if(vlen >= sizeof raw) vlen = sizeof raw - 1;
            memcpy(raw,eq+1,vlen);
            raw[vlen] = 0;
            url_decode(raw,buf,n);
            return buf;
        }
        if(!amp) break;
        p = amp + 1;
    }
    return NULL;
}

static double qs_num(const char *qs,const char *key,double dflt){
    char buf[64];
    if(!qs_get(qs,key,buf,sizeof buf) || !buf[0]) return dflt;
    return atof(buf);
}

static int qs_int(const char *qs,const char *key,int dflt){
    char buf[64];
    if(!qs_get(qs,key,buf,sizeof buf) || !buf[0]) return dflt;
    return atoi(buf);
}

static int qs_is(const char *qs,const char *key,const char *want){
    char buf[64];
    if(!qs_get(qs,key,buf,sizeof buf)) return 0;
    return !strcmp(buf,want);
}

/* indexed key, e.g. "hs3" */
static void copy_n(char *dst,size_t dn,const char *src){
    size_t k = strlen(src);
    if(k > dn-1) k = dn-1;
    memcpy(dst,src,k);
    dst[k] = 0;
}

static void ikey(char *dst,size_t n,const char *base,int i){
    if(i < 0) snprintf(dst,n,"%s",base);
    else      snprintf(dst,n,"%s%d",base,i);
}

/* ------------------------------------------------------------------ JSON */

typedef struct { char *p; size_t n, cap; } Json;

static void jput(Json *j,const char *fmt,...){
    va_list ap;
    int k;
    va_start(ap,fmt);
    if(j->n < j->cap) {
        k = vsnprintf(j->p + j->n, j->cap - j->n, fmt, ap);
        if(k > 0) j->n += (size_t)k;
        if(j->n > j->cap) j->n = j->cap;
    }
    va_end(ap);
}

static void jstr(Json *j,const char *key,const char *val){
    const char *p;
    jput(j,"\"%s\":\"",key);
    for(p = val ? val : ""; *p; p++){
        if(*p == '"' || *p == '\\') jput(j,"\\%c",*p);
        else if((unsigned char)*p < 0x20) jput(j,"\\u%04x",(unsigned)(unsigned char)*p);
        else jput(j,"%c",*p);
    }
    jput(j,"\",");
}

static void jnum(Json *j,const char *key,double v){
    if(v != v || v > 1e300 || v < -1e300) jput(j,"\"%s\":null,",key);
    else jput(j,"\"%s\":%.6f,",key,v);
}

static void jint(Json *j,const char *key,long v){ jput(j,"\"%s\":%ld,",key,v); }

static void jtrim(Json *j){          /* drop a trailing comma */
    if(j->n && j->p[j->n-1] == ',') j->n--;
}

/* ------------------------------------------------- build a sight from query */

static void web_sight(const char *qs,int i,Sight *si){
    char key[32],buf[64];

    memset(si,0,sizeof *si);
    strcpy(si->body,"sun");
    si->limb = 1;
    si->temp_c = 10.0;
    si->press_mb = 1010.0;

    ikey(key,sizeof key,"body",i);
    if(qs_get(qs,key,buf,sizeof buf) && buf[0]) copy_n(si->body,sizeof si->body,buf);
    ikey(key,sizeof key,"star",i);
    if(qs_get(qs,key,buf,sizeof buf) && buf[0]) copy_n(si->star,sizeof si->star,buf);

    ikey(key,sizeof key,"y",i);    si->y  = qs_int(qs,key,2026);
    ikey(key,sizeof key,"mo",i);   si->mo = qs_int(qs,key,1);
    ikey(key,sizeof key,"d",i);    si->d  = qs_int(qs,key,1);
    ikey(key,sizeof key,"h",i);    si->h  = qs_int(qs,key,0);
    ikey(key,sizeof key,"mi",i);   si->mi = qs_int(qs,key,0);
    ikey(key,sizeof key,"s",i);    si->s  = qs_num(qs,key,0.0);

    ikey(key,sizeof key,"hs",i);     si->hs_deg = qs_num(qs,key,0.0);
    ikey(key,sizeof key,"ie",i);     si->ie_min = qs_num(qs,key,0.0);
    ikey(key,sizeof key,"iearc",i);  si->ie_on_arc = qs_is(qs,key,"on");
    ikey(key,sizeof key,"horizon",i);si->artificial = qs_is(qs,key,"ah");
    ikey(key,sizeof key,"he",i);     si->he = qs_num(qs,key,0.0);
    ikey(key,sizeof key,"heunit",i); si->he_metres = qs_is(qs,key,"m");
    ikey(key,sizeof key,"limb",i);
    if(qs_get(qs,key,buf,sizeof buf)){
        si->limb = !strcmp(buf,"ll") ? 1 : (!strcmp(buf,"ul") ? -1 : 0);
    }
    ikey(key,sizeof key,"temp",i);   si->temp_c   = qs_num(qs,key,10.0);
    ikey(key,sizeof key,"press",i);  si->press_mb = qs_num(qs,key,1010.0);
    ikey(key,sizeof key,"drlat",i);  si->dr_lat   = qs_num(qs,key,0.0);
    ikey(key,sizeof key,"drlon",i);  si->dr_lon   = qs_num(qs,key,0.0);

    ikey(key,sizeof key,"course",i); si->course = qs_num(qs,key,0.0);
    ikey(key,sizeof key,"speed",i);  si->speed  = qs_num(qs,key,0.0);
    ikey(key,sizeof key,"set",i);    si->set    = qs_num(qs,key,0.0);
    ikey(key,sizeof key,"drift",i);  si->drift  = qs_num(qs,key,0.0);
    si->has_motion = (si->speed != 0.0 || si->drift != 0.0);

    ikey(key,sizeof key,"label",i);
    if(qs_get(qs,key,buf,sizeof buf)) copy_n(si->label,sizeof si->label,buf);
}

static void json_reduction(Json *j,const Sight *si,const Reduction *r){
    jstr(j,"body",si->body);
    jstr(j,"star",si->star);
    jint(j,"y",si->y); jint(j,"mo",si->mo); jint(j,"d",si->d);
    jint(j,"h",si->h); jint(j,"mi",si->mi); jnum(j,"s",si->s);
    jint(j,"artificial",si->artificial);
    jint(j,"limb",si->limb);
    jint(j,"ie_on_arc",si->ie_on_arc);
    jnum(j,"ie_min",si->ie_min);
    jnum(j,"he",si->he);
    jint(j,"he_metres",si->he_metres);
    jnum(j,"temp_c",si->temp_c);
    jnum(j,"press_mb",si->press_mb);

    jnum(j,"hs",r->hs);
    jnum(j,"ic_min",r->ic_min);
    jnum(j,"halved",r->halved);
    jnum(j,"dip_min",r->dip_min);
    jnum(j,"ha",r->ha);
    jnum(j,"r0_min",r->r0_min);
    jnum(j,"f",r->f);
    jnum(j,"r_min",r->r_min);
    jnum(j,"sd_min",r->sd_min);
    jnum(j,"sd_raw_min",r->sd_raw_min);
    jnum(j,"par_min",r->par_min);
    jnum(j,"hp_min",r->hp_min);
    jnum(j,"ho",r->ho);
    jint(j,"used_sd",r->used_sd);
    jint(j,"used_par",r->used_par);

    jint(j,"hour_h",r->hour_h);
    jnum(j,"frac_hour",r->frac_hour);
    jnum(j,"gha_hour",r->gha_hour);
    jnum(j,"inc_deg",r->inc_deg);
    jnum(j,"v_min",r->v_min);
    jnum(j,"vcorr_min",r->vcorr_min);
    jnum(j,"gha",r->gha);
    jnum(j,"dec_hour",r->dec_hour);
    jnum(j,"d_min",r->d_min);
    jnum(j,"dcorr_min",r->dcorr_min);
    jnum(j,"dec",r->dec);
    jnum(j,"sha_deg",r->sha_deg);
    jnum(j,"gha_aries_hour",r->gha_aries_hour);
    jnum(j,"gha_aries",r->gha_aries);
    jint(j,"is_star",r->is_star);
    jint(j,"has_v",r->has_v);
    jnum(j,"gha_exact",r->gha_exact);
    jnum(j,"dec_exact",r->dec_exact);

    jnum(j,"ap_lat",r->ap_lat);
    jnum(j,"ap_lon",r->ap_lon);
    jint(j,"lha",r->lha);
    jint(j,"same_name",r->same_name);
    jint(j,"dec_deg_whole",r->dec_deg_whole);
    jnum(j,"tab_hc_min",r->tab_hc_min);
    jnum(j,"d_tab_min",r->d_tab_min);
    jnum(j,"dec_inc_min",r->dec_inc_min);
    jnum(j,"dcorr229_min",r->dcorr229_min);
    jnum(j,"secdiff_min",r->secdiff_min);
    jnum(j,"hc_min",r->hc_min);
    jnum(j,"z",r->z);
    jnum(j,"zn",r->zn);
    jnum(j,"intercept_nm",r->intercept_nm);
}

/* ------------------------------------------------------------- endpoints */


static int web_body_id(const char *name,NavBody *b){
    static const char *names[]={"sun","moon","mercury","venus","mars","jupiter","saturn","aries"};
    int i;
    if(!name || !*name) name="sun";
    for(i=0;i<8;i++){
        if(!strcmp(name,names[i])){ *b=(NavBody)i; return 1; }
    }
    return 0;
}

static size_t api_almanac(const char *qs,char *out,size_t cap){
    Json j;
    char bname[64],starq[64],err[160];
    int y,mo,d,h,mi;
    double sec,jd;
    NavBody b;

    j.p=out; j.n=0; j.cap=cap;
    jput(&j,"{");

    if(!qs_get(qs,"body",bname,sizeof bname) || !bname[0]) strcpy(bname,"sun");
    y=qs_int(qs,"y",2026); mo=qs_int(qs,"mo",1); d=qs_int(qs,"d",1);
    h=qs_int(qs,"h",0); mi=qs_int(qs,"mi",0); sec=qs_num(qs,"s",0.0);
    jd=nav_julian_date(y,mo,d,h,mi,sec);

    if(!strcmp(bname,"star")){
        int m[16];
        size_t nm;
        NavStarAlmanac a;
        if(!qs_get(qs,"star",starq,sizeof starq) || !starq[0]){
            jstr(&j,"error","enter a star name");
            jtrim(&j); jput(&j,"}"); return j.n;
        }
        nm=navsight_star_match(starq,m,16);
        if(nm!=1){
            if(nm==0) snprintf(err,sizeof err,"no star matches \"%s\"",starq);
            else snprintf(err,sizeof err,"\"%s\" matches %lu stars; use more letters",
                          starq,(unsigned long)nm);
            jstr(&j,"error",err);
            jtrim(&j); jput(&j,"}"); return j.n;
        }
        if(nav_star_almanac((size_t)m[0],jd,&a)){
            jstr(&j,"error","could not compute star almanac");
            jtrim(&j); jput(&j,"}"); return j.n;
        }
        jint(&j,"ok",1); jint(&j,"is_star",1);
        jstr(&j,"body","star"); jstr(&j,"name",a.name);
        jint(&j,"y",y); jint(&j,"mo",mo); jint(&j,"d",d);
        jint(&j,"h",h); jint(&j,"mi",mi); jnum(&j,"s",sec);
        jnum(&j,"gha",a.gha_deg); jnum(&j,"sha",a.sha_deg); jnum(&j,"ra",a.ra_deg);
        jnum(&j,"dec",a.dec_deg); jnum(&j,"mag",a.magnitude);
        jtrim(&j); jput(&j,"}"); return j.n;
    }

    if(!web_body_id(bname,&b)){
        jstr(&j,"error","unknown body");
        jtrim(&j); jput(&j,"}"); return j.n;
    }else{
        NavAlmanac a;
        if(nav_almanac(b,jd,&a)){
            jstr(&j,"error","could not compute almanac");
            jtrim(&j); jput(&j,"}"); return j.n;
        }
        jint(&j,"ok",1); jint(&j,"is_star",0);
        jstr(&j,"body",bname); jstr(&j,"name",nav_body_name(b));
        jint(&j,"y",y); jint(&j,"mo",mo); jint(&j,"d",d);
        jint(&j,"h",h); jint(&j,"mi",mi); jnum(&j,"s",sec);
        jnum(&j,"gha",a.gha_deg); jnum(&j,"dec",a.dec_deg); jnum(&j,"ra",a.ra_deg);
        jnum(&j,"hp_min",a.hp_deg*60.0); jnum(&j,"sd_min",a.sd_deg*60.0);
        jtrim(&j); jput(&j,"}"); return j.n;
    }
}

static size_t api_reduce(const char *qs,char *out,size_t cap){
    Json j; Sight si; Reduction r; char err[160];
    j.p = out; j.n = 0; j.cap = cap;

    web_sight(qs,-1,&si);
    jput(&j,"{");
    if(si.hs_deg <= 0.0){
        jstr(&j,"error","no Hs given");
        jtrim(&j); jput(&j,"}");
        return j.n;
    }
    if(navsight_reduce(&si,&r,err,sizeof err)){
        jstr(&j,"error",err);
        jtrim(&j); jput(&j,"}");
        return j.n;
    }
    jint(&j,"ok",1);
    jint(&j,"has_dr",(si.dr_lat != 0.0 || si.dr_lon != 0.0));
    json_reduction(&j,&si,&r);
    jtrim(&j); jput(&j,"}");
    return j.n;
}

static size_t api_fix(const char *qs,char *out,size_t cap){
    Json j;
    Sight sel[MAXFIX];
    Reduction red[MAXFIX];
    FixResult fx;
    char err[160];
    int n = qs_int(qs,"n",0), i, k = 0;

    j.p = out; j.n = 0; j.cap = cap;
    jput(&j,"{");

    if(n < 2 || n > MAXFIX){
        jstr(&j,"error","a fix needs 2 to 16 sights");
        jtrim(&j); jput(&j,"}");
        return j.n;
    }
    for(i=0;i<n;i++){
        Sight si; Reduction r;
        web_sight(qs,i,&si);
        if(si.hs_deg <= 0.0 || navsight_reduce(&si,&r,err,sizeof err)) continue;
        sel[k] = si; red[k] = r; k++;
    }
    if(k < 2){
        jstr(&j,"error","fewer than two sights could be reduced");
        jtrim(&j); jput(&j,"}");
        return j.n;
    }
    if(navsight_fix(sel,red,(size_t)k,
                    qs_get(qs,"aty",err,sizeof err) ? 1 : 0,
                    qs_int(qs,"aty",0),qs_int(qs,"atmo",1),qs_int(qs,"atd",1),
                    qs_int(qs,"ath",0),qs_int(qs,"atmi",0),qs_num(qs,"ats",0.0),
                    &fx,err,sizeof err)){
        jstr(&j,"error",err);
        jtrim(&j); jput(&j,"}");
        return j.n;
    }

    jint(&j,"ok",1);
    jnum(&j,"lat",fx.lat);
    jnum(&j,"lon",fx.lon);
    jnum(&j,"rms_nm",fx.rms_nm);
    jint(&j,"have_ellipse",fx.have_ellipse);
    jnum(&j,"semi_major_nm",fx.semi_major_nm);
    jnum(&j,"semi_minor_nm",fx.semi_minor_nm);
    jnum(&j,"ellipse_brg",fx.ellipse_brg);
    jint(&j,"fy",fx.fy); jint(&j,"fmo",fx.fmo); jint(&j,"fd",fx.fd);
    jint(&j,"fh",fx.fh); jint(&j,"fmi",fx.fmi); jnum(&j,"fs",fx.fs);
    jput(&j,"\"lops\":[");
    for(i=0;i<k;i++){
        jput(&j,"{");
        jstr(&j,"name",(!strcmp(sel[i].body,"star")) ? sel[i].star : sel[i].body);
        jstr(&j,"label",sel[i].label);
        jnum(&j,"zn",red[i].zn);
        jnum(&j,"intercept_nm",red[i].intercept_nm);
        jnum(&j,"ho",red[i].ho);
        jnum(&j,"hc",red[i].hc_min/60.0);
        jnum(&j,"ap_lat",red[i].ap_lat);
        jnum(&j,"ap_lon",red[i].ap_lon);
        jnum(&j,"advanced_nm",fx.advanced_nm[i]);
        jnum(&j,"advanced_brg",fx.advanced_brg[i]);
        jnum(&j,"residual_nm",fx.residual_nm[i]);
        jtrim(&j);
        jput(&j,"},");
    }
    jtrim(&j);
    jput(&j,"],");
    jtrim(&j); jput(&j,"}");
    return j.n;
}

static size_t api_stars(const char *qs,char *out,size_t cap){
    Json j;
    char q[64];
    int m[16];
    size_t nm,i;

    j.p = out; j.n = 0; j.cap = cap;
    if(!qs_get(qs,"q",q,sizeof q)) q[0] = 0;
    nm = navsight_star_match(q,m,16);
    jput(&j,"{");
    jint(&j,"count",(long)nm);
    jput(&j,"\"matches\":[");
    for(i=0;i<nm && i<16;i++) jput(&j,"\"%s\",",nav_star_name((size_t)m[i]));
    jtrim(&j);
    jput(&j,"]}");
    return j.n;
}


/* ------------------------------------------------------------ daily almanac tables for web GUI */
static void web_dm(double x,char *buf,size_t n,int dec_places){
    double a=fabs(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m >= 59.9995){ d++; m=0.0; }
    if(dec_places==0) snprintf(buf,n,"%03d %02.0f'",d,m);
    else snprintf(buf,n,"%03d %04.1f'",d,m);
}
static void web_dec(double x,char *buf,size_t n,int dec_places){
    double a=fabs(x); int d=(int)floor(a); double m=(a-d)*60.0;
    if(m >= 59.9995){ d++; m=0.0; }
    if(dec_places==0) snprintf(buf,n,"%c%02d %02.0f'",x<0?'S':'N',d,m);
    else snprintf(buf,n,"%c%02d %04.1f'",x<0?'S':'N',d,m);
}

#include "air_daily.inc"

static size_t api_daily(const char *qs,char *out,size_t cap){
    Json j={out,0,cap};
    char sty[16];
    int y=qs_int(qs,"y",2026), mo=qs_int(qs,"mo",1), d=qs_int(qs,"d",1);
    int air=0, step, total, k;
    static const NavBody pb[]={NAV_VENUS,NAV_MARS,NAV_JUPITER,NAV_SATURN};
    if(!qs_get(qs,"style",sty,sizeof sty)) strcpy(sty,"naval");
    air=!strcmp(sty,"air"); step=air?10:60; total=24*60/step;
    if(air) return api_daily_air(qs,out,cap);

    jput(&j,"<h2>%s table &mdash; %04d-%02d-%02d UTC</h2>",air?"AIR 10-minute":"NAVAL daily",y,mo,d);
    if(air) jput(&j,"<div class=\"hint\">10-minute tabulation for rapid interpolation. Sun/Aries are shown to 0.1&prime;; Moon and planets to 1&prime;.</div>");
    else jput(&j,"<div class=\"hint\">Whole-hour daily table, matching the CLI NAVALM daily-table cadence.</div>");
    jput(&j,"<div style=\"overflow-x:auto\"><table class=\"lops\"><thead><tr><th>UTC</th><th>Aries GHA</th><th>Sun GHA</th><th>Sun Dec</th><th>Moon GHA</th><th>Moon Dec</th>");
    for(k=0;k<4;k++) jput(&j,"<th>%s GHA</th><th>%s Dec</th>",nav_body_name(pb[k]),nav_body_name(pb[k]));
    jput(&j,"</tr></thead><tbody>");
    for(k=0;k<total;k++){
        int mins=k*step, hh=mins/60, mm=mins%60, q;
        double jd=nav_julian_date(y,mo,d,hh,mm,0.0);
        NavAlmanac a;
        char g[32],dc[32];
        int dp=air?1:1;
        jput(&j,"<tr><td>%02d:%02d</td>",hh,mm);
        if(!nav_almanac(NAV_ARIES,jd,&a)){ web_dm(a.gha_deg,g,sizeof g,dp); jput(&j,"<td>%s</td>",g); } else jput(&j,"<td>ERR</td>");
        if(!nav_almanac(NAV_SUN,jd,&a)){ web_dm(a.gha_deg,g,sizeof g,dp); web_dec(a.dec_deg,dc,sizeof dc,dp); jput(&j,"<td>%s</td><td>%s</td>",g,dc); } else jput(&j,"<td colspan=\"2\">ERR</td>");
        if(!nav_almanac(NAV_MOON,jd,&a)){ web_dm(a.gha_deg,g,sizeof g,air?0:1); web_dec(a.dec_deg,dc,sizeof dc,air?0:1); jput(&j,"<td>%s</td><td>%s</td>",g,dc); } else jput(&j,"<td colspan=\"2\">ERR</td>");
        for(q=0;q<4;q++){
            if(!nav_almanac(pb[q],jd,&a)){ web_dm(a.gha_deg,g,sizeof g,air?0:1); web_dec(a.dec_deg,dc,sizeof dc,air?0:1); jput(&j,"<td>%s</td><td>%s</td>",g,dc); }
            else jput(&j,"<td colspan=\"2\">ERR</td>");
        }
        jput(&j,"</tr>");
    }
    jput(&j,"</tbody></table></div>");
    if(air) jput(&j,"<div class=\"hint\" style=\"margin-top:.6rem\">AIR-style table includes all four navigational planets rather than selecting only three.</div>");
    return j.n;
}

/* ------------------------------------------------------------ HTTP plumbing */

static void send_all(int fd,const char *buf,size_t n){
    size_t off = 0;
    while(off < n){
        ssize_t k = write(fd,buf+off,n-off);
        if(k <= 0) return;
        off += (size_t)k;
    }
}

static void send_response(int fd,const char *status,const char *ctype,
                          const char *body,size_t len)
{
    char head[512];
    int k = snprintf(head,sizeof head,
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %lu\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n"
                     "\r\n",status,ctype,(unsigned long)len);
    if(k > 0) send_all(fd,head,(size_t)k);
    if(len) send_all(fd,body,len);
}

static void handle_request(int fd,const char *req)
{
    char method[8],target[1024];
    char *qs;
    static char json[JSON_MAX];
    static char pagebuf[524288];
    size_t n;

    if(sscanf(req,"%7s %1023s",method,target) != 2){
        send_response(fd,"400 Bad Request","text/plain","bad request\n",12);
        return;
    }
    if(strcmp(method,"GET") && strcmp(method,"HEAD")){
        send_response(fd,"405 Method Not Allowed","text/plain","use GET\n",8);
        return;
    }

    qs = strchr(target,'?');
    if(qs) *qs++ = 0;

    if(!strcmp(target,"/") || !strcmp(target,"/index.html")){
        send_response(fd,"200 OK","text/html; charset=utf-8",
                      navweb_page,strlen(navweb_page));
        return;
    }
    if(!strcmp(target,"/api/version")){
        n = (size_t)snprintf(json,sizeof json,"{\"version\":\"%s\"}",NAVALM_WEB_VERSION);
        send_response(fd,"200 OK","application/json",json,n);
        return;
    }
    if(!strcmp(target,"/api/almanac")){
        n = api_almanac(qs,json,sizeof json);
        send_response(fd,"200 OK","application/json",json,n);
        return;
    }
    if(!strcmp(target,"/daily/naval")){
        int y=qs_int(qs,"y",2026), mo=qs_int(qs,"mo",1), d=qs_int(qs,"d",1);
        FILE *f=tmpfile();
        if(!f){ send_response(fd,"500 Internal Server Error","text/plain","table failed\n",13); return; }
        if(navalm_write_daily_html(f,y,mo,d)){ fclose(f); send_response(fd,"500 Internal Server Error","text/plain","table failed\n",13); return; }
        fflush(f);
        { long sz=ftell(f);
          if(sz<0 || (size_t)sz>=sizeof pagebuf){ fclose(f); send_response(fd,"500 Internal Server Error","text/plain","table too large\n",16); return; }
          rewind(f); n=fread(pagebuf,1,(size_t)sz,f); fclose(f);
          send_response(fd,"200 OK","text/html; charset=utf-8",pagebuf,n);
        }
        return;
    }
    if(!strcmp(target,"/daily/air")){
        static const char *head =
            "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<title>Air Almanac daily page</title><style>"
            "body{margin:1rem;background:#fff;color:#111;font:13px/1.35 Georgia,'Times New Roman',serif}"
            ".airpage{page-break-after:always;margin-bottom:2rem}"
            ".airtitle{text-align:center;font-weight:700;letter-spacing:.04em;margin:.4rem 0 .5rem}"
            "table.air{border-collapse:collapse;width:100%;font:12px/1.25 ui-monospace,Menlo,Consolas,monospace}"
            "table.air th{font:11px system-ui;font-weight:600;letter-spacing:.05em;padding:.2rem .35rem;"
            "border-bottom:1px solid #111;text-align:center}"
            "table.air td{padding:.1rem .35rem;text-align:right;white-space:nowrap}"
            "table.air td.ut{text-align:left;color:#444}"
            "table.air tr.hr td{border-top:1px solid #bbb}"
            "table.air tr.half td{background:#f4f2ec}"
            ".mag{font-size:10px;margin-left:.25em;color:#444}"
            ".airnote{margin:.4rem 0;font:11px system-ui;letter-spacing:.06em;text-align:center}"
            ".airfoot{margin:.3rem 0 0;font:11px system-ui;color:#555}"
            "</style></head><body>";
        size_t hl = strlen(head), bl;
        memcpy(pagebuf,head,hl);
        bl = api_daily_air(qs,pagebuf+hl,sizeof pagebuf - hl - 32);
        memcpy(pagebuf+hl+bl,"</body></html>",14);
        send_response(fd,"200 OK","text/html; charset=utf-8",pagebuf,hl+bl+14);
        return;
    }
    if(!strcmp(target,"/api/daily")){
        n = api_daily(qs,pagebuf,sizeof pagebuf);
        send_response(fd,"200 OK","text/html; charset=utf-8",pagebuf,n);
        return;
    }
    if(!strcmp(target,"/api/stars")){
        n = api_stars(qs,json,sizeof json);
        send_response(fd,"200 OK","application/json",json,n);
        return;
    }
    if(!strcmp(target,"/api/reduce")){
        n = api_reduce(qs,json,sizeof json);
        send_response(fd,"200 OK","application/json",json,n);
        return;
    }
    if(!strcmp(target,"/api/fix")){
        n = api_fix(qs,json,sizeof json);
        send_response(fd,"200 OK","application/json",json,n);
        return;
    }
    send_response(fd,"404 Not Found","text/plain","not found\n",10);
}

int navweb_run(const char *addr,int port)
{
    int srv,opt = 1;
    struct sockaddr_in sa;

    signal(SIGPIPE,SIG_IGN);

    srv = socket(AF_INET,SOCK_STREAM,0);
    if(srv < 0){ perror("socket"); return 1; }
    setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof opt);

    memset(&sa,0,sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    if(inet_pton(AF_INET,addr,&sa.sin_addr) != 1){
        fprintf(stderr,"bad listen address: %s\n",addr);
        close(srv);
        return 2;
    }
    if(bind(srv,(struct sockaddr*)&sa,sizeof sa)){ perror("bind"); close(srv); return 1; }
    if(listen(srv,8)){ perror("listen"); close(srv); return 1; }

    printf("navalm %s web interface on http://%s:%d/\n",NAVALM_WEB_VERSION,addr,port);
    if(strcmp(addr,"127.0.0.1"))
        printf("listening on %s: anyone who can reach this machine can use it\n",addr);
    printf("the browser keeps its own sight list; %s and %s are not touched\n",
           navsight_store_path(),navrc_path());
    printf("ctrl-C to stop\n");
    fflush(stdout);

    for(;;){
        struct sockaddr_in ca;
        socklen_t clen = sizeof ca;
        int c = accept(srv,(struct sockaddr*)&ca,&clen);
        char req[REQ_MAX];
        size_t got = 0;
        int headers = 0;

        if(c < 0){
            if(errno == EINTR) continue;
            perror("accept");
            break;
        }

        /* read until the end of the headers, bounded */
        while(got + 1 < sizeof req){
            ssize_t k = read(c,req+got,sizeof req - got - 1);
            if(k <= 0) break;
            got += (size_t)k;
            req[got] = 0;
            if(strstr(req,"\r\n\r\n") || strstr(req,"\n\n")) break;
        }
        req[got] = 0;
        {
            const char *p;
            for(p = req; *p; p++) if(*p == '\n' && ++headers > HDR_MAX) break;
        }
        if(got && headers <= HDR_MAX) handle_request(c,req);
        else send_response(c,"431 Request Header Fields Too Large","text/plain","too big\n",8);

        close(c);
    }
    close(srv);
    return 0;
}
