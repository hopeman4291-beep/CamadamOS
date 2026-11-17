/* CamadamOS Chat: an IRC client over plain TCP (port 6667) */
#include "kernel.h"

#define ML 400
#define MC 120
#define TOP 44
#define BOTTOM 34
typedef struct {
    char lines[ML][MC]; u8 lcol[ML]; int n, scroll;
    char server[64], nick[24], chan[48], input[256];
    int focus;                /* 0 input, 1 server, 2 nick, 3 channel */
    int sock, state;          /* 0 offline, 1 connecting, 2 registered */
    char rx[1024]; int rxn;
    u32 last_rx; int pinged;
} irc_t;
static const u32 nickcols[8]={RGB(200,60,60),RGB(40,130,60),RGB(40,80,200),RGB(170,90,0),RGB(140,50,170),RGB(0,130,140),RGB(190,40,120),RGB(90,110,30)};
enum { C_TEXT, C_INFO, C_ME, C_ERR, C_NICK0 };

static int cols(window_t *w){ return imin(MC-1,(win_cw(w)-20)/8); }
static void add_line(window_t *w,irc_t *c,const char *s,int col){
    int maxc=cols(w);
    char buf[600]; strncpy0(buf,s,sizeof buf);
    const char *p=buf; int first=1;
    while(*p || first){
        if(c->n==ML){ memmove(c->lines[0],c->lines[1],(ML-1)*MC); memmove(c->lcol,c->lcol+1,ML-1); c->n--; }
        int take=imin((int)strlen(p),first?maxc:maxc-4);
        if(take<(int)strlen(p)){ int k=take; while(k>take-20 && k>0 && p[k]!=' ') k--; if(k>0 && p[k]==' ') take=k+1; }
        char *L=c->lines[c->n]; L[0]=0;
        if(!first) strncpy0(L,"    ",MC);
        int l0=(int)strlen(L); memcpy(L+l0,p,take); L[l0+take]=0;
        c->lcol[c->n]=(u8)col; c->n++;
        p+=take; first=0;
        if(!*p) break;
    }
    c->scroll=0; wm_invalidate();
}
static void to_ascii(const char *in,char *out,int n){
    u32 len=strlen(in), i=0; int k=0;
    while(i<len && k<n-1){
        u8 ch=(u8)in[i];
        if(ch==3){ i++; int d=0; while(d<2 && in[i]>='0'&&in[i]<='9'){ i++; d++; } if(in[i]==','&&in[i+1]>='0'&&in[i+1]<='9'){ i++; d=0; while(d<2 && in[i]>='0'&&in[i]<='9'){ i++; d++; } } continue; }
        if(ch<32){ i++; continue; }
        if(ch<128){ out[k++]=(char)ch; i++; continue; }
        const char *t=translit(utf8(in,len,&i)); while(*t && k<n-1) out[k++]=*t++;
    }
    out[k]=0;
}
static int nick_color(const char *nk){ u32 h=0; while(*nk && *nk!='!') h=h*31+(u8)*nk++; return C_NICK0+(int)(h%8); }
static void send_raw(irc_t *c,const char *s){
    if(c->sock<0) return;
    char b[600]; strncpy0(b,s,sizeof b); strcat0(b,"\r\n",sizeof b);
    tcp_write(c->sock,b,(int)strlen(b));
}
static void disconnect(window_t *w,irc_t *c,const char *why){
    if(c->sock>=0){ tcp_close(c->sock); c->sock=-1; }
    if(c->state) add_line(w,c,why,C_ERR);
    c->state=0;
}
static void handle_line(window_t *w,irc_t *c,char *line){
    char prefix[128]="", cmd[32]="", *params[16]; int np=0; char *trail=0;
    char *p=line;
    if(*p==':'){ p++; int k=0; while(*p && *p!=' ' && k<127) prefix[k++]=*p++; prefix[k]=0; while(*p==' ') p++; }
    int k=0; while(*p && *p!=' ' && k<31) cmd[k++]=*p++; cmd[k]=0;
    while(*p){
        while(*p==' ') p++;
        if(!*p) break;
        if(*p==':'){ trail=p+1; break; }
        if(np<15) params[np++]=p;
        while(*p && *p!=' ') p++;
        if(*p) *p++=0;
    }
    char nick[64]; int q=0; while(prefix[q] && prefix[q]!='!' && q<63){ nick[q]=prefix[q]; q++; } nick[q]=0;
    char t[600]; char msg[700];
    to_ascii(trail?trail:"",t,sizeof t);
    if(!strcmp(cmd,"PING")){ char r[300]; strncpy0(r,"PONG :",sizeof r); strcat0(r,trail?trail:"",sizeof r); send_raw(c,r); return; }
    if(!strcmp(cmd,"001")){
        c->state=2; add_line(w,c,"Connected! Type a message, or /join #channel",C_INFO);
        if(c->chan[0]){ char r[80]; strncpy0(r,"JOIN ",sizeof r); strcat0(r,c->chan,sizeof r); send_raw(c,r); }
        return;
    }
    if(!strcmp(cmd,"433")){ strcat0(c->nick,"_",sizeof c->nick); char r[64]; strncpy0(r,"NICK ",sizeof r); strcat0(r,c->nick,sizeof r); send_raw(c,r); add_line(w,c,"Nickname taken, trying another...",C_INFO); return; }
    if(!strcmp(cmd,"PRIVMSG")||!strcmp(cmd,"NOTICE")){
        const char *target=np?params[0]:"";
        int is_action = trail && !strncmp(trail,"\x01" "ACTION ",8);
        if(is_action){ to_ascii(trail+8,t,sizeof t); int L=(int)strlen(t); if(L && t[L-1]==1) t[L-1]=0; }
        else if(trail && trail[0]==1) return; /* other CTCP */
        msg[0]=0;
        if(target[0]!='#' && strcmp(cmd,"NOTICE")) strcat0(msg,"(private) ",sizeof msg);
        else if(strcmp(target,c->chan) && target[0]=='#'){ strcat0(msg,"[",sizeof msg); strcat0(msg,target,sizeof msg); strcat0(msg,"] ",sizeof msg); }
        if(is_action){ strcat0(msg,"* ",sizeof msg); strcat0(msg,nick,sizeof msg); strcat0(msg," ",sizeof msg); }
        else { strcat0(msg,"<",sizeof msg); strcat0(msg,nick[0]?nick:"server",sizeof msg); strcat0(msg,"> ",sizeof msg); }
        strcat0(msg,t,sizeof msg);
        add_line(w,c,msg,strcmp(cmd,"NOTICE")?nick_color(nick):C_INFO); return;
    }
    if(!strcmp(cmd,"JOIN")){
        const char *ch=trail?trail:(np?params[0]:"");
        if(!strcmp(nick,c->nick)){ strncpy0(c->chan,ch,sizeof c->chan); char tt[80]; strncpy0(tt,"Chat - ",sizeof tt); strcat0(tt,ch,sizeof tt); wm_set_title(w,tt); }
        strncpy0(msg,"--> ",sizeof msg); strcat0(msg,nick,sizeof msg); strcat0(msg," joined ",sizeof msg); strcat0(msg,ch,sizeof msg); add_line(w,c,msg,C_INFO); return;
    }
    if(!strcmp(cmd,"PART")||!strcmp(cmd,"QUIT")){
        strncpy0(msg,"<-- ",sizeof msg); strcat0(msg,nick,sizeof msg); strcat0(msg,!strcmp(cmd,"PART")?" left":" quit",sizeof msg);
        if(t[0]){ strcat0(msg," (",sizeof msg); strcat0(msg,t,sizeof msg); strcat0(msg,")",sizeof msg); }
        add_line(w,c,msg,C_INFO); return;
    }
    if(!strcmp(cmd,"NICK")){
        if(!strcmp(nick,c->nick)) strncpy0(c->nick,trail?trail:(np?params[0]:c->nick),sizeof c->nick);
        strncpy0(msg,nick,sizeof msg); strcat0(msg," is now known as ",sizeof msg); strcat0(msg,trail?trail:(np?params[0]:""),sizeof msg); add_line(w,c,msg,C_INFO); return;
    }
    if(!strcmp(cmd,"353")){ strncpy0(msg,"People here: ",sizeof msg); strcat0(msg,t,sizeof msg); add_line(w,c,msg,C_INFO); return; }
    if(!strcmp(cmd,"332")){ strncpy0(msg,"Topic: ",sizeof msg); strcat0(msg,t,sizeof msg); add_line(w,c,msg,C_INFO); return; }
    if(!strcmp(cmd,"ERROR")){ disconnect(w,c,t[0]?t:"Disconnected by the server."); return; }
    if(cmd[0]>='0'&&cmd[0]<='9'){ if(!strcmp(cmd,"366")||!strcmp(cmd,"333")) return; if(t[0]) add_line(w,c,t,cmd[0]=='4'?C_ERR:C_INFO); return; }
    if(!strcmp(cmd,"MODE")||!strcmp(cmd,"CAP")) return;
    if(t[0]) add_line(w,c,t,C_INFO);
}
static void st(const char *m){ (void)m; wm_pump(); }
static void do_connect(window_t *w,irc_t *c){
    if(c->state) return;
    add_line(w,c,"Connecting...",C_INFO); wm_pump();
    if(!net.present){ add_line(w,c,"No network card. Wired Ethernet is needed.",C_ERR); return; }
    if(!net_wait_ready(10000)){ add_line(w,c,"No network. Plug in an Ethernet cable.",C_ERR); return; }
    u32 ip; if(!dns_resolve(c->server,&ip)){ add_line(w,c,"Couldn't find that server.",C_ERR); return; }
    st(0);
    c->sock=tcp_open(ip,6667,64*1024);
    if(c->sock<0){ add_line(w,c,"The server didn't answer.",C_ERR); return; }
    c->state=1; c->rxn=0; c->last_rx=ticks; c->pinged=0;
    char r[128];
    strncpy0(r,"NICK ",sizeof r); strcat0(r,c->nick,sizeof r); send_raw(c,r);
    send_raw(c,"USER camadam 0 * :CamadamOS " OS_VERSION " user");
    add_line(w,c,"Connected to the server, logging in (this can take a few seconds)...",C_INFO);
}
static void command(window_t *w,irc_t *c,char *in){
    char r[400];
    if(in[0]!='/'){
        if(!c->state){ add_line(w,c,"Not connected. Press Connect first.",C_ERR); return; }
        if(!c->chan[0]){ add_line(w,c,"Join a channel first, e.g. /join #camadamos",C_ERR); return; }
        strncpy0(r,"PRIVMSG ",sizeof r); strcat0(r,c->chan,sizeof r); strcat0(r," :",sizeof r); strcat0(r,in,sizeof r); send_raw(c,r);
        char m[400]; strncpy0(m,"<",sizeof m); strcat0(m,c->nick,sizeof m); strcat0(m,"> ",sizeof m); strcat0(m,in,sizeof m); add_line(w,c,m,C_ME);
        return;
    }
    char *cmd=in+1, *arg=cmd; while(*arg && *arg!=' ') arg++; if(*arg){ *arg++=0; while(*arg==' ') arg++; }
    if(!strcmp(cmd,"join")||!strcmp(cmd,"j")){ if(*arg){ char ch[48]; if(arg[0]!='#'){ strncpy0(ch,"#",sizeof ch); strcat0(ch,arg,sizeof ch); } else strncpy0(ch,arg,sizeof ch); strncpy0(r,"JOIN ",sizeof r); strcat0(r,ch,sizeof r); send_raw(c,r); } }
    else if(!strcmp(cmd,"part")||!strcmp(cmd,"leave")){ strncpy0(r,"PART ",sizeof r); strcat0(r,*arg?arg:c->chan,sizeof r); send_raw(c,r); if(!*arg||!strcmp(arg,c->chan)) c->chan[0]=0; }
    else if(!strcmp(cmd,"nick")){ if(*arg){ strncpy0(r,"NICK ",sizeof r); strcat0(r,arg,sizeof r); if(c->state) send_raw(c,r); else strncpy0(c->nick,arg,sizeof c->nick); } }
    else if(!strcmp(cmd,"msg")){ char *t=arg; while(*t && *t!=' ') t++; if(*t){ *t++=0; strncpy0(r,"PRIVMSG ",sizeof r); strcat0(r,arg,sizeof r); strcat0(r," :",sizeof r); strcat0(r,t,sizeof r); send_raw(c,r);
        char m[400]; strncpy0(m,"-> ",sizeof m); strcat0(m,arg,sizeof m); strcat0(m,": ",sizeof m); strcat0(m,t,sizeof m); add_line(w,c,m,C_ME); } }
    else if(!strcmp(cmd,"me")){ strncpy0(r,"PRIVMSG ",sizeof r); strcat0(r,c->chan,sizeof r); strcat0(r," :\x01" "ACTION ",sizeof r); strcat0(r,arg,sizeof r); strcat0(r,"\x01",sizeof r); send_raw(c,r);
        char m[400]; strncpy0(m,"* ",sizeof m); strcat0(m,c->nick,sizeof m); strcat0(m," ",sizeof m); strcat0(m,arg,sizeof m); add_line(w,c,m,C_ME); }
    else if(!strcmp(cmd,"names")){ strncpy0(r,"NAMES ",sizeof r); strcat0(r,c->chan,sizeof r); send_raw(c,r); }
    else if(!strcmp(cmd,"topic")){ strncpy0(r,"TOPIC ",sizeof r); strcat0(r,c->chan,sizeof r); send_raw(c,r); }
    else if(!strcmp(cmd,"quit")||!strcmp(cmd,"disconnect")){ send_raw(c,"QUIT :Bye from CamadamOS"); disconnect(w,c,"Disconnected."); }
    else if(!strcmp(cmd,"connect")||!strcmp(cmd,"server")){ if(*arg) strncpy0(c->server,arg,sizeof c->server); do_connect(w,c); }
    else if(!strcmp(cmd,"clear")){ c->n=0; }
    else if(!strcmp(cmd,"raw")||!strcmp(cmd,"quote")) send_raw(c,arg);
    else if(!strcmp(cmd,"help")){
        add_line(w,c,"/join #channel   /part   /nick name   /msg nick text   /me does something",C_INFO);
        add_line(w,c,"/names   /topic   /clear   /quit   /connect server",C_INFO);
    } else add_line(w,c,"Unknown command. Try /help",C_ERR);
}
static void field(int x,int y,int w,const char *label,const char *v,int focused){
    draw_text(x,y+6,label,RGB(90,90,110));
    int fx=x+text_width(label)+6;
    fill_round_rect(fx,y,w,26,5,RGB(255,255,255)); draw_rect(fx,y,w,26,focused?accent_dark():RGB(190,186,204));
    draw_text(fx+6,y+5,v,RGB(30,30,45));
    if(focused && (ticks/500)%2==0) vline(fx+6+(int)strlen(v)*8,y+5,16,RGB(30,30,45));
}
static void irc_draw(window_t *w){
    irc_t *c=w->st; int cw=win_cw(w), ch=win_ch(w);
    fill_rect(0,0,cw,TOP,RGB(240,238,246)); hline(0,TOP-1,cw,RGB(200,196,214));
    field(8,9,150,"Server",c->server,c->focus==1);
    field(240,9,120,"Nick",c->nick,c->focus==2);
    field(406,9,110,"Channel",c->chan,c->focus==3);
    ui_button(cw-104,9,96,26,c->state?"Disconnect":"Connect",0);
    fill_rect(0,TOP,cw,ch-TOP-BOTTOM,RGB(255,255,255));
    int rows=(ch-TOP-BOTTOM-8)/17, first=imax(0,c->n-rows-c->scroll);
    for(int r=0;r<rows && first+r<c->n;r++){
        int li=first+r; int col=c->lcol[li];
        u32 cc = col==C_INFO?RGB(120,120,140): col==C_ERR?RGB(200,40,40): col==C_ME?accent_dark(): col>=C_NICK0?nickcols[col-C_NICK0]:RGB(30,30,40);
        const char *L=c->lines[li];
        if((col>=C_NICK0||col==C_ME) && L[0]=='<'){ /* colour the nick, keep message black */
            const char *e=L; while(*e && *e!='>') e++;
            char nk[64]; int k=imin(63,(int)(e-L)+1); memcpy(nk,L,k); nk[k]=0;
            draw_text_bold(10,TOP+4+r*17,nk,cc); draw_text(10+k*8,TOP+4+r*17,L+k,RGB(30,30,40));
        } else draw_text(10,TOP+4+r*17,L,cc);
    }
    fill_rect(0,ch-BOTTOM,cw,BOTTOM,RGB(244,242,250)); hline(0,ch-BOTTOM,cw,RGB(210,206,224));
    char pr[40]; strncpy0(pr,c->chan[0]?c->chan:"(no channel)",sizeof pr); strcat0(pr," >",sizeof pr);
    draw_text_bold(8,ch-BOTTOM+9,pr,accent_dark());
    int ix=16+text_width(pr), maxc=(cw-ix-12)/8; int L=(int)strlen(c->input);
    const char *shown=c->input+(L>maxc?L-maxc:0);
    draw_text(ix,ch-BOTTOM+9,shown,RGB(30,30,45));
    if(c->focus==0 && (ticks/500)%2==0) vline(ix+(int)strlen(shown)*8,ch-BOTTOM+8,16,RGB(30,30,45));
    fill_circle(cw-14,ch-BOTTOM/2,5,c->state==2?RGB(60,190,90):c->state==1?RGB(240,180,40):RGB(190,190,200));
}
static char *focused_buf(irc_t *c,int *n){
    switch(c->focus){ case 1: *n=sizeof c->server; return c->server; case 2: *n=sizeof c->nick; return c->nick; case 3: *n=sizeof c->chan; return c->chan; }
    *n=sizeof c->input; return c->input;
}
static void irc_key(window_t *w,int k){
    irc_t *c=w->st; int n; char *b=focused_buf(c,&n); int L=(int)strlen(b);
    if(k==KEY_CTRL('v')){ for(const char *p=clip_get();*p && L<n-1;p++) if(*p>=32 && *p<127) b[L++]=*p; b[L]=0; return; }
    if(k==KEY_CTRL('c')){ int tot=0; for(int i=0;i<c->n;i++) tot+=(int)strlen(c->lines[i])+1; char *o=kmalloc(tot+1); if(!o) return; int q=0;
        for(int i=0;i<c->n;i++){ int l=(int)strlen(c->lines[i]); memcpy(o+q,c->lines[i],l); q+=l; o[q++]='\n'; } clip_set(o,q); kfree(o); wm_toast("Copied the chat"); return; }
    if(k=='\n'){
        if(c->focus){ c->focus=0; if(!c->state) do_connect(w,c); return; }
        if(!c->input[0]) return;
        char line[256]; strncpy0(line,c->input,sizeof line); c->input[0]=0;
        command(w,c,line); return;
    }
    if(k=='\t'){ c->focus=(c->focus+1)%4; return; }
    if(k==KEY_UP){ c->scroll=imin(c->scroll+3,imax(0,c->n-5)); return; }
    if(k==KEY_DOWN){ c->scroll=imax(0,c->scroll-3); return; }
    if(k=='\b'){ if(L) b[L-1]=0; return; }
    if(k>=32 && k<127 && L<n-1){ b[L]=(char)k; b[L+1]=0; }
}
static void irc_mouse(window_t *w,int ev,int x,int y,int bt){
    irc_t *c=w->st; int cw=win_cw(w);
    if(ev==EV_WHEEL){ c->scroll=clampi(c->scroll-bt*3,0,imax(0,c->n-5)); return; }
    if(ev!=EV_DOWN) return;
    if(hit(x,y,cw-104,9,96,26)){ if(c->state){ send_raw(c,"QUIT :Bye from CamadamOS"); disconnect(w,c,"Disconnected."); } else do_connect(w,c); return; }
    if(hit(x,y,62,9,150,26)) c->focus=1;
    else if(hit(x,y,280,9,120,26)) c->focus=2;
    else if(hit(x,y,468,9,110,26)) c->focus=3;
    else c->focus=0;
}
static void irc_tick(window_t *w){
    irc_t *c=w->st; static u32 blink;
    if(ticks-blink>500){ blink=ticks; wm_invalidate(); }
    if(!c->state) return;
    char tmp[512]; int got;
    while((got=tcp_read(c->sock,tmp,sizeof tmp))>0){
        c->last_rx=ticks; c->pinged=0;
        for(int i=0;i<got;i++){
            char ch=tmp[i];
            if(ch=='\n'){ c->rx[c->rxn]=0; if(c->rxn && c->rx[c->rxn-1]=='\r') c->rx[c->rxn-1]=0; handle_line(w,c,c->rx); c->rxn=0; if(!c->state) return; }
            else if(c->rxn<(int)sizeof(c->rx)-1) c->rx[c->rxn++]=ch;
        }
    }
    if(!tcp_is_open(c->sock) && !tcp_avail(c->sock)){ disconnect(w,c,"The connection was closed."); return; }
    if(c->state==2 && ticks-c->last_rx>120000 && !c->pinged){ send_raw(c,"PING :camadamos"); c->pinged=1; }
    if(c->pinged && ticks-c->last_rx>180000) disconnect(w,c,"The server stopped answering.");
}
static void irc_close(window_t *w){ irc_t *c=w->st; if(c->state){ send_raw(c,"QUIT :Bye from CamadamOS"); } if(c->sock>=0) tcp_close(c->sock); }
void app_irc(void){
    window_t *w=wm_open("Chat",700,imin(480,scr_h-110),IC_IRC); if(!w) return;
    irc_t *c=kzalloc(sizeof *c); c->sock=-1;
    strncpy0(c->server,"irc.libera.chat",sizeof c->server);
    strncpy0(c->nick,"Camadam",sizeof c->nick); char d[8]; itoa10(100+rnd(900),d); strcat0(c->nick,d,sizeof c->nick);
    strncpy0(c->chan,"#camadamos",sizeof c->chan);
    w->st=c; w->draw=irc_draw; w->key=irc_key; w->mouse=irc_mouse; w->tick=irc_tick; w->close=irc_close;
    w->resizable=1; w->min_w=600; w->min_h=240;
    add_line(w,c,"Welcome to Chat! This is IRC, the internet's classic chat system (since 1988).",C_INFO);
    add_line(w,c,"Check the server, nickname and channel above, then press Connect.",C_INFO);
    add_line(w,c,"Tab moves between the boxes. Type /help for commands.",C_INFO);
}
