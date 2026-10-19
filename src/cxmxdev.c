/* cxmx-mod-lod-114d by cxmx-dev, CC BY-NC-SA 4.0 */
/* cxmxdev.dll - cxmx-mod 1.14d. Loaded by PlugY.ini DllToLoad2=cxmxdev.dll.
   Disable: clear that line. Patches memory only (never the Game.exe file).
   Every patch site is byte-verified first; on any mismatch that feature is skipped.
   A) Socketed potions (qhp qmp qrv qh4 qh5 qm4 qm5 qrl): after a successful drink the
      socketed gems/runes/jewels' stats become a 10 minute buff (state STATE_ID).
   B) /pot chat command (needs the goldcmd caves): server gives a Horadric Cube + test kit,
      placed in the stash (PlugY current stash page), else inventory, else ground.
   2026-10-15 improve6: C (creator hook 0x558D96) removed; one server-only socket fix shared by
   /pot, C2, C3 (game setters + client update); meters count game frames; install deferred until
   BaseMod has patched 0x558D90 (its target is logged, C2 skipped if it changed); log trimmed at
   load; cxmxdev.ini [features] A B C2 C3 D E = 1/0.
   2026-10-15 crashlog+deathclear: cxmxdev-crash.log (vectored handler, chains on); meters clear on player
   death (mode 0/17). 2026-10-15 mainthread: no install thread. DllMain hooks only the draw call 0x44CB22; the rest
   installs on the main thread at the first frames (after PlugY/d2gl), once BaseMod's 0x558D90 jmp
   is stable 200 ms (max 15 s, then C2 skipped). Late code writes restore PAGE_EXECUTE_READ.
   2026-10-15 chargehook: F) a left click (down 0 / held 2) with Charge (skill 107) as the left skill acts as
   Shift held (stand still / force cast). Client mouse dispatcher 0x462D00: 0x462DCF "test bl,4; je" (bl = MK
   flags, 4 = Shift) sets cmd flag 0x20; the hook also sets it for Charge. Left skill = [[player+0xA8]+8], id =
   word [[skill]]. cxmxdev.ini F=1/0.
   2026-10-19 potmeter: E meters are per player. The host only records drinks of its OWN player (unit id = client
   player 0x7A6A70); a LAN guest records its own drink on the client from the use-item packet (0x20 / 0x26, via the
   client send 0x478350) with a wall clock, because the buff itself runs on the host.
   2026-10-19 mfbox: the GF/MF panel boxes add the live socketed-potion buff (player and merc) to the client stats.
   2026-10-19 PTX: portrait teleport works from any area (see PTX above ptp_req).
   2026-10-19 mfshare: a player's socketed-potion MF/GF buff is also put on his merc (real server buff, merc box + meter). */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef unsigned int U32; typedef unsigned char U8;

#define STATE_ID   186
#define BUFF_FRAMES (10*60*25)

typedef void* (__stdcall *AllocSL_t)(void*,U32,U32,U32,U32);
typedef void  (__stdcall *SLSetState_t)(void*,U32);
typedef void  (__stdcall *Toggle_t)(void*,U32,U32);
typedef void  (__stdcall *SetRemCb_t)(void*,void*);
typedef void  (__stdcall *Merge_t)(void*,void*,U32);
typedef void  (__stdcall *SetExpire_t)(void*,U32);
typedef void  (__stdcall *SLSetStat_t)(void*,U32,int,U32);
typedef void* (__stdcall *GetSLState_t)(void*,U32);
typedef void* (__stdcall *FirstItem_t)(void*);
typedef void* (__stdcall *NextItem_t)(void*);
typedef U8*   (__stdcall *ItemsBin_t)(U32);
typedef void* (__fastcall *QCreate_t)(void*,void*,U32,U32,U32,U32);
typedef int   (__fastcall *Potion_t)(void*,void*,void*,U32,U32,U32,U32);

#define AllocSL    ((AllocSL_t)0x6251F0)
#define SLSetState ((SLSetState_t)0x6252D0)
#define ToggleSt   ((Toggle_t)0x639DB0)
#define SetRemCb   ((SetRemCb_t)0x625CE0)
#define MergeSL    ((Merge_t)0x626E10)
#define SetExpire  ((SetExpire_t)0x6260B0)
#define SLSetStat  ((SLSetStat_t)0x627150)
#define GetSLState ((GetSLState_t)0x6256B0)
#define FirstItem  ((FirstItem_t)0x63B2C0)
#define NextItem   ((NextItem_t)0x63DFA0)
#define ItemsBin   ((ItemsBin_t)0x6335F0)
#define QCreate    ((QCreate_t)0x5466B0)
#define STATE_REMOVE_CB ((void*)0x56E900)
#define DATATABLES (*(U8**)0x744304)

static Potion_t orig3, orig5;
static FILE* g_log;
static char g_dir[MAX_PATH], g_ini[MAX_PATH];
static int cfgA=1,cfgB=1,cfgC2=1,cfgC3=1,cfgD=1,cfgE=1,cfgF=1,cfgG=1,cfgH=0;
static int __stdcall sock_fix(void* item);
static void logf_(const char* f, ...) {
  va_list a; if(!g_log) return; va_start(a,f); vfprintf(g_log,f,a); va_end(a); fputc('\n',g_log); fflush(g_log);
}

#define CODE(s) ((U32)(s)[0]|((U32)(s)[1]<<8)|((U32)(s)[2]<<16)|((U32)(s)[3]<<24))
static int is_sock_potion(void* item) {
  static const char* c[]={"qhp ","qmp ","qrv ","qh4 ","qh5 ","qm4 ","qm5 ","qrl "};
  U8* b; U32 code; int i;
  if(!item || *(U32*)item!=4) return 0;
  b=ItemsBin(*(U32*)((U8*)item+4)); if(!b) return 0;
  code=*(U32*)(b+0x80);
  for(i=0;i<8;i++) if(code==CODE(c[i])) return 1;
  return 0;
}

typedef struct { U32 key; int val; } Stat;   /* key = statId<<16 | layer */
static int skip_stat(U32 id){ return id==70||id==72||id==73||id==92||id==194; }
static int add_list(Stat* out,int n,U8* sl,int full) {
  U32* p; int cnt,i,j;
  if(!sl) return n;
  if(full && (*(U32*)(sl+0x10)&0x80000000)) { p=*(U32**)(sl+0x48); cnt=*(short*)(sl+0x4C); }
  else { p=*(U32**)(sl+0x24); cnt=*(short*)(sl+0x28); }
  if(!p||cnt<=0||cnt>512) return n;
  for(i=0;i<cnt;i++){
    U32 key=p[i*2]; int v=(int)p[i*2+1];
    if(skip_stat(key>>16)||v==0) continue;
    for(j=0;j<n;j++) if(out[j].key==key){ out[j].val+=v; break; }
    if(j==n && n<64){ out[n].key=key; out[n].val=v; n++; }
  }
  return n;
}
static int collect(void* item, Stat* out) {
  int n=0; void* inv=*(void**)((U8*)item+0x60); void* c; int guard=0;
  if(inv) for(c=FirstItem(inv); c && guard<8; c=NextItem(c), guard++)
    n=add_list(out,n,*(U8**)((U8*)c+0x5C),1);
  return n;
}
typedef void (__stdcall *MarkUpd_t)(void*);
typedef int  (__fastcall *AddEvt_t)(void*,void*,U32,U32,U32,U32);
typedef int  (__stdcall *SLGetStat_t)(void*,U32,U32);
#define MarkUpd   ((MarkUpd_t)0x64C040)   /* queue unit stat/state update to clients */
#define AddEvt    ((AddEvt_t)0x5417D0)    /* game,unit,type,frame,0,0; type 12 = statlist expiry */
static int sl_get(void* sl,U32 key){
  U32* p=*(U32**)((U8*)sl+0x24); int cnt=*(short*)((U8*)sl+0x28), i;
  for(i=0;p&&i<cnt&&i<512;i++) if(p[i*2]==key) return (int)p[i*2+1];
  return 0;
}
/* ---- E: on-screen buff meters (client draw) ----
   Each drink is its own timed stat list on the server (own 10-minute expiry, removed by the
   game's expiry event); the meter keeps one line per drink with its own countdown. Recorded
   where the buff is applied (host/single player); LAN guests see nothing. */
#define MAXENT 24
/* Time = the server game's own frame counter (game+0xA8, 25 fps), captured by the set-unit-mode
   hook, so meters pause with the game and match the real expiry event (state 186 list). */
typedef struct { U32 key[12]; int val[12]; int n; int num; U32 startf, endf; void* game; int cl; U32 uid; } MEntry;
static MEntry g_ment[MAXENT]; static int g_nment, g_mtotal;
static volatile U32 g_sframe; static void* volatile g_sgame;
static CRITICAL_SECTION g_mcs;
/* potmeter: local (client) player of this process; g_stick = last tick a SERVER game frame ran here (host / single player). */
#define LOCALPL (*(U8**)0x7A6A70)
static volatile DWORD g_stick;
static U32 cl_frame(void){ return GetTickCount()/40; } /* 25 per second, like game frames */
static void meter_add(Stat* st,int n,void* game,U32 startf,U32 endf,int cl){
  int i; MEntry* e;
  EnterCriticalSection(&g_mcs);
  if(g_nment==MAXENT){ memmove(g_ment,g_ment+1,sizeof(MEntry)*(MAXENT-1)); g_nment--; }
  if(g_nment && g_ment[0].game!=game){ g_nment=0; g_mtotal=0; }
  e=&g_ment[g_nment++]; e->n=0; e->num=++g_mtotal; e->startf=startf; e->endf=endf; e->game=game; e->cl=cl;
  if(!cl){ g_sgame=game; g_sframe=startf; }
  for(i=0;i<n&&e->n<12;i++){ e->key[e->n]=st[i].key; e->val[e->n]=st[i].val; e->n++; }
  LeaveCriticalSection(&g_mcs);
}
/* mfbox (2026-10-19, CxMx): the GF/MF boxes read the CLIENT unit (GetUnitStat 0x625480 on LOCALPL / the client merc), but
   the socketed-potion buff is a timed stat list made on the SERVER (apply_buff) and the game does not send stats 79/80 of
   that list to the client, so the boxes showed gear only (MF +9%) while the meters showed the buff. The boxes now add the
   live buff lists the meters track: own player = g_ment (same filter as draw_meters); own merc = g_mme (host: apply_buff on
   a merc whose owner 0x58F0D0 is this PC's player; LAN guest: belt potion fed to the merc, packet 0x26 +5 set). */
#define MAXMME 12
static MEntry g_mme[MAXMME]; static int g_nmme, g_mmtotal;
static void merc_meter_add(Stat* st,int n,void* game,U32 startf,U32 endf,int cl,U32 mid){
  int i; MEntry* e;
  EnterCriticalSection(&g_mcs);
  if(g_nmme==MAXMME){ memmove(g_mme,g_mme+1,sizeof(MEntry)*(MAXMME-1)); g_nmme--; }
  e=&g_mme[g_nmme++]; memset(e,0,sizeof *e); e->num=++g_mmtotal; e->startf=startf; e->endf=endf; e->game=game; e->cl=cl; e->uid=mid;
  if(!cl){ g_sgame=game; g_sframe=startf; }
  for(i=0;i<n&&e->n<12;i++){ e->key[e->n]=st[i].key; e->val[e->n]=st[i].val; e->n++; }
  LeaveCriticalSection(&g_mcs);
}
static void merc_meter_clear(void){ EnterCriticalSection(&g_mcs); g_nmme=0; LeaveCriticalSection(&g_mcs); }
/* sum of stat sid over the live buff lists: merc 0 = own player (g_ment), 1 = own merc with unit id mid (g_mme) */
static int buff_sum(int merc,U32 sid,U32 mid){
  int i,k,s=0,left,cnt; U32 en; MEntry* e; U8* me=LOCALPL; void* gm=g_sgame;
  EnterCriticalSection(&g_mcs);
  __try {
    cnt= merc? g_nmme : g_nment;
    for(i=0;i<cnt;i++){
      e= merc? &g_mme[i] : &g_ment[i];
      if(merc){ if(e->uid!=mid || (!e->cl && e->game!=gm)) continue; }
      else if(e->cl? (e->game!=(void*)me) : (e->game!=gm)) continue;
      en= e->cl? cl_frame() : g_sframe;
      if(en<e->startf) continue;
      left=(int)(e->endf-en); if(left<=0 || left>BUFF_FRAMES+25) continue;
      for(k=0;k<e->n;k++) if((e->key[k]>>16)==sid) s+=e->val[k];
    }
  } __except(1){ s=0; }
  LeaveCriticalSection(&g_mcs);
  return s;
}
static int stat_txt(wchar_t* o,int cap,U32 key,int v){
  U32 id=key>>16;
  switch(id){
  case 0: return _snwprintf(o,cap,L"Str %+d",v);
  case 1: return _snwprintf(o,cap,L"Energy %+d",v);
  case 2: return _snwprintf(o,cap,L"Dex %+d",v);
  case 3: return _snwprintf(o,cap,L"Vit %+d",v);
  case 7: return _snwprintf(o,cap,L"Life %+d",v>>8);
  case 9: return _snwprintf(o,cap,L"Mana %+d",v>>8);
  case 11: return _snwprintf(o,cap,L"Stamina %+d",v>>8);
  case 16: return _snwprintf(o,cap,L"Def %+d%%",v);
  case 19: return _snwprintf(o,cap,L"AR %+d",v);
  case 31: return _snwprintf(o,cap,L"Def %+d",v);
  case 34: return _snwprintf(o,cap,L"DR %+d",v);
  case 35: return _snwprintf(o,cap,L"MDR %+d",v);
  case 36: return _snwprintf(o,cap,L"DR %+d%%",v);
  case 39: return _snwprintf(o,cap,L"Fire Res %+d%%",v);
  case 41: return _snwprintf(o,cap,L"Light Res %+d%%",v);
  case 43: return _snwprintf(o,cap,L"Cold Res %+d%%",v);
  case 45: return _snwprintf(o,cap,L"Poison Res %+d%%",v);
  case 74: return _snwprintf(o,cap,L"Regen %+d",v);
  case 78: return _snwprintf(o,cap,L"Attacker Dmg %+d",v);
  case 79: return _snwprintf(o,cap,L"Gold %+d%%",v);
  case 80: return _snwprintf(o,cap,L"MF %+d%%",v);
  case 89: return _snwprintf(o,cap,L"Light Radius %+d",v);
  case 99: return _snwprintf(o,cap,L"Faster Hit Rec %+d%%",v);
  case 96: return _snwprintf(o,cap,L"Run/Walk %+d%%",v);
  case 127: return _snwprintf(o,cap,L"All Skills %+d",v);
  default: return _snwprintf(o,cap,L"stat%u %+d",id,v);
  }
}
typedef int  (__fastcall *SetFont_t)(int);
typedef void (__fastcall *PrintStr_t)(const wchar_t*,int,U32,U32,U32);
#define SetFont  ((SetFont_t)0x502EF0)
#define PrintStr ((PrintStr_t)0x502320)
#define FPS_FN   0x477980
static int meter_y0(void);
static U8* my_merc(U8* me);
static void draw_meters(void){
  wchar_t line[220]; int i,k,len,y=meter_y0(),left,secs,fill,col,j; U32 now=g_sframe, cnow=cl_frame(), en; void* gm=g_sgame; U8* me;
  if(!g_nment && !g_nmme) return;
  EnterCriticalSection(&g_mcs);
  __try {
    me=LOCALPL; /* potmeter: own player dead (mode 0 death / 17 dead) -> timed lists are gone, clear */
    if(me && *(U32*)me==0 && (*(U32*)(me+0x10)==0 || *(U32*)(me+0x10)==17)){ g_nment=0; __leave; }
    SetFont(0);
    for(i=0,j=0;i<g_nment;i++){
      MEntry* e=&g_ment[i];
      en= e->cl? cnow : now;
      if(e->cl? (e->game!=(void*)me) : (e->game!=gm)) continue; /* other/new game (client: other player unit) */
      if(en<e->startf) continue;
      left=(int)(e->endf-en);                    /* frames */
      if(left>BUFF_FRAMES+25 || left<=-125) continue; /* drop 5 s after expiry */
      g_ment[j++]=*e;
      if(left<=0){ len=_snwprintf(line,200,L"#%d expired: ",e->num); col=1; }
      else { secs=(left+24)/25; fill=(int)((__int64)left*10/BUFF_FRAMES);
        len=_snwprintf(line,200,L"#%d %d:%02d [",e->num,secs/60,secs%60);
        for(k=0;k<10;k++) line[len++]= k<fill? L'|' : L'.';
        line[len++]=L']'; line[len++]=L' ';
        col= secs<60?1:(secs<180?9:2); }
      for(k=0;k<e->n && len<200;k++){ int w; if(k){ line[len++]=L','; line[len++]=L' '; }
        w=stat_txt(line+len,210-len,e->key[k],e->val[k]); if(w<0) break; len+=w; }
      if(len>210) len=210; line[len]=0;
      PrintStr(line,10,y,col,0); y+=13;
    }
    g_nment=j;
    /* mfshare (2026-10-19): merc meter lines under the player's, for the own merc's live buff lists (g_mme) */
    if(g_nmme){ U8* mm= me? my_merc(me):0; U32 mid= mm? *(U32*)(mm+0xC):0; U32 mmode= mm? *(U32*)(mm+0x10):0;
      if(mm && (mmode==0||mmode==12)) g_nmme=0; /* dead merc: her timed lists are gone */
      for(i=0;mid && i<g_nmme;i++){ MEntry* e=&g_mme[i];
        if(e->uid!=mid || (!e->cl && e->game!=gm)) continue;
        en= e->cl? cnow : now; if(en<e->startf) continue;
        left=(int)(e->endf-en); if(left<=0 || left>BUFF_FRAMES+25) continue;
        secs=(left+24)/25; fill=(int)((__int64)left*10/BUFF_FRAMES);
        len=_snwprintf(line,200,L"Merc #%d %d:%02d [",e->num,secs/60,secs%60);
        for(k=0;k<10;k++) line[len++]= k<fill? L'|' : L'.';
        line[len++]=L']'; line[len++]=L' ';
        col= secs<60?1:(secs<180?9:2);
        for(k=0;k<e->n && len<200;k++){ int w; if(k){ line[len++]=L','; line[len++]=L' '; }
          w=stat_txt(line+len,210-len,e->key[k],e->val[k]); if(w<0) break; len+=w; }
        if(len>210) len=210; line[len]=0;
        PrintStr(line,10,y,col,0); y+=13; } }
  } __except(1){ }
  LeaveCriticalSection(&g_mcs);
}
/* ---- MG (2026-10-19): Gold Find / Magic Find on the inventory panel (own player) and the merc panel (own merc).
   Client draw from draw_hook (after the game's panels 0x456EE0), so the host, single player and every LAN guest
   each see their own numbers. Stats 79 item_goldbonus / 80 item_magicbonus through GetUnitStat 0x625480 (layer 0,
   all stat lists: items, charms, sets, buffs; per-level stats 239/240 are op stats already folded into 79/80).
   UI vars (0x4538D0): 1 = inventory (right panel), 0x24 = merc panel (left panel). Panel x like the game's own
   panel cels: right = ResX - [0x7A2858] - 320, left = [0x7A2858]; top = [0x7A285C] + ResY - 480.
   Merc = client monster (hash 0x7A5E70 + 0x200) with a hireling txt id whose owner (0x479150) is the local player. */
typedef int (__stdcall *GetUStat_t)(void*,U32,U32);
#define GetUStat ((GetUStat_t)0x625480)
typedef int (__fastcall *GetUiVar_t)(int);
#define GetUiVar ((GetUiVar_t)0x4538D0)
typedef U32 (__fastcall *MonOwner_t)(U32);
#define MonOwner ((MonOwner_t)0x479150)
static int cfgMG, mgPX=206, mgPY=375, mgMX=14, mgMY=375, mgPW=104, mgMW=250, mgPasses=2, g_mgh;
/* mgfbox (2026-10-19): CxMx - text at the panel top-left overlapped the gear art. Now in the empty bottom strip of each
   panel (measured on his 1068x600 screenshot: panel rel y ~372-428 above the bottom edge 432; merc/char strip rel x ~8-262
   left of the close button at 268-305; inventory: right of the gold box (rel x 83-200), up to ~310). Dark box = DrawRectangle
   0x4F6300 (left, top, right, bottom, color 0 black, trans 1 = half) drawn MGBoxPasses times (2 = ~75% dark), then
   Gold Find on line 1 and Magic Find below. If the text is wider than MGPW/MGMW it uses "GF"/"MF".
   mgfup (2026-10-19, CxMx): font 0 is 22 px at his resolution, so the box is 2*22+8 = 52 px tall; at rel y 385 it hung over
   the panel's bottom gold border (432). Default y 375 -> box bottom 427, just above the gold border, on both panels. */
static U8* my_merc(U8* me){
  int b,g; U8* u; U32 myid=*(U32*)(me+0xC);
  for(b=0;b<128;b++) for(u=*(U8**)(0x7A5E70+1*0x200+b*4),g=0; u && g<256; u=*(U8**)(u+0xE4),g++){
    U32 t=*(U32*)(u+4);
    if(*(U32*)u!=1 || !(t==271||t==338||t==359||t==560||t==561)) continue;
    if(MonOwner(*(U32*)(u+0xC))==myid) return u; }
  return 0;
}
static void mg_box(U8* u,int bx,int by,int maxw,const wchar_t* who,int gfb,int mfb){
  wchar_t a[64], b[64]; int gf=GetUStat(u,79,0)+gfb, mf=GetUStat(u,80,0)+mfb, w1,w2,w,h=0,ww=0,i;
  typedef int (__fastcall *TW_t)(const wchar_t*);
  SetFont(0);
  _snwprintf(a,63,L"%sGold Find: %+d%%",who,gf); a[63]=0; _snwprintf(b,63,L"%sMagic Find: %+d%%",who,mf); b[63]=0;
  w1=((TW_t)0x501820)(a); w2=((TW_t)0x501820)(b); w=w1>w2?w1:w2;
  if(maxw>0 && w+8>maxw){ _snwprintf(a,63,L"%sGF: %+d%%",who,gf); a[63]=0; _snwprintf(b,63,L"%sMF: %+d%%",who,mf); b[63]=0;
    w1=((TW_t)0x501820)(a); w2=((TW_t)0x501820)(b); w=w1>w2?w1:w2; }
  if(!g_mgh){ ((void(__fastcall*)(const wchar_t*,int*,int*))0x502520)(L"Gold Find",&ww,&h); g_mgh=(h>=6&&h<=30)?h:11;
    logf_("MG box: font 0 text height %d (measured %d), inventory box +%d,+%d, merc box +%d,+%d",g_mgh,h,mgPX,mgPY,mgMX,mgMY); }
  for(i=0;i<mgPasses;i++) ((void(__stdcall*)(int,int,int,int,int,int))0x4F6300)(bx,by,bx+w+8,by+2*g_mgh+8,0,1);
  PrintStr(a,bx+4,by+3+g_mgh,4,0);
  PrintStr(b,bx+4,by+4+2*g_mgh,4,0);
}
static int g_mglog[2][2]={{-1,-1},{-1,-1}};
static void mg_log(int w,U8* u,int gb,int mb){ if(gb==g_mglog[w][0] && mb==g_mglog[w][1]) return; g_mglog[w][0]=gb; g_mglog[w][1]=mb;
  logf_("mfbox %s: GF %d gear + %d buff, MF %d gear + %d buff (box shows the totals)",w?"merc":"player",GetUStat(u,79,0),gb,GetUStat(u,80,0),mb); }
static void draw_mgf(void){
  U8* me; U8* m; int rx,ry,wx,ny,top;
  __try {
    me=LOCALPL; if(!me || *(U32*)me!=0) return;
    rx=*(int*)0x71146C; ry=*(int*)0x711470; wx=*(int*)0x7A2858; ny=*(int*)0x7A285C; top=ny+ry-480;
    if(GetUiVar(1)){ int gb=buff_sum(0,79,0), mb=buff_sum(0,80,0); mg_log(0,me,gb,mb); mg_box(me,rx-wx-320+mgPX,top+mgPY,mgPW,L"",gb,mb); }
    if(GetUiVar(0x24) && (m=my_merc(me))){ int gb=0, mb=0; U32 mm=*(U32*)(m+0x10), mid=*(U32*)(m+0xC);
      if(mm==0 || mm==12) merc_meter_clear(); else { gb=buff_sum(1,79,mid); mb=buff_sum(1,80,mid); } /* dead merc: timed lists are gone */
      mg_log(1,m,gb,mb); mg_box(m,wx+mgMX,top+mgMY,mgMW,L"Merc ",gb,mb); }
  } __except(1){ }
}
/* ---- PT (2026-10-19): portrait teleport. Right-click another player's top-left party portrait -> teleport next to them.
   Portraits: 0x494020 -> 0x493B50 (players, roster 0x7BB5C0, own player skipped) / 0x493CE0.. (merc, pets); every portrait
   is a cel (DrawCelContext 0x4F6480, ctx+0x34 = cel file) at x = 15 + 56*i, BOTTOM y = 60 (push 0x3C), life bar
   0x493A00 = rect x..x+46, y 14..19; names (font 6, 0x492FA0, centered on x+22) at y 72 / 82 alternating (stagger toggle).
   The player cel call 0x493C8A is routed through pc_stub: records x + roster unit id ([ebp-0x14] of 0x493B50) and the
   cel size (once). The other 6 portrait cel calls count portraits (meter placement, MM).
   Right button down = WM_RBUTTONDOWN handler 0x44C180 (message table 0x70F2E8; stdcall(msg), +0xC x / +0xE y), hooked at entry.
   A click on a recorded player portrait is consumed (no right skill) and sends chat packet 0x15 "/ptp <id>" through the
   client send 0x478350; the server (host / single player) handles it in server_stub -> ptp_req. */
typedef struct { int x; U32 id; } PCel;
static PCel g_pc[16], g_pcf[16]; static int g_npc, g_npcf, g_nall, g_nallf, g_pcw, g_pch, g_pclog, cfgPT, g_ptok;
static wchar_t g_ptmsg[96]; static DWORD g_ptmsgt, g_ptlast;
static void __stdcall pc_rec(U32 id,int x,U8* ctx){
  __try {
    if(g_npc<16){ g_pc[g_npc].x=x; g_pc[g_npc].id=id; g_npc++; }
    if(!g_pch && ctx){ U8* cf=*(U8**)(ctx+0x34); U8* c= cf? *(U8**)(cf+0x18):0;
      if(c){ int w=*(int*)(c+4), h=*(int*)(c+8);
        if(w>=8&&w<=128&&h>=8&&h<=128){ g_pcw=w; g_pch=h; }
        if(g_pclog<2){ g_pclog++; logf_("PT portrait cel: %dx%d (x %d, bottom y 60)%s",w,h,x,g_pch?"":" - out of range, using 46x46"); } } }
  } __except(1){ }
  g_nall++;
}
__declspec(naked) static void pc_stub(void){ __asm {
  pushad
  push dword ptr [esp+0x24]
  push dword ptr [esp+0x2C]
  push dword ptr [ebp-0x14]
  call pc_rec
  popad
  push 0x4F6480
  ret
}}
__declspec(naked) static void pa_stub(void){ __asm {
  inc g_nall
  push 0x4F6480
  ret
}}
static void cl_send(U8* buf,int n){ __try { __asm {
  push edi
  mov edi,n
  push buf
  mov eax,0x478350
  call eax
  pop edi
} } __except(1){ logf_("PT send fault"); } }
static U8* roster_find(U32 id){ U8* r; int g; for(r=*(U8**)0x7BB5C0,g=0; r && g<16; r=*(U8**)(r+0x80),g++) if(*(U32*)(r+0x10)==id) return r; return 0; }
static void pt_say(const wchar_t* fmt,const char* nm){ wchar_t wn[20]; int i; for(i=0;i<16&&nm&&nm[i];i++) wn[i]=(U8)nm[i]; wn[i]=0;
  _snwprintf(g_ptmsg,95,fmt,wn); g_ptmsg[95]=0; g_ptmsgt=GetTickCount(); }
static int jlv(void* u);
/* PT fix (2026-10-19, CxMx LAN test: Merlin next to him in Blood Moor, still "another area"). The client check compared
   party roster level ids (+0x24) of both players, but the roster level is not a live position: our own roster entry is
   not refreshed by the server (no party-update packet about yourself), so it can hold an old level (town at join) while
   we walk around. Now both levels come from the real units: ours = LOCALPL path room, his = his player unit in the
   client unit hash 0x7A5E70 (type 0, id & 0x7F, next +0xE4) via the same room level call as the server (0x61A1B0).
   Out of client range (no unit) -> no client verdict, the request goes to the server, whose ptp_req check decides. */
static U8* cl_player(U32 id){ U8* u; int g; for(u=*(U8**)(0x7A5E70+(id&0x7F)*4),g=0; u && g<64; u=*(U8**)(u+0xE4),g++) if(*(U32*)u==0 && *(U32*)(u+0xC)==id) return u; return 0; }
static int pt_click(int mx,int my){
  int i, w=g_pcw?g_pcw:46, h=g_pch?g_pch:46, top=60-h, lm, lt, lr, lrm; U8* me; U8* rt; U8* rm; U8* tu; U8 pk[32]; int n; DWORD now;
  if(top>14) top=14;
  for(i=0;i<g_npcf;i++) if(mx>=g_pcf[i].x && mx<g_pcf[i].x+w && my>=top && my<=60) break;
  if(i>=g_npcf) return 0;
  __try {
    me=LOCALPL; if(!me) return 0;
    rt=roster_find(g_pcf[i].id); rm=roster_find(*(U32*)(me+0xC));
    if(!rt){ pt_say(L"Portrait teleport: player not found%s",""); return 1; }
    tu=cl_player(g_pcf[i].id); lm=jlv(me); lt=tu?jlv(tu):-1; lr=(int)*(U32*)(rt+0x24); lrm=rm?(int)*(U32*)(rm+0x24):-1;
    logf_("PT check: me level %d (roster %d), %s unit %s level %d (roster %d)",lm,lrm,(char*)rt,tu?"in range":"not in range",lt,lr);
    /* PTX: another area is no longer refused here; the host moves you (same act: one move, other act: act loading screen) */
    now=GetTickCount(); if(now-g_ptlast<1000) return 1; g_ptlast=now;
    pk[0]=0x15; pk[1]=1; pk[2]=0; n=3+_snprintf((char*)pk+3,20,"/ptp %u",g_pcf[i].id); pk[n++]=0; pk[n++]=0; pk[n++]=0;
    cl_send(pk,n); pt_say(((lm>0&&lt>0&&lt!=lm)||(!tu&&lr>0&&lm>0&&lr!=lm))? L"Teleporting to %s (other area)" : L"Teleporting to %s",(char*)rt);
    logf_("PT request: right-click on portrait of %s (id %u), sent /ptp",(char*)rt,g_pcf[i].id);
  } __except(1){ logf_("PT click fault"); }
  return 1;
}
static volatile int g_rbdeat;
static int __stdcall rbd_check(U8* p){
  int x,y;
  __try { x=*(unsigned short*)(p+0xC); y=*(unsigned short*)(p+0xE); return cfgPT && g_ptok && pt_click(x,y); } __except(1){ return 0; }
}
__declspec(naked) static void rbd_stub(void){ __asm {
  pushad
  push dword ptr [esp+0x24]
  call rbd_check
  mov g_rbdeat,eax
  popad
  cmp g_rbdeat,0
  jne eat
  push ebp
  mov ebp,esp
  push ecx
  mov dword ptr ds:[0x70F2BC],4
  push 0x44C18E
  ret
eat:
  ret 4
}}
static void pt_frame(void){ /* main thread, once per frame from draw_hook */
  memcpy(g_pcf,g_pc,sizeof g_pc); g_npcf=g_npc; g_nallf=g_nall; g_npc=0; g_nall=0;
  if(g_ptmsg[0] && GetTickCount()-g_ptmsgt<3000){ __try { int w,rx=*(int*)0x71146C; SetFont(0);
      w=((int(__fastcall*)(const wchar_t*))0x501820)(g_ptmsg); PrintStr(g_ptmsg,(rx-w)/2,*(int*)0x711470/3,4,0); } __except(1){ } }
}
/* MM (2026-10-19): meters 5 px below the top-left portraits. Geometry from 0x494020/0x493B50: portrait cels bottom y 60,
   names under them (font 6, 0x492FA0) at baseline y 72, then alternating 82/72 per portrait (stagger toggle [ebp-0x10]),
   so the block ends at 72 with one portrait and 82 with two or more (count = portrait cel calls last frame, PT hooks).
   First meter line: its text top = block bottom + MeterGap (5); PrintStr y is the text bottom, so y = bottom + gap + h,
   h = font 0 text height from 0x502520 (fastcall text, &w; stack &h), measured once and logged. No portraits: y 70 as before. */
static int g_mh, mmGap=5;
static int meter_y0(void){ int w=0,h=0;
  if(!g_nallf) return 70;
  if(!g_mh){ __try { SetFont(0); ((void(__fastcall*)(const wchar_t*,int*,int*))0x502520)(L"#1 9:59 [||||||||||]",&w,&h); } __except(1){ h=0; }
    g_mh=(h>=6&&h<=30)?h:12; logf_("MM meter text height %d (measured %d), portraits block bottom 72/82, gap %d",g_mh,h,mmGap); }
  return (g_nallf>=2?82:72)+mmGap+g_mh; }
static volatile int g_meteron, g_done; static DWORD g_t0, g_stab0;
/* potmeter (2026-10-19): LAN guest meter. Client send 0x478350 (push buf, edi = size; 14 callers, all client->server
   packets). 0x20 use inventory item (id +1), 0x26 use belt item (id +1, +5 = 1 feeds the merc). The item is the CLIENT
   copy from the client unit hash 0x7A5E70 + 4*0x200 (id & 0x7F, next +0xE4). Only when no server game ran in this
   process in the last 3 s (a guest); the host and single player use the server record in apply_buff. */
static int g_sendok, g_cdrink;
static void __stdcall send_cb(U8* pk,U32 sz){
  __try {
    U32 id; U8* it; U8* me; int n=0, kids=0, guard, tomerc; Stat st[64]; void* inv; void* c; U32 f;
    if(!g_meteron || !pk || sz<5 || (pk[0]!=0x20 && pk[0]!=0x26)) return;
    tomerc= pk[0]==0x26 && sz>=9 && *(U32*)(pk+5);                /* belt potion given to the merc (mfbox: merc list) */
    if(g_stick && GetTickCount()-g_stick<3000) return;            /* host / single player: server path */
    me=LOCALPL; if(!me) return;
    id=*(U32*)(pk+1);
    for(it=*(U8**)(0x7A5E70+4*0x200+(id&0x7F)*4),guard=0; it && guard<256; it=*(U8**)(it+0xE4),guard++)
      if(*(U32*)it==4 && *(U32*)(it+0xC)==id) break;
    if(!it || !is_sock_potion(it)) return;
    inv=*(void**)(it+0x60);
    if(inv) for(c=FirstItem(inv),guard=0; c && guard<8; c=NextItem(c),guard++) kids++;
    if(!kids) return;                                             /* empty socket: the host gives no buff either */
    n=collect(it,st);
    if(!n) n=add_list(st,0,*(U8**)(it+0x5C),1);                   /* client copy without child stats: parent full list */
    f=cl_frame();
    if(tomerc){ U8* m=my_merc(me); if(m){ merc_meter_add(st,n,0,f,f+BUFF_FRAMES,1,*(U32*)(m+0xC)); logf_("mfbox (LAN guest): fed merc %u item %u (%d stats), client timer",*(U32*)(m+0xC),id,n); } return; }
    meter_add(st,n,(void*)me,f,f+BUFF_FRAMES,1); g_cdrink++;
    { Stat ms[12]; int k=0,q; U8* mm; for(q=0;q<n&&k<12;q++){ U32 sid=st[q].key>>16; if(sid==79||sid==80) ms[k++]=st[q]; } /* mfshare */
      if(k && (mm=my_merc(me))){ merc_meter_add(ms,k,0,f,f+BUFF_FRAMES,1,*(U32*)(mm+0xC)); logf_("mfshare (LAN guest): MF/GF share recorded for merc %u (%d stats), client timer",*(U32*)(mm+0xC),k); } }
    logf_("meter (LAN guest) #%d: drank item %u (%d in socket, %d stats shown), client timer",g_cdrink,id,kids,n);
  } __except(1){ logf_("meter send fault"); }
}
__declspec(naked) static void send_stub(void){ __asm {
  pushad
  push edi
  push dword ptr [esp+0x28]
  call send_cb
  popad
  push ebp
  mov ebp,esp
  sub esp,0x204
  push 0x478359
  ret
}}
static void install(void);
static void try_install(void){ /* main thread, called from the draw hook each frame until done */
  DWORD now=GetTickCount();
  if(*(volatile U8*)0x558D90==0xE9){ if(!g_stab0) g_stab0=now; } else g_stab0=0;
  if((g_stab0 && now-g_stab0>=200) || now-g_t0>=15000){ g_done=1; install(); }
}
static void __stdcall draw_hook(void){
  ((void(*)(void))FPS_FN)();
  if(!g_done) try_install();
  pt_frame();
  if(g_meteron) draw_meters();
  if(cfgMG) draw_mgf();
}
/* Same pattern as the game's own timed states (0x56E970 / potion 0x5BE3F0). Every drink gets
   its OWN stat list: alloc timed list (flag 2) with its own expiry, state 186, remove-callback
   0x56E900, merge into the player, expiry event 12, client update. Lists stack without limit;
   when one expires the game removes only that list (the callback keeps state 186 while another
   list with that state is still on the player, via 0x63A4A0) and life/mana are capped. */
/* mfshare (2026-10-19, CxMx, like his D2R mod): when a PLAYER drinks a socketed potion, the MF (80) and GF (79) part of
   the buff is also put on his merc as a real server buff: the same timed stat list (own expiry, state 186, expiry event)
   through apply_buff on the merc, so she finds more and the list ends with the player's. Merc = live server monster
   (hash game+0x1320, next +0xE4) with a hireling class (271/338/359/560/561) whose owner 0x58F0D0 is the player.
   apply_buff's merc branch then records it for this PC's merc box/meter. Belt feeding the merc is unchanged (full buff). */
static U8* srv_merc(U8* g,U8* pl){ int b,k; U8* u; U32 c,m;
  for(b=0;b<128;b++) for(u=*(U8**)(g+0x1320+b*4),k=0; u && k<256; u=*(U8**)(u+0xE4),k++){
    c=*(U32*)(u+4); if(*(U32*)u!=1 || !(c==271||c==338||c==359||c==560||c==561)) continue;
    m=*(U32*)(u+0x10); if(m==0||m==12) continue;
    if(((U8*(__fastcall*)(void*,int))0x58F0D0)(u,0)==pl) return u; }
  return 0; }
static void apply_buff(void* game, void* pl, Stat* st, int n);
static void merc_share(void* game,void* pl,Stat* st,int n){ Stat ms[12]; int i,k=0; U8* m=0;
  for(i=0;i<n&&k<12;i++){ U32 id=st[i].key>>16; if(id==79||id==80) ms[k++]=st[i]; }
  if(!k) return;
  __try { m=srv_merc((U8*)game,(U8*)pl); } __except(1){ m=0; }
  if(!m){ logf_("mfshare: player %u drank MF/GF, no live merc",*(U32*)((U8*)pl+0xC)); return; }
  logf_("mfshare: player %u MF/GF buff (%d stats) shared with merc %u",*(U32*)((U8*)pl+0xC),k,*(U32*)(m+0xC));
  apply_buff(game,m,ms,k); }
static void apply_buff(void* game, void* pl, Stat* st, int n) {
  U32 frame, expire; void* sl; U8* dt=DATATABLES; int i, v;
  if(!dt || STATE_ID >= *(int*)(dt+0xC4)) { logf_("buff skipped: state %d not in states.txt",STATE_ID); return; }
  frame=*(U32*)((U8*)game+0xA8); expire=frame+BUFF_FRAMES;
  sl=AllocSL(*(void**)((U8*)game+0x1C),2,expire,*(U32*)pl,*(U32*)((U8*)pl+0xC));
  if(!sl) return;
  SLSetState(sl,STATE_ID); ToggleSt(pl,STATE_ID,1); SetRemCb(sl,STATE_REMOVE_CB); MergeSL(pl,sl,1);
  SetExpire(sl,expire);
  for(i=0;i<n;i++){ v=st[i].val; if(v>0x3FFFFFFF) v=0x3FFFFFFF; if(v<-0x3FFFFFFF) v=-0x3FFFFFFF;
    SLSetStat(sl,st[i].key>>16,v,st[i].key&0xFFFF); }
  AddEvt(game,pl,12,expire,0,0);
  MarkUpd(pl);
  { U8* me=LOCALPL; int mine=0; __try { mine= me && *(U32*)pl==0 && *(U32*)(me+0xC)==*(U32*)((U8*)pl+0xC); } __except(1){ mine=0; }
    if(mine) meter_add(st,n,game,frame,expire,0);
    logf_("buff: drink with %d stats until frame %u (own list), player %u %s",n,expire,*(U32*)((U8*)pl+0xC),mine?"(this PC, meter here)":"(LAN guest, meter on their PC)");
    if(!mine && *(U32*)pl==1){ U8* ow=0; int my=0; /* mfbox: merc buff -> merc GF/MF box when her owner is this PC's player */
      __try { ow=((U8*(__fastcall*)(void*,int))0x58F0D0)(pl,0); my= me && ow && *(U32*)ow==0 && *(U32*)(ow+0xC)==*(U32*)(me+0xC); } __except(1){ my=0; }
      if(my) merc_meter_add(st,n,game,frame,expire,0,*(U32*)((U8*)pl+0xC));
      logf_("mfbox: merc %u buff %s",*(U32*)((U8*)pl+0xC),my?"recorded for this PC's merc box":"not this PC's merc"); } }
  if(*(U32*)pl==0){ __try { merc_share(game,pl,st,n); } __except(1){ logf_("mfshare fault"); } }
}static int potion_common(Potion_t orig,void* g,void* p,void* item,U32 a,U32 b,U32 c,U32 d){
  Stat st[64]; int n=0, r;
  __try { if(g&&p&&is_sock_potion(item)) n=collect(item,st); } __except(1){ n=0; logf_("collect fault"); }
  r=orig(g,p,item,a,b,c,d);
  if(r && n>0) { __try { apply_buff(g,p,st,n); } __except(1){ logf_("buff fault"); } }
  return r;
}
static int __fastcall hook3(void* g,void* p,void* item,U32 a,U32 b,U32 c,U32 d){ return potion_common(orig3,g,p,item,a,b,c,d); }
static int __fastcall hook5(void* g,void* p,void* item,U32 a,U32 b,U32 c,U32 d){ return potion_common(orig5,g,p,item,a,b,c,d); }

/* ---- /pot ---- */
/* QCreate (0x5466B0) places its new item with Place (0x560200, auto-fit, page from
   pItemData+0x45; 4 = stash, PlugY's current stash page). While /pot runs, the call at
   0x546741 goes through place_stub: try stash page 4, then inventory page 0; if both
   fail QCreate's own drop path puts it on the ground. Same server routine and packets
   the game uses, so the client updates live. */
typedef int (__fastcall *Place_t)(void*,void*,U32,U32,U32,U32,U32,U32,U32,U32);
#define Place ((Place_t)0x560200)
#define PLACE_CALL 0x546741
static int g_potmode, g_nstash, g_ninv, g_nsock;
static void* g_item;
static int __fastcall place_hook(void* a,void* b,U32 p3,U32 p4,U32 p5,U32 p6,U32 p7,U32 p8,U32 p9,U32 p10){
  U8* d=0; int r;
  if(!g_potmode || !g_item) return Place(a,b,p3,p4,p5,p6,p7,p8,p9,p10);
  __try { d=*(U8**)((U8*)g_item+0x14); } __except(1){ d=0; }
  if(d && is_sock_potion(g_item)){ /* give socketed potions 1 socket: ITEMFLAG_SOCKETED + item_numsockets (194) */
    sock_fix(g_item); g_nsock++;
  }
  if(d){
    d[0x45]=4;
    r=Place(a,b,p3,p4,p5,p6,p7,p8,p9,p10);
    if(r){ g_nstash++; return r; }
    d[0x45]=0;
  }
  r=Place(a,b,p3,p4,p5,p6,p7,p8,p9,p10);
  if(r) g_ninv++;
  return r;
}
__declspec(naked) static void place_stub(void){ __asm {
  mov g_item,esi
  jmp place_hook
}}
static int g_place_ok;
/* Forced uniques for /pot. 0x559CE0 builds the item-drop struct (memset 0) and calls the creator
   0x558D90 at 0x559DFC with EDX = struct. The unique roller 0x5566B0 (non-load path) picks row
   struct+0x40 - 1 when that row is eligible (enabled, same base code, item level >= row lvl, not
   already spawned this game); otherwise it rolls normally. While g_funiq is set, uniq_stub writes
   it into struct+0x40 before jumping on to the creator (BaseMod's jmp there stays in the chain). */
#define UNIQ_CALL 0x559DFC
static volatile U32 g_funiq; static int g_uniq_ok;
__declspec(naked) static void uniq_stub(void){ __asm {
  mov eax,g_funiq
  test eax,eax
  jz go
  mov [edx+0x40],eax
go:
  push 0x558D90
  ret
}}
/* UniqueItems.bin row of a base code: DATATABLES+0xC24 rows (0x14C each, code at +0x28),
   +0xC28 count. Prefer the expected row id(s), else the last row with that code (ours are appended). */
static int uniq_row(const char* code,int want1,int want2){
  U8* t; int n,i; U32 c=CODE(code);
  __try { t=*(U8**)(DATATABLES+0xC24); n=*(int*)(DATATABLES+0xC28);
    if(!t||n<=0) return -1;
    if(want1>=0&&want1<n&&*(U32*)(t+want1*0x14C+0x28)==c) return want1;
    if(want2>=0&&want2<n&&*(U32*)(t+want2*0x14C+0x28)==c) return want2;
    for(i=n-1;i>=0;i--) if(*(U32*)(t+i*0x14C+0x28)==c) return i;
  } __except(1){ }
  return -1;
}
static void* give_unique(void* game,void* pl,const char* code,int want1,int want2,const char* nm){
  void* it=0; int r=uniq_row(code,want1,want2), q=-1;
  if(r<0||!g_uniq_ok){ logf_("/pot %s: skipped (%s)",nm,r<0?"row not found":"unique hook off"); return 0; }
  g_funiq=(U32)r+1;
  __try { it=QCreate(game,pl,CODE(code),99,7,1); } __except(1){ it=0; }
  g_funiq=0;
  if(it){ __try { q=*(U32*)(*(U8**)((U8*)it+0x14)); } __except(1){ q=-1; } }
  logf_("/pot %s: row %d, %s (quality %d)",nm,r,it?"created":"FAILED",q);
  return it;
}
static void __stdcall give_kit(void* game, void* pl) {
  static const char* pots[]={"qhp ","qmp ","qrv ","qh4 ","qh5 ","qm4 ","qm5 ","qrl "};
  static const char* gems[]={"gpr ","gpb ","gpg ","gpy ","gpv ","gpw ","skz ","r30 ","r24 ","r18 "};
  int i, made=0;
  g_nstash=0; g_ninv=0; g_nsock=0; g_potmode=g_place_ok;
  __try {
    if(QCreate(game,pl,CODE("box "),0,2,1)) made++;
    for(i=0;i<8;i++)  if(QCreate(game,pl,CODE(pots[i]),0,2,1)) made++;
    for(i=0;i<10;i++) if(QCreate(game,pl,CODE(gems[i]),0,2,1)) made++;
    for(i=0;i<2;i++)  if(QCreate(game,pl,CODE("jew "),0,4,1)) made++;
    if(give_unique(game,pl,"rin ",402,401,"Ring of the Eternal Munchies")) made++;
    if(give_unique(game,pl,"amu ",403,402,"Tapped Heartstring")) made++;
  } __except(1) { logf_("/pot fault"); }
  g_funiq=0;
  g_potmode=0; g_item=0;
  logf_("/pot: %d items (stash %d, inv %d, ground %d, socketed %d)%s",made,g_nstash,g_ninv,made-g_nstash-g_ninv,g_nsock,
        g_place_ok?"":" [stash hook off]");
}static U8 potpkt[10]={0x15,0x01,0x00,'/','p','o','t',0,0,0};
__declspec(naked) static void client_stub(void){ __asm {
  test edi,edi
  jz notpot
  mov eax,[edi]
  or eax,0x20202000
  cmp eax,0x746F702F
  jne notpot
  cmp byte ptr [edi+4],0
  jne notpot
  push offset potpkt
  mov edi,10
  mov eax,0x478350
  call eax
  pop edi
  pop esi
  pop ebx
  push 0x47CA4F
  ret
notpot:
  push 0x6CB83D
  ret
}}
static void __stdcall ptp_req(U8* g,U8* pl,U8* pkt);
__declspec(naked) static void server_stub(void){ __asm {
  cmp dword ptr [esp+8],8
  jb orig
  cmp dword ptr [eax+3],0x7074702F
  jne notptp
  cmp byte ptr [eax+7],0x20
  jne orig
  push ecx
  push edx
  push eax
  push edx
  push ecx
  call ptp_req
  pop edx
  pop ecx
  xor eax,eax
  ret 8
notptp:
  cmp dword ptr [eax+3],0x746F702F
  jne orig
  cmp byte ptr [eax+7],0
  jne orig
  push ecx
  push edx
  push edx
  push ecx
  call give_kit
  pop edx
  pop ecx
  xor eax,eax
  ret 8
orig:
  push ebp
  mov ebp,esp
  sub esp,0x38C
  push 0x54A5D9
  ret
}}

/* ---- Shared socket fix (used by /pot, C2, C3) ----
   C (hook in the creator at 0x558D96) was REMOVED 2026-10-15: it never saw drops (BaseMod owns
   0x558D90) and its global return-address stack was not thread-safe.
   SERVER copies only: a server unit has its game at +0x80 (see 0x554010); client copies have 0
   (editing the client copy caused Halt 900 at 0x4C25FE). Uses the game's own setters so the
   change is queued for the client: 0x6280D0 item flag (0x800 socketed), 0x627260 unit stat
   (194 = 1), 0x64C040 mark unit update. C2 (0x558AA0) runs before the item is placed; C3 runs at
   the entry of set-unit-mode (before it queues the update; 0x558AA0 only marks the unit new,
   +0xC4 |= 0x2000000, after mode 3), so the drop is sent with its socket. */
typedef void (__stdcall *SetFlag_t)(void*,U32,U32);
typedef void (__stdcall *SetUStat_t)(void*,U32,int,U32);
#define SetItemFlag ((SetFlag_t)0x6280D0)
#define SetUnitStat ((SetUStat_t)0x627260)
static int g_ndrop, g_nseen, g_nload; static int g_smode=-1; /* item mode seen by C3, -1 = C2 or /pot */
static int __stdcall sock_fix(void* item){
  U8* d; void* sl; int ns; U32 fl; int did=0;
  __try {
    if(!item || *(U32*)item!=4) return 0;
    if(!*(void**)((U8*)item+0x80)) return 0;          /* client copy: never touch */
    if(!is_sock_potion(item)) return 0;
    d=*(U8**)((U8*)item+0x14); sl=*(void**)((U8*)item+0x5C); if(!d) return 0;
    if(!(*(U32*)(d+0x18)&0x80000)){ if(++g_nload<=20||g_nload%100==0) logf_("potload #%d: item %u is still being read from a save (flags %08X) - socket later (tick/sweep)",g_nload,*(U32*)((U8*)item+0xC),*(U32*)(d+0x18)); return 0; } /* potload 2026-10-19: no INIT flag 0x80000 = mid-decode (the decoder sets the mode before it reads the socket field); socketing here made old socketless potions read 4 bits long -> "bad dead bodies" on join */
    fl=*(U32*)(d+0x18); ns=sl?sl_get(sl,194u<<16):0;
    g_nseen++;
    if(ns<1 || !(fl&0x800)){
      SetItemFlag(item,0x800,1); if(ns<1) SetUnitStat(item,194,1,0); MarkUpd(item); g_ndrop++; did=1; }
    if(g_nseen<=30 || (g_nseen%50)==0)
      logf_("socketed potion #%d (server, mode %d): flags %08X sockets %d -> %d (fixed %d so far)",g_nseen,g_smode,fl,ns,(sl=*(void**)((U8*)item+0x5C))?sl_get(sl,194u<<16):-1,g_ndrop);
  } __except(1){ logf_("sock_fix fault"); }
  return did;
}
/* potclient (2026-10-16): a test log: the server tick DID socket item 29 one frame late ('already known to client'),
   and an update does not resend the item, so the client copy kept no socket = white label. The client copy (+0x80 = 0) of
   our 8 codes now gets the same socket flag 0x800 + 1 socket stat when it enters ground/dropping mode (3/5), so the label
   matches the server copy (always socketed by the tick/C2/C3). Display only; the server copy decides what you pick up. */
static int g_pcl;
static void pot_client_fix(void* u){ U8* d; void* sl; int ns;
  __try { if(!u||*(U32*)u!=4||*(void**)((U8*)u+0x80)||!is_sock_potion(u)) return;
    d=*(U8**)((U8*)u+0x14); if(!d) return; sl=*(void**)((U8*)u+0x5C); ns=sl?sl_get(sl,194u<<16):0;
    if(ns>=1 && (*(U32*)(d+0x18)&0x800)) return;
    SetItemFlag(u,0x800,1); if(ns<1) SetUnitStat(u,194,1,0); g_pcl++;
    if(g_pcl<=40||g_pcl%50==0) logf_("potclient #%d: client copy item %u mode %u had no socket -> grey (flag 0x800, 1 socket)",g_pcl,*(U32*)((U8*)u+0xC),*(U32*)((U8*)u+0x10));
  } __except(1){ logf_("potclient fault"); } }
/* potfix (2026-10-15): diagnostics + backstops for the socketed potions. Every set-unit-mode and ground-drop of one of
   our 8 codes is logged before any check (first 80 per run: game+0x80, mode, item flags, stat list, sockets, item-data
   +0x28/+0x2C (item level candidates), caller). sock_fix no longer needs an existing stat list (a fresh white potion may
   have none; SetUnitStat creates it) - that early return is the likely reason sock_fix never logged. C3 now fixes our
   potions on EVERY mode change (0 inventory/stash/cube, 1 equip, 2 belt, 3 ground, 4 cursor, 5 dropping), and the first
   walk/run of each player per game sweeps his whole item list (inventory, stash, cube, belt) once. */
static int g_ndiag;
static void pot_diag(void* u,const char* where,int mode,U32 ret){ U8* d; void* sl; U8* b; U32 g,fl=0,i28=0,i2c=0; int ns=-1;
  if(g_ndiag>=80) return;
  __try { if(!u||*(U32*)u!=4||!is_sock_potion(u)) return;
    g=*(U32*)((U8*)u+0x80); d=*(U8**)((U8*)u+0x14); sl=*(void**)((U8*)u+0x5C); b=ItemsBin(*(U32*)((U8*)u+4));
    if(d){ fl=*(U32*)(d+0x18); i28=*(U32*)(d+0x28); i2c=*(U32*)(d+0x2C); }
    if(sl) ns=sl_get(sl,194u<<16);
    g_ndiag++;
    logf_("potdiag #%d %s: item %u %.3s mode %d (unit mode %u) +0x80 %08X data %p flags %08X stats %p sockets %d ilvl? %u/%u caller %08X",
      g_ndiag,where,*(U32*)((U8*)u+0xC),b?(char*)(b+0x80):"???",mode,*(U32*)((U8*)u+0x10),g,d,fl,sl,ns,i28,i2c,ret);
  } __except(1){ logf_("potdiag fault"); } }
static int __stdcall drop_cb(void* item,U32 ret){ pot_diag(item,"drop",-1,ret); return sock_fix(item); }
static void* g_swu[16]; static U32 g_swid[16]; static void* g_swgame;
static void pot_sweep(void* u){ int i,n=0,fixed=0; U32 id=*(U32*)((U8*)u+0xC); void* g=*(void**)((U8*)u+0x80); void* inv; void* it;
  if(g!=g_swgame){ memset(g_swu,0,sizeof g_swu); g_swgame=g; }
  for(i=0;i<16;i++) if(g_swu[i]==u && g_swid[i]==id) return;
  for(i=0;i<16 && g_swu[i];i++); if(i==16) i=0; g_swu[i]=u; g_swid[i]=id;
  inv=*(void**)((U8*)u+0x60); if(!inv) return;
  g_smode=100;
  for(it=FirstItem(inv); it && n<600; it=NextItem(it), n++) if(is_sock_potion(it)){ pot_diag(it,"sweep",*(int*)((U8*)it+0x10),0); fixed+=sock_fix(it); }
  g_smode=-1;
  logf_("potfix sweep: player %u, %d items checked, %d socketed potion(s) fixed",id,n,fixed);
}
/* C2: ground-drop routine 0x558AA0 (ecx,edx, stack: item, room, x, y; ret 0x10). Hook after the
   frame at 0x558AA6; item = [ebp+8]. */
__declspec(naked) static void drop_stub(void){ __asm {
  pushad
  push dword ptr [ebp+4]
  push dword ptr [ebp+8]
  call drop_cb
  popad
  push edi
  mov edi,[ebp+0xC]
  test edi,edi
  push 0x558AAC
  ret
}}
/* Set-unit-mode hook 0x624690(unit, mode) (stdcall), hook at 0x624694. Every server unit passes
   here, so it also records the server game and its frame for the meters. C3: a socketed potion
   put into mode 3 (ground) gets its socket (server copy only, via sock_fix). */
/* +0x80 is the game only on server units; client copies hold 0 or small/odd values. Validate before
   reading so the caught read at mode_cb no longer fills cxmxdev-crash.log: >= 0x10000, below 2 GB,
   committed readable memory (VirtualQuery, cached for the last good game), and the game's own
   back-pointer check is not available, so also require the frame field to be readable. */
static void* g_lastgame;
static int game_ok(void* g){
  MEMORY_BASIC_INFORMATION mi;
  if((U32)g<0x10000||(U32)g>=0x80000000u) return 0;
  if(g==g_lastgame) return 1;
  if(!VirtualQuery(g,&mi,sizeof mi)||mi.State!=MEM_COMMIT||(mi.Protect&(PAGE_GUARD|PAGE_NOACCESS))
     ||!(mi.Protect&(PAGE_READWRITE|PAGE_EXECUTE_READWRITE))) return 0;
  if((U8*)g+0x200>(U8*)mi.BaseAddress+mi.RegionSize) return 0;
  g_lastgame=g; return 1;
}
/* potnodrop (2026-10-16): monster/ground drops of our socketed potions never pass set-unit-mode or 0x558AA0 as SERVER
   units (a test log: every mode 3/5 sighting was the CLIENT copy, +0x80 = 0, which sock_fix rightly skips), so C2/C3
   never fixed them. Now every server game frame, at the very start of the frame update (before 0x52D7B0 runs), every item
   unit in the game's item table (game+0x1720, 128 buckets; 2026-10-16 fix: 0x1920 was the wrong list, so the tick never saw an item - 0x557F10 walks items at 0x1720; both lists are scanned, type 4 checked) that is one of our 8 codes without socket flag/stat gets
   sock_fix. Items created during the previous frame are fixed before their first update goes out. */
static int g_ptfix;
/* potremove (2026-10-16): a socketless potion of our 8 codes the client already has on the ground (sent white) is removed
   with the game's own ground-item expiry routine 0x558B40 (ecx game, edx item: room unlink, client remove packets, free).
   potsend (below) sockets new ones before their first send, so this is the last-resort backstop. */
static int g_prm;
static void pot_remove(U8* g,U8* u){ U32 id=*(U32*)(u+0xC), m=*(U32*)(u+0x10); U8* b=ItemsBin(*(U32*)(u+4)); char c[4]={0};
  if(b) memcpy(c,b+0x80,3);
  __try { __asm {
    mov ecx,g
    mov edx,u
    mov eax,0x558B40
    call eax
  } g_prm++; logf_("potremove #%d: %s item %u (mode %u) was on the ground with no socket, already sent white -> removed",g_prm,c,id,m); }
  __except(1){ logf_("potremove fault (item %u)",id); } }
static void __stdcall pot_tick(U8* g){ int b,n,t,nrm=0; U8* u; U8* rm[16];
  g_stick=GetTickCount(); if(!g_stick) g_stick=1; /* potmeter: a server game runs in this process */
  if(!cfgC3) return;
  __try { if(!game_ok(g)) return;
    for(t=0;t<2;t++) for(b=0;b<128;b++) for(u=*(U8**)(g+(t?0x1920:0x1720)+b*4),n=0; u && n<256; u=*(U8**)(u+0xE4),n++){
      U8* d; void* sl; int ns; U32 nw;
      if(*(U32*)u!=4 || !is_sock_potion(u)) continue;
      d=*(U8**)(u+0x14); if(!d) continue; sl=*(void**)(u+0x5C); ns=sl?sl_get(sl,194u<<16):0;
      if(ns>=1 && (*(U32*)(d+0x18)&0x800)) continue;
      nw=*(U32*)(u+0xC4)&0x2000000;
      if(!nw && *(U32*)(u+0x10)==3 && nrm<16){ rm[nrm++]=u; continue; } /* potremove: already shown white on the ground */
      g_smode=200+(int)*(U32*)(u+0x10);
      if(sock_fix(u)){ g_ptfix++; if(g_ptfix<=40||g_ptfix%50==0) logf_("potfix tick #%d: item %u mode %u socket added (%s)",g_ptfix,*(U32*)(u+0xC),*(U32*)(u+0x10),nw?"new, not sent yet":"already known to client"); }
      g_smode=-1; }
    for(b=0;b<nrm;b++) pot_remove(g,rm[b]);
  } __except(1){ g_smode=-1; logf_("potfix tick fault"); } }
/* potcreate (2026-10-16): stop them at the source. Every item the game creates goes through the creator entry 0x558D90
   (ecx game, edx spawn struct, 1 stack arg, ret 4; ~20 callers: monster/TC drops 0x5592B9..0x55A6AF, vendors/gamble,
   cube, quest gifts, /pot). BaseMod replaces that entry with a jmp to its own creator. We point that jmp at a wrapper that
   calls BaseMod's creator, then sockets one of our 8 codes on the returned item - before any drop, placement or send can
   happen. potsend + potremove stay as backstops. */
static U32 g_bmcreate; static int g_pcr;
static void __stdcall potcreate_cb(U8* it){ U8* d; void* sl; int ns;
  __try { if(!it||*(U32*)it!=4||!is_sock_potion(it)) return;
    d=*(U8**)(it+0x14); if(!d) return; sl=*(void**)(it+0x5C); ns=sl?sl_get(sl,194u<<16):0;
    if(ns>=1 && (*(U32*)(d+0x18)&0x800)) return;
    g_smode=400; if(sock_fix(it)){ g_pcr++; if(g_pcr<=40||g_pcr%50==0) logf_("potcreate #%d: item %u socketed at creation (before any send)",g_pcr,*(U32*)(it+0xC)); }
    else logf_("potcreate: item %u NOT socketed (game ptr %08X)",*(U32*)(it+0xC),*(U32*)(it+0x80));
    g_smode=-1; } __except(1){ g_smode=-1; logf_("potcreate fault"); } }
__declspec(naked) static void potcreate_wrap(void){ __asm {
  push dword ptr [esp+4]
  call dword ptr [g_bmcreate]
  pushad
  push eax
  call potcreate_cb
  popad
  ret 4
}}
static void potcreate_install(void){ U8 c[5]; U32 rel;
  if(!cfgC3){ logf_("potcreate: OFF (C3=0)"); return; }
  __try { if(*(U8*)0x558D90!=0xE9){ logf_("potcreate: SKIPPED (no BaseMod jmp at 0x558D90)"); return; }
    g_bmcreate=0x558D95+*(U32*)0x558D91; } __except(1){ logf_("potcreate: SKIPPED (read fault)"); return; }
  rel=(U32)potcreate_wrap-(0x558D90+5); c[0]=0xE9; memcpy(c+1,&rel,4);
  if(write_mem(0x558D90,c,5)) logf_("potcreate: ON (0x558D90 -> wrapper -> BaseMod creator %08X)",g_bmcreate);
  else { g_bmcreate=0; logf_("potcreate: SKIPPED (write failed)"); } }
/* potsend (2026-10-16): the per-unit client update routines 0x5431F0 (unit in esi) and 0x5433F0 (edx unit) send a unit whose
   'new' flag 0x2000000 is set. Hooked at entry: one of our 8 codes still socketless gets its socket right there, BEFORE the
   first item packet, so the client only ever sees it grey. */
static int g_psn;
static void __stdcall potsend_cb(U8* u){ U8* d; void* sl; int ns;
  __try { if(!u||*(U32*)u!=4||!*(void**)(u+0x80)||!is_sock_potion(u)) return;
    d=*(U8**)(u+0x14); if(!d) return; sl=*(void**)(u+0x5C); ns=sl?sl_get(sl,194u<<16):0;
    if(ns>=1 && (*(U32*)(d+0x18)&0x800)) return;
    g_smode=300+(int)*(U32*)(u+0x10);
    if(sock_fix(u)){ g_psn++; if(g_psn<=40||g_psn%50==0) logf_("potsend #%d: item %u mode %u socket added before send (%s)",g_psn,*(U32*)(u+0xC),*(U32*)(u+0x10),(*(U32*)(u+0xC4)&0x2000000)?"new":"already known"); }
    g_smode=-1; } __except(1){ g_smode=-1; logf_("potsend fault"); } }
__declspec(naked) static void potsend_a(void){ __asm {
  pushad
  push esi
  call potsend_cb
  popad
  push ebp
  mov ebp,esp
  sub esp,0x10
  push 0x5431F6
  ret
}}
__declspec(naked) static void potsend_b(void){ __asm {
  pushad
  push edx
  call potsend_cb
  popad
  push ebp
  mov ebp,esp
  sub esp,8
  push 0x5433F6
  ret
}}
static int g_fhook;
static int g_jon;
/* G (2026-10-15 lvl20): every player, any class, gets HARD (base) level 20 Charge 107, Teleport 54, Telekinesis 43.
   Server only (unit +0x80 game ok), applied from mode_cb before the mode change. Skill list [unit+0xA8]: first skill +4;
   skill: +0 pSkillsTxt (word id), +4 next, +0x28 base level. 0x56DEB0 = D2Game set-base-level wrapper
   (__fastcall ecx unit, edx skill; stack level, bRemove; ret 8): D2Common 0x647280 adds the skill if missing, sets +0x28,
   refreshes stats and sends the player's client the update. Only raises a level below 20 (never lowers). Non-class skills
   are not saved in .d2s, so they are reapplied on every join; Paladin Charge (class skill) is saved at 20. */
#define SETBASELVL 0x56DEB0
static const int g_lvl20[3]={107,54,43};
static int skill_base(void* u,int id){
  U8* s; int n=0; U8* l=*(U8**)((U8*)u+0xA8); if(!l) return -1;
  for(s=*(U8**)(l+4); s && n<512; s=*(U8**)(s+4), n++){
    U8* t=*(U8**)s; if(t && *(short*)t==id && *(int*)(s+0x34)==-1) return *(int*)(s+0x28); } /* +0x34 owner -1 = own skill, not item oskill (0x647110) */
  return 0;
}
static void set_base(void* u,int id,int lvl){ __asm {
  push 0
  push lvl
  mov edx,id
  mov ecx,u
  mov eax,SETBASELVL
  call eax
}}
/* gsync: 0x5531C0 = get pClient (fastcall ecx unit, stack 1 dummy line, ret 4; 0 if not a player);
   0x53C4A0 = send SCMD 0x21 set-skill (fastcall ecx client, edx unit; stack skill word, base level byte, flag byte; ret 0xC).
   Same pair the respec code uses at 0x5703DD/0x5703E6. Client handler 0x45DCD0 -> 0x647280 on the client unit. */
static void sync_client(void* u,int id,int lvl){ __asm {
  push 0x10a1
  mov edx,0x6dab88
  mov ecx,u
  mov eax,0x5531C0
  call eax
  test eax,eax
  jz done
  push 0
  push lvl
  push id
  mov edx,u
  mov ecx,eax
  mov eax,0x53C4A0
  call eax
done:
}}
static void* g_g20u[16]; static U32 g_g20id[16]; static void* g_g20game;
static void lvl20_fix(void* u){
  int i,b; U32 id=*(U32*)((U8*)u+0xC); void* g=*(void**)((U8*)u+0x80);
  if(g!=g_g20game){ memset(g_g20u,0,sizeof g_g20u); g_g20game=g; }
  for(i=0;i<16;i++) if(g_g20u[i]==u && g_g20id[i]==id) return; /* once per player unit per game */
  for(i=0;i<16 && g_g20u[i];i++); if(i==16) i=0; g_g20u[i]=u; g_g20id[i]=id;
  for(i=0;i<3;i++){ int id2=g_lvl20[i],a; b=skill_base(u,id2);
    if(b>0 && b<20) set_base(u,id2,20); /* g2: only raise skills the player already owns */
    a=skill_base(u,id2);
    if(b>0) sync_client(u,id2,a); /* gsync: tell the client (SCMD 0x21) - 0x56DEB0 does not */
    logf_("G unit %u skill %d: base %d -> %d%s",id,id2,b,a,b>0?" (sent 0x21)":" (not owned, skipped)"); }
}
/* I (DEV + BOSS-mod, cxmxdev.ini I=1, default 0): Fallen King (superuniques row 66, with escort) once per game
   near the Blood Moor (level 2) / Cold Plains (level 3) edge. Server, from mode_cb on a player walk/run.
   Disassembly (1.14d): 0x5A49B0 = game's superunique spawn (__fastcall ecx game, edx Room1; stack x, y, id; ret 0xC),
   its only caller is the preset spawner 0x54E600; it checks/sets the per-game spawned bit at game+0x1D30 and
   spawns the minions (0x5A2120). x=y=0 -> 0x5A09E0 -> 0x54DC40 picks a free walkable spot in the room.
   0x61A1B0 = level id of a Room1 (__stdcall, Room1+0x10 -> Room2). Room1: +0 near-room list, +0x24 count;
   unit path +0x2C, path +0x1C = Room1. Trigger: the player's room is Blood Moor and touches a Cold Plains room. */
typedef void* (__fastcall *SpawnSU_t)(void* game,void* room,int x,int y,int id);
typedef int (__stdcall *RoomLvl_t)(void* room);
#define SPAWN_SU ((SpawnSU_t)0x5A49B0)
#define ROOM_LVL ((RoomLvl_t)0x61A1B0)
#define SU_FALLENKING 66
static int cfgI, g_iok; static void* g_igame; static int g_idone;
/* I v2 (path guard): place him on the road a few subtiles inside the Blood Moor from the Cold Plains border.
   Act 1 wilderness borders are walls/cliffs except where the road crosses, so the walkable gap on the shared
   edge of a loaded Blood Moor room and a Cold Plains room is the road. Room1 +0x20 = CollMap (1.13 layout):
   +0 posX, +4 posY, +8 sizeX, +0xC sizeY (subtiles), +0x20 WORD* map; bit 0x1 = blocks walk, 0xFFFF = none.
   Longest walkable run (>=3) over all loaded BM/CP room pairs -> its centre -> GUARD_D subtiles inward, then the
   game's spawn finds the nearest free spot. No gap loaded yet (player elsewhere on the border): retry on the
   next player walk/run; still once per game (game spawned bit). */
#define GUARD_D 10
typedef struct { int x,y,w,h; unsigned short* m; } CMap;
static int cm_get(U8* r,CMap* c){ U8* cm=*(U8**)(r+0x20); if(!cm) return 0;
  c->x=*(int*)cm; c->y=*(int*)(cm+4); c->w=*(int*)(cm+8); c->h=*(int*)(cm+0xC); c->m=*(unsigned short**)(cm+0x20);
  return c->m && c->w>0 && c->w<=512 && c->h>0 && c->h<=512; }
static int cm_in(CMap* c,int x,int y){ return x>=c->x && y>=c->y && x<c->x+c->w && y<c->y+c->h; }
static int cm_walk(CMap* c,int x,int y){ unsigned short v; if(!cm_in(c,x,y)) return 0; v=c->m[(y-c->y)*c->w+(x-c->x)]; return v!=0xFFFF && !(v&1); }
static void boss_spawn(U8* u,U8* g){
  U8* pa; U8* room; U8** nr; U8* bm[64]; U8* cp[64]; CMap bc[64], cc[64]; int n,i,j,nb=0,nc=0,lv;
  int best=0,bx=0,by=0,dx=0,dy=0,d,k; void* m;
  if(g!=(U8*)g_igame){ g_igame=g; g_idone=0; }
  if(g_idone) return;
  if(g[0x1D30+SU_FALLENKING/8] & (1<<(SU_FALLENKING&7))){ g_idone=1; return; }
  pa=*(U8**)(u+0x2C); if(!pa) return; room=*(U8**)(pa+0x1C); if(!room) return;
  lv=ROOM_LVL(room); if(lv!=2 && lv!=3) return;
  nr=*(U8***)room; n=*(int*)(room+0x24); if(!nr||n<1||n>64) return;
  for(i=0;i<n;i++){ U8* r=nr[i]; if(!r) continue; lv=ROOM_LVL(r);
    if(lv==2 && nb<64 && cm_get(r,&bc[nb])) bm[nb++]=r; else if(lv==3 && nc<64 && cm_get(r,&cc[nc])) cp[nc++]=r; }
  if(!nb||!nc) return;
  for(i=0;i<nb;i++) for(j=0;j<nc;j++){ CMap* R=&bc[i]; CMap* C=&cc[j]; int e,a0,a1,ix,ox,ddx,ddy,vert,t,run,rs;
    for(e=0;e<4;e++){
      if(e==0){ if(C->x!=R->x+R->w) continue; vert=1; ix=R->x+R->w-1; ox=C->x; ddx=-1; ddy=0; }
      else if(e==1){ if(C->x+C->w!=R->x) continue; vert=1; ix=R->x; ox=R->x-1; ddx=1; ddy=0; }
      else if(e==2){ if(C->y!=R->y+R->h) continue; vert=0; ix=R->y+R->h-1; ox=C->y; ddx=0; ddy=-1; }
      else { if(C->y+C->h!=R->y) continue; vert=0; ix=R->y; ox=R->y-1; ddx=0; ddy=1; }
      if(vert){ a0=max(R->y,C->y); a1=min(R->y+R->h,C->y+C->h); } else { a0=max(R->x,C->x); a1=min(R->x+R->w,C->x+C->w); }
      run=0; rs=a0;
      for(t=a0;t<=a1;t++){ int ok= t<a1 && (vert ? (cm_walk(R,ix,t)&&cm_walk(C,ox,t)) : (cm_walk(R,t,ix)&&cm_walk(C,t,ox)));
        if(ok){ if(!run) rs=t; run++; }
        else if(run){ if(run>best){ int mid=rs+run/2; best=run; dx=ddx; dy=ddy; if(vert){ bx=ix; by=mid; } else { bx=mid; by=ix; } } run=0; } } } }
  if(best<3) return; /* road gap not loaded yet */
  for(d=GUARD_D; d>=2; d--){ int x=bx+dx*d, y=by+dy*d;
    for(k=0;k<nb;k++) if(cm_in(&bc[k],x,y) && cm_walk(&bc[k],x,y)){
      g_idone=1; m=SPAWN_SU(g,bm[k],x,y,SU_FALLENKING);
      logf_("I Fallen King guard: road gap %d subtiles at (%d,%d) inward (%d,%d), spawn (%d,%d) room %p -> %p%s",best,bx,by,dx,dy,x,y,bm[k],m,
            (g[0x1D30+SU_FALLENKING/8]&(1<<(SU_FALLENKING&7)))?" (spawned bit set)":" (not spawned)");
      return; } }
}static void jk_mode(void* u,U32 mode);
/* FQ (BOSS-mod, cxmxdev.ini FQ=1, default 0): Frostquill, Coldcrow's Pet = superuniques row 67 (monstats frostquill,
   a Quill Rat clone, Cold Enchanted). Hook on the game's superunique spawn 0x5A49B0 (first 9 bytes moved to a trampoline).
   When Coldcrow (superunique 2) spawns in The Cave Level 1 (level 9), Frostquill spawns FQDist subtiles from her
   (8 directions, FQDist down to 3, walkable spot in her room or a loaded Cave L1 neighbour; else the game picks a free
   spot in her room). Blue: states.txt row 188 cx_frostblue (colorshift 108, the chill/freeze blue palette), toggled on
   at spawn. Once per game (the game's own superunique spawned bit). */
#define SU_COLDCROW 2
#define SU_FROSTQUILL 67
#define FQ_STATE 188
static int cfgFQ, fqDist=8, g_fqok, g_fqin; static U8* g_fqtr;
/* CW (2026-10-18, cxmxdev.ini CW=1): Cairn Stone Warden = superuniques row 68 (monstats cairnwarden, Clay Golem art
   2x + grey, token ZX, Lightning Enchanted + Extra Strong). Rakanishu (superunique 3) is the Cairn Stones preset boss in
   Stony Field (level 4); when the game spawns him, the Warden spawns CWDist subtiles away, same placement as FQ. */
#define SU_RAKANISHU 3
#define SU_CAIRNWARDEN 68
#define CW_STATE 189 /* 2026-10-19 states.txt cx_stoneskin: overlays cxstoneskin (front) + cxstoneskin_back (Art finals, orbiting shard ring) */
static int cfgCW, cwDist=12;
static void* su_near(void* g,void* room,void* m,int x,int y,int su,int dist,int lvneed,const char* tag,const char* who){
  U8* pa=*(U8**)((U8*)m+0x2C); int cx,cy,lv,d,k,i,n,fx=0,fy=0; U8** nr; void* f=0; U8* fr=0;
  static const int dx8[8]={1,1,0,-1,-1,-1,0,1}, dy8[8]={0,1,1,1,0,-1,-1,-1};
  lv=ROOM_LVL(room);
  if(lv!=lvneed){ logf_("%s %s spawned in level %d (need %d): skipped",tag,who,lv,lvneed); return 0; }
  if(((U8*)g)[0x1D30+su/8] & (1<<(su&7))){ logf_("%s already spawned this game",tag); return 0; }
  cx= pa? *(unsigned short*)(pa+2) : x; cy= pa? *(unsigned short*)(pa+6) : y;
  nr=*(U8***)room; n=*(int*)((U8*)room+0x24); if(!nr||n<0||n>64) n=0;
  for(d=dist; d>=3 && !fr; d--) for(k=0;k<8 && !fr;k++){ int tx=cx+dx8[k]*d, ty=cy+dy8[k]*d; CMap c;
    if(cm_get((U8*)room,&c) && cm_in(&c,tx,ty)){ if(cm_walk(&c,tx,ty)){ fr=(U8*)room; fx=tx; fy=ty; } continue; }
    for(i=0;i<n;i++){ U8* r=nr[i]; if(r && r!=(U8*)room && cm_get(r,&c) && cm_in(&c,tx,ty)){
        if(cm_walk(&c,tx,ty) && ROOM_LVL(r)==lvneed){ fr=r; fx=tx; fy=ty; } break; } } }
  f= fr ? SPAWN_SU(g,fr,fx,fy,su) : SPAWN_SU(g,room,0,0,su);
  logf_("%s spawn: %s (%d,%d) -> su %d %s (%d,%d) dist %d subtiles, unit %p%s",tag,who,cx,cy,su,fr?"at":"game free spot, no walkable tile in range",fx,fy,
        fr?(int)(sqrt((double)((fx-cx)*(fx-cx)+(fy-cy)*(fy-cy)))+0.5):-1,f,f?"":" (spawn failed)");
  return f;
}
static void* __fastcall fq_hook(void* g,void* room,int x,int y,int id){
  void* m=((SpawnSU_t)(void*)g_fqtr)(g,room,x,y,id); void* f;
  if(g_fqin || !m || !room) return m;
  g_fqin=1;
  __try {
    if(id==SU_COLDCROW && cfgFQ){ f=su_near(g,room,m,x,y,SU_FROSTQUILL,fqDist,9,"FQ","Coldcrow");
      if(f){ ToggleSt(f,FQ_STATE,1); MarkUpd(f); logf_("FQ Frostquill blue state %d on",FQ_STATE); } }
    else if(id==SU_RAKANISHU && cfgCW){ U8* dt=DATATABLES; f=su_near(g,room,m,x,y,SU_CAIRNWARDEN,cwDist,4,"CW","Rakanishu (Cairn Stones)");
      if(f){ if(dt && CW_STATE < *(int*)(dt+0xC4)){ ToggleSt(f,CW_STATE,1); MarkUpd(f); logf_("CW Warden stone skin state %d on",CW_STATE); }
             else logf_("CW stone skin state %d skipped: not in states.txt",CW_STATE); } }
  } __except(1){ logf_("FQ/CW spawn fault"); }
  g_fqin=0; return m;
}
static int g_kremap;
/* cwmode (2026-10-19): Cairn Stone Warden (hcIdx 736, ZX) and Frostquill (735, FQ) have art + animdata only for some modes.
   A server mode with no sequence/art leaves the unit stuck in it and the client draws nothing (Warden invisible but
   alive, Lightning Enchanted still firing). Remap any mode they have no art for: KB/BL -> GH (3), others -> NU (1).
   Monster modes: 0 DT 1 NU 2 WL 3 GH 4 A1 5 A2 6 BL 7 SC 8 S1 9 S2 10 S3 11 S4 12 DD 13 KB 14 SQ 15 RN.
   Also logs every Warden mode change (CW mode ...), first 300 then every 50th. */
#define CW_MODES 0xB13Fu /* 0 1 2 3 4 5 8 12 13 15 */
#define FQ_MODES 0x303Fu /* 0 1 2 3 4 5 12 13 (FQ KB art = GH copy, 2026-10-19) */
static int g_cwml, g_cwrm;
static U32 su_mode(U8* u,U32 mode){ U32 id=*(U32*)(u+4), ok, nm=mode;
  if(id!=736 && id!=735) return mode;
  ok= id==736 ? CW_MODES : FQ_MODES;
  if(mode<16 && !(ok&(1u<<mode))) nm= (mode==13||mode==6) ? 3 : 1; else if(mode>=16) nm=1;
  if(id==736){ g_cwml++; if(g_cwml<=300 || g_cwml%50==0) logf_("CW mode #%d: Warden %u mode %u -> %u%s frame %u",g_cwml,*(U32*)(u+0xC),*(U32*)(u+0x10),mode,nm!=mode?" (no art, remapped)":"",g_sframe); }
  if(nm!=mode){ g_cwrm++; if(g_cwrm<=50) logf_("%s mode %u has no art -> %u [#%d]",id==736?"CW":"FQ",mode,nm,g_cwrm); }
  return nm; }/* jkfix (2026-10-16): Resurrect has anim SQ in skills.txt; our King row has Sk1mode=S1 (no monseq sequence), so the
   server put him in mode 14 with no sequence and he never left it (stood idle until he died). Remap King 14 -> 8 (S1,
   cast event frame 8 in BOSS-mod animdata.d2) so the cast plays and finishes. */
/* chdiag (2026-10-18): assassin + Charge diagnostics, log only (client press in charge_cb, server modes here). */
static int left_skill(U8* p); static int g_chds, g_chdc;
static void jm_dying(U8* u,U32 mode); /* jmdeath, defined after jend */
static U32 __stdcall mode_cb(void* u,U32 mode,U32 ret){
  void* g;
  __try {
    if(!u) return mode;
    if(*(U32*)u==1 && *(U32*)((U8*)u+4)==734 && mode==14){ mode=8; if(++g_kremap<=30||g_kremap%50==0) logf_("JK fix: King SQ (14) -> S1 (8) [#%d]",g_kremap); } if(*(U32*)u==1) mode=su_mode((U8*)u,mode); if(*(U32*)u==4) pot_diag(u,"mode",(int)mode,ret);
    g=*(void**)((U8*)u+0x80); if(!game_ok(g)) return mode;
    g_sgame=g; g_sframe=*(U32*)((U8*)g+0xA8);
    if(*(U32*)u==1) jk_mode(u,mode); /* jking: King mode-change diagnostics (log only) */
    if(*(U32*)u==1 && (mode==0||mode==12)) jm_dying((U8*)u,mode); /* jmdeath: put a controlled merc back to her own alignment before the death handler runs */
    if(cfgC3 && *(U32*)u==4){ /* potfix: every mode */ g_smode=(int)mode; sock_fix(u); g_smode=-1; if(mode==3||mode==5) pot_client_fix(u); } /* 2026-10-15: monster loot arrives as mode 5 (dropping), never 3 here; 4 = cursor (pickup) backstop */
    if(cfgC3 && *(U32*)u==0 && (mode==1||mode==2||mode==3)) pot_sweep(u); /* potfix: once per player per game */
    if(cfgG && *(U32*)u==0 && (mode==2||mode==3)) lvl20_fix(u);
    if(*(U32*)u==0 && *(U32*)((U8*)u+4)==6 && g_chds<60 && left_skill((U8*)u)==107){ g_chds++; logf_("CH assassin server: left skill Charge, mode %u (18 = charging) [#%d]",mode,g_chds); }
    if(g_iok && *(U32*)u==0 && (mode==1||mode==2||mode==3)) boss_spawn((U8*)u,(U8*)g); /* g2: first walk/run, never during load */ /* g-fix: neutral/walk/run only, once per unit */
    /* player death (mode 0 death, 17 dead): the game drops timed lists on death, so clear the meters */
    if(*(U32*)u==0 && (mode==0 || mode==17) && g_nment && LOCALPL && *(U32*)(LOCALPL+0xC)==*(U32*)((U8*)u+0xC)){ EnterCriticalSection(&g_mcs); g_nment=0; LeaveCriticalSection(&g_mcs); } /* potmeter: own player only */
  } __except(1){ }
  return mode;
}
__declspec(naked) static void mode_stub(void){ __asm {
  pushad
  push dword ptr [ebp+4]
  push dword ptr [ebp+0xC]
  push dword ptr [ebp+8]
  call mode_cb
  mov [ebp+0xC],eax
  popad
  mov esi,[ebp+8]
  test esi,esi
  jz retz
  push 0x62469B
  ret
retz:
  push 0x6246F2
  ret
}}
/* F: Charge without Shift. Replaces 0x462DCF F6 C3 04 74 04 (test bl,4; je 0x462DD8). EBP frame of 0x462D00:
   [ebp-0x24] command flags, [ebp-0x20] player unit, ESI = event (0 left down, 1 left up, 2 left held). */
/* H (2026-10-15 chargeH, default 0): nonstop Charge. Disassembly: hovered unit = 0x467A10 -> [ebp-0x1C]; client unit hash
   0x7A5E70 + type*0x200, 128 buckets, chain +0xE4, id +0xC; monsters type 1. [ebp-4]/[ebp+8] = cursor world coords
   (0x45AFF0). Path [unit+0x2C]: word +2 x, +6 y. Player mode +0x10 (18 = sequence, Charge's SQ anim).
   Event (ESI) 0 press -> case 0 unit path when ECX = unit; 2 held -> case 2 ignores hover (walk/repeat), >5 = ignored.
   H: Charge on left, nothing hovered: press/held -> nearest live selectable monster within 20 subtiles of the cursor
   becomes the hovered unit and the event becomes a press, so the game casts Charge on a real target (no point-charge).
   While the player is in mode 18 (charging) held events are dropped (ESI=6) so a walk can't cut the dash; when the
   dash ends the next held event recasts (rate limit 100 ms). No monster in range: event left alone (walk). F (force
   flag on bare ground) still exists but should stay F=0 (point-charge crashed at impact). */
#define CHARGE_AT 0x462DCF
#define SKILL_CHARGE 107
static DWORD g_hlast;
static int left_skill(U8* p){ U8* l; U8* s; U8* t; if(!p) return -1; l=*(U8**)(p+0xA8); if(!l) return -1; s=*(U8**)(l+8); if(!s) return -1; t=*(U8**)s; if(!t) return -1; return *(short*)t; }
static int upos(U8* u,int* x,int* y){ U8* pa=*(U8**)(u+0x2C); if(!pa) return 0; *x=*(unsigned short*)(pa+2); *y=*(unsigned short*)(pa+6); return 1; }
static U8* near_mon(int cx,int cy,int px,int py){
  U8* best=0; int bd=20*20+1,i,n,x,y,dx,dy,dd; U8* u;
  for(i=0;i<128;i++) for(u=*(U8**)(0x7A5E70+0x200*1+i*4),n=0; u && n<256; u=*(U8**)(u+0xE4),n++){
    U32 md=*(U32*)(u+0x10), fl=*(U32*)(u+0xC4);
    if(*(U32*)u!=1 || md==0 || md==12 || !(fl&2) || (fl>>21&1)) continue;
    if(!upos(u,&x,&y)) continue;
    dx=x-px; dy=y-py; if(dx*dx+dy*dy>30*30) continue;          /* near the player too (on screen) */
    dx=x-cx; dy=y-cy; dd=dx*dx+dy*dy; if(dd<bd){ bd=dd; best=u; } }
  return best;
}
/* chargeshift: H targeting dropped. Disassembly: real Shift = global 0x7A0664 -> bit 4 of the MK flags that the
   press (0x44BF40) and held (0x44C060/0x44C2C0) handlers pass to 0x462D00; inside, only "test bl,4" -> cmd flag 0x20
   uses it (the held path 0x462A20 checks 0x20 too). So F = exactly Shift: on press/held, Charge on left, nothing
   hovered -> bl|=4 and the flags arg [ebp+0xC]|=4. Hovered NPC/object/item/monster = normal click. H kept as no-op. */
static void __stdcall charge_cb(U8* ebp,U32* regs){ /* pushad block: [1]=ESI [4]=EBX */
  __try {
    U32 ev=regs[1]; U8* pl=*(U8**)(ebp-0x20); U8* hov=*(U8**)(ebp-0x1C);
    if(pl && *(U32*)(pl+4)==6 && ev<=1 && g_chdc<40 && left_skill(pl)==SKILL_CHARGE){ g_chdc++; logf_("CH assassin client: Charge click ev %u hovered %p (type %d) mode %u [#%d]",ev,hov,hov?(int)*(U32*)hov:-1,*(U32*)(pl+0x10),g_chdc); }
    if(!cfgF || (ev!=0 && ev!=1) || hov /* heldfix: jump table 0x462EC0: 0 press (flag 5), 1 HELD (flag 9 -> 0x462A20), 2 release (0x11) */ || left_skill(pl)!=SKILL_CHARGE) return;
    regs[4]|=4; *(U32*)(ebp+0xC)|=4;
  } __except(1){ }
}__declspec(naked) static void charge_stub(void){ __asm {
  pushad
  push esp
  push ebp
  call charge_cb
  popad
  test bl,4
  jz skip
  or dword ptr [ebp-0x24],0x20
skip:
  push 0x462DD8
  ret
}}
static int g_late; /* 1 once DllMain is done: code pages go back to PAGE_EXECUTE_READ */
static int bytes_eq(U32 va,const U8* b,int n){ __try { return memcmp((void*)va,b,n)==0; } __except(1){ return 0; } }
static int write_mem(U32 va,const void* b,int n){
  DWORD old; if(!VirtualProtect((void*)va,n,PAGE_EXECUTE_READWRITE,&old)) return 0;
  memcpy((void*)va,b,n); VirtualProtect((void*)va,n,(g_late&&(old==PAGE_EXECUTE_READWRITE||old==PAGE_EXECUTE_READ||old==PAGE_EXECUTE_WRITECOPY))?PAGE_EXECUTE_READ:old,&old); /* g-fix: .data (0x7417AC hook A) keeps RW; RX forced only on code */ FlushInstructionCache(GetCurrentProcess(),(void*)va,n); return 1;
}
static int write_jmp(U32 at,void* to){ U8 j[5]; U32 rel=(U32)to-(at+5); j[0]=0xE9; memcpy(j+1,&rel,4); return write_mem(at,j,5); }

/* Log: cxmxdev.log next to the DLL. At load, if it is over 512 KB keep only the last 128 KB. */
#define LOG_MAX  (512*1024)
#define LOG_KEEP (128*1024)
static void open_log(void){
  char p[MAX_PATH]; FILE* f; long sz; char* buf; size_t got; char* st;
  _snprintf(p,MAX_PATH,"%scxmxdev.log",g_dir); p[MAX_PATH-1]=0;
  f=fopen(p,"rb");
  if(f){ fseek(f,0,SEEK_END); sz=ftell(f);
    if(sz>LOG_MAX && (buf=(char*)malloc(LOG_KEEP+1))){
      fseek(f,sz-LOG_KEEP,SEEK_SET); got=fread(buf,1,LOG_KEEP,f); buf[got]=0; fclose(f); f=0;
      st=strchr(buf,'\n'); st=st?st+1:buf;
      f=fopen(p,"wb"); if(f){ fprintf(f,"---- log trimmed at load (was %ld bytes, kept last %u)\n",sz,(unsigned)(got-(st-buf))); fwrite(st,1,got-(st-buf),f); fclose(f); f=0; }
      free(buf);
    }
    if(f) fclose(f); }
  g_log=fopen(p,"a");
}
static int cfg(const char* k){ return GetPrivateProfileIntA("features",k,1,g_ini)!=0; }
/* BaseMod (PlugY DllToLoad) replaces the creator entry 0x558D90 with a jmp at runtime. Log the
   target as module+RVA (the absolute address moves with relocation). [basemod] CreatorRVA in
   cxmxdev.ini is the expected RVA (0 = learn it on this launch and write it). If the target is
   not in BaseMod.dll or its RVA changed, C2 (which relies on the drop path behind it) is skipped. */
static int check_basemod(void){
  U8* e=(U8*)0x558D90; char mod[MAX_PATH]="(none)", *nm; HMODULE hm=0; U32 tgt=0,rva=0,exp; char v[32];
  if(e[0]==0xE9){ tgt=0x558D95+*(int*)(e+1);
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)tgt,&hm)){
      GetModuleFileNameA(hm,mod,MAX_PATH); rva=tgt-(U32)hm; } }
  nm=strrchr(mod,'\\'); nm=nm?nm+1:mod;
  logf_("BaseMod creator 0x558D90: %02X %02X %02X %02X %02X -> %08X (%s +0x%X)",e[0],e[1],e[2],e[3],e[4],tgt,nm,rva);
  if(!rva || _stricmp(nm,"BaseMod.dll")){ logf_("BaseMod creator jump: not found / not BaseMod.dll"); return 0; }
  exp=GetPrivateProfileIntA("basemod","CreatorRVA",0,g_ini);
  if(!exp){ _snprintf(v,32,"0x%X",rva); v[31]=0; WritePrivateProfileStringA("basemod","CreatorRVA",v,g_ini);
    logf_("BaseMod creator RVA learned: 0x%X (saved to cxmxdev.ini)",rva); return 1; }
  if(exp!=rva){ logf_("BaseMod creator RVA CHANGED: expected 0x%X, now 0x%X",exp,rva); return 0; }
  return 1;
}
/* J (BOSS-mod, cxmxdev.ini J=1, default 0): Fallen King mind control. SERVER ONLY (host / single player).
   Disassembly 1.14d: game frame update 0x52D870 (ecx game, game+0xA8 += 1); J ticks from its call 0x52D8DF -> 0x52D7B0.
   Client->server packet table 0x6E0D18 (8 bytes/entry: fn, flag; entry n at +n*8). Handlers are
   __fastcall(ecx game, edx player; stack pkt, size; ret 8), 0 = ok. Wrapped entries: 01-04 walk/run (loc/entity),
   05-0A left skill, 0C-11 right skill (0x620190/0x6201D0 = left/right skill, id = word [[skill]]),
   13 interact (type +1, id +5), 20 use inventory item (id +1), 49 waypoint go.
   Units: game+0x1120 + type*0x200 = unit hash [128] (id & 0x7F, next +0xE4); +0x10 mode; +0x2C path
   (path +2 x, +6 y, +0x1C room; 0x6488C0/0x648900). 0x554EA0 = server reposition (Teleport srvdofunc 27 0x5CA360 uses it:
   __fastcall ecx game, edx unit; stack room(0 = from x/y), x, y, 0, 0; ret 0x14), clients get the reassign.
   0x6229F0(unit,x,y,0x804) != 0 = blocked spot (same check as Teleport).
   Every JCdMin..JCdMax frames (default 375..500 = 15-20 s) the live Fallen King (monstats 734) takes every player on
   his level within JRange subtiles for JDur frames (125 = 5 s). During it: JB blocks relocating skills, TP scroll/tome,
   waypoint/portal objects, level warps and waypoint packets; JP drops move requests that end farther from the King
   (inside 6 subtiles is free) and pulls 2 subtiles toward him every 8 frames; a dropped move/skill snaps the client back
   (reposition to the server spot). JV shows the Terror curse overlay (state 56, own timed list, game expiry). */
#define KF_HCIDX 734
#define J_STATE 56
#define J_LEASH 8
typedef int (__fastcall *Pkt_t)(void*,void*,U8*,U32);
typedef void* (__stdcall *GetSk_t)(void*);
typedef int (__stdcall *Blocked_t)(void*,int,int,U32);
#define GETLSK ((GetSk_t)0x620190)
#define GETRSK ((GetSk_t)0x6201D0)
#define SPOTBLK ((Blocked_t)0x6229F0)
#define REPOS 0x554EA0
#define PKT_TBL 0x6E0D18
#define FRAME_CALL 0x52D8DF
static int cfgJ, cfgJB=1, cfgJP=1, cfgJV=1, cfgJW=1, g_jpulls, jRange=20, jDur=125, jDurBase=125, jDurPer=25, jDurStep=3, jDurMax=250, jEndDist=5, g_jpost, jCdMin=375, jCdMax=500;
static Pkt_t g_jorig[0x50];
typedef struct { U32 id; U32 until; U32 lastp; U32 start; int isfar; int nearn; int type; int tint; U32 lasta; } JCtl;
/* jsleep (2026-10-16): JPM=1 sleepwalk. Server walk (op 0x01 handler) + SCMD 0x0F player-move to the player's OWN client.
   0x5482A0: edx unit; stack client, mode; ret 8 -> 0x53B570 builds 0x0F (type 0, id, movetype=[0x7319EC+mode*12], path target,
   current x/y). Client 0x0F handler 0x45CD40 (table 0x7114F8, 12-byte entries) -> 0x4804E0 (has a local-player branch,
   0x463DD0) + 0x480C10 -> 0x461250 starts the walk on the client unit, so client and server walk the same path.
   JM=1: the King may pick a merc (monstats 271/338/359/560/561) instead of a player; server walk 0x5809D0 toward him. */
static int cfgJPM, cfgJM, g_jsw, g_jmn;
/* jmattack (2026-10-16): JMA=1 - a mind-controlled merc turns on her owner for the duration. Uses the game's own
   SetAlignment 0x5543B0 (__fastcall ecx unit, dl alignment 0 evil/1 neutral/2 good; stack notify=1; ret 4): it keeps
   alignment in state 105 (stat 172) and refreshes hostility (0x554340); Conversion-style code and the hireling create
   (0x5732B3, dl=2) use it. Owner / hireling record untouched. Original read from state 105 stat 172 (0x625D00) and put back
   on every end path (time up, King gone, merc gone/dead/left level, player dead/gone/left level, new game) plus a
   per-tick watchdog. Merc save data does not hold alignment (rebuilt on load). */
static int cfgJMA, g_jman, g_jmrn, g_jmfa, g_jmsl, g_jmao, g_jlc, g_jkmn, g_jrz; static U32 g_jkmel, g_rzid[256], g_rzm[256];
typedef U32 (__stdcall *SLGet_t)(void*,U32,U32);
#define SLGETSTAT ((SLGet_t)0x625D00)
#define SETALIGN 0x5543B0
typedef struct { U32 id; int orig; int on; int tint; U32 owner; int ar0; int aron; int life; } JMA; static JMA g_jma[8];
static int jalign(void* u){ void* sl; int v=-1; __try { sl=GetSLState(u,105); v= sl? (int)SLGETSTAT(sl,0xAC,0) : 4; } __except(1){ v=-1; } return v; }
static int jsetalign(void* u,int a){ int ok=1; __try { __asm {
  push 1
  mov edx,a
  mov ecx,u
  mov eax,SETALIGN
  call eax
} } __except(1){ ok=0; logf_("J merc align fault"); } return ok; }
/* jmforce (2026-10-16): 0x5543B0 refuses alignment 0 when unit flag 0x80000000 (+0xC4, the hireling/pet flag, test at
   0x554406 via 0x451F30) is set - that was 'game refused'. Clear that flag only around the call, put it back at once. */
static int jsetalign_force(void* u,int a){ U32 fl0=0; int ok=0; __try { fl0=*(U32*)((U8*)u+0xC4); *(U32*)((U8*)u+0xC4)=fl0&~0x80000000u;
  ok=jsetalign(u,a); } __except(1){ logf_("J merc force-align fault"); } __try { *(U32*)((U8*)u+0xC4)=(*(U32*)((U8*)u+0xC4)&~0x80000000u)|(fl0&0x80000000u); } __except(1){} return ok; }
/* jmtint: red tint while she is controlled = states.txt row 91 'red' (colorshift 100), blinks the last 1.5 s, off on restore */
#define JM_TINT 91
#define JM_ARBONUS 20000
/* jmhostile (2026-10-16): the hit/hostility test 0x554200 (edx attacker, stack defender; ret 4) climbs both units to their
   owners and returns 0 when they meet - her owner is you, so her arrows/swings were dropped. Hooked at entry: attacker =
   a controlled merc (or her missile, owner type/id at +0x94/+0x98) and defender = a player -> 1 (hostile), so her real
   attack and damage go through the game's normal hit path. Everything else runs the original test. */
static int jm_controlled(U32 id);
static volatile int g_hres; static int g_jmhit;
static int __stdcall hostile_cb(U8* a,U8* d){ U32 id=0;
  __try { if(!cfgJMA||!a||!d||*(U32*)d!=0) return 0;
    if(*(U32*)a==1) id=*(U32*)(a+0xC); else if(*(U32*)a==3 && *(U32*)(a+0x94)==1) id=*(U32*)(a+0x98); else return 0;
    if(!jm_controlled(id)) return 0;
    g_jmhit++; if(g_jmhit<=60||g_jmhit%25==0){ void* sl=*(void**)(d+0x5C); logf_("J merc hit allowed #%d: %s of merc %u -> player %u (player life now %d)",g_jmhit,*(U32*)a==3?"missile":"attack",id,*(U32*)(d+0xC),sl?(sl_get(sl,6u<<16)>>8):-1); }
    return 1; } __except(1){ return 0; } }
__declspec(naked) static void hostile_stub(void){ __asm {
  pushad
  push dword ptr [esp+0x24]
  push edx
  call hostile_cb
  mov g_hres,eax
  popad
  cmp g_hres,0
  jne yes
  push ebp
  mov ebp,esp
  push ecx
  push esi
  push 0x554205
  ret
yes:
  mov eax,1
  ret 4
}}
/* jmhit (2026-10-16): a test log: evil (alignment 0) but her hireling AI still chose the King/monsters. 0x5DDF90 is the
   monster AI's own 'mode on target' helper (169 AI callers): ecx game, edx unit, stack mode, target; ret 8 -> 0x5A7E60 builds
   the mode request (target at +8) -> 0x5A7C20 runs it. Each time she is idle/walking we order attack 1 (mode 4) on the player. */
static int jmattack_cmd(void* g,void* u,void* t);
/* jmforcehit (2026-10-16): to-hit roll 0x57DA?? (ebx attacker, [ebp-0xC] defender) ends 0x57DB72 call 0x5347B0; 0x57DB77
   cmp esi(roll 0-99),edi(chance 5-95); jge miss (0x57DBCF), else hit (0x57DB7B). Hooked at 0x57DB72: attacker a
   controlled merc (or her missile) and defender a player -> roll forced to 0 = always hits. Block is separate. */
static int jm_controlled(U32 id);
static volatile int g_fh; static int g_jfh;
static int g_jfhd; static int jm_any(void);
static int __stdcall forcehit_cb(U8* a,U8* d){ U32 id=0;
  __try { if(!cfgJMA||!a||!d||*(U32*)d!=0) return 0;
    /* jfhdiag: while any merc is controlled, log who rolls to-hit against a player (first 40) to see the arrow path */
    { int any=jm_any();
      if(any && g_jfhd<40){ g_jfhd++; logf_("J to-hit diag #%d: attacker type %u class %u id %u -> player %u (owner fields %u/%u)",g_jfhd,*(U32*)a,*(U32*)(a+4),*(U32*)(a+0xC),*(U32*)(d+0xC),*(U32*)(a+0x94),*(U32*)(a+0x98)); } }
    if(*(U32*)a==1) id=*(U32*)(a+0xC); else if(*(U32*)a==3 && *(U32*)(a+0x94)==1) id=*(U32*)(a+0x98); else return 0;
    if(!jm_controlled(id)) return 0;
    g_jfh++; if(g_jfh<=60||g_jfh%25==0) logf_("J merc forced hit #%d: merc %u -> player %u (to-hit roll forced)",g_jfh,id,*(U32*)(d+0xC));
    return 1; } __except(1){ return 0; } }
/* jmpct (2026-10-16): damage %% 0x57C060 (eax = ctx: +8 attacker, +0xC defender, +0x18 damage struct) is called at 0x57C2AA.
For a player defender hit by a hireling (0x63EE90) it returns 17 = the game's 17%% pet-vs-player cut (why her 028d757 hits were
only -18/-15 and arrows looked like nothing). Hooked at that call: controlled merc (missiles resolve to her) -> player = 100%%. */
static int g_jpct;
static int __stdcall jpct_cb(U8* s,int pct){ U8* a; U8* d; U8* dm; int tot=0;
  __try { if(!cfgJMA||!s) return pct; a=*(U8**)(s+8); d=*(U8**)(s+0xC);
    if(!a||!d||*(U32*)a!=1||*(U32*)d!=0||pct==100||!jm_controlled(*(U32*)(a+0xC))) return pct;
    dm=*(U8**)(s+0x18); if(dm) tot=(*(int*)(dm+8)+*(int*)(dm+0x10)+*(int*)(dm+0x1C)+*(int*)(dm+0x20)+*(int*)(dm+0x24)+*(int*)(dm+0x28))>>8;
    g_jpct++; logf_("J merc damage allowed #%d: merc %u -> player %u, %d damage before resists at 100%% (game wanted %d%%)",g_jpct,*(U32*)(a+0xC),*(U32*)(d+0xC),tot,pct);
    return 100; } __except(1){ return pct; } }
__declspec(naked) static void jpct_stub(void){ __asm {
  push eax
  mov edx,0x57C060
  call edx
  pop ecx
  push eax
  push ecx
  call jpct_cb
  push 0x57C2AF
  ret
}}
__declspec(naked) static void forcehit_stub(void){ __asm {
  mov eax,0x5347B0
  call eax
  pushad
  push dword ptr [ebp-0xC]
  push ebx
  call forcehit_cb
  mov g_fh,eax
  popad
  cmp g_fh,0
  je norm
  xor esi,esi
norm:
  cmp esi,edi
  jge miss
  push 0x57DB7B
  ret
miss:
  push 0x57DBCF
  ret
}}
static int jmattack_cmd(void* g,void* u,void* t){ int ok=1; __try { __asm {
  push t
  push 4
  mov edx,u
  mov ecx,g
  mov eax,0x5DDF90
  call eax
} } __except(1){ ok=0; logf_("J merc attack-order fault"); } return ok; }
static void jmtint(void* u,int on){ __try { ToggleSt(u,JM_TINT,on?1:0); MarkUpd(u); } __except(1){ logf_("J merc tint fault"); } }
static U32 g_jcd[32][2];
static int jcd_ok(U32 id,int t,U32 f){ int i; for(i=0;i<32;i++) if(g_jcd[i][0]==(id^((U32)t<<28))) return f>=g_jcd[i][1]; return 1; }
static void jcd_set(U32 id,int t,U32 f){ int i,j=0; U32 k=id^((U32)t<<28); for(i=0;i<32;i++){ if(g_jcd[i][0]==k){ j=i; break; } if(g_jcd[i][1]<g_jcd[j][1]) j=i; }
  g_jcd[j][0]=k; g_jcd[j][1]=f; }
static U8* junit(U8* g,int type,U32 id);
static int jmerc(U8* u){ U32 c=*(U32*)(u+4); return c==271||c==338||c==359||c==560||c==561; }
static void jsend0f(U8* u){ U32 m=*(U32*)(u+0x10); if(m!=2&&m!=3&&m!=6) return; __try { __asm {
  push 0x10a2
  mov edx,0x6dab88
  mov ecx,u
  mov eax,0x5531C0
  call eax
  test eax,eax
  jz done
  push m
  push eax
  mov edx,u
  mov eax,0x5482A0
  call eax
done:
} } __except(1){ logf_("J sleepwalk 0x0F fault"); } }
static void jmwalk(void* g,void* u,int x,int y){ __try { __asm {
  push 0
  push y
  push x
  push 2
  push 0
  mov edx,u
  mov ecx,g
  mov eax,0x5809D0
  call eax
} } __except(1){ logf_("J merc walk fault"); } }
static U32 g_kwm, g_kwf; static int g_kwlog;
typedef int (__stdcall *SetMode_t)(void*,U32);
#define SETMODE ((SetMode_t)0x624690)
static JCtl g_jc[16]; static void* g_jgame; static U32 g_jnext, g_jkid; static int g_jkx, g_jky, g_jklv, g_jklive, g_jlog;
static U32 g_jendf;
static int g_jkmlog, g_jkslog; static U32 g_jklastsnap;
static void jmrestore(U32 id,const char* why){ int i; U8* u; __try {
  for(i=0;i<8;i++) if(g_jma[i].on && g_jma[i].id==id){ g_jma[i].on=0; u= g_jgame? junit((U8*)g_jgame,1,id):0; g_jmrn++;
    if(u && g_jma[i].aron){ SetUnitStat(u,19,g_jma[i].ar0,0); MarkUpd(u); g_jma[i].aron=0; logf_("J merc AR restore (merc %u): attack rating -> %d",id,g_jma[i].ar0); }
    if(u){ jsetalign_force(u,g_jma[i].orig); jmtint(u,0); logf_("J merc restore #%d (merc %u, %s): alignment -> %d, now %d, red tint off",g_jmrn,id,why,g_jma[i].orig,jalign(u)); }
    else logf_("J merc restore #%d (merc %u, %s): unit gone, nothing to restore",g_jmrn,id,why); } } __except(1){ logf_("J merc restore fault"); } }
static int jm_controlled(U32 id){ int i; for(i=0;i<16;i++) if(g_jc[i].until && g_jc[i].type==1 && g_jc[i].id==id) return 1; return 0; }
static int jm_any(void){ int i; for(i=0;i<16;i++) if(g_jc[i].until && g_jc[i].type==1) return 1; return 0; }
static void jend0(int i,const char* why,U32 f){ const char* tn=g_jc[i].type? "merc":"player"; logf_("J mind control end (%s %u, %s, frame %u)",tn,g_jc[i].id,why,f);
  /* jgreen (2026-10-16): a test log - an early end ('reached King' at 2 s, 'King gone/dead') cleared control and
     tint but the green overlay (state 56, timed for the full duration) stayed, so he walked away while still green.
     Every end path now switches the overlay off too, so overlay, tint and control always end on the same frame. */
  if(g_jgame){ U8* gu=junit((U8*)g_jgame,g_jc[i].type,g_jc[i].id);
    if(gu){ __try { if(GetSLState(gu,J_STATE)){ ToggleSt(gu,J_STATE,0); MarkUpd(gu); logf_("J overlay off (%s %u, %s, frame %u, %d frames early)",tn,g_jc[i].id,why,f,(int)(g_jc[i].until-f)); } }
      __except(1){ logf_("J overlay off fault"); } } }
  if(g_jc[i].type==0 && g_jc[i].tint){ U8* pu= g_jgame? junit((U8*)g_jgame,0,g_jc[i].id):0; if(pu) jmtint(pu,0); g_jc[i].tint=0; logf_("J player tint off (player %u, %s)",g_jc[i].id,why); }
  if(g_jc[i].type==1) jmrestore(g_jc[i].id,why);
  logf_("J end cleanup (%s %u): slot cleared, moves pass untouched from now",tn,g_jc[i].id); if(f) jcd_set(g_jc[i].id,g_jc[i].type,f+jCdMin); g_jc[i].until=0; g_jc[i].lastp=0; g_jc[i].isfar=0; g_jendf=f; g_jpost=0; }
/* jodeath (2026-10-19, CxMx): when the King's control on a merc ends and her owner is dead (player mode 0/17) or gone
   (owner lookup 0x58F0D0 = monData+0x28 owner type/id -> 0x552F60, NULL when he left), she dies too. jend0 first ends
   the slot and restores her own alignment (jmrestore), so she dies as a normal good hireling (no evil-merc region
   count, see jmdeath); then life 0 and the game's own kill routine 0x57CCB0 (ecx game, edx victim, stack killer 0,
   flag 1; ret 8 - same call as 0x5A6FD8/0x5A731F), which sets mode DT through 0x5A7E60/0x5A7C20 and runs the normal
   death events. The kill runs after the slot is cleared, so mode_cb/jm_dying finds nothing to end. Owner alive ->
   unchanged (she returns to normal). Not on the 'merc dying' path or when she is already dead. */
typedef U8* (__fastcall *Owner_t)(void* u,int unused);
#define MONOWNER ((Owner_t)0x58F0D0)
typedef void (__fastcall *KillU_t)(void* g,void* u,void* killer,int flag);
#define KILLUNIT ((KillU_t)0x57CCB0)
static int g_jodn;
static void jend(int i,const char* why,U32 f){ U8* mu; U8* ow=0; U32 id=g_jc[i].id; int ty=g_jc[i].type, die=0, om=-1; void* g=g_jgame;
  if(ty==1 && g && strcmp(why,"merc dying")!=0){ __try { mu=junit((U8*)g,1,id);
      if(mu){ U32 mm=*(U32*)(mu+0x10); if(mm!=0 && mm!=12){ ow=MONOWNER(mu,0); om= ow? (int)*(U32*)(ow+0x10) : -1; die= !ow || om==0 || om==17; } }
    } __except(1){ die=0; logf_("J owner check fault (merc %u)",id); } }
  jend0(i,why,f);
  if(die){ __try { mu=junit((U8*)g,1,id);
      if(mu && *(U32*)(mu+0x10)!=0 && *(U32*)(mu+0x10)!=12){ g_jodn++;
        logf_("J control end, owner dead -> merc dies #%d: merc %u (%s), owner %s (mode %d), alignment %d, frame %u",g_jodn,id,why,ow?"dead":"gone",om,jalign(mu),f);
        SetUnitStat(mu,6,0,0); MarkUpd(mu); KILLUNIT(g,mu,0,1);
        logf_("J owner-dead kill #%d: merc %u now mode %u",g_jodn,id,*(U32*)(mu+0x10)); } }
    __except(1){ logf_("J owner-dead kill fault (merc %u)",id); } } }/* jmdeath (2026-10-19): LAN crash, host Game.exe C0000005 at 0x547E7A (write 0x2D0, EDI 0). Monster mode table
   0x6E2260 (0x5A7990: base + mode*16); mode 0 (DT) start = 0x5A6FF0 -> 0x547E50 (ecx game+0xF0 = monster-region array,
   edx unit). It skips units with monData+0x5C bit 2, else region = array[monData+0x58], and when the unit is evil
   (0x6259B0 = state 105 stat 0xAC alignment, 0 = evil / no state) adds 1 to region+0x2D0 (killed; +0x2CC = spawned,
   read at 0x5902B2). 0x547E90 (from unit removal 0x5CC990) subtracts 1 the same way. A hireling has no region
   (array[0] = NULL) but is never evil, so the game never gets there. JMA forces the controlled merc to alignment 0,
   so when she died (or was removed) while still controlled the game wrote to NULL+0x2D0 and the host crashed; the
   guest then lost connection. Fix 1: mode_cb ends her control slot and restores her alignment when she enters
   DT/DD. Fix 2: both counters are replaced by the same code with a NULL-region check (logged, never written). */
static int g_jmdn, g_rkn;
static void jm_dying(U8* u,U32 mode){ U32 id; int i, hit=0, orig=-1;
  __try { id=*(U32*)(u+0xC);
    for(i=0;i<8;i++) if(g_jma[i].on && g_jma[i].id==id) orig=g_jma[i].orig;
    for(i=0;i<16;i++) if(g_jc[i].until && g_jc[i].type==1 && g_jc[i].id==id){ hit=1; jend(i,"merc dying",g_sframe); }
    for(i=0;i<8;i++) if(g_jma[i].on && g_jma[i].id==id){ hit=1; jmrestore(id,"merc dying"); }
    if(orig>=0 && jalign(u)!=orig){ hit=1; jsetalign_force(u,orig); }
    if(hit){ g_jmdn++; if(g_jmdn<=50) logf_("J merc death #%d: merc %u entering mode %u while controlled, alignment now %d (restored before the death handler)",g_jmdn,id,mode,jalign(u)); }
  } __except(1){ logf_("J merc death fault"); } }
typedef int (__stdcall *UAlign_t)(void*);
#define UALIGN ((UAlign_t)0x6259B0)
static void rk_count(U8** arr,U8* u,int d){ U8* md=0; U8* r; U32 idx=0;
  __try {
    if(u && *(U32*)u==1) md=*(U8**)(u+0x14);
    if(md && (*(unsigned short*)(md+0x5C)&2)) return;
    if(md) idx=*(U32*)(md+0x58);
    r=arr[idx];
    if(UALIGN(u)!=0) return;
    if(!r){ g_rkn++; if(g_rkn<=50) logf_("region count guard #%d: %s unit type %u class %u id %u region %u is NULL, %s skipped (was a crash at 0x547E7A/0x547EBA)",g_rkn,d>0?"kill":"removal",u?*(U32*)u:0,u?*(U32*)(u+4):0,u?*(U32*)(u+0xC):0,idx,d>0?"+1":"-1"); return; }
    *(int*)(r+0x2D0)+=d;
  } __except(1){ g_rkn++; if(g_rkn<=50) logf_("region count guard #%d: fault, skipped",g_rkn); } }
static void __fastcall rk_inc(U8** arr,U8* u){ rk_count(arr,u,1); }
static void __fastcall rk_dec(U8** arr,U8* u){ rk_count(arr,u,-1); }
static void rk_install(void){ static const U8 b[]={0x56,0x8B,0xF2,0x57,0x8B,0xF9,0xBA,0x02,0x00,0x00,0x00}; U32 a[2]={0x547E50,0x547E90}; void* f[2]={(void*)rk_inc,(void*)rk_dec}; int i;
  for(i=0;i<2;i++){ U8 c[5]; U32 r=(U32)f[i]-(a[i]+5); c[0]=0xE9; memcpy(c+1,&r,4);
    if(!bytes_eq(a[i],b,sizeof b)){ logf_("region count guard 0x%06X: SKIPPED (bytes mismatch)",a[i]); continue; }
    logf_("region count guard 0x%06X: %s",a[i],write_mem(a[i],c,5)?"ON (NULL monster region never written)":"SKIPPED (write failed)"); } }static int jx(void* u){ U8* p=*(U8**)((U8*)u+0x2C); return p? *(unsigned short*)(p+2):0; }
static int jy(void* u){ U8* p=*(U8**)((U8*)u+0x2C); return p? *(unsigned short*)(p+6):0; }
static int jlv(void* u){ U8* p=*(U8**)((U8*)u+0x2C); U8* r; if(!p) return -1; r=*(U8**)(p+0x1C); return r? ROOM_LVL(r):-1; }
static int d2(int ax,int ay,int bx,int by){ return (ax-bx)*(ax-bx)+(ay-by)*(ay-by); }
static void jreset(void* g){ if(g!=g_jgame){ memset(g_jc,0,sizeof g_jc); memset(g_jma,0,sizeof g_jma); memset(g_jcd,0,sizeof g_jcd); g_jgame=g; g_jnext=0; g_jklive=0; g_jkid=0; g_jlog=0; g_jkmlog=0; g_jkslog=0; g_jklastsnap=0; g_jendf=0; } }
static JCtl* jfind(void* g,void* pl){ int i; U32 id, f; if(!pl||g!=g_jgame||!g_jklive) return 0; id=*(U32*)((U8*)pl+0xC); f=*(U32*)((U8*)g+0xA8);
  for(i=0;i<16;i++) if(g_jc[i].until && g_jc[i].type==(int)*(U32*)pl && g_jc[i].id==id && f<g_jc[i].until) return &g_jc[i];
  return 0; }
static void jlogb(void* pl,const char* what,int v){ g_jlog++;
  if(g_jlog<=60 || g_jlog%50==0) logf_("J blocked (player %u): %s %d [#%d]",*(U32*)((U8*)pl+0xC),what,v,g_jlog); }
static int repos(void* g,void* u,int x,int y){ int r=0; __try { __asm {
  push 0
  push 0
  push y
  push x
  push 0
  mov edx,u
  mov ecx,g
  mov eax,REPOS
  call eax
  mov r,eax
} } __except(1){ r=0; logf_("J reposition fault"); } return r; }
/* PT server side (host / single player): "/ptp <id>" chat from server_stub. Same level only (room level of both paths),
   both alive, not while the Fallen King holds the player (J), 1 per second per player. Spot: first free ring spot 1..4
   subtiles around the target (0x6229F0 collision mask 0x804, the Teleport skill's own test), then the server
   reposition 0x554EA0 (room 0, x, y, 0, 0 = Teleport srvdofunc 27 0x5CA3C9), which also updates every client. */
static U32 g_ptcd[16][2]; static int g_ptn;
/* PT move fix (2026-10-19, LAN test: "Teleporting to Merlin" but nobody moved; host log "reposition ... returned 0" and
   "no free spot near ..."). Both helpers worked in the CLICKING player's room context, which only reaches that room and
   its neighbours (fine for the Teleport skill, a few subtiles): 0x554EA0 with room 0 resolves x,y via 0x463740 (ecx
   room hint, edx x, stack y; hint room + adjacent rooms 0x619790) seeded with the mover's own room -> no room for a spot
   100+ subtiles away -> returns 0; 0x6229F0(unit,..) tests collision from the unit's room (0x620BB0) the same way.
   Now everything is resolved from the TARGET: free-spot test SPOTBLK(target,..), room = 0x463740(target room, x, y),
   and that room is passed to 0x554EA0 (non-zero room skips its local lookup; 0x64E7B0 still settles the final spot).
   0x554EA0 is distance-independent once it has the room: for a player it sends 0x07 (map add, 0x53BC50) for the new room
   to the mover's own client, re-links visibility old room set -> new room set (0x554670: 0x571F90 remove / 0x571600 add
   per client) and sends the new position to every client (0x5754B0), so the mover's and the other clients resync.
   No ring spot passes -> the target's own spot and room, 0x64E7B0 picks the nearest free one. */
static U8* pt_room(U8* hint,int x,int y){ U8* r=0; if(!hint) return 0; __try { __asm {
  push y
  mov ecx,hint
  mov edx,x
  mov eax,0x463740
  call eax
  mov r,eax
} } __except(1){ r=0; logf_("PT room lookup fault"); } return r; }
static int pt_repos(void* g,void* u,U8* room,int x,int y){ int r=0; __try { __asm {
  push 0
  push 0
  push y
  push x
  push room
  mov edx,u
  mov ecx,g
  mov eax,REPOS
  call eax
  mov r,eax
} } __except(1){ r=0; logf_("PT reposition fault"); } return r; }
/* PTX (2026-10-19, CxMx): cross-area portrait teleport. The game's level change is 0x53AEC0 (ecx game, edx unit; stack
   level, tile calc; ret 8). The waypoint handler 0x584F60 calls it at 0x585060 (after 0x554190 clears the interaction;
   tile calc 13 for towns). Inside: act of the level (0x6427F0, first level per act at 0x6EB2F0); another act ->
   0x537340 + 0x53ACC0 (act change: the client gets the act loading screen); same act -> 0x61B060 spawn spot + 0x554EA0.
   PT therefore: same act, other level -> 0x554EA0 with the TARGET's room (the warp's own same-act step, with the room
   taken from the target instead of a level spawn tile), so he lands next to the target in one move. Other act -> warp
   to that act's town (tile calc 13, like a waypoint to town), then pt_tick (server frame) moves him next to the target
   once he has stood in the new act for 1 s (same-act step), max 30 s. Same level: unchanged (ptp_req below). */
typedef struct { U32 id, tid, until, seen; } PtPend;
static PtPend g_ptp[8]; static void* g_ptpg;
static int pt_act(int lv){ return ((int(__stdcall*)(int))0x6427F0)(lv)&0xFF; }
static int pt_warp(void* g,void* u,int lv,int tc){ int ok=0; __try { ((void(__stdcall*)(void*))0x554190)(u); __asm {
  push tc
  push lv
  mov edx,u
  mov ecx,g
  mov eax,0x53AEC0
  call eax
} ok=1; } __except(1){ ok=0; logf_("PT warp fault"); } return ok; }
static int pt_place(U8* g,U8* pl,U8* t,int* ox,int* oy){
  int r,dx,dy,x=0,y=0,ok=0,tx=jx(t),ty=jy(t); U8* tp=*(U8**)(t+0x2C); U8* troom=tp? *(U8**)(tp+0x1C):0; U8* sroom=0;
  *ox=tx; *oy=ty; if(!troom) return 0;
  for(r=1;r<=4&&!ok;r++) for(dy=-r;dy<=r&&!ok;dy++) for(dx=-r;dx<=r&&!ok;dx++){
    if(dx!=-r&&dx!=r&&dy!=-r&&dy!=r) continue;
    x=tx+dx; y=ty+dy; if(!SPOTBLK(t,x,y,0x804) && (sroom=pt_room(troom,x,y))!=0) ok=1; }
  if(!ok){ x=tx; y=ty; sroom=troom; }
  *ox=x; *oy=y;
  return pt_repos(g,pl,sroom,x,y);
}
static void pt_far(U8* g,U8* pl,U8* t,int lp,int lt,U32 me,U32 id){
  int ap=pt_act(lp), at=pt_act(lt), x, y, rr, i, town; U32 f=*(U32*)(g+0xA8);
  if(ap==at){ rr=pt_place(g,pl,t,&x,&y);
    logf_("PT #%d: player %u level %d -> %u level %d (same act %d): moved next to him at %d,%d %s, now level %d at %d,%d",g_ptn,me,lp,id,lt,at+1,x,y,rr?"ok":"returned 0",jlv(pl),jx(pl),jy(pl)); return; }
  if(at<0||at>4){ logf_("PT #%d: player %u -> %u refused (target act %d unknown)",g_ptn,me,id,at); return; }
  town=*(int*)(0x6EB2F0+at*4);
  if(g!=g_ptpg){ memset(g_ptp,0,sizeof g_ptp); g_ptpg=g; }
  for(i=0;i<8;i++) if(g_ptp[i].id==me+1) break;
  if(i>=8) for(i=0;i<8;i++) if(!g_ptp[i].id || f>g_ptp[i].until) break;
  if(i>=8) i=0;
  g_ptp[i].id=me+1; g_ptp[i].tid=id; g_ptp[i].until=f+750; g_ptp[i].seen=0;
  rr=pt_warp(g,pl,town,13);
  logf_("PT #%d: player %u level %d (act %d) -> %u level %d (act %d): act change, warp 0x53AEC0 to town level %d %s; moves next to him on arrival",g_ptn,me,lp,ap+1,id,lt,at+1,town,rr?"queued":"FAILED");
  if(!rr) g_ptp[i].id=0;
}
static void __stdcall pt_tick(U8* g){ int i,x,y,rr,lp,lt; U8* pl; U8* t; U32 f,m; PtPend* p;
  if(!cfgPT || g!=g_ptpg) return;
  __try { if(!game_ok(g)) return; f=*(U32*)(g+0xA8);
    for(i=0;i<8;i++){ p=&g_ptp[i]; if(!p->id) continue;
      if(f>p->until){ logf_("PT arrival: player %u not settled in the new act within 30 s, gave up",p->id-1); p->id=0; continue; }
      pl=junit(g,0,p->id-1); if(!pl) continue;                 /* in the act change */
      m=*(U32*)(pl+0x10); if(m==0||m==17){ logf_("PT arrival: player %u dead, cancelled",p->id-1); p->id=0; continue; }
      lp=jlv(pl); if(lp<0){ p->seen=0; continue; }
      t=junit(g,0,p->tid); if(!t){ logf_("PT arrival: target %u left the game, player %u stays in town",p->tid,p->id-1); p->id=0; continue; }
      m=*(U32*)(t+0x10); lt=jlv(t); if(lt<0 || m==0 || m==17) continue;
      if(pt_act(lp)!=pt_act(lt)){ p->seen=0; continue; }      /* not arrived yet (or the target changed act) */
      if(!p->seen){ p->seen=f?f:1; continue; }
      if(f-p->seen<25) continue;                                 /* stood 1 s in the new act */
      p->id=0;
      if(lp==lt && d2(jx(pl),jy(pl),jx(t),jy(t))<=25){ logf_("PT arrival: player %u already next to %u",*(U32*)(pl+0xC),p->tid); continue; }
      rr=pt_place(g,pl,t,&x,&y);
      logf_("PT arrival: player %u (level %d) -> next to %u (level %d) at %d,%d %s, now level %d at %d,%d",*(U32*)(pl+0xC),lp,p->tid,lt,x,y,rr?"ok":"returned 0",jlv(pl),jx(pl),jy(pl));
    }
  } __except(1){ logf_("PT tick fault"); }
}
static void __stdcall ptp_req(U8* g,U8* pl,U8* pkt){
  __try {
    U32 id=0,f,me,m; int i,j=-1,lp,lt,tx,ty,x=0,y=0,ok=0,r,dx,dy,rr; U8* t; U8* tp; U8* troom; U8* sroom=0; const char* s=(const char*)pkt+8;
    if(!cfgPT||!g||!pl||*(U32*)pl!=0) return;
    for(i=0;i<10&&s[i]>='0'&&s[i]<='9';i++) id=id*10+(U32)(s[i]-'0');
    f=*(U32*)(g+0xA8); me=*(U32*)(pl+0xC); g_ptn++;
    for(i=0;i<16;i++){ if(g_ptcd[i][0]==me+1){ j=i; break; } if(!g_ptcd[i][0]&&j<0) j=i; }
    if(j<0) j=0;
    if(g_ptcd[j][0]==me+1 && g_ptcd[j][1]<=f && f-g_ptcd[j][1]<25){ logf_("PT #%d: player %u -> %u refused (1/s limit)",g_ptn,me,id); return; }
    t=junit(g,0,id);
    if(!t||t==pl){ logf_("PT #%d: player %u -> %u refused (target not in this game)",g_ptn,me,id); return; }
    m=*(U32*)(pl+0x10); if(m==0||m==17){ logf_("PT #%d: player %u refused (dead)",g_ptn,me); return; }
    m=*(U32*)(t+0x10); if(m==0||m==17){ logf_("PT #%d: player %u -> %u refused (target dead)",g_ptn,me,id); return; }
    if(g_jon && cfgJB && jfind(g,pl)){ logf_("PT #%d: player %u refused (Fallen King hold)",g_ptn,me); return; }
    lp=jlv(pl); lt=jlv(t);
    if(lp<0||lt<0){ logf_("PT #%d: player %u (level %d) -> %u (level %d) refused (no level)",g_ptn,me,lp,id,lt); return; }
    if(lp!=lt){ g_ptcd[j][0]=me+1; g_ptcd[j][1]=f; pt_far(g,pl,t,lp,lt,me,id); return; } /* PTX: other area */
    tx=jx(t); ty=jy(t); tp=*(U8**)(t+0x2C); troom=tp? *(U8**)(tp+0x1C):0;
    if(!troom){ logf_("PT #%d: player %u -> %u refused (target has no room)",g_ptn,me,id); return; }
    for(r=1;r<=4&&!ok;r++) for(dy=-r;dy<=r&&!ok;dy++) for(dx=-r;dx<=r&&!ok;dx++){
      if(dx!=-r&&dx!=r&&dy!=-r&&dy!=r) continue;
      x=tx+dx; y=ty+dy; if(!SPOTBLK(t,x,y,0x804) && (sroom=pt_room(troom,x,y))!=0) ok=1; }
    if(!ok){ x=tx; y=ty; sroom=troom; ok=1; logf_("PT #%d: no ring spot passed the check near %d,%d - using the target spot, 0x64E7B0 settles a free one",g_ptn,tx,ty); }
    g_ptcd[j][0]=me+1; g_ptcd[j][1]=f;
    rr=pt_repos(g,pl,sroom,x,y);
    logf_("PT #%d: player %u -> %u level %d: reposition to %d,%d room %p (target %d,%d room %p) %s, now %d,%d",g_ptn,me,id,lp,x,y,sroom,tx,ty,troom,rr?"ok":"returned 0",jx(pl),jy(pl));
  } __except(1){ logf_("PT fault"); }
}
static void pt_install(void){
  static const U8 cel[5]={0xE8,0xF1,0x27,0x06,0x00};
  static const U32 other[6]={0x493DA6,0x493DD1,0x493DF3,0x493E8D,0x493F6B,0x493FFD};
  int i, n=0; U8 c[5]; U32 rel;
  cfgPT=cfg("PT");
  if(bytes_eq(0x493C8A,cel,5)){ rel=(U32)pc_stub-(0x493C8A+5); c[0]=0xE8; memcpy(c+1,&rel,4); if(write_mem(0x493C8A,c,5)) n|=1; }
  for(i=0;i<6;i++){ U8 b[5]; U32 r0=0x4F6480-(other[i]+5); b[0]=0xE8; memcpy(b+1,&r0,4);
    if(bytes_eq(other[i],b,5)){ rel=(U32)pa_stub-(other[i]+5); c[0]=0xE8; memcpy(c+1,&rel,4); if(write_mem(other[i],c,5)) n+=2; } }
  logf_("portrait cel hooks: player %s, other %d/6",(n&1)?"ON":"SKIPPED (0x493C8A mismatch)",n>>1);
  if(!cfgPT){ logf_("PT portrait teleport: OFF (cxmxdev.ini)"); return; }
  if(!(n&1)){ logf_("PT portrait teleport: SKIPPED (no portrait hook)"); return; }
  { static const U8 rb[14]={0x55,0x8B,0xEC,0x51,0xC7,0x05,0xBC,0xF2,0x70,0x00,0x04,0x00,0x00,0x00}; static const U8 n9[9]={0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
    if(bytes_eq(0x44C180,rb,14) && write_jmp(0x44C180,(void*)rbd_stub)){ write_mem(0x44C185,n9,9); g_ptok=1; } }
  logf_("PT portrait teleport: %s",g_ptok?"ON (right button down 0x44C180; right-click a party portrait; any area: same act one move, other act via town)":"SKIPPED (0x44C180 bytes mismatch)");
}
static void jsnap(void* g,void* pl){ (void)g; (void)pl; } /* jleash: no snap-back (it caused the black flash); the 1/s pull corrects drift */
static void jstate(void* game,void* pl,int frames){ U32 f,ex; void* sl; U8* dt=DATATABLES;
  if(!cfgJV||!dt||J_STATE>=*(int*)(dt+0xC4)) return;
  __try { f=*(U32*)((U8*)game+0xA8); ex=f+frames;
    sl=AllocSL(*(void**)((U8*)game+0x1C),2,ex,*(U32*)pl,*(U32*)((U8*)pl+0xC)); if(!sl) return;
    SLSetState(sl,J_STATE); ToggleSt(pl,J_STATE,1); SetRemCb(sl,STATE_REMOVE_CB); MergeSL(pl,sl,1); SetExpire(sl,ex);
    AddEvt(game,pl,12,ex,0,0); MarkUpd(pl); } __except(1){ logf_("J state fault"); } }
/* jking (2026-10-16): read-only diagnostics for "King idle during mind control". Logs every King mode change
   (capped 300) with whether a mind control is active, and a 1/s King snapshot (mode, spot, player distance)
   while one is active and for 10 s after (capped 200). Changes nothing in the game. */
static int jactive(void){ int i; for(i=0;i<16;i++) if(g_jc[i].until) return 1; return 0; }
static void jk_mode(void* u,U32 mode){ U32 f;
  if(!cfgJ || *(U32*)((U8*)u+4)!=KF_HCIDX || g_jkmlog>=300) return; f=g_sframe; g_jkmlog++;
  logf_("JK mode #%d: King %u mode %u -> %u at %d,%d frame %u (J %s)",g_jkmlog,*(U32*)((U8*)u+0xC),*(U32*)((U8*)u+0x10),mode,jx(u),jy(u),f,jactive()?"ACTIVE":"off"); }
static void jk_snap(U8* k,U32 f){ int i;
  if(g_jkslog>=200 || f-g_jklastsnap<25) return;
  if(!jactive() && !(g_jendf && f-g_jendf<=250)) return;
  g_jklastsnap=f; g_jkslog++;
  for(i=0;i<16;i++) if(g_jc[i].until) break;
  logf_("JK snap #%d frame %u: King mode %u at %d,%d; J %s%s",g_jkslog,f,*(U32*)(k+0x10),g_jkx,g_jky,jactive()?"ACTIVE":"ended ",
    jactive()?"":"(<=10 s ago)"); }static int call_orig(Pkt_t fn,void* g,void* pl,U8* pkt,U32 sz);
/* game frame tick (server thread, same as the packet handlers) */
static void __stdcall j_tick(U8* g){
  U32 f; int b,i,n,go=0,started=0; U8* u; U8* k=0; int r2=jRange*jRange;
  __try {
    if(!g_jon || !game_ok(g)) return; jreset(g); f=*(U32*)(g+0xA8);
    for(b=0;b<128 && !k;b++) for(u=*(U8**)(g+0x1320+b*4),n=0; u && n<64; u=*(U8**)(u+0xE4),n++)
      if(*(U32*)(u+4)==KF_HCIDX && *(U32*)u==1){ U32 m=*(U32*)(u+0x10); if(m!=0 && m!=12){ k=u; break; } }
    if(!k){ if(g_jklive){ for(i=0;i<16;i++) if(g_jc[i].until) jend(i,"King gone/dead",f); }
      g_jklive=0; return; }
    g_jklive=1; g_jkid=*(U32*)(k+0xC); g_jkx=jx(k); g_jky=jy(k); g_jklv=jlv(k); jk_snap(k,f);
    /* jkmelee (2026-10-16): a live player within 3 subtiles and the King idle/walking -> staff swing (attack 1) on him,
       at most every 20 frames, through the AI's own mode-on-target helper 0x5DDF90 */
    { U32 km=*(U32*)(k+0x10); if((km==1||km==2) && f-g_jkmel>=20){ U8* q; int bb,nn,bd=1<<30; U8* bp=0;
        for(bb=0;bb<128;bb++) for(q=*(U8**)(g+0x1120+bb*4),nn=0; q && nn<64; q=*(U8**)(q+0xE4),nn++){ U32 qm=*(U32*)(q+0x10); int qd;
          if(qm==0||qm==17||jlv(q)!=g_jklv) continue; qd=d2(jx(q),jy(q),g_jkx,g_jky); if(qd<bd){ bd=qd; bp=q; } }
        if(bp && bd<=9){ g_jkmel=f; if(jmattack_cmd(g,k,bp)){ g_jkmn++; if(g_jkmn<=60||g_jkmn%25==0) logf_("JK melee #%d: King staff swing on player %u, dist %d, mode %u -> %u",g_jkmn,*(U32*)(bp+0xC),(int)sqrt((double)bd),km,*(U32*)(k+0x10)); } } } }
    /* jkrez (2026-10-16): log every Fallen (19-23) / Fallen Shaman (58-62) on his level that goes dead (12) -> alive */
    { U8* q; int bb,nn; for(bb=0;bb<128;bb++) for(q=*(U8**)(g+0x1320+bb*4),nn=0; q && nn<64; q=*(U8**)(q+0xE4),nn++){ U32 hc=*(U32*)(q+4), id, qm; int z;
        if(!((hc>=19&&hc<=23)||(hc>=58&&hc<=62))) continue; id=*(U32*)(q+0xC); qm=*(U32*)(q+0x10); z=(int)(id&255);
        if(g_rzid[z]==id && g_rzm[z]==12 && qm!=12 && qm!=0){ g_jrz++; if(g_jrz<=100) logf_("J resurrect #%d: %s %u (class %u) rose at frame %u, King mode %u, dist to King %d",g_jrz,hc>=58?"Fallen Shaman":"Fallen",id,hc,f,*(U32*)(k+0x10),(int)sqrt((double)d2(jx(q),jy(q),g_jkx,g_jky))); }
        g_rzid[z]=id; g_rzm[z]=qm; } }
    /* jkfix watchdog: a cast/sequence mode (8-11, 14) that has not changed for 3 s is stuck -> back to neutral so the AI runs */
    { U32 km=*(U32*)(k+0x10); if(km!=g_kwm){ g_kwm=km; g_kwf=f; }
      else if((km==14||(km>=8&&km<=11)) && f-g_kwf>=75){ g_kwlog++; if(g_kwlog<=30) logf_("JK watchdog: King stuck in mode %u for %u frames -> neutral [#%d]",km,f-g_kwf,g_kwlog);
        __try { SETMODE(k,1); } __except(1){ logf_("JK watchdog fault"); } g_kwm=1; g_kwf=f; } }
    for(i=0;i<16;i++) if(g_jc[i].until && f>=g_jc[i].until) jend(i,"time up",f);
    if(!g_jnext) g_jnext=f+jCdMin;
    go=(f>=g_jnext); started=0;
    /* end slots whose unit is gone or dead (player or merc) */
    for(i=0;i<16;i++) if(g_jc[i].until){ U8* t=junit(g,g_jc[i].type,g_jc[i].id); U32 tm= t? *(U32*)(t+0x10):0;
      if(!t || (g_jc[i].type==0 ? (tm==0||tm==17) : (tm==0||tm==12))) jend(i,g_jc[i].type? "merc gone/dead":"player gone/dead",f);
      else if(g_jc[i].type==1 && jlv(t)!=g_jklv) jend(i,"merc left area",f); }
    /* jmattack: merc slots also end when no live player is left on his level (player dead, left area or game) */
    for(i=0;i<16;i++) if(g_jc[i].until && g_jc[i].type==1){ int np=0;
      for(b=0;b<128 && !np;b++) for(u=*(U8**)(g+0x1120+b*4),n=0; u && n<64; u=*(U8**)(u+0xE4),n++){ U32 pm=*(U32*)(u+0x10); if(pm!=0 && pm!=17 && jlv(u)==g_jklv){ np=1; break; } }
      if(!np) jend(i,"player dead/gone/left area",f); }
    /* jmattack watchdog: any altered merc without a live slot is put back */
    for(i=0;i<8;i++) if(g_jma[i].on){ int j, live=0; for(j=0;j<16;j++) if(g_jc[j].until && g_jc[j].type==1 && g_jc[j].id==g_jma[i].id) live=1;
      if(!live) jmrestore(g_jma[i].id,"watchdog"); }
    /* jptint: the controlled player is tinted red (state 91) too, blinking the last 1.5 s */
    for(i=0;i<16;i++) if(g_jc[i].until && g_jc[i].type==0){ U8* pu=junit(g,0,g_jc[i].id); U32 rem=g_jc[i].until-f; int want;
      if(!pu) continue; want= rem>37 ? 1 : (int)(((rem/6)&1)==0);
      if(want!=g_jc[i].tint){ if(!g_jc[i].tint && rem>37) logf_("J player tint on (player %u, %u frames)",g_jc[i].id,rem); jmtint(pu,want); g_jc[i].tint=want; } }
    /* jmforce tick: per controlled merc - re-assert evil if the game flipped her back, blink the tint in the last 1.5 s,
       and when she is idle/walking walk her at the nearest live player on the level so her own AI engages him */
    if(cfgJMA) for(i=0;i<16;i++) if(g_jc[i].until && g_jc[i].type==1){ int e; U8* mu=junit(g,1,g_jc[i].id); U32 rem=g_jc[i].until-f, mm; U8* pp=0; int bd=1<<30;
      if(!mu) continue; for(e=0;e<8;e++) if(g_jma[e].on && g_jma[e].id==g_jc[i].id) break; if(e==8) continue;
      mm=*(U32*)(mu+0x10);
      { int want= rem>37 ? 1 : (int)(((rem/6)&1)==0); if(want!=g_jma[e].tint){ jmtint(mu,want); g_jma[e].tint=want; } }
      if((f-g_jc[i].start)%25==0 && jalign(mu)!=0){ jsetalign_force(mu,0); g_jmfa++; if(g_jmfa<=20) logf_("J merc force #%d: merc %u alignment was %d, set evil again",g_jmfa,g_jc[i].id,jalign(mu)); }
      for(b=0;b<128;b++) for(u=*(U8**)(g+0x1120+b*4),n=0; u && n<64; u=*(U8**)(u+0xE4),n++){ U32 pm=*(U32*)(u+0x10); int dd;
        if(pm==0||pm==17||jlv(u)!=jlv(mu)) continue; dd=d2(jx(u),jy(u),jx(mu),jy(mu)); if(dd<bd){ bd=dd; pp=u; } }
      if(pp && (mm==1||mm==2||mm==15) && (f-g_jc[i].lasta)>=6 && (*(U32*)(mu+4)==271 || *(U32*)(mu+4)==359 || bd<=9)){ g_jc[i].lasta=f; g_jc[i].lastp=f;
        if(jmattack_cmd(g,mu,pp)){ g_jmao++; if(g_jmao<=40||g_jmao%25==0) logf_("J merc attack order #%d: merc %u (mode %u) -> attack 1 on player %u, dist %d, now mode %u",g_jmao,g_jc[i].id,mm,*(U32*)(pp+0xC),(int)sqrt((double)bd),*(U32*)(mu+0x10)); } }
      else if(pp && (f-g_jc[i].lastp)>=12 && (mm==1||mm==2||mm==15) && bd>4){ g_jc[i].lastp=f; jmwalk(g,mu,jx(pp),jy(pp)); }
      if(pp){ void* psl=*(void**)(pp+0x5C); int lf= psl? (sl_get(psl,6u<<16)>>8):-1;
        if(g_jma[e].life>=0 && lf!=g_jma[e].life){ g_jlc++; if(g_jlc<=300) logf_("J life #%d: player %u %d -> %d (%+d) frame %u, merc %u controlled",g_jlc,*(U32*)(pp+0xC),g_jma[e].life,lf,lf-g_jma[e].life,f,g_jc[i].id); }
        g_jma[e].life=lf; }
      if((f-g_jc[i].start)%25==0){ g_jmsl++; if(g_jmsl<=80) logf_("J merc 1/s #%d: merc %u mode %u (4/5=attack, 7-11=cast) alignment %d, player %u dist %d, %u frames left, tint %s",
        g_jmsl,g_jc[i].id,mm,jalign(mu),pp?*(U32*)(pp+0xC):0,pp?(int)sqrt((double)bd):-1,rem,g_jma[e].tint?"on":"off"); } }
    /* pick ONE target per cast: player or (JM) merc, each with its own cooldown */
    { U8* pick=0; int npick=0, ty;
    if(go) for(ty=0;ty<=(cfgJM?1:0);ty++) for(b=0;b<128;b++) for(u=*(U8**)(g+0x1120+ty*0x200+b*4),n=0; u && n<64; u=*(U8**)(u+0xE4),n++){
      U32 m=*(U32*)(u+0x10);
      if(ty==0 ? (m==0||m==17) : (!jmerc(u)||m==0||m==12)) continue;
      if(jlv(u)!=g_jklv || jfind(g,u) || d2(jx(u),jy(u),g_jkx,g_jky)>r2 || !jcd_ok(*(U32*)(u+0xC),ty,f)) continue;
      { int w=1; if(ty==1){ int pd=1<<30, md=d2(jx(u),jy(u),g_jkx,g_jky), bb, nn; U8* q;
          for(bb=0;bb<128;bb++) for(q=*(U8**)(g+0x1120+bb*4),nn=0; q && nn<64; q=*(U8**)(q+0xE4),nn++){ U32 qm=*(U32*)(q+0x10); int qd;
            if(qm==0||qm==17||jlv(q)!=g_jklv) continue; qd=d2(jx(q),jy(q),g_jkx,g_jky); if(qd<pd) pd=qd; }
          if(md<pd) w=3; }
        npick+=w; if(rand()%npick<w) pick=u; } }
    for(ty=0;ty<=(cfgJM?1:0);ty++) for(b=0;b<128;b++) for(u=*(U8**)(g+0x1120+ty*0x200+b*4),n=0; u && n<64; u=*(U8**)(u+0xE4),n++){
      U32 m=*(U32*)(u+0x10); int px,py; JCtl* c;
      if(ty==1 && !jmerc(u)) continue;
      if(ty==0 ? (m==0||m==17) : (m==0||m==12)) continue;
      if(jlv(u)!=g_jklv) continue;
      px=jx(u); py=jy(u); c=jfind(g,u);
      if(!c && u==pick){
        for(i=0;i<16 && g_jc[i].until;i++); if(i==16) continue;
        { int sd=(int)sqrt((double)d2(px,py,g_jkx,g_jky)), dur=jDurBase;
        if(sd>5) dur+=((sd-5+jDurStep-1)/jDurStep)*jDurPer; if(dur>jDurMax) dur=jDurMax;
        g_jc[i].id=*(U32*)(u+0xC); g_jc[i].type=ty; g_jc[i].until=f+dur; g_jc[i].lastp=0; g_jc[i].start=f; g_jc[i].isfar=(jEndDist>0 && sd>jEndDist); g_jc[i].nearn=0; g_jc[i].tint=0; g_jc[i].lasta=0; jstate(g,u,dur);
        if(ty==1){ g_jmn++; if(g_jmn<=60) logf_("J merc #%d: mind control start (merc %u class %u)",g_jmn,*(U32*)(u+0xC),*(U32*)(u+4));
          if(cfgJMA){ int e, a0=jalign(u), a1; for(e=0;e<8 && g_jma[e].on;e++); g_jc[i].isfar=0;
            if(e<8 && a0>=0){ g_jma[e].id=g_jc[i].id; g_jma[e].orig=(a0>=0&&a0<=2)? a0:2; g_jma[e].on=1; g_jma[e].owner=0; jsetalign_force(u,0); a1=jalign(u); g_jman++;
              { void* bsl=*(void**)(u+0x5C); g_jma[e].ar0= bsl? sl_get(bsl,19u<<16):0; g_jma[e].life=-1;
                __try { SetUnitStat(u,19,g_jma[e].ar0+JM_ARBONUS,0); MarkUpd(u); g_jma[e].aron=1; } __except(1){ logf_("J merc AR fault"); }
                logf_("J merc AR: merc %u attack rating %d -> %d while controlled (to-hit capped at 95%%)",g_jc[i].id,g_jma[e].ar0,g_jma[e].ar0+JM_ARBONUS); }
              jmtint(u,1); g_jma[e].tint=1;
              logf_("J merc attack #%d: merc %u turns on her owner, alignment %d -> %d%s, red tint on",g_jman,g_jc[i].id,a0,a1,a1==0?" (forced, hireling flag held off for the call)":" (still refused)"); }
            else logf_("J merc attack: skipped (merc %u, alignment read %d, free entry %d)",g_jc[i].id,a0,e<8); } }
        logf_("J mind control start (%s %u at %d,%d, King %u at %d,%d, dist %d, duration %d frames = %d.%d s, until frame %u)",ty?"merc":"player",g_jc[i].id,px,py,g_jkid,g_jkx,g_jky,sd,dur,dur/25,(dur%25)*4/10,g_jc[i].until); }
        c=&g_jc[i]; started=1; }
      /* jsleep: JPM player sleepwalk / JM merc walk, every 15 frames while > 5 subtiles from him, never during attacks/casts */
      if(c && f-c->lastp>=15 && (ty==1 ? (cfgJM && !cfgJMA) : (cfgJPM && g_jorig[1])) && (ty==1 || m==1||m==2||m==3||m==6)){ int dd=d2(px,py,g_jkx,g_jky);
        if(dd>9){ double L=sqrt((double)dd); int nx=g_jkx+(int)((px-g_jkx)*3.0/L), ny=g_jky+(int)((py-g_jky)*3.0/L); c->lastp=f;
          if(ty==1){ jmwalk(g,u,nx,ny); g_jmn++; if(g_jmn<=60||g_jmn%25==0) logf_("J merc #%d: walk %u %d,%d dist %d -> %d,%d",g_jmn,c->id,px,py,(int)L,nx,ny); }
          else { U8 wp[5]; wp[0]=1; *(unsigned short*)(wp+1)=(unsigned short)nx; *(unsigned short*)(wp+3)=(unsigned short)ny; g_jsw++;
            __try { call_orig(g_jorig[1],g,u,wp,5); } __except(1){ logf_("J sleepwalk walk fault"); }
            jsend0f(u);
            if(g_jsw<=60||g_jsw%25==0) logf_("J sleepwalk #%d (player %u): %d,%d dist %d -> %d,%d mode %u",g_jsw,c->id,px,py,(int)L,nx,ny,*(U32*)(u+0x10)); } } }
      /* jendfix: the old check ended on the first frame (>=1 s) the player was inside JEndDist, so a Charge/run already in
         flight at the cast ended it in 1 s. Now: >=2 s in, and inside JEndDist on 12 frames in a row (0.5 s). */
      if(c && c->isfar){ int dd=d2(px,py,g_jkx,g_jky); if(dd<=jEndDist*jEndDist) c->nearn++; else c->nearn=0;
        if(f-c->start>=50 && c->nearn>=12){ logf_("J reached King: player %d,%d King %d,%d dist %d after %u frames",px,py,g_jkx,g_jky,(int)sqrt((double)dd),f-c->start); jend((int)(c-g_jc),"reached King",f); c=0; } }
      /* jnopull: no reposition at all (it caused the black flash). Outside J_LEASH subtiles, at most once per 25 frames,
         issue a normal server WALK (game's own op 0x01 handler, same path JW uses) to 5 subtiles from him */
      if(c && ty==0 && !cfgJPM && cfgJP && m>=1 && m<=6 && f-c->lastp>=25 && g_jorig[1]){ int dd=d2(px,py,g_jkx,g_jky);
        if(dd>J_LEASH*J_LEASH){ double L=sqrt((double)dd); U8 wp[5]; int nx=g_jkx+(int)((px-g_jkx)*5.0/L), ny=g_jky+(int)((py-g_jky)*5.0/L);
          wp[0]=1; *(unsigned short*)(wp+1)=(unsigned short)nx; *(unsigned short*)(wp+3)=(unsigned short)ny; c->lastp=f; g_jpulls++;
          __try { call_orig(g_jorig[1],g,u,wp,5); } __except(1){ logf_("J walk-pull fault"); }
          if(g_jpulls<=40||g_jpulls%25==0) logf_("J walk-pull #%d (player %u): %d,%d dist %d -> walk %d,%d (5 from King)",g_jpulls,c->id,px,py,(int)L,nx,ny); } } } }
    if(started) g_jnext=f+jCdMin+(U32)(rand()%(jCdMax-jCdMin+1));
  } __except(1){ logf_("J tick fault"); }
}
/* ---- GC (2026-10-19): gold auto-pickup during Charge and Teleport (server: host / single player; LAN guests included).
   The existing auto-pickup is BaseMod.dll [AutoPickup] Gold=1 (fn 0x10016780: room unit list, mode 3 gold within 10
   subtiles = 2 tiles, squared 100 via the gold-radius cave; only while walking/running). It picks up with the game's
   own gold pickup 0x55C850 (esi = player, stack game, item; ret 8; adds stat 14 capped at clvl*10000 = 0x622E70,
   removes and frees the ground item) after checking gold < max. GC uses that same routine from the server frame hook:
   active while the player is in mode 18 with Charge (107) on either skill (+5 frames), and for 10 frames after a
   same-level jump of >= GCJump subtiles in one frame (Teleport skill, charges or charm; also other repositions).
   Ground gold = server item hash g+0x1920, type 4, mode 3, item type 4 (0x62B400), static path x +0xC / y +0x10, same
   level, within GCRad subtiles of the player. No pickup sound (BaseMod's own sound path is internal to BaseMod). */
static int cfgGC, gcRad=10, gcJump=6; static int g_gcn, g_gcg;
typedef struct { U32 id; int x,y,lv; U32 until, lastf; int why; } GCs; static GCs g_gcs[16]; static void* g_gcgame;
static int gc_pick(U8* g,U8* pl,U8* it){ int ok=1; __try { __asm {
  push esi
  push it
  push g
  mov esi,pl
  mov eax,0x55C850
  call eax
  pop esi
} } __except(1){ ok=0; logf_("GC pickup fault"); } return ok; }
static int gc_skill(U8* pl){ void* s; int a=-1,b=-1;
  __try { s=GETLSK(pl); if(s && *(U8**)s) a=**(short**)s; s=GETRSK(pl); if(s && *(U8**)s) b=**(short**)s; } __except(1){ }
  return a==SKILL_CHARGE || b==SKILL_CHARGE; }
static void __stdcall gc_tick(U8* g){
  int b,n,i,px,py,lv,cnt,k; U8* u; U8* it; U32 f,m; GCs* s; U8* cand[32];
  if(!cfgGC) return;
  __try { if(!game_ok(g)) return; f=*(U32*)(g+0xA8);
    if(g!=g_gcgame){ memset(g_gcs,0,sizeof g_gcs); g_gcgame=g; }
    for(b=0;b<128;b++) for(u=*(U8**)(g+0x1120+b*4),n=0; u && n<64; u=*(U8**)(u+0xE4),n++){
      if(*(U32*)u!=0) continue; m=*(U32*)(u+0x10); if(m==0||m==17) continue;
      px=jx(u); py=jy(u); lv=jlv(u); if(lv<0) continue;
      for(i=0,s=0;i<16;i++) if(g_gcs[i].id==*(U32*)(u+0xC)+1){ s=&g_gcs[i]; break; }
      if(!s){ for(i=0;i<16;i++) if(!g_gcs[i].id || f-g_gcs[i].lastf>250){ s=&g_gcs[i]; memset(s,0,sizeof *s); s->id=*(U32*)(u+0xC)+1; s->x=px; s->y=py; s->lv=lv; break; } }
      if(!s) continue;
      if(s->lastf && f-s->lastf<=2 && s->lv==lv && abs(px-s->x)+abs(py-s->y)>=gcJump){ s->until=f+10; s->why=2; }
      if(m==18 && gc_skill(u)){ if(s->until<f+5) s->until=f+5; s->why=1; }
      s->x=px; s->y=py; s->lv=lv; s->lastf=f;
      if(f>=s->until) continue;
      cnt=0;
      for(k=0;k<128 && cnt<32;k++) for(it=*(U8**)(g+0x1920+k*4),i=0; it && i<256 && cnt<32; it=*(U8**)(it+0xE4),i++){
        U8* sp; int dx,dy;
        if(*(U32*)it!=4 || *(U32*)(it+0x10)!=3) continue;
        sp=*(U8**)(it+0x2C); if(!sp || !*(U8**)sp) continue;
        dx=*(int*)(sp+0xC)-px; dy=*(int*)(sp+0x10)-py; if(dx*dx+dy*dy>gcRad*gcRad) continue;
        if(((int(__stdcall*)(void*))0x62B400)(it)!=4) continue;
        if(ROOM_LVL(*(U8**)sp)!=lv) continue;
        cand[cnt++]=it; }
      for(i=0;i<cnt;i++){ int g0=GetUStat(u,14,0), mx=((int(__stdcall*)(void*))0x622E70)(u), amt=GetUStat(cand[i],14,0);
        if(g0>=mx){ if(g_gcn<40){ g_gcn++; logf_("GC: player %u gold full (%d/%d), %d piles left",*(U32*)(u+0xC),g0,mx,cnt-i); } break; }
        if(gc_pick(g,u,cand[i])){ g_gcg++; if(g_gcg<=60||g_gcg%50==0) logf_("GC gold pickup #%d: player %u +%d gold during %s (now %d)",g_gcg,*(U32*)(u+0xC),amt,s->why==1?"Charge":"Teleport",GetUStat(u,14,0)); } }
    }
  } __except(1){ logf_("GC tick fault"); }
}
__declspec(naked) static void frame_stub(void){ __asm {
  pushad
  push edi
  call pot_tick
  popad
  pushad
  push edi
  call gc_tick
  popad
  pushad
  push edi
  call pt_tick
  popad
  pushad
  push edi
  call j_tick
  popad
  push 0x52D7B0
  ret
}}
static int call_orig(Pkt_t fn,void* g,void* pl,U8* pkt,U32 sz){ int r; __asm {
  push sz
  push pkt
  mov eax,pkt
  mov edx,pl
  mov ecx,g
  call fn
  mov r,eax
} return r; }
static const int g_jskill[]={54,107,132,143,151,270,275,219,220,292};
static const int g_jobj[]={59,60,119,145,156,157,237,238,288,323,324,398,402,429,494,496,511,539};
static U8* junit(U8* g,int type,U32 id){ U8* u; int n; if(type<0||type>5) return 0;
  for(u=*(U8**)(g+0x1120+type*0x200+(id&0x7F)*4),n=0; u && n<64; u=*(U8**)(u+0xE4),n++) if(*(U32*)(u+0xC)==id) return u; return 0; }
static int jblock(U8* g,U8* pl,U8* pkt,U32 sz){ JCtl* c; int op=pkt[0],i;
  if(!game_ok(g)) return 0; jreset(g); c=jfind(g,pl);
  if(!c){ if(op>=1 && op<=4 && g_jendf && g_jpost<5){ g_jpost++; logf_("J move after end passed untouched (op %d) [#%d]",op,g_jpost); } return 0; }
  if(op>=1 && op<=4 && cfgJPM && (op==1||op==3) && sz>=5){ int tx=*(unsigned short*)(pkt+1), ty=*(unsigned short*)(pkt+3), px=jx(pl), py=jy(pl);
    int dt=d2(tx,ty,g_jkx,g_jky), dp=d2(px,py,g_jkx,g_jky);
    if(dt>25 && dt>dp){ double L=sqrt((double)dt); int cx=g_jkx+(int)((tx-g_jkx)*3.0/L), cy=g_jky+(int)((ty-g_jky)*3.0/L);
      *(unsigned short*)(pkt+1)=(unsigned short)cx; *(unsigned short*)(pkt+3)=(unsigned short)cy; jlogb(pl,"sleepwalk: move away -> toward King",cx*10000+cy); return 2; }
    return 0; }
  if(op>=1 && op<=4){ if(!cfgJP) return 0;
    if((op==1||op==3) && sz>=5){ int tx=*(unsigned short*)(pkt+1), ty=*(unsigned short*)(pkt+3), px=jx(pl), py=jy(pl);
      int dt=d2(tx,ty,g_jkx,g_jky), dp=d2(px,py,g_jkx,g_jky);
      if(dt<=36 || dt<=dp) return 0;
      if(cfgJW){ double L=sqrt((double)dt); int cx=g_jkx+(int)((tx-g_jkx)*6.0/L), cy=g_jky+(int)((ty-g_jky)*6.0/L);
        *(unsigned short*)(pkt+1)=(unsigned short)cx; *(unsigned short*)(pkt+3)=(unsigned short)cy; /* JW: walk to the leash edge toward him */
        jlogb(pl,"move away -> walk toward King",cx*10000+cy); return 0; }
      jlogb(pl,"move away to",tx*10000+ty); return 1; }
    if(sz>=9 && *(U32*)(pkt+1)==1 && *(U32*)(pkt+5)==g_jkid) return 0;
    jlogb(pl,"move to unit type",*(U32*)(pkt+1)); jsnap(g,pl); return 1; }
  if(!cfgJB) return 0;
  if(op>=5 && op<=0x11){ void* s=op<=0x0A? GETLSK(pl):GETRSK(pl); int id=-1;
    if(s && *(U8**)s) id=**(short**)s;
    for(i=0;i<(int)(sizeof g_jskill/sizeof g_jskill[0]);i++) if(id==g_jskill[i]){ jlogb(pl,"skill",id); jsnap(g,pl); return 1; }
    return 0; }
  if(op==0x13 && sz>=9){ int t=*(int*)(pkt+1); U32 id=*(U32*)(pkt+5); U8* o;
    if(t==5){ jlogb(pl,"level warp",id); jsnap(g,pl); return 1; }
    if(t==2 && (o=junit(g,2,id))){ int cl=*(int*)(o+4);
      for(i=0;i<(int)(sizeof g_jobj/sizeof g_jobj[0]);i++) if(cl==g_jobj[i]){ jlogb(pl,"waypoint/portal object",cl); jsnap(g,pl); return 1; } }
    return 0; }
  if(op==0x20 && sz>=5){ U32 id=*(U32*)(pkt+1); void* inv=*(void**)(pl+0x60); void* it; int gd=0;
    if(inv) for(it=FirstItem(inv); it && gd<300; it=NextItem(it),gd++) if(*(U32*)((U8*)it+0xC)==id){ U8* b=ItemsBin(*(U32*)((U8*)it+4));
        if(b && (*(U32*)(b+0x80)==CODE("tsc ")||*(U32*)(b+0x80)==CODE("tbk "))){ jlogb(pl,"town portal item",id); return 1; } break; }
    return 0; }
  if(op==0x49){ jlogb(pl,"waypoint travel",0); return 1; }
  return 0; }
static int __fastcall j_pkt(void* g,void* pl,U8* pkt,U32 sz){ int op=0, b=0;
  __try { op=pkt[0]; b=jblock((U8*)g,(U8*)pl,pkt,sz); } __except(1){ b=0; logf_("J packet fault"); }
  if(b==1) return 0;
  if(op<=0||op>=0x50||!g_jorig[op]) return 3;
  if(b==2){ int r=call_orig(g_jorig[op],g,pl,pkt,sz); jsend0f((U8*)pl); return r; }
  return call_orig(g_jorig[op],g,pl,pkt,sz); }
static const U8 g_jops[]={1,2,3,4,5,6,7,8,9,0x0A,0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x13,0x20,0x49};
static const U32 g_jexp[]={0x5497E0,0x549890,0x5498D0,0x549920,0x549D00,0x549D80,0x549E00,0x549E80,0x549EE0,0x549F40,
  0x549FC0,0x54A040,0x54A0C0,0x54A140,0x54A1A0,0x54A200,0x54AA90,0x54B1E0,0x54C5D0};
static void frame_install(void){ static const U8 fc0[]={0x8B,0xCF,0xE8,0xCC,0xFE,0xFF,0xFF}; U8 c[5]; U32 rel;
  if(!bytes_eq(0x52D8DD,fc0,7)){ logf_("frame hook 0x52D8DF: SKIPPED (bytes mismatch)"); return; }
  rel=(U32)frame_stub-(0x52D8DF+5); c[0]=0xE8; memcpy(c+1,&rel,4);
  if(write_mem(0x52D8DF,c,5)){ g_fhook=1; logf_("frame hook 0x52D8DF: ON (potnodrop tick%s)",cfgC3?"":" idle, C3=0"); }
  else logf_("frame hook 0x52D8DF: SKIPPED (write failed)"); }
static void j_install(void){
  static const U8 fc[]={0x8B,0xCF,0xE8,0xCC,0xFE,0xFF,0xFF};
  int i, ok=1; U32 cnt=sizeof g_jops; void* h=(void*)j_pkt; U8 c[5]; U32 rel;
  cfgJ=GetPrivateProfileIntA("features","J",0,g_ini)!=0;
  if(!cfgJ){ logf_("J Fallen King mind control: OFF (cxmxdev.ini)"); return; }
  cfgJB=cfg("JB"); cfgJW=cfg("JW"); cfgJP=cfg("JP"); cfgJV=cfg("JV");
  cfgJPM=GetPrivateProfileIntA("features","JPM",0,g_ini)!=0; cfgJM=GetPrivateProfileIntA("features","JM",0,g_ini)!=0; cfgJMA=GetPrivateProfileIntA("features","JMA",0,g_ini)!=0;
  jRange=GetPrivateProfileIntA("features","JRange",20,g_ini); jDur=GetPrivateProfileIntA("features","JDur",125,g_ini);
  jCdMin=GetPrivateProfileIntA("features","JCdMin",375,g_ini); jCdMax=GetPrivateProfileIntA("features","JCdMax",500,g_ini);
  jDurBase=GetPrivateProfileIntA("features","JDurBase",jDur,g_ini); jDurPer=GetPrivateProfileIntA("features","JDurPer",25,g_ini);
  jDurStep=GetPrivateProfileIntA("features","JDurStep",3,g_ini); jDurMax=GetPrivateProfileIntA("features","JDurMax",250,g_ini); jEndDist=GetPrivateProfileIntA("features","JEndDist",5,g_ini);
  if(jDurBase<25) jDurBase=25; if(jDurStep<1) jDurStep=1; if(jDurPer<0) jDurPer=0; if(jDurMax<jDurBase) jDurMax=jDurBase; if(jDurMax>500) jDurMax=500; if(jEndDist<0) jEndDist=0;
  if(jRange<3) jRange=3; if(jRange>60) jRange=60; if(jDur<25) jDur=25; if(jDur>500) jDur=500;
  if(jCdMin<50) jCdMin=50; if(jCdMax<jCdMin) jCdMax=jCdMin;
  if(!g_fhook){ logf_("J: SKIPPED (no frame hook)"); return; } (void)fc;
  for(i=0;i<(int)cnt;i++) if(*(U32*)(PKT_TBL+g_jops[i]*8)!=g_jexp[i]){ ok=0; logf_("J: packet 0x%02X table entry %08X != %08X",g_jops[i],*(U32*)(PKT_TBL+g_jops[i]*8),g_jexp[i]); }
  if(!ok){ logf_("J: SKIPPED (packet table mismatch)"); return; }
  for(i=0;i<(int)cnt;i++){ g_jorig[g_jops[i]]=(Pkt_t)g_jexp[i]; if(!write_mem(PKT_TBL+g_jops[i]*8,&h,4)) ok=0; }
  (void)rel; (void)c;
  if(!ok){
    for(i=0;i<(int)cnt;i++) write_mem(PKT_TBL+g_jops[i]*8,&g_jexp[i],4);
    logf_("J: SKIPPED (write failed, packet table restored)"); return; }
  g_jon=1; rk_install(); logf_("J sleepwalk JPM=%d, merc target JM=%d, merc attacks owner JMA=%d",cfgJPM,cfgJM,cfgJMA);
  if(cfgJMA){ static const U8 hb[]={0x55,0x8B,0xEC,0x51,0x56}; U8 hc[5]; U32 hr;
    if(bytes_eq(0x554200,hb,5)){ hr=(U32)hostile_stub-(0x554200+5); hc[0]=0xE9; memcpy(hc+1,&hr,4);
      logf_("J merc hit hook 0x554200: %s",write_mem(0x554200,hc,5)?"ON (controlled merc may hit players)":"SKIPPED (write failed)"); }
    else logf_("J merc hit hook 0x554200: SKIPPED (bytes mismatch)");
    { U8 fb[9]; U32 fr=0x5347B0-(0x57DB72+5); fb[0]=0xE8; memcpy(fb+1,&fr,4); fb[5]=0x3B; fb[6]=0xF7; fb[7]=0x7D; fb[8]=0x54;
      if(bytes_eq(0x57DB72,fb,9)){ U8 fc[9]; fr=(U32)forcehit_stub-(0x57DB72+5); fc[0]=0xE9; memcpy(fc+1,&fr,4); memset(fc+5,0x90,4);
        logf_("J merc forced-hit hook 0x57DB72: %s",write_mem(0x57DB72,fc,9)?"ON (controlled merc never misses players)":"SKIPPED (write failed)"); }
      else logf_("J merc forced-hit hook 0x57DB72: SKIPPED (bytes mismatch)"); }
    { U8 pb[5]; U32 pr=0x57C060-(0x57C2AA+5); pb[0]=0xE8; memcpy(pb+1,&pr,4);
      if(bytes_eq(0x57C2AA,pb,5)){ pr=(U32)jpct_stub-(0x57C2AA+5); pb[0]=0xE9; memcpy(pb+1,&pr,4);
        logf_("J merc damage hook 0x57C2AA: %s",write_mem(0x57C2AA,pb,5)?"ON (controlled merc does full damage to players, not 17%)":"SKIPPED (write failed)"); }
      else logf_("J merc damage hook 0x57C2AA: SKIPPED (bytes mismatch)"); } }
  logf_("J Fallen King mind control: ON (frame 0x52D8DF, %u packet handlers; JB=%d JP=%d JV=%d JW=%d range %d dur %d+%d/%d max %d enddist %d cd %d-%d)",cnt,cfgJB,cfgJP,cfgJV,cfgJW,jRange,jDurBase,jDurPer,jDurStep,jDurMax,jEndDist,jCdMin,jCdMax);
}
/* asscharge (2026-10-18): the Charge (seqnum 4) sequence table is per WEAPON CLASS, not per player class (D2MOO
   SequenceTbls.cpp gPlayerWeaponsSequenceCharge). 1.14d Game.exe: sequence table 0x7483B8, [4] (at 0x7483C8) = 0x746B88 = 14 records
   {D2AnimSeqTxt* ptr, frames, frames} (12 bytes), Charge frames at 0x746B28 (15 x 6 bytes). Records 8-13 (1JS 1JT 1SS
   1ST HT1 HT2) are empty, so an assassin with claws/katars (HT1/HT2) has no sequence and Charge fails. Fill HT1/HT2
   with a copy whose A1 frames are clamped to the assassin's 11 A1 frames (vanilla uses A1 frame 12), and 1JS-1ST
   (barbarian dual wield, 16 A1 frames) with the vanilla frames. Data write only, at install. */
static U8 g_chseq[15*6]={
  0,0,3,0,0,1, 0,0,3,1,0,1, 0,0,3,2,0,1, 0,0,3,3,0,1, 0,0,3,4,0,1, 0,0,3,5,0,1, 0,0,3,6,0,1, 0,0,3,7,0,1,
  0,0,7,1,0,0, 0,0,7,3,0,0, 0,0,7,4,0,3, 0,0,7,5,0,1, 0,0,7,7,0,0, 0,0,7,9,0,0, 0,0,7,10,0,0 };
static void chseq_install(void){
  static const U8 vs[18]={0,0,3,0,0,1, 0,0,3,1,0,1, 0,0,3,2,0,1};
  U32* tb=(U32*)0x746B88; U32 rec[3]; int i,ok=1;
  if(!cfg("CH")){ logf_("CH assassin Charge sequence: OFF (cxmxdev.ini)"); return; }
  __try { if(*(U32*)0x7483C8!=0x746B88 || tb[0]!=0x746B28 || tb[1]!=15 || tb[2]!=15 || !bytes_eq(0x746B28,vs,18)) ok=0;
    for(i=8;i<14 && ok;i++) if(tb[i*3]) ok=0; } __except(1){ ok=0; }
  if(!ok){ __try { logf_("CH assassin Charge sequence: SKIPPED (table mismatch or already filled: [0x7483C8]=%08X rec0 %08X/%u/%u rec12 %08X rec13 %08X)",*(U32*)0x7483C8,tb[0],tb[1],tb[2],tb[36],tb[39]); } __except(1){ logf_("CH assassin Charge sequence: SKIPPED (table unreadable)"); } return; }
  for(i=8;i<14;i++){ rec[0]= i>=12 ? (U32)g_chseq : 0x746B28; rec[1]=15; rec[2]=15; if(!write_mem(0x746B88+i*12,rec,12)) ok=0; }
  logf_("CH assassin Charge sequence: %s (HT1/HT2 -> clamped copy %p, 1JS-1ST -> vanilla 0x746B28)",ok?"ON":"PARTIAL (write failed)",g_chseq);
}
/* Integrity tag (cxmx-mod-lod-114d by cxmx-dev, CC BY-NC-SA 4.0). Read-only: a worker thread hashes
   (FNV-1a 64) the mod's own data tables and patch data next to this DLL and compares them with the
   values below, then only sets the game window title to the credit or to the UNOFFICIAL BUILD tag.
   It never writes a file, never touches the game, saves or memory, and any fault is swallowed.
   A missing patch\ folder is fine (those entries are optional). */
static const char g_credit[]="cxmx-mod by cxmx-dev";
static const char g_unoff[]="UNOFFICIAL BUILD - not by cxmx-dev";
typedef struct { const char* rel; unsigned __int64 h; int req; } IntegFile;
static const IntegFile k_integ[]={
  {"data\\global\\excel\\armor.txt",0xB7671A5F9C434F11ULL,1},
  {"data\\global\\excel\\automagic.txt",0xAE006ED236F386E1ULL,1},
  {"data\\global\\excel\\belts.txt",0x32EA1DE5C0619F73ULL,1},
  {"data\\global\\excel\\charstats.txt",0xE9D1C1CA200E358DULL,1},
  {"data\\global\\excel\\gems.txt",0x83E1780280AA8113ULL,1},
  {"data\\global\\excel\\inventory.txt",0x8056148BAC829AB1ULL,1},
  {"data\\global\\excel\\itemtypes.txt",0x3E2FC26E29247554ULL,1},
  {"data\\global\\excel\\levels.txt",0x27F453D2606D6EF4ULL,1},
  {"data\\global\\excel\\MagicPrefix.txt",0xA64BFF057E71275DULL,1},
  {"data\\global\\excel\\misc.txt",0x8BB764421BE8E2AFULL,1},
  {"data\\global\\excel\\missiles.txt",0x2ACF5112ACB75DB2ULL,1},
  {"data\\global\\excel\\monstats.txt",0xA4AE713D5A419813ULL,1},
  {"data\\global\\excel\\monstats2.txt",0x9439F4C17249F9E9ULL,1},
  {"data\\global\\excel\\overlay.txt",0xFC0032D9F22301C2ULL,1},
  {"data\\global\\excel\\shrines.txt",0x69CE851B44C3AE2AULL,1},
  {"data\\global\\excel\\skilldesc.txt",0x55D50D9407EC652EULL,1},
  {"data\\global\\excel\\skills.txt",0x5EDA391429792F3BULL,1},
  {"data\\global\\excel\\states.txt",0x2E410691826DA28BULL,1},
  {"data\\global\\excel\\superuniques.txt",0xCAEC5C1D253C4A98ULL,1},
  {"data\\global\\excel\\treasureclassex.txt",0xC9DCB1F01D6CE37FULL,1},
  {"data\\global\\excel\\UniqueItems.txt",0x0EF0504BBE51BCF3ULL,1},
  {"data\\global\\excel\\weapons.txt",0xAEF39CC47D0D0216ULL,1},
  {"data\\local\\lng\\eng\\BaseMod.tbl",0x3FC0AF0CAFDF85D2ULL,1},
  {"data\\local\\lng\\eng\\patchstring.tbl",0x9CD38739A02C3CA4ULL,1},
  {"patch\\gameexe-patches.json",0x99FF9F9F00A3F1A9ULL,0},
  {"patch\\basemod-patches.json",0x7F0410D8348F3F4EULL,0},
};
static unsigned __int64 integ_hash(const char* p,int* ok){
  static unsigned char buf[65536]; unsigned __int64 h=0xCBF29CE484222325ULL; DWORD got,i; HANDLE f;
  *ok=0; f=CreateFileA(p,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
  if(f==INVALID_HANDLE_VALUE) return 0;
  while(ReadFile(f,buf,sizeof buf,&got,0) && got){ for(i=0;i<got;i++){ h^=buf[i]; h*=0x100000001B3ULL; } }
  CloseHandle(f); *ok=1; return h; }
static BOOL CALLBACK integ_enum(HWND w,LPARAM lp){ DWORD pid=0; GetWindowThreadProcessId(w,&pid);
  if(pid==GetCurrentProcessId() && IsWindowVisible(w) && !GetWindow(w,GW_OWNER)){ *(HWND*)lp=w; return FALSE; } return TRUE; }
static DWORD WINAPI integ_thread(LPVOID x){
  __try {
    char p[MAX_PATH], cur[256], nt[320]; const char* tag; int i, n, ok, bad=0, seen=0; unsigned __int64 h; HWND w; DWORD_PTR r;
    for(i=0;i<(int)(sizeof(k_integ)/sizeof(k_integ[0]));i++){
      _snprintf(p,MAX_PATH,"%s%s",g_dir,k_integ[i].rel); p[MAX_PATH-1]=0;
      h=integ_hash(p,&ok);
      if(!ok){ if(k_integ[i].req) bad++; continue; }
      seen++; if(h!=k_integ[i].h) bad++; }
    tag=bad?g_unoff:g_credit;
    logf_("integrity: %s (%d checked, %d differ or missing)",tag,seen,bad);
    for(n=0;n<240;n++){ /* 2 minutes: set the title once the window exists, re-set it if the game resets it */
      w=0; EnumWindows(integ_enum,(LPARAM)&w);
      if(w){ cur[0]=0;
        if(SendMessageTimeoutA(w,WM_GETTEXT,sizeof(cur),(LPARAM)cur,SMTO_ABORTIFHUNG,1000,&r)){ cur[sizeof(cur)-1]=0;
          if(!strstr(cur,"cxmx-dev")){ _snprintf(nt,sizeof(nt),"%s - %s",cur[0]?cur:"Diablo II",tag); nt[sizeof(nt)-1]=0;
            SendMessageTimeoutA(w,WM_SETTEXT,0,(LPARAM)nt,SMTO_ABORTIFHUNG,1000,&r); } } }
      Sleep(500); }
  } __except(1) { }
  return 0; }
static void integ_start(void){ HANDLE th; __try { th=CreateThread(0,0,integ_thread,0,0,0); if(th) CloseHandle(th); } __except(1) { } }
static void install(void){
  static const U8 qc[]={0x55,0x8B,0xEC,0x83,0xEC,0x08};
  static const U8 cs[]={0xE9,0x7F,0x01,0x00,0x00};
  static const U8 ss[]={0x55,0x8B,0xEC,0x81,0xEC,0x8C,0x03,0x00,0x00,0xE9};
  static const U8 sv[]={0xE9,0x2B,0x11,0x18,0x00};
  U32 p3=0x7417AC, p5=0x7417BC; void* h; int bm;
  g_late=1;
  integ_start();
  cfgA=cfg("A"); cfgB=cfg("B"); cfgC2=cfg("C2"); cfgC3=cfg("C3"); cfgD=cfg("D"); cfgE=cfg("E"); cfgF=cfg("F"); cfgG=cfg("G"); cfgH=GetPrivateProfileIntA("features","H",0,g_ini)!=0; cfgI=GetPrivateProfileIntA("features","I",0,g_ini)!=0;
  logf_("---- cxmxdev load pid %lu (installed %lu ms after DLL load, main thread)", GetCurrentProcessId(), GetTickCount()-g_t0);
  logf_("cxmxdev.ini features: A=%d B=%d C2=%d C3=%d D=%d E=%d F=%d G=%d H=%d (C removed)",cfgA,cfgB,cfgC2,cfgC3,cfgD,cfgE,cfgF,cfgG,cfgH);
  if(!cfgA) logf_("A socketed-potion buff: OFF (cxmxdev.ini)");
  else if(*(U32*)p3==0x5BE3F0 && *(U32*)p5==0x5BEAC0){
    orig3=(Potion_t)0x5BE3F0; orig5=(Potion_t)0x5BEAC0;
    h=(void*)hook3; write_mem(p3,&h,4); h=(void*)hook5; write_mem(p5,&h,4);
    logf_("A socketed-potion buff: ON (state %d, %d frames, unlimited stacking)",STATE_ID,BUFF_FRAMES);
  } else logf_("A socketed-potion buff: SKIPPED (potion table mismatch %08X %08X)",*(U32*)p3,*(U32*)p5);
  if(!cfgB) logf_("B /pot: OFF (cxmxdev.ini)");
  else if(bytes_eq(0x5466B0,qc,6) && bytes_eq(0x6CB6B9,cs,5) && bytes_eq(0x6CB75F,ss,10) && bytes_eq(0x54A5D0,sv,5)){
    write_jmp(0x6CB75F,(void*)server_stub); write_jmp(0x6CB6B9,(void*)client_stub);
    logf_("B /pot: ON");
    { static const U8 pc[]={0xE8,0xBA,0x9A,0x01,0x00};
      if(bytes_eq(PLACE_CALL,pc,5)){ U8 c[5]; U32 rel=(U32)place_stub-(PLACE_CALL+5); c[0]=0xE8; memcpy(c+1,&rel,4);
        g_place_ok=write_mem(PLACE_CALL,c,5); }
      logf_("B /pot stash placement: %s",g_place_ok?"ON":"SKIPPED (place call mismatch)"); }
    { static const U8 uc[]={0xE8,0x8F,0xEF,0xFF,0xFF};
      if(bytes_eq(UNIQ_CALL,uc,5)){ U8 c[5]; U32 rel=(U32)uniq_stub-(UNIQ_CALL+5); c[0]=0xE8; memcpy(c+1,&rel,4);
        g_uniq_ok=write_mem(UNIQ_CALL,c,5); }
      logf_("B /pot uniques (Munchies ring row %d, Tapped Heartstring row %d): %s",uniq_row("rin ",402,401),uniq_row("amu ",403,402),
            g_uniq_ok?"ON":"SKIPPED (creator call mismatch)"); }
  } else logf_("B /pot: SKIPPED (goldcmd caves not found)");
  logf_("C creator hook: REMOVED (sockets come from C2/C3)");
  if(!cfgD) logf_("D Telekinesis any item: OFF (cxmxdev.ini)");
  else { /* D: Telekinesis picks up any item. Server srvdofunc 21 = 0x5C98F0 (table 0x7322B0); for an
       item target it only picks up item types 0x16,4,0x26,0x38,9,0x29 (else cancels at 0x553380).
       0x5C9A84: jump straight to the pickup at 0x5C9AE5. DEV only. */
    static const U8 tk[]={0x6A,0x16,0x53,0xE8,0x24,0x01,0x06,0x00,0x85,0xC0,0x75,0x55};
    static const U8 jp[]={0xEB,0x5F};
    if(bytes_eq(0x5C9A84,tk,12) && write_mem(0x5C9A84,jp,2)) logf_("D Telekinesis any item: ON");
    else logf_("D Telekinesis any item: SKIPPED (0x5C9A84 bytes mismatch)"); }
  if(!cfgF && !cfgH) logf_("F/H Charge hook: OFF (cxmxdev.ini)");
  else { static const U8 ch[]={0xF6,0xC3,0x04,0x74,0x04,0x83,0x4D,0xDC,0x20};
    if(bytes_eq(CHARGE_AT,ch,9) && write_jmp(CHARGE_AT,(void*)charge_stub)) logf_("F/H Charge hook: ON (0x462DCF, F=%d H=%d)",cfgF,cfgH);
    else logf_("F Charge without Shift: SKIPPED (0x462DCF bytes mismatch)"); }
  bm=g_stab0?check_basemod():0; if(!g_stab0) logf_("C2: BaseMod jmp never stable, skipped");
  if(!cfgC2) logf_("C2 socket at ground drop: OFF (cxmxdev.ini)");
  else if(!bm) logf_("C2 socket at ground drop: SKIPPED (BaseMod creator jump missing or changed)");
  else { static const U8 dd[]={0x57,0x8B,0x7D,0x0C,0x85,0xFF,0x89,0x55,0xF8};
    if(bytes_eq(0x558AA6,dd,9) && write_jmp(0x558AA6,(void*)drop_stub)){ U8 nop=0x90; write_mem(0x558AAB,&nop,1);
      logf_("C2 socket at ground drop: ON (0x558AA6)"); }
    else logf_("C2 socket at ground drop: SKIPPED (0x558AA6 bytes mismatch)"); }
  { static const U8 mm[]={0x55,0x8B,0xEC,0x56,0x8B,0x75,0x08,0x85,0xF6,0x74,0x57};
    int ok=0;
    if(cfgC3||cfgE||cfgI){
      if(bytes_eq(0x624690,mm,11) && write_jmp(0x624694,(void*)mode_stub)){ static const U8 n2[2]={0x90,0x90}; write_mem(0x624699,n2,2); ok=1; }
      logf_("set-unit-mode hook 0x624694: %s",ok?"ON":"SKIPPED (bytes mismatch)"); }
    logf_("C3 socket on ground potion: %s",!cfgC3?"OFF (cxmxdev.ini)":(ok?"ON":"SKIPPED"));
    { static const U8 su[]={0x55,0x8B,0xEC,0x81,0xEC,0x10,0x01,0x00,0x00,0xA1,0xE0,0x9D,0x74,0x00};
      static const U8 rl[]={0x55,0x8B,0xEC,0x8B,0x45,0x08,0x85,0xC0,0x75,0x04,0x5D,0xC2,0x04,0x00,0x8B,0x48,0x10,0xE8};
      if(!cfgI) logf_("I Fallen King edge spawn: OFF (cxmxdev.ini)");
      else if(!ok) logf_("I Fallen King edge spawn: SKIPPED (no set-unit-mode hook)");
      else if(!bytes_eq(0x5A49B0,su,14)||!bytes_eq(0x61A1B0,rl,18)) logf_("I Fallen King edge spawn: SKIPPED (0x5A49B0/0x61A1B0 bytes mismatch)");
      else { g_iok=1; logf_("I Fallen King edge spawn: ON (su %d, Blood Moor/Cold Plains edge)",SU_FALLENKING); } }
    if(!cfgE) logf_("E buff meters: OFF (cxmxdev.ini)");
    else if(!ok) logf_("E buff meters: SKIPPED (no game-frame clock)");
    else { static const U8 sb[]={0x55,0x8B,0xEC,0x81,0xEC,0x04,0x02,0x00,0x00};
      g_meteron=1; logf_("E buff meters: ON (0x44CB22, game frames)");
      if(bytes_eq(0x478350,sb,9) && write_jmp(0x478350,(void*)send_stub)){ static const U8 n4[4]={0x90,0x90,0x90,0x90}; write_mem(0x478355,n4,4); g_sendok=1; }
      logf_("E LAN guest meters: %s",g_sendok?"ON (client send 0x478350, own drinks only)":"SKIPPED (0x478350 bytes mismatch)"); } }
  frame_install();
  cfgMG=cfg("MG"); mgPX=GetPrivateProfileIntA("features","MGPX",206,g_ini); mgPY=GetPrivateProfileIntA("features","MGPY",375,g_ini);
  mgMX=GetPrivateProfileIntA("features","MGMX",14,g_ini); mgMY=GetPrivateProfileIntA("features","MGMY",375,g_ini);
  mgPW=GetPrivateProfileIntA("features","MGPW",104,g_ini); mgMW=GetPrivateProfileIntA("features","MGMW",250,g_ini);
  mgPasses=GetPrivateProfileIntA("features","MGBoxPasses",2,g_ini); if(mgPasses<0) mgPasses=0; if(mgPasses>4) mgPasses=4;
  logf_("MG Gold/Magic Find text: %s (inventory +%d,+%d, merc panel +%d,+%d)",cfgMG?"ON":"OFF (cxmxdev.ini)",mgPX,mgPY,mgMX,mgMY);
  { static const U8 ea[]={0x55,0x8B,0xEC,0x83,0xEC,0x10}, eb[]={0x55,0x8B,0xEC,0x83,0xEC,0x08}; int ok=0; U8 c[6]; U32 rel;
    if(cfgC3){
      if(bytes_eq(0x5431F0,ea,6)){ rel=(U32)potsend_a-(0x5431F0+5); c[0]=0xE9; memcpy(c+1,&rel,4); c[5]=0x90; if(write_mem(0x5431F0,c,6)) ok|=1; }
      if(bytes_eq(0x5433F0,eb,6)){ rel=(U32)potsend_b-(0x5433F0+5); c[0]=0xE9; memcpy(c+1,&rel,4); c[5]=0x90; if(write_mem(0x5433F0,c,6)) ok|=2; } }
    logf_("potsend hooks 0x5431F0/0x5433F0: %s%s, potremove backstop: %s",(ok&1)?"A on":"A SKIPPED",(ok&2)?" B on":" B SKIPPED",cfgC3?"ON":"OFF"); }
  potcreate_install();
  chseq_install();
  pt_install();
  mmGap=GetPrivateProfileIntA("features","MeterGap",5,g_ini); if(mmGap<0||mmGap>40) mmGap=5;
  cfgGC=cfg("GC"); gcRad=GetPrivateProfileIntA("features","GCRad",10,g_ini); if(gcRad<2) gcRad=2; if(gcRad>30) gcRad=30;
  gcJump=GetPrivateProfileIntA("features","GCJump",6,g_ini); if(gcJump<3) gcJump=3;
  logf_("GC gold pickup during Charge/Teleport: %s (radius %d subtiles, jump >= %d, server frame hook %s)",cfgGC?"ON":"OFF (cxmxdev.ini)",gcRad,gcJump,g_fhook?"on":"MISSING - inactive");
  j_install();
  cfgCW=GetPrivateProfileIntA("features","CW",0,g_ini)!=0; cwDist=GetPrivateProfileIntA("features","CWDist",12,g_ini); if(cwDist<3) cwDist=3; if(cwDist>30) cwDist=30;
  cfgFQ=GetPrivateProfileIntA("features","FQ",0,g_ini)!=0; fqDist=GetPrivateProfileIntA("features","FQDist",8,g_ini); if(fqDist<3) fqDist=3; if(fqDist>30) fqDist=30;
  { static const U8 su[]={0x55,0x8B,0xEC,0x81,0xEC,0x10,0x01,0x00,0x00,0xA1,0xE0,0x9D,0x74,0x00}; U32 rel;
    if(!cfgFQ && !cfgCW) logf_("FQ/CW boss spawns: OFF (cxmxdev.ini)");
    else if(!bytes_eq(0x5A49B0,su,14)) logf_("FQ Frostquill (Coldcrow's Pet): SKIPPED (0x5A49B0 bytes mismatch)");
    else if(!(g_fqtr=(U8*)VirtualAlloc(0,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE))) logf_("FQ Frostquill: SKIPPED (no trampoline memory)");
    else { memcpy(g_fqtr,(void*)0x5A49B0,9); g_fqtr[9]=0xE9; rel=0x5A49B9-((U32)g_fqtr+14); memcpy(g_fqtr+10,&rel,4);
      if(write_jmp(0x5A49B0,(void*)fq_hook)){ U8 nop[4]={0x90,0x90,0x90,0x90}; write_mem(0x5A49B5,nop,4); g_fqok=1;
        logf_("FQ Frostquill (Coldcrow's Pet): ON (hook 0x5A49B0, su %d near su %d in Cave L1, dist %d subtiles, blue state %d)",SU_FROSTQUILL,SU_COLDCROW,fqDist,FQ_STATE); }
      if(g_fqok) logf_("CW Cairn Stone Warden: %s (su %d near su %d Rakanishu in Stony Field, dist %d subtiles)",cfgCW?"ON":"OFF (cxmxdev.ini)",SU_CAIRNWARDEN,SU_RAKANISHU,cwDist);
      else logf_("FQ Frostquill: SKIPPED (write failed)"); } }
  if(*(volatile U8*)0x558D90!=0xE9 || !g_stab0) logf_("install: BaseMod 0x558D90 not stable after %lu ms",GetTickCount()-g_t0);
}
/* DllMain (PlugY DllToLoad2): hook only the draw call 0x44CB22 (same as the old load-time install,
   page permission restored as found). Everything else installs on the main thread from draw_hook. */
/* Crash capture: vectored handler (first in line, so it sees the fault before D2's handler/Halt
   dialog) logs fatal-class exceptions to cxmxdev-crash.log, then lets the normal handlers run.
   First-chance: some may be caught later (our own __try, D2's), so it logs at most 6 per run. */
static int g_ncrash;
static void modoff(U32 a,char* o,int cap){ HMODULE hm=0; char m[MAX_PATH]="?",*n;
  if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)a,&hm)){
    GetModuleFileNameA(hm,m,MAX_PATH); n=strrchr(m,'\\'); _snprintf(o,cap,"%s+0x%X",n?n+1:m,a-(U32)hm); }
  else _snprintf(o,cap,"?"); o[cap-1]=0; }
static LONG CALLBACK crash_veh(PEXCEPTION_POINTERS ep){
  DWORD c=ep->ExceptionRecord->ExceptionCode; CONTEXT* x=ep->ContextRecord; char path[MAX_PATH], mo[160]; FILE* f; int i,k;
  /* crashlog2: any error-class code (0xC....... ), skip C++/debug/breakpoint; dedupe by address, 32 per run */
  static U32 seen[32]; static int nseen; U32 ea=(U32)ep->ExceptionRecord->ExceptionAddress;
  if((c&0xF0000000)!=0xC0000000 || c==0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
  for(i=0;i<nseen;i++) if(seen[i]==ea) return EXCEPTION_CONTINUE_SEARCH;
  if(nseen>=32) return EXCEPTION_CONTINUE_SEARCH; seen[nseen++]=ea; g_ncrash++;
  _snprintf(path,MAX_PATH,"%scxmxdev-crash.log",g_dir); path[MAX_PATH-1]=0;
  f=fopen(path,"a"); if(!f) return EXCEPTION_CONTINUE_SEARCH;
  { SYSTEMTIME t; GetLocalTime(&t); fprintf(f,"==== %04d-%02d-%02d %02d:%02d:%02d pid %lu tid %lu (first-chance #%d)\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),GetCurrentThreadId(),g_ncrash); }
  modoff((U32)ep->ExceptionRecord->ExceptionAddress,mo,160);
  fprintf(f,"code %08lX at %08X (%s)",c,(U32)ep->ExceptionRecord->ExceptionAddress,mo);
  if(c==EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters>=2)
    fprintf(f," %s %08X",ep->ExceptionRecord->ExceptionInformation[0]?(ep->ExceptionRecord->ExceptionInformation[0]==8?"exec":"write"):"read",(U32)ep->ExceptionRecord->ExceptionInformation[1]);
  fprintf(f,"\nEAX %08lX EBX %08lX ECX %08lX EDX %08lX ESI %08lX EDI %08lX EBP %08lX ESP %08lX EIP %08lX\nstack:",
    x->Eax,x->Ebx,x->Ecx,x->Edx,x->Esi,x->Edi,x->Ebp,x->Esp,x->Eip);
  __try { U32* sp=(U32*)x->Esp; for(i=0,k=0;i<512 && k<16;i++){ U32 v=sp[i]; HMODULE hm;
      if(v>0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)v,&hm)){
        modoff(v,mo,160); fprintf(f," %08X(%s)",v,mo); k++; } } } __except(1){ fprintf(f," (stack read fault)"); }
  fprintf(f,"\n"); fclose(f);
  return EXCEPTION_CONTINUE_SEARCH;
}
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevuef;
static LONG WINAPI crash_uef(PEXCEPTION_POINTERS ep){ char p[MAX_PATH]; FILE* f; _snprintf(p,MAX_PATH,"%scxmxdev-crash.log",g_dir); p[MAX_PATH-1]=0;
  if((f=fopen(p,"a"))){ fprintf(f,"---- LAST-CHANCE code %08lX at %08X\n",ep->ExceptionRecord->ExceptionCode,(U32)ep->ExceptionRecord->ExceptionAddress); fclose(f); }
  return g_prevuef?g_prevuef(ep):EXCEPTION_CONTINUE_SEARCH; }
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x){
  if(r==DLL_PROCESS_ATTACH){ char* s; static const U8 fc[]={0xE8,0x59,0xAE,0x02,0x00};
    DisableThreadLibraryCalls(h); g_t0=GetTickCount();
    GetModuleFileNameA(h,g_dir,MAX_PATH); s=strrchr(g_dir,'\\'); if(s) s[1]=0; else g_dir[0]=0;
    _snprintf(g_ini,MAX_PATH,"%scxmxdev.ini",g_dir); g_ini[MAX_PATH-1]=0;
    InitializeCriticalSection(&g_mcs);
    open_log();
    AddVectoredExceptionHandler(1,crash_veh); g_prevuef=SetUnhandledExceptionFilter(crash_uef);
    if(bytes_eq(0x44CB22,fc,5)){ U8 c[5]; U32 rel=(U32)draw_hook-(0x44CB22+5); c[0]=0xE8; memcpy(c+1,&rel,4);
      if(!write_mem(0x44CB22,c,5)){ g_done=1; logf_("draw hook 0x44CB22: write failed, nothing installed"); } }
    else { g_done=1; logf_("draw hook 0x44CB22: bytes mismatch, nothing installed"); } }
  return TRUE;
}
