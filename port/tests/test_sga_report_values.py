"""Render the production SGA layout with passing and failing original snapshots."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class SgaReportValues(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang")
        if not cc:
            raise unittest.SkipTest("C compiler unavailable")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        work = Path(cls.tmp.name)
        layout = (ROOT / "port/src/port/ui/ui_report_layouts.inc").read_text()
        source = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RP_TEXT 96
enum { RP_IT_TEXT, RP_IT_CHIP, UI_TYPE_HEADING, UI_TYPE_CAPTION,
       UI_TYPE_BODY, UI_TYPE_LABEL, UI_ICON_STAR };
typedef int UiColor;
typedef struct { int unused; } UiCtx, UiDraw;
typedef struct { float x,y,w,h; } UiRect;
typedef struct { int text_primary,text_secondary,money; } UiTokens;
typedef struct { int type,x,y,big; uint16_t col; char s[RP_TEXT]; } RpItem;
typedef struct { int n; RpItem it[16]; } RpSnap;
static UiTokens tokens;
static UiDraw draw;
static char rendered[32][RP_TEXT+16];
static UiRect rects[32];
static int count;
static const UiTokens *ui_tokens(UiCtx *c) { (void)c; return &tokens; }
static UiDraw *ui_drawer(UiCtx *c) { (void)c; return &draw; }
static float ui_s(UiCtx *c,float v) { (void)c; return v; }
static UiRect ui_rect(float x,float y,float w,float h) { return (UiRect){x,y,w,h}; }
static uint32_t ui_id(const char *s) { (void)s; return 0; }
static uint32_t ui_idi(uint32_t id,int n) { return id+n; }
static UiColor text_colour(const UiTokens *t,uint16_t c) { (void)t; return c; }
static void report_string(UiCtx *c,UiRect r,const char *s,int style,UiColor color,float align,uint32_t id) {
  (void)c;(void)style;(void)color;(void)align;(void)id;
  if(count>=32)abort();
  snprintf(rendered[count],sizeof rendered[count],"%s",s);rects[count++]=r;
}
static void ui_text_in(UiCtx *c,UiRect r,int style,UiColor color,const char *s,float align) {
  (void)c;(void)r;(void)style;(void)color;(void)s;(void)align;
}
static void report_stripe(UiCtx *c,UiRect r,int row,int total) { (void)c;(void)r;(void)row;(void)total; }
static void ui_icon(UiCtx *c,int icon,UiRect r,float size,UiColor color) { (void)c;(void)icon;(void)r;(void)size;(void)color; }
'''
        for signature in ["static const RpItem *report_at(", "static void report_value(", "static void report_sga("]:
            source += function(layout, signature) + "\n"
        source += r'''
static void add(RpSnap *s,int x,int y,const char *text) {
  RpItem *it=&s->it[s->n++];it->x=x;it->y=y;snprintf(it->s,sizeof it->s,"%s",text);
}
static int find(const char *text) {
  for(int i=0;i<count;i++)if(!strcmp(rendered[i],text))return i;
  return -1;
}
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL: %s\n",#c);return 1; } } while(0)
int main(int argc,char **argv) {
  CHECK(argc==2);RpSnap s={0};UiCtx c={0};UiRect body={0,0,1384,616};
  // showCourseReport (0044fb30): success uses right-aligned (396,77);
  // rejection uses left-aligned (370,80); first prize is centred (400,338).
  add(&s,75,77,"Selection Criteria: Daily Fee");
  add(&s,174,288,"Committee recommendation");
  if(!strcmp(argv[1],"rejected")) {
    add(&s,370,80,"0 of 100");add(&s,400,310,"Improvement Required!");
    report_sga(&c,&s,body);
    CHECK(find("Grade: 0 of 100")>=0);CHECK(find("Improvement Required!")>=0);
    CHECK(count==4);
  } else {
    add(&s,396,77,"85 of 100");add(&s,400,310,"SGA Players Championship");
    add(&s,400,338,"$250,000 first prize");
    add(&s,700,338,"unrelated text");
    report_sga(&c,&s,body);
    if(!strcmp(argv[1],"grade"))CHECK(find("Grade: 85 of 100")>=0);
    else {
      int p=find("$250,000 first prize"),r=find("SGA Players Championship");
      CHECK(p>=0 && r>=0);CHECK(rects[p].y>=rects[r].y+rects[r].h);
      CHECK(rects[p].y+rects[p].h<=body.y+body.h);CHECK(find("unrelated text")==-1);
    }
  }
  return 0;
}
'''
        fixture = work / "sga.c"
        fixture.write_text(source)
        cls.exe = work / ("sga.exe" if os.name == "nt" else "sga")
        subprocess.run([cc, *shlex.split(os.environ.get("CFLAGS", "")), "-std=c11",
                        str(fixture), "-o", str(cls.exe)], check=True)

    def test_passing_grade(self):
        subprocess.run([str(self.exe), "grade"], check=True)

    def test_existing_first_prize_and_layout(self):
        subprocess.run([str(self.exe), "prize"], check=True)

    def test_rejected_evaluation_without_prize(self):
        subprocess.run([str(self.exe), "rejected"], check=True)


if __name__ == "__main__":
    unittest.main()
