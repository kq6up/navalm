/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* navalm 2.1 - setup defaults in ~/.navalmrc */

#include "navrc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

const char *navrc_path(void){
    static char path[1024];
    const char *env = getenv("NAVALMRC");
    const char *home;
    if(env && *env) return env;
    home = getenv("HOME");
    if(!home || !*home) home = ".";
    snprintf(path,sizeof path,"%s/.navalmrc",home);
    return path;
}

void navrc_init(NavDefaults *d){
    memset(d,0,sizeof *d);
    d->limb = 1;
    d->temp_c = 10.0;
    d->press_mb = 1010.0;
    d->he = 0.0;
}

int navrc_load(NavDefaults *d){
    FILE *f;
    char line[256];
    navrc_init(d);
    f = fopen(navrc_path(),"r");
    if(!f) return 0;
    while(fgets(line,sizeof line,f)){
        char key[32],val[128];
        char *eq;
        size_t n;
        if(line[0]=='#' || line[0]=='\n') continue;
        eq = strchr(line,'=');
        if(!eq) continue;
        n = (size_t)(eq-line);
        if(n >= sizeof key) n = sizeof key - 1;
        memcpy(key,line,n); key[n]=0;
        snprintf(val,sizeof val,"%s",eq+1);
        n = strlen(val);
        while(n && (val[n-1]=='\n' || val[n-1]=='\r' || val[n-1]==' ')) val[--n]=0;

        if(!strcmp(key,"ie"))            d->ie_min = atof(val);
        else if(!strcmp(key,"iearc"))    d->ie_on_arc = !strcmp(val,"on");
        else if(!strcmp(key,"horizon"))  d->artificial = !strcmp(val,"ah");
        else if(!strcmp(key,"he"))       d->he = atof(val);
        else if(!strcmp(key,"heunit"))   d->he_metres = !strcmp(val,"m");
        else if(!strcmp(key,"limb"))     d->limb = !strcmp(val,"ll") ? 1 : (!strcmp(val,"ul") ? -1 : 0);
        else if(!strcmp(key,"temp"))     d->temp_c = atof(val);
        else if(!strcmp(key,"press"))    d->press_mb = atof(val);
        else if(!strcmp(key,"drlat")){   d->dr_lat = atof(val); d->have_dr = 1; }
        else if(!strcmp(key,"drlon")){   d->dr_lon = atof(val); d->have_dr = 1; }
        else if(!strcmp(key,"course")){  d->course = atof(val); d->have_motion = 1; }
        else if(!strcmp(key,"speed")){   d->speed  = atof(val); d->have_motion = 1; }
        else if(!strcmp(key,"set"))      d->set    = atof(val);
        else if(!strcmp(key,"drift"))    d->drift  = atof(val);
    }
    fclose(f);
    return 0;
}

int navrc_save(const NavDefaults *d,char *err,size_t errn){
    const char *path = navrc_path();
    char tmp[1100];
    FILE *f;
    snprintf(tmp,sizeof tmp,"%s.tmp",path);
    f = fopen(tmp,"w");
    if(!f){ snprintf(err,errn,"cannot write %s",tmp); return 1; }
    fprintf(f,"# navalm setup defaults, format 1\n");
    fprintf(f,"ie=%.2f\n",d->ie_min);
    fprintf(f,"iearc=%s\n",d->ie_on_arc?"on":"off");
    fprintf(f,"horizon=%s\n",d->artificial?"ah":"sea");
    fprintf(f,"he=%.2f\n",d->he);
    fprintf(f,"heunit=%s\n",d->he_metres?"m":"ft");
    fprintf(f,"limb=%s\n",d->limb>0?"ll":(d->limb<0?"ul":"center"));
    fprintf(f,"temp=%.1f\n",d->temp_c);
    fprintf(f,"press=%.1f\n",d->press_mb);
    if(d->have_dr){
        fprintf(f,"drlat=%.6f\n",d->dr_lat);
        fprintf(f,"drlon=%.6f\n",d->dr_lon);
    }
    if(d->have_motion){
        fprintf(f,"course=%.1f\n",d->course);
        fprintf(f,"speed=%.2f\n",d->speed);
        fprintf(f,"set=%.1f\n",d->set);
        fprintf(f,"drift=%.2f\n",d->drift);
    }
    fclose(f);
    if(rename(tmp,path)){ snprintf(err,errn,"cannot rename %s to %s",tmp,path); return 1; }
    return 0;
}

void navrc_apply(const NavDefaults *d,Sight *si){
    si->ie_min    = d->ie_min;
    si->ie_on_arc = d->ie_on_arc;
    si->artificial= d->artificial;
    si->he        = d->he;
    si->he_metres = d->he_metres;
    si->limb      = d->limb;
    si->temp_c    = d->temp_c;
    si->press_mb  = d->press_mb;
    if(d->have_dr){ si->dr_lat = d->dr_lat; si->dr_lon = d->dr_lon; }
    if(d->have_motion){
        si->course = d->course; si->speed = d->speed;
        si->set = d->set; si->drift = d->drift;
        si->has_motion = (d->speed != 0.0 || d->drift != 0.0);
    }
}

void navrc_from_sight(NavDefaults *d,const Sight *si){
    d->ie_min = si->ie_min;
    d->ie_on_arc = si->ie_on_arc;
    d->artificial = si->artificial;
    d->he = si->he;
    d->he_metres = si->he_metres;
    d->limb = si->limb;
    d->temp_c = si->temp_c;
    d->press_mb = si->press_mb;
    d->dr_lat = si->dr_lat;
    d->dr_lon = si->dr_lon;
    d->have_dr = 1;
    if(si->has_motion){
        d->course = si->course; d->speed = si->speed;
        d->set = si->set; d->drift = si->drift;
        d->have_motion = 1;
    }
}

void navrc_print(const NavDefaults *d,FILE *out){
    fprintf(out,"setup defaults in %s\n",navrc_path());
    fprintf(out,"  index error   %.1f' %s arc\n",d->ie_min,d->ie_on_arc?"on":"off");
    fprintf(out,"  horizon       %s\n",d->artificial?"artificial":"sea");
    if(!d->artificial)
        fprintf(out,"  height of eye %.1f %s\n",d->he,d->he_metres?"m":"ft");
    fprintf(out,"  limb          %s\n",d->limb>0?"lower":(d->limb<0?"upper":"center"));
    fprintf(out,"  temperature   %.1f C\n",d->temp_c);
    fprintf(out,"  pressure      %.1f mb\n",d->press_mb);
    if(d->have_dr){
        double la=fabs(d->dr_lat), lo=fabs(d->dr_lon);
        int lad=(int)floor(la), lod=(int)floor(lo);
        double lam=(la-lad)*60.0, lom=(lo-lod)*60.0;
        fprintf(out,"  DR            %02d %04.1f %c  %03d %04.1f %c\n",
                lad,lam,d->dr_lat<0?'S':'N',lod,lom,d->dr_lon<0?'W':'E');
    } else
        fprintf(out,"  DR            (not set)\n");
    if(d->have_motion)
        fprintf(out,"  motion        course %05.1f  speed %.1f kt  set %05.1f  drift %.1f kt\n",
                d->course,d->speed,d->set,d->drift);
}
