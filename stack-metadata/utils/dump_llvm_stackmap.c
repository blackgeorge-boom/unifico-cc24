/**
 * Utility for dumping stackmap information generated by LLVM.
 *
 * Author: Rob Lyerly <rlyerly@vt.edu>
 * Date: 5/18/2017
 */

#include <unistd.h>

#include "definitions.h"
#include "bin.h"
#include "stackmap.h"
#include "util.h"

///////////////////////////////////////////////////////////////////////////////
// Configuration
///////////////////////////////////////////////////////////////////////////////

static const char *args = "hf:n:s:";
static const char *help =
		"dump-llvm-stackmap - dump stackmap information generated by LLVM\n\n\
\
Usage: ./dump-llvm-stackmap [ OPTIONS ]\n\
Options:\n\
\t-h      : print help & exit\n\
\t-f file : name of object file\n\
\t-n function : name of function to examine\n\n\
\t-s name : name of stackmap section added to objects "
		"(default is '.llvm_stackmaps')";

static const char *thebin_fn = NULL;
static const char *func_name = NULL;
static const char *sm_section_name = ".llvm_pcn_stackmaps";
bool verbose = false;

///////////////////////////////////////////////////////////////////////////////
// Utilities
///////////////////////////////////////////////////////////////////////////////

static void print_help()
{
	printf("%s\n", help);
	exit(0);
}

static void parse_args(int argc, char **argv)
{
	int arg;

	while((arg = getopt(argc, argv, args)) != -1)
	{
		switch(arg)
		{
			case 'h':
				print_help();
				break;
			case 'f':
				thebin_fn = optarg;
				break;
			case 'n':
				func_name = optarg;
				break;
			case 's':
				sm_section_name = optarg;
				break;
			default:
				fprintf(stderr, "Unknown argument '%c'\n", arg);
				break;
		}
	}

	if(!thebin_fn)
		die("please specify a binary (run with -h for more information)",
			INVALID_ARGUMENT);
}

///////////////////////////////////////////////////////////////////////////////
// Printing metadata
///////////////////////////////////////////////////////////////////////////////

static inline bool dump_location(live_value *record)
{
	printf("    Location: ");
	switch(record->type) {
		case SM_REGISTER:
			printf("in register %u", record->regnum);
			break;
		case SM_DIRECT:
			printf("at register %u + %d",
				   record->regnum, record->offset_or_constant);
			break;
		case SM_INDIRECT:
			printf("at pointer generated by register %u + %d",
				   record->regnum, record->offset_or_constant);
			break;
		case SM_CONSTANT:
			printf("%d", record->offset_or_constant);
			break;
		case SM_CONST_IDX:
			printf("in constant pool at index %d",
				   record->offset_or_constant);
			break;
		default:
			printf("unknown type %x\n", record->type);
			return false;
	}

	if(record->is_duplicate)
		printf(" (duplicate record)");

	if(record->is_ptr)
		printf(", is a pointer");

	if(record->is_alloca)
		printf(", is an alloca of size %u byte(s)", record->alloca_size);

	if(record->is_temporary)
		printf(", is a stackmap temporary");

	printf("\n");

	return true;
}

static inline bool dump_arch_location(arch_live_value *record)
{
	printf("    Arch-specific location: ");
	switch(record->type) {
		case SM_REGISTER:
			printf("in register %u", record->regnum);
			break;
		case SM_INDIRECT:
			printf("at pointer generated by register %u + %d",
				   record->regnum, record->offset);
			break;
		default:
			printf("unknown type %x\n", record->type);
			return false;
	}

	switch(record->inst_type) {
#define X(name) \
  case name: printf(", " #name " "); break;
#include "../common/include/StackTransformTypes.def"
		VALUE_GEN_INST
#undef X
		default:
			printf(", unknown instruction type %x\n", record->inst_type);
			return false;
	}

	switch(record->operand_type) {
		case SM_REGISTER:
			printf("register %u\n", record->operand_regnum);
			break;
		case SM_DIRECT:
			printf("value stored at register %u + %ld\n", record->operand_regnum,
				   record->operand_offset_or_constant);
			break;
		case SM_INDIRECT:
			printf("register %u + %ld\n", record->operand_regnum,
				   record->operand_offset_or_constant);
			break;
		case SM_CONSTANT:
			printf("value = %ld / 0x%lx\n", record->operand_offset_or_constant,
				   record->operand_offset_or_constant);
			break;
		default:
			printf("unknown operand type %x\n", record->operand_type);
			return false;
	}
	return true;
}

static inline bool dump_call_site(call_site_record *cs)
{
	unsigned i;

	printf("  Call site %lu: function %u, offset @ %u, %u locations, %u "
		   "live-outs, %u arch-specific locations\n",
		   cs->id, cs->func_idx, cs->offset, cs->num_locations, cs->num_live_outs,
		   cs->num_arch_live);

	for(i = 0; i < cs->num_locations; i++)
		if(!dump_location(&cs->locations[i]))
			return false;

	for(i = 0; i < cs->num_live_outs; i++)
		printf("    Live-out: register %u (size=%u bytes)",
			   cs->live_outs[i].regnum, cs->live_outs[i].size);

	for(i = 0; i < cs->num_arch_live; i++)
		if(!dump_arch_location(&cs->arch_live[i]))
			return false;

	return true;
}

static inline bool dump_stackmaps(bin* b, stack_map_section *sm, size_t num_sm)
{
	size_t i;
	uint32_t j;
	int64_t func_idx;  // If func_name is given, then this will be its index in the function records.
	uint64_t func;
	GElf_Sym sym;
	const char *sym_name;

	printf("Found %lu stackmaps\n", num_sm);
	for(i = 0; i < num_sm; i++)
	{
		printf("Stackmap v%u: %u functions, %u constants, %u call sites\n",
			   sm[i].version, sm[i].num_functions, sm[i].num_constants,
			   sm[i].num_records);
		func_idx = -1;

		for(j = 0; j < sm[i].num_functions; j++)
		{
			func = sm[i].function_records[j].func_addr;
			sym = get_sym_by_addr(b->e, func, STT_FUNC);
			sym_name = get_sym_name(b->e, sym);

			/*
			 * If `func_name` is given as an argument, only check the function
			 * record with that name.
			 */
			if (func_name && strcmp(sym_name, func_name) != 0)
			{
				continue;
			}
			else
			{
				func_idx = j;
			}
			printf("  Function %s: address=%lx, stack size=%lu, number of unwinding "
				   "entries: %u, offset into unwinding section: %u\n",
				   sym_name, sm[i].function_records[j].func_addr,
				   sm[i].function_records[j].stack_size,
				   sm[i].function_records[j].num_unwind,
				   sm[i].function_records[j].unwind_offset);
		}

		for(j = 0; j < sm[i].num_constants; j++)
			printf("  Constant %u: %lu / %ld / %lx\n", j, sm[i].constants[j],
				   sm[i].constants[j], sm[i].constants[j]);

		for(j = 0; j < sm[i].num_records; j++)
		{
			if (func_name && func_idx != sm[i].call_sites[j].func_idx)
			{
				continue;
			}
			if(!dump_call_site(&sm[i].call_sites[j]))
				return false;
		}
	}

	return true;
}

ret_t dump_metadata(bin *thebin)
{
	char sec_name[BUF_SIZE];
	stack_map_section *sm;
	size_t num_sm;
	ret_t ret;

	snprintf(sec_name, BUF_SIZE, "%s", sm_section_name);
	if((ret = init_stackmap(thebin, &sm, &num_sm)) == SUCCESS)
	{
		printf("Reading section %s: ", sec_name);
		if(!dump_stackmaps(thebin, sm, num_sm))
		{
			printf("failed.\n");
			free_stackmaps(sm, num_sm);
			return READ_ELF_FAILED;
		}
	}
	else return ret;

	free_stackmaps(sm, num_sm);
	return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// Driver
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
	ret_t ret;
	bin *thebin = NULL;

	parse_args(argc, argv);

	if(elf_version(EV_CURRENT) == EV_NONE)
		die("could not initialize libELF", INVALID_ELF_VERSION);

	if((ret = init_elf_bin(thebin_fn, &thebin)) != SUCCESS)
		die("could not initialize the binary", ret);

	if((ret = dump_metadata(thebin)) != SUCCESS)
		die("could not read metadata from binary", ret);

	free_elf_bin(thebin);

	return 0;
}

