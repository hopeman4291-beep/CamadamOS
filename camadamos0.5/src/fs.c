/* CamadamOS RAM file system with built-in starter files */
#include "kernel.h"

fsnode_t fs_nodes[FS_MAX];
u32 fs_rev;

int fs_find(int dir,const char *name){
    for(int i=0;i<FS_MAX;i++) if(fs_nodes[i].used && fs_nodes[i].parent==dir && i!=dir && strcmp(fs_nodes[i].name,name)==0) return i;
    return -1;
}
int fs_lookup(const char *path){
    int cur=0; char part[FS_NAME];
    while(*path=='/') path++;
    while(*path){
        int n=0; while(*path && *path!='/' && n<FS_NAME-1) part[n++]=*path++;
        part[n]=0; while(*path=='/') path++;
        cur=fs_find(cur,part); if(cur<0) return -1;
    }
    return cur;
}
int fs_create(int dir,const char *name,int type){
    if(fs_find(dir,name)>=0) return -1;
    for(int i=1;i<FS_MAX;i++) if(!fs_nodes[i].used){
        fsnode_t *n=&fs_nodes[i]; memset(n,0,sizeof *n);
        n->used=1; n->parent=dir; n->type=type; n->song=-1; strncpy0(n->name,name,FS_NAME);
        fs_rev++; return i;
    }
    return -1;
}
void fs_delete(int node){
    if(node<=0 || !fs_nodes[node].used) return;
    fs_rev++;
    for(int i=0;i<FS_MAX;i++) if(fs_nodes[i].used && fs_nodes[i].parent==node && i!=node) fs_delete(i);
    if(!fs_nodes[node].readonly || fs_nodes[node].type==FT_IMAGE) kfree(fs_nodes[node].data);
    fs_nodes[node].used=0;
}
static int ncmp(int a,int b){
    fsnode_t *x=&fs_nodes[a], *y=&fs_nodes[b];
    if((x->type==FT_DIR)!=(y->type==FT_DIR)) return x->type==FT_DIR?-1:1;
    return strcmp(x->name,y->name);
}
int fs_children(int dir,int *out,int max){
    int n=0;
    for(int i=1;i<FS_MAX && n<max;i++) if(fs_nodes[i].used && fs_nodes[i].parent==dir) out[n++]=i;
    for(int i=1;i<n;i++){ int v=out[i], j=i-1; while(j>=0 && ncmp(out[j],v)>0){ out[j+1]=out[j]; j--; } out[j+1]=v; }
    return n;
}
void fs_path(int node,char *out,int n){
    if(node<=0){ strncpy0(out,"/",n); return; }
    char tmp[256]; tmp[0]=0;
    int chain[16], c=0;
    while(node>0 && c<16){ chain[c++]=node; node=fs_nodes[node].parent; }
    for(int i=c-1;i>=0;i--){ strcat0(tmp,"/",sizeof tmp); strcat0(tmp,fs_nodes[chain[i]].name,sizeof tmp); }
    strncpy0(out,tmp,n);
}
void fs_unique_name(int dir,const char *base,const char *ext,char *out){
    for(int i=1;i<1000;i++){
        char num[12]; itoa10(i,num);
        strncpy0(out,base,FS_NAME); strcat0(out," ",FS_NAME); strcat0(out,num,FS_NAME); strcat0(out,ext,FS_NAME);
        if(fs_find(dir,out)<0) return;
    }
}

static int mkfile(int dir,const char *name,int type,const char *text){
    int n=fs_create(dir,name,type); if(n<0) return -1;
    fs_nodes[n].data=(char*)text; fs_nodes[n].size=text?strlen(text):0; fs_nodes[n].readonly=1;
    return n;
}

/* ---------- starter content ---------- */
static const char welcome_txt[]=
"Welcome to CamadamOS!\n\n"
"This whole operating system was written from scratch in C and assembly.\n"
"There is no Windows, no Linux and no macOS underneath. When your PC\n"
"starts, the GRUB bootloader loads our kernel into memory, and from that\n"
"moment everything on the screen is drawn by our own code.\n\n"
"Things to try:\n"
"- Double-click the icons on the left of the desktop.\n"
"- Drag windows around by their title bars.\n"
"- Play some music, then open Minesweeper while it plays.\n"
"- Plug in an Ethernet cable and open Web to browse.\n"
"- Change the wallpaper and colours in Settings.\n"
"- Draw something in Paint and press Save. It appears in /Pictures.\n\n"
"Note: files live in memory (RAM), so new files disappear when you restart.\n";
static const char howto_txt[]=
"Keyboard and mouse tips\n\n"
"Left click        select, press buttons, reveal squares\n"
"Right click       place a flag in Minesweeper\n"
"Double-click      open an icon or a file\n"
"Arrow keys/Space  scroll pages in the Web browser\n"
"F1                open the About window\n"
"F2 / F3           mouse slower / faster\n"
"F4                keyboard mouse (arrows, Enter, Space, Tab)\n"
"F5                Mouse Test (raw touchpad data)\n"
"Esc               close the Camadam menu\n\n"
"Piano keys: A W S E D F T G Y H U J K play one octave,\n"
"with W E T Y U as the black keys.\n";
static const char ideas_txt[]=
"Song ideas\n\n"
"- A slow blues for alto sax in C minor\n"
"- A tune built from the Camadam theme in the Music app\n"
"- Something with a Turkish makam flavour\n";

static const char home_page[]=
"<html><head><title>Camadam Web - Start</title></head><body>"
"<h1>Welcome to the Camadam Web</h1>"
"<p>This browser speaks the web of the 1990s: plain <b>HTML</b> over plain <b>HTTP</b>, through a network stack written from scratch for CamadamOS. Plug in an <b>Ethernet cable</b> and off you go.</p>"
"<form action=\"http://frogfind.com/\" method=\"get\"><b>Search the web with FrogFind:</b><br><input type=\"text\" name=\"q\" size=\"36\"> <input type=\"submit\" value=\"Search\"></form>"
"<p>You can also type words straight into the address bar to search, or type an address like <tt>info.cern.ch</tt>.</p>"
"<h2>Old-web friendly sites</h2><ul>"
"<li><a href=\"http://info.cern.ch/\">info.cern.ch</a> - the very first website, from 1991</li>"
"<li><a href=\"http://frogfind.com/\">FrogFind</a> - a search engine made for vintage computers</li>"
"<li><a href=\"http://68k.news/\">68k.news</a> - today's headlines, made for old browsers</li>"
"<li><a href=\"http://textfiles.com/\">textfiles.com</a> - a museum of text files from the BBS era</li>"
"<li><a href=\"https://en.wikipedia.org/wiki/Operating_system\">Wikipedia: Operating system</a> - opened through FrogFind</li>"
"</ul>"
"<h2>On this computer</h2><ul>"
"<li><a href=\"cam://howitworks\">How this operating system works</a></li>"
"<li><a href=\"cam://web\">How the web browser works</a></li>"
"<li><a href=\"cam://games\">Games</a> and <a href=\"cam://music\">music</a> on CamadamOS</li>"
"<li><a href=\"cam://about\">About CamadamOS</a></li></ul>"
"<hr><center><i>Best viewed with CamadamOS Web at 1024 x 768</i></center></body></html>";
static const char web_page[]=
"<html><head><title>How the browser works</title></head><body><h1>How the browser works</h1>"
"<p>When you open a web page, CamadamOS does every step itself:</p><ol>"
"<li><b>Network card driver.</b> It talks directly to the Ethernet chip (Intel e1000 or Realtek) to send and receive raw packets.</li>"
"<li><b>DHCP.</b> When the cable is connected, it asks the router for an IP address.</li>"
"<li><b>DNS.</b> It turns a name like frogfind.com into a number like 104.21.3.70.</li>"
"<li><b>ARP.</b> It finds the hardware address of the router on your local network.</li>"
"<li><b>TCP.</b> It opens a reliable connection to the web server: SYN, SYN-ACK, ACK.</li>"
"<li><b>HTTP.</b> It sends <tt>GET / HTTP/1.0</tt> and receives the page.</li>"
"<li><b>HTML.</b> It reads the tags (headings, paragraphs, links, lists, forms) and lays the words out on screen.</li></ol>"
"<h2>What about HTTPS?</h2><p>Most sites today use HTTPS, which encrypts everything. Writing that encryption from scratch is a huge project, so CamadamOS sends HTTPS addresses through <a href=\"http://frogfind.com/\">FrogFind</a>, which fetches the modern page and returns a simple version over plain HTTP.</p>"
"<p><a href=\"cam://home\">Back to the start page</a></p></body></html>";
static const char games_page[]=
"<html><head><title>Games</title></head><body><h1>Games</h1>"
"<h2>Minesweeper</h2><p>Clear the field without stepping on a mine. Numbers tell you how many mines touch that square. Right-click to plant a flag. Your first click is always safe.</p>"
"<h2>Solitaire</h2><p>Classic Klondike. Build the four foundation piles from Ace to King, one suit each. On the table, stack cards downward in alternating colours. Double-click a card to send it home.</p>"
"<h2>Snake</h2><p>Steer with the arrow keys, eat the apples and don't bite your own tail. It gets faster as you grow.</p>"
"<p><a href=\"cam://home\">Back to the start page</a></p></body></html>";
static const char music_page[]=
"<html><head><title>Music</title></head><body><h1>Music</h1>"
"<p>CamadamOS plays music through the <b>PC speaker</b>, the tiny beeper inside IBM-compatible PCs since 1981. It plays one note at a time as a square wave.</p>"
"<h2>Included songs</h2><ul><li>Ode to Joy (Beethoven)</li><li>Fur Elise (Beethoven)</li><li>Rondo alla Turca (Mozart)</li>"
"<li>In the Hall of the Mountain King (Grieg)</li><li>Greensleeves (traditional)</li><li>Twinkle, Twinkle, Little Star (traditional)</li><li>Camadam Blues (written for CamadamOS)</li></ul>"
"<p>There's also a <b>Piano</b>: play it with the keys A W S E D F T G Y H U J K.</p>"
"<p><a href=\"cam://home\">Back to the start page</a></p></body></html>";
static const char how_page[]=
"<html><head><title>How CamadamOS works</title></head><body><h1>How this operating system works</h1>"
"<h3>1. Power on</h3><p>The firmware (BIOS or UEFI) checks the hardware and looks for something bootable, such as your USB stick.</p>"
"<h3>2. The bootloader</h3><p>GRUB starts from the USB stick, switches the screen to 1024 x 768, loads the CamadamOS kernel into memory and jumps to its first instruction.</p>"
"<h3>3. Assembly start-up</h3><p>A few lines of assembly set up a stack and call the C function <tt>kmain</tt>. Everything after that is C.</p>"
"<h3>4. Taking control of the hardware</h3><p>The kernel installs its own GDT and IDT, reprograms the interrupt controller and sets the timer chip to tick 1000 times per second.</p>"
"<h3>5. Drivers</h3><p>Keyboard and touchpad bytes arrive through the PS/2 controller. The clock comes from the CMOS chip, sound from timer channel 2, and the network card is found by scanning the PCI bus.</p>"
"<h3>6. Drawing</h3><p>Every frame is drawn into an off-screen buffer, then only the changed pixels are copied to the graphics card.</p>"
"<h3>7. Windows</h3><p>The window manager keeps a list of windows from back to front, draws them in order, and sends each click to the window under the pointer.</p>"
"<p><a href=\"cam://web\">How the web browser works</a> - <a href=\"cam://home\">Start page</a></p></body></html>";
static const char about_page[]=
"<html><head><title>About CamadamOS</title></head><body><h1>About CamadamOS</h1>"
"<p>Version " OS_VERSION ". A hobby operating system built from scratch, made for learning and for fun.</p>"
"<h2>What is inside</h2><ul><li>A 32-bit x86 kernel in C with a little NASM assembly</li>"
"<li>Drivers for keyboard, mouse/touchpad, timer, clock, PC speaker and Ethernet</li>"
"<li>A TCP/IP network stack: ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP and HTTP</li>"
"<li>A graphical desktop with windows, a taskbar, themes and wallpapers</li>"
"<li>A file system that lives in memory</li>"
"<li>Programs: Minesweeper, Solitaire, Snake, Paint, Files, Web, Music, Piano, Notepad, Calculator, Terminal and Settings</li></ul>"
"<p><a href=\"cam://home\">Back to the start page</a></p></body></html>";

static void make_sunset(int dir){
    int n=fs_create(dir,"Sunset.img",FT_IMAGE); if(n<0) return;
    int w=320,h=200; u32 *px=kmalloc(w*h*4); if(!px) return;
    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
        u32 c = y<130 ? mix(RGB(40,30,110),RGB(255,140,60),y*255/130) : mix(RGB(30,60,120),RGB(10,20,50),(y-130)*255/70);
        int dx=x-160, dy=y-125; if(dx*dx+dy*dy<45*45 && y<130) c=mix(RGB(255,230,120),RGB(255,170,60),(y-80)*255/50>255?255:((y-80)<0?0:(y-80)*255/50));
        if(y>=130 && (y%6==0) && x>110+ (y-130) && x<210-(y-130) && ((x+y)%9<5)) c=RGB(255,180,90);
        px[y*w+x]=c;
    }
    fs_nodes[n].data=(char*)px; fs_nodes[n].img_w=w; fs_nodes[n].img_h=h; fs_nodes[n].size=w*h*4;
}

void fs_init(void){
    memset(fs_nodes,0,sizeof fs_nodes);
    fs_nodes[0].used=1; fs_nodes[0].type=FT_DIR; fs_nodes[0].parent=0; strncpy0(fs_nodes[0].name,"/",FS_NAME);
    int docs=fs_create(0,"Documents",FT_DIR), mus=fs_create(0,"Music",FT_DIR);
    int pages=fs_create(0,"Pages",FT_DIR), pics=fs_create(0,"Pictures",FT_DIR);
    fs_create(0,"Downloads",FT_DIR);
    mkfile(docs,"Welcome.txt",FT_TEXT,welcome_txt);
    mkfile(docs,"Tips.txt",FT_TEXT,howto_txt);
    mkfile(docs,"Song ideas.txt",FT_TEXT,ideas_txt);
    mkfile(pages,"home.htm",FT_PAGE,home_page);
    mkfile(pages,"web.htm",FT_PAGE,web_page);
    mkfile(pages,"games.htm",FT_PAGE,games_page);
    mkfile(pages,"music.htm",FT_PAGE,music_page);
    mkfile(pages,"howitworks.htm",FT_PAGE,how_page);
    mkfile(pages,"about.htm",FT_PAGE,about_page);
    for(int i=0;i<song_count;i++){
        char nm[FS_NAME]; strncpy0(nm,songs[i].title,FS_NAME-6); strcat0(nm,".song",FS_NAME);
        int n=mkfile(mus,nm,FT_SONG,0); if(n>=0) fs_nodes[n].song=i;
    }
    make_sunset(pics);
    basic_install_samples();
}
