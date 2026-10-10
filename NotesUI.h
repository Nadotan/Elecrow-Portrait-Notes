
#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <SPIFFS.h>
#include <FS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "lvgl_port.h"

// Elecrow Notes V2: 600x1024 portrait.
// LVGL 9.1 rotation and native GT911 input.
namespace Notes {

constexpr int SW=600, SH=1024, PW=568, PH=862;
constexpr int MAX_BOOKS=12, MAX_PAGES=32, MAX_POINTS=24000, HISTORY=4;
constexpr uint32_t MAGIC=0x4E545332;
constexpr uint32_t BG=0xF7F7F8, INK=0x25252A;
constexpr uint32_t SECONDARY=0x707781, ACCENT=0x386DD6;

enum Tool : uint8_t {
 PEN=0, MARKER=1, ERASER=2, SELECT=3
};

struct Point {
 uint16_t x,y,ink;
 uint8_t radius, first, type;
};

struct Header {
 uint32_t magic, count;
};

struct Book {
 char title[30];
 uint8_t paper, pages, cover, favorite;
 uint32_t recent;
};

struct Snapshot {
 Point *data;
 uint32_t count;
};

static Book books[MAX_BOOKS] = {};
static Preferences prefs;

static int book_count=0, active_book=-1;
static int active_page=0, editing_book=-1;
static uint32_t recent_counter=0;

static Point *points=nullptr;
static uint16_t *pixels=nullptr;
static uint32_t count=0;

static Snapshot undo_stack[HISTORY]={};
static Snapshot redo_stack[HISTORY]={};
static int undo_size=0, redo_size=0;

static bool storage=false, dirty=false, pen_down=false;
static bool needs_refresh=false, tools_visible=true;

static int prev_x=0,prev_y=0;
static int sel_x=0,sel_y=0,sel_x2=0,sel_y2=0;
static bool selection_active=false;

static Tool current_tool=PEN;
static uint16_t pen_color=0, marker_color=0;
static uint8_t pen_size=2, marker_size=9;

static const uint32_t PALETTE[] = {
 0x25252A,0x3164D8,0xC63650,0x2E9D70,0x9B5CC7,
 0xE29128,0x161616,0xFFE071,0x38A7B1
};

static const uint8_t SIZES[]={1,2,3,5,8,12};

static uint32_t last_draw=0,last_edit=0;

static lv_obj_t *root=nullptr,*canvas=nullptr;
static lv_obj_t *toast=nullptr,*paper_view=nullptr;
static lv_obj_t *selection_box=nullptr;

static lv_obj_t *name_input=nullptr,*soft_keyboard=nullptr;
static lv_obj_t *tool_popup=nullptr,*delete_button=nullptr;

static uint8_t selected_paper=1, selected_cover=0;
static lv_obj_t *template_buttons[3]={};
static lv_obj_t *cover_buttons[4]={};

static uint16_t rgb(uint32_t n){
 return lv_color_to_u16(lv_color_hex(n));
}

static void say(const char *s){
 if(toast)lv_label_set_text(toast,s);
 Serial.println(s);
}

static void filename(char *out,size_t n){
 snprintf(out,n,"/n2_%d_%d.dat",active_book,active_page);
}

static void save_catalog(){
 if(!prefs.begin("portrait2",false))return;
 prefs.putInt("books_n",book_count);
 prefs.putUInt("recent",recent_counter);
 prefs.putBytes("catalog",books,sizeof(books));
 prefs.end();
}

static void load_catalog(){
 if(!prefs.begin("portrait2",true))return;

 book_count=prefs.getInt("books_n",0);
 if(book_count<0||book_count>MAX_BOOKS)book_count=0;

 recent_counter=prefs.getUInt("recent",0);

 if(prefs.getBytesLength("catalog")==sizeof(books))
  prefs.getBytes("catalog",books,sizeof(books));

 prefs.end();

 for(int i=0;i<book_count;i++){
  books[i].title[sizeof(books[i].title)-1]=0;
  if(books[i].paper>2)books[i].paper=0;
  if(books[i].pages==0||books[i].pages>MAX_PAGES)
   books[i].pages=1;
 }
}

static void clear_snapshots(Snapshot *s,int &n){
 for(int i=0;i<n;i++){
  free(s[i].data);
  s[i]={};
 }
 n=0;
}

static void history_reset(){
 clear_snapshots(undo_stack,undo_size);
 clear_snapshots(redo_stack,redo_size);
}

static void push_snapshot(Snapshot *s,int &n){
 Point *copy=nullptr;

 if(count){
  copy=(Point*)heap_caps_malloc(
   count*sizeof(Point),
   MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT
  );

  if(!copy)return;
  memcpy(copy,points,count*sizeof(Point));
 }

 if(n==HISTORY){
  free(s[0].data);
  memmove(s,s+1,(HISTORY-1)*sizeof(Snapshot));
  n--;
 }

 s[n++]={copy,count};
}

static void edit_begin(){
 push_snapshot(undo_stack,undo_size);
 clear_snapshots(redo_stack,redo_size);
}

static void make_background(){
 if(!pixels)return;

 uint16_t white=rgb(0xFFFFFF);
 uint16_t line=rgb(0xDDE4F0);

 for(size_t i=0;i<(size_t)PW*PH;i++)
  pixels[i]=white;

 uint8_t style=active_book>=0?
  books[active_book].paper:0;

 if(style==1||style==2){
  for(int y=35;y<PH;y+=33)
   for(int x=0;x<PW;x++)
    pixels[y*PW+x]=line;

  if(style==2)
   for(int x=31;x<PW;x+=33)
    for(int y=0;y<PH;y++)
     pixels[y*PW+x]=line;
 }

 needs_refresh=true;
}

static uint16_t blend565(uint16_t bg,uint16_t fg){
 int br=(bg>>11)&31;
 int bg6=(bg>>5)&63;
 int bb=bg&31;

 int fr=(fg>>11)&31;
 int fg6=(fg>>5)&63;
 int fb=fg&31;

 return (uint16_t)(
  (((br*3+fr)/4)<<11)|
  (((bg6*3+fg6)/4)<<5)|
  ((bb*3+fb)/4)
 );
}

static void dot(int x,int y,uint16_t ink,uint8_t r,uint8_t type){
 if(!pixels)return;

 for(int dy=-(int)r;dy<=r;dy++){
  int yy=y+dy;
  if(yy<0||yy>=PH)continue;

  for(int dx=-(int)r;dx<=r;dx++){
   int xx=x+dx;

   if(xx<0||xx>=PW||dx*dx+dy*dy>r*r)
    continue;

   uint16_t &pixel=pixels[yy*PW+xx];

   pixel=type==MARKER?
    blend565(pixel,ink):ink;
  }
 }
}

static void segment(
 int x,int y,int xx,int yy,
 uint16_t ink,uint8_t r,uint8_t type
){
 int dx=abs(xx-x),sx=x<xx?1:-1;
 int dy=-abs(yy-y),sy=y<yy?1:-1;
 int err=dx+dy;

 for(;;){
  dot(x,y,ink,r,type);
  if(x==xx&&y==yy)break;

  int e=err*2;

  if(e>=dy){
   err+=dy;
   x+=sx;
  }

  if(e<=dx){
   err+=dx;
   y+=sy;
  }
 }
}

static void redraw(){
 make_background();

 int x=0,y=0;

 for(uint32_t i=0;i<count;i++){
  Point &p=points[i];

  if(p.first)
   dot(p.x,p.y,p.ink,p.radius,p.type);
  else
   segment(x,y,p.x,p.y,p.ink,p.radius,p.type);

  x=p.x;
  y=p.y;
 }

 needs_refresh=true;
}

static void changed(){
 dirty=true;
 needs_refresh=true;
 last_edit=millis();
}

static bool save(){
 if(!storage||active_book<0)return false;

 char path[32];
 filename(path,sizeof(path));

 File f=SPIFFS.open(path,"w");
 if(!f)return false;

 Header h={MAGIC,count};
 bool ok=f.write(
  (const uint8_t*)&h,sizeof(h)
 )==sizeof(h);

 size_t bytes=count*sizeof(Point),off=0;
 const uint8_t *b=(const uint8_t*)points;

 while(ok&&off<bytes){
  size_t n=bytes-off;
  if(n>2048)n=2048;

  ok=f.write(b+off,n)==n;
  off+=n;
 }

 f.close();

 if(ok)dirty=false;
 return ok;
}

static void load(){
 history_reset();
 count=0;
 make_background();
 dirty=false;
 selection_active=false;

 if(!storage||active_book<0)return;

 char path[32];
 filename(path,sizeof(path));

 File f=SPIFFS.open(path,"r");
 if(!f)return;

 Header h={};

 bool ok=f.read(
  (uint8_t*)&h,sizeof(h)
 )==sizeof(h)
 &&h.magic==MAGIC
 &&h.count<=MAX_POINTS
 &&f.size()==sizeof(h)+h.count*sizeof(Point);

 if(ok){
  uint8_t *b=(uint8_t*)points;
  size_t len=h.count*sizeof(Point),off=0;

  while(ok&&off<len){
   size_t n=len-off;
   if(n>2048)n=2048;

   ok=f.read(b+off,n)==n;
   off+=n;
  }
 }

 f.close();

 if(ok){
  count=h.count;
  redraw();
 }else{
  say("Saved note could not be read");
 }
}

static void history_go(bool redo){
 Snapshot *from=redo?redo_stack:undo_stack;
 int &nf=redo?redo_size:undo_size;

 if(!nf)return;

 push_snapshot(
  redo?undo_stack:redo_stack,
  redo?undo_size:redo_size
 );

 Snapshot s=from[--nf];

 if(s.count)
  memcpy(points,s.data,s.count*sizeof(Point));

 count=s.count;

 free(s.data);
 from[nf]={};

 redraw();
 changed();
}

static bool erase_stroke_at(int x,int y){
 int pad=14;

 for(uint32_t i=0;i<count;){
  uint32_t end=i+1;

  while(end<count&&!points[end].first)
   end++;

  bool hit=false;

  for(uint32_t j=i;j<end;j++){
   int dx=(int)points[j].x-x;
   int dy=(int)points[j].y-y;

   if(dx*dx+dy*dy<pad*pad){
    hit=true;
    break;
   }
  }

  if(hit){
   memmove(
    points+i,
    points+end,
    (count-end)*sizeof(Point)
   );

   count-=end-i;
   redraw();
   changed();
   return true;
  }

  i=end;
 }

 return false;
}

static bool stroke_in_selection(uint32_t i,uint32_t end){
 int left=min(sel_x,sel_x2);
 int right=max(sel_x,sel_x2);
 int top=min(sel_y,sel_y2);
 int bottom=max(sel_y,sel_y2);

 for(uint32_t j=i;j<end;j++)
  if(points[j].x>=left&&points[j].x<=right
    &&points[j].y>=top&&points[j].y<=bottom)
   return true;

 return false;
}

static void delete_selection(){
 if(!selection_active)return;

 edit_begin();
 uint32_t dst=0;

 for(uint32_t i=0;i<count;){
  uint32_t end=i+1;

  while(end<count&&!points[end].first)
   end++;

  if(!stroke_in_selection(i,end)){
   memmove(
    points+dst,
    points+i,
    (end-i)*sizeof(Point)
   );
   dst+=end-i;
  }

  i=end;
 }

 count=dst;
 selection_active=false;
 redraw();
 changed();
}

static void select_rect_update(){
 if(!selection_box)return;

 int l=min(sel_x,sel_x2);
 int t=min(sel_y,sel_y2);

 int w=max(2,abs(sel_x2-sel_x));
 int h=max(2,abs(sel_y2-sel_y));

 lv_obj_set_pos(selection_box,l,t);
 lv_obj_set_size(selection_box,w,h);
 lv_obj_remove_flag(selection_box,LV_OBJ_FLAG_HIDDEN);
}

static void canvas_touch(lv_event_t *e){
 lv_event_code_t ev=lv_event_get_code(e);

 if(ev==LV_EVENT_RELEASED||ev==LV_EVENT_PRESS_LOST){
  pen_down=false;

  if(current_tool==SELECT&&selection_active&&delete_button)
   lv_obj_remove_flag(
    delete_button,LV_OBJ_FLAG_HIDDEN
   );

  return;
 }

 if(ev!=LV_EVENT_PRESSED&&ev!=LV_EVENT_PRESSING)
  return;

 lv_indev_t *dev=lv_indev_active();
 if(!dev||!canvas)return;

 lv_point_t p;
 lv_indev_get_point(dev,&p);

 lv_area_t a;
 lv_obj_get_coords(canvas,&a);

 int x=p.x-a.x1,y=p.y-a.y1;

 if(x<0||x>=PW||y<0||y>=PH){
  pen_down=false;
  return;
 }

 bool first=!pen_down;

 if(first){
  pen_down=true;
  prev_x=x;
  prev_y=y;

  if(current_tool!=SELECT)
   edit_begin();

  if(current_tool==SELECT){
   sel_x=sel_x2=x;
   sel_y=sel_y2=y;
   selection_active=false;
  }
 }else if(x==prev_x && y==prev_y){
  return;
 }

 if(current_tool==SELECT){
  sel_x2=x;
  sel_y2=y;
  selection_active=true;
  select_rect_update();
 }else if(current_tool==ERASER){
  erase_stroke_at(x,y);
 }else if(count<MAX_POINTS){
  uint8_t type=current_tool==MARKER?MARKER:PEN;
  uint16_t ink=current_tool==MARKER?
   marker_color:pen_color;
  uint8_t radius=current_tool==MARKER?
   marker_size:pen_size;

  points[count++]={
   (uint16_t)x,
   (uint16_t)y,
   ink,
   radius,
   (uint8_t)first,
   type
  };

  if(first)
   dot(x,y,ink,radius,type);
  else
   segment(
    prev_x,prev_y,x,y,ink,radius,type
   );

  changed();
 }else{
  say("Page is full");
 }

 prev_x=x;
 prev_y=y;
}

static lv_obj_t *txt(
 lv_obj_t *parent,const char *content,
 int x,int y,bool heading=false,
 uint32_t shade=INK
){
 lv_obj_t *l=lv_label_create(parent);
 lv_label_set_text(l,content);
 lv_obj_set_pos(l,x,y);

 lv_obj_set_style_text_color(
  l,lv_color_hex(shade),0
 );

 if(heading)
  lv_obj_set_style_text_font(
   l,&lv_font_montserrat_24,0
  );

 return l;
}

static void basic_panel(
 lv_obj_t *obj,uint32_t bg=0xFFFFFF,int radius=15
){
 lv_obj_set_style_radius(obj,radius,0);
 lv_obj_set_style_bg_color(
  obj,lv_color_hex(bg),0
 );
 lv_obj_set_style_border_width(obj,0,0);
 lv_obj_set_style_shadow_width(obj,0,0);
 lv_obj_set_style_pad_all(obj,0,0);
}

static lv_obj_t *button(
 lv_obj_t *parent,
 const char *caption,
 int x,int y,int w,int h,
 lv_event_cb_t cb,
 intptr_t id,
 uint32_t background=0xFFFFFF,
 uint32_t foreground=INK
){
 lv_obj_t *b=lv_button_create(parent);

 lv_obj_set_pos(b,x,y);
 lv_obj_set_size(b,w,h);

 basic_panel(b,background,12);

 lv_obj_set_style_text_color(
  b,lv_color_hex(foreground),0
 );

 // Expand hit area slightly.
 // Keep notebook cards CLICKED to allow scrolling.
 bool scrolling_card=(
  parent!=root &&
  parent!=tool_popup &&
  lv_obj_has_flag(parent,LV_OBJ_FLAG_SCROLLABLE)
 );

 lv_obj_set_ext_click_area(
  b,parent==root?3:2
 );

 lv_obj_add_event_cb(
  b,cb,
  scrolling_card?LV_EVENT_CLICKED:LV_EVENT_PRESSED,
  (void*)id
 );

 lv_obj_t *l=lv_label_create(b);
 lv_label_set_text(l,caption);
 lv_obj_center(l);

 return b;
}

static void clear_screen(){
 lv_obj_t *scr=lv_screen_active();
 lv_obj_clean(scr);
 basic_panel(scr,BG,0);

 lv_obj_remove_flag(
  scr,LV_OBJ_FLAG_SCROLLABLE
 );

 root=scr;

 toast=canvas=paper_view=selection_box=tool_popup=nullptr;
 name_input=soft_keyboard=delete_button=nullptr;

 for(int i=0;i<3;i++)
  template_buttons[i]=nullptr;

 for(int i=0;i<4;i++)
  cover_buttons[i]=nullptr;
}

static void home();
static void new_form(int book_index);
static void editor();

static void open_book(int i){
 if(i<0||i>=book_count)return;

 active_book=i;
 active_page=0;

 books[i].recent=++recent_counter;
 save_catalog();

 load();
 editor();
}

static void home_click(lv_event_t *e){
 intptr_t id=(intptr_t)lv_event_get_user_data(e);

 if(id<0)
  new_form(-1);
 else
  open_book((int)id);
}

static void go_home(lv_event_t*){
 if(dirty&&!save()){
  say("Cannot leave: storage unavailable");
  return;
 }

 home();
}

static void keyboard_event(lv_event_t *e){
 lv_event_code_t c=lv_event_get_code(e);

 if((c==LV_EVENT_FOCUSED||c==LV_EVENT_PRESSED)
  &&soft_keyboard)
  lv_obj_remove_flag(
   soft_keyboard,LV_OBJ_FLAG_HIDDEN
  );

 if((c==LV_EVENT_READY||c==LV_EVENT_CANCEL)
  &&soft_keyboard)
  lv_obj_add_flag(
   soft_keyboard,LV_OBJ_FLAG_HIDDEN
  );
}

static void dismiss_keyboard(lv_event_t *){
 if(soft_keyboard)
  lv_obj_add_flag(
   soft_keyboard,LV_OBJ_FLAG_HIDDEN
  );
}

static void template_click(lv_event_t *e){
 selected_paper=(uint8_t)(intptr_t)
  lv_event_get_user_data(e);

 for(int i=0;i<3;i++)
  if(template_buttons[i])
   lv_obj_set_style_bg_color(
    template_buttons[i],
    lv_color_hex(
     i==selected_paper?0xE4EEFF:0xEEEEF0
    ),0
   );

 say(
  selected_paper==0?
  "Blank paper selected":
  selected_paper==1?
  "Ruled paper selected":
  "Grid paper selected"
 );
}

static void cover_click(lv_event_t *e){
 selected_cover=(uint8_t)(intptr_t)
  lv_event_get_user_data(e);

 for(int i=0;i<4;i++)
  if(cover_buttons[i])
   lv_obj_set_style_border_width(
    cover_buttons[i],
    i==selected_cover?3:0,0
   );

 say("Cover selected");
}

static void confirm_book(lv_event_t*){
 const char *name=name_input?
  lv_textarea_get_text(name_input):"";

 if(editing_book<0&&book_count==MAX_BOOKS){
  say("Library is full");
  return;
 }

 int index=editing_book>=0?
  editing_book:book_count++;

 Book &b=books[index];

 if(editing_book<0){
  memset(&b,0,sizeof(b));
  b.pages=1;
 }

 snprintf(
  b.title,sizeof(b.title),"%s",
  (name&&*name)?name:"Untitled notebook"
 );

 b.paper=selected_paper;
 b.cover=selected_cover;
 b.recent=++recent_counter;

 save_catalog();
 active_book=index;

 if(editing_book<0){
  active_page=0;
  count=0;
  history_reset();
  make_background();
  dirty=false;
 }else{
  load();
  redraw();
 }

 editor();
}

static void form_back(lv_event_t*){
 if(editing_book>=0)
  editor();
 else
  home();
}

static void preview(
 lv_obj_t *parent,
 int style,int x,int y,int w,int h
){
 lv_obj_t *sample=lv_obj_create(parent);

 lv_obj_set_pos(sample,x,y);
 lv_obj_set_size(sample,w,h);

 basic_panel(sample,0xFFFFFF,7);

 lv_obj_remove_flag(
  sample,LV_OBJ_FLAG_CLICKABLE
 );

 if(style>0){
  for(int j=1;j<5;j++){
   lv_obj_t *ln=lv_obj_create(sample);

   lv_obj_set_pos(ln,8,j*19);
   lv_obj_set_size(ln,w-16,1);

   basic_panel(ln,0xC5D5EC,0);

   lv_obj_remove_flag(
    ln,LV_OBJ_FLAG_CLICKABLE
   );
  }

  if(style==2){
   for(int j=1;j<5;j++){
    lv_obj_t *ln=lv_obj_create(sample);

    lv_obj_set_pos(ln,j*28,7);
    lv_obj_set_size(ln,1,h-14);

    basic_panel(ln,0xC5D5EC,0);

    lv_obj_remove_flag(
     ln,LV_OBJ_FLAG_CLICKABLE
    );
   }
  }
 }
}

static void new_form(int index){
 editing_book=index;
 clear_screen();

 selected_paper=index>=0?
  books[index].paper:1;

 selected_cover=index>=0?
  books[index].cover:0;

 txt(
  root,index>=0?"Notebook settings":"New notebook",
  24,28,true
 );

 button(
  root,"Cancel",463,19,112,52,
  form_back,0,0xEDEEF0
 );

 txt(root,"Notebook title",28,110,false,SECONDARY);

 name_input=lv_textarea_create(root);
 lv_obj_set_pos(name_input,25,149);
 lv_obj_set_size(name_input,551,65);

 lv_textarea_set_one_line(name_input,true);

 lv_textarea_set_text(
  name_input,
  index>=0?books[index].title:"My notebook"
 );

 lv_obj_set_ext_click_area(name_input,6);

 lv_obj_add_event_cb(
  name_input,keyboard_event,
  LV_EVENT_FOCUSED,nullptr
 );

 lv_obj_add_event_cb(
  name_input,keyboard_event,
  LV_EVENT_PRESSED,nullptr
 );

 button(
  root,"Done typing",445,216,130,41,
  dismiss_keyboard,0,0xECEEF3,INK
 );

 txt(root,"Page template",28,264,true);

 const char *labels[]={"Blank","Ruled","Grid"};

 for(int i=0;i<3;i++){
  int x=24+i*190;

  lv_obj_t *b=button(
   root,labels[i],
   x,310,178,203,
   template_click,i,
   selected_paper==i?0xE4EEFF:0xEEEEF0,INK
  );

  template_buttons[i]=b;

  lv_obj_t *inner=lv_obj_create(b);

  lv_obj_set_pos(inner,12,14);
  lv_obj_set_size(inner,154,139);

  basic_panel(inner,0xFFFFFF,6);

  preview(inner,i,8,8,138,123);

  lv_obj_remove_flag(
   inner,LV_OBJ_FLAG_CLICKABLE
  );

  lv_obj_t *l=lv_obj_get_child(b,0);
  lv_obj_set_pos(l,58,164);
 }

 txt(root,"Notebook cover",28,555,true);

 const uint32_t covers[]={
  0xDDEAFF,0xF8E2CD,0xDBEFE4,0xF1E2ED
 };

 for(int i=0;i<4;i++){
  cover_buttons[i]=button(
   root," ",24+i*141,603,130,75,
   cover_click,i,covers[i],INK
  );

  lv_obj_set_style_border_width(
   cover_buttons[i],
   i==selected_cover?3:0,0
  );

  lv_obj_set_style_border_color(
   cover_buttons[i],
   lv_color_hex(0x386DD6),0
  );
 }

 button(
  root,
  index>=0?"Save changes":"Create notebook",
  25,733,550,70,
  confirm_book,0,ACCENT,0xFFFFFF
 );

 toast=txt(
  root,"Choose a template and cover",
  28,834,false,SECONDARY
 );

 soft_keyboard=lv_keyboard_create(root);

 lv_obj_set_size(soft_keyboard,SW,310);
 lv_obj_set_pos(soft_keyboard,0,SH-310);

 lv_keyboard_set_textarea(
  soft_keyboard,name_input
 );

 lv_obj_add_flag(
  soft_keyboard,LV_OBJ_FLAG_HIDDEN
 );

 lv_obj_add_event_cb(
  soft_keyboard,keyboard_event,
  LV_EVENT_READY,nullptr
 );

 lv_obj_add_event_cb(
  soft_keyboard,keyboard_event,
  LV_EVENT_CANCEL,nullptr
 );
}

static void home(){
 active_book=-1;
 clear_screen();

 txt(root,"Notes",23,24,true);
 txt(root,"My notebooks",27,100,true);

 char counter[48];

 snprintf(
  counter,sizeof(counter),
  "%d notebooks",book_count
 );

 txt(root,counter,29,143,false,SECONDARY);

 lv_obj_t *list=lv_obj_create(root);

 lv_obj_set_pos(list,13,193);
 lv_obj_set_size(list,574,700);

 basic_panel(list,BG,0);

 lv_obj_add_flag(
  list,LV_OBJ_FLAG_SCROLLABLE
 );

 int order[MAX_BOOKS];

 for(int i=0;i<book_count;i++)
  order[i]=i;

 for(int i=0;i<book_count;i++)
  for(int j=i+1;j<book_count;j++)
   if(books[order[j]].recent>books[order[i]].recent){
    int t=order[i];
    order[i]=order[j];
    order[j]=t;
   }

 const uint32_t covers[]={
  0xDDEAFF,0xF8E2CD,0xDBEFE4,0xF1E2ED
 };

 for(int i=0;i<book_count;i++){
  int idx=order[i];
  int x=8+(i%2)*282;
  int y=8+(i/2)*248;

  lv_obj_t *card=button(
   list,"",x,y,270,234,
   home_click,idx,0xFFFFFF,INK
  );

  lv_obj_t *cover=lv_obj_create(card);

  lv_obj_set_pos(cover,11,10);
  lv_obj_set_size(cover,248,152);

  basic_panel(
   cover,covers[books[idx].cover%4],9
  );

  lv_obj_remove_flag(
   cover,LV_OBJ_FLAG_CLICKABLE
  );

  preview(
   cover,books[idx].paper,83,14,82,120
  );

  txt(
   card,books[idx].title,
   15,173,false,INK
  );

  char meta[40];

  snprintf(
   meta,sizeof(meta),
   "%d pages  -  %s",
   books[idx].pages,
   books[idx].paper==0?"Blank":
   books[idx].paper==1?"Ruled":"Grid"
  );

  txt(card,meta,15,202,false,SECONDARY);
 }

 if(!book_count){
  txt(list,"Your library is empty",140,240,true);
  txt(
   list,"Create your first notebook below",
   124,284,false,SECONDARY
  );
 }

 button(
  root,"+  New notebook",
  314,921,262,71,
  home_click,-1,ACCENT,0xFFFFFF
 );

 toast=txt(
  root,
  storage?"Auto-save ready":
   "Storage unavailable - pages will not persist",
  25,902,false,
  storage?SECONDARY:0xB13F40
 );
}

static void pen_tool(Tool t){
 current_tool=t;
 selection_active=false;

 if(delete_button)
  lv_obj_add_flag(
   delete_button,LV_OBJ_FLAG_HIDDEN
  );

 if(selection_box)
  lv_obj_add_flag(
   selection_box,LV_OBJ_FLAG_HIDDEN
  );

 if(tool_popup){
  lv_obj_delete(tool_popup);
  tool_popup=nullptr;
 }

 say(
  t==PEN?"Pen ready":
  t==MARKER?"Highlighter ready":
  t==ERASER?"Stroke eraser ready":
  "Select an area; then tap Delete"
 );
}

static void color_pick(lv_event_t *e){
 intptr_t id=(intptr_t)lv_event_get_user_data(e);

 if(id<0||id>=
  (intptr_t)(sizeof(PALETTE)/sizeof(PALETTE[0])))
  return;

 if(current_tool==MARKER)
  marker_color=rgb(PALETTE[id]);
 else
  pen_color=rgb(PALETTE[id]);

 say("Brush color updated");
}

static void size_pick(lv_event_t *e){
 intptr_t id=(intptr_t)lv_event_get_user_data(e);

 if(id<0||id>=
  (intptr_t)(sizeof(SIZES)/sizeof(SIZES[0])))
  return;

 if(current_tool==MARKER)
  marker_size=SIZES[id];
 else
  pen_size=SIZES[id];

 say("Brush size updated");
}

static void close_popup(lv_event_t *){
 if(tool_popup){
  lv_obj_delete(tool_popup);
  tool_popup=nullptr;
 }
}

static void popup_settings(){
 if(tool_popup){
  close_popup(nullptr);
  return;
 }

 tool_popup=lv_obj_create(root);

 lv_obj_set_pos(tool_popup,18,149);
 lv_obj_set_size(tool_popup,564,240);

 basic_panel(tool_popup,0xFFFFFF,15);

 lv_obj_set_style_border_width(tool_popup,1,0);

 lv_obj_set_style_border_color(
  tool_popup,lv_color_hex(0xD0D6E0),0
 );

 lv_obj_remove_flag(
  tool_popup,LV_OBJ_FLAG_SCROLLABLE
 );

 txt(
  tool_popup,
  current_tool==MARKER?
   "Highlighter settings":"Pen settings",
  14,10,false,INK
 );

 button(
  tool_popup,"Close X",
  450,7,100,38,
  close_popup,0,0xECEEF3,INK
 );

 txt(tool_popup,"Color",15,51,false,SECONDARY);

 for(int i=0;i<
  (int)(sizeof(PALETTE)/sizeof(PALETTE[0]));i++)
 {
  button(
   tool_popup," ",
   14+i*60,79,51,49,
   color_pick,i,
   PALETTE[i],0xFFFFFF
  );
 }

 txt(tool_popup,"Size",15,150,false,SECONDARY);

 for(int i=0;i<
  (int)(sizeof(SIZES)/sizeof(SIZES[0]));i++)
 {
  char label[12];

  snprintf(
   label,sizeof(label),"%d",(int)SIZES[i]
  );

  button(
   tool_popup,label,
   14+i*91,175,85,48,
   size_pick,i,0xECEEF3,INK
  );
 }
}

static void editor_action(lv_event_t *e){
 intptr_t id=(intptr_t)lv_event_get_user_data(e);

 switch(id){
  case 0:
   go_home(nullptr);
   return;

  case 1:
   if(current_tool==PEN && tool_popup){
    close_popup(nullptr);
    return;
   }
   pen_tool(PEN);
   popup_settings();
   return;

  case 2:
   if(current_tool==MARKER && tool_popup){
    close_popup(nullptr);
    return;
   }
   pen_tool(MARKER);
   popup_settings();
   return;

  case 3:
   pen_tool(ERASER);
   return;

  case 4:
   pen_tool(SELECT);
   return;

  case 5:
   history_go(false);
   return;

  case 6:
   history_go(true);
   return;

  case 7:
   if(dirty&&!save()){
    say("Save failed");
    return;
   }
   new_form(active_book);
   return;

  case 8:
   tools_visible=!tools_visible;
   editor();
   return;

  case 9:
   if(active_page==0)return;

   if(dirty&&!save()){
    say("Save failed");
    return;
   }

   active_page--;
   load();
   editor();
   return;

  case 10:
   if(active_page+1>=MAX_PAGES)return;

   if(dirty&&!save()){
    say("Save failed");
    return;
   }

   active_page++;

   if(active_page>=books[active_book].pages){
    books[active_book].pages=active_page+1;
    save_catalog();
   }

   load();
   editor();
   return;

  case 11:
   delete_selection();
   editor();
   return;

  case 12:
   if(save())
    say("Saved");
   else
    say("Save failed - check storage");
   return;
 }
}

static void editor(){
 clear_screen();

 button(
  root,"<",12,11,54,52,
  editor_action,0,0xFFFFFF
 );

 txt(
  root,books[active_book].title,
  80,24,true
 );

 button(
  root,"...",430,11,58,52,
  editor_action,7,0xFFFFFF
 );

 button(
  root,tools_visible?"Hide":"Tools",
  495,11,91,52,
  editor_action,8,0xF0F2F4
 );

 int top=76;

 if(tools_visible){
  lv_obj_t *bar=lv_obj_create(root);

  lv_obj_set_pos(bar,8,76);
  lv_obj_set_size(bar,584,66);

  basic_panel(bar,0xFFFFFF,12);

  lv_obj_remove_flag(
   bar,LV_OBJ_FLAG_SCROLLABLE
  );

  const char *names[]={
   "Pen","Mark","Erase","Select",
   "Undo","Redo","Save"
  };

  const intptr_t ids[]={1,2,3,4,5,6,12};

  for(int i=0;i<7;i++){
   bool chosen=i<4&&(int)current_tool==i;

   button(
    bar,names[i],
    6+i*82,8,77,50,
    editor_action,ids[i],
    chosen?0xE4ECFF:0xF1F2F5,INK
   );
  }

  top=151;
 }

 int view_height=946-top;

 paper_view=lv_obj_create(root);

 lv_obj_set_pos(paper_view,8,top);
 lv_obj_set_size(paper_view,584,view_height);

 basic_panel(paper_view,0xFFFFFF,10);

 lv_obj_remove_flag(
  paper_view,LV_OBJ_FLAG_SCROLLABLE
 );

 canvas=lv_canvas_create(paper_view);

 lv_canvas_set_buffer(
  canvas,pixels,PW,PH,
  LV_COLOR_FORMAT_RGB565
 );

 lv_obj_set_pos(canvas,8,0);

 lv_obj_add_flag(
  canvas,LV_OBJ_FLAG_CLICKABLE
 );

 lv_obj_remove_flag(
  canvas,LV_OBJ_FLAG_SCROLLABLE
 );

 lv_obj_add_event_cb(
  canvas,canvas_touch,
  LV_EVENT_ALL,nullptr
 );

 selection_box=lv_obj_create(canvas);

 lv_obj_set_size(selection_box,2,2);

 lv_obj_set_style_bg_opa(
  selection_box,LV_OPA_TRANSP,0
 );

 lv_obj_set_style_border_color(
  selection_box,lv_color_hex(ACCENT),0
 );

 lv_obj_set_style_border_width(
  selection_box,2,0
 );

 lv_obj_set_style_radius(
  selection_box,0,0
 );

 lv_obj_remove_flag(
  selection_box,LV_OBJ_FLAG_CLICKABLE
 );

 lv_obj_add_flag(
  selection_box,LV_OBJ_FLAG_HIDDEN
 );

 if(selection_active)
  select_rect_update();

 redraw();

 button(
  root,"< Page",10,960,113,53,
  editor_action,9,0xFFFFFF
 );

 char page[36];

 snprintf(
  page,sizeof(page),
  "Page %d / %d",
  active_page+1,
  books[active_book].pages
 );

 txt(
  root,page,220,976,false,SECONDARY
 );

 button(
  root,"Page +",468,960,117,53,
  editor_action,10,0xFFFFFF
 );

 toast=txt(
  root,
  storage?"Autosave enabled":"Save unavailable",
  13,934,false,
  storage?SECONDARY:0xB13F40
 );

 delete_button=button(
  root,"Delete selected",
  324,881,260,49,
  editor_action,11,0xF5DDE1,INK
 );

 if(!selection_active)
  lv_obj_add_flag(
   delete_button,LV_OBJ_FLAG_HIDDEN
  );
}

static void ticker(lv_timer_t*){
 if(needs_refresh&&canvas&&millis()-last_draw>27){
  lv_obj_invalidate(canvas);
  needs_refresh=false;
  last_draw=millis();
 }

 if(dirty&&millis()-last_edit>1300){
  if(save())
   say("All changes saved");
  else
   say("Auto-save unavailable");

  last_edit=millis();
 }
}

static void start(){
 if(!lvgl_port_lock(-1))return;

 lv_display_set_rotation(
  lv_display_get_default(),
  LV_DISPLAY_ROTATION_90
 );

 points=(Point*)heap_caps_malloc(
  MAX_POINTS*sizeof(Point),
  MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT
 );

 pixels=(uint16_t*)heap_caps_malloc(
  (size_t)PW*PH*2,
  MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT
 );

 if(!points||!pixels){
  Serial.println("Notes V2: PSRAM allocation failed");
  lvgl_port_unlock();
  return;
 }

 pen_color=rgb(INK);
 marker_color=rgb(0xFFE071);

 storage=SPIFFS.begin(true);

 if(storage){
  Serial.printf(
   "Notes V2 storage: %u total, %u used\n",
   (unsigned)SPIFFS.totalBytes(),
   (unsigned)SPIFFS.usedBytes()
  );
 }else{
  Serial.println(
   "Notes V2 SPIFFS mount failed: check partition table"
  );
 }

 load_catalog();
 home();
 lv_timer_create(ticker,30,nullptr);

 lvgl_port_unlock();
}

}
