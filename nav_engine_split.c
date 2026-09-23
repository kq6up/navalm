/* NAV89 split TI-89 engine.
 * Compile with NAV_ENABLE_MOON, NAV_ENABLE_PLANETS, or NAV_ENABLE_STARS.
 * Sun/Aries support is common to all variants.
 */
#include "nav_engine.h"
#include "planet_data.h"
#ifdef NAV_ENABLE_MOON
#include "moon_data.h"
#endif
#ifdef NAV_ENABLE_STARS
#include "star_data.h"
#endif
#include <math.h>
#include <stddef.h>
#ifdef NAV_ENABLE_STARS
#include <string.h>
#include <ctype.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif
#define D2R (M_PI/180.0)
#define R2D (180.0/M_PI)
typedef struct { double x,y,z; } Vec3;
typedef struct { double ra,dec; } Eq;
static double norm360(double x){x=fmod(x,360.0);if(x<0)x+=360.0;return x;}
static double sind(double x){return sin(x*D2R);} static double cosd(double x){return cos(x*D2R);}
static double tand(double x){return tan(x*D2R);} static double asind(double x){if(x>1)x=1;if(x<-1)x=-1;return asin(x)*R2D;}
static double atan2d(double y,double x){return atan2(y,x)*R2D;}

double nav_julian_date(int y,int m,int d,int hh,int mm,double ss){int Y=y,M=m;if(M<=2){Y--;M+=12;}int A=Y/100,B=2-A+A/4;double f=(hh+(mm+ss/60.0)/60.0)/24.0;return floor(365.25*(Y+4716))+floor(30.6001*(M+1))+d+B-1524.5+f;}
static void jd_to_calendar(double jd,int *yo,int *mo,double *do_){double Z=floor(jd+0.5),F=(jd+0.5)-Z,A=Z;if(Z>=2299161){double a=floor((Z-1867216.25)/36524.25);A=Z+1+a-floor(a/4);}double B=A+1524,C=floor((B-122.1)/365.25),D=floor(365.25*C),E=floor((B-D)/30.6001);double day=B-D-floor(30.6001*E)+F;int m=(E<14)?(int)E-1:(int)E-13;int y=(m>2)?(int)C-4716:(int)C-4715;if(yo)*yo=y;if(mo)*mo=m;if(do_)*do_=day;}
static int leap(int y){return y%4==0&&(y%100!=0||y%400==0);} static double decyear(double jd){int y,m;double d;jd_to_calendar(jd,&y,&m,&d);static const int c[12]={0,31,59,90,120,151,181,212,243,273,304,334};double n=c[m-1]+d;if(m>2&&leap(y))n+=1;return y+(n-1)/(leap(y)?366.0:365.0);}
typedef struct{int y,m,d,tai;} Leap; static const Leap leaps[]={{1972,1,1,10},{1972,7,1,11},{1973,1,1,12},{1974,1,1,13},{1975,1,1,14},{1976,1,1,15},{1977,1,1,16},{1978,1,1,17},{1979,1,1,18},{1980,1,1,19},{1981,7,1,20},{1982,7,1,21},{1983,7,1,22},{1985,7,1,23},{1988,1,1,24},{1990,1,1,25},{1991,1,1,26},{1992,7,1,27},{1993,7,1,28},{1994,7,1,29},{1996,1,1,30},{1997,7,1,31},{1999,1,1,32},{2006,1,1,33},{2009,1,1,34},{2012,7,1,35},{2015,7,1,36},{2017,1,1,37}};
static int tai_for(double jd){int t=10;size_t i;for(i=0;i<sizeof(leaps)/sizeof(leaps[0]);i++){if(jd>=nav_julian_date(leaps[i].y,leaps[i].m,leaps[i].d,0,0,0))t=leaps[i].tai;else break;}return t;}
static double old_dt(double y){double u,t;if(y < -500){u=(y-1820)/100;return -20+32*u*u;}if(y<500){u=y/100;return 10583.6-1014.41*u+33.78311*u*u-5.952053*u*u*u-0.1798452*u*u*u*u+0.022174192*u*u*u*u*u+0.0090316521*u*u*u*u*u*u;}if(y<1600){u=(y-1000)/100;return 1574.2-556.01*u+71.23472*u*u+0.319781*u*u*u-0.8503463*u*u*u*u-0.005050998*u*u*u*u*u+0.0083572073*u*u*u*u*u*u;}if(y<1700){t=y-1600;return 120-.9808*t-.01532*t*t+t*t*t/7129;}if(y<1800){t=y-1700;return 8.83+.1603*t-.0059285*t*t+.00013336*t*t*t-t*t*t*t/1174000;}if(y<1860){t=y-1800;return 13.72-.332447*t+.0068612*t*t+.0041116*t*t*t-.00037436*t*t*t*t+.0000121272*t*t*t*t*t-.0000001699*t*t*t*t*t*t+.000000000875*t*t*t*t*t*t*t;}if(y<1900){t=y-1860;return 7.62+.5737*t-.251754*t*t+.01680668*t*t*t-.0004473624*t*t*t*t+t*t*t*t*t/233174;}if(y<1920){t=y-1900;return -2.79+1.494119*t-.0598939*t*t+.0061966*t*t*t-.000197*t*t*t*t;}if(y<1941){t=y-1920;return 21.20+.84493*t-.076100*t*t+.0020936*t*t*t;}if(y<1961){t=y-1950;return 29.07+.407*t-t*t/233+t*t*t/2547;}if(y<1986){t=y-1975;return 45.45+1.067*t-t*t/260-t*t*t/718;}if(y<2005){t=y-2000;return 63.86+.3345*t-.060374*t*t+.0017275*t*t*t+.000651814*t*t*t*t+.00002373599*t*t*t*t*t;}if(y<2050){t=y-2000;return 62.92+.32217*t+.005589*t*t;}if(y<2150)return -20+32*((y-1820)/100)*((y-1820)/100)-.5628*(2150-y);u=(y-1820)/100;return -20+32*u*u;}
double nav_delta_t_with_dut1_seconds(double jd,double dut1){if(jd>=nav_julian_date(1972,1,1,0,0,0))return 32.184+tai_for(jd)-dut1;return old_dt(decyear(jd));} double nav_delta_t_seconds(double jd){return nav_delta_t_with_dut1_seconds(jd,0);}
static double eps(double jd){double T=(jd-2451545)/36525;return 23.4392911-.01300417*T-1.639e-7*T*T+5.04e-7*T*T*T;}
static void nut(double jd,double *dp,double *de){double T=(jd-2415020)/36525,A=100.002136*T,B=5.372617*T,L=norm360(279.6967+360*(A-floor(A))),O=norm360(259.1833-360*(B-floor(B)));*dp=(-17.2*sind(O)-1.319*sind(2*L))/3600;*de=(9.203*cosd(O)+.574*cosd(2*L))/3600;}
static Eq e2q(double lon,double lat,double e){Eq q;q.ra=norm360(atan2d(sind(lon)*cosd(e)-tand(lat)*sind(e),cosd(lon)));q.dec=asind(sind(lat)*cosd(e)+cosd(lat)*sind(e)*sind(lon));return q;}
static void q2e(double ra,double dec,double e,double *lon,double *lat){*lon=norm360(atan2d(sind(ra)*cosd(e)+tand(dec)*sind(e),cosd(ra)));*lat=asind(sind(dec)*cosd(e)-cosd(dec)*sind(e)*sind(ra));}
static Eq addnut(Eq q,double jd){double e=eps(jd),l,b,dp,de;q2e(q.ra,q.dec,e,&l,&b);nut(jd,&dp,&de);return e2q(norm360(l+dp),b,e+de);} static Eq aberr(Eq q,double sl,double jd){double e=eps(jd),l,b;q2e(q.ra,q.dec,e,&l,&b);return e2q(norm360(l-20.49*cosd(sl-l)/(3600*cosd(b))),b-20.49*sind(sl-l)*sind(b)/3600,e);}
double nav_gast_deg(double jd){double j0=floor(jd-.5)+.5,ut=(jd-j0)*24,T=(j0-2451545)/36525,gm=6.697374558+(8640184.81287*T+.093104*T*T-.0000062*T*T*T)/3600+ut*1.00273790935,dp,de;nut(jd,&dp,&de);return norm360((gm+dp*cosd(eps(jd))/15)*15);} double nav_gha_aries_deg(double jd){return nav_gast_deg(jd);}
static double ecoord(const NavSeries s[6],double T){double sum=0,p=1;int d;for(d=0;d<6;d++){double v=0;size_t i;for(i=0;i<s[d].n;i++)v+=s[d].term[i].a*cosd(s[d].term[i].b+s[d].term[i].c*T);sum+=v*p;p*=T;}return sum;} static Vec3 eplanet(const NavSeries d[3][6],double T){Vec3 v={ecoord(d[0],T),ecoord(d[1],T),ecoord(d[2],T)};return v;} static void vll(Vec3 v,double*l,double*b){*l=norm360(atan2d(v.y,v.x));*b=atan2d(v.z,sqrt(v.x*v.x+v.y*v.y));}
static double sunlon(double jd){double T=(jd-2451545)/365250;Vec3 e=eplanet(nav_sun,T);e.x=-e.x;e.y=-e.y;e.z=-e.z;double l,b;vll(e,&l,&b);return l;} static double sunsd(double jd){double T=(jd-2451545)/365250;Vec3 e=eplanet(nav_sun,T),g={-e.x,-e.y,-e.z};double r=sqrt(g.x*g.x+g.y*g.y+g.z*g.z);return .266564/r;}
static int sun_app(double jd,NavAlmanac*o){double tt=jd+nav_delta_t_seconds(jd)/86400,T=(tt-2451545)/365250;Vec3 e=eplanet(nav_ear,T),g={-e.x,-e.y,-e.z};double r=sqrt(g.x*g.x+g.y*g.y+g.z*g.z),l,b;vll(g,&l,&b);Eq q=e2q(l,b,eps(tt));q=addnut(q,tt);q=aberr(q,sunlon(jd),tt);o->ra_deg=q.ra;o->dec_deg=q.dec;o->gha_deg=norm360(nav_gast_deg(jd)-q.ra);o->distance=r;o->hp_deg=.0024427/r;o->sd_deg=sunsd(tt);return 0;}

#ifdef NAV_ENABLE_PLANETS
static const NavSeries (*pser(NavBody b))[6]{switch(b){case NAV_MERCURY:return nav_mer;case NAV_VENUS:return nav_ven;case NAV_MARS:return nav_mar;case NAV_JUPITER:return nav_jup;case NAV_SATURN:return nav_sat;default:return NULL;}}
static int planet_app(NavBody b,double jd,NavAlmanac*o){double tt=jd+nav_delta_t_seconds(jd)/86400,T=(tt-2451545)/365250;Vec3 e=eplanet(nav_ear,T),p={0,0,0};const NavSeries(*s)[6]=pser(b);if(!s)return -1;p=eplanet(s,T);double dx=p.x-e.x,dy=p.y-e.y,dz=p.z-e.z,r=sqrt(dx*dx+dy*dy+dz*dz),Tl=T-r*.0057756/365250;p=eplanet(s,Tl);Vec3 g={p.x-e.x,p.y-e.y,p.z-e.z};r=sqrt(g.x*g.x+g.y*g.y+g.z*g.z);double l,la;vll(g,&l,&la);Eq q=e2q(l,la,eps(tt));q=addnut(q,tt);q=aberr(q,sunlon(jd),tt);o->ra_deg=q.ra;o->dec_deg=q.dec;o->gha_deg=norm360(nav_gast_deg(jd)-q.ra);o->distance=r;o->hp_deg=.0024427/r;o->sd_deg=0;return 0;}
#endif

#ifdef NAV_ENABLE_MOON
static void margs(double jd,double*Lp,double*D,double*M,double*Mp,double*F,double*A1,double*A2,double*A3,double*E,double*E2){double T=(jd-2451545)/36525;*Lp=norm360((((-1.53388348621e-8*T+1.85583502369e-6)*T-.0013268)*T+481267.881342)*T+218.3164591);*D=norm360((((-8.84446999514e-9*T+1.83194471924e-6)*T-.00163)*T+445267.111517)*T+297.8502042);*M=norm360(((4.08329930584e-8*T-.0001536)*T+35999.0502909)*T+357.5291092);*Mp=norm360((((-6.79717237629e-8*T+1.43474081407e-5)*T+.008997)*T+477198.867631)*T+134.9634114);*F=norm360((((1.15833246458e-9*T-2.83607487238e-7)*T-.0034029)*T+483202.017527)*T+93.2720993);*A1=norm360(119.75+131.849*T);*A2=norm360(53.09+479264.29*T);*A3=norm360(313.45+481266.484*T);*E=1-.002516*T-.0000074*T*T;*E2=*E**E;}
static double msum(const MoonTerm*a,size_t n,double D,double M,double Mp,double F,double E,double E2,int co){double s=0;size_t i;for(i=0;i<n;i++){double ef=1;int am=a[i].m<0?-a[i].m:a[i].m;if(am==1)ef=E;else if(am==2)ef=E2;double an=a[i].d*D+a[i].m*M+a[i].mp*Mp+a[i].f*F;s+=a[i].coeff*ef*(co?cosd(an):sind(an));}return s;}
static void moon_tt(double jd,double*l,double*b,double*r){double Lp,D,M,Mp,F,A1,A2,A3,E,E2;margs(jd,&Lp,&D,&M,&Mp,&F,&A1,&A2,&A3,&E,&E2);double sl=msum(moon_lon,moon_lon_n,D,M,Mp,F,E,E2,0)+3958*sind(A1)+1962*sind(Lp-F)+318*sind(A2);double sb=msum(moon_lat,moon_lat_n,D,M,Mp,F,E,E2,0)-2235*sind(Lp)+382*sind(A3)+175*sind(A1-F)+175*sind(A1+F)+127*sind(Lp-Mp)-115*sind(Lp+Mp);double sr=msum(moon_dist,moon_dist_n,D,M,Mp,F,E,E2,1);*l=norm360(Lp+sl/1e6);*b=sb/1e6;*r=385000.56+sr/1000;}
int nav_moon_geocentric(double jd,double*l,double*b,double*r){if(!l||!b||!r)return -1;moon_tt(jd+nav_delta_t_seconds(jd)/86400,l,b,r);return 0;}
#else
int nav_moon_geocentric(double jd,double*l,double*b,double*r){(void)jd;(void)l;(void)b;(void)r;return -1;}
#endif

#ifdef NAV_ENABLE_STARS
static void precess(double ra0,double dec0,double jd,double*ra,double*dec){double T=(jd-2451545)/36525,x=.64061614*T+.000083856*T*T+.0000049994*T*T*T,z=.64061614*T+.000304078*T*T+.0000050564*T*T*T,th=.55675303*T-.00011851*T*T-.00001162*T*T*T,cx=cosd(x),sx=sind(x),cz=cosd(z),sz=sind(z),ct=cosd(th),st=sind(th);double m[3][3]={{cx*ct*cz-sx*sz,cx*ct*sz+sx*cz,cx*st},{-sx*ct*cz-cx*sz,-sx*ct*sz+cx*cz,-sx*st},{-st*cz,-st*sz,ct}},v[3]={cosd(dec0)*cosd(ra0),cosd(dec0)*sind(ra0),sind(dec0)},w[3];int i;for(i=0;i<3;i++)w[i]=m[0][i]*v[0]+m[1][i]*v[1]+m[2][i]*v[2];*ra=norm360(atan2d(w[1],w[0]));*dec=atan2d(w[2],sqrt(w[0]*w[0]+w[1]*w[1]));}
size_t nav_star_count_get(void){return nav_star_count;} const char*nav_star_name(size_t i){return i<nav_star_count?nav_stars[i].name:NULL;} int nav_star_find(const char*n){size_t i;if(!n)return-1;for(i=0;i<nav_star_count;i++){const char*a=n,*b=nav_stars[i].name;int ok=1;while(*a||*b){while(*a&&(*a==' '||*a=='-'||*a=='_'))a++;while(*b&&(*b==' '||*b=='-'||*b=='_'))b++;if(!*a||!*b)break;if(tolower((unsigned char)*a)!=tolower((unsigned char)*b)){ok=0;break;}a++;b++;}if(ok&&!*a&&!*b)return(int)i;}return-1;}
int nav_star_almanac(size_t i,double jd,NavStarAlmanac*o){if(!o||i>=nav_star_count)return-1;const NavStarData*s=&nav_stars[i];double tt=jd+nav_delta_t_seconds(jd)/86400,c=(tt-2451545)/36525,ra0=norm360(s->ra_deg+(s->pm_ra_s_per_century/240)*c),dec0=s->dec_deg+(s->pm_dec_arcsec_per_century/3600)*c,ra,dec;precess(ra0,dec0,tt,&ra,&dec);Eq q={ra,dec};q=addnut(q,tt);q=aberr(q,sunlon(jd),tt);o->name=s->name;o->ra_deg=q.ra;o->dec_deg=q.dec;o->sha_deg=norm360(360-q.ra);o->gha_deg=norm360(nav_gha_aries_deg(jd)+o->sha_deg);o->magnitude=s->mag;return 0;}
#else
size_t nav_star_count_get(void){return 0;} const char*nav_star_name(size_t i){(void)i;return NULL;} int nav_star_find(const char*n){(void)n;return-1;} int nav_star_almanac(size_t i,double jd,NavStarAlmanac*o){(void)i;(void)jd;(void)o;return-1;}
#endif

int nav_almanac(NavBody b,double jd,NavAlmanac*o){if(!o)return-1;*o=(NavAlmanac){0};if(b==NAV_ARIES){o->gha_deg=nav_gha_aries_deg(jd);return 0;}if(b==NAV_SUN)return sun_app(jd,o);
#ifdef NAV_ENABLE_MOON
if(b==NAV_MOON){double tt=jd+nav_delta_t_seconds(jd)/86400,l,la,r;moon_tt(tt,&l,&la,&r);Eq q=e2q(l,la,eps(tt));q=addnut(q,tt);q=aberr(q,sunlon(jd),tt);o->ra_deg=q.ra;o->dec_deg=q.dec;o->gha_deg=norm360(nav_gast_deg(jd)-q.ra);o->distance=r;o->hp_deg=asind(6378.14/r);o->sd_deg=.2725*o->hp_deg;return 0;}
#endif
#ifdef NAV_ENABLE_PLANETS
if(b>=NAV_MERCURY&&b<=NAV_SATURN)return planet_app(b,jd,o);
#endif
return-2;}
const char*nav_body_name(NavBody b){static const char*n[]={"Sun","Moon","Mercury","Venus","Mars","Jupiter","Saturn","Aries"};return(b>=0&&b<=NAV_ARIES)?n[b]:"?";}
