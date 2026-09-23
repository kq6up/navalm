/* navalm 2.0 - command handling for: sight, lop, fix */

#include "navsight.h"
#include "navrc.h"
#include "nav_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

static int need(int argc,int i,int k,const char *opt){
    if(i+k < argc) return 1;
    fprintf(stderr,"%s needs %d value%s\n",opt,k,k==1?"":"s");
    return 0;
}

static int num(const char *s,double *out){
    char *e; double v;
    if(!s || !*s) return 0;
    v = strtod(s,&e);
    if(*e) return 0;
    *out = v;
    return 1;
}

static int parse_hemi(const char *s,char a,char b){
    char c;
    if(!s || !s[0] || s[1]) return 0;
    c = (char)toupper((unsigned char)s[0]);
    return (c==a || c==b) ? c : 0;
}

/* --dr LATdeg LATmin N|S LONdeg LONmin E|W  (also used for --ap) */
static int parse_position(int argc,char **argv,int i,double *lat,double *lon){
    double ld,lm,od,om; char ns,ew;
    if(!need(argc,i,6,argv[i])) return -1;
    if(!num(argv[i+1],&ld) || !num(argv[i+2],&lm) || lm<0 || lm>=60) return -1;
    ns = (char)parse_hemi(argv[i+3],'N','S'); if(!ns) return -1;
    if(!num(argv[i+4],&od) || !num(argv[i+5],&om) || om<0 || om>=60) return -1;
    ew = (char)parse_hemi(argv[i+6],'E','W'); if(!ew) return -1;
    ld = fabs(ld)+lm/60.0; od = fabs(od)+om/60.0;
    if(ld>90.0 || od>180.0) return -1;
    *lat = (ns=='S') ? -ld : ld;
    *lon = (ew=='W') ? -od : od;     /* east positive */
    return 6;
}

/* Options shared by "sight" and "lop edit".  Returns 0 on success. */
static int parse_sight_options(int argc,char **argv,int start,Sight *si,
                               int *have_dr,int *do_save,int *brief)
{
    int i;
    for(i=start;i<argc;i++){
        const char *o = argv[i];
        double a,b;
        if(!strcmp(o,"--hs")){
            if(!need(argc,i,2,o)) return 1;
            if(!num(argv[i+1],&a) || !num(argv[i+2],&b) || b<0 || b>=60){
                fprintf(stderr,"--hs wants degrees and minutes, e.g. --hs 94 37.2\n"); return 1; }
            si->hs_deg = fabs(a) + b/60.0;
            i += 2;
        } else if(!strcmp(o,"--ie") || !strcmp(o,"--ic")){
            if(!need(argc,i,1,o)) return 1;
            if(!num(argv[i+1],&a)){ fprintf(stderr,"%s wants arcminutes\n",o); return 1; }
            si->ie_min = fabs(a);
            i += 1;
            if(i+1 < argc && (!strcmp(argv[i+1],"on") || !strcmp(argv[i+1],"off"))){
                si->ie_on_arc = !strcmp(argv[i+1],"on");
                i += 1;
            } else if(a < 0){
                si->ie_on_arc = 1;           /* a negative value means on the arc */
            }
        } else if(!strcmp(o,"--ah") || !strcmp(o,"--artificial")){
            si->artificial = 1;
        } else if(!strcmp(o,"--sea")){
            si->artificial = 0;
        } else if(!strcmp(o,"--he")){
            if(!need(argc,i,1,o)) return 1;
            if(!num(argv[i+1],&a) || a < 0){ fprintf(stderr,"--he wants a height\n"); return 1; }
            si->he = a; si->artificial = 0; i += 1;
            if(i+1 < argc && (!strcmp(argv[i+1],"m") || !strcmp(argv[i+1],"ft"))){
                si->he_metres = !strcmp(argv[i+1],"m");
                i += 1;
            }
        } else if(!strcmp(o,"--limb")){
            if(!need(argc,i,1,o)) return 1;
            if(!strcmp(argv[i+1],"ll") || !strcmp(argv[i+1],"lower")) si->limb = 1;
            else if(!strcmp(argv[i+1],"ul") || !strcmp(argv[i+1],"upper")) si->limb = -1;
            else if(!strcmp(argv[i+1],"center") || !strcmp(argv[i+1],"centre")) si->limb = 0;
            else { fprintf(stderr,"--limb wants ll, ul or center\n"); return 1; }
            i += 1;
        } else if(!strcmp(o,"--temp")){
            if(!need(argc,i,1,o) || !num(argv[i+1],&a)){ fprintf(stderr,"--temp wants Celsius\n"); return 1; }
            si->temp_c = a; i += 1;
        } else if(!strcmp(o,"--tempf")){
            if(!need(argc,i,1,o) || !num(argv[i+1],&a)){ fprintf(stderr,"--tempf wants Fahrenheit\n"); return 1; }
            si->temp_c = (a-32.0)*5.0/9.0; i += 1;
        } else if(!strcmp(o,"--press")){
            if(!need(argc,i,1,o) || !num(argv[i+1],&a) || a<=0){ fprintf(stderr,"--press wants millibars\n"); return 1; }
            si->press_mb = a; i += 1;
        } else if(!strcmp(o,"--inhg")){
            if(!need(argc,i,1,o) || !num(argv[i+1],&a) || a<=0){ fprintf(stderr,"--inhg wants inches of mercury\n"); return 1; }
            si->press_mb = a*33.8639; i += 1;
        } else if(!strcmp(o,"--dr") || !strcmp(o,"--ap")){
            int k = parse_position(argc,argv,i,&si->dr_lat,&si->dr_lon);
            if(k < 0){ fprintf(stderr,"%s wants LATdeg LATmin N|S LONdeg LONmin E|W\n",o); return 1; }
            *have_dr = 1; i += k;
        } else if(!strcmp(o,"--label")){
            if(!need(argc,i,1,o)) return 1;
            strncpy(si->label,argv[i+1],sizeof si->label-1);
            si->label[sizeof si->label-1] = 0;
            i += 1;
        } else if(!strcmp(o,"--course")){
            if(!need(argc,i,1,o) || !num(argv[i+1],&a)){ fprintf(stderr,"--course wants degrees true\n"); return 1; }
            si->course = a; si->has_motion = 1; i += 1;
        } else if(!strcmp(o,"--speed")){
            if(!need(argc,i,1,o) || !num(argv[i+1],&a)){ fprintf(stderr,"--speed wants knots\n"); return 1; }
            si->speed = a; si->has_motion = 1; i += 1;
        } else if(!strcmp(o,"--set")){
            if(!need(argc,i,1,o) || !num(argv[i+1],&a)){ fprintf(stderr,"--set wants degrees true\n"); return 1; }
            si->set = a; si->has_motion = 1; i += 1;
        } else if(!strcmp(o,"--drift")){
            if(!need(argc,i,1,o) || !num(argv[i+1],&a)){ fprintf(stderr,"--drift wants knots\n"); return 1; }
            si->drift = a; si->has_motion = 1; i += 1;
        } else if(!strcmp(o,"--save")){
            *do_save = 1;
        } else if(!strcmp(o,"--brief")){
            *brief = 1;
        } else {
            fprintf(stderr,"unknown option: %s\n",o);
            return 1;
        }
    }
    return 0;
}

static void sight_init(Sight *si){
    NavDefaults d;
    memset(si,0,sizeof *si);
    strcpy(si->body,"sun");
    navrc_load(&d);
    navrc_apply(&d,si);
}

/* sight BODY|star NAME YYYY MM DD HH MM SS [options] */
static int cmd_sight(int argc,char **argv){
    Sight si;
    Reduction r;
    char err[160];
    int i = 2, have_dr = 0, do_save = 0, brief = 0;

    sight_init(&si);
    if(argc < 3){ fprintf(stderr,"sight: need a body\n"); return 2; }

    if(!strcmp(argv[i],"star")){
        int m[16];
        size_t nm;
        if(argc < 4){ fprintf(stderr,"sight star: need a star name\n"); return 2; }
        nm = navsight_star_match(argv[i+1],m,16);
        if(nm == 0){ fprintf(stderr,"unknown star: %s\n",argv[i+1]); return 2; }
        if(nm > 1){
            size_t k;
            fprintf(stderr,"\"%s\" matches %lu stars:\n",argv[i+1],(unsigned long)nm);
            for(k=0;k<nm && k<16;k++) fprintf(stderr,"  %lu  %s\n",(unsigned long)k+1,nav_star_name((size_t)m[k]));
            fprintf(stderr,"use more letters\n");
            return 2;
        }
        strcpy(si.body,"star");
        snprintf(si.star,sizeof si.star,"%s",nav_star_name((size_t)m[0]));
        i += 2;
    } else {
        if(navsight_body_index(argv[i]) < 0 || !strcmp(argv[i],"aries")){
            fprintf(stderr,"sight: unknown body %s\n",argv[i]); return 2; }
        strncpy(si.body,argv[i],sizeof si.body-1);
        i += 1;
    }

    if(i+5 >= argc){ fprintf(stderr,"sight: need YYYY MM DD HH MM SS\n"); return 2; }
    si.y=atoi(argv[i]); si.mo=atoi(argv[i+1]); si.d=atoi(argv[i+2]);
    si.h=atoi(argv[i+3]); si.mi=atoi(argv[i+4]); si.s=atof(argv[i+5]);
    i += 6;

    if(parse_sight_options(argc,argv,i,&si,&have_dr,&do_save,&brief)) return 2;
    if(si.hs_deg <= 0.0){ fprintf(stderr,"sight: --hs is required\n"); return 2; }

    if(navsight_reduce(&si,&r,err,sizeof err)){ fprintf(stderr,"%s\n",err); return 1; }

    if(brief) navsight_print_brief(stdout,&si,&r);
    else {
        navsight_print_correction(stdout,&si,&r);
        if(have_dr) navsight_print_reduction(stdout,&si,&r);
        else printf("\n(no --dr given, so the sight was not reduced)\n");
    }

    if(do_save){
        SightStore st;
        int id;
        if(navsight_store_load(&st,err,sizeof err)){ fprintf(stderr,"%s\n",err); return 1; }
        id = navsight_store_add(&st,&si);
        if(id <= 0){ fprintf(stderr,"out of memory\n"); navsight_store_free(&st); return 1; }
        if(navsight_store_save(&st,err,sizeof err)){ fprintf(stderr,"%s\n",err); navsight_store_free(&st); return 1; }
        printf("\nsaved as LOP %d in %s\n",id,navsight_store_path());
        navsight_store_free(&st);
    }
    return 0;
}

static int reduce_or_warn(const Sight *si,Reduction *r){
    char err[160];
    if(navsight_reduce(si,r,err,sizeof err)){
        fprintf(stderr,"LOP %d: %s\n",si->id,err);
        return 1;
    }
    return 0;
}

static int cmd_lop(int argc,char **argv){
    SightStore st;
    char err[160];
    const char *sub = (argc>2) ? argv[2] : "list";

    if(navsight_store_load(&st,err,sizeof err)){ fprintf(stderr,"%s\n",err); return 1; }

    if(!strcmp(sub,"list")){
        size_t i;
        if(!st.n){ printf("no LOPs stored in %s\n",navsight_store_path()); }
        for(i=0;i<st.n;i++){
            Reduction r;
            if(reduce_or_warn(&st.v[i],&r)) continue;
            navsight_print_brief(stdout,&st.v[i],&r);
            if(st.v[i].has_motion)
                printf("     motion  course %05.1f  speed %.1f kt%s",
                       st.v[i].course,st.v[i].speed,
                       (st.v[i].drift!=0.0)?"":"\n");
            if(st.v[i].has_motion && st.v[i].drift!=0.0)
                printf("  set %05.1f  drift %.1f kt\n",st.v[i].set,st.v[i].drift);
        }
        navsight_store_free(&st);
        return 0;
    }

    if(!strcmp(sub,"show")){
        Sight *si; Reduction r;
        if(argc<4){ fprintf(stderr,"lop show ID\n"); navsight_store_free(&st); return 2; }
        si = navsight_store_find(&st,atoi(argv[3]));
        if(!si){ fprintf(stderr,"no LOP %s\n",argv[3]); navsight_store_free(&st); return 1; }
        if(reduce_or_warn(si,&r)){ navsight_store_free(&st); return 1; }
        navsight_print_correction(stdout,si,&r);
        navsight_print_reduction(stdout,si,&r);
        if(si->has_motion)
            printf("\nMotion for advancing: course %05.1f at %.1f kt, set %05.1f drift %.1f kt\n",
                   si->course,si->speed,si->set,si->drift);
        navsight_store_free(&st);
        return 0;
    }

    if(!strcmp(sub,"delete") || !strcmp(sub,"rm")){
        if(argc<4){ fprintf(stderr,"lop delete ID\n"); navsight_store_free(&st); return 2; }
        if(navsight_store_delete(&st,atoi(argv[3]))){
            fprintf(stderr,"no LOP %s\n",argv[3]); navsight_store_free(&st); return 1; }
        if(navsight_store_save(&st,err,sizeof err)){ fprintf(stderr,"%s\n",err); navsight_store_free(&st); return 1; }
        printf("deleted LOP %s\n",argv[3]);
        navsight_store_free(&st);
        return 0;
    }

    if(!strcmp(sub,"clear")){
        int yes = (argc>3 && !strcmp(argv[3],"--yes"));
        if(!yes){ fprintf(stderr,"lop clear --yes   (removes every stored LOP)\n"); navsight_store_free(&st); return 2; }
        st.n = 0;
        if(navsight_store_save(&st,err,sizeof err)){ fprintf(stderr,"%s\n",err); navsight_store_free(&st); return 1; }
        printf("store cleared\n");
        navsight_store_free(&st);
        return 0;
    }

    if(!strcmp(sub,"edit")){
        Sight *si; Reduction r;
        int have_dr=0,do_save=0,brief=0;
        if(argc<4){ fprintf(stderr,"lop edit ID [options]\n"); navsight_store_free(&st); return 2; }
        si = navsight_store_find(&st,atoi(argv[3]));
        if(!si){ fprintf(stderr,"no LOP %s\n",argv[3]); navsight_store_free(&st); return 1; }
        if(parse_sight_options(argc,argv,4,si,&have_dr,&do_save,&brief)){ navsight_store_free(&st); return 2; }
        if(navsight_store_save(&st,err,sizeof err)){ fprintf(stderr,"%s\n",err); navsight_store_free(&st); return 1; }
        if(!reduce_or_warn(si,&r)){
            navsight_print_correction(stdout,si,&r);
            navsight_print_reduction(stdout,si,&r);
        }
        navsight_store_free(&st);
        return 0;
    }

    if(!strcmp(sub,"advance") || !strcmp(sub,"motion")){
        Sight *si;
        int have_dr=0,do_save=0,brief=0;
        if(argc<4){
            fprintf(stderr,"lop advance ID --course DEG --speed KT [--set DEG --drift KT]\n");
            fprintf(stderr,"lop advance ID --clear\n");
            navsight_store_free(&st); return 2;
        }
        si = navsight_store_find(&st,atoi(argv[3]));
        if(!si){ fprintf(stderr,"no LOP %s\n",argv[3]); navsight_store_free(&st); return 1; }
        if(argc>4 && !strcmp(argv[4],"--clear")){
            si->has_motion = 0;
            si->course = si->speed = si->set = si->drift = 0.0;
        } else if(parse_sight_options(argc,argv,4,si,&have_dr,&do_save,&brief)){
            navsight_store_free(&st); return 2;
        }
        if(navsight_store_save(&st,err,sizeof err)){ fprintf(stderr,"%s\n",err); navsight_store_free(&st); return 1; }
        if(si->has_motion)
            printf("LOP %d will be advanced at course %05.1f, %.1f kt",si->id,si->course,si->speed);
        else
            printf("LOP %d will not be advanced",si->id);
        if(si->has_motion && (si->drift != 0.0))
            printf(", set %05.1f drift %.1f kt",si->set,si->drift);
        printf("\n");
        navsight_store_free(&st);
        return 0;
    }

    fprintf(stderr,"lop: unknown subcommand %s\n",sub);
    navsight_store_free(&st);
    return 2;
}

/* fix ID ID [ID...] [--at YYYY MM DD HH MM SS] */
static int cmd_fix(int argc,char **argv){
    SightStore st;
    Sight sel[16];
    Reduction red[16];
    FixResult fx;
    char err[160],b1[40],b2[40];
    size_t n = 0;
    size_t used[16];
    int i, have_at = 0, ay=0,amo=0,ad=0,ah=0,ami=0;
    int update_dr = 0, update_lops = 0;
    double as = 0.0;

    if(navsight_store_load(&st,err,sizeof err)){ fprintf(stderr,"%s\n",err); return 1; }

    for(i=2;i<argc;i++){
        if(!strcmp(argv[i],"--update-dr")){
            update_dr = 1;
        } else if(!strcmp(argv[i],"--update-lops")){
            update_lops = 1;
        } else if(!strcmp(argv[i],"--at")){
            if(i+6 >= argc){ fprintf(stderr,"--at wants YYYY MM DD HH MM SS\n"); navsight_store_free(&st); return 2; }
            ay=atoi(argv[i+1]); amo=atoi(argv[i+2]); ad=atoi(argv[i+3]);
            ah=atoi(argv[i+4]); ami=atoi(argv[i+5]); as=atof(argv[i+6]);
            have_at = 1; i += 6;
        } else {
            Sight *si = navsight_store_find(&st,atoi(argv[i]));
            if(!si){ fprintf(stderr,"no LOP %s\n",argv[i]); navsight_store_free(&st); return 1; }
            if(n >= 16){ fprintf(stderr,"at most 16 LOPs in a fix\n"); navsight_store_free(&st); return 2; }
            sel[n] = *si;
            if(reduce_or_warn(si,&red[n])){ navsight_store_free(&st); return 1; }
            used[n] = (size_t)(si - st.v);
            n++;
        }
    }

    if(n < 2){ fprintf(stderr,"fix: give at least two LOP ids\n"); navsight_store_free(&st); return 2; }

    if(navsight_fix(sel,red,n,have_at,ay,amo,ad,ah,ami,as,&fx,err,sizeof err)){
        fprintf(stderr,"%s\n",err); navsight_store_free(&st); return 1;
    }

    printf("Fix at %04d-%02d-%02d %02d:%02d:%04.1f UTC from %lu LOPs\n\n",
           fx.fy,fx.fmo,fx.fd,fx.fh,fx.fmi,fx.fs,(unsigned long)n);
    for(i=0;i<(int)n;i++){
        printf("  LOP %-3d %-9s Zn %05.1f  a %5.1f %s",
               sel[i].id,(!strcmp(sel[i].body,"star"))?sel[i].star:sel[i].body,
               red[i].zn,fabs(red[i].intercept_nm),red[i].intercept_nm>=0?"T":"A");
        if(fx.advanced_nm[i] > 0.05)
            printf("  advanced %.1f NM %03.0f",fx.advanced_nm[i],fx.advanced_brg[i]);
        printf("  residual %+.1f NM\n",fx.residual_nm[i]);
    }

    printf("\n  Fix     %s  %s\n",
           navsight_fmt_lat(fx.lat,b1,sizeof b1),navsight_fmt_lon(fx.lon,b2,sizeof b2));
    printf("  RMS     %.2f NM\n",fx.rms_nm);
    if(fx.have_ellipse)
        printf("  1-sigma ellipse  %.2f x %.2f NM, major axis %03.0f\n",
               fx.semi_major_nm,fx.semi_minor_nm,fx.ellipse_brg);
    else
        printf("  (two LOPs: this is their intersection, with no redundancy to estimate error)\n");

    if(update_dr){
        NavDefaults d;
        navrc_load(&d);
        d.dr_lat = fx.lat; d.dr_lon = fx.lon; d.have_dr = 1;
        if(navrc_save(&d,err,sizeof err)) fprintf(stderr,"%s\n",err);
        else printf("\n  setup DR updated to the fix in %s\n",navrc_path());
    }
    if(update_lops){
        size_t k;
        for(k=0;k<n;k++){
            st.v[used[k]].dr_lat = fx.lat;
            st.v[used[k]].dr_lon = fx.lon;
        }
        if(navsight_store_save(&st,err,sizeof err)) fprintf(stderr,"%s\n",err);
        else printf("  DR of the %lu LOPs set to the fix; they will re-reduce against it\n",
                    (unsigned long)n);
    }

    navsight_store_free(&st);
    return 0;
}

/* setup [options]   - show or change the defaults in ~/.navalmrc */
static int cmd_setup(int argc,char **argv){
    NavDefaults d;
    Sight si;
    char err[160];
    int have_dr=0,do_save=0,brief=0;

    navrc_load(&d);
    if(argc <= 2 || (argc==3 && !strcmp(argv[2],"--show"))){
        navrc_print(&d,stdout);
        return 0;
    }
    memset(&si,0,sizeof si);
    strcpy(si.body,"sun");
    navrc_apply(&d,&si);
    if(parse_sight_options(argc,argv,2,&si,&have_dr,&do_save,&brief)) return 2;
    navrc_from_sight(&d,&si);
    if(!have_dr && !d.have_dr) d.have_dr = 0;
    if(navrc_save(&d,err,sizeof err)){ fprintf(stderr,"%s\n",err); return 1; }
    navrc_print(&d,stdout);
    return 0;
}

int navsight_command(int argc,char **argv){
    if(!strcmp(argv[1],"sight")) return cmd_sight(argc,argv);
    if(!strcmp(argv[1],"lop"))   return cmd_lop(argc,argv);
    if(!strcmp(argv[1],"fix"))   return cmd_fix(argc,argv);
    if(!strcmp(argv[1],"setup")) return cmd_setup(argc,argv);
    return -1;
}
