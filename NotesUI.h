#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <SPIFFS.h>
#include <FS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lvgl_port.h"

// Elecrow Portrait Notes -- UI inspired by modern tablet notebook apps.
// Capacitive GT911 cannot detect S Pen pressure/palm rejection.
namespace Notes {
constexpr int SCREEN_W=600, SCREEN_H=1024, PAPER_W=568, PAPER_H=770;
constexpr int MAX_BOOKS=6, MAX_PAGES=12, MAX_POINTS=24000;
constexpr uint32_t MAGIC=0x4e4f5435;
struct Point {uint16_t x,y,c; uint8_t r,first;};
struct Header {uint32_t magic,count;};
struct Book {char title[24];uint8_t paper;uint8_t pages;uint8_t cover;};
static Preferences prefs;
static Book books[MAX_BOOKS] = {};
static int total_books=0,active_book=-1,active_page=0;
static Point *points=nullptr;
static uint16_t *pixels=nullptr;
static uint32_t count=0;
static bool storage=false,pressed=false,dirty=false,refresh=false,show_tools=true;
static uint16_t color=0;static uint8_t radius=2;
static int prev_x=0,prev_y=0;
static uint32_t last_draw=0,last_edit=0;
static lv_obj_t *root=nullptr,*paper=nullptr,*toast=nullptr,*page_text=nullptr,*toolbar=nullptr,*home_panel=nullptr;
static lv_obj_t *title_input=nullptr,*paper_choice=nullptr;
static uint8_t choice=1;
static uint16_t rgb(uint32_t hex){return lv_color_to_u16(lv_color_hex(hex));}
static void msg(const char *s){if(toast)lv_label_set_text(toast,s);Serial.println(s);}
static void path(char *buf,size_t n){snprintf(buf,n,"/b%d_p%d.dat",active_book,active_page);}
static void make_bg(){
  uint16_t white=rgb(0xffffff),rule=rgb(0xd7e4f7);
  if(!pixels)return;
  for(int y=0;y<PAPER_H;y++)for(int x=0;x<PAPER_W;x++)pixels[y*PAPER_W+x]=white;
  int kind=active_book>=0?books[active_book].paper:0;
  if(kind==1||kind==2){
    for(int y=32;y<PAPER_H;y+=32)for(int x=0;x<PAPER_W;x++)pixels[y*PAPER_W+x]=rule;
    if(kind==2)for(int x=24;x<PAPER_W;x+=32)for(int y=0;y<PAPER_H;y++)pixels[y*PAPER_W+x]=rule;
  }
  refresh=true;
}
static void dot(int x,int y,uint16_t ink,int r){
 for(int dy=-r;dy<=r;dy++){int yy=y+dy;if(yy<0||yy>=PAPER_H)continue;
  for(int dx=-r;dx<=r;dx++){int xx=x+dx;if(xx<0||xx>=PAPER_W)continue;
   if(dx*dx+dy*dy<=r*r)pixels[yy*PAPER_W+xx]=ink;
  }
 }
}
static void segment(int x,int y,int xx,int yy,uint16_t ink,int r){
 int dx=abs(xx-x),sx=x<xx?1:-1,dy=-abs(yy-y),sy=y<yy?1:-1,err=dx+dy;
 for(;;){dot(x,y,ink,r);if(x==xx&&y==yy)break;int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}
}
static void redraw(){
 make_bg();int px=0,py=0;
 for(uint32_t i=0;i<count;i++){Point &p=points[i];if(p.first)dot(p.x,p.y,p.c,p.r);else segment(px,py,p.x,p.y,p.c,p.r);px=p.x;py=p.y;}
 refresh=true;
}
static bool save(){
 if(!storage || active_book<0)return false;
 char name[30];path(name,sizeof(name));
 File f=SPIFFS.open(name,"w");if(!f)return false;
 Header h={MAGIC,count};bool ok=f.write((const uint8_t*)&h,sizeof(h))==sizeof(h);
 size_t left=count*sizeof(Point),offset=0;const uint8_t *p=(const uint8_t*)points;
 while(ok&&left){size_t n=left>2048?2048:left;ok=f.write(p+offset,n)==n;offset+=n;left-=n;}
 f.close();if(ok){dirty=false;}return ok;
}
static bool load(){
 count=0;make_bg();if(!storage||active_book<0)return false;
 char name[30];path(name,sizeof(name));File f=SPIFFS.open(name,"r");if(!f)return false;
 Header h;bool ok=f.read((uint8_t*)&h,sizeof(h))==sizeof(h)&&h.magic==MAGIC&&h.count<=MAX_POINTS&&f.size()==sizeof(h)+h.count*sizeof(Point);
 if(ok){size_t left=h.count*sizeof(Point),offset=0;uint8_t *p=(uint8_t*)points;
  while(ok&&left){size_t n=left>2048?2048:left;ok=f.read(p+offset,n)==n;offset+=n;left-=n;}
  if(ok){count=h.count;redraw();}
 }
 f.close();dirty=false;return ok;
}
static void remember_books(){
 if(!prefs.begin("portrait",false))return;
 prefs.putInt("n",total_books);prefs.putBytes("books",books,sizeof(books));prefs.end();
}
static void restore_books(){
 if(!prefs.begin("portrait",true))return;
 total_books=prefs.getInt("n",0);if(total_books<0||total_books>MAX_BOOKS)total_books=0;
 if(prefs.getBytesLength("books")==sizeof(books))prefs.getBytes("books",books,sizeof(books));
 prefs.end();
}
static void touch(lv_event_t *e){
 lv_event_code_t ev=lv_event_get_code(e);
 if(ev==LV_EVENT_RELEASED||ev==LV_EVENT_PRESS_LOST){pressed=false;return;}
 if(ev!=LV_EVENT_PRESSED&&ev!=LV_EVENT_PRESSING)return;
 lv_indev_t *d=lv_indev_active();if(!d||!paper||!points)return;
 lv_point_t p;lv_indev_get_point(d,&p);lv_area_t a;lv_obj_get_coords(paper,&a);
 int x=p.x-a.x1,y=p.y-a.y1;
 if(x<0||y<0||x>=PAPER_W||y>=PAPER_H){pressed=false;return;}
 bool first=!pressed;
 if(first){pressed=true;prev_x=x;prev_y=y;}
 else {int dx=x-prev_x,dy=y-prev_y;if(dx*dx+dy*dy<3)return;}
 if(count>=MAX_POINTS){msg("Page full");return;}
 points[count++]={(uint16_t)x,(uint16_t)y,color,radius,(uint8_t)(first?1:0)};
 // A new stroke starts on press, even if the pointer was previously released.
 if(first){dot(x,y,color,radius);}
 else {segment(prev_x,prev_y,x,y,color,radius);}
 prev_x=x;prev_y=y;dirty=refresh=true;last_edit=millis();
}
static lv_obj_t *label(lv_obj_t *p,const char *s,int x,int y,int size){
 lv_obj_t *o=lv_label_create(p);lv_label_set_text(o,s);lv_obj_set_pos(o,x,y);if(size==20)lv_obj_set_style_text_font(o,&lv_font_montserrat_30,0);return o;
}
static lv_obj_t *btn(lv_obj_t *p,const char *s,int x,int y,int w,int h,lv_event_cb_t cb,intptr_t id){
 lv_obj_t *b=lv_button_create(p);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
 lv_obj_set_style_radius(b,13,0);lv_obj_set_style_bg_color(b,lv_color_hex(0xf3f5f9),0);
 lv_obj_set_style_text_color(b,lv_color_hex(0x26364d),0);
 lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,(void*)id);
 lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,s);lv_obj_center(l);return b;
}
static void screen_clear(){
 lv_obj_t *scr=lv_screen_active();lv_obj_clean(scr);lv_obj_set_style_bg_color(scr,lv_color_hex(0xf6f7fb),0);
 lv_obj_remove_flag(scr,LV_OBJ_FLAG_SCROLLABLE);root=scr;toast=nullptr;paper=nullptr;toolbar=nullptr;
}
static void home();static void editor();static void create_form();
static void home_action(lv_event_t *e){intptr_t id=(intptr_t)lv_event_get_user_data(e);if(id==-1){create_form();return;}active_book=(int)id;active_page=0;load();editor();}
static void new_action(lv_event_t *e){(void)e;if(total_books>=MAX_BOOKS)return;
 Book &b=books[total_books];memset(&b,0,sizeof(b));
 const char *name=title_input?lv_textarea_get_text(title_input):"";
 snprintf(b.title,sizeof(b.title),"%s",(name&&*name)?name:"My notebook");b.paper=choice;b.pages=1;b.cover=total_books%3;
 active_book=total_books++;active_page=0;remember_books();count=0;make_bg();editor();
}
static void template_action(lv_event_t *e){choice=(uint8_t)(intptr_t)lv_event_get_user_data(e);msg("Template selected");}
static void back_action(lv_event_t *e){(void)e;home();}
static void create_form(){
 screen_clear();label(root,"New notebook",24,24,20);
 btn(root,"Back",465,16,106,52,back_action,0);
 label(root,"Notebook name",24,103,20);
 title_input=lv_textarea_create(root);lv_obj_set_pos(title_input,24,143);lv_obj_set_size(title_input,550,58);
 lv_textarea_set_one_line(title_input,true);lv_textarea_set_text(title_input,"My notebook");
 label(root,"Choose paper",24,242,20);
 const char *names[]={"Blank","Ruled","Grid"};
 for(int i=0;i<3;i++)btn(root,names[i],24+i*186,299,174,125,template_action,i);
 btn(root,"Create notebook",28,485,544,70,new_action,0);
 toast=label(root,"Choose a template, then Create",30,584,0);
}
static void home(){
 active_book=-1;screen_clear();label(root,"My notebooks",26,30,20);
 label(root,"Your library",28,75,0);btn(root,"+ New",432,22,142,62,home_action,-1);
 for(int i=0;i<total_books;i++){
  int x=20+(i%2)*292,y=132+(i/2)*260;
  lv_obj_t *card=btn(root,books[i].title,x,y,276,222,home_action,i);
  lv_obj_set_style_bg_color(card,lv_color_hex(i%3==0?0xd6e6ff:(i%3==1?0xf2e2cf:0xd8eadf)),0);
  label(card,books[i].paper==0?"Blank paper":(books[i].paper==1?"Ruled paper":"Grid paper"),15,165,0);
 }
 toast=label(root,storage?"Notes are saved on-device":"Storage unavailable: notes may not persist",22,950,0);
}
static void undo(){if(!count)return;uint32_t i=count-1;while(i>0&&!points[i].first)i--;count=i;redraw();dirty=true;last_edit=millis();}
static void editor_action(lv_event_t *e){intptr_t id=(intptr_t)lv_event_get_user_data(e);
 switch(id){
 case 0:if(dirty&&!save())msg("Save failed");home();return;
 case 1:color=rgb(0x202a3c);radius=2;msg("Pen");break;
 case 2:color=rgb(0xc44c67);radius=3;msg("Red pen");break;
 case 3:color=rgb(0x276acb);radius=3;msg("Blue pen");break;
 case 4:color=rgb(0xffe58c);radius=9;msg("Highlighter");break;
 case 5:color=rgb(0xffffff);radius=15;msg("Eraser (blank pages only)");break;
 case 6:undo();break;
 case 7:if(save())msg("Saved");else msg("Cannot save - check storage");break;
 case 8:if(dirty&&!save()){msg("Save failed");return;}active_page=(active_page+1)%MAX_PAGES;
   if(active_page>=books[active_book].pages){books[active_book].pages=active_page+1;remember_books();}
   load();editor();return;
 case 9:show_tools=!show_tools;editor();return;
 case 10:if(active_page>0){if(dirty&&!save()){msg("Save failed");return;}active_page--;load();editor();}return;
 }
}
static void editor(){
 screen_clear();
 const int y=show_tools?156:77;
 btn(root,"< Library",12,9,116,52,editor_action,0);
 label(root,books[active_book].title,145,22,20);
 btn(root,show_tools?"Hide tools":"Show tools",443,9,148,52,editor_action,9);
 if(show_tools){
  toolbar=lv_obj_create(root);lv_obj_set_pos(toolbar,10,73);lv_obj_set_size(toolbar,580,73);
  lv_obj_set_style_bg_color(toolbar,lv_color_hex(0xffffff),0);
  lv_obj_set_style_pad_all(toolbar,0,0);lv_obj_remove_flag(toolbar,LV_OBJ_FLAG_SCROLLABLE);
  const char *names[]={"Pen","Red","Blue","Mark","Erase","Undo","Save"};
  for(int i=0;i<7;i++)btn(toolbar,names[i],5+i*82,9,78,51,editor_action,i+1);
 }
 paper=lv_canvas_create(root);lv_canvas_set_buffer(paper,pixels,PAPER_W,PAPER_H,LV_COLOR_FORMAT_RGB565);
 lv_obj_set_pos(paper,16,y);lv_obj_add_flag(paper,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_remove_flag(paper,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_event_cb(paper,touch,LV_EVENT_ALL,nullptr);
 redraw();
 btn(root,"Previous",12,947,128,58,editor_action,10);
 char buf[40];snprintf(buf,sizeof(buf),"Page %d / %d",active_page+1,books[active_book].pages);
 page_text=label(root,buf,229,967,0);
 btn(root,"Next +",463,947,126,58,editor_action,8);
 toast=label(root,storage?"Auto-save on":"Storage unavailable",22,922,0);
}
static void timer(lv_timer_t*){
 if(refresh&&paper&&millis()-last_draw>28){lv_obj_invalidate(paper);refresh=false;last_draw=millis();}
 if(dirty&&millis()-last_edit>1600){if(save())msg("Auto-saved");else msg("Auto-save failed");last_edit=millis();}
}
static void start(){
 if(!lvgl_port_lock(-1))return;
 // Display rotation is handled by LVGL; input is mapped by the vendor port patch.
 lv_display_set_rotation(lv_display_get_default(),LV_DISPLAY_ROTATION_90);
 points=(Point*)heap_caps_malloc(MAX_POINTS*sizeof(Point),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 pixels=(uint16_t*)heap_caps_malloc(PAPER_W*PAPER_H*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!points||!pixels){Serial.println("Notes: PSRAM allocation failed");lvgl_port_unlock();return;}
 color=rgb(0x202a3c);
 storage=SPIFFS.begin(false);restore_books();
 home();lv_timer_create(timer,30,nullptr);lvgl_port_unlock();
}
}
