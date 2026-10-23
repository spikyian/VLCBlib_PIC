/* CAN bus simulator and the driver tests.
 * Loads one copy of a node object per node (dlopen of a private copy, so each
 * has its own driver statics and registers), steps them, and moves frames
 * between them with CAN arbitration (lowest identifier, data before RTR). */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include "bus.h"

#define MAXN 12
#define TICKS_PER_STEP 60           /* ~1 ms: about one frame time at 125 kbit/s */
#define ONE_SEC 62500u

typedef struct Msg { uint8_t opc, len, d[7]; } Msg;
typedef struct Node {
    void *h; NodeApi a; char variant[48];
    int app_every;                  /* application takes received messages every n steps */
    int app_burst;                  /* ... up to this many at a time */
    Msg log[4096]; int nlog;
    volatile uint32_t clock;        /* each node's own tick: oscillators differ */
} Node;

static Node nodes[MAXN];
static int nn_;
static volatile uint32_t ticks;
static uint32_t step_no;
static BusFrame trace[20000]; static int ntrace; static int trace_sender[20000];
static const char *bdir = "build";   /* run.sh passes its build directory */

/* scripted responder: answers every RTR with zero-length frames from these CANIDs */
static uint8_t responder_ids[128]; static int nresponder;
static BusFrame inject[512]; static int ninject;

static int fails, checks;
static char tname[128];
static void check(int cond, const char *fmt, ...) {
    checks++;
    if (cond) return;
    fails++;
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "  FAIL [%s] ", tname); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n");
    va_end(ap);
}

#define SYM(f) do { n->a.f = dlsym(n->h, "api_" #f); if (!n->a.f) { fprintf(stderr, "no api_%s\n", #f); exit(2); } } while (0)
static int add_node(const char *variant, int nvm_canid) {
    Node *n = &nodes[nn_];
    char src[256], dst[256], buf[65536];
    snprintf(src, sizeof src, "%s/%s.so", bdir, variant);
    snprintf(dst, sizeof dst, "/tmp/cannode_%d_%d.so", getpid(), nn_);
    FILE *in = fopen(src, "rb"), *out = fopen(dst, "wb");
    if (!in || !out) { fprintf(stderr, "cannot copy %s\n", src); exit(2); }
    size_t k; while ((k = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, k, out);
    fclose(in); fclose(out);
    memset(n, 0, sizeof *n);
    n->h = dlopen(dst, RTLD_NOW | RTLD_LOCAL);
    unlink(dst);
    if (!n->h) { fprintf(stderr, "%s\n", dlerror()); exit(2); }
    strncpy(n->variant, variant, sizeof n->variant - 1);
    SYM(set_clock); SYM(factory); SYM(powerup); SYM(poll); SYM(send); SYM(recv); SYM(isr);
    SYM(irq_pending); SYM(diag); SYM(canid); SYM(nvm_canid); SYM(set_nvm_canid); SYM(peek);
    SYM(result); SYM(rx); SYM(faults); SYM(rx_count); SYM(tx_count); SYM(drain); SYM(esd);
    n->clock = ticks;
    n->a.set_clock(&n->clock);
    n->a.set_nvm_canid((uint8_t)nvm_canid);
    n->app_every = 1; n->app_burst = 4;
    return nn_++;
}

static void reset_bus(void) {
    for (int i = 0; i < nn_; i++) dlclose(nodes[i].h);
    nn_ = 0; ticks = 1000; step_no = 0; ntrace = 0; nresponder = 0; ninject = 0;
}

static int is_k80(int i) { return strncmp(nodes[i].variant, "k80", 3) == 0; }

/* one step: every node's main loop and interrupts, then one frame on the bus */
static void step(void) {
    step_no++;
    ticks += TICKS_PER_STEP;
    for (int i = 0; i < nn_; i++) nodes[i].clock += TICKS_PER_STEP + (uint32_t)((i * 7) % 5);   /* up to ~7% apart */
    for (int i = 0; i < nn_; i++) {
        Node *n = &nodes[i];
        if (is_k80(i) || n->a.irq_pending()) n->a.isr();     /* K80: canIsr also runs on timer interrupts */
        n->a.poll();
        if (step_no % n->app_every == 0) {
            Msg m;
            for (int b = 0; b < n->app_burst && n->a.recv(&m.opc, &m.len, m.d); b++)
                if (n->nlog < 4096) n->log[n->nlog++] = m;
        }
        if (is_k80(i) || n->a.irq_pending()) n->a.isr();
    }
    /* bus: the injected frames first (a responder), then arbitration */
    BusFrame f; int win = -1; memset(&f, 0, sizeof f);
    if (ninject) {
        f = inject[0]; memmove(inject, inject + 1, sizeof inject[0] * --ninject);
    } else {
        uint32_t best = 0xFFFFFFFF;
        BusFrame c; memset(&c, 0, sizeof c);
        for (int i = 0; i < nn_; i++) {
            if (!nodes[i].a.peek(&c)) continue;
            uint32_t key = ((uint32_t)c.id << 1) | c.rtr;
            if (key < best) { best = key; win = i; f = c; }
        }
        if (win < 0) return;
        for (int i = 0; i < nn_; i++) {
            if (!nodes[i].a.peek(&c)) continue;
            nodes[i].a.result(i == win);
        }
    }
    if (ntrace < 20000) { trace[ntrace] = f; trace_sender[ntrace] = win; ntrace++; }
    for (int i = 0; i < nn_; i++) if (i != win) nodes[i].a.rx(&f);
    if (f.rtr && nresponder) {
        for (int k = 0; k < nresponder && ninject < 512; k++) {
            BusFrame r = { .id = (uint16_t)(0x780 | responder_ids[k]), .rtr = 0, .ext = 0, .dlc = 0 };
            /* (an RTR reply's priority bits are 0; keep the CANID in bits 0-6) */
            r.id = responder_ids[k];
            inject[ninject++] = r;
        }
    }
}
static void run(int steps) { while (steps--) step(); }

static uint8_t frame_canid(const BusFrame *f) { return f->id & 0x7F; }

static void powerup_all(void) { for (int i = 0; i < nn_; i++) nodes[i].a.powerup(); }

static int send(int i, uint8_t opc, int len, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    uint8_t d[7] = { b0, b1, b2, b3, 0, 0, 0 };
    return nodes[i].a.send(opc, (uint8_t)len, d);
}

static int unique_canids(void) {
    for (int i = 0; i < nn_; i++) for (int j = i + 1; j < nn_; j++)
        if (nodes[i].a.canid() == nodes[j].a.canid()) return 0;
    return 1;
}

/* ===================================================================== */
#define OPC_ACON 0x90
#define OPC_ASON 0x98
#define OPC_RQNP 0x10
#define OPC_ENUM 0x5D
#define OPC_CANID 0x75
#define DIAG_TX_MESSAGES 6
#define DIAG_RX_MESSAGES 9
#define DIAG_RX_OVERRUN 8
#define DIAG_CANID_ENUMS 0x0D
#define DIAG_CANID_CONFLICTS 0x0E
#define DIAG_CANID_CHANGES 0x0F
#define DIAG_CANID_ENUMS_FAIL 0x10

static int any_canid0_data(void) {
    for (int k = 0; k < ntrace; k++) if (!trace[k].rtr && trace[k].dlc && frame_canid(&trace[k]) == 0) return 1;
    return 0;
}

/* T1: a frame goes from one node to another unchanged */
static void t_basic(const char *va, const char *vb) {
    reset_bus();
    int a = add_node(va, 5), b = add_node(vb, 6);
    powerup_all(); run(5);
    check(send(a, OPC_ACON, 5, 0x01, 0x02, 0x00, 0x07), "send");
    run(10);
    check(nodes[b].nlog == 1, "b received %d", nodes[b].nlog);
    if (nodes[b].nlog) {
        Msg *m = &nodes[b].log[0];
        check(m->opc == OPC_ACON && m->len == 5 && m->d[0] == 1 && m->d[1] == 2 && m->d[3] == 7, "content opc %02x len %d", m->opc, m->len);
    }
    int seen = 0;
    for (int k = 0; k < ntrace; k++) if (!trace[k].rtr && trace[k].dlc) { seen++;
        check(frame_canid(&trace[k]) == 5, "CANID on bus %d", frame_canid(&trace[k]));
        check(((trace[k].id >> 7) & 0xF) == 0xA, "priority bits %x (pNORMAL: MjPri 2, MinPri 2)", (trace[k].id >> 7) & 0xF); }
    check(seen == 1, "%d data frames on bus", seen);
    check(nodes[a].a.diag(DIAG_TX_MESSAGES) >= 1, "TX_MESSAGES");
    check(nodes[b].a.diag(DIAG_RX_MESSAGES) >= 1, "RX_MESSAGES");
    check(nodes[a].a.faults() == 0 && nodes[b].a.faults() == 0, "model faults");
}

/* T2: factory-reset nodes all get distinct CANIDs once they have all talked
 * (two nodes enumerating at once can pick the same CANID; the conflict is
 * found when one of them next transmits) */
static void t_enumerate2(const char *v, const char *v2, int count, int chk) {
    reset_bus();
    for (int i = 0; i < count; i++) add_node((i & 1) ? v2 : v, 0);
    powerup_all(); run(3);
    for (int i = 0; i < count; i++) { check(send(i, OPC_ACON, 5, 0, (uint8_t)i, 0, 1), "send %d", i); run(3 + i * 7); }
    run(1000);
    /* one node at a time, each long enough for a conflict it reveals to settle
     * (two nodes that see each other's conflict at the same moment re-enumerate
     * in step and can swap CANIDs indefinitely - a property of the protocol,
     * the same in every driver, not exercised here) */
    for (int round = 0; round < 3; round++)
        for (int i = 0; i < count; i++) { send(i, OPC_ACON, 5, 0, (uint8_t)i, 0, 2); run(500); }
    { char b[128]; int o = 0; for (int i = 0; i < count; i++) o += snprintf(b + o, sizeof b - o, " %d(e%d c%d)", nodes[i].a.canid(), nodes[i].a.diag(DIAG_CANID_ENUMS), nodes[i].a.diag(DIAG_CANID_CONFLICTS));
      check(unique_canids(), "CANIDs not unique:%s", b); }
    for (int i = 0; i < count; i++) {
        uint8_t c = nodes[i].a.canid();
        check(c >= 1 && c <= 99, "node %d CANID %d", i, c);
        check(nodes[i].a.nvm_canid() == c, "node %d NVM %d != %d", i, nodes[i].a.nvm_canid(), c);
    }
    /* every node got every other node's first frame */
    for (int i = 0; i < count; i++) {
        int got = 0;
        for (int k = 0; k < nodes[i].nlog; k++) if (nodes[i].log[k].opc == OPC_ACON && nodes[i].log[k].d[3] == 1) got++;
        check(got == count - 1, "node %d received %d of %d", i, got, count - 1);
    }
    if (chk) check(!any_canid0_data(), "a data frame was sent with CANID 0");
}

static void t_enumerate(const char *v, int count, int chk) { t_enumerate2(v, v, count, chk); }

/* T3: two nodes with the same CANID resolve the conflict */
static void t_conflict(const char *va, const char *vb) {
    reset_bus();
    int a = add_node(va, 7), b = add_node(vb, 7);
    powerup_all(); run(3);
    send(a, OPC_ACON, 5, 0, 1, 0, 1);
    run(2000);
    check(nodes[a].a.canid() != nodes[b].a.canid(), "still both %d", nodes[a].a.canid());
    check(nodes[b].a.diag(DIAG_CANID_CONFLICTS) >= 1, "b conflicts %d", nodes[b].a.diag(DIAG_CANID_CONFLICTS));
}

/* T4: a node under load still answers RTRs: another node enumerating must not take its CANID */
static int t_rtr_under_load(const char *vtarget, const char *vflood, const char *vnew) {
    reset_bus();
    int t = add_node(vtarget, 1);           /* holds CANID 1 */
    int flood = add_node(vflood, 50);
    int newbie = add_node(vnew, 0);         /* holds its frame until it has a CANID */
    nodes[t].app_every = 2; nodes[t].app_burst = 1;   /* busy application: frames pile up, but do not overflow */
    powerup_all(); run(3);
    for (int b = 0; b < 30; b++) {
        for (int k = 0; k < 8; k++) { send(flood, OPC_ASON, 5, 0, 1, (uint8_t)b, (uint8_t)k); if (b == 15 && k == 2) send(newbie, OPC_ACON, 5, 0, 9, 0, 1); run(1); }
        run(14);
    }
    run(3000);
    int rtr_replies_from_t = 0;
    for (int k = 0; k < ntrace; k++) if (trace_sender[k] == t && !trace[k].rtr && trace[k].dlc == 0) rtr_replies_from_t++;
    check(nodes[newbie].a.canid() != 1, "new node took CANID 1, held by a busy node (its RTR replies: %d)", rtr_replies_from_t);
    check(nodes[t].a.diag(DIAG_RX_OVERRUN) == 0, "test set-up: the busy node overran (%d)", nodes[t].a.diag(DIAG_RX_OVERRUN));
    return nodes[newbie].a.canid() != 1;
}

/* T5: the reply to an RTR carries the replier's whole CANID */
static void t_rtr_reply_canid(const char *v, int canid) {
    reset_bus();
    int r = add_node(v, canid);
    int e = add_node(v, 0);
    powerup_all(); run(3);
    send(e, OPC_ACON, 5, 0, 1, 0, 1);       /* e enumerates */
    run(600);
    int found = 0, wrong = -1;
    for (int k = 0; k < ntrace; k++) if (trace_sender[k] == r && !trace[k].rtr && trace[k].dlc == 0) {
        if (frame_canid(&trace[k]) == canid) found = 1; else wrong = frame_canid(&trace[k]);
    }
    check(found, "RTR reply from CANID %d carried %d", canid, wrong);
}

/* T6: a received RTR is not reported as a message (K80 legacy returned an uninitialised result) */
static void t_rtr_not_message(const char *v) {
    reset_bus();
    int a = add_node(v, 3), b = add_node(v, 4);
    nodes[b].app_every = 1000000;          /* b's application does not read during the run */
    powerup_all(); run(3);
    BusFrame rtr = { .id = 9, .rtr = 1, .dlc = 0 };
    nodes[b].a.rx(&rtr);
    uint8_t opc, len, d[7];
    int got = nodes[b].a.recv(&opc, &len, d);
    check(!got, "RTR reported as a received message (len %d opc %02x)", len, opc);
    (void)a;
}

/* T6b: zero-length frames (RTR replies) are never reported as messages, however
 * they are buffered (the K80 interrupt path moved them into the message queue) */
static void t_zero_len_not_message(const char *v) {
    reset_bus();
    int a = add_node(v, 3);
    nodes[a].app_every = 40; nodes[a].app_burst = 50;       /* frames pile up between reads */
    powerup_all(); run(3);
    for (int k = 0; k < 12; k++) {
        BusFrame z; memset(&z, 0, sizeof z); z.id = (uint16_t)(20 + k); z.dlc = 0;
        inject[ninject++] = z;
    }
    run(200);
    check(nodes[a].nlog == 0, "%d zero-length frames reported as messages", nodes[a].nlog);
}

/* T7: with CAN_ADDITIONAL_CANID_CHECKS frames wait for a CANID and go in order */
static void t_hold(const char *v) {
    reset_bus();
    int a = add_node(v, 0), b = add_node(v, 9);
    powerup_all(); run(3);
    for (int k = 0; k < 6; k++) check(send(a, OPC_ACON, 5, 0, 1, 0, (uint8_t)k), "send %d", k);
    run(1500);
    check(!any_canid0_data(), "data frame with CANID 0");
    check(nodes[b].nlog == 6, "b got %d of 6", nodes[b].nlog);
    for (int k = 0; k < nodes[b].nlog && k < 6; k++) check(nodes[b].log[k].d[3] == k, "order: #%d is %d", k, nodes[b].log[k].d[3]);
    check(nodes[a].a.canid() != 0 && nodes[a].a.canid() != 9, "a CANID %d", nodes[a].a.canid());
}

/* T8: no free CANID: frames stay held, retry after ENUMERATION_RETRY, then go */
static void t_backoff(const char *v) {
    reset_bus();
    int a = add_node(v, 0), b = add_node(v, 100);   /* b: an out-of-range CANID, never offered */
    for (int k = 1; k <= 99; k++) responder_ids[nresponder++] = (uint8_t)k;
    powerup_all(); run(3);
    send(a, OPC_ACON, 5, 0, 1, 0, 1);
    run(1000);                                      /* < 5 s: first enumeration failed */
    check(nodes[a].a.canid() == 0, "a took CANID %d with none free", nodes[a].a.canid());
    check(nodes[a].a.diag(DIAG_CANID_ENUMS_FAIL) >= 1, "ENUMS_FAIL %d", nodes[a].a.diag(DIAG_CANID_ENUMS_FAIL));
    int enums1 = nodes[a].a.diag(DIAG_CANID_ENUMS);
    run(1000);
    check(nodes[a].a.diag(DIAG_CANID_ENUMS) == enums1, "re-enumerated before ENUMERATION_RETRY");
    check(!any_canid0_data(), "data frame with CANID 0");
    nresponder = 30;                                /* CANIDs 31.. free now */
    run(5000);
    check(nodes[a].a.canid() == 31, "a CANID %d after retry (expect 31)", nodes[a].a.canid());
    check(nodes[b].nlog == 1, "b got %d", nodes[b].nlog);
    check(!any_canid0_data(), "data frame with CANID 0");
}

/* T9: CONSUMED_EVENTS: the node receives the events it sends */
static void t_coe(const char *v) {
    reset_bus();
    int a = add_node(v, 5); add_node(v, 6);
    powerup_all(); run(3);
    send(a, OPC_ACON, 5, 0, 1, 0, 42);
    run(10);
    int own = 0; for (int k = 0; k < nodes[a].nlog; k++) if (nodes[a].log[k].opc == OPC_ACON && nodes[a].log[k].d[3] == 42) own++;
    check(own == 1, "own event received %d times", own);
}

/* T10: a flood is delivered in order; nothing lost without an overrun being counted */
static void t_flood(const char *vrx, const char *vtx, int every, int burst) {
    reset_bus();
    int r = add_node(vrx, 5), s = add_node(vtx, 6);
    nodes[r].app_every = every; nodes[r].app_burst = burst;
    powerup_all(); run(3);
    int sent = 0;
    for (int k = 0; k < 600; k++) { if (send(s, OPC_ASON, 5, 0, 1, (uint8_t)(k >> 8), (uint8_t)k)) sent++; run(1); }
    run(3000);
    int last = -1, inorder = 1;
    for (int k = 0; k < nodes[r].nlog; k++) { int v = nodes[r].log[k].d[2] << 8 | nodes[r].log[k].d[3]; if (v <= last) inorder = 0; last = v; }
    check(inorder, "out of order");
    int lost = sent - nodes[r].nlog;
    check(lost == 0 || nodes[r].a.diag(DIAG_RX_OVERRUN) > 0, "%d lost, no overrun counted", lost);
    check(nodes[r].a.faults() == 0 && nodes[s].a.faults() == 0, "model faults");
}

/* T12: the same script on legacy and shared drivers puts the same frames on the bus */
static int trace_script(const char *v, BusFrame *out, int *nout, Msg *rx, int *nrx) {
    reset_bus();
    int a = add_node(v, 0), b = add_node(v, 12), c = add_node(v, 12);
    powerup_all(); run(3);
    send(b, OPC_ACON, 5, 0, 1, 0, 1); run(5);
    send(a, OPC_ASON, 5, 0, 2, 0, 2); run(400);
    for (int k = 0; k < 20; k++) { send(c, OPC_ACON, 5, 0, 3, 0, (uint8_t)k); send(a, OPC_RQNP, 1, 0, 0, 0, 0); run(2); }
    run(2000);
    memcpy(out, trace, sizeof trace[0] * ntrace); *nout = ntrace;
    memcpy(rx, nodes[b].log, sizeof(Msg) * nodes[b].nlog); *nrx = nodes[b].nlog;
    return 0; (void)c;
}
static BusFrame tr1[20000], tr2[20000]; static Msg rx1[4096], rx2[4096];
static void t_equivalent(const char *vl, const char *vs, int expect_same) {
    int n1, n2, m1, m2;
    trace_script(vl, tr1, &n1, rx1, &m1);
    trace_script(vs, tr2, &n2, rx2, &m2);
    int same = n1 == n2 && memcmp(tr1, tr2, sizeof tr1[0] * n1) == 0 && m1 == m2 && memcmp(rx1, rx2, sizeof(Msg) * m1) == 0;
    if (expect_same) check(same, "bus traces differ: %d vs %d frames, rx %d vs %d", n1, n2, m1, m2);
    else printf("    (%s vs %s: %s, %d vs %d frames)\n", vl, vs, same ? "same" : "differ", n1, n2);
}

/* T12b: a full transmit buffer refuses the frame and counts it */
static int t_txfull(const char *v) {
    reset_bus();
    int a = add_node(v, 5);
    powerup_all();
    int ok = 0;
    for (int k = 0; k < 40; k++) ok += send(a, OPC_ACON, 5, 0, 1, 0, (uint8_t)k);
    check(nodes[a].a.diag(5) == (uint16_t)(40 - ok), "TX_BUFFER_OVERRUN %d, refused %d", nodes[a].a.diag(5), 40 - ok);
    return ok;
}

/* T13: ESD and service basics */
static void t_esd(const char *v, int expect) {
    reset_bus();
    int a = add_node(v, 5);
    powerup_all();
    check(nodes[a].a.esd(1) == expect, "ESD type %d", nodes[a].a.esd(1));
    check(nodes[a].a.diag(0) == 18, "diag count %d", nodes[a].a.diag(0));
    check(nodes[a].a.diag(19) == 0xFFFF, "diag 19 not NULL");
}

/* T14: the peripheral is set up the same: register state after power-up */
static int regs_of(const char *v, uint8_t *buf) {
    reset_bus();
    int a = add_node(v, 45);
    powerup_all();
    int (*f)(uint8_t *, int) = (int (*)(uint8_t *, int))dlsym(nodes[a].h, "api_regs");
    return f(buf, 4096);
}
static void t_regs(const char *vl, const char *vs, const int *allowed, int nallowed) {
    static uint8_t a[4096], b[4096];
    int n = regs_of(vl, a), m = regs_of(vs, b);
    check(n == m, "sizes");
    for (int i = 0; i < n; i++) {
        if (a[i] == b[i]) continue;
        int ok = 0; for (int k = 0; k < nallowed; k++) ok |= allowed[k] == i;
        check(ok, "register byte %d: %02x (existing) vs %02x (shared)", i, a[i], b[i]);
    }
}

#define T(name, call) do { snprintf(tname, sizeof tname, "%s", name); int f0 = fails; call; printf("%-62s %s\n", tname, fails == f0 ? "ok" : "FAILED"); } while (0)
/* a test keithb's driver is expected to fail: shows the defect; counts as a failure only if it passes */
static int xfails;
#define TX(name, why, call) do { snprintf(tname, sizeof tname, "%s", name); int f0 = fails, c0 = checks; \
    FILE *keep = stderr; stderr = fopen("/dev/null", "w"); call; fclose(stderr); stderr = keep; \
    int failed = fails != f0; fails = f0; (void)c0; \
    if (failed) { xfails++; printf("%-62s existing driver fails, as expected: %s\n", tname, why); } \
    else { fails++; printf("%-62s PASSED - defect not reproduced: %s\n", tname, why); } } while (0)

int main(int argc, char **argv) {
    if (argc > 1) bdir = argv[1];
    const char *fam[] = { "q83", "k80" };
    for (int fi = 0; fi < 2; fi++) {
        const char *F = fam[fi];
        int k80 = fi == 1;
        char v[64], w[64], x[64], y[64], n[128], sc[64], s0[64];
        printf("== %s\n", F);
        snprintf(sc, 64, "%s_split_chk", F); snprintf(s0, 64, "%s_split", F);
        for (int legacy = 0; legacy < 2; legacy++) {
            const char *M = legacy ? "legacy" : "split";
            int kl = k80 && legacy;     /* keithb's K80 driver */
            snprintf(v, 64, "%s_%s", F, M); snprintf(w, 64, "%s_%s_chk", F, M);
            snprintf(x, 64, "%s_%s_coe", F, M); snprintf(y, 64, "%s_%s_chk_coe", F, M);
            snprintf(n, 128, "%s basic", v); T(n, t_basic(v, v));
            snprintf(n, 128, "%s enumerate 6 nodes", v); T(n, t_enumerate(v, 6, 0));
            snprintf(n, 128, "%s enumerate 6 nodes", w);
            T(n, t_enumerate(w, 6, !kl));     /* keithb K80 ignores CAN_ADDITIONAL_CANID_CHECKS */
            snprintf(n, 128, "%s CANID conflict", v); T(n, t_conflict(v, v));
            snprintf(n, 128, "%s RTR reply carries CANID 45", v);
            if (kl) TX(n, "TXB2SIDL copied from TXB0SIDL", t_rtr_reply_canid(v, 45)); else T(n, t_rtr_reply_canid(v, 45));
            snprintf(n, 128, "%s received RTR is not a message", v);
            if (kl) TX(n, "messageAvailable returned unset", t_rtr_not_message(v)); else T(n, t_rtr_not_message(v));
            snprintf(n, 128, "%s RTR replies are not messages under load", v);
            if (kl) TX(n, "interrupt queues zero-length frames as messages", t_zero_len_not_message(v)); else T(n, t_zero_len_not_message(v));
            snprintf(n, 128, "%s answers RTRs under load", v);
            if (kl) TX(n, "ISR path skips handleSelfEnumeration", t_rtr_under_load(v, s0, sc)); else T(n, t_rtr_under_load(v, s0, sc));
            snprintf(n, 128, "%s hold until CANID", w);
            if (kl) TX(n, "no CAN_ADDITIONAL_CANID_CHECKS on K80", t_hold(w)); else T(n, t_hold(w));
            snprintf(n, 128, "%s no free CANID: back off, retry", w);
            if (kl) TX(n, "no CAN_ADDITIONAL_CANID_CHECKS on K80", t_backoff(w)); else T(n, t_backoff(w));
            snprintf(n, 128, "%s consume own events", x); T(n, t_coe(x));
            snprintf(n, 128, "%s consume own events", y); T(n, t_coe(y));
            snprintf(n, 128, "%s flood, fast application", v); T(n, t_flood(v, s0, 1, 4));
            snprintf(n, 128, "%s flood, slow application", v); T(n, t_flood(v, s0, 4, 1));
            snprintf(n, 128, "%s ESD and diagnostics", v); T(n, t_esd(v, fi == 0 ? 2 : 1));
        }
        snprintf(v, 64, "%s_legacy", F); snprintf(w, 64, "%s_split", F);
        snprintf(n, 128, k80 ? "%s existing and shared: bus traces (they differ by the fixes)" : "%s existing and shared: identical bus traces", F);
        if (k80) T(n, t_equivalent(v, w, 0)); else T(n, t_equivalent(v, w, 1));
        snprintf(v, 64, "%s_legacy_chk", F); snprintf(w, 64, "%s_split_chk", F);
        snprintf(n, 128, k80 ? "%s _chk existing and shared: bus traces (they differ by the fixes)" : "%s _chk existing and shared: identical bus traces", F);
        if (k80) T(n, t_equivalent(v, w, 0)); else T(n, t_equivalent(v, w, 1));
        { snprintf(v, 64, "%s_legacy", F); snprintf(w, 64, "%s_split", F);
          int a1 = 0, a2 = 0;
          snprintf(n, 128, "%s full TX buffer: same capacity as legacy", F);
          T(n, a1 = t_txfull(v); a2 = t_txfull(w); check(a1 == a2, "accepted %d legacy, %d split", a1, a2));
          printf("    (%d frames accepted before the buffer is full)\n", a2); }
        { static const int k80_sidl[] = { 8 * 14 + 14 + 2, 8 * 14 + 28 + 2 };   /* TXB1SIDL, TXB2SIDL: all 7 CANID bits */
          snprintf(v, 64, "%s_legacy_chk_coe", F); snprintf(w, 64, "%s_split_chk_coe", F);
          snprintf(n, 128, k80 ? "%s registers after power-up: same but TXB1SIDL, TXB2SIDL" : "%s registers after power-up: identical", F);
          T(n, t_regs(v, w, k80_sidl, k80 ? 2 : 0)); }
        snprintf(v, 64, "%s_legacy", F); snprintf(w, 64, "%s_split", F);
        snprintf(n, 128, "%s mixed bus: legacy and split nodes", F); T(n, t_basic(v, w); t_basic(w, v));
    }
    printf("== mixed families\n");
    T("q83_split <-> k80_split", t_basic("q83_split", "k80_split"); t_basic("k80_split", "q83_split"));
    T("q83_split_chk and k80_split_chk: CANID conflict", t_conflict("q83_split_chk", "k80_split_chk"));
    T("q83_split_chk and k80_split_chk: enumerate 6 nodes", t_enumerate2("q83_split_chk", "k80_split_chk", 6, 1));
    printf("\n%d checks, %d failed; %d defects of the existing K80 driver reproduced\n", checks, fails, xfails);
    return fails ? 1 : 0;
}
