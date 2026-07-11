// Standalone VIA 6522 tests for the Vectrex core. Compiles via6522.cpp alone.
// Two categories:
//   [fix]  datasheet fixes ported from the PET emulator's tier-2 VIA review
//   [lock] regression locks on existing (vecx-calibrated) behavior — these
//          freeze current timing on purpose; do NOT "correct" them.
#include <cstdio>
#include <cstdint>
#include "via6522.h"

// --- hook stubs (vecx.cpp provides these in the real build) -----------------
void via_hook_on_orb_written(uint8_t, uint8_t) {}
void via_hook_on_ora_written(uint8_t, uint8_t) {}
uint8_t via_hook_read_port_a(uint8_t, uint8_t ora) { return ora; }
uint8_t via_hook_get_compare_bit(void) { return 0; }

// --- tiny assert framework ---------------------------------------------------
static int g_fail = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if(!(cond)){ ++g_fail; \
    std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);} } while(0)
#define CHECK_EQ(a,b) do { ++g_checks; long _va=(long)(a), _vb=(long)(b); \
    if(_va!=_vb){ ++g_fail; std::printf("FAIL %s:%d  CHECK_EQ(%s,%s)  got %ld != %ld\n", \
    __FILE__, __LINE__, #a, #b, _va, _vb);} } while(0)

enum { R_ORB=0,R_ORA=1,R_DDRB=2,R_DDRA=3,R_T1LL=4,R_T1CH=5,R_T1LLB=6,R_T1LH=7,
       R_T2LL=8,R_T2CH=9,R_SR=10,R_ACR=11,R_PCR=12,R_IFR=13,R_IER=14,R_ORA_NHS=15 };

static void step(int n){ for(int i=0;i<n;i++){ via_sstep0(); via_sstep1(); } }
static void clr_ifr(void){ via_write_reg(R_IFR, 0x7F); }

// Run until the given IFR bit sets; returns step count or -1.
static int steps_to_flag(uint8_t mask, int maxc){
    for(int i=1;i<=maxc;i++){ via_sstep0(); via_sstep1();
        if(via_get_ifr()&mask) return i; }
    return -1;
}

// =============================== [lock] tests ================================

static void test_reset_defaults(){
    via_reset();
    CHECK_EQ(via_read_reg(R_ACR), 0x00);
    CHECK_EQ(via_read_reg(R_PCR), 0x00);
    CHECK_EQ(via_read_reg(R_IFR), 0x00);
    CHECK_EQ(via_read_reg(R_IER), 0x80);
    CHECK_EQ(via_get_ca2(), 1);
    CHECK_EQ(via_get_cb2h(), 1);
}

// vecx timing lock: T1 free-run steady-state period is N+1 (real chip is N+2;
// vector drawing is calibrated to this, so we freeze it).
static void test_t1_freerun_period_lock(){
    via_reset();
    const int N = 100;
    via_write_reg(R_ACR, 0x40);
    via_write_reg(R_T1LL, N & 0xFF);
    via_write_reg(R_T1CH, (N>>8) & 0xFF);      // arm
    int p1 = steps_to_flag(0x40, N+10); clr_ifr();
    int p2 = steps_to_flag(0x40, N+10); clr_ifr();
    int p3 = steps_to_flag(0x40, N+10);
    CHECK_EQ(p1, N+1);
    CHECK_EQ(p2, N+1);
    CHECK_EQ(p3, N+1);
}

static void test_t1_oneshot_fires_once_pb7(){
    via_reset();
    const int N = 60;
    via_write_reg(R_ACR, 0x80);                // one-shot, PB7 under T1
    via_write_reg(R_T1LL, N);
    via_write_reg(R_T1CH, 0);                  // arm: PB7 (/RAMP) goes low
    CHECK_EQ(via_get_t1pb7(), 0x00);
    CHECK(steps_to_flag(0x40, N+10) > 0);
    CHECK_EQ(via_get_t1pb7(), 0x80);           // timeout: PB7 back high
    clr_ifr();
    bool refired = false;                      // 64K wrap must not re-flag
    for(int i=0;i<70000;i++){ via_sstep0(); if(via_get_ifr()&0x40){refired=true;break;} }
    CHECK(!refired);
}

static void test_t2_oneshot_fires_once(){
    via_reset();
    const int N = 80;
    via_write_reg(R_T2LL, N);
    via_write_reg(R_T2CH, 0);                  // arm
    CHECK(steps_to_flag(0x20, N+10) > 0);
    clr_ifr();
    bool refired = false;
    for(int i=0;i<70000;i++){ via_sstep0(); if(via_get_ifr()&0x20){refired=true;break;} }
    CHECK(!refired);
}

static void test_counter_reads_clear_only_flag(){
    via_reset();
    via_write_reg(R_T1LL, 30); via_write_reg(R_T1CH, 0);
    CHECK(steps_to_flag(0x40, 40) > 0);
    (void)via_read_reg(R_T1LL);                // read reg4 = T1CL
    CHECK_EQ(via_get_ifr() & 0x40, 0);
    uint8_t a = via_read_reg(R_T1LL); step(5); // counter keeps running
    uint8_t b = via_read_reg(R_T1LL);
    CHECK(a != b);

    via_write_reg(R_T2LL, 30); via_write_reg(R_T2CH, 0);
    CHECK(steps_to_flag(0x20, 40) > 0);
    (void)via_read_reg(R_T2LL);                // read reg8 = T2CL
    CHECK_EQ(via_get_ifr() & 0x20, 0);
}

// SR mode 100 (ACR=$10): shift out free-running at T2 rate -> CB2 square wave
// with pattern $55. vecx timing lock: bit period = 2*(T2LL+1). Free run never
// sets IFR2.
static void test_sr_mode4_cb2_square_wave(){
    via_reset();
    const int N = 50;
    via_write_reg(R_ACR, 0x10);
    via_write_reg(R_T2LL, N);
    via_write_reg(R_SR, 0x55);
    int t_hi=-1, t_lo=-1, t_hi2=-1;
    for(int i=1;i<=8*(N+1) && t_hi2<0;i++){
        via_sstep0();
        int out = via_get_cb2_output();
        if(t_hi<0){ if(out==1) t_hi=i; }
        else if(t_lo<0){ if(out==0) t_lo=i; }
        else if(out==1) t_hi2=i;
    }
    CHECK(t_hi > 0);
    CHECK_EQ(t_lo - t_hi, 2*(N+1));
    CHECK_EQ(t_hi2 - t_lo, 2*(N+1));
    CHECK_EQ(via_get_ifr() & 0x04, 0);         // free run: no SR interrupt
}

static void test_cb2_input_edge_decode(){
    via_reset();
    via_write_reg(R_PCR, 0x00);                // CB2 mode 000: negative edge
    via_signal_cb2_input(1); clr_ifr();
    via_signal_cb2_input(0);
    CHECK_EQ(via_get_ifr() & 0x08, 0x08);
    clr_ifr();
    via_write_reg(R_PCR, 0x40);                // CB2 mode 010: positive edge
    via_signal_cb2_input(0); clr_ifr();
    via_signal_cb2_input(1);
    CHECK_EQ(via_get_ifr() & 0x08, 0x08);
}

static void test_ca2_handshake_restored_by_ca1(){
    via_reset();
    via_write_reg(R_PCR, 0x08);                // CA2 mode 100: handshake output
    CHECK_EQ(via_get_ca2(), 1);
    (void)via_read_reg(R_ORA);                 // ORA access drives CA2 low
    CHECK_EQ(via_get_ca2(), 0);
    via_signal_ca1_edge(1); clr_ifr();         // baseline high (inactive edge)
    via_signal_ca1_edge(0);                    // active neg edge restores CA2
    CHECK_EQ(via_get_ca2(), 1);
    CHECK_EQ(via_get_ifr() & 0x02, 0x02);      // and sets CA1 flag
}

static void test_cb2_handshake_restored_by_cb1(){
    via_reset();
    via_write_reg(R_PCR, 0x80);                // CB2 mode 100: handshake output
    CHECK_EQ(via_get_cb2h(), 1);
    via_write_reg(R_ORB, 0x00);                // ORB write drives CB2 low
    CHECK_EQ(via_get_cb2h(), 0);
    via_signal_cb1_edge(1); clr_ifr();
    via_signal_cb1_edge(0);                    // active neg edge restores CB2
    CHECK_EQ(via_get_cb2h(), 1);
    CHECK_EQ(via_get_ifr() & 0x10, 0x10);
}

static void test_ifr_ier_semantics(){
    via_reset();
    via_write_reg(R_IER, 0xC0);                // enable T1
    CHECK_EQ(via_read_reg(R_IER), 0xC0);       // reads back with bit7 set
    via_write_reg(R_T1LL, 20); via_write_reg(R_T1CH, 0);
    CHECK(steps_to_flag(0x40, 30) > 0);
    CHECK_EQ(via_irq_level(), 0x80);           // enabled flag -> IRQ + IFR7
    CHECK_EQ(via_read_reg(R_IFR) & 0xC0, 0xC0);
    via_write_reg(R_IFR, 0x40);                // write-1-to-clear
    CHECK_EQ(via_get_ifr(), 0x00);
    CHECK_EQ(via_irq_level(), 0x00);
    via_write_reg(R_IER, 0x40);                // bit7=0: clear enable
    CHECK_EQ(via_read_reg(R_IER), 0x80);
}

// =============================== [fix] tests =================================

// Fix 1: writing T1L-H acks the T1 interrupt (re-program-from-IRQ idiom).
static void test_t1lh_write_clears_ifr6(){
    via_reset();
    via_write_reg(R_ACR, 0x40);
    via_write_reg(R_T1LL, 40); via_write_reg(R_T1CH, 0);
    CHECK(steps_to_flag(0x40, 50) > 0);
    via_write_reg(R_T1LH, 0x01);
    CHECK_EQ(via_get_ifr() & 0x40, 0);
}

// Fix 2: ORB WRITE clears CB1 always, CB2 unless independent input mode.
static void test_orb_write_clears_cb_flags(){
    via_reset();
    via_write_reg(R_PCR, 0x00);
    via_signal_cb1_edge(1); via_signal_cb2_input(1); clr_ifr();
    via_signal_cb1_edge(0); via_signal_cb2_input(0);
    CHECK_EQ(via_get_ifr() & 0x18, 0x18);      // CB1+CB2 flags set
    via_write_reg(R_ORB, 0x00);
    CHECK_EQ(via_get_ifr() & 0x18, 0x00);      // both cleared by the write

    via_write_reg(R_PCR, 0x20);                // CB2 mode 001: independent
    via_signal_cb1_edge(1); via_signal_cb2_input(1); clr_ifr();
    via_signal_cb1_edge(0); via_signal_cb2_input(0);
    via_write_reg(R_ORB, 0x00);
    CHECK_EQ(via_get_ifr() & 0x08, 0x08);      // CB2 flag survives
    CHECK_EQ(via_get_ifr() & 0x10, 0x00);      // CB1 flag cleared
}

// Fix 2: ORA WRITE clears CA1 always, CA2 unless independent input mode.
static void test_ora_write_clears_ca_flags(){
    via_reset();
    via_write_reg(R_PCR, 0x00);
    via_signal_ca1_edge(1); via_signal_ca2_input(1); clr_ifr();
    via_signal_ca1_edge(0); via_signal_ca2_input(0);
    CHECK_EQ(via_get_ifr() & 0x03, 0x03);      // CA1+CA2 flags set
    via_write_reg(R_ORA, 0x00);
    CHECK_EQ(via_get_ifr() & 0x03, 0x00);

    via_write_reg(R_PCR, 0x02);                // CA2 mode 001: independent
    via_signal_ca1_edge(1); via_signal_ca2_input(1); clr_ifr();
    via_signal_ca1_edge(0); via_signal_ca2_input(0);
    via_write_reg(R_ORA, 0x00);
    CHECK_EQ(via_get_ifr() & 0x01, 0x01);      // CA2 flag survives
    CHECK_EQ(via_get_ifr() & 0x02, 0x00);      // CA1 flag cleared
}

// Fix 2: register $F (ORA no-handshake) performs NO flag clears, read or write.
static void test_regF_no_flag_clears(){
    via_reset();
    via_write_reg(R_PCR, 0x00);
    via_signal_ca1_edge(1); via_signal_ca2_input(1); clr_ifr();
    via_signal_ca1_edge(0); via_signal_ca2_input(0);
    CHECK_EQ(via_get_ifr() & 0x03, 0x03);
    (void)via_read_reg(R_ORA_NHS);
    CHECK_EQ(via_get_ifr() & 0x03, 0x03);      // read $F: flags untouched
    via_write_reg(R_ORA_NHS, 0x12);
    CHECK_EQ(via_get_ifr() & 0x03, 0x03);      // write $F: flags untouched
    CHECK_EQ(via_get_ora(), 0x12);             // ...but the latch updates
}

// Fix 3: CA1 edge polarity is PCR bit 0 (not bit 1).
static void test_ca1_polarity_pcr_bit0(){
    via_reset();
    via_write_reg(R_PCR, 0x00);                // negative edge
    via_signal_ca1_edge(1); clr_ifr();
    via_signal_ca1_edge(0);
    CHECK_EQ(via_get_ifr() & 0x02, 0x02);
    via_write_reg(R_PCR, 0x01);                // positive edge
    via_signal_ca1_edge(0); clr_ifr();
    via_signal_ca1_edge(1);
    CHECK_EQ(via_get_ifr() & 0x02, 0x02);
}

// Fix 3: CB1 edge polarity is PCR bit 4 (not bit 5).
static void test_cb1_polarity_pcr_bit4(){
    via_reset();
    via_write_reg(R_PCR, 0x00);                // negative edge
    via_signal_cb1_edge(1); clr_ifr();
    via_signal_cb1_edge(0);
    CHECK_EQ(via_get_ifr() & 0x10, 0x10);
    via_write_reg(R_PCR, 0x10);                // positive edge
    via_signal_cb1_edge(0); clr_ifr();
    via_signal_cb1_edge(1);
    CHECK_EQ(via_get_ifr() & 0x10, 0x10);
}

int main(){
    test_reset_defaults();
    test_t1_freerun_period_lock();
    test_t1_oneshot_fires_once_pb7();
    test_t2_oneshot_fires_once();
    test_counter_reads_clear_only_flag();
    test_sr_mode4_cb2_square_wave();
    test_cb2_input_edge_decode();
    test_ca2_handshake_restored_by_ca1();
    test_cb2_handshake_restored_by_cb1();
    test_ifr_ier_semantics();
    test_t1lh_write_clears_ifr6();
    test_orb_write_clears_cb_flags();
    test_ora_write_clears_ca_flags();
    test_regF_no_flag_clears();
    test_ca1_polarity_pcr_bit0();
    test_cb1_polarity_pcr_bit4();
    std::printf("%s: %d checks, %d failures\n",
        g_fail ? "TESTS FAILED" : "ALL TESTS PASSED", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
