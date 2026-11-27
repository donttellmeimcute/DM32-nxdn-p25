/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM32_MONITOR_CW_H
#define DM32_MONITOR_CW_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/* Hardware-independent prototype. The radio task must own tuning and inhibit TX. */
enum mc_mode { MC_UNKNOWN, MC_FM, MC_DMR, MC_P25, MC_NXDN48, MC_NXDN96, MC_YSF, MC_M17, MC_DSTAR, MC_CW };
enum mc_filter { MC_ANALOG=1, MC_DIGITAL=2, MC_BOTH=3, MC_MORSE=4 };
enum mc_action { MC_IDLE, MC_TUNE, MC_PROBE, MC_FOUND, MC_STOP };
enum mc_state { MC_OFF, MC_SETTLE, MC_LISTEN, MC_VERIFY, MC_HELD, MC_PAUSED };
struct mc_cfg { uint32_t lo, hi, step; uint16_t settle, dwell, verify; int16_t threshold; uint16_t enabled; uint8_t filter; };
typedef uint32_t (*mc_next_allowed)(uint32_t hz, const struct mc_cfg *cfg, void *ctx);
struct mc_scan { struct mc_cfg cfg; mc_next_allowed next; uint32_t hz, since, tune_id; int16_t peak; uint8_t state, mode; };
struct mc_sample { uint32_t tune_id; int16_t rssi; uint8_t evidence; bool valid; };
typedef bool (*mc_allowed)(uint32_t hz, void *ctx);
static inline bool mc_match(const struct mc_cfg *c, unsigned m) {
    if (m == MC_UNKNOWN || m > MC_CW || !(c->enabled & (1u << m))) return false;
    return m == MC_CW ? (c->filter & MC_MORSE) != 0 : m == MC_FM ? (c->filter & MC_ANALOG) != 0 : (c->filter & MC_DIGITAL) != 0;
}
static inline enum mc_action mc_stop(struct mc_scan *s) { s->state=MC_OFF; return MC_STOP; }
static inline enum mc_action mc_tune(struct mc_scan *s, uint32_t hz, uint32_t now) {
    s->hz=hz; s->since=now; ++s->tune_id; s->peak=-160; s->mode=MC_UNKNOWN; s->state=MC_SETTLE; return MC_TUNE;
}
/* Skip unavailable raster positions in bounded batches; stop rather than hang on a hole. */
static inline enum mc_action mc_advance(struct mc_scan *s, uint32_t now, mc_allowed allowed, void *ctx) {
    if (s->next) {
        uint32_t f=s->next(s->hz,&s->cfg,ctx);
        return f>=s->cfg.lo && f<=s->cfg.hi && (f-s->cfg.lo)%s->cfg.step==0 && allowed(f,ctx) ?
            mc_tune(s,f,now) : mc_stop(s);
    }
    uint32_t f=s->hz;
    for (unsigned n=0; n<256; ++n) {
        f=(f<s->cfg.lo || f>s->cfg.hi || s->cfg.hi-f<s->cfg.step) ? s->cfg.lo : f+s->cfg.step;
        if (allowed(f,ctx)) return mc_tune(s,f,now);
        if (f==s->hz) break;
    }
    return mc_stop(s);
}
static inline enum mc_action mc_start(struct mc_scan *s, const struct mc_cfg *c, uint32_t now, mc_allowed allowed, void *ctx) {
    if (!allowed || !c->lo || c->hi<c->lo || c->step<10 || c->settle<25 || c->dwell<75 || !c->verify || c->threshold < -150 || c->threshold > -30 || !c->filter || (c->filter & ~7u)) return mc_stop(s);
    s->cfg=*c; s->hz=0; return mc_advance(s,now,allowed,ctx);
}
static inline void mc_pause(struct mc_scan *s) { if (s->state!=MC_OFF) s->state=MC_PAUSED; }
static inline enum mc_action mc_resume(struct mc_scan *s, uint32_t now, mc_allowed allowed, void *ctx) {
    if (s->state!=MC_HELD && s->state!=MC_PAUSED) return MC_IDLE;
    return mc_advance(s,now,allowed,ctx);
}
/* MC_PROBE requests mode classification from the adapter; do not infer FM/CW from RSSI. */
static inline enum mc_action mc_poll(struct mc_scan *s, uint32_t now, struct mc_sample a, mc_allowed allowed, void *ctx) {
    if (s->state==MC_OFF || s->state==MC_HELD || s->state==MC_PAUSED) return MC_IDLE;
    if (s->state==MC_SETTLE) {
        if (now-s->since<s->cfg.settle) return MC_IDLE;
        s->state=MC_LISTEN; s->since=now; return MC_IDLE;
    }
    bool fresh=a.valid && a.tune_id==s->tune_id;
    if (fresh && a.rssi>=s->cfg.threshold) {
        if (a.rssi>s->peak) s->peak=a.rssi;
        if (mc_match(&s->cfg,a.evidence)) { s->mode=a.evidence; s->state=MC_HELD; return MC_FOUND; }
        if (s->state==MC_LISTEN) { s->state=MC_VERIFY; s->since=now; return MC_PROBE; }
    }
    if (s->state==MC_VERIFY && now-s->since>=s->cfg.verify) {
        /* Unknown strong RF is useful only in combined FM/digital discovery. */
        if ((s->cfg.filter & MC_BOTH)==MC_BOTH && fresh && a.rssi>=s->cfg.threshold && a.evidence==MC_UNKNOWN) { s->mode=MC_UNKNOWN; s->state=MC_HELD; return MC_FOUND; }
        return mc_advance(s,now,allowed,ctx);
    }
    if (s->state==MC_LISTEN && now-s->since>=s->cfg.dwell) return mc_advance(s,now,allowed,ctx);
    return MC_IDLE;
}
/* Fixed-speed Morse codec. 1=dot, 2=dash, 0=end of character. No RF output. */
static const char *const mc_codes[]={".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",".--","-..-","-.--","--..","-----",".----","..---","...--","....-",".....","-....","--...","---..","----."};
static inline const char *mc_encode(char c) {
    if(c>='a' && c<='z') c=(char)(c-'a'+'A');
    if(c>='A' && c<='Z') return mc_codes[c-'A'];
    if(c>='0' && c<='9') return mc_codes[26+c-'0'];
    return NULL;
}
static inline uint16_t mc_unit_ms(unsigned wpm) { return wpm>=5 && wpm<=40 ? (uint16_t)(1200u/wpm) : 0; }
struct mc_morse { uint16_t unit; uint8_t bits, count; bool bad; };
static inline bool mc_morse_init(struct mc_morse *m,unsigned wpm) { *m=(struct mc_morse){0}; m->unit=mc_unit_ms(wpm); return m->unit!=0; }
static inline bool mc_mark(struct mc_morse *m,uint32_t ms) {
    if (!m->unit || m->bad || m->count>=5 || ms<m->unit/2u || ms>m->unit*4u || (ms>m->unit*3u/2u && ms<m->unit*5u/2u)) { m->bad=true; return false; }
    m->bits=(uint8_t)((m->bits<<1)|(ms>=m->unit*5u/2u)); ++m->count; return true;
}
static inline char mc_letter(struct mc_morse *m) {
    char result='?';
    if (!m->bad && m->count) for(unsigned i=0;i<36;i++) {
        const char *p=mc_codes[i]; unsigned n=0,b=0;
        for(;*p;p++,n++) b=(b<<1)|(*p=='-');
        if(n==m->count && b==m->bits) {result=(char)(i<26?'A'+i:'0'+i-26);break;}
    }
    m->bits=m->count=0; m->bad=false; return result;
}
#endif
