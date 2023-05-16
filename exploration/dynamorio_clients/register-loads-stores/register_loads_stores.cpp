/* ******************************************************************************
 * Copyright (c) 2014-2018 Google, Inc.  All rights reserved.
 * Copyright (c) 2011 Massachusetts Institute of Technology  All rights
 * reserved. Copyright (c) 2008 VMware, Inc.  All rights reserved.
 * ******************************************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of VMware, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* Code Manipulation API Sample:
 * register_loads_stores.cpp
 *
 * Based on inscount.cpp
 *
 * Reports the dynamic count of the total number of loads/stores executed.
 * Both operations should happen between the memory and a register.
 * Illustrates how to perform performant clean calls.
 * Demonstrates effect of clean call optimization and auto-inlining with
 * different -opt_cleancall values.
 *
 * The runtime options for this client include:
 *   -only_from_app  Do not count instructions in shared libraries.
 * The options are handled using the droption extension.
 */

#include "dr_api.h"
#include "drmgr.h"
#include "droption.h"

#ifdef WINDOWS
#define DISPLAY_STRING(msg) dr_messagebox(msg)
#else
#define DISPLAY_STRING(msg) dr_printf("%s\n", msg);
#endif

#define NULL_TERMINATE(buf) (buf)[(sizeof((buf)) / sizeof((buf)[0])) - 1] = '\0'

static droption_t<bool>
    only_from_app(DROPTION_SCOPE_CLIENT, "only_from_app", false,
                  "Only count app, not lib, instructions",
                  "Count only instructions in the application itself, ignoring "
                  "instructions in "
                  "shared libraries.");

/* Application module */
static app_pc exe_start;

/* we only have a global count */
static uint64 global_count;

typedef struct mem_access_cnt {
    uint loads;
    uint stores;
} mem_access_cnt_t;

static uint ld;
static uint st;

/* A simple clean call that will be automatically inlined because it has only
 * one argument and contains no calls to other functions.
 */
static void load_count(uint num_loads) { ld += num_loads; }
static void store_count(uint num_stores) { st += num_stores; }

static void event_exit(void);
static dr_emit_flags_t event_bb_analysis(void *drcontext, void *tag,
                                         instrlist_t *bb, bool for_trace,
                                         bool translating, void **user_data);
static dr_emit_flags_t event_app_instruction(void *drcontext, void *tag,
                                             instrlist_t *bb, instr_t *inst,
                                             bool for_trace, bool translating,
                                             void *user_data);

DR_EXPORT void dr_client_main(client_id_t id, int argc, const char *argv[])
{
    dr_set_client_name("DynamoRIO Sample Client 'inscount'",
                       "http://dynamorio.org/issues");

    /* Options */
    if (!droption_parser_t::parse_argv(DROPTION_SCOPE_CLIENT, argc, argv,
                                       nullptr, nullptr))
        DR_ASSERT(false);
    drmgr_init();

    /* Get main module address */
    if (only_from_app.get_value()) {
        module_data_t *exe = dr_get_main_module();
        if (exe != nullptr)
            exe_start = exe->start;
        dr_free_module_data(exe);
    }

    /* register events */
    dr_register_exit_event(event_exit);
    drmgr_register_bb_instrumentation_event(event_bb_analysis,
                                            event_app_instruction, nullptr);

    /* make it easy to tell, by looking at log file, which client executed */
    dr_log(nullptr, DR_LOG_ALL, 1, "Client 'inscount' initializing\n");
#ifdef SHOW_RESULTS
    /* also give notification to stderr */
    if (dr_is_notify_on()) {
        dr_fprintf(STDERR, "Client inscount is running\n");
    }
#endif
}

static void event_exit(void)
{
#ifdef SHOW_RESULTS
    char msg[512];
    int len;
    len = dr_snprintf(msg, sizeof(msg) / sizeof(msg[0]),
                      "loads,stores\n%llu,%llu", ld, st);
    DR_ASSERT(len > 0);
    NULL_TERMINATE(msg);
    DISPLAY_STRING(msg);
#endif /* SHOW_RESULTS */
    drmgr_exit();
}

static dr_emit_flags_t event_bb_analysis(void *drcontext, void *tag,
                                         instrlist_t *bb, bool for_trace,
                                         bool translating, void **user_data)
{
    instr_t *instr;
    mem_access_cnt_t *mem_access_cnt = new mem_access_cnt_t;
    mem_access_cnt->loads = 0;
    mem_access_cnt->stores = 0;

#ifdef VERBOSE
    dr_printf("in dynamorio_basic_block(tag=" PFX ")\n", tag);
#ifdef VERBOSE_VERBOSE
    instrlist_disassemble(drcontext, tag, bb, STDOUT);
#endif
#endif
    /* Only count in app BBs */
    if (only_from_app.get_value()) {
        module_data_t *mod = dr_lookup_module(dr_fragment_app_pc(tag));
        if (mod != nullptr) {
            bool from_exe = (mod->start == exe_start);
            dr_free_module_data(mod);
            if (!from_exe) {
                *user_data = nullptr;
                return DR_EMIT_DEFAULT;
            }
        }
    }

    /* Count instructions.
     */
    for (instr = instrlist_first(bb); instr != nullptr;
         instr = instr_get_next(instr)) {
        std::string op_name =
            std::string{decode_opcode_name(instr_get_opcode(instr))};

#ifdef X86
        if (op_name.find("mov") == std::string::npos) {
            continue;
        }
#endif

        if (instr_reads_memory(instr)) {
            for (int i = 0; i < instr_num_dsts(instr); i++) {
                if (opnd_is_reg(instr_get_dst(instr, i))) {
                    mem_access_cnt->loads++;
                    continue;
                }
            }
        }
        if (instr_writes_memory(instr)) {
            for (int i = 0; i < instr_num_srcs(instr); i++) {
                if (opnd_is_reg(instr_get_src(instr, i))) {
                    mem_access_cnt->stores++;
                    continue;
                }
            }
        }
    }

    *user_data = (void *)(ptr_uint_t)mem_access_cnt;

#if defined(VERBOSE) && defined(VERBOSE_VERBOSE)
    dr_printf("Finished counting for dynamorio_basic_block(tag=" PFX ")\n",
              tag);
    instrlist_disassemble(drcontext, tag, bb, STDOUT);
#endif
    return DR_EMIT_DEFAULT;
}

static dr_emit_flags_t event_app_instruction(void *drcontext, void *tag,
                                             instrlist_t *bb, instr_t *instr,
                                             bool for_trace, bool translating,
                                             void *user_data)
{
    mem_access_cnt_t *mem_access_cnt;
    /* By default drmgr enables auto-predication, which predicates all
     * instructions with the predicate of the current instruction on ARM. We
     * disable it here because we want to unconditionally execute the following
     * instrumentation.
     */
    drmgr_disable_auto_predication(drcontext, bb);
    if (!drmgr_is_first_instr(drcontext, instr))
        return DR_EMIT_DEFAULT;
    /* Only insert calls for in-app BBs */
    if (user_data == nullptr)
        return DR_EMIT_DEFAULT;
    /* Insert clean call */
    mem_access_cnt = (mem_access_cnt_t *)(ptr_uint_t)user_data;

    dr_insert_clean_call(drcontext, bb, instrlist_first_app(bb),
                         (void *)load_count, false /* save fpstate */, 1,
                         OPND_CREATE_INT32(mem_access_cnt->loads));
    dr_insert_clean_call(drcontext, bb, instrlist_first_app(bb),
                         (void *)store_count, false /* save fpstate */, 1,
                         OPND_CREATE_INT32(mem_access_cnt->stores));

    return DR_EMIT_DEFAULT;
}