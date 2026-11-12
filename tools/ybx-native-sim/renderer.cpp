// Native YBX UI preview: actual FreeInkUI 1-bit DisplayTarget + YbxAccountLayout.
// No MCU, Wi-Fi, SPI, SD or production authorization emulation.
#include <FreeInkUI.h>
#include <FreeInkUIDisplayTarget.h>
#include <util/YbxAccountLayout.h>
#include <util/YbxSettingsNavigation.h>
#include "sim_small.h"
#include "sim_body.h"
#include "sim_title.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace f=freeink::ui;
using f::Rect;
using f::Color;
using f::Paint;
struct Params {
 std::string screen="sources",model="x4pro",orientation="portrait",scenario="linked",out="frame.pgm",meta="";
 int focus=1;
};
Params parse(int argc,char**argv){
 Params p;
 for(int i=1;i<argc;i+=2){
  if(i+1>=argc)throw std::runtime_error("missing value");
  const std::string k=argv[i],v=argv[i+1];
  if(k=="--screen")p.screen=v; else if(k=="--model")p.model=v;
  else if(k=="--orientation")p.orientation=v; else if(k=="--scenario")p.scenario=v;
  else if(k=="--out")p.out=v; else if(k=="--meta")p.meta=v; else if(k=="--focus")p.focus=std::clamp(std::atoi(v.c_str()),0,8);
  else throw std::runtime_error("unknown option "+k);
 }
 if(p.screen!="sources"&&p.screen!="pair"&&p.screen!="settings")throw std::runtime_error("unknown screen");
 if(p.model!="x3"&&p.model!="x4"&&p.model!="x4pro")throw std::runtime_error("unknown model");
 if(p.orientation!="portrait"&&p.orientation!="landscape"&&p.orientation!="portrait-inverted"&&p.orientation!="landscape-inverted")throw std::runtime_error("unknown rotation");
 return p;
}
Rect R(int x,int y,int w,int h){return ybxaccountui::rect(x,y,w,h);}
struct Surface {
 int w,h,panelW,panelH,stride;std::vector<uint8_t> pixels;f::DisplayTarget target;
 static f::Orientation nativeRotation(const std::string& orientation){
  if(orientation=="portrait")return f::Orientation::Portrait;
  if(orientation=="portrait-inverted")return f::Orientation::PortraitInverted;
  if(orientation=="landscape-inverted")return f::Orientation::LandscapeClockwise;
  return f::Orientation::LandscapeCounterClockwise;
 }
 Surface(int W,int H,const std::string& orientation):
  w(W),h(H),panelW(std::max(W,H)),panelH(std::min(W,H)),
  stride((panelW+7)/8),pixels(stride*panelH,0xff),
  target(pixels.data(),int16_t(panelW),int16_t(panelH),int16_t(stride),nativeRotation(orientation)){
  target.setFont(f::FONT_SLOT_SMALL,f::kSimSmallFont);
  target.setFont(f::FONT_SLOT_BODY,f::kSimBodyFont);
  target.setFont(f::FONT_SLOT_TITLE,f::kSimTitleFont);
 }
 void save(const std::string& path){
  std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("output error");
  out<<"P5\n"<<panelW<<" "<<panelH<<"\n255\n";
  for(int y=0;y<panelH;y++)for(int x=0;x<panelW;x++){
   unsigned char c=(pixels[y*stride+x/8] & (0x80>>(x%8)))?255:0;
   out.write(reinterpret_cast<const char*>(&c),1);
  }
 }
};
f::TextStyle S(int font=0,bool bold=false,f::TextAlign align=f::TextAlign::Left,int lines=1,Color color=Color::Black){
 f::TextStyle s;s.font=font;s.bold=bold;s.align=align;s.maxLines=uint8_t(lines);s.color=color;return s;
}
void T(Surface& c,Rect r,const std::string& str,int font=0,bool bold=false,
       f::TextAlign align=f::TextAlign::Left,int lines=1,Color color=Color::Black){
 if(!r.empty())c.target.text(r,str.c_str(),S(font,bold,align,lines,color));
}
void line(Surface& c,Rect r){c.target.fill(r,Paint::solid(Color::Black));}
void btn(Surface& c,Rect r,const char* label,int n,int focused){
 const bool active=focused==n;
 c.target.fill(r,Paint::solid(active?Color::Black:Color::White));
 c.target.stroke(r,Paint::solid(Color::Black),2);
 T(c,r.inset({11,5,7,5}),label,f::FONT_SLOT_SMALL,true,f::TextAlign::Center,2,
   active?Color::White:Color::Black);
}
void header(Surface& c, const Params& p){
 c.target.fill(R(0,0,c.w,c.h),Paint::solid(Color::White));
 T(c,R(17,8,85,25),"YBX",f::FONT_SLOT_BODY,true);
 T(c,R(c.w-165,10,145,19),"13:42    80%",f::FONT_SLOT_SMALL,false,f::TextAlign::Right);
 line(c,R(18,37,c.w-36,2));
 T(c,R(18,48,c.w-36,43),p.screen=="pair"?"Подключение":p.screen=="sources"?"Источники":"Настройки",f::FONT_SLOT_TITLE,true);
}
void sources(Surface& c,const Params& p){
 const auto l=ybxaccountui::layout(R(0,0,c.w,c.h),c.target.lineHeight(0),c.target.lineHeight(1),false);
 T(c,l.intro,"Книги из ваших подключённых сервисов",0,false,f::TextAlign::Left,2);
 std::array<bool,3> linked={false,false,false};
 if(p.scenario=="linked")linked={true,true,false};
 if(p.scenario=="all")linked={true,true,true};
 const char* names[]={"Яндекс Книги","WB Книги","ЛитРес"};
 const char* marks[]={"Я","WB","Л"};
 const bool checked=p.scenario!="offline"&&p.scenario!="unpaired"&&p.scenario!="loading";
 for(int i=0;i<3;i++){
  if(l.sources[i].empty())continue;
  c.target.stroke(l.sources[i],Paint::solid(Color::Black),2);
  c.target.fill(l.sourceMarks[i],Paint::solid(linked[i]?Color::Black:Color::LightGray));
  c.target.stroke(l.sourceMarks[i],Paint::solid(Color::Black),1);
  T(c,l.sourceMarks[i],marks[i],1,true,f::TextAlign::Center,1,linked[i]?Color::White:Color::Black);
  T(c,l.sourceTitles[i],names[i],1,true);
  T(c,l.sourceDetails[i],!checked?"Не проверено":linked[i]?"В каталоге":"Нет в каталоге",0);
  if(l.sourceBands){
   line(c,l.sourceActionLine[i]);
   T(c,l.sourceActionLeft[i].inset({7,8,5,8}),"Обновить все",0);
   T(c,l.sourceActionRight[i].inset({7,8,5,8}),"Управление",0);
  }
 }
 const char* state="Нет подключённых источников";
 if(p.scenario=="linked")state="2 сервиса в каталоге";
 if(p.scenario=="all")state="3 сервиса в каталоге";
 if(p.scenario=="offline")state="Нет связи";
 if(p.scenario=="unpaired")state="Ридер не подключён";
 if(p.scenario=="expired")state="Токен истёк";
 if(p.scenario=="removed")state="Ридер удалён";
 if(p.scenario=="provider")state="Требуется вход в сервис";
 T(c,l.status,state,0,false,f::TextAlign::Left,2);
 const bool pair=p.scenario=="unpaired"||p.scenario=="expired"||p.scenario=="removed";
 btn(c,l.actions[0],"Назад",0,p.focus);
 btn(c,l.actions[1],pair?"Войти":"Обновить",1,p.focus);
 btn(c,l.actions[2],"Действия",2,p.focus);
}
void pairing(Surface& c,const Params& p){
 auto l=ybxaccountui::layout(R(0,0,c.w,c.h),c.target.lineHeight(0),c.target.lineHeight(1),true);
 T(c,l.intro,"Откройте YBX на телефоне",1,true);
 if(l.qrInline){
  c.target.fill(l.inlineQr,Paint::solid(Color::White));
  c.target.stroke(l.inlineQr,Paint::solid(Color::Black),2);
  auto q=l.inlineQr.inset({24,24,24,24});
  c.target.stroke(q,Paint::solid(Color::Black),2);
  for(int i=0;i<3;i++){
   int side=std::min(28,q.width/5);
   c.target.stroke(R(i==1?q.right()-side-8:q.x+8,i==2?q.bottom()-side-8:q.y+8,side,side),Paint::solid(Color::Black),3);
  }
  T(c,R(q.x,q.y+q.height/2-12,q.width,27),"QR DEMO",0,true,f::TextAlign::Center);
 }
 T(c,l.codeCaption,"ОДНОРАЗОВЫЙ КОД",0,false,l.compact?f::TextAlign::Left:f::TextAlign::Center);
 T(c,l.code,"ABCD 1234",2,true,l.compact?f::TextAlign::Left:f::TextAlign::Center);
 T(c,l.pairHelp,"Откройте my.ybx.app/pair и введите код",0,false,
   l.compact?f::TextAlign::Left:f::TextAlign::Center,3);
 T(c,l.pairWait,p.scenario=="codeExpired"?"Код истёк":"Ожидание подтверждения",0,false,f::TextAlign::Center,2);
 btn(c,l.actions[0],"Отмена",0,p.focus);
 btn(c,l.actions[1],"Новый код",1,p.focus);
 if(l.actionCount>2)btn(c,l.actions[2],"QR",2,p.focus);
}
void settings(Surface& c,const Params& p){
 const std::array<const char*,9> labels{"Аккаунт YBX","Источники","Синхронизация","Оформление","Язык","Экран сна","Обновления","Поддержка","Системные настройки"};
 const std::array<const char*,9> values{"Настроен","Проверить","Включена","Тема","Русский","Настроить","Проверить","Сведения","Все разделы"};
 const int row=std::min(61,std::max(38,(c.h-95-45)/9)),y0=94;
 f::DeviceContext device=c.target.deviceContext();
 f::InteractionBuffer<32> hits;
 f::InputSnapshot input{};
 f::Frame<32> frame(c.target,device,input,hits);
 for(size_t i=0;i<ybxsettings::ORDER.size();i++){
  Rect r=R(18,y0+int(i)*row,c.w-36,row);
  if(int(i)==p.focus)c.target.fill(r,Paint::solid(Color::LightGray));
  f::SettingRowProps props;
  props.label=labels[i];props.value=values[i];props.drawChevron=true;
  props.labelText=S(1,true);props.valueText=S(0);props.action=1;
  props.valueId=int16_t(i);
  f::settingRow(frame,r,props);
  line(c,R(r.x,r.bottom()-1,r.width,1));
 }
}
int main(int argc,char**argv){
 try{
  Params p=parse(argc,argv);
  const bool portrait=p.orientation=="portrait"||p.orientation=="portrait-inverted";
  const int shortSide=p.model=="x4pro"?528:480,longSide=p.model=="x4pro"?792:800;
  Surface c(portrait?shortSide:longSide,portrait?longSide:shortSide,p.orientation);
  header(c,p);
  if(p.screen=="sources")sources(c,p);
  if(p.screen=="pair")pairing(c,p);
  if(p.screen=="settings")settings(c,p);
  c.save(p.out);
  if(!p.meta.empty()){
    std::ofstream json(p.meta);
    if(!json)throw std::runtime_error("metadata output error");
    json<<"{\"width\":"<<c.w<<",\"height\":"<<c.h<<",\"hits\":[";
    bool first=true;
    auto hit=[&](Rect r,const char* action,int focus) {
      if(r.empty())return;
      if(!first)json<<",";
      first=false;
      json<<"{\"x\":"<<r.x<<",\"y\":"<<r.y<<",\"w\":"<<r.width
          <<",\"h\":"<<r.height<<",\"action\":\""<<action
          <<"\",\"focus\":"<<focus<<"}";
    };
    if(p.screen=="sources"||p.screen=="pair"){
      const bool pairing=p.screen=="pair";
      auto l=ybxaccountui::layout(R(0,0,c.w,c.h),
        c.target.lineHeight(0),c.target.lineHeight(1),pairing);
      hit(l.actions[0],pairing?"cancel":"back",0);
      hit(l.actions[1],pairing?"renew":"primary",1);
      if(l.actionCount>2)hit(l.actions[2],pairing?"qr":"options",2);
      if(pairing){
        if(l.qrInline)hit(l.inlineQr,"qr",2);
      } else {
        for(int i=0;i<3;i++){
          if(l.sourceBands){
            hit(l.sourceActionLeft[i],"refresh",1);
            hit(l.sourceActionRight[i],"cabinet",2);
          }else{hit(l.sources[i],"cabinet",2);}
        }
      }
    }else{
      const int row=std::min(61,std::max(38,(c.h-95-45)/9));
      for(int i=0;i<9;i++)hit(R(18,94+i*row,c.w-36,row),"settings",i);
    }
    json<<"]}\n";
  }
  std::printf("render %dx%d %s %s %s\n",c.w,c.h,p.screen.c_str(),p.model.c_str(),p.orientation.c_str());
  return 0;
 }catch(const std::exception& ex){std::fprintf(stderr,"sim: %s\n",ex.what());return 2;}
}
