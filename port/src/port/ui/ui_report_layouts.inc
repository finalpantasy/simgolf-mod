// Organized reports. Content and actions remain the original game's snapshots.
// Native grids replace fixed source positions; those positions only identify fields.
static void report_control(const char *name,UiRect r) {
  if(g_layout.nctl>=48)return;
  int i=g_layout.nctl++;snprintf(g_layout.ctl_name[i],sizeof g_layout.ctl_name[i],"%s",name);
  g_layout.ctl_x[i]=r.x+r.w/2;g_layout.ctl_y[i]=r.y+r.h/2;
}
static const RpItem *report_at(const RpSnap *s, int x, int y) {
  for(int i=0;i<s->n;i++) if(s->it[i].type==RP_IT_TEXT && s->it[i].x==x && s->it[i].y==y && s->it[i].s[0]) return &s->it[i];
  return NULL;
}
static void report_string(UiCtx *c, UiRect r, const char *source, int style, UiColor color, float align, uint32_t id) {
  char text[RP_TEXT*2]; fix_text(text,sizeof text,source);
  ui_text_fit(c,r,style,color,text,align);
  if(ui_text_width(c,style,text)>r.w) {
    ui_hit(c,r,0,id,UI_HIT_BLOCK|UI_HIT_INTERACT);
    ui_tooltip(c,id,r,text);
  }
}
static float report_wrap(UiCtx *c, UiRect r, const char *source, int style, UiColor color) {
  char text[RP_TEXT*2];fix_text(text,sizeof text,source);
  float line=ui_font(c,style).line,y=r.y;
  char *p=text;
  while(*p) {
    while(*p==' ')p++;
    int n=(int)strlen(p),cut=n;
    while(cut>0) {
      char save=p[cut];p[cut]=0;float w=ui_text_width(c,style,p);p[cut]=save;
      if(w<=r.w)break;
      cut--;while(cut>0 && p[cut]!=' ')cut--;
    }
    if(!cut)cut=n; // one long token is fitted, never an infinite wrap loop
    char save=p[cut];p[cut]=0;
    report_string(c,ui_rect(r.x,y,r.w,line),p,style,color,0,ui_idi(ui_id("report.wrap"),(int)y));
    p[cut]=save;p+=cut;y+=line;
  }
  return y-r.y;
}
static void report_value(UiCtx *c,const RpSnap *s,int x,int y,UiRect r,int style,float align) {
  const RpItem *it=report_at(s,x,y);if(!it)return;
  report_string(c,r,it->s,style,text_colour(ui_tokens(c),it->col),align,ui_idi(ui_id("report.value"),x+y*1000));
}
static void report_stripe(UiCtx *c,UiRect r,int row,int total) {
  const UiTokens *t=ui_tokens(c);
  if(total || !(row%2))uid_rrect(ui_drawer(c),r,ui_s(c,6),total?t->accent_soft:uid_alpha(t->surface_inset,.50f));
}
static int report_nearest(const float *source,int n,int x) {
  int best=0;for(int i=1;i<n;i++)if(fabsf(source[i]-x)<fabsf(source[best]-x))best=i;return best;
}
static int report_row(const int *rows,int n,int y) {
  for(int i=0;i<n;i++)if(abs(rows[i]-y)<=3)return i;return -1;
}

// showCourseReport (0044fb30) places this legend below the last hole, so
// its source Y changes with the hole count. It is never a table row.
static int report_course_legend(const RpSnap *s,const RpItem *it) {
  if(s->kind!=RP_COURSE && s->kind!=RP_YEAR)return 0;
  return it->type==RP_IT_TEXT &&
    ((it->x==178 && !strcmp(it->s,"Top 100 Hole")) ||
     (it->x==335 && !strcmp(it->s,"Top 18 Hole")) ||
     (it->x==487 && !strcmp(it->s,"Scenic Hole")));
}

static int report_grid(UiCtx *c,const RpSnap *s,UiRect body,SDL_Texture *const *art,RpAction *acts,int max) {
  const UiTokens *t=ui_tokens(c);UiDraw *d=ui_drawer(c);int na=0;
  float sx[32]={0},weight[32]={0};int nc=0,header=-1;
  int rows[ROWS],nr=0;
  for(int i=0;i<s->n;i++) {
    const RpItem *it=&s->it[i];
    if(it->type!=RP_IT_TEXT || !it->s[0] || same_text(it->s,s->title))continue;
    if(report_course_legend(s,it))continue;
    if(s->kind==RP_ROSTER && it->y>=530)continue;
    if((s->kind==RP_COURSE || s->kind==RP_YEAR) && it->y>=440)continue;
    if(report_row(rows,nr,it->y)<0 && nr<ROWS)rows[nr++]=it->y;
  }
  for(int i=1;i<nr;i++)for(int j=i;j>0 && rows[j]<rows[j-1];j--){int y=rows[j];rows[j]=rows[j-1];rows[j-1]=y;}
  if(s->kind==RP_COURSE || s->kind==RP_YEAR) {
    const float source[]={15,151,172,227,278,324,369,415,462,471,635,709,786};
    const float weights[]={2.4f,.9f,.6f,.9f,1,1,1,1,1,1.7f,1.1f,1.3f,1.5f};
    nc=13;memcpy(sx,source,sizeof source);memcpy(weight,weights,sizeof weights);header=report_row(rows,nr,76);
  } else if(s->kind==RP_COMMENTS) {
    nc=3;sx[0]=182;sx[1]=504;sx[2]=598;weight[0]=7;weight[1]=1;weight[2]=1.4f;header=report_row(rows,nr,96);
  } else if(s->kind==RP_FINANCE) {
    nc=1;sx[0]=98;weight[0]=2.2f;
    for(int i=0;i<s->n && nc<32;i++)if(s->it[i].type==RP_IT_TEXT && s->it[i].y==60 && s->it[i].s[0]) {
      sx[nc]=s->it[i].x+34;weight[nc++]=1.25f;
    }
    header=report_row(rows,nr,60);
  } else if(s->kind==RP_ROSTER) {
    nc=23;const float source[]={28,170,209,249,320};const float weights[]={4.5f,1.2f,1.2f,1.5f,3.2f};
    memcpy(sx,source,sizeof source);memcpy(weight,weights,sizeof weights);
    for(int i=5;i<23;i++){sx[i]=390+21*(i-5);weight[i]=.9f;}
    header=report_row(rows,nr,60);
  } else if(s->kind==RP_KEYS) {
    nc=4;sx[0]=68;sx[1]=108;sx[2]=444;sx[3]=483;weight[0]=1;weight[1]=4.5f;weight[2]=1;weight[3]=4.5f;
  } else if(s->kind==RP_BEST) {
    nc=2;sx[0]=246;sx[1]=551;weight[0]=7;weight[1]=2;header=report_row(rows,nr,96);
  } else {
    // Best-score/tournament tables retain the exact source column order.
    int hy=-1;
    for(int r=0;r<nr && hy<0;r++) {
      int n=0;for(int i=0;i<s->n;i++)if(s->it[i].type==RP_IT_TEXT && s->it[i].s[0] && abs(s->it[i].y-rows[r])<=3)n++;
      if(n>=3)hy=rows[r];
    }
    for(int i=0;i<s->n && nc<24;i++)if(s->it[i].type==RP_IT_TEXT && s->it[i].s[0] && abs(s->it[i].y-hy)<=3) {
      int x=s->it[i].x,found=0;for(int j=0;j<nc;j++)if(fabsf(sx[j]-x)<12)found=1;
      if(!found){sx[nc]=x;weight[nc++]=1;}
    }
    for(int i=1;i<nc;i++)for(int j=i;j>0 && sx[j]<sx[j-1];j--){float v=sx[j];sx[j]=sx[j-1];sx[j-1]=v;}
    if(nc){weight[0]=2.4f;header=report_row(rows,nr,hy);}else{nc=1;sx[0]=0;weight[0]=1;}
    if(s->kind==RP_TOURNEY && nc>=5) {
      weight[0]=3.2f;for(int i=1;i<nc-2;i++)weight[i]=.7f;
      weight[nc-2]=1;weight[nc-1]=2.3f;
    }
  }
  float total=0;for(int i=0;i<nc;i++)total+=weight[i];
  float x[32],w[32],left=body.x;
  for(int i=0;i<nc;i++){x[i]=left;w[i]=body.w*weight[i]/total;left+=w[i];}
  float top=body.y,hh=ui_s(c,36),rh=ui_s(c,s->kind==RP_KEYS?38:32);
  if(s->kind==RP_COURSE || s->kind==RP_YEAR) {
    ui_text_in(c,ui_rect(x[1],top,x[6]-x[1],ui_s(c,24)),UI_TYPE_CAPTION,t->text_secondary,"Play",0);
    ui_text_in(c,ui_rect(x[6],top,x[9]-x[6],ui_s(c,24)),UI_TYPE_CAPTION,t->text_secondary,"Skills",0);
    ui_text_in(c,ui_rect(x[10],top,body.x+body.w-x[10],ui_s(c,24)),UI_TYPE_CAPTION,t->text_secondary,"Income and costs",0);top+=ui_s(c,28);
  }
  if(s->kind==RP_KEYS) {
    ui_text_in(c,ui_rect(x[0],top,x[2]-x[0],hh),UI_TYPE_HEADING,t->text_primary,"Reports and game",0);
    ui_text_in(c,ui_rect(x[2],top,body.x+body.w-x[2],hh),UI_TYPE_HEADING,t->text_primary,"Tools and view",0);top+=hh+ui_s(c,8);
  }
  if(header>=0) {
    uid_rrect(d,ui_rect(body.x,top,body.w,hh),ui_s(c,8),t->surface_inset);
    for(int i=0;i<s->n;i++) {
      const RpItem *it=&s->it[i];if(it->type!=RP_IT_TEXT || !it->s[0] || abs(it->y-rows[header])>3)continue;
      int col=report_nearest(sx,nc,it->x);
      // Finance headers are left-anchored; amounts end 34 source pixels later.
      if(s->kind==RP_FINANCE && it->x!=98)col=report_nearest(sx,nc,it->x+34);
      UiRect cell=ui_rect(x[col]+ui_s(c,8),top,w[col]-ui_s(c,16),hh);
      float align=col?1:0;
      if(s->kind==RP_ROSTER)align=col>=5?.5f:col==0||col==4?0:1;
      report_string(c,cell,it->s,UI_TYPE_CAPTION,t->text_secondary,align,ui_idi(ui_id("report.header"),i));

    }
    if(s->kind==RP_COURSE || s->kind==RP_YEAR)ui_text_in(c,ui_rect(x[0]+ui_s(c,8),top,w[0],hh),UI_TYPE_CAPTION,t->text_secondary,"Hole",0);
    top+=hh+ui_s(c,8);
  }
  float footer=ui_s(c,s->kind==RP_ROSTER?72:(s->kind==RP_COURSE||s->kind==RP_YEAR)?44:0);
  UiRect view=ui_rect(body.x,top,body.w,body.y+body.h-top-footer);
  int data=nr-(header>=0);if(data<1)data=1;
  if(s->kind==RP_ROSTER || s->kind==RP_KEYS)rh=fminf(rh,view.h/data);
  float off=s->kind==RP_ROSTER?0:ui_vscroll_begin(c,ui_idi(ui_id("report.grid"),s->kind),view,data*rh,rh*3);
  int row=0;
  for(int r=0;r<nr;r++) {
    if(r==header)continue;
    UiRect rr=ui_rect(view.x,view.y+off+row*rh,view.w,rh);
    int ordinal=row++;
    if(rr.y+rr.h<view.y || rr.y>view.y+view.h)continue;
    int is_total=0;
    for(int i=0;i<s->n;i++)if(s->it[i].type==RP_IT_TEXT && abs(s->it[i].y-rows[r])<=3 && !strncmp(s->it[i].s,"Total",5))is_total=1;
    report_stripe(c,rr,ordinal,is_total);
    for(int col=0;col<nc;col++) {
      char text[RP_TEXT*2]="";uint16_t color=0;int first=-1;
      for(int i=0;i<s->n;i++) {
        const RpItem *it=&s->it[i];
        if(it->type!=RP_IT_TEXT || !it->s[0] || report_course_legend(s,it) || same_text(it->s,s->title) || abs(it->y-rows[r])>3 || report_nearest(sx,nc,it->x)!=col)continue;
        size_t n=strlen(text);snprintf(text+n,sizeof text-n,"%s%s",n?"  ":"",it->s);color=it->col;if(first<0)first=i;
      }
      UiRect cell=ui_rect(x[col]+ui_s(c,8),rr.y,w[col]-ui_s(c,16),rh);
      if((s->kind==RP_COURSE || s->kind==RP_YEAR) && (col==0 || col==9)) cell.w-=ui_s(c,22);
      int key=s->kind==RP_KEYS && !(col%2);
      if(key)uid_rrect(d,ui_inset(cell,0,ui_s(c,5),0,ui_s(c,5)),ui_s(c,6),t->accent_soft);
      int style=(nc>15)?UI_TYPE_CAPTION:(is_total||key)?UI_TYPE_LABEL:UI_TYPE_BODY;
      UiColor color_out=(strchr(text,'-')&&numeric(text))?t->danger:text_colour(t,color);
      float align=key?.5f:((col && s->kind!=RP_KEYS)?1:0);
      if(s->kind==RP_ROSTER)align=col>=5?.5f:col==0||col==4?0:1;
      report_string(c,cell,text,style,color_out,align,ui_idi(ui_id("report.cell"),r*32+col));

      (void)first;
    }
    // Source pictograms remain factual content. Native markers replace the roster's UI sprites.
    for(int i=0;i<s->n;i++) {
      const RpItem *it=&s->it[i];if(it->type!=RP_IT_CHIP || it->big || abs(it->y-rows[r])>7)continue;
      if(s->kind==RP_ROSTER) {
        if(it->x<370 || it->group<1)continue;
        int col=5+(int)lroundf((it->x-381)/21.0f);if(col<5||col>=23)continue;
        int index=it->group-1;
        UiIcon icon=index>=10?UI_ICON_CHECK:index>=8?UI_ICON_CAMERA:UI_ICON_X;
        UiColor cc=index>=8?t->accent_soft_text:t->danger;
        ui_icon(c,icon,ui_rect(x[col],rr.y,w[col],rh),ui_s(c,17),cc);
      } else if(s->kind==RP_COURSE || s->kind==RP_YEAR) {
        int col=it->x<120?0:it->x>=540&&it->x<600?9:-1;
        if(col<0)continue;
        SDL_Texture *tx=art&&it->art?art[it->art-1]:NULL;if(!tx)continue;
        float size=ui_s(c,18),scale=size/fmaxf(it->w,it->h);
        uid_image(d,tx,NULL,ui_rect(x[col]+w[col]-size-ui_s(c,4),rr.y+(rh-size)/2,it->w*scale,it->h*scale),0,(UiColor){255,255,255,255});
      }
    }
  }
  if(s->kind!=RP_ROSTER)ui_vscroll_end(c);
  if(s->kind==RP_ROSTER) {
    uint32_t id=ui_id("roster.wheel");ui_hit(c,view,0,id,UI_HIT_BLOCK|UI_HIT_WHEEL);
    float wh=ui_wheel(c,id);
    if(wh!=0 && na<max)acts[na++]=(RpAction){RP_A_CLICK,780|((wh>0?98:508)<<16)};
    float yy=body.y+body.h-ui_s(c,64);
    const char *names[]={"Member","Silver Member","Gold Member","Resigned","Photo Opp","Happy Ending"};
    for(int i=0;i<6;i++) {
      UiRect r=ui_rect(body.x+i*body.w/6,yy,body.w/6,ui_s(c,24));
      UiIcon icon=i==4?UI_ICON_CAMERA:i==3?UI_ICON_X:i==5?UI_ICON_CHECK:UI_ICON_USERS;
      ui_icon(c,icon,ui_rect(r.x,r.y,ui_s(c,20),r.h),ui_s(c,16),i==3?t->danger:t->accent_soft_text);
      ui_text_fit(c,ui_rect(r.x+ui_s(c,26),r.y,r.w-ui_s(c,28),r.h),UI_TYPE_CAPTION,t->text_secondary,names[i],0);
    }
    ui_text_in(c,ui_rect(body.x,yy+ui_s(c,32),body.w-ui_s(c,280),ui_s(c,28)),UI_TYPE_CAPTION,t->text_secondary,"Use the wheel or arrows to browse members.",0);
    for(int i=0;i<2;i++) {
      UiRect r=ui_rect(body.x+body.w-ui_s(c,264)+i*ui_s(c,136),yy+ui_s(c,28),ui_s(c,128),ui_s(c,32));
      report_control(i?"rosterdown":"rosterup",r);
      if(ui_button(c,ui_idi(ui_id("roster.arrow"),i),r,i?"Scroll down":"Scroll up",i?UI_ICON_DOWN:UI_ICON_UP,UI_BTN_PILL,0)&&na<max)
        acts[na++]=(RpAction){RP_A_CLICK,780|((i?508:98)<<16)};
    }
  } else if(s->kind==RP_COURSE || s->kind==RP_YEAR) {
    int n=0;for(int i=0;i<s->n;i++)if(s->it[i].type==RP_IT_TEXT && (s->it[i].y>=440 || report_course_legend(s,&s->it[i])) && s->it[i].s[0]) {
      report_string(c,ui_rect(body.x+(n%3)*body.w/3,body.y+body.h-ui_s(c,32),body.w/3,ui_s(c,28)),s->it[i].s,UI_TYPE_CAPTION,t->text_secondary,0,ui_idi(ui_id("report.legend"),i));n++;
    }
  }
  return na;
}

static void report_sga(UiCtx *c,const RpSnap *s,UiRect body) {
  const UiTokens *t=ui_tokens(c);UiDraw *d=ui_drawer(c);
  report_value(c,s,75,77,ui_rect(body.x,body.y,body.w*.7f,ui_s(c,32)),UI_TYPE_HEADING,0);
  const RpItem *grade=report_at(s,370,80);char grade_text[RP_TEXT+16];
  snprintf(grade_text,sizeof grade_text,"Grade: %s",grade?grade->s:"");
  report_string(c,ui_rect(body.x+body.w*.75f,body.y,body.w*.25f,ui_s(c,32)),grade_text,UI_TYPE_HEADING,t->text_primary,1,ui_id("sga.grade"));
  float top=body.y+ui_s(c,56),rh=ui_s(c,42);
  const char *head[]={"Criterion","Current","Rating","Ideal (Minimum)"};
  float start[]={0,.29f,.46f,.73f},width[]={.28f,.16f,.26f,.27f};
  for(int j=0;j<4;j++)ui_text_in(c,ui_rect(body.x+start[j]*body.w,top,width[j]*body.w,ui_s(c,28)),UI_TYPE_CAPTION,t->text_secondary,head[j],0);
  top+=ui_s(c,36);
  for(int row=0;row<10;row++) {
    int y=104+17*row;UiRect r=ui_rect(body.x,top+row*rh,body.w,rh);report_stripe(c,r,row,0);
    report_value(c,s,119,y,ui_rect(r.x+ui_s(c,10),r.y,r.w*.27f,rh),UI_TYPE_BODY,0);
    report_value(c,s,238,y,ui_rect(r.x+r.w*.29f,r.y,r.w*.16f,rh),UI_TYPE_LABEL,0);
    report_value(c,s,490,y,ui_rect(r.x+r.w*.73f,r.y,r.w*.27f,rh),UI_TYPE_BODY,0);
    int stars=0;for(int i=0;i<s->n;i++)if(s->it[i].type==RP_IT_CHIP && !s->it[i].big && abs(s->it[i].y-y)<7 && s->it[i].x>=300 && s->it[i].x<490)stars++;
    for(int i=0;i<stars;i++)ui_icon(c,UI_ICON_STAR,ui_rect(r.x+r.w*.46f+i*ui_s(c,22),r.y,ui_s(c,20),rh),ui_s(c,17),t->money);
    report_value(c,s,386,y,ui_rect(r.x+r.w*.46f,r.y,r.w*.26f,rh),UI_TYPE_CAPTION,0);
  }
  float y=top+10*rh+ui_s(c,24);
  report_value(c,s,174,288,ui_rect(body.x,y,body.w*.35f,ui_s(c,32)),UI_TYPE_LABEL,0);
  report_value(c,s,400,310,ui_rect(body.x+body.w*.36f,y,body.w*.64f,ui_s(c,32)),UI_TYPE_HEADING,0);
}

static void report_hole(UiCtx *c,const RpSnap *s,UiRect body) {
  const UiTokens *t=ui_tokens(c);float gap=ui_s(c,32),cw=(body.w-gap)/2,rh=ui_s(c,44);
  ui_text_in(c,ui_rect(body.x,body.y,cw,ui_s(c,32)),UI_TYPE_HEADING,t->text_primary,"Hole character",0);
  ui_text_in(c,ui_rect(body.x+cw+gap,body.y,cw,ui_s(c,32)),UI_TYPE_HEADING,t->text_primary,"Scoring",0);
  float top=body.y+ui_s(c,44);
  for(int row=0;row<4;row++)for(int group=0;group<2;group++) {
    UiRect r=ui_rect(body.x+group*(cw+gap),top+row*rh,cw,rh);report_stripe(c,r,row,0);
    int y=83+21*row;
    report_value(c,s,group?441:190,y,ui_rect(r.x+ui_s(c,10),r.y,cw*.46f,rh),UI_TYPE_BODY,0);
    if(!group)report_value(c,s,356,y,ui_rect(r.x+cw*.48f,r.y,cw*.5f,rh),UI_TYPE_LABEL,1);
    else for(int i=0;i<s->n;i++)if(s->it[i].type==RP_IT_TEXT && s->it[i].y==y && s->it[i].x>500)
      report_string(c,ui_rect(r.x+cw*.48f,r.y,cw*.5f,rh),s->it[i].s,UI_TYPE_LABEL,t->text_primary,1,ui_idi(ui_id("hole.value"),i));
  }
  float y=top+4*rh+ui_s(c,28);
  ui_text_in(c,ui_rect(body.x,y,body.w,ui_s(c,28)),UI_TYPE_HEADING,t->text_primary,"Average shots on this hole",0);y+=ui_s(c,40);
  const int xs[]={445,479,513,547,581,615};
  for(int i=0;i<6;i++) {
    UiRect r=ui_rect(body.x+i*body.w/6,y,body.w/6-ui_s(c,8),ui_s(c,64));
    uid_rrect(ui_drawer(c),r,ui_s(c,8),t->surface_inset);
    report_value(c,s,xs[i],168,ui_rect(r.x,r.y,r.w,ui_s(c,28)),UI_TYPE_CAPTION,.5f);
    for(int j=0;j<s->n;j++)if(s->it[j].type==RP_IT_TEXT && s->it[j].x==xs[i] && s->it[j].y>168 && s->it[j].y<202)
      report_string(c,ui_rect(r.x,r.y+ui_s(c,28),r.w,ui_s(c,32)),s->it[j].s,UI_TYPE_LABEL,t->text_primary,.5f,ui_idi(ui_id("hole.shots"),j));
  }
  y+=ui_s(c,88);
  ui_text_in(c,ui_rect(body.x,y,body.w,ui_s(c,28)),UI_TYPE_HEADING,t->text_primary,"Player comments",0);y+=ui_s(c,40);
  UiRect view=ui_rect(body.x,y,body.w,body.y+body.h-y);
  int n=0;for(int i=0;i<s->n;i++)if(s->it[i].type==RP_IT_TEXT && s->it[i].y>202 && s->it[i].s[0])n++;
  float off=ui_vscroll_begin(c,ui_id("hole.comments"),view,n*ui_s(c,44),ui_s(c,88));
  int row=0;for(int i=0;i<s->n;i++) {
    const RpItem *it=&s->it[i];if(it->type!=RP_IT_TEXT || it->y<=202 || !it->s[0])continue;
    UiRect r=ui_rect(view.x,view.y+off+row*ui_s(c,44),view.w,ui_s(c,44));report_stripe(c,r,row++,0);
    report_string(c,ui_inset(r,ui_s(c,12),0,ui_s(c,12),0),it->s,UI_TYPE_BODY,t->text_secondary,0,ui_idi(ui_id("hole.comment"),i));
  }
  if(!n)ui_text_in(c,view,UI_TYPE_BODY,t->text_secondary,"No comments recorded yet.",0);
  ui_vscroll_end(c);
}

static UiColor report_curve_color(const UiTokens *t,uint16_t color) {
  color&=0x7fff;
  if(color==0x4010)return t->accent_soft_text;
  if(color==0 || color==0x7d08 || color==0x0208)return t->money;
  if(color==0x03e0)return t->accent;
  return t->text_secondary;
}
static void report_history(UiCtx *c,const RpSnap *s,UiRect body) {
  const UiTokens *t=ui_tokens(c);UiDraw *d=ui_drawer(c);float gap=ui_s(c,32),ew=ui_s(c,380);
  UiRect left=ui_rect(body.x,body.y,body.w-ew-gap,body.h),events=ui_rect(left.x+left.w+gap,body.y,ew,body.h);
  const char *labels[]={"Skill","Cash","Fun","Members"};const uint16_t colors[]={0x4010,0x7d08,0x03e0,0x0210};
  for(int i=0;i<4;i++) {
    UiRect r=ui_rect(left.x+i*left.w/4,left.y,left.w/4,ui_s(c,28));
    uid_line(d,r.x,r.y+r.h/2,r.x+ui_s(c,20),r.y+r.h/2,ui_s(c,3),report_curve_color(t,colors[i]));
    ui_text_in(c,ui_rect(r.x+ui_s(c,28),r.y,r.w-ui_s(c,28),r.h),UI_TYPE_CAPTION,t->text_secondary,labels[i],0);
  }
  UiRect plot=ui_rect(left.x+ui_s(c,44),left.y+ui_s(c,56),left.w-ui_s(c,112),left.h-ui_s(c,96));
  uid_rrect(d,ui_inset_all(plot,-ui_s(c,12)),ui_s(c,12),uid_alpha(t->surface_inset,.5f));
  for(int k=0;k<=5;k++)uid_line(d,plot.x,plot.y+k*plot.h/5,plot.x+plot.w,plot.y+k*plot.h/5,ui_s(c,1),t->divider);
  // Keep the original graph's scaling and history computations. Event leaders
  // leave the plot; their labels instead have a dedicated scrollable timeline.
  float kx=plot.w/(711-105),ky=plot.h/(521-71);
  uid_set_clip(d,&plot);
  for(int i=0;i<s->n;i++) {
    const RpItem *it=&s->it[i];if(it->type!=RP_IT_LINE || (it->col&0x7fff)==0x3ff)continue;
    uid_line(d,plot.x+(it->x-105)*kx,plot.y+(it->y-71)*ky,plot.x+(it->w-105)*kx,plot.y+(it->h-71)*ky,ui_s(c,2),report_curve_color(t,it->col));
  }
  uid_set_clip(d,NULL);
  for(int i=0;i<s->n;i++) {
    const RpItem *it=&s->it[i];if(it->type!=RP_IT_TEXT || !it->s[0] || it->y>=542)continue;
    if(it->x==77 || it->x==727) {
      float x=it->x==77?left.x:plot.x+plot.w+ui_s(c,10);
      report_string(c,ui_rect(x,plot.y+(it->y-71)*ky-ui_s(c,10),ui_s(c,60),ui_s(c,20)),it->s,UI_TYPE_MICRO,t->text_secondary,0,ui_idi(ui_id("history.axis"),i));
    }
  }
  ui_text_in(c,ui_rect(plot.x,plot.y+plot.h+ui_s(c,20),plot.w,ui_s(c,24)),UI_TYPE_CAPTION,t->text_secondary,"Course history",.5f);
  
  int n=0;for(int i=0;i<s->n;i++)if(s->it[i].type==RP_IT_TEXT && s->it[i].align==RP_AL_LEFT && s->it[i].y<542 && s->it[i].s[0])n++;
  char event_title[64];snprintf(event_title,sizeof event_title,"Course events (%d)",n);
  ui_text_in(c,ui_rect(events.x,events.y,events.w,ui_s(c,32)),UI_TYPE_HEADING,t->text_primary,event_title,0);
  UiRect view=ui_rect(events.x,events.y+ui_s(c,48),events.w,events.h-ui_s(c,80));
  report_control("eventscroll",view);
  float rh=ui_s(c,60),off=ui_vscroll_begin(c,ui_id("history.events"),view,n*rh,rh*2);int row=0;
  for(int i=0;i<s->n;i++) {
    const RpItem *it=&s->it[i];if(it->type!=RP_IT_TEXT || it->align!=RP_AL_LEFT || it->y>=542 || !it->s[0])continue;
    UiRect r=ui_rect(view.x,view.y+off+row*rh,view.w,rh);report_stripe(c,r,row++,0);
    report_wrap(c,ui_rect(r.x+ui_s(c,12),r.y+ui_s(c,8),r.w-ui_s(c,24),r.h-ui_s(c,16)),it->s,UI_TYPE_CAPTION,t->text_secondary);
  }
  if(!n)ui_text_in(c,view,UI_TYPE_BODY,t->text_secondary,"No course events recorded yet.",0);
  ui_vscroll_end(c);
  if(n*rh>view.h)ui_text_in(c,ui_rect(events.x,events.y+events.h-ui_s(c,24),events.w,ui_s(c,24)),UI_TYPE_CAPTION,t->text_secondary,"Scroll here to see all events.",0);
}

static int draw_organized_report(UiCtx *c,const RpSnap *s,SDL_Texture *const *art,RpAction *acts,int max) {
  const UiTokens *t=ui_tokens(c);UiRect scr=ui_screen(c);int na=0;
  int sga=s->kind==RP_SGA || (s->kind==RP_YEAR && report_at(s,75,77));
  float height=s->kind==RP_FINANCE?600:s->kind==RP_BEST?640:sga?820:s->kind==RP_KEYS?840:s->kind==RP_TOURNEY?880:940;
  float margin=ui_s(c,t->edge_margin),fit=fminf(1,fminf((scr.w-2*margin)/ui_s(c,1440),(scr.h-2*margin)/ui_s(c,height)));
  if(fit<1)ui_push_scale(c,fit);
  UiRect panel=ui_rect_round(ui_center(scr,ui_s(c,1440),ui_s(c,height))),in=ui_inset_all(panel,ui_s(c,28));
  uid_rrect(ui_drawer(c),scr,0,t->scrim);uint32_t sid=ui_id("reports.scrim");ui_hit(c,scr,0,sid,UI_HIT_MODAL|UI_HIT_BLOCK|UI_HIT_INTERACT);
  ui_panel(c,panel,ui_s(c,t->radius_panel),UI_PANEL_STRONG|UI_PANEL_READABLE);
  memset(&g_layout,0,sizeof g_layout);g_layout.panel=panel;
  UiRect close=ui_rect(in.x+in.w-ui_s(c,40),in.y,ui_s(c,40),ui_s(c,40));
  g_layout.close_x=close.x+close.w/2;g_layout.close_y=close.y+close.h/2;
  if(ui_icon_button(c,ui_id("reports.close"),close,UI_ICON_X,0,"Close (Esc)")&&na<max)acts[na++]=(RpAction){RP_A_CLOSE,0};
  char title[128];title_case(title,sizeof title,s->title[0]?s->title:default_title(s->kind));
  ui_text_fit(c,ui_rect(in.x,in.y,in.w-close.w-ui_s(c,16),ui_s(c,40)),UI_TYPE_TITLE,t->text_primary,title,0);
  UiRect nav=ui_rect(in.x,in.y+ui_s(c,52),ui_segmented_width(c,g_tab_label,NULL,TABS),ui_s(c,38));
  int selected=-1;for(int i=0;i<TABS;i++)if(g_tab_kind[i]==s->kind)selected=i;
  float x=nav.x;for(int i=0;i<TABS;i++) {
    float w=ui_segmented_width(c,&g_tab_label[i],NULL,1)-ui_s(c,4);
    g_layout.tab_x[i]=x+w/2;x+=w+ui_s(c,2);
  }
  g_layout.ntabs=TABS;g_layout.tab_y=nav.y+nav.h/2;
  if(s->kind==RP_TOURNEY)g_layout.ntabs=0;
  int was=selected;if(s->kind!=RP_TOURNEY && ui_segmented(c,ui_id("reports.tabs"),nav,g_tab_label,NULL,TABS,&selected)&&selected!=was&&selected>=0&&na<max)
    acts[na++]=(RpAction){RP_A_TAB,g_tab_kind[selected]};
  UiRect body=ui_rect(in.x,in.y+ui_s(c,116),in.w,in.h-ui_s(c,148));
  if(sga)report_sga(c,s,body);
  else if(s->kind==RP_HOLE)report_hole(c,s,body);
  else if(s->kind==RP_HISTO)report_history(c,s,body);
  else na+=report_grid(c,s,body,art,acts+na,max-na);
  ui_text_in(c,ui_rect(in.x,in.y+in.h-ui_s(c,24),in.w,ui_s(c,24)),UI_TYPE_CAPTION,t->text_secondary,s->kind==RP_TOURNEY?"Esc closes this report.":"Esc closes. Use the tabs to switch reports.",0);
  if(ui_clicked(c,sid)&&na<max)acts[na++]=(RpAction){RP_A_CLOSE,0};
  if(fit<1)ui_pop_scale(c);
  return na;
}
