#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

enum { N = 1 << 16, LIVE = 900, BUDGET = 853, RUNS = 100 };
typedef struct { unsigned used; int resident; } Tex;
static Tex slots[N];
static Tex *order[N];
static int old_pick[LIVE], new_pick[LIVE];

static int cmp(const void *a,const void *b){
 const Tex *x=*(Tex *const *)a,*y=*(Tex *const *)b;
 if(x->used!=y->used)return x->used<y->used?-1:1;
 return x<y?-1:x!=y;
}
static void reset(void){
 for(int i=0;i<N;i++){slots[i].resident=0;slots[i].used=0;}
 uint32_t seed=0x77a4u;
 for(int i=0;i<LIVE;i++){
  int index=(i*71)%N;
  seed=seed*1664525u+1013904223u;
  slots[index].resident=1;slots[index].used=seed%1000u;
 }
}
static int old_run(void){
 int n=LIVE,p=0;
 while(n>BUDGET){
  Tex *old=NULL;
  for(int i=0;i<N;i++)if(slots[i].resident&&(!old||slots[i].used<old->used))old=&slots[i];
  assert(old);old_pick[p++]=(int)(old-slots);old->resident=0;n--;
 }
 return p;
}
static int new_run(void){
 int n=0,p=0;for(int i=0;i<N;i++)if(slots[i].resident)order[n++]=&slots[i];
 qsort(order,(size_t)n,sizeof *order,cmp);
 for(int i=0;i<n&&n-p>BUDGET;i++){
  new_pick[p++]=(int)(order[i]-slots);order[i]->resident=0;
 }
 return p;
}
static double ms(LARGE_INTEGER a,LARGE_INTEGER b,LARGE_INTEGER freq){return (double)(b.QuadPart-a.QuadPart)*1000.0/freq.QuadPart;}
int main(void){
 LARGE_INTEGER a,b,c,f;QueryPerformanceFrequency(&f);
 QueryPerformanceCounter(&a);
 for(int i=0;i<RUNS;i++){reset();assert(old_run()==LIVE-BUDGET);}
 QueryPerformanceCounter(&b);
 for(int i=0;i<RUNS;i++){reset();assert(new_run()==LIVE-BUDGET);}
 QueryPerformanceCounter(&c);
 reset();old_run();reset();new_run();
 for(int i=0;i<LIVE-BUDGET;i++)assert(old_pick[i]==new_pick[i]);
 printf("victims=%d exact_order=1 old_ms=%.3f new_ms=%.3f (%d runs)\n",LIVE-BUDGET,ms(a,b,f),ms(b,c,f),RUNS);
 return 0;
}
