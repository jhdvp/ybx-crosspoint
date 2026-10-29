#pragma once
#include <FreeInkUICore.h>
#include <algorithm>
#include <array>
namespace ybxaccountui {
using freeink::ui::Rect;
inline Rect rect(int x,int y,int w,int h) {
 return {static_cast<int16_t>(x),static_cast<int16_t>(y),
         static_cast<int16_t>(std::max(0,w)),static_cast<int16_t>(std::max(0,h))};
}
struct Layout {
 Rect content,brandMark,eyebrow,title,headerLine,intro,sectionTitle;
 std::array<Rect,3> sources{},sourceTitles{},sourceDetails{},sourceMarks{},sourceActionLine{},
                     sourceActionLeft{},sourceActionRight{};
 Rect status,statusCaption,statusDetail;
 Rect codeCaption,code,pairHelp,pairWait,inlineQr;
 std::array<Rect,3> actions{};
 int actionCount=3;
 bool compact=false,qrInline=false,sourceBands=false;
};
// Faithful responsive geometry of artifacts/ybx-screen-plans-20261028:
// 06-sources and 07-pair. The original is a 528x792 visual concept.
inline Layout layout(Rect safe,int smallLine,int bodyLine,bool pairing) {
 Layout o;
 const int pad=std::max(10,std::min(20,static_cast<int>(safe.width/25)));
 o.content=rect(safe.x+pad,safe.y+3,safe.width-2*pad,safe.height-6);
 const Rect c=o.content;
 o.compact=c.height<560;
 const int headerY=o.compact?9:14;
 o.brandMark=rect(c.x,c.y+headerY,26,26);
 o.eyebrow=rect(c.x+34,c.y+headerY, std::max(0,c.width-40),30);
 o.headerLine=rect(c.x,c.y+(o.compact?43:52),c.width,2);
 o.title=rect(c.x,c.y+(o.compact?51:64),c.width,std::max(28,bodyLine+7));
 o.intro=rect(c.x,c.y+(o.compact?87:109),c.width,
              std::max(smallLine+4,o.compact?25:36));
 const int footerH=48,footerY=c.bottom()-footerH;
 const int gap=8;
 const int actionCount=pairing&&o.compact?3:pairing?2:3;
 o.actionCount=actionCount;
 const int w=(c.width-(actionCount-1)*gap)/actionCount;
 for(int i=0;i<actionCount;i++){
   const int x=c.x+i*(w+gap);
   o.actions[i]=rect(x,footerY,(i==actionCount-1?c.right():x+w)-x,footerH);
 }
 if(pairing){
   if(o.compact){
     const int qrSide=std::min({190,std::max(98,c.height-242),std::max(90,c.width/3)});
     o.inlineQr=rect(c.x+6,c.y+128,qrSide,qrSide);
     const int rightX=o.inlineQr.right()+18,rightW=c.right()-rightX;
     o.codeCaption=rect(rightX,c.y+124,rightW,smallLine+2);
     o.code=rect(rightX,o.codeCaption.bottom()+7,rightW,std::max(40,bodyLine+10));
     o.pairHelp=rect(rightX,o.code.bottom()+12,rightW,
                     std::max(0,std::min(3*smallLine+12,footerY-o.code.bottom()-52)));
     o.pairWait=rect(c.x,footerY-smallLine-14,c.width,smallLine+4);
   } else {
     const int qrY=o.intro.bottom()+12;
     const int budget=footerY-qrY-(smallLine+14)-(bodyLine+20)-(smallLine*3+26)-(smallLine+14);
     const int qrSide=std::min({248,c.width-32,std::max(0,budget)});
     o.inlineQr=rect(c.x+(c.width-qrSide)/2,qrY,qrSide,qrSide);
     o.codeCaption=rect(c.x,o.inlineQr.bottom()+12,c.width,smallLine+3);
     o.code=rect(c.x,o.codeCaption.bottom()+2,c.width,std::max(46,bodyLine+17));
     o.pairHelp=rect(c.x+10,o.code.bottom()+10,c.width-20,
                     std::max(0,std::min(3*smallLine+12,footerY-o.code.bottom()-smallLine-25)));
     const int waitY=std::max(o.pairHelp.bottom()+8,footerY-smallLine-18);
     o.pairWait=rect(c.x,waitY,c.width,
                     std::max(0,std::min(smallLine+7,footerY-waitY-7)));
   }
   o.qrInline=o.inlineQr.width>=160&&o.inlineQr.height>=160;
   return o;
 }
 const int statusH=o.compact?36:51;
 o.status=rect(c.x,footerY-8-statusH,c.width,statusH);
 o.statusCaption=rect(o.status.x+3,o.status.y+2,o.status.width-6,smallLine);
 o.statusDetail=rect(o.status.x+3,o.status.y+2+smallLine,o.status.width-6,
                     std::max(0,statusH-smallLine-3));
 o.sectionTitle=o.intro;
 const int top=o.intro.bottom()+(o.compact?9:12);
 const int end=o.status.y-9;
 const int available=std::max(0,end-top);
 const int rowGap=o.compact?6:12;
 const int h=std::max(0,std::min(138,(available-2*rowGap)/3));
 o.sourceBands=h>=94;
 for(int i=0;i<3;i++){
   const int y=top+i*(h+rowGap);
   o.sources[i]=rect(c.x,y,c.width,h);
   const int badge=o.compact?27:38;
   o.sourceMarks[i]=rect(c.x+12,y+(o.sourceBands?12:std::max(2,(h-badge)/2)),badge,badge);
   const int infoX=o.sourceMarks[i].right()+12;
   o.sourceTitles[i]=rect(infoX,y+(o.compact?7:9),
                           c.right()-infoX-9,std::max(bodyLine,22));
   o.sourceDetails[i]=rect(infoX,o.sourceTitles[i].bottom()+2,
                            c.right()-infoX-9,smallLine+3);
   if(o.sourceBands){
     const int bandY=y+h-39;
     o.sourceActionLine[i]=rect(c.x,bandY,c.width,1);
     const int half=c.width/2;
     o.sourceActionLeft[i]=rect(c.x,bandY+1,half,38);
     o.sourceActionRight[i]=rect(c.x+half,bandY+1,c.width-half,38);
   }
 }
 return o;
}
} // namespace ybxaccountui
