/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#include "b-em.h"
#include "common.h"
#include "cpu/cpu.h"
#include "6502.h"
#include "model.h"
#include "mem.h"
#include "6502debug.h"

#if PICO_BUILD
#include "hardware/gpio.h"
CU_REGISTER_DEBUG_PINS(core_use)
//CU_SELECT_DEBUG_PINS(core_use)
#endif

#ifndef NO_USE_TUBE
#error no tube support
#endif

#if PI_ASM32
void *pi_mem_handler_base;
#endif

#if true //ndef PICO_ON_DEVICE
C6502 g_cpu;
#else
__attribute__((section(".special.cpu"))) C6502 g_cpu;
#endif

#ifndef NO_USE_DEBUGGER
static int dbg_core6502 = 0;

static int dbg_debug_enable(int newvalue) {
    int oldvalue = dbg_core6502;
    dbg_core6502 = newvalue;
    return oldvalue;
};

uint16_t pc3, oldpc, oldoldpc;
static uint32_t dbg_reg_get(int which) {
    switch (which)
    {
        case REG_A:
            return get_a();
        case REG_X:
            return get_x();
        case REG_Y:
            return get_y();
        case REG_S:
            return g_cpu.sp;
        case REG_P:
            return g_cpu.status;
        case REG_PC:
            return get_pc();
    }
    return 0;
}

static void dbg_reg_set(int which, uint32_t value) {
    switch (which)
    {
        case REG_A:
            set_a(value);
            break;
        case REG_X:
            set_x(value);
            break;
        case REG_Y:
            set_y(value);
            break;
        case REG_S:
            g_cpu.sp = value;
            break;
        case REG_P:
            g_cpu.status = value;
            break;
        case REG_PC:
            g_cpu.pc = value;
            break;
    }
}

static size_t dbg_reg_print(int which, char *buf, size_t bufsize) {
    switch (which)
    {
        case REG_P: {
            PREG p;
            p.c = FLG_ISSET(g_cpu, c);
            p.d = FLG_ISSET(g_cpu, d);
            p.v = FLG_ISSET(g_cpu, v);
            p.n = FLG_ISSET(g_cpu, n);
            p.z = FLG_ISSET(g_cpu, z);
            p.i = FLG_ISSET(g_cpu, i);
            return dbg6502_print_flags(&p, buf, bufsize);
        }
        case REG_PC:
            return snprintf(buf, bufsize, "%04X", g_cpu.pc);
            break;
        default:
            return snprintf(buf, bufsize, "%02X", dbg_reg_get(which));
    }
}

static void dbg_reg_parse(int which, const char *str) {
    uint32_t value = strtol(str, NULL, 16);
    dbg_reg_set(which, value);
}
#endif

#ifndef NO_USE_DEBUGGER
static uint32_t dbg_get_instr_addr() {
    return oldpc;
}

static const char *trap_names[] = { "BRK", NULL };

static uint32_t dbg_disassemble(uint32_t addr, char *buf, size_t bufsize) {
    return dbg6502_disassemble(&core6502_cpu_debug, addr, buf, bufsize, x65c02 ? M65C02 : M6502);
}

static uint32_t dbg_readmem(uint32_t addr) {
    return C6502_Read8(addr);
}

static void dbg_writemem(uint32_t addr, uint32_t value) {
    C6502_Write8(addr, value);
}

cpu_debug_t core6502_cpu_debug = {
        .cpu_name       = "core6502",
        .debug_enable   = dbg_debug_enable,
        .memread        = dbg_readmem,
        .memwrite       = dbg_writemem,
        .disassemble    = dbg_disassemble,
        .reg_names      = dbg6502_reg_names,
        .reg_get        = dbg_reg_get,
        .reg_set        = dbg_reg_set,
        .reg_print      = dbg_reg_print,
        .reg_parse      = dbg_reg_parse,
        .get_instr_addr = dbg_get_instr_addr,
        .trap_names     = trap_names
};

#endif

void wibble() {
//    if (g_cpu.pc == 0xfff4) {
//        printf("OSBYTEY!\n");
//    }
//    if (512248 == get_cpu_timestamp()) {
//        printf("SBC LAND!\n");
//    }

}
#ifndef USE_HW_EVENT
int32_t get_cpu_timestamp() {
    extern int framesrun;
    return 40000 * framesrun + g_cpu.clk;
}
#endif
#ifdef PRINT_INSTRUCTIONS
void __time_critical_func(print_instructions)() {
//    wibble();
//if (get_cpu_timestamp() >= 1450295) {
//    printf("argh!\n");
//}
//if (get_cpu_timestamp() >= 2000000) {
//    fflush(stdout);
//    exit(0);
//}
    printf("%d PC=%04x A=%02x X=%02x Y=%02x (%02x)\n", (int)get_cpu_timestamp(), g_cpu.pc, g_cpu.a, g_cpu.x, g_cpu.y, g_cpu.status);
}
#endif
#ifndef NDEBUG
#if THUMB_CPU_USE_ASM
uint8_t UnmappedMemReadFunction( uint16_t addr, __unused uint32_t op_code) {
#else
uint8_t UnmappedMemReadFunction( uint16_t addr ) {
#endif
    printf("Unmapped read %04x\n", addr);
    assert(false);
    return 0;
}
void UnmappedMemWriteFunction( uint16_t addr, uint8_t val ) {
    printf("Unmapped write %04x %02x\n", addr, val);
    assert(false);
}
#endif

void CPUInit()
{
#if THUMB_CPU_USE_ASM
#if PI_ASM32
    printf("Using Pico ARM-ified ASM CPU on Pi\n");
#else
    printf("Using Pico Thumb CPU\n");
#endif
#else
    printf("Using Pico C CPU\n");
#endif
    memset(&g_cpu, 0, sizeof(g_cpu));
#ifndef NDEBUG
    g_cpu.memHandlers = (MemHandler *)calloc(64, sizeof(MemHandler)); // this will be leaked, but fine.
    for(int i=0; i < CPU_MEM_SIZE / CPU_MEM_BLOCKSIZE; i++) {
        MemHandlerSetReadFunction(g_cpu.memHandlers + i, UnmappedMemReadFunction);
        MemHandlerSetWriteFunction(g_cpu.memHandlers + i, UnmappedMemWriteFunction);
    }
#endif

#if THUMB_CPU_USE_ASM

    for( int i = 0 ; i < 256 ; i++ )
    {
        g_cpu.znFlags[ i ] = 0;
        if( i == 0 )        g_cpu.znFlags[ i ] |= ( 1 << FLG_BIT_z );
        if( i & 0x80 )      g_cpu.znFlags[ i ] |= ( 1 << FLG_BIT_n );
    }
    // index 256 for a value that is both zero and negative!
    g_cpu.znFlags[256] = (1 << FLG_BIT_z) | (1 << FLG_BIT_n );

    for( int i = 0 ; i < 16 ; i++ )
    {        
        g_cpu.nvczFlags[ i ] = 0;
        if( i & 0x8 )      g_cpu.nvczFlags[ i ] |= ( 1 << FLG_BIT_n );
        if( i & 0x1 )      g_cpu.nvczFlags[ i ] |= ( 1 << FLG_BIT_v );
        if( i & 0x2 )      g_cpu.nvczFlags[ i ] |= ( 1 << FLG_BIT_c );
        if( i & 0x4 )      g_cpu.nvczFlags[ i ] |= ( 1 << FLG_BIT_z );
    }
#endif

}

void CPUSetPC( uint16_t pc )
{
    printf("set_pc %04x\n", pc);
    g_cpu.pc = pc;
}

void CPUReset( void )
{
    g_cpu.pc = C6502_Read16( 0xfffc );
    g_cpu.sp = 0xfd;
    g_cpu.status = 0; FLG_SET(g_cpu, i);
    g_cpu.interrupt = 0;
}

void __time_critical_func(CPUNMI)( bool c6512 )
{
    C6502_Push16( g_cpu.pc );
    C6502_Push8( g_cpu.status | 0x20);
    FLG_SET( g_cpu, i );
    if (c6512) FLG_CLR( g_cpu, d);
    g_cpu.pc = C6502_Read16( 0xfffa );
    g_cpu.clk += 7;
}

void /*__time_critical*/ CPUIRQ( bool c6512 )
{
    C6502_Push16( g_cpu.pc );
    C6502_Push8( g_cpu.status | 0x20);
    FLG_SET( g_cpu, i );
    if (c6512) FLG_CLR( g_cpu, d);
    g_cpu.pc = C6502_Read16( 0xfffe );
    g_cpu.clk += 7;
}

void CPUUpdatePC(uint16_t _pc)
{
    assert_if_asm();
    uint op = C6502_Read8( g_cpu.pc );
    int delta = 3;
    if (op != 0x8d) {
        printf("Unknown call site for update pc!\n");
        assert(false);
    }
    g_cpu.pc = (_pc - delta);
}

#if THUMB_CPU_USE_ASM


extern MemHandler *cpu_memHandlers[16];   // cpu_mem.c: the map for each 4K of PC

void __time_critical_func(CPURun)( bool c6512 )
{
    DEBUG_PINS_SET(core_use, 1);
    extern void entry_6502( C6502* cpu );
#if PICO_ON_DEVICE
//    interp_configure_none( mm_interp0, 0, 7, 3, 8 );
    interp_config config = interp_default_config();
    interp_config_set_shift(&config, 7);
    interp_config_set_mask(&config, 3, 8);
    interp_set_config(interp0, 0, &config);
    interp0->base[ 0 ] = (uintptr_t)( g_cpu.memHandlers );
#endif
#if PI_ASM32
    pi_mem_handler_base = g_cpu.memHandlers;
#endif
    advance_hardware(get_cpu_timestamp());
    set_next_cpu_clk();
    // hw_event version
    while (g_cpu.clk < 0) {
#if PICO_ON_DEVICE && !PI_ASM32
        // entry_6502 uses whatever map the interpolator holds, and the asm
        // only switches maps on a jump.  Pick the map for the current PC, or
        // the MOS VDU code (C000-DFFF) resumes with the normal map after a
        // frame or a breakout and writes the shadow screen into main RAM.
        interp0->base[ 0 ] = (uintptr_t)( cpu_memHandlers[ g_cpu.pc >> 12 ] );
#endif
        entry_6502( &g_cpu );
#ifndef CPU_ASM_INLINE_BREAKOUT
        CPUASMBreakout();
#endif
    }
    DEBUG_PINS_CLR(core_use, 1);
}

#else

void CPURun( bool c6512) {
    static int oldnmi;

//    if (get_cpu_timestamp() > 5000000) exit(0);
    extern void (*g_optable_n6502[])(void);
    extern void (*g_optable_c6512[])(void);
    void (**optable)(void) = c6512 ? g_optable_c6512 : g_optable_n6502;
#ifndef USE_HW_EVENT
    while( g_cpu.clk < 0 )
    {
#ifndef NO_USE_DEBUGGER
        pc3 = oldoldpc;
        oldoldpc = oldpc;
        oldpc = g_cpu.pc;
        if (dbg_core6502)
            debug_preexec(&core6502_cpu_debug, oldpc);
#endif
#ifdef PRINT_INSTRUCTIONS
        print_instructions();
#endif
        uint8_t     op = C6502_Read8( g_cpu.pc );
        MemMapUpdatePC(g_cpu.pc);
#if 0
        {
            static unsigned char newCode[65536];
            if( !newCode[ g_cpu.pc ] )
            {
            printf( "BRS: %04x : %02x\n", g_cpu.pc, op );
            newCode[ g_cpu.pc] = 1;
            }
        }
#endif
        g_cpu.clkAdjust = 0;
        uint32_t clk = g_cpu.clk;
        optable[ op ]();
        clk += g_cpu.clkAdjust;
            if (g_cpu.interrupt && !FLG_ISSET(g_cpu, i)) {
                    g_cpu.interrupt &= ~128;
                    //            printf("Take IRQ at %04x %d\n", g_cpu.pc, get_cpu_timestamp());
                    CPUIRQ(c6512);
        }
        g_cpu.interrupt &= ~128;
        if (clk != g_cpu.clk) {
            polltime(g_cpu.clk - clk);
        }

        if (g_cpu.nmi && !oldnmi) {
//            printf("Take NMI at %04x %d\n", g_cpu.pc, get_cpu_timestamp());
            clk = g_cpu.clk;
            CPUNMI(c6512);
            polltime(g_cpu.clk - clk);
            g_cpu.nmi = 0;
        }
        oldnmi = g_cpu.nmi;
    }
#else

    static bool first;
    if (!first) {
#if 0 //!PICO_ON_DEVICE
        extern void (*platform_key_down)(int scancode, int keysym, int modifiers);
        platform_key_down(225, 0, 0);
#endif
        first = true;
    }
    advance_hardware(get_cpu_timestamp());
    set_next_cpu_clk();
    // hw_event version
    while (g_cpu.clk < 0) {
        while (g_cpu.clk < 0) {
#ifdef PRINT_INSTRUCTIONS
            print_instructions();
#endif
            uint8_t op = C6502_Read8(g_cpu.pc);
//            if (get_cpu_timestamp() >= 1310299) {
//                printf("EXEC %d %04x %02x\n", get_cpu_timestamp(), g_cpu.pc, op);
//            }
            MemMapUpdatePC(g_cpu.pc);
            optable[op]();
        }
//        printf("GT %d\n", get_cpu_timestamp());
        // we only need to check interrupts here because the above loop always terminates
        // when an hw_event is triggered (or in response to a breakout request due to IRQ)
        if (g_cpu.interrupt && !FLG_ISSET(g_cpu, i)) {
            if (g_cpu.cli_breakout) {
                uint8_t op = C6502_Read8(g_cpu.pc);
                if (op == 0x78) {
                    // do the SEI before then taking the interrupt
                    FLG_SET(g_cpu, i);
                    g_cpu.clk += 2;
                    g_cpu.pc = (g_cpu.pc + 1);
                }
            }
                //            printf("Take IRQ at %04x %d\n", g_cpu.pc, get_cpu_timestamp());

                g_cpu.interrupt &= ~128;
                CPUIRQ(c6512);
        }
        g_cpu.interrupt &= ~128;
        g_cpu.cli_breakout = false;
        advance_hardware(get_cpu_timestamp());
        if (g_cpu.nmi && !oldnmi) {
//            printf("Take NMI at %04x %d\n", g_cpu.pc, get_cpu_timestamp());
            CPUNMI(c6512);
            g_cpu.nmi = 0;
            advance_hardware(get_cpu_timestamp());
        }
        set_next_cpu_clk();
        oldnmi = g_cpu.nmi;
    }
#endif
}
#endif

// include this anyway, for the opnames table
#include "6502_c.inl"

