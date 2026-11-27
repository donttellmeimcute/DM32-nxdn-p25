/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "monitor_session.h"
#include <assert.h>
#include <stdio.h>
static bool band(uint32_t f,void *ctx) {(void)ctx; return f>=146000000 && f<=146025000 && f!=146012500;}
struct mock_radio { bool lock, reject, fail_tune, fail_restore, fail_probe; unsigned tunes, probes, releases; };
static bool acquire(void *v) { struct mock_radio *m=v; if(m->reject) return false; assert(!m->lock); m->lock=true; return true; }
static bool restore(void *v) { struct mock_radio *m=v; assert(m->lock); return !m->fail_restore; }
static void release(void *v) { struct mock_radio *m=v; assert(m->lock); m->lock=false; ++m->releases; }
static bool tune(void *v,uint32_t f,uint32_t gen) { struct mock_radio *m=v; assert(m->lock && gen && band(f,NULL)); ++m->tunes; return !m->fail_tune; }
static bool probe(void *v,uint32_t gen,uint16_t enabled) { struct mock_radio *m=v; assert(m->lock && gen && enabled); ++m->probes; return !m->fail_probe; }
static void session_tests(void) {
    struct mc_session s={0}; struct mock_radio m={0};
    const struct mc_radio_ops ops={acquire,restore,release,tune,probe,band};
    struct mc_cfg c={146000000,146025000,12500,25,75,300,-75,1u<<MC_DMR,MC_BOTH};
    m.reject=true;
    assert(mc_session_start(&s,&c,0,&ops,&m)==MC_STOP && !m.lock && !m.tunes);
    m.reject=false;
    assert(mc_session_start(&s,&c,1,&ops,&m)==MC_TUNE && mc_session_tx_blocked(&s));
    struct mc_sample a={s.scan.tune_id,-50,MC_UNKNOWN,true};
    assert(mc_session_poll(&s,26,a,&ops,&m)==MC_IDLE);
    assert(mc_session_poll(&s,27,a,&ops,&m)==MC_PROBE && m.probes==1);
    a.evidence=MC_DMR;
    assert(mc_session_poll(&s,28,a,&ops,&m)==MC_FOUND && m.lock);
    mc_session_pause(&s);
    assert(mc_session_resume(&s,29,&ops,&m)==MC_TUNE && m.tunes==2);
    m.fail_restore=true;
    assert(!mc_session_stop(&s,&ops,&m) && s.restore_fault && m.lock && !m.releases);
    assert(mc_session_start(&s,&c,30,&ops,&m)==MC_STOP && m.lock);
    assert(mc_session_resume(&s,30,&ops,&m)==MC_IDLE);
    m.fail_restore=false;
    assert(mc_session_stop(&s,&ops,&m) && !mc_session_tx_blocked(&s) && m.releases==1);
    m.fail_tune=true;
    assert(mc_session_start(&s,&c,31,&ops,&m)==MC_STOP && !m.lock && m.releases==2);
    m.fail_tune=false; m.fail_probe=true;
    assert(mc_session_start(&s,&c,40,&ops,&m)==MC_TUNE);
    a=(struct mc_sample){s.scan.tune_id,-50,MC_UNKNOWN,true};
    assert(mc_session_poll(&s,65,a,&ops,&m)==MC_IDLE);
    assert(mc_session_poll(&s,66,a,&ops,&m)==MC_STOP && !m.lock && m.releases==3);
}
int main(void) {
    session_tests();
    struct mc_scan s={0}; struct mc_cfg c={146000000,146025000,12500,25,75,300,-75,(1u<<MC_FM)|(1u<<MC_DMR),MC_BOTH};
    assert(mc_start(&s,&c,0,band,NULL)==MC_TUNE && s.hz==146000000);
    struct mc_sample a={s.tune_id,-55,MC_DMR,true};
    assert(mc_poll(&s,24,a,band,NULL)==MC_IDLE);
    assert(mc_poll(&s,25,a,band,NULL)==MC_IDLE);
    a.tune_id--; assert(mc_poll(&s,26,a,band,NULL)==MC_IDLE);
    a.tune_id++; a.evidence=MC_UNKNOWN; assert(mc_poll(&s,27,a,band,NULL)==MC_PROBE);
    a.evidence=MC_DMR; assert(mc_poll(&s,28,a,band,NULL)==MC_FOUND && s.mode==MC_DMR);
    assert(mc_resume(&s,29,band,NULL)==MC_TUNE && s.hz==146025000);
    mc_pause(&s); assert(mc_poll(&s,100,a,band,NULL)==MC_IDLE);
    assert(mc_resume(&s,101,band,NULL)==MC_TUNE && s.hz==146000000);
    a=(struct mc_sample){s.tune_id,-60,MC_UNKNOWN,true};
    assert(mc_poll(&s,126,a,band,NULL)==MC_IDLE); assert(mc_poll(&s,127,a,band,NULL)==MC_PROBE);
    assert(mc_poll(&s,427,a,band,NULL)==MC_FOUND && s.mode==MC_UNKNOWN);
    c.filter=MC_DIGITAL; assert(mc_start(&s,&c,500,band,NULL)==MC_TUNE);
    a=(struct mc_sample){s.tune_id,-60,MC_FM,true};
    assert(mc_poll(&s,525,a,band,NULL)==MC_IDLE); assert(mc_poll(&s,526,a,band,NULL)==MC_PROBE);
    assert(mc_poll(&s,826,a,band,NULL)==MC_TUNE && s.hz==146025000);
    c.step=0; assert(mc_start(&s,&c,0,band,NULL)==MC_STOP);
    struct mc_morse m; assert(!mc_morse_init(&m,0)); assert(mc_morse_init(&m,20));
    for(char ch='A';ch<='Z';ch++){for(const char *p=mc_encode(ch);*p;p++)assert(mc_mark(&m,*p=='.'?60:180));assert(mc_letter(&m)==ch);}
    for(char ch='0';ch<='9';ch++){for(const char *p=mc_encode(ch);*p;p++)assert(mc_mark(&m,*p=='.'?60:180));assert(mc_letter(&m)==ch);}
    assert(!mc_mark(&m,120));assert(mc_letter(&m)=='?');
    printf("PASS: RX session failure recovery, monitor state/filter/stale RSSI and 36 Morse round trips; scan=%zu bytes, Morse=%zu bytes (host ABI)\n",sizeof s,sizeof m);
}
