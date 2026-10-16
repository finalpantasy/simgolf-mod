#include "port/atmosphere.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static double bound(double v,double a,double b){return !isfinite(v)?a:v<a?a:v>b?b:v;}
static double smooth(double a,double b,double v){v=bound((v-a)/(b-a),0,1);return v*v*(3-2*v);}
AtmosphereTuning atmosphere_defaults(void){AtmosphereTuning t={.26,.065,.55,.32,.32,.40,12,.24};return t;}
int atmosphere_load(AtmosphereTuning *t,const char *path){
  if(!t||!path)return 0;FILE *f=fopen(path,"r");if(!f)return 0;
  AtmosphereTuning n=atmosphere_defaults();char line[160],key[48],extra;double value;unsigned seen=0;int valid=1;
  while(fgets(line,sizeof line,f)){
    if(line[0]=='#'||line[0]=='\n'||line[0]=='\r')continue;
    if(sscanf(line,"%47[^,],%lf %c",key,&value,&extra)!=2||!isfinite(value)){valid=0;break;}
    unsigned bit=0;
    if(!strcmp(key,"mist_strength")){n.mist=bound(value,0,.45);bit=1;}
    else if(!strcmp(key,"haze_strength")){n.haze=bound(value,0,.15);bit=2;}
    else if(!strcmp(key,"gust_strength")){n.gust=bound(value,0,1);bit=4;}
    else if(!strcmp(key,"golden_strength")){n.golden=bound(value,0,.6);bit=8;}
    else if(!strcmp(key,"shadow_opacity")){n.shadow_opacity=bound(value,.1,.5);bit=16;}
    else if(!strcmp(key,"puddle_drain")){n.puddle_drain=bound(value,.15,1);bit=32;}
    else if(!strcmp(key,"front_lead_seconds")){n.front_lead=bound(value,0,60);bit=64;}
    else if(!strcmp(key,"distant_thunder_gain")){n.distant_gain=bound(value,0,.5);bit=128;}
    else {valid=0;break;}
    if(seen&bit){valid=0;break;}seen|=bit;
  }
  if(ferror(f))valid=0;fclose(f);if(!valid||seen!=255)return 0;*t=n;return 1;
}
AtmosphereSample atmosphere_sample(const AtmosphereTuning *t,double seconds,double hour,
  double c,double rh,double cloud,double rain,double wind,uint32_t seed){
  AtmosphereSample s={0};if(!t)return s;
  seconds=bound(seconds,0,1e9);hour=bound(hour,0,24);c=bound(c,-45,55);rh=bound(rh,5,100);
  cloud=bound(cloud,0,1);rain=bound(rain,0,1);wind=bound(wind,0,2);
  double phase=(seed&65535)*6.28318530718/65535;
  double a=.17,b=.071;
  // Analytic integral prevents weather textures jumping when gust speed changes.
  s.gust=1+t->gust*(.42*sin(seconds*a+phase)+.22*sin(seconds*b+phase*1.7));
  s.drift=seconds+t->gust*(.42*(cos(phase)-cos(seconds*a+phase))/a+
                         .22*(cos(phase*1.7)-cos(seconds*b+phase*1.7))/b);
  double morning=smooth(3.5,5,hour)*(1-smooth(8,11,hour));
  double cool=1-smooth(14,28,c),damp=smooth(68,94,rh);
  s.mist=t->mist*morning*damp*(.35+.65*cool)*(1-.7*rain)*(1-smooth(.35,1.4,wind));
  s.haze=t->haze*(.25+.75*cloud)*(.35+.65*damp)*(1-.65*rain);
  double low_sun=exp(-pow((hour-7.3)/1.25,2))+exp(-pow((hour-17.2)/1.25,2));
  s.golden=t->golden*bound(low_sun,0,1)*(1-.8*cloud)*(1-.85*rain);
  return s;
}
double atmosphere_basin(const float *h,int w,int n,double x,double z){
  if(!h||w<3||n<3||w>256||n>256||!isfinite(x)||!isfinite(z))return 0;
  // Terrain::buildGrid (1000a130) centers world X/Z about half the map extent.
  int ix=(int)floor(x/100+w*.5),iz=(int)floor(z/100+n*.5);
  if(ix<1||iz<1||ix>=w-1||iz>=n-1)return 0;
  double mid=h[iz*w+ix],sum=0,lo=1e12,hi=-1e12;
  if(!isfinite(mid))return 0;
  for(int j=-1;j<=1;j++)for(int i=-1;i<=1;i++)if(i||j){
    double v=h[(iz+j)*w+ix+i];if(!isfinite(v))return 0;sum+=v;if(v<lo)lo=v;if(v>hi)hi=v;
  }
  double concavity=bound((sum/8-mid)/18,0,1),slope=bound((hi-lo)/70,0,1);
  return bound((.18+.82*concavity)*(1-.9*slope),0,1);
}
void atmosphere_receivers(uint8_t *mask,int w,int h,const float *v,const float *world,
  const int32_t *b,int nb,int nv,int tw,int th,const float *heights,int mw,int mh){
  if(!mask||!v||!world||!b||!heights||w<1||h<1||w>4096||h>4096||tw<1||th<1||
     nb<1||nb>65536||nv<3||nv>2000000)return;
  for(int k=0;k<nb;k++){
    int first=b[k*5],count=b[k*5+1];
    if(first<0||count<3||count>nv||first>nv-count||(b[k*5+4]&8))continue;
    for(int i=first;i+2<first+count;i+=3){
      double x[3],y[3],low[3];int ok=1;
      for(int j=0;j<3;j++){
        x[j]=v[(i+j)*8]*(double)w/tw;y[j]=(th-v[(i+j)*8+1])*(double)h/th;
        if(!isfinite(x[j])||!isfinite(y[j])||fabs(x[j])>1e7||fabs(y[j])>1e7)ok=0;
        low[j]=atmosphere_basin(heights,mw,mh,world[(i+j)*3],world[(i+j)*3+2]);
      }
      double den=(y[1]-y[2])*(x[0]-x[2])+(x[2]-x[1])*(y[0]-y[2]);
      if(!ok||fabs(den)<1e-6)continue;
      int left=(int)bound(floor(fmin(x[0],fmin(x[1],x[2]))),0,w-1),right=(int)bound(ceil(fmax(x[0],fmax(x[1],x[2]))),0,w-1);
      int top=(int)bound(floor(fmin(y[0],fmin(y[1],y[2]))),0,h-1),bottom=(int)bound(ceil(fmax(y[0],fmax(y[1],y[2]))),0,h-1);
      for(int yy=top;yy<=bottom;yy++)for(int xx=left;xx<=right;xx++){
        uint8_t *p=mask+yy*w+xx;if(*p!=127&&(*p<128||*p>143))continue;
        double a=((y[1]-y[2])*(xx+.5-x[2])+(x[2]-x[1])*(yy+.5-y[2]))/den;
        double c=((y[2]-y[0])*(xx+.5-x[2])+(x[0]-x[2])*(yy+.5-y[2]))/den,d=1-a-c;
        if(a<0||c<0||d<0)continue;
        *p=(uint8_t)(127+lround(16*bound(a*low[0]+c*low[1]+d*low[2],0,1)));
      }
    }
  }
}
