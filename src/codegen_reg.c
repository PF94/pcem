#include "ibm.h"
#include "codegen.h"
#include "codegen_backend.h"
#include "codegen_ir_defs.h"
#include "codegen_reg.h"

uint8_t reg_last_version[IREG_COUNT];
uint8_t reg_version_refcount[IREG_COUNT][256];

ir_reg_t invalid_ir_reg = {IREG_INVALID};

ir_reg_t host_regs[CODEGEN_HOST_REGS];
static uint8_t host_reg_dirty[CODEGEN_HOST_REGS];

static uint8_t host_regs_locked;

enum
{
        REG_BYTE,
        REG_WORD,
        REG_DWORD,
        REG_QWORD,
        REG_POINTER
};

struct
{
        int native_size;
        void *p;
} ireg_data[IREG_COUNT] =
{
        [IREG_EAX] = {REG_DWORD, &EAX},
	[IREG_ECX] = {REG_DWORD, &ECX},
	[IREG_EDX] = {REG_DWORD, &EDX},
	[IREG_EBX] = {REG_DWORD, &EBX},
	[IREG_ESP] = {REG_DWORD, &ESP},
	[IREG_EBP] = {REG_DWORD, &EBP},
	[IREG_ESI] = {REG_DWORD, &ESI},
	[IREG_EDI] = {REG_DWORD, &EDI},

	[IREG_flags_op]  = {REG_DWORD, &cpu_state.flags_op},
	[IREG_flags_res] = {REG_DWORD, &cpu_state.flags_res},
	[IREG_flags_op1] = {REG_DWORD, &cpu_state.flags_op1},
	[IREG_flags_op2] = {REG_DWORD, &cpu_state.flags_op2},

	[IREG_pc]    = {REG_DWORD, &cpu_state.pc},
	[IREG_oldpc] = {REG_DWORD, &cpu_state.oldpc},

	[IREG_eaaddr] = {REG_DWORD, &cpu_state.eaaddr},
	[IREG_ea_seg] = {REG_POINTER, &cpu_state.ea_seg},

	[IREG_op32] = {REG_DWORD, &cpu_state.op32},
	[IREG_ssegs] = {REG_BYTE, &cpu_state.ssegs},
	
	[IREG_rm_mod_reg] = {REG_DWORD, &cpu_state.rm_data.rm_mod_reg_data},

	[IREG_ins]    = {REG_DWORD, &cpu_state.cpu_recomp_ins},
	[IREG_cycles] = {REG_DWORD, &cpu_state._cycles},

	/*Temporary registers are stored on the stack, and are not guaranteed to
          be preserved across uOPs. They will not be written back if they will
          not be read again.*/
	[IREG_temp0] = {REG_DWORD, NULL},
	[IREG_temp1] = {REG_DWORD, NULL},
	[IREG_temp2] = {REG_DWORD, NULL},
	[IREG_temp3] = {REG_DWORD, NULL}
};

void codegen_reg_reset()
{
        int c;
        
        for (c = 0; c < IREG_COUNT; c++)
        {
                reg_last_version[c] = 0;
                reg_version_refcount[c][0] = 0;
        }
        for (c = 0; c < CODEGEN_HOST_REGS; c++)
        {
                host_regs[c] = invalid_ir_reg;
                host_reg_dirty[c] = 0;
        }
}

static inline int ir_reg_is_invalid(ir_reg_t ir_reg)
{
        return (ir_reg.reg == IREG_INVALID);
}

static void codegen_reg_load(codeblock_t *block, int c, ir_reg_t ir_reg)
{
        switch (ireg_data[ir_reg.reg].native_size)
        {
                case REG_DWORD:
                codegen_direct_read_32(block, codegen_host_reg_list[c], ireg_data[ir_reg.reg].p);
                break;

                default:
                fatal("codegen_reg_load - native_size=%i\n", ireg_data[ir_reg.reg].native_size);
        }

        host_regs[c] = ir_reg;
}

static void codegen_reg_writeback(codeblock_t *block, int c)
{
        int ir_reg = host_regs[c].reg;

        switch (ireg_data[ir_reg].native_size)
        {
                case REG_BYTE:
                codegen_direct_write_8(block, ireg_data[ir_reg].p, codegen_host_reg_list[c]);
                break;

                case REG_DWORD:
                codegen_direct_write_32(block, ireg_data[ir_reg].p, codegen_host_reg_list[c]);
                break;

                case REG_POINTER:
                codegen_direct_write_ptr(block, ireg_data[ir_reg].p, codegen_host_reg_list[c]);
                break;

                default:
                fatal("codegen_reg_flush - native_size=%i\n", ireg_data[ir_reg].native_size);
        }

        host_regs[c] = invalid_ir_reg;
        host_reg_dirty[c] = 0;
}

void codegen_reg_alloc_register(ir_reg_t dest_reg_a, ir_reg_t src_reg_a, ir_reg_t src_reg_b)
{
        int c;
        int dest_reference = 0;
        
        host_regs_locked = 0;
        
/*        pclog("alloc_register: dst=%i.%i src_a=%i.%i src_b=%i.%i\n", dest_reg_a.reg, dest_reg_a.version,
                src_reg_a.reg, src_reg_a.version,
                src_reg_b.reg, src_reg_b.version);*/
        
        if (!ir_reg_is_invalid(dest_reg_a))
        {
                if (!ir_reg_is_invalid(src_reg_a) && src_reg_a.reg == dest_reg_a.reg && src_reg_a.version == dest_reg_a.version-1)
                        dest_reference++;
                if (!ir_reg_is_invalid(src_reg_b) && src_reg_b.reg == dest_reg_a.reg && src_reg_b.version == dest_reg_a.version-1)
                        dest_reference++;
        }
        for (c = 0; c < CODEGEN_HOST_REGS; c++)
        {
                if (!ir_reg_is_invalid(src_reg_a) && host_regs[c].reg == src_reg_a.reg)
                {
                        if (host_regs[c].version != src_reg_a.version)
                                fatal("codegen_reg_alloc_register - host_regs[c].version != src_reg_a.version\n");
                        host_regs_locked |= (1 << c);
                }
                if (!ir_reg_is_invalid(src_reg_b) && host_regs[c].reg == src_reg_b.reg)
                {
                        if (host_regs[c].version != src_reg_b.version)
                                fatal("codegen_reg_alloc_register - host_regs[c].version != src_reg_b.version\n");
                        host_regs_locked |= (1 << c);
                }
                if (!ir_reg_is_invalid(dest_reg_a) && host_regs[c].reg == dest_reg_a.reg)
                {
                        if (host_regs[c].version == dest_reg_a.version || (host_regs[c].version == (dest_reg_a.version-1) && reg_version_refcount[host_regs[c].reg][host_regs[c].version] == dest_reference))
                                host_regs_locked |= (1 << c);
                        else
                                fatal("codegen_reg_alloc_register - host_regs[c].version != dest_reg_a.version  %i,%i %i\n", host_regs[c].version, dest_reg_a.version, dest_reference);
                }
        }
}

ir_host_reg_t codegen_reg_alloc_read_reg(codeblock_t *block, ir_reg_t ir_reg)
{
        int c;

        /*Search for required register*/
        for (c = 0; c < CODEGEN_HOST_REGS; c++)
        {
                if (!ir_reg_is_invalid(host_regs[c]) && host_regs[c].reg == ir_reg.reg && host_regs[c].version == ir_reg.version)
                        break;

                if (!ir_reg_is_invalid(host_regs[c]) && host_regs[c].reg == ir_reg.reg && reg_version_refcount[host_regs[c].reg][host_regs[c].version])
                        fatal("codegen_reg_alloc_read_reg - version mismatch!\n");
        }

        if (c == CODEGEN_HOST_REGS)
        {
                /*No unused registers. Search for an unlocked register*/
                for (c = 0; c < CODEGEN_HOST_REGS; c++)
                {
                        if (!(host_regs_locked & (1 << c)))
                                break;
                }
                if (c == CODEGEN_HOST_REGS)
                        fatal("codegen_reg_alloc_read_reg - out of registers\n");
                if (host_reg_dirty[c])
                        codegen_reg_writeback(block, c);
                codegen_reg_load(block, c, ir_reg);
                host_regs_locked |= (1 << c);
//                fatal("codegen_reg_alloc_read_reg - read %i.%i to %i\n", ir_reg.reg,ir_reg.version, c);
//                codegen_reg_writeback(block, c);
        }

        reg_version_refcount[host_regs[c].reg][host_regs[c].version]--;
        if (reg_version_refcount[host_regs[c].reg][host_regs[c].version] < 0)
                fatal("codegen_reg_alloc_read_reg - refcount < 0\n");

        host_reg_dirty[c] = 0;
//        pclog(" codegen_reg_alloc_read_reg: %i.%i %i\n", ir_reg.reg, ir_reg.version, codegen_host_reg_list[c]);
        return codegen_host_reg_list[c];
}

ir_host_reg_t codegen_reg_alloc_write_reg(codeblock_t *block, ir_reg_t ir_reg)
{
        int c;
        
        /*Search for previous version in host register*/
        for (c = 0; c < CODEGEN_HOST_REGS; c++)
        {
                if (!ir_reg_is_invalid(host_regs[c]) && host_regs[c].reg == ir_reg.reg)
                {
                        if (host_regs[c].version == ir_reg.version-1)
                        {
                                if (reg_version_refcount[host_regs[c].reg][host_regs[c].version] != 0)
                                        fatal("codegen_reg_alloc_write_reg - previous version refcount != 0\n");
                                break;
                        }
                }
        }
        
        if (c == CODEGEN_HOST_REGS)
        {
                /*Search for unused registers*/
                for (c = 0; c < CODEGEN_HOST_REGS; c++)
                {
                        if (ir_reg_is_invalid(host_regs[c]))
                                break;
                }
        
                if (c == CODEGEN_HOST_REGS)
                {
                        /*No unused registers. Search for an unlocked register*/
                        for (c = 0; c < CODEGEN_HOST_REGS; c++)
                        {
                                if (!(host_regs_locked & (1 << c)))
                                        break;
                        }
                        if (c == CODEGEN_HOST_REGS)
                                fatal("codegen_reg_alloc_write_reg - out of registers\n");
                        if (host_reg_dirty[c])
                                codegen_reg_writeback(block, c);
                }
        }
        
        host_regs[c].reg = ir_reg.reg;
        host_regs[c].version = ir_reg.version;
        host_reg_dirty[c] = 1;
//        pclog(" codegen_reg_alloc_write_reg: %i.%i %i\n", ir_reg.reg, ir_reg.version, codegen_host_reg_list[c]);
        return codegen_host_reg_list[c];
}

void codegen_reg_flush(ir_data_t *ir, codeblock_t *block)
{
        int c;
        
        for (c = 0; c < CODEGEN_HOST_REGS; c++)
        {
                if (!ir_reg_is_invalid(host_regs[c]))
                {
                        codegen_reg_writeback(block, c);
                }
        }
}
