#include "ibm.h"
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include "x87.h"
#include "x86_flags.h"
#include "mem.h"
#include "nmi.h"
#include "pic.h"
#include "codegen.h"

#define CPU_BLOCK_END() cpu_block_end = 1

#include "386_common.h"

static inline void fetch_ea_32_long(uint32_t rmdat)
{
        eal_r = eal_w = NULL;
        easeg = cpu_state.ea_seg->base;
        if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC)
        {
                uint32_t addr = easeg + cpu_state.eaaddr;
                if (readlookup2[addr >> 12] != -1)
                        eal_r = (uint32_t*)(readlookup2[addr >> 12] + addr);
                if (writelookup2[addr >> 12] != -1)
                        eal_w = (uint32_t*)(writelookup2[addr >> 12] + addr);
        }
}

static inline void fetch_ea_16_long(uint32_t rmdat)
{
        eal_r = eal_w = NULL;
        easeg = cpu_state.ea_seg->base;
        if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC)
        {
                uint32_t addr = easeg + cpu_state.eaaddr;
                if (readlookup2[addr >> 12] != -1)
                        eal_r = (uint32_t*)(readlookup2[addr >> 12] + addr);
                if (writelookup2[addr >> 12] != -1)
                        eal_w = (uint32_t*)(writelookup2[addr >> 12] + addr);
        }
}

#define fetch_ea_16(rmdat)              cpu_state.pc++; if (cpu_mod != 3) fetch_ea_16_long(rmdat);
#define fetch_ea_32(rmdat)              cpu_state.pc++; if (cpu_mod != 3) fetch_ea_32_long(rmdat);

#define PREFETCH_RUN(instr_cycles, bytes, modrm, reads, read_ls, writes, write_ls, ea32)
#define PREFETCH_PREFIX()
#define PREFETCH_FLUSH()

#define OP_TABLE(name) dynarec_ops_ ## name
/*Temporary*/
#define CLOCK_CYCLES(c)
#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

#include "386_ops.h"
