/* CLI adapter; included by navoccult.c to keep the core API independent. */
static int cli_num(const char *s,double *v){char *end;*v=strtod(s,&end);return *s&&!*end&&isfinite(*v);}
static int cli_epoch(const char *s,double *jd){
    int y,mo,d,h=0,mi=0,used=0;double sec=0;char tail=0;
    if(sscanf(s,"%d-%d-%dT%d:%d:%lf%n",&y,&mo,&d,&h,&mi,&sec,&used)!=6){
        if(sscanf(s,"%d-%d-%d%n",&y,&mo,&d,&used)!=3)return 1;
    }
    if(s[used]){tail=s[used];if(tail!='Z'||s[used+1])return 1;}
    if(y<1600||y>2399||mo<1||mo>12||d<1||d>31||h<0||h>23||mi<0||mi>59||!isfinite(sec)||sec<0||sec>=60)return 1;
    int md[]={31,28,31,30,31,30,31,31,30,31,30,31};if(y%4==0&&(y%100!=0||y%400==0))md[1]=29;if(d>md[mo-1])return 1;
    *jd=nav_julian_date(y,mo,d,h,mi,sec);return 0;
}
static void cli_help(void){
    puts("Lunar stellar occultation planner and UTC recovery (Precise engine, DE440s).\n"
         "  navalm occultations --date YYYY-MM-DD --lat DEG --lon DEG [options]\n"
         "  navalm occultation --star NAME --near YYYY-MM-DDTHH:MM:SSZ\n"
         "      --watch YYYY-MM-DDTHH:MM:SS --contact D|R --lat DEG --lon DEG [options]\n"
         "East longitude positive; dates/watch readings use the UTC calendar and scale.\n"
         "Planner: --days 7 --magnitude 6.5 --altitude 5 --sort time|magnitude|central|grazing\n"
         "         --filter all|direct|grazing --daylight --sun-altitude -6 --margin 30\n"
         "Both:    --height 0 (m) --dut1 0 (UT1 minus UTC, seconds)\n"
         "Recovery: --reaction 0 (positive = late, seconds) --limb-offset 0 (arcsec)\n"
         "          --timing-sigma .2 (s) --limb-sigma 2 (arcsec) --position-sigma .1 (NM/axis)\n"
         "Smooth Moon only: near-limb is a geometry ranking, NOT a terrain graze prediction.\n"
         "Examples:\n"
         "  navalm occultations --date 2026-03-27 --lat 39.8 --lon -105 --days 1 --magnitude 4\n"
         "  navalm occultation --star Regulus --near 2026-02-03T03:00:00Z --contact D\n"
         "    --watch 2026-02-03T03:00:30 --lat 34 --lon -117 --daylight\n"
         "The second example shows syntax; enter YOUR predicted event and raw watch reading.\n"
         "See OCCULTATIONS.md. No saved sights or clock logs are changed by these commands.");
}
int navoccult_command(int argc,char **argv){
    int recovery=!strcmp(argv[1],"occultation"),star=-1,sort=0,filter=0,have_lat=0,have_lon=0,have_date=0,have_watch=0,have_near=0,have_contact=0;
    double near=0;NavOccultRequest q;navoccult_init(&q);NavOccultObservation o={0,0,0,.2,2,.1,0};char err[512];
    if(argc==2||(argc==3&&!strcmp(argv[2],"--help"))){cli_help();return argc==2?2:0;}
    for(int i=2;i<argc;i++){
        const char *key=argv[i];if(!strcmp(key,"--daylight")){q.night_only=0;continue;}
        if(i+1>=argc){fprintf(stderr,"%s needs a value\n",key);return 2;}const char *v=argv[++i];double *number=NULL;
        if(!strcmp(key,"--lat")){number=&q.lat;have_lat=1;}
        else if(!strcmp(key,"--lon")){number=&q.lon;have_lon=1;}
        else if(!strcmp(key,"--height"))number=&q.height_m;
        else if(!strcmp(key,"--dut1"))number=&q.dut1_sec;
        else if(!strcmp(key,"--days")&&!recovery)number=&q.days;
        else if(!strcmp(key,"--magnitude")&&!recovery)number=&q.max_magnitude;
        else if(!strcmp(key,"--altitude"))number=&q.min_alt_deg;
        else if(!strcmp(key,"--sun-altitude"))number=&q.max_sun_alt_deg;
        else if(!strcmp(key,"--margin")&&!recovery)number=&q.margin_arcsec;
        else if(!strcmp(key,"--reaction")&&recovery)number=&o.reaction_sec;
        else if(!strcmp(key,"--limb-offset")&&recovery)number=&o.limb_offset_arcsec;
        else if(!strcmp(key,"--timing-sigma")&&recovery)number=&o.timing_sigma_sec;
        else if(!strcmp(key,"--limb-sigma")&&recovery)number=&o.limb_sigma_arcsec;
        else if(!strcmp(key,"--position-sigma")&&recovery)number=&o.position_sigma_nm;
        else if(!strcmp(key,"--star")&&recovery){star=navoccult_star_find(v);if(star<0){fprintf(stderr,"Unknown catalogue star: %s\n",v);return 2;}continue;}
        else if(!strcmp(key,"--date")&&!recovery){if(cli_epoch(v,&q.start_jd))goto bad_epoch;have_date=1;continue;}
        else if(!strcmp(key,"--near")&&recovery){if(cli_epoch(v,&near))goto bad_epoch;have_near=1;q.start_jd=near-.25;q.days=.5;continue;}
        else if(!strcmp(key,"--watch")&&recovery){if(cli_epoch(v,&o.watch_jd))goto bad_epoch;have_watch=1;continue;}
        else if(!strcmp(key,"--contact")&&recovery){if(strcmp(v,"D")&&strcmp(v,"R")){fprintf(stderr,"--contact is D (disappearance) or R (reappearance)\n");return 2;}o.contact=!strcmp(v,"R");have_contact=1;continue;}
        else if(!strcmp(key,"--sort")&&!recovery){const char *m[]={"time","magnitude","central","grazing"};int found=0;for(int k=0;k<4;k++)if(!strcmp(v,m[k])){sort=k;found=1;}if(!found){fprintf(stderr,"Unknown sort: %s\n",v);return 2;}continue;}
        else if(!strcmp(key,"--filter")&&!recovery){if(!strcmp(v,"all"))filter=0;else if(!strcmp(v,"direct"))filter=1;else if(!strcmp(v,"grazing"))filter=2;else{fprintf(stderr,"Unknown filter: %s\n",v);return 2;}continue;}
        else{fprintf(stderr,"Unknown option: %s\n",key);return 2;}
        if(!cli_num(v,number)){fprintf(stderr,"%s needs a finite number\n",key);return 2;}continue;
    bad_epoch:fprintf(stderr,"Invalid UTC date/time: %s\n",v);return 2;
    }
    if(!have_lat||!have_lon||(!recovery&&!have_date)||(recovery&&(!have_near||!have_watch||!have_contact||star<0))){cli_help();return 2;}
    if(recovery){NavOccultEvent event;NavOccultRecovery r;
        if(navoccult_event(&q,star,near,0,&event,err,sizeof err)||navoccult_recover(&q,&event,&o,&r,err,sizeof err)){fprintf(stderr,"occultation: %s\n",err);return 1;}
        navoccult_recovery_report(stdout,&q,&event,&o,&r);
    }else{
        NavOccultResult *r=calloc(1,sizeof *r);if(!r){fprintf(stderr,"Not enough memory\n");return 1;}
        if(navoccult_search(&q,r,NULL,NULL,err,sizeof err)){fprintf(stderr,"occultations: %s\n",err);free(r);return 1;}
        size_t used=0;for(size_t i=0;i<r->count;i++)if(!filter||(filter==1?!r->events[i].grazing:r->events[i].grazing))r->events[used++]=r->events[i];r->count=used;
        navoccult_sort(r,sort);navoccult_report(stdout,&q,r);free(r);
    }return 0;
}
