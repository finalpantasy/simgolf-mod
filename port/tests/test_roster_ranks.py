"""Exercise the production report grid with original membership roster rows."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

from test_sga_report_values import function

ROOT = Path(__file__).resolve().parents[2]


class RosterRanks(unittest.TestCase):
    def test_rank_markers_and_labels(self):
        cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler unavailable")
        layout = (ROOT / "port/src/port/ui/ui_report_layouts.inc").read_text()
        source = r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/ui/ui_tokens.h"
#include "port/ui/ui_icons.h"
#include "port/ui/ui_layout.h"
#include "port/reports_kinds.h"
#define RP_TEXT 96
#define ROWS 64
enum { RP_IT_TEXT, RP_IT_CHIP, RP_A_CLICK, UI_HIT_BLOCK=1, UI_HIT_WHEEL=2, UI_BTN_PILL };
typedef struct { int unused; } UiCtx, UiDraw, SDL_Texture;
typedef struct { int type,x,y,w,h,big,group,art; unsigned short col; char s[RP_TEXT]; } RpItem;
typedef struct { int kind,n; char title[64]; RpItem it[64]; } RpSnap;
typedef struct { int kind,arg; } RpAction;
static UiTokens tokens;
static UiDraw draw;
static float scale=1;
static struct { UiIcon icon; UiRect r; } icons[128];
static struct { char s[192]; UiRect r; } texts[256];
static struct { UiColor color; UiRect r; } fills[128];
static int ni,nt,nf;
static const UiTokens *ui_tokens(UiCtx *c) { return &tokens; }
static UiDraw *ui_drawer(UiCtx *c) { return &draw; }
static float ui_s(UiCtx *c,float v) { return v*scale; }
static unsigned ui_id(const char *s) { return 0; }
static unsigned ui_idi(unsigned id,int i) { return i; }
static int same_text(const char *a,const char *b) { return !strcmp(a,b); }
static int numeric(const char *s) { return 0; }
static UiColor text_colour(const UiTokens *t,unsigned short col) { return t->text_primary; }
static UiColor uid_alpha(UiColor color,float alpha) { return color; }
static void uid_rrect(UiDraw *d,UiRect r,float radius,UiColor color) { fills[nf].r=r;fills[nf++].color=color; }
static void report_string(UiCtx *c,UiRect r,const char *s,int style,UiColor color,float align,unsigned id) {
  if(!*s)return;snprintf(texts[nt].s,sizeof texts[nt].s,"%s",s);texts[nt++].r=r;
}
static void ui_text_fit(UiCtx *c,UiRect r,int style,UiColor color,const char *s,float align) { report_string(c,r,s,style,color,align,0); }
static void ui_text_in(UiCtx *c,UiRect r,int style,UiColor color,const char *s,float align) { ui_text_fit(c,r,style,color,s,align); }
static void ui_icon(UiCtx *c,UiIcon icon,UiRect r,float size,UiColor color) { icons[ni].icon=icon;icons[ni++].r=r; }
static void report_stripe(UiCtx *c,UiRect r,int row,int total) {}
static float ui_vscroll_begin(UiCtx *c,unsigned id,UiRect r,float h,float step) { return 0; }
static void ui_vscroll_end(UiCtx *c) {}
static void ui_hit(UiCtx *c,UiRect r,int a,unsigned id,int flags) {}
static float ui_wheel(UiCtx *c,unsigned id) { return 0; }
static int ui_button(UiCtx *c,unsigned id,UiRect r,const char *s,UiIcon icon,int style,int flags) { return 0; }
static void report_control(const char *name,UiRect r) {}
static void uid_image(UiDraw *d,SDL_Texture *tx,void *src,UiRect r,int flags,UiColor color) {}
UiRect ui_inset(UiRect r,float l,float t,float right,float b) { return r; }
'''
        for signature in ["static int report_nearest(", "static int report_row(",
                          "static int report_course_legend(", "static int report_roster_rank(",
                          "static int report_grid("]:
            if signature in layout:
                source += function(layout, signature) + "\n"
        source += r'''
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL: %s\n",#c);return 1; } } while(0)
static void add(RpSnap *s,int x,int y,const char *text) {
  RpItem *it=&s->it[s->n++];it->x=x;it->y=y;snprintf(it->s,sizeof it->s,"%s",text);
}
int main(void) {
  const char *rank[]={"Member","Silver Member","Gold Member","Resigned","Visitor"};
  for(int resolution=1;resolution<=2;resolution++) {
    scale=(float)resolution;ni=nt=nf=0;
    RpSnap s={0};UiCtx c={0};RpAction acts[8];s.kind=RP_ROSTER;
    add(&s,28,60,"Name");add(&s,320,60,"Status");
    for(int i=0;i<5;i++){add(&s,28,89+20*i,"Golfer");add(&s,320,89+20*i,rank[i]);}
    // Hole outcomes retain the original camera/check/X symbols.
    for(int i=0;i<3;i++){RpItem *it=&s.it[s.n++];it->type=RP_IT_CHIP;it->x=381+21*i;it->y=85;it->group=9+2*i;if(i==2)it->group=7;}
    report_grid(&c,&s,ui_rect(0,0,1384*scale,616*scale),NULL,acts,8);
    int marker[5]={0},silver=0,gold=0;
    for(int i=0;i<5;i++) {
      int found=-1;for(int j=0;j<nt;j++)if(!strcmp(texts[j].s,rank[i])){found=j;break;}
      CHECK(found>=0);UiRect r=texts[found].r;
      CHECK(r.w>0 && r.x>=0 && r.x+r.w<=1384*scale);
      for(int j=0;j<ni;j++)if(icons[j].r.x<r.x && icons[j].r.x+icons[j].r.w<=r.x && fabsf(icons[j].r.y+icons[j].r.h/2-r.y-r.h/2)<scale)marker[i]=icons[j].icon;
    }
    CHECK(marker[0] && marker[1] && marker[2] && marker[3]);
    CHECK(marker[0]!=marker[1] && marker[1]!=marker[2] && marker[0]!=marker[2]);
    CHECK(marker[3]==UI_ICON_X && marker[4]==0);
    for(int j=0;j<nf;j++) {
      UiColor c=fills[j].color;
      if(c.r==192 && c.g==192 && c.b==192)silver++;
      if(c.r==212 && c.g==175 && c.b==55)gold++;
    }
    CHECK(silver==2 && gold==2); // row and matching legend
    CHECK(ni==13); // four rank markers, three hole outcomes, six legend markers
  }
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Path(tmp) / "roster.c"
            fixture.write_text(source)
            exe = Path(tmp) / ("roster.exe" if os.name == "nt" else "roster")
            subprocess.run([cc, *shlex.split(os.environ.get("CFLAGS", "")), "-std=c11",
                            "-I", str(ROOT / "port/src"), str(fixture), "-o", str(exe),
                            *([] if os.name == "nt" else ["-lm"])], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
