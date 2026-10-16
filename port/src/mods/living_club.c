// Living Club: event-driven narrative, original simulation RNG untouched.
#include "mods/living_club.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static uint32_t mix(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
  x *= 0x846ca68bu; return x ^ (x >> 16);
}
static int clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static int integer(const JsonVal *v, int lo, int hi, int fallback) {
  return json_is_int(v) && v->n >= lo && v->n <= hi ? (int)v->n : fallback;
}
static int number(const JsonVal *o, const char *k, int lo, int hi, int fallback) {
  return integer(json_get(o, k), lo, hi, fallback);
}
static const char *string(const JsonVal *o, const char *k) {
  const JsonVal *v = json_get(o, k); return v && v->type == JS_STR ? v->s : "";
}
static const JsonVal *item(const JsonVal *a, int i) {
  return a && a->type == JS_ARR && i >= 0 && i < a->len ? a->items[i] : NULL;
}
static int tune(const LcContent *c, const char *key, int fallback) {
  return number(c->tuning, key, 0, 1000000, fallback);
}
static int bank_valid(const JsonVal *v) {
  if (!v || v->type != JS_ARR || v->len < 3 || v->len > 128) return 0;
  for (int i=0; i<v->len; i++) {
    const JsonVal *s=v->items[i];
    if (!s || s->type!=JS_STR || !*s->s || strlen(s->s)>110) return 0;
    for (const unsigned char *p=(const unsigned char *)s->s; *p; p++) if (*p<32 || *p>126) return 0;
  }
  return 1;
}
static const char *groups[] = {"good","bad","hazard","scenery","delay","needs","comfort","strategy","social","practice","arrival","return"};
int lc_content_load(LcContent *c, const char *path, char *err, size_t n) {
  memset(c,0,sizeof *c);
  c->root=json_load(path,err,n); if (!c->root) return 0;
  c->personas=json_get(c->root,"personalities"); c->stories=json_get(c->root,"stories");
  c->tuning=json_get(c->root,"tuning");
  int ok=number(c->root,"version",1,1,0)==1 && c->personas && c->personas->type==JS_ARR &&
    c->personas->len==LC_PERSONAS && c->stories && c->stories->type==JS_ARR &&
    c->stories->len>0 && c->stories->len<=LC_ARCS && c->tuning && c->tuning->type==JS_OBJ;
  for (int i=0; ok && i<LC_PERSONAS; i++) {
    const JsonVal *p=item(c->personas,i), *b=json_get(p,"lines"), *a=json_get(p,"story_weights");
    ok=*string(p,"id") && *string(p,"name") && strlen(string(p,"name"))<35 &&
      a && a->type==JS_ARR && a->len==c->stories->len;
    for (int j=0; ok && j<i; j++) ok=strcmp(string(p,"id"),string(item(c->personas,j),"id"))!=0;
    for (int j=0; ok && j<a->len; j++) ok=integer(a->items[j],1,100,0)>0;
    for (int j=0; ok && j<12; j++) ok=bank_valid(json_get(b,groups[j]));
    const JsonVal *weights=json_get(p,"reactions");
    for (int j=0; ok && j<12; j++) ok=number(weights,groups[j],-2,2,99)!=99;
    ok=ok && number(p,"trait_mask",0,31,-1)>=0;
  }
  for (int i=0; ok && i<c->stories->len; i++) {
    const JsonVal *s=item(c->stories,i);
    ok=*string(s,"id") && *string(s,"title") && *string(s,"trigger") &&
      number(s,"goal",1,100,0)>0;
    for (int j=0; ok && j<i; j++) ok=strcmp(string(s,"id"),string(item(c->stories,j),"id"))!=0;
    const char *stages[]={"opening","return","progress","success","setback","closure"};
    for (int j=0; ok && j<6; j++) ok=bank_valid(json_get(s,stages[j]));
  }
  const JsonVal *traits=json_get(c->root,"traits");
  for (int i=0; ok && i<5; i++) {
    const JsonVal *t=item(traits,i);
    ok=bank_valid(json_get(t,"set")) && bank_valid(json_get(t,"clear"));
  }
  if (!ok) { snprintf(err,n,"%s: invalid Living Club content (25 unique personas, reactions and story banks required)",path); lc_content_free(c); return 0; }
  c->seed=(uint32_t)tune(c,"seed",265443);
  return 1;
}
void lc_content_free(LcContent *c) { json_free(c->root); memset(c,0,sizeof *c); }
void lc_reset(LcWorld *w, uint32_t seed) { memset(w,0,sizeof *w); w->seed=seed; }
static const JsonVal *persona(const LcContent *c, const LcPerson *p) { return item(c->personas,p->persona); }
const char *lc_persona_name(const LcContent *c, const LcPerson *p) { return p ? string(persona(c,p),"name") : ""; }
LcPerson *lc_person(LcWorld *w, const LcContent *c, int profile, int traits) {
  if (profile<0 || profile>=LC_PROFILES || !c->root) return NULL;
  LcPerson *p=&w->people[profile];
  if (!p->used) {
    memset(p,0,sizeof *p); p->used=1; p->id=profile; p->traits=traits&31;
    p->best_round=0; p->arc=-1; p->last_partner=p->arc_partner=-1;
    p->confidence=tune(c,"initial_confidence",50); p->resolve=tune(c,"initial_resolve",50);
    // Weighted by all five original traits, never by skill, gender or wealth.
    int weights[LC_PERSONAS], total=0;
    for (int i=0; i<LC_PERSONAS; i++) {
      int bits=(traits ^ number(item(c->personas,i),"trait_mask",0,31,0))&31, matches=5;
      while (bits) { matches-=bits&1; bits>>=1; }
      total += weights[i]=1+matches*tune(c,"trait_affinity",2);
    }
    unsigned roll=mix((uint32_t)profile ^ w->seed)%(unsigned)total;
    for (int i=0; i<LC_PERSONAS; i++) { if (roll<(unsigned)weights[i]) { p->persona=i; break; } roll-=weights[i]; }
    for (int i=0;i<LC_BONDS;i++) p->bonds[i].partner=-1;
  }
  p->traits=traits&31; // Player edits remain authoritative, identity stays stable.
  return p;
}
static void journal(LcPerson *p, const char *line) {
  char entry[LC_TEXT];snprintf(entry,sizeof entry,"Visit %d: %s",p->visits,line);
  if (!*line || !strcmp(p->journal[0],entry)) return;
  memmove(p->journal[1],p->journal[0],(LC_JOURNAL-1)*LC_TEXT);
  snprintf(p->journal[0],LC_TEXT,"%s",entry);
}
static void expand(const char *s, const LcContext *x, const LcPerson *p, char *out, size_t n) {
  if (!n) return;
  char hole[16],visit[16]; snprintf(hole,sizeof hole,"%d",x->hole); snprintf(visit,sizeof visit,"%d",p->visits);
  const char *keys[]={"{hole}","{partner}","{name}","{visit}"};
  const char *vals[]={hole,x->partner_name&&*x->partner_name?x->partner_name:"my partner",x->name&&*x->name?x->name:"Golfer",visit};
  size_t used=0;
  while (*s && used+1<n) {
    int matched=0;
    for (int i=0;i<4;i++) if (!strncmp(s,keys[i],strlen(keys[i]))) {
      size_t len=strlen(vals[i]); if (len>n-used-1) len=n-used-1;
      memcpy(out+used,vals[i],len); used+=len; s+=strlen(keys[i]); matched=1; break;
    }
    if (!matched) out[used++]=*s++;
  }
  out[used]=0;
}
static uint32_t hashstr(const char *s) { uint32_t h=2166136261u; while (*s) { h^=(unsigned char)*s++; h*=16777619u; } return h; }
static void choose(LcPerson *p, const JsonVal *bank, int event, const LcContext *x, char *out, size_t n) {
  if (!n) return; *out=0;
  if (!bank || bank->type!=JS_ARR || !bank->len) return;
  uint32_t h=mix((uint32_t)p->id*7919u+p->serial*104729u+(uint32_t)p->visits*15485863u+(uint32_t)x->hole*31u);
  int start=(int)(h%(uint32_t)bank->len), e=clamp(event,0,LC_EVENTS-1);
  for (int j=0;j<bank->len;j++) {
    const JsonVal *s=item(bank,(start+j)%bank->len);
    if (!s || s->type!=JS_STR) continue;
    uint32_t fingerprint=hashstr(s->s);
    if (bank->len>1 && (fingerprint==p->last_hash[e] || fingerprint==p->last_line_hash)) continue;
    p->last_hash[e]=p->last_line_hash=fingerprint; expand(s->s,x,p,out,n); return;
  }
}
static const char *group(const LcContent *c, int event) {
  char key[16]; snprintf(key,sizeof key,"%d",event);
  const char *g=string(json_get(c->root,"event_groups"),key); return *g?g:NULL;
}
int lc_reaction(const LcContent *c,const LcPerson *p,int event,int original) {
  const char *g=group(c,event);if(!p || !g)return original;
  // Neutral shot setup cannot create mood or hole-fun rewards. Preserve large
  // original reactions too; this is a small sensitivity, not a replacement cap.
  if(!original || event<0 || event>=LC_EVENTS || !p->seen[event] ||
     p->seen[event] % clamp(tune(c,"reaction_interval",4),1,1000))return original;
  int delta=number(json_get(persona(c,p),"reactions"),g,-2,2,0);
  delta=clamp(delta,-1,1);
  int result=original==INT_MIN||original==INT_MAX?original:original+delta;
  if(original<0 && result>=0)result=-1;
  if(original>0 && result<=0)result=1;
  return result;
}
static LcBond *bond(LcPerson *p,int partner,int create) {
  if (partner<0 || partner>=LC_PROFILES || partner==p->id) return NULL;
  int lowest=0;
  for (int i=0;i<LC_BONDS;i++) {
    if (p->bonds[i].partner==partner) return &p->bonds[i];
    if (p->bonds[i].partner<0) lowest=i;
    else if (p->bonds[lowest].partner>=0 && p->bonds[i].rounds<p->bonds[lowest].rounds) lowest=i;
  }
  if (!create) return NULL;
  memset(&p->bonds[lowest],0,sizeof p->bonds[lowest]); p->bonds[lowest].partner=partner;
  return &p->bonds[lowest];
}
static void story_line(LcPerson *p,const LcContent *c,const char *stage,const LcContext *x,char *out,size_t n) {
  choose(p,json_get(item(c->stories,p->arc),stage),x->event,x,out,n); journal(p,out);
}
static void close_arc(LcPerson *p,const LcContent *c,const LcContext *x,char *out,size_t n) {
  p->stage=4;p->ending_kind=2;p->endings=clamp(p->endings+1,0,1000000);p->arc_visit=p->visits;
  story_line(p,c,"closure",x,out,n);
}
void lc_visit(LcWorld *w,const LcContent *c,const LcContext *x,char *out,size_t n) {
  if (n) *out=0;
  LcPerson *p=lc_person(w,c,x->profile,x->traits); if (!p) return;
  if (p->visits<1000000) p->visits++;
  p->visit_good=p->visit_bad=0; p->serial++; p->last_partner=x->partner;
  LcBond *b=bond(p,x->partner,1);
  if (b) { b->rounds=clamp(b->rounds+1,0,1000000); b->warmth=clamp(b->warmth+1,-100,100); }
  int partner_arc=p->arc>=0 && !strcmp(string(item(c->stories,p->arc),"trigger"),"partner");
  if(p->arc>=0 && p->stage!=4 &&
     ((partner_arc && x->partner!=p->arc_partner && p->visits-p->arc_visit>=tune(c,"missing_partner_visits",5)) ||
      p->visits-p->arc_visit>=tune(c,"story_max_visits",8))) {
    close_arc(p,c,x,out,n);return;
  }
  if (partner_arc && x->partner!=p->arc_partner && p->stage!=4) {
    choose(p,json_get(json_get(persona(c,p),"lines"),p->visits==1?"arrival":"return"),61,x,out,n);
  } else if (p->arc>=0 && p->stage>0 && p->stage<4 && p->visits>p->arc_visit) {
    p->stage=2; story_line(p,c,"return",x,out,n);
  } else if (p->stage==5) {
    p->stage=2; story_line(p,c,"return",x,out,n);
  } else {
    const char *key=p->visits==1?"arrival":"return";
    choose(p,json_get(json_get(persona(c,p),"lines"),key),61,x,out,n);
    if (p->visits>1) journal(p,out);
  }
}
static int eligible(const char *trigger,const char *g,const LcPerson *p,const LcBond *b,int hole) {
  if (!strcmp(trigger,"trouble")) return !strcmp(g,"bad") || !strcmp(g,"hazard");
  if (!strcmp(trigger,"partner")) return b && b->rounds>=2;
  if (!strcmp(trigger,"scenery")) return !strcmp(g,"scenery");
  if (!strcmp(trigger,"practice")) return !strcmp(g,"practice");
  if (!strcmp(trigger,"strategy")) return !strcmp(g,"strategy");
  if (!strcmp(trigger,"patience")) return !strcmp(g,"delay");
  if (!strcmp(trigger,"comfort")) return !strcmp(g,"comfort");
  if (!strcmp(trigger,"success")) return !strcmp(g,"good");
  if (!strcmp(trigger,"regular")) return p->visits>=3;
  if (!strcmp(trigger,"nemesis")) return hole>0 && p->troubles[hole]>=2;
  return 0;
}
static int progresses(const char *trigger,const char *g,const LcPerson *p,const LcBond *b,int hole) {
  if (!strcmp(trigger,"partner")) return b && b->rounds>=2 && !strcmp(g,"good");
  if (!strcmp(trigger,"scenery")) return !strcmp(g,"scenery");
  if (!strcmp(trigger,"practice")) return !strcmp(g,"practice") || !strcmp(g,"good");
  if (!strcmp(trigger,"strategy")) return !strcmp(g,"strategy") || !strcmp(g,"good");
  if (!strcmp(trigger,"comfort")) return !strcmp(g,"comfort");
  if (!strcmp(trigger,"nemesis")) return !strcmp(g,"good") && hole==p->arc_hole;
  return !strcmp(g,"good");
}
void lc_event(LcWorld *w,const LcContent *c,const LcContext *x,char *out,size_t n) {
  if (n) *out=0;
  if (x->event<0 || x->event>=LC_EVENTS) return;
  const char *g=group(c,x->event); if (!g) return; // Original story speech is never replaced.
  LcPerson *p=lc_person(w,c,x->profile,x->traits); if (!p) return;
  int e=x->event, hole=x->hole>0 && x->hole<LC_HOLES?x->hole:0;
  p->serial++; p->seen[e]=clamp(p->seen[e]+1,0,1000000);
  int good=!strcmp(g,"good"), bad=!strcmp(g,"bad") || !strcmp(g,"hazard");
  p->good=clamp(p->good+good,0,1000000); p->bad=clamp(p->bad+bad,0,1000000);
  p->visit_good=clamp(p->visit_good+good,0,1000000); p->visit_bad=clamp(p->visit_bad+bad,0,1000000);
  int reaction=number(json_get(persona(c,p),"reactions"),g,-2,2,0);
  p->confidence=clamp(p->confidence+(good-bad)*tune(c,"confidence_step",2)+(good||bad?reaction:0),0,100);
  p->resolve=clamp(p->resolve+(bad && reaction>=0?1:good?1:bad?-1:0),0,100);
  int old_trouble=hole?p->troubles[hole]:0;
  if (hole && bad) {
    p->troubles[hole]=clamp(p->troubles[hole]+1,0,1000000);
    if (!p->nemesis || p->troubles[hole]>p->troubles[p->nemesis]) p->nemesis=hole;
  }
  if (hole && good) {
    p->triumphs[hole]=clamp(p->triumphs[hole]+1,0,1000000);
    if (!p->favorite || p->triumphs[hole]>p->triumphs[p->favorite]) p->favorite=hole;
  }
  LcBond *b=bond(p,x->partner,1);
  if (b) {
    b->shared_good=clamp(b->shared_good+good,0,1000000); b->shared_bad=clamp(b->shared_bad+bad,0,1000000);
    int kind=(p->traits&16)?1:-1;
    b->warmth=clamp(b->warmth+(good?1:bad?kind:0),-100,100);
  }
  // A completed arc rests for a configured number of visits, then another can emerge.
  int previous_arc=-1;
  if (p->stage==4 && p->visits-p->arc_visit>=tune(c,"story_rest_visits",2)) { previous_arc=p->arc;p->arc=-1; p->stage=0; }
  if (p->arc<0 && p->visits>=1) {
    int weights[LC_ARCS]={0}, total=0;
    const JsonVal *aff=json_get(persona(c,p),"story_weights");
    for (int i=0;i<c->stories->len;i++) if (eligible(string(item(c->stories,i),"trigger"),g,p,b,hole)) {
      weights[i]=integer(item(aff,i),1,100,1); total+=weights[i];
    }
    if(previous_arc>=0 && total>weights[previous_arc]){total-=weights[previous_arc];weights[previous_arc]=0;}
    if (total) {
      unsigned roll=mix(p->serial ^ w->seed ^ (uint32_t)p->id)%(unsigned)total;
      for (int i=0;i<c->stories->len;i++) { if (roll<(unsigned)weights[i]) { p->arc=i; break; } roll-=weights[i]; }
      p->stage=1; p->arc_visit=p->visits; p->arc_good=p->arc_bad=p->arc_setbacks=p->ending_kind=0;
      p->arc_partner=x->partner; p->arc_hole=hole;
      p->arc_progress_visit=p->arc_setback_visit=0;
      story_line(p,c,"opening",x,out,n); return;
    }
  } else if (p->arc>=0 && p->stage!=4 && p->visits>p->arc_visit) {
    const JsonVal *arc=item(c->stories,p->arc);
    const char *trigger=string(arc,"trigger");
    int matching_partner=strcmp(trigger,"partner") || x->partner==p->arc_partner;
    int matching_hole=strcmp(trigger,"nemesis") || hole==p->arc_hole;
    if (matching_partner && matching_hole && progresses(trigger,g,p,b,hole) && p->arc_progress_visit!=p->visits) {
      p->arc_progress_visit=p->visits;
      p->arc_good=clamp(p->arc_good+1,0,1000000);
      if (p->arc_good>=number(arc,"goal",1,100,3) && p->visits-p->arc_visit>=tune(c,"story_min_visits",2)) {
        p->stage=4; p->ending_kind=1; p->endings=clamp(p->endings+1,0,1000000); p->arc_visit=p->visits;
        story_line(p,c,"success",x,out,n); return;
      }
      if (p->stage!=3) { p->stage=3; story_line(p,c,"progress",x,out,n); return; }
    } else if (matching_partner && matching_hole && bad && ++p->arc_bad>=tune(c,"setback_after",3) &&
               p->visit_bad>=p->visit_good+tune(c,"setback_margin",3) && p->arc_setback_visit!=p->visits) {
      p->arc_setback_visit=p->visits;
      p->stage=5; p->setbacks=clamp(p->setbacks+1,0,1000000);p->arc_setbacks++; p->arc_bad=0;
      if(p->arc_setbacks>=tune(c,"story_setbacks_limit",3)){close_arc(p,c,x,out,n);return;}
      story_line(p,c,"setback",x,out,n); return;
    }
  }
  const JsonVal *context=json_get(c->root,"context");
  const JsonVal *bank=NULL;
  if (good && old_trouble>=tune(c,"nemesis_after",2)) bank=json_get(context,"redemption");
  else if (bad && old_trouble>=tune(c,"nemesis_after",2)) bank=json_get(context,"nemesis");
  else if (b && b->rounds>=tune(c,"familiar_after",3) && p->serial%4==0)
    bank=json_get(context,b->warmth>=0?"familiar":"rival");
  else if (p->serial%5==0) {
    // Original traits add a second voice layer, chosen only for relevant contexts.
    int trait=!strcmp(g,"delay")?2:!strcmp(g,"social")?1:!strcmp(g,"scenery")?0:bad?3:4;
    bank=json_get(item(json_get(c->root,"traits"),trait),(p->traits&(1<<trait))?"set":"clear");
  }
  if (!bank) bank=json_get(json_get(persona(c,p),"lines"),g);
  choose(p,bank,e,x,out,n);
}
void lc_leave(LcWorld *w,const LcContent *c,int profile,int score,int completed) {
  if (profile<0 || profile>=LC_PROFILES || !w->people[profile].used) return;
  LcPerson *p=&w->people[profile]; p->rounds=clamp(p->rounds+1,0,1000000);
  if (completed && score>0) {
    p->completed=clamp(p->completed+1,0,1000000);
    if (!p->best_round || score<p->best_round) p->best_round=score;
  }
  if (!completed && p->stage>0 && p->stage!=4 && p->arc_setback_visit!=p->visits &&
      p->visit_bad>=tune(c,"setback_after",3) && p->visit_bad>=p->visit_good+tune(c,"setback_margin",3)) {
    p->arc_setback_visit=p->visits;
    p->stage=5; p->setbacks=clamp(p->setbacks+1,0,1000000);p->arc_setbacks++;
    LcContext x={profile,p->last_partner,p->traits,p->nemesis,35,NULL,NULL};
    char line[LC_TEXT];
    if(p->arc_setbacks>=tune(c,"story_setbacks_limit",3))close_arc(p,c,&x,line,sizeof line);
    else story_line(p,c,"setback",&x,line,sizeof line);
  }
}
void lc_summary(const LcContent *c,const LcPerson *p,char *out,size_t n) {
  if (!p) { if(n)*out=0; return; }
  const char *title=p->arc>=0?string(item(c->stories,p->arc),"title"):"Finding their story";
  const char *stage=p->stage==4?"resolved":p->stage==5?"setback":p->stage==3?"progress":p->stage==2?"returning":"beginning";
  snprintf(out,n,"%s: %s. %s",title,stage,p->journal[0]);
}

// A portable, versioned snapshot. Original .sve bytes are never changed.
#define SCALARS(X) X(visits) X(rounds) X(completed) X(confidence) X(resolve) X(good) X(bad) X(visit_good) X(visit_bad) X(best_round) X(nemesis) X(favorite) X(stage) X(arc_visit) X(arc_good) X(arc_bad) X(arc_partner) X(arc_hole) X(arc_setbacks) X(ending_kind) X(endings) X(setbacks) X(last_partner)
static JsonVal *ints(const int *p,int n) { JsonVal *a=json_new(JS_ARR); for(int i=0;i<n;i++) json_push(a,json_num(p[i])); return a; }
JsonVal *lc_snapshot(const LcWorld *w,const LcContent *c) {
  JsonVal *root=json_new(JS_OBJ),*people=json_new(JS_ARR);
  json_set(root,"version",json_num(1)); json_set(root,"seed",json_num(w->seed));
  for(int i=0;i<LC_PROFILES;i++) {
    const LcPerson *p=&w->people[i]; if(!p->used)continue;
    JsonVal *v=json_new(JS_OBJ); json_set(v,"id",json_num(i));
    json_set(v,"persona",json_str(string(persona(c,p),"id"))); json_set(v,"traits",json_num(p->traits));
    json_set(v,"arc",json_str(p->arc>=0?string(item(c->stories,p->arc),"id"):""));
    json_set(v,"serial",json_num(p->serial));
    json_set(v,"last_line_hash",json_num(p->last_line_hash));
    json_set(v,"arc_progress_visit",json_num(p->arc_progress_visit));
    json_set(v,"arc_setback_visit",json_num(p->arc_setback_visit));
#define WRITE(k) json_set(v,#k,json_num(p->k));
    SCALARS(WRITE)
#undef WRITE
    json_set(v,"troubles",ints(p->troubles,LC_HOLES)); json_set(v,"triumphs",ints(p->triumphs,LC_HOLES));
    json_set(v,"seen",ints(p->seen,LC_EVENTS));
    JsonVal *hashes=json_new(JS_ARR),*j=json_new(JS_ARR),*b=json_new(JS_ARR);
    for(int k=0;k<LC_EVENTS;k++)json_push(hashes,json_num(p->last_hash[k]));
    for(int k=0;k<LC_JOURNAL;k++)json_push(j,json_str(p->journal[k]));
    for(int k=0;k<LC_BONDS;k++) { const LcBond *bond=&p->bonds[k]; int a[]={bond->partner,bond->rounds,bond->warmth,bond->shared_good,bond->shared_bad}; json_push(b,ints(a,5)); }
    json_set(v,"hashes",hashes);json_set(v,"journal",j);json_set(v,"bonds",b);json_push(people,v);
  }
  json_set(root,"people",people);return root;
}
static int read_ints(const JsonVal *a,int *out,int count,int lo,int hi) {
  if(!a || a->type!=JS_ARR || a->len!=count)return 0;
  for(int i=0;i<count;i++) { int x=integer(item(a,i),lo,hi,INT_MIN); if(x==INT_MIN)return 0; out[i]=x; } return 1;
}
static int find_id(const JsonVal *a,const char *id) { for(int i=0;i<a->len;i++) if(!strcmp(string(item(a,i),"id"),id))return i;return -1; }
static int restore(LcWorld *w,const LcContent *c,const JsonVal *root) {
  const JsonVal *a=json_get(root,"people"),*seed=json_get(root,"seed");
  if(number(root,"version",1,1,0)!=1 || !a || a->type!=JS_ARR || a->len>LC_PROFILES || !json_is_int(seed) || seed->n<0 || seed->n>UINT32_MAX)return 0;
  lc_reset(w,(uint32_t)seed->n);
  for(int i=0;i<a->len;i++) {
    const JsonVal *v=item(a,i);int id=number(v,"id",0,LC_PROFILES-1,-1); if(id<0 || w->people[id].used)return 0;
    LcPerson *p=&w->people[id];p->used=1;p->id=id;
    p->persona=find_id(c->personas,string(v,"persona")); if(p->persona<0)return 0;
    p->traits=number(v,"traits",0,31,-1);if(p->traits<0)return 0;
    const char *arc=string(v,"arc");p->arc=*arc?find_id(c->stories,arc):-1;if(*arc && p->arc<0)return 0;
    const JsonVal *serial=json_get(v,"serial");if(!json_is_int(serial)||serial->n<0||serial->n>UINT32_MAX)return 0;p->serial=(uint32_t)serial->n;
    const JsonVal *last=json_get(v,"last_line_hash");
    if(last && (!json_is_int(last)||last->n<0||last->n>UINT32_MAX))return 0;
    p->last_line_hash=last?(uint32_t)last->n:0;
#define READ(k) p->k=number(v,#k,(!strcmp(#k,"last_partner")||!strcmp(#k,"arc_partner"))?-1:0,1000000,INT_MIN); if(p->k==INT_MIN)return 0;
    SCALARS(READ)
#undef READ
    // Earlier version-1 companions lack these counters. Conservatively reserve
    // the current visit so a mid-round upgrade cannot grant extra story beats.
    p->arc_progress_visit=number(v,"arc_progress_visit",0,p->visits,json_get(v,"arc_progress_visit")?INT_MIN:p->visits);
    p->arc_setback_visit=number(v,"arc_setback_visit",0,p->visits,json_get(v,"arc_setback_visit")?INT_MIN:p->visits);
    if(p->arc_progress_visit==INT_MIN || p->arc_setback_visit==INT_MIN)return 0;
    if(p->confidence>100||p->resolve>100||p->stage>5||p->ending_kind>2||p->nemesis>=LC_HOLES||p->favorite>=LC_HOLES||p->arc_hole>=LC_HOLES||p->last_partner>=LC_PROFILES||p->arc_partner>=LC_PROFILES||p->arc_visit>p->visits||((p->arc<0)!=(p->stage==0)))return 0;
    if(!read_ints(json_get(v,"troubles"),p->troubles,LC_HOLES,0,1000000)||!read_ints(json_get(v,"triumphs"),p->triumphs,LC_HOLES,0,1000000)||!read_ints(json_get(v,"seen"),p->seen,LC_EVENTS,0,1000000))return 0;
    const JsonVal *h=json_get(v,"hashes"),*j=json_get(v,"journal"),*b=json_get(v,"bonds");
    if(!h||h->type!=JS_ARR||h->len!=LC_EVENTS||!j||j->type!=JS_ARR||j->len!=LC_JOURNAL||!b||b->type!=JS_ARR||b->len!=LC_BONDS)return 0;
    for(int k=0;k<LC_EVENTS;k++){ const JsonVal *x=item(h,k);if(!json_is_int(x)||x->n<0||x->n>UINT32_MAX)return 0;p->last_hash[k]=(uint32_t)x->n; }
    for(int k=0;k<LC_JOURNAL;k++){const JsonVal *s=item(j,k);if(!s||s->type!=JS_STR||strlen(s->s)>=LC_TEXT)return 0;snprintf(p->journal[k],LC_TEXT,"%s",s->s);}
    for(int k=0;k<LC_BONDS;k++){int z[5];if(!read_ints(item(b,k),z,5,-100,1000000)||z[0]<-1||z[0]>=LC_PROFILES||z[0]==id||z[1]<0||z[2]<-100||z[2]>100||z[3]<0||z[4]<0)return 0;
      for(int t=0;t<k;t++)if(z[0]>=0&&p->bonds[t].partner==z[0])return 0;
      p->bonds[k]=(LcBond){z[0],z[1],z[2],z[3],z[4]};}
  }return 1;
}
static uint32_t crc(uint32_t c,const void *data,size_t n) {
  const unsigned char *p=data;c=~c;while(n--){c^=*p++;for(int k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(int)(c&1));}return ~c;
}
static int file_crc(const char *path,uint32_t *size,uint32_t *sum) {
  FILE *f=fopen(path,"rb");if(!f)return 0;unsigned char buf[8192];size_t k;*size=*sum=0;
  while((k=fread(buf,1,sizeof buf,f))){if(k>UINT32_MAX-*size){fclose(f);return 0;}*size+=(uint32_t)k;*sum=crc(*sum,buf,k);}int ok=!ferror(f);if(fclose(f))ok=0;return ok;
}
int lc_save(const LcWorld *w,const LcContent *c,const char *save,char *err,size_t n) {
  uint32_t size,sum;if(!file_crc(save,&size,&sum)){snprintf(err,n,"cannot checksum save");return 0;}
  JsonVal *state=lc_snapshot(w,c);char *payload=json_write_compact(state);json_free(state);if(!payload)return 0;
  char side[1024],temp[1040];snprintf(side,sizeof side,"%s.living-club.json",save);snprintf(temp,sizeof temp,"%s.tmp",side);
  FILE *f=fopen(temp,"wb");int ok=0;
  if(f){ok=fprintf(f,"{\"format\":\"simgolf-living-club\",\"version\":1,\"save_size\":%u,\"save_crc\":%u,\"payload_crc\":%u,\"state\":%s}\n",size,sum,crc(0,payload,strlen(payload)),payload)>0;if(fclose(f))ok=0;}free(payload);
#ifdef _WIN32
  extern int __stdcall MoveFileExA(const char *,const char *,unsigned long);
  if(ok)ok=MoveFileExA(temp,side,9)!=0;
#else
  if(ok)ok=rename(temp,side)==0;
#endif
  if(!ok)snprintf(err,n,"cannot finish narrative sidecar; previous history kept");return ok;
}
int lc_load(LcWorld *w,const LcContent *c,const char *save,char *err,size_t n) {
  // Missing, corrupt and wrong-save histories always start clean.
  lc_reset(w,c->seed);
  char side[1024];snprintf(side,sizeof side,"%s.living-club.json",save);
  FILE *f=fopen(side,"rb");if(!f){snprintf(err,n,"legacy save: no Living Club history");return 0;}
  if(fseek(f,0,SEEK_END)||ftell(f)>8*1024*1024){fclose(f);snprintf(err,n,"oversized history rejected");return 0;}fclose(f);
  JsonVal *root=json_load(side,err,n);if(!root)return 0;
  JsonVal *state=json_get(root,"state");char *payload=state?json_write_compact(state):NULL;
  uint32_t size=0,sum=0;const JsonVal *sz=json_get(root,"save_size"),*sc=json_get(root,"save_crc"),*pc=json_get(root,"payload_crc");
  int ok=!strcmp(string(root,"format"),"simgolf-living-club")&&number(root,"version",1,1,0)==1&&payload&&file_crc(save,&size,&sum)&&json_is_int(sz)&&sz->n==size&&json_is_int(sc)&&sc->n==sum&&json_is_int(pc)&&pc->n==crc(0,payload,strlen(payload));
  LcWorld *staging=calloc(1,sizeof *staging);if(!staging)ok=0;
  if(ok)ok=restore(staging,c,state);
  if(ok)*w=*staging;else snprintf(err,n,"stale or invalid Living Club history rejected");
  free(staging);free(payload);json_free(root);return ok;
}
