/* CamadamOS music: song library and PC-speaker playback engine.
 * Notation: NOTE[#|b]OCTAVE:UNITS, R:UNITS for a rest, T<ms> to change tempo. */
#include "kernel.h"

const song_t songs[]={
 {"Ode to Joy","Ludwig van Beethoven",210,
  "E5:2 E5:2 F5:2 G5:2 G5:2 F5:2 E5:2 D5:2 C5:2 C5:2 D5:2 E5:2 E5:3 D5:1 D5:4 "
  "E5:2 E5:2 F5:2 G5:2 G5:2 F5:2 E5:2 D5:2 C5:2 C5:2 D5:2 E5:2 D5:3 C5:1 C5:4 "
  "D5:2 D5:2 E5:2 C5:2 D5:2 E5:1 F5:1 E5:2 C5:2 D5:2 E5:1 F5:1 E5:2 D5:2 C5:2 D5:2 G4:4 "
  "E5:2 E5:2 F5:2 G5:2 G5:2 F5:2 E5:2 D5:2 C5:2 C5:2 D5:2 E5:2 D5:3 C5:1 C5:4"},
 {"Fur Elise","Ludwig van Beethoven",125,
  "E5:1 D#5:1 E5:1 D#5:1 E5:1 B4:1 D5:1 C5:1 A4:2 R:1 C4:1 E4:1 A4:1 B4:2 R:1 E4:1 G#4:1 B4:1 C5:2 R:1 E4:1 "
  "E5:1 D#5:1 E5:1 D#5:1 E5:1 B4:1 D5:1 C5:1 A4:2 R:1 C4:1 E4:1 A4:1 B4:2 R:1 E4:1 C5:1 B4:1 A4:3 R:1 "
  "E5:1 D#5:1 E5:1 D#5:1 E5:1 B4:1 D5:1 C5:1 A4:2 R:1 C4:1 E4:1 A4:1 B4:2 R:1 E4:1 G#4:1 B4:1 C5:2 R:1 E4:1 "
  "E5:1 D#5:1 E5:1 D#5:1 E5:1 B4:1 D5:1 C5:1 A4:2 R:1 C4:1 E4:1 A4:1 B4:2 R:1 E4:1 C5:1 B4:1 A4:2 R:1 "
  "B4:1 C5:1 D5:1 E5:3 G4:1 F5:1 E5:1 D5:3 F4:1 E5:1 D5:1 C5:3 E4:1 D5:1 C5:1 B4:2 R:1 E4:1 E5:1 R:2 E5:1 E6:1 R:2 D#5:1 E5:1 R:2 D#5:1 "
  "E5:1 D#5:1 E5:1 D#5:1 E5:1 B4:1 D5:1 C5:1 A4:2 R:1 C4:1 E4:1 A4:1 B4:2 R:1 E4:1 G#4:1 B4:1 C5:2 R:1 E4:1 "
  "E5:1 D#5:1 E5:1 D#5:1 E5:1 B4:1 D5:1 C5:1 A4:2 R:1 C4:1 E4:1 A4:1 B4:2 R:1 E4:1 C5:1 B4:1 A4:6"},
 {"Rondo alla Turca","Wolfgang Amadeus Mozart",105,
  "B4:1 A4:1 G#4:1 A4:1 C5:4 D5:1 C5:1 B4:1 C5:1 E5:4 F5:1 E5:1 D#5:1 E5:1 B5:1 A5:1 G#5:1 A5:1 B5:1 A5:1 G#5:1 A5:1 C6:4 "
  "A5:2 B5:2 C6:2 B5:2 A5:2 G#5:2 A5:2 E5:2 F5:2 D5:2 C5:4 B4:3 A4:1 A4:4 "
  "B4:1 A4:1 G#4:1 A4:1 C5:4 D5:1 C5:1 B4:1 C5:1 E5:4 F5:1 E5:1 D#5:1 E5:1 B5:1 A5:1 G#5:1 A5:1 B5:1 A5:1 G#5:1 A5:1 C6:4 "
  "A5:2 B5:2 C6:2 B5:2 A5:2 G#5:2 A5:2 E5:2 F5:2 D5:2 C5:4 B4:3 A4:1 A4:8"},
 {"Hall of the Mountain King","Edvard Grieg",230,
  "B4:1 C#5:1 D5:1 E5:1 F#5:1 D5:1 F#5:2 F5:1 C#5:1 F5:2 E5:1 C5:1 E5:2 B4:1 C#5:1 D5:1 E5:1 F#5:1 D5:1 F#5:1 B5:1 A5:1 F#5:1 D5:1 F#5:1 A5:4 "
  "T190 B4:1 C#5:1 D5:1 E5:1 F#5:1 D5:1 F#5:2 F5:1 C#5:1 F5:2 E5:1 C5:1 E5:2 B4:1 C#5:1 D5:1 E5:1 F#5:1 D5:1 F#5:1 B5:1 A5:1 F#5:1 D5:1 F#5:1 A5:4 "
  "T150 F#5:1 G#5:1 A#5:1 B5:1 C#6:1 A#5:1 C#6:2 D6:1 A#5:1 D6:2 C#6:1 A#5:1 C#6:2 F#5:1 G#5:1 A#5:1 B5:1 C#6:1 A#5:1 C#6:2 D6:1 A#5:1 D6:2 C#6:4 "
  "T115 B4:1 C#5:1 D5:1 E5:1 F#5:1 D5:1 F#5:2 F5:1 C#5:1 F5:2 E5:1 C5:1 E5:2 B4:1 C#5:1 D5:1 E5:1 F#5:1 D5:1 F#5:1 B5:1 A5:1 F#5:1 D5:1 F#5:1 A5:4 "
  "T90 B5:2 F#5:2 D5:2 B4:2 B5:6"},
 {"Greensleeves","Traditional",135,
  "A4:2 C5:4 D5:2 E5:3 F5:1 E5:2 D5:4 B4:2 G4:3 A4:1 B4:2 C5:4 A4:2 A4:3 G#4:1 A4:2 B4:4 G#4:2 E4:4 "
  "A4:2 C5:4 D5:2 E5:3 F5:1 E5:2 D5:4 B4:2 G4:3 A4:1 B4:2 C5:3 B4:1 A4:2 G#4:3 F#4:1 G#4:2 A4:6 A4:4 R:2 "
  "G5:6 G5:3 F#5:1 E5:2 D5:4 B4:2 G4:3 A4:1 B4:2 C5:4 A4:2 A4:3 G#4:1 A4:2 B4:4 G#4:2 E4:4 R:2 "
  "G5:6 G5:3 F#5:1 E5:2 D5:4 B4:2 G4:3 A4:1 B4:2 C5:3 B4:1 A4:2 G#4:3 F#4:1 G#4:2 A4:6 A4:6"},
 {"Twinkle Twinkle Little Star","Traditional",230,
  "C5:2 C5:2 G5:2 G5:2 A5:2 A5:2 G5:4 F5:2 F5:2 E5:2 E5:2 D5:2 D5:2 C5:4 "
  "G5:2 G5:2 F5:2 F5:2 E5:2 E5:2 D5:4 G5:2 G5:2 F5:2 F5:2 E5:2 E5:2 D5:4 "
  "C5:2 C5:2 G5:2 G5:2 A5:2 A5:2 G5:4 F5:2 F5:2 E5:2 E5:2 D5:2 D5:2 C5:4"},
 {"Camadam Blues","Camadam",95,
  "C5:3 Eb5:1 F5:3 F#5:1 G5:6 R:2 Bb5:3 G5:1 F5:3 Eb5:1 C5:8 R:8 "
  "F5:3 Ab5:1 Bb5:3 B5:1 C6:6 R:2 Eb6:3 C6:1 Bb5:3 Ab5:1 F5:8 R:8 "
  "C5:3 Eb5:1 F5:3 F#5:1 G5:6 R:2 Bb5:3 G5:1 F5:3 Eb5:1 C5:8 R:8 "
  "G5:3 Bb5:1 G5:3 F5:1 Ab5:3 G5:1 F5:3 Eb5:1 F5:3 Eb5:1 C5:3 Bb4:1 C5:4 G4:2 Bb4:2 C5:12 R:4"},
};
const int song_count = sizeof songs / sizeof songs[0];

/* centi-Hertz for octave 4, C..B */
static const u32 base_ch[12]={26163,27718,29366,31113,32963,34923,36999,39200,41530,44000,46616,49388};
static u32 freq_of(int semi, int oct){
    u32 f=base_ch[semi];
    if(oct>=4) f <<= (oct-4); else f >>= (4-oct);
    return f/100;
}
u32 note_freq(int n){ /* n = semitones above C0 */ return freq_of(n%12, n/12); }

/* parse one token; returns pointer after token, or 0 at end */
static const char *parse(const char *p, u32 *freq, u32 *units, int *tempo){
    while(*p==' ') p++;
    if(!*p) return 0;
    *freq=0; *units=2; *tempo=0;
    if(*p=='T'){ p++; int v=0; while(*p>='0'&&*p<='9') v=v*10+(*p++-'0'); *tempo=v; *units=0; return p; }
    if(*p=='R'){ p++; }
    else {
        static const int idx[7]={9,11,0,2,4,5,7}; /* A B C D E F G */
        int semi=idx[*p-'A']; p++;
        if(*p=='#'){ semi++; p++; } else if(*p=='b'){ semi--; p++; }
        int oct=*p-'0'; p++;
        if(semi<0){ semi+=12; oct--; } if(semi>11){ semi-=12; oct++; }
        *freq=freq_of(semi,oct);
    }
    if(*p==':'){ p++; u32 v=0; while(*p>='0'&&*p<='9') v=v*10+(*p++-'0'); *units=v; }
    return p;
}

/* ---- engine state (touched by the timer interrupt) ---- */
static volatile int cur=-1;
static const char *pos;
static u32 note_end, gap_start, cur_freq, ms_unit;
static u32 total_ms, start_t;
static volatile u32 piano_until, piano_freq, piano_vis_until;

static u32 song_length_ms(int s){
    const char *p=songs[s].notes; u32 f,u; int t; u32 ms=songs[s].ms_per_unit, tot=0;
    while((p=parse(p,&f,&u,&t))){ if(t) ms=t; tot+=u*ms; }
    return tot;
}
void music_play(int s){
    if(s<0||s>=song_count) return;
    u32 len=song_length_ms(s);
    cli();
    cur=s; pos=songs[s].notes; ms_unit=songs[s].ms_per_unit; note_end=ticks; gap_start=ticks; cur_freq=0;
    total_ms=len; start_t=ticks; piano_until=0;
    sti();
}
void music_stop(void){ cli(); cur=-1; cur_freq=0; snd_tone_off(); sti(); }
int music_playing(void){ return (cur>=0 && cur<song_count) ? cur : -1; }
void music_play_notes(const char *notes,int ms){
    cli(); cur=99; pos=notes; ms_unit=ms; note_end=ticks; gap_start=ticks; cur_freq=0; total_ms=1; start_t=ticks; piano_until=0; sti();
}
int music_progress(void){ if(cur<0||!total_ms) return 0; u32 e=ticks-start_t; return e>=total_ms?1000:(int)(e*1000/total_ms); }
u32 music_current_freq(void){ if(piano_until>ticks||piano_vis_until>ticks) return piano_freq; return (cur>=0 && ticks<gap_start)?cur_freq:0; }
void piano_note(u32 f,u32 ms){
    if(snd.hda){ snd_note(f, ms<=150 ? INST_CHIP : INST_PIANO, ms, ms<=150?70:110); piano_freq=f; piano_vis_until=ticks+ms; return; }
    cli(); cur=-1; piano_freq=f; piano_until=ticks+ms; speaker_tone(f); sti();
}

void music_tick_ms(void){
    if(piano_until){
        if(ticks>=piano_until){ piano_until=0; speaker_off(); }
        return;
    }
    if(cur<0) return;
    if(ticks>=gap_start && cur_freq){ snd_tone_off(); cur_freq=0; }
    if(ticks<note_end) return;
    u32 f,u; int t;
    for(;;){
        const char *n=parse(pos,&f,&u,&t);
        if(!n){ cur=-1; snd_tone_off(); cur_freq=0; return; }
        pos=n;
        if(t){ ms_unit=t; continue; }
        break;
    }
    u32 dur=u*ms_unit;
    note_end=ticks+dur;
    u32 gap = dur/8; if(gap<12) gap=12; if(gap>60) gap=60;
    gap_start=note_end-gap;
    cur_freq=f;
    if(f) snd_tone_on(f); else snd_tone_off();
}
