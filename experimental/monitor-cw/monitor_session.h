/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM32_MONITOR_SESSION_H
#define DM32_MONITOR_SESSION_H
#include "monitor_cw.h"
/*
 * Radio-task adapter contract.
 * acquire_rx must atomically take ownership, inhibit ALL TX producers and
 * snapshot the previous channel; false must leave the radio unchanged.
 * restore_rx must restore while the TX interlock is still asserted.
 * release_rx may drop ownership only after a successful restore.
 * All callbacks and session operations run in the same radio task.
 */
struct mc_radio_ops {
    bool (*acquire_rx)(void *);
    bool (*restore_rx)(void *);
    void (*release_rx)(void *);
    bool (*tune_rx)(void *, uint32_t, uint32_t);
    bool (*probe_rx)(void *, uint32_t, uint16_t);
    mc_allowed allowed;
    mc_next_allowed next_allowed;
};
struct mc_session {
    struct mc_scan scan;
    bool owns_rx, restore_fault;
};
static inline bool mc_session_tx_blocked(const struct mc_session *s) {
    return s->owns_rx;
}
static inline bool mc_session_stop(struct mc_session *s,
                                   const struct mc_radio_ops *ops, void *ctx) {
    mc_stop(&s->scan);
    if (!s->owns_rx) return true;
    if (!ops->restore_rx(ctx)) { s->restore_fault=true; return false; }
    ops->release_rx(ctx);
    s->owns_rx=false; s->restore_fault=false;
    return true;
}
static inline enum mc_action mc_session_apply(struct mc_session *s,
    enum mc_action a, const struct mc_radio_ops *ops, void *ctx) {
    if (!s->owns_rx || s->restore_fault) return MC_STOP;
    if (a==MC_TUNE && !ops->tune_rx(ctx,s->scan.hz,s->scan.tune_id))
        a=MC_STOP;
    if (a==MC_PROBE && !ops->probe_rx(ctx,s->scan.tune_id,s->scan.cfg.enabled))
        a=MC_STOP;
    if (a==MC_STOP) (void)mc_session_stop(s,ops,ctx);
    return a;
}
static inline enum mc_action mc_session_start(struct mc_session *s,
    const struct mc_cfg *cfg, uint32_t now, const struct mc_radio_ops *ops, void *ctx) {
    if (s->owns_rx || !cfg || !ops || !ops->acquire_rx || !ops->restore_rx ||
        !ops->release_rx || !ops->tune_rx || !ops->probe_rx || !ops->allowed)
        return MC_STOP;
    if (!ops->acquire_rx(ctx)) return MC_STOP;
    s->owns_rx=true; s->restore_fault=false;
    s->scan.next=ops->next_allowed;
    return mc_session_apply(s,mc_start(&s->scan,cfg,now,ops->allowed,ctx),ops,ctx);
}
static inline enum mc_action mc_session_poll(struct mc_session *s,
    uint32_t now, struct mc_sample sample, const struct mc_radio_ops *ops, void *ctx) {
    if (!s->owns_rx || s->restore_fault) return MC_IDLE;
    return mc_session_apply(s,mc_poll(&s->scan,now,sample,ops->allowed,ctx),ops,ctx);
}
static inline void mc_session_pause(struct mc_session *s) {
    if (s->owns_rx && !s->restore_fault) mc_pause(&s->scan);
}
static inline enum mc_action mc_session_resume(struct mc_session *s,
    uint32_t now, const struct mc_radio_ops *ops, void *ctx) {
    if (!s->owns_rx || s->restore_fault) return MC_IDLE;
    return mc_session_apply(s,mc_resume(&s->scan,now,ops->allowed,ctx),ops,ctx);
}
#endif
