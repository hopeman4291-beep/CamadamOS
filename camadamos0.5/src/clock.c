/* CamadamOS Clock (clock, stopwatch, timer, alarm) and Weather */
#include "kernel.h"

static const s16 sin60[60]={0,105,208,309,407,500,588,669,743,809,866,914,951,978,995,1000,995,978,951,914,866,809,743,669,588,500,407,309,208,105,
    0,-105,-208,-309,-407,-500,-588,-669,-743,-809,-866,-914,-951,-978,-995,-1000,-995,-978,-951,-914,-866,-809,-743,-669,-588,-500,-407,-309,-208,-105};
static void now(int *h,int *m,int *s){ rtc_time(h,m,s); *h=(*h+clock_offset+24)%24; }
static void two(char *o,int v){ o[0]='0'+v/10; o[1]='0'+v%10; o[2]=0; }
static const char *wdays[7]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
static const char *months[12]={"January","February","March","April","May","June","July","August","September","October","November","December"};
static int weekday(int d,int m,int y){ static const int t[12]={0,3,2,5,0,3,5,1,4,6,2,4}; if(m<3) y--; return (y+y/4-y/100+y/400+t[m-1]+d)%7; }

/* ---------- global timer / alarm, serviced even when the window is closed ---------- */
static int timer_running; static u32 timer_end, timer_left_ms, timer_set_s=300;
static int alarm_on, alarm_h=7, alarm_m=0, alarm_fired_min=-1;
static u32 ring_until, ring_next; static int ring_step;
static void ring(const char *msg){ ring_until=ticks+12000; ring_next=ticks; ring_step=0; wm_message("Clock",msg); }
void clock_service(void){
    if(timer_running && ticks>=timer_end){ timer_running=0; timer_left_ms=0; ring("Time's up!"); }
    if(alarm_on){ int h,m,s; now(&h,&m,&s); if(h==alarm_h && m==alarm_m && alarm_fired_min!=h*60+m){ alarm_fired_min=h*60+m; char b[32]="Alarm! It's "; char t[4]; two(t,h); strcat0(b,t,32); strcat0(b,":",32); two(t,m); strcat0(b,t,32); ring(b); } }
    if(ring_until && ticks<ring_until && ticks>=ring_next){
        static const u16 notes[8]={1046,1318,1568,2093,0,1568,2093,0};
        if(notes[ring_step%8]) piano_note(notes[ring_step%8],110);
        ring_step++; ring_next=ticks+130;
    } else if(ring_until && ticks>=ring_until) ring_until=0;
}

typedef struct { int tab; int sw_run; u32 sw_start, sw_acc; u32 laps[8]; int nlaps; } clk_t;
static const char *ctabs[4]={"Clock","Stopwatch","Timer","Alarm"};
static void big_time(int cx,int y,const char *t,u32 c,int sc){ draw_text_big(cx-text_width(t)*sc/2,y,t,c,sc); }
static void ck_draw(window_t *w){
    clk_t *c=w->st; int cw=win_cw(w), ch=win_ch(w);
    gradient_v(0,0,cw,ch,RGB(24,22,40),RGB(46,36,72));
    for(int i=0;i<4;i++){
        int x=10+i*(cw-20)/4, bw=(cw-20)/4-6;
        fill_round_rect(x,8,bw,28,8,i==c->tab?accent_colors[theme_accent_idx]:RGB(60,54,86));
        draw_text_center(x,14,bw,ctabs[i],RGB(255,255,255));
    }
    int h,m,s; now(&h,&m,&s); char b[40], t[4];
    if(c->tab==0){
        int cx=cw/2, cy=170, r=110;
        fill_circle(cx,cy,r+6,RGB(255,190,60)); fill_circle(cx,cy,r,RGB(250,248,240));
        for(int i=0;i<60;i++){ int x1=cx+sin60[i]*(r-4)/1000, y1=cy-sin60[(i+15)%60]*(r-4)/1000; int L=i%5?4:12;
            int x2=cx+sin60[i]*(r-4-L)/1000, y2=cy-sin60[(i+15)%60]*(r-4-L)/1000; draw_line(x1,y1,x2,y2,i%5?RGB(150,150,160):RGB(40,40,50)); if(i%5==0) draw_line(x1+1,y1,x2+1,y2,RGB(40,40,50)); }
        for(int i=1;i<=12;i++){ int a=(i*5)%60; char n[4]; itoa10(i,n); int x=cx+sin60[a]*(r-28)/1000, y=cy-sin60[(a+15)%60]*(r-28)/1000; draw_text(x-text_width(n)/2,y-8,n,RGB(40,40,50)); }
        int ha=(h%12)*5+m/12;
        for(int d=-2;d<=2;d++) draw_line(cx+d/2,cy+d%2,cx+sin60[ha]*55/1000,cy-sin60[(ha+15)%60]*55/1000,RGB(30,30,40));
        for(int d=-1;d<=1;d++) draw_line(cx+d,cy,cx+sin60[m]*82/1000,cy-sin60[(m+15)%60]*82/1000,RGB(30,30,40));
        draw_line(cx,cy,cx+sin60[s]*92/1000,cy-sin60[(s+15)%60]*92/1000,RGB(220,50,50));
        fill_circle(cx,cy,5,RGB(220,50,50));
        two(t,h); strncpy0(b,t,sizeof b); strcat0(b,":",sizeof b); two(t,m); strcat0(b,t,sizeof b); strcat0(b,":",sizeof b); two(t,s); strcat0(b,t,sizeof b);
        big_time(cx,cy+r+18,b,RGB(255,255,255),3);
        int d,mo,y; rtc_date(&d,&mo,&y); if(mo<1||mo>12) mo=1;
        strncpy0(b,wdays[weekday(d,mo,y)],sizeof b); strcat0(b,", ",sizeof b); itoa10(d,t); strcat0(b,t,sizeof b); strcat0(b," ",sizeof b); strcat0(b,months[mo-1],sizeof b); strcat0(b," ",sizeof b); char yy[8]; itoa10(y,yy); strcat0(b,yy,sizeof b);
        draw_text_center(0,cy+r+74,cw,b,RGB(210,200,240));
    } else if(c->tab==1){
        u32 e=c->sw_acc+(c->sw_run?ticks-c->sw_start:0);
        int mm=(int)(e/60000), ss=(int)(e/1000%60), cs=(int)(e/10%100);
        two(t,mm%100); strncpy0(b,t,sizeof b); strcat0(b,":",sizeof b); two(t,ss); strcat0(b,t,sizeof b); strcat0(b,".",sizeof b); two(t,cs); strcat0(b,t,sizeof b);
        big_time(cw/2,70,b,RGB(255,255,255),4);
        ui_button(cw/2-160,150,100,34,c->sw_run?"Stop":"Start",c->sw_run); ui_button(cw/2-50,150,100,34,"Lap",0); ui_button(cw/2+60,150,100,34,"Reset",0);
        for(int i=0;i<c->nlaps;i++){ u32 l=c->laps[i]; char lb[40]; strncpy0(lb,"Lap ",sizeof lb); itoa10(i+1,t); strcat0(lb,t,sizeof lb); strcat0(lb,"    ",sizeof lb);
            two(t,(int)(l/60000)%100); strcat0(lb,t,sizeof lb); strcat0(lb,":",sizeof lb); two(t,(int)(l/1000%60)); strcat0(lb,t,sizeof lb); strcat0(lb,".",sizeof lb); two(t,(int)(l/10%100)); strcat0(lb,t,sizeof lb);
            draw_text_center(0,206+i*20,cw,lb,RGB(210,200,240)); }
    } else if(c->tab==2){
        u32 left = timer_running ? (timer_end>ticks?timer_end-ticks:0) : (timer_left_ms?timer_left_ms:timer_set_s*1000);
        u32 secs=(left+999)/1000;
        two(t,(int)(secs/60)%100); strncpy0(b,t,sizeof b); strcat0(b,":",sizeof b); two(t,(int)(secs%60)); strcat0(b,t,sizeof b);
        big_time(cw/2,70,b,timer_running?RGB(255,210,120):RGB(255,255,255),5);
        static const char *pre[5]={"1 min","3 min","5 min","10 min","25 min"};
        for(int i=0;i<5;i++) ui_button(cw/2-205+i*82,170,76,28,pre[i],0);
        ui_button(cw/2-160,210,60,28,"-1 m",0); ui_button(cw/2-96,210,60,28,"+1 m",0); ui_button(cw/2-32,210,64,28,"+10 s",0);
        ui_button(cw/2+40,210,120,28,timer_running?"Pause":"Start",timer_running);
        ui_button(cw/2-60,250,120,28,"Reset",0);
    } else {
        two(t,alarm_h); strncpy0(b,t,sizeof b); strcat0(b,":",sizeof b); two(t,alarm_m); strcat0(b,t,sizeof b);
        big_time(cw/2,70,b,alarm_on?RGB(255,210,120):RGB(160,150,190),5);
        ui_button(cw/2-150,170,60,28,"H+",0); ui_button(cw/2-150,204,60,28,"H-",0);
        ui_button(cw/2+90,170,60,28,"M+",0); ui_button(cw/2+90,204,60,28,"M-",0);
        ui_button(cw/2-70,186,140,32,alarm_on?"Alarm ON":"Alarm OFF",alarm_on);
        draw_text_center(0,250,cw,"The alarm rings even if you close this window.",RGB(180,170,210));
        if(ring_until) ui_button(cw/2-60,280,120,30,"Stop ringing",1);
    }
    (void)ch;
}
static void ck_mouse(window_t *w,int ev,int x,int y,int bt){
    clk_t *c=w->st; int cw=win_cw(w); (void)bt;
    if(ev!=EV_DOWN) return;
    for(int i=0;i<4;i++) if(hit(x,y,10+i*(cw-20)/4,8,(cw-20)/4-6,28)){ c->tab=i; return; }
    if(c->tab==1){
        if(hit(x,y,cw/2-160,150,100,34)){ if(c->sw_run){ c->sw_acc+=ticks-c->sw_start; c->sw_run=0; } else { c->sw_start=ticks; c->sw_run=1; } }
        if(hit(x,y,cw/2-50,150,100,34) && c->sw_run){ if(c->nlaps==8){ memmove(c->laps,c->laps+1,7*4); c->nlaps--; } c->laps[c->nlaps++]=c->sw_acc+ticks-c->sw_start; }
        if(hit(x,y,cw/2+60,150,100,34)){ c->sw_run=0; c->sw_acc=0; c->nlaps=0; }
    } else if(c->tab==2){
        static const int pm[5]={1,3,5,10,25};
        for(int i=0;i<5;i++) if(hit(x,y,cw/2-205+i*82,170,76,28)){ timer_set_s=pm[i]*60; timer_running=0; timer_left_ms=0; }
        if(!timer_running){
            if(hit(x,y,cw/2-160,210,60,28) && timer_set_s>60){ timer_set_s-=60; timer_left_ms=0; }
            if(hit(x,y,cw/2-96,210,60,28) && timer_set_s<99*60){ timer_set_s+=60; timer_left_ms=0; }
            if(hit(x,y,cw/2-32,210,64,28) && timer_set_s<99*60){ timer_set_s+=10; timer_left_ms=0; }
        }
        if(hit(x,y,cw/2+40,210,120,28)){
            if(timer_running){ timer_left_ms=timer_end>ticks?timer_end-ticks:0; timer_running=0; }
            else { timer_end=ticks+(timer_left_ms?timer_left_ms:timer_set_s*1000); timer_running=1; }
        }
        if(hit(x,y,cw/2-60,250,120,28)){ timer_running=0; timer_left_ms=0; ring_until=0; }
    } else if(c->tab==3){
        if(hit(x,y,cw/2-150,170,60,28)) alarm_h=(alarm_h+1)%24;
        if(hit(x,y,cw/2-150,204,60,28)) alarm_h=(alarm_h+23)%24;
        if(hit(x,y,cw/2+90,170,60,28)) alarm_m=(alarm_m+1)%60;
        if(hit(x,y,cw/2+90,204,60,28)) alarm_m=(alarm_m+59)%60;
        if(hit(x,y,cw/2-70,186,140,32)){ alarm_on=!alarm_on; alarm_fired_min=-1; }
        if(hit(x,y,cw/2-60,280,120,30)) ring_until=0;
    }
}
static void ck_tick(window_t *w){ static u32 last; clk_t *c=w->st; u32 iv = (c->tab==1&&c->sw_run)?40:250; if(ticks-last>=iv){ last=ticks; wm_invalidate(); } }
void app_clock(void){
    window_t *w=wm_open("Clock",480,400,IC_CLOCK); if(!w) return;
    clk_t *c=kzalloc(sizeof *c);
    w->st=c; w->draw=ck_draw; w->mouse=ck_mouse; w->tick=ck_tick;
}

/* =========================== Weather =========================== */
typedef struct { char date[12]; int tmax, tmin, code; } wday_t;
typedef struct {
    char city[40], area[48]; int focus, have, err;
    int temp, feels, hum, wind, code; char desc[48];
    wday_t days[3]; int ndays; char status[96];
} wx_t;
static const char *jfind(const char *s,const char *end,const char *key){
    int kl=(int)strlen(key);
    for(const char *p=s;p+kl+2<end;p++) if(p[0]=='"' && !strncmp(p+1,key,kl) && p[kl+1]=='"'){ p+=kl+2; while(p<end && (*p==' '||*p==':')) p++; return p; }
    return 0;
}
static int jint(const char *p){ if(!p) return 0; if(*p=='"') p++; int neg=0; if(*p=='-'){ neg=1; p++; } int v=0; while(*p>='0'&&*p<='9') v=v*10+(*p++-'0'); return neg?-v:v; }
static void jstr(const char *p,char *o,int n){
    o[0]=0; if(!p||*p!='"') return; p++; int k=0;
    while(*p && *p!='"' && k<n-1){
        if(*p=='\\' && p[1]=='u'){ u32 v=0; for(int i=2;i<6;i++){ char c=p[i]; v=v*16+((c>='0'&&c<='9')?c-'0':((c|32)-'a'+10)); } const char *t=translit(v); while(*t && k<n-1) o[k++]=*t++; p+=6; continue; }
        if(*p=='\\') p++;
        if((u8)*p>=0x80){ u32 i=0; const char *t=translit(utf8(p,4,&i)); while(*t && k<n-1) o[k++]=*t++; p+=i; continue; }
        o[k++]=*p++;
    }
    o[k]=0;
}
static int round_num(const char *p){ /* "12.7" -> 13 */ if(!p) return 0; int neg=0; if(*p=='-'){ neg=1; p++; } int v=0; while(*p>='0'&&*p<='9') v=v*10+(*p++-'0'); if(*p=='.' && p[1]>='5') v++; return neg?-v:v; }
/* map WMO (Open-Meteo) codes to wttr/WWO-style categories */
static int wmo_to_wwo(int c){
    if(c==0) return 113; if(c<=2) return 116; if(c==3) return 122; if(c==45||c==48) return 248;
    if((c>=51&&c<=67)||(c>=80&&c<=82)) return 296; if((c>=71&&c<=77)||c==85||c==86) return 338; if(c>=95) return 389; return 116;
}
static const char *code_desc(int c){
    switch(c){ case 113: return "Clear"; case 116: return "Partly cloudy"; case 119: case 122: return "Cloudy"; case 248: return "Fog";
        case 296: return "Rain"; case 338: return "Snow"; case 389: return "Thunderstorm"; }
    return "";
}
static int category(int c){ /* 0 sun,1 partly,2 cloud,3 fog,4 rain,5 snow,6 thunder */
    if(c==113) return 0; if(c==116) return 1; if(c==119||c==122) return 2; if(c==143||c==248||c==260) return 3;
    if(c==200||(c>=386&&c<=395)) return 6;
    if(c==179||c==182||c==185||(c>=227&&c<=230)||(c>=317&&c<=350)||(c>=368&&c<=377)) return 5;
    return 4;
}
static void wicon(int cat,int x,int y,int s){ /* s = size (≈ radius scale) */
    u32 sun=RGB(255,200,50), cloud=RGB(235,238,245), dark=RGB(150,155,170);
    if(cat==0||cat==1){ int cx=cat?x-s/3:x, cy=cat?y-s/3:y; for(int i=0;i<60;i+=5) draw_line(cx+sin60[i]*s*7/10/1000,cy-sin60[(i+15)%60]*s*7/10/1000,cx+sin60[i]*s/1000,cy-sin60[(i+15)%60]*s/1000,sun); fill_circle(cx,cy,s/2,sun); }
    if(cat>=1){ u32 c= cat>=4?dark:cloud; fill_circle(x-s/3,y+s/6,s/3,c); fill_circle(x+s/6,y-s/12,s*2/5,c); fill_circle(x+s/2,y+s/5,s/4,c); fill_rect(x-s/3,y+s/6,s*5/6,s/4+1,c); }
    if(cat==3) for(int i=0;i<3;i++) fill_rect(x-s/2,y+s/2+i*s/6,s,s/14+1,RGB(200,200,210));
    if(cat==4) for(int i=0;i<4;i++) draw_line(x-s/3+i*s/4,y+s/2,x-s/3+i*s/4-s/8,y+s*3/4,RGB(80,150,255));
    if(cat==5) for(int i=0;i<4;i++) fill_circle(x-s/3+i*s/4,y+s/2+(i&1)*s/8,s/14+1,RGB(255,255,255));
    if(cat==6){ draw_line(x,y+s/3,x-s/8,y+s/2,sun); draw_line(x-s/8,y+s/2,x+s/12,y+s/2,sun); draw_line(x+s/12,y+s/2,x-s/10,y+s*4/5,sun); }
}
static void wx_status(const char *m){ (void)m; wm_pump(); }
static int fetch_wttr(wx_t *x){
    char url[160], e[80]; int k=0;
    for(const char *p=x->city;*p && k<70;p++) e[k++]= *p==' ' ? '+' : *p;
    e[k]=0;
    strncpy0(url,"http://wttr.in/",sizeof url); strcat0(url,e,sizeof url); strcat0(url,"?format=j1",sizeof url);
    char *body; u32 n; char loc[200];
    int st=http_get(url,&body,&n,loc,sizeof loc,0,0,wx_status);
    if(st!=200||!body){ kfree(body); return st<0?st:-100; }
    const char *end=body+n;
    const char *cur=jfind(body,end,"current_condition");
    if(!cur){ kfree(body); return -101; }
    x->temp=jint(jfind(cur,end,"temp_C")); x->feels=jint(jfind(cur,end,"FeelsLikeC")); x->hum=jint(jfind(cur,end,"humidity"));
    x->wind=jint(jfind(cur,end,"windspeedKmph")); x->code=jint(jfind(cur,end,"weatherCode"));
    const char *wd=jfind(cur,end,"weatherDesc"); if(wd){ const char *v=jfind(wd,end,"value"); jstr(v,x->desc,sizeof x->desc); }
    const char *na=jfind(body,end,"nearest_area"); x->area[0]=0;
    if(na){ const char *an=jfind(na,end,"areaName"); if(an){ const char *v=jfind(an,end,"value"); jstr(v,x->area,sizeof x->area); } }
    const char *wk=jfind(body,end,"weather"); x->ndays=0;
    const char *ds=wk?jfind(wk,end,"date"):0;
    for(int d=0;d<3 && ds;d++){
        const char *next=jfind(ds,end,"date");
        const char *lim=next?next:end;
        wday_t *dy=&x->days[d]; jstr(ds,dy->date,sizeof dy->date);
        dy->tmax=jint(jfind(ds,lim,"maxtempC")); dy->tmin=jint(jfind(ds,lim,"mintempC"));
        const char *hc=ds; for(int i=0;i<5 && hc;i++){ const char *f=jfind(hc,lim,"weatherCode"); if(!f) break; hc=f; }
        dy->code=(hc&&hc!=ds)?jint(hc):x->code;
        x->ndays++; ds=next;
    }
    kfree(body); return 0;
}
static int fetch_openmeteo(wx_t *x){
    char url[200], e[80]; int k=0;
    for(const char *p=x->city;*p && k<70;p++) e[k++]= *p==' ' ? '+' : *p;
    e[k]=0;
    strncpy0(url,"http://geocoding-api.open-meteo.com/v1/search?count=1&name=",sizeof url); strcat0(url,e,sizeof url);
    char *body; u32 n; char loc[200];
    int st=http_get(url,&body,&n,loc,sizeof loc,0,0,wx_status);
    if(st!=200||!body){ kfree(body); return -1; }
    const char *end=body+n; const char *la=jfind(body,end,"latitude"), *lo=jfind(body,end,"longitude"), *nm=jfind(body,end,"name");
    if(!la||!lo){ kfree(body); return -2; }
    char lat[16], lon[16]; int a=0; while(a<15 && ((la[a]>='0'&&la[a]<='9')||la[a]=='.'||la[a]=='-')){ lat[a]=la[a]; a++; } lat[a]=0;
    a=0; while(a<15 && ((lo[a]>='0'&&lo[a]<='9')||lo[a]=='.'||lo[a]=='-')){ lon[a]=lo[a]; a++; } lon[a]=0;
    jstr(nm,x->area,sizeof x->area);
    kfree(body);
    strncpy0(url,"http://api.open-meteo.com/v1/forecast?current_weather=true&daily=weathercode,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=3&latitude=",sizeof url);
    strcat0(url,lat,sizeof url); strcat0(url,"&longitude=",sizeof url); strcat0(url,lon,sizeof url);
    st=http_get(url,&body,&n,loc,sizeof loc,0,0,wx_status);
    if(st!=200||!body){ kfree(body); return -3; }
    end=body+n;
    const char *cw=jfind(body,end,"current_weather"); if(!cw){ kfree(body); return -4; }
    x->temp=round_num(jfind(cw,end,"temperature")); x->wind=round_num(jfind(cw,end,"windspeed")); x->code=wmo_to_wwo(jint(jfind(cw,end,"weathercode")));
    x->feels=x->temp; x->hum=-1; strncpy0(x->desc,code_desc(x->code),sizeof x->desc);
    const char *dl=jfind(cw,end,"daily"); x->ndays=0;
    if(dl){
        const char *tm=jfind(dl,end,"time"), *wc=jfind(dl,end,"weathercode"), *mx=jfind(dl,end,"temperature_2m_max"), *mn=jfind(dl,end,"temperature_2m_min");
        if(tm&&wc&&mx&&mn){ tm++; wc++; mx++; mn++;
            for(int d=0;d<3;d++){
                wday_t *dy=&x->days[d]; jstr(tm,dy->date,sizeof dy->date);
                dy->code=wmo_to_wwo(jint(wc)); dy->tmax=round_num(mx); dy->tmin=round_num(mn); x->ndays++;
                while(*tm && *tm!=',') tm++; if(*tm) tm++; while(*wc && *wc!=',') wc++; if(*wc) wc++;
                while(*mx && *mx!=',') mx++; if(*mx) mx++; while(*mn && *mn!=',') mn++; if(*mn) mn++;
                if(!*tm) break;
            } }
    }
    kfree(body); return 0;
}
static void refresh(window_t *w,wx_t *x){
    strncpy0(x->status,"Loading weather...",sizeof x->status); x->err=0; wm_invalidate(); wm_pump();
    int r=fetch_wttr(x);
    if(r){ strncpy0(x->status,"Trying a second weather service...",sizeof x->status); wm_invalidate(); wm_pump(); r=fetch_openmeteo(x); }
    if(r){ x->err=1; x->have=0; strncpy0(x->status,net.dhcp==3?"Couldn't get the weather. Check the city name.":"No network: plug in an Ethernet cable.",sizeof x->status); }
    else { x->have=1; int h,m,s; now(&h,&m,&s); char t[4]; strncpy0(x->status,"Updated at ",sizeof x->status); two(t,h); strcat0(x->status,t,sizeof x->status); strcat0(x->status,":",sizeof x->status); two(t,m); strcat0(x->status,t,sizeof x->status); }
    char tt[64]; strncpy0(tt,"Weather - ",sizeof tt); strcat0(tt,x->have&&x->area[0]?x->area:x->city,sizeof tt); wm_set_title(w,tt);
}
static void wx_draw(window_t *w){
    wx_t *x=w->st; int cw=win_cw(w), ch=win_ch(w);
    int cat=x->have?category(x->code):1;
    u32 top= cat==0?RGB(70,150,240): cat>=4?RGB(70,80,110): RGB(110,140,190);
    gradient_v(0,0,cw,ch,top,mix(top,RGB(20,20,40),150));
    fill_round_rect(10,10,cw-130,30,8,RGB(255,255,255)); draw_rect(10,10,cw-130,30,x->focus?accent_dark():RGB(200,200,215));
    draw_text(20,17,x->city,RGB(30,30,45)); if(x->focus && (ticks/500)%2==0) vline(20+(int)strlen(x->city)*8,15,18,RGB(30,30,45));
    ui_button(cw-112,10,102,30,"Refresh",0);
    if(x->have){
        draw_text_bold(20,56,x->area[0]?x->area:x->city,RGB(255,255,255));
        wicon(cat,90,140,70);
        char b[48], n[12]; itoa10(x->temp,n); strncpy0(b,n,sizeof b); strcat0(b,"C",sizeof b);
        draw_text_big(190,96,b,RGB(255,255,255),4); fill_circle(190+text_width(n)*4+4,104,5,RGB(255,255,255)); fill_circle(190+text_width(n)*4+4,104,2,top);
        draw_text_bold(192,166,x->desc,RGB(240,240,255));
        int y=196;
        strncpy0(b,"Feels like ",sizeof b); itoa10(x->feels,n); strcat0(b,n,sizeof b); strcat0(b," C",sizeof b); draw_text(192,y,b,RGB(220,225,245)); y+=18;
        if(x->hum>=0){ strncpy0(b,"Humidity ",sizeof b); itoa10(x->hum,n); strcat0(b,n,sizeof b); strcat0(b,"%",sizeof b); draw_text(192,y,b,RGB(220,225,245)); y+=18; }
        strncpy0(b,"Wind ",sizeof b); itoa10(x->wind,n); strcat0(b,n,sizeof b); strcat0(b," km/h",sizeof b); draw_text(192,y,b,RGB(220,225,245));
        int bw=(cw-40)/3;
        for(int d=0;d<x->ndays;d++){
            int bx=20+d*bw, by=262;
            blend_rect(bx,by,bw-10,120,0xFFFFFF,40);
            wday_t *dy=&x->days[d];
            const char *label=d==0?"Today":d==1?"Tomorrow":"";
            char lb[24];
            if(!label[0]){ int yy=jint(dy->date), mm=jint(dy->date+5), dd=jint(dy->date+8); if(mm>=1&&mm<=12) strncpy0(lb,wdays[weekday(dd,mm,yy)],sizeof lb); else strncpy0(lb,dy->date,sizeof lb); label=lb; }
            draw_text_center(bx,by+8,bw-10,label,RGB(255,255,255));
            wicon(category(dy->code),bx+(bw-10)/2,by+54,30);
            itoa10(dy->tmax,n); strncpy0(b,n,sizeof b); strcat0(b," / ",sizeof b); itoa10(dy->tmin,n); strcat0(b,n,sizeof b); strcat0(b," C",sizeof b);
            draw_text_center(bx,by+94,bw-10,b,RGB(255,255,255));
        }
    } else if(!x->err && !x->status[0]) draw_text_center(0,160,cw,"Type a city and press Refresh.",RGB(255,255,255));
    draw_text(12,ch-22,x->status,RGB(230,230,250));
    draw_text(cw-text_width("wttr.in / open-meteo.com")-12,ch-22,"wttr.in / open-meteo.com",RGB(170,180,210));
}
static void wx_mouse(window_t *w,int ev,int px,int py,int b){
    wx_t *x=w->st; int cw=win_cw(w); (void)b;
    if(ev!=EV_DOWN) return;
    if(hit(px,py,cw-112,10,102,30)){ x->focus=0; refresh(w,x); return; }
    x->focus=hit(px,py,10,10,cw-130,30);
}
static void wx_key(window_t *w,int k){
    wx_t *x=w->st; int L=(int)strlen(x->city);
    if(k=='\n'){ x->focus=0; refresh(w,x); return; }
    if(!x->focus) { if(k>=32&&k<127){ x->focus=1; x->city[0]=0; L=0; } else return; }
    if(k=='\b'){ if(L) x->city[L-1]=0; }
    else if(k>=32 && k<127 && L<(int)sizeof x->city-1){ x->city[L]=(char)k; x->city[L+1]=0; }
}
static void wx_tick(window_t *w){ wx_t *x=w->st; static u32 last; if(x->focus && ticks-last>500){ last=ticks; wm_invalidate(); }
    static u32 auto_t; if(x->have && ticks-auto_t>30*60*1000){ auto_t=ticks; refresh(w,x); } }
void app_weather(void){
    window_t *w=wm_open("Weather",520,420,IC_WEATHER); if(!w) return;
    wx_t *x=kzalloc(sizeof *x); strncpy0(x->city,"Izmir",sizeof x->city);
    w->st=x; w->draw=wx_draw; w->mouse=wx_mouse; w->key=wx_key; w->tick=wx_tick;
    wm_pump();
    refresh(w,x);
}
