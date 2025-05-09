/*
 * Minimal ARMv6 assembler with ELF output, public domain (CC0)
 *
 * Supports:
 *   - .text, .data, .bss sections
 *   - label resolution and branch patching
 *   - data-processing, branching, load/store (including LDRB/STRB, STM/LDM,
 * PUSH/POP)
 *   - pre/post-indexed addressing with register/immediate offsets and shift
 * support
 *   - flat symbol table (no scoping)
 *   - ELF32 output suitable for qemu-arm and Linux
 *
 * No copyright. No warranty. Use for any purpose.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EI_NIDENT 16
#define ET_EXEC 2
#define EM_ARM 40
#define EV_CURRENT 1
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

#define BASE_VADDR 0x8000 /* Typical Linux ARM virtual address base */
#define MAX_LABELS 256
#define MAX_LINES 4096
#define MAX_LINE 256

/* Section identifiers for code/data/bss */
enum section { SEC_NONE, SEC_TEXT, SEC_DATA, SEC_BSS };

/* Label table: name, section, offset */
struct label {
	char name[64];
	enum section sec;
	unsigned int offset;
};

/* Patch table for unresolved symbol references */
struct patch {
	char name[64];
	enum section sec;
	unsigned int offset; /* word offset in section */
	int instr_type;	     /* 0=branch, 1=.word, 2=ldr=imm */
	int condbits;	     /* for branch: condition bits */
};

/* Symbol and patch tables */
static struct label labels[MAX_LABELS];
static int label_count = 0;
static struct patch patches[MAX_LABELS];
static int patch_count = 0;

/* Section buffers */
static uint32_t text_buf[MAX_LINES], data_buf[MAX_LINES];
static int text_count = 0, data_count = 0, bss_count = 0;
static enum section current_sec = SEC_TEXT;
static unsigned int entry_addr = 0x8000;

/* --- Utility: trim whitespace from start and end --- */
void trim(char *s) {
	char *p = s;
	while (isspace(*p))
		p++;
	if (p != s)
		memmove(s, p, strlen(p) + 1);
	int n = strlen(s);
	while (n > 0 && isspace(s[n - 1]))
		s[--n] = 0;
}

/* --- Add a label to symbol table --- */
void add_label(const char *name, enum section sec, unsigned int off) {
	if (label_count < MAX_LABELS) {
		strncpy(labels[label_count].name, name, 63);
		labels[label_count].name[63] = 0;
		labels[label_count].sec = sec;
		labels[label_count].offset = off;
		label_count++;
	}
}

/* --- Find label by name, return section and offset --- */
int find_label(const char *name, enum section *sec, unsigned int *off) {
	int i;
	for (i = 0; i < label_count; ++i) {
		if (strcmp(labels[i].name, name) == 0) {
			*sec = labels[i].sec;
			*off = labels[i].offset;
			return 1;
		}
	}
	return 0;
}

/* --- Add a patch for forward/backward reference to patch up later --- */
void add_patch(const char *name, enum section sec, unsigned int off,
	       int instr_type, int condbits) {
	if (patch_count < MAX_LABELS) {
		strncpy(patches[patch_count].name, name, 63);
		patches[patch_count].name[63] = 0;
		patches[patch_count].sec = sec;
		patches[patch_count].offset = off;
		patches[patch_count].instr_type = instr_type;
		patches[patch_count].condbits = condbits;
		patch_count++;
	}
}

/* --- Parse ARM register name (r0...r15, sp, lr, pc) --- */
int parse_reg(const char *tok) {
	if ((tok[0] == 'r' || tok[0] == 'R') && isdigit(tok[1]) &&
	    (tok[2] == 0 || isdigit(tok[2]))) {
		int n = atoi(tok + 1);
		if (n >= 0 && n <= 15)
			return n;
	}
	if (strcasecmp(tok, "sp") == 0)
		return 13;
	if (strcasecmp(tok, "lr") == 0)
		return 14;
	if (strcasecmp(tok, "pc") == 0)
		return 15;
	return -1;
}

/* --- Remove @ or ; comments from a line --- */
void strip_comment(char *line) {
	char *at = strchr(line, '@');
	if (at)
		*at = 0;
	at = strchr(line, ';');
	if (at)
		*at = 0;
}

/* --- Parse ARM shift operand (e.g. "lsl #2" or "lsr r3") --- */
int parse_shift(const char *s, int *type, int *amount) {
	char stype[4], sval[16];
	int n = sscanf(s, "%3s %15s", stype, sval);
	if (n < 2)
		return 0;
	if (strcasecmp(stype, "lsl") == 0)
		*type = 0;
	else if (strcasecmp(stype, "lsr") == 0)
		*type = 1;
	else if (strcasecmp(stype, "asr") == 0)
		*type = 2;
	else if (strcasecmp(stype, "ror") == 0)
		*type = 3;
	else
		return 0;
	if (sval[0] == '#')
		*amount = atoi(sval + 1);
	else if (sval[0] == 'r' || sval[0] == 'R')
		*amount = -parse_reg(sval);
	else
		return 0;
	return 1;
}

/* --- Encode ARM operand2 with optional shift --- */
unsigned int encode_shift_operand(const char *rm, const char *shift) {
	int Rm = parse_reg(rm);
	if (Rm < 0)
		return 0;
	if (!shift || !*shift)
		return Rm;
	int stype, amount;
	if (!parse_shift(shift, &stype, &amount))
		return 0;
	if (amount >= 0) {
		return Rm | ((amount & 0x1F) << 7) | (stype << 5);
	} else {
		int Rs = -amount;
		return Rm | (Rs << 8) | (stype << 5) | (1 << 4);
	}
}

/* --- Return ARM opcode field for mnemonic --- */
int get_opcode(const char *op) {
	if (!strcmp(op, "and"))
		return 0;
	if (!strcmp(op, "eor"))
		return 1;
	if (!strcmp(op, "sub"))
		return 2;
	if (!strcmp(op, "rsb"))
		return 3;
	if (!strcmp(op, "add"))
		return 4;
	if (!strcmp(op, "adc"))
		return 5;
	if (!strcmp(op, "sbc"))
		return 6;
	if (!strcmp(op, "rsc"))
		return 7;
	if (!strcmp(op, "tst"))
		return 8;
	if (!strcmp(op, "teq"))
		return 9;
	if (!strcmp(op, "cmp"))
		return 10;
	if (!strcmp(op, "cmn"))
		return 11;
	if (!strcmp(op, "orr"))
		return 12;
	if (!strcmp(op, "mov"))
		return 13;
	if (!strcmp(op, "bic"))
		return 14;
	if (!strcmp(op, "mvn"))
		return 15;
	return -1;
}

/*
 * ARM instruction encoding:
 * - Data-processing (including shifted register)
 * - Branches (with label patch)
 * - Load/store (LDR/STR[B], pre/post-indexed, shifts)
 * - STM/LDM (multiple registers)
 * - PUSH/POP
 * - SWI
 */
unsigned int encode_instr(const char *op, char **args, int nargs, int lineno,
			  char *errmsg) {
	int Rd, Rn, Rm, Rs, imm, cond = 0xE << 28, reglist = 0, wbit = 0,
				 ubit = 1, pbit = 1, lbit = 0;
	char op_base[8];
	strncpy(op_base, op, 7);
	op_base[7] = 0;
	for (int i = 0; op_base[i]; ++i)
		op_base[i] = tolower(op_base[i]);
	/* Parse condition codes (e.g. bne, beq, etc.) */
	if (strlen(op_base) > 2 && (op_base[0] == 'b' || op_base[0] == 'm')) {
		char *suffix = op_base + strlen(op_base) - 2;
		if (!strcmp(suffix, "eq"))
			cond = 0x0 << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "ne"))
			cond = 0x1 << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "cs"))
			cond = 0x2 << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "cc"))
			cond = 0x3 << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "mi"))
			cond = 0x4 << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "pl"))
			cond = 0x5 << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "vs"))
			cond = 0x6 << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "vc"))
			cond = 0x7 << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "hi"))
			cond = 0x8 << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "ls"))
			cond = 0x9 << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "ge"))
			cond = 0xA << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "lt"))
			cond = 0xB << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "gt"))
			cond = 0xC << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "le"))
			cond = 0xD << 28, op_base[strlen(op_base) - 2] = 0;
		else if (!strcmp(suffix, "al"))
			cond = 0xE << 28, op_base[strlen(op_base) - 2] = 0;
	}
	/* Data-processing (with shifted register) */
	if (nargs == 4) {
		Rd = parse_reg(args[0]);
		Rn = parse_reg(args[1]);
		if (Rd < 0 || Rn < 0)
			goto err;
		unsigned int operand2 = encode_shift_operand(args[2], args[3]);
		int opc = get_opcode(op_base);
		if (opc < 0)
			goto err;
		return cond | 0x800000 | (opc << 21) | (Rn << 16) | (Rd << 12) |
		       operand2;
	}
/* Data-processing (immediate/register) */
#define DP(opc, name)                                                          \
	if (strcmp(op_base, name) == 0 && (nargs == 3 || nargs == 2)) {        \
		if (nargs == 2 &&                                              \
		    (strcmp(name, "mov") == 0 || strcmp(name, "mvn") == 0)) {  \
			Rd = parse_reg(args[0]);                               \
			if (Rd < 0)                                            \
				goto err;                                      \
			if (args[1][0] == '#') {                               \
				imm = atoi(args[1] + 1);                       \
				if (imm < 0 || imm > 255)                      \
					goto err;                              \
				return cond | 0x3A00000 | (opc << 21) |        \
				       (Rd << 12) | imm;                       \
			} else {                                               \
				Rm = parse_reg(args[1]);                       \
				if (Rm < 0)                                    \
					goto err;                              \
				return cond | 0x1A00000 | (opc << 21) |        \
				       (Rd << 12) | Rm;                        \
			}                                                      \
		} else if (nargs == 3) {                                       \
			Rd = parse_reg(args[0]);                               \
			Rn = parse_reg(args[1]);                               \
			if (Rd < 0 || Rn < 0)                                  \
				goto err;                                      \
			if (args[2][0] == '#') {                               \
				imm = atoi(args[2] + 1);                       \
				if (imm < 0 || imm > 255)                      \
					goto err;                              \
				return cond | 0x2800000 | (opc << 21) |        \
				       (Rn << 16) | (Rd << 12) | imm;          \
			} else {                                               \
				Rm = parse_reg(args[2]);                       \
				if (Rm < 0)                                    \
					goto err;                              \
				return cond | 0x800000 | (opc << 21) |         \
				       (Rn << 16) | (Rd << 12) | Rm;           \
			}                                                      \
		}                                                              \
	}
	DP(0, "and")
	DP(1, "eor") DP(2, "sub") DP(3, "rsb") DP(4, "add") DP(5, "adc")
	    DP(6, "sbc") DP(7, "rsc") DP(12, "orr") DP(13, "mov") DP(14, "bic")
		DP(15, "mvn")
#undef DP
/* Comparison instructions (CMP, CMN, TST, TEQ) */
#define CMP(opc, name)                                                         \
	if (strcmp(op_base, name) == 0 && nargs == 2) {                        \
		Rn = parse_reg(args[0]);                                       \
		if (Rn < 0)                                                    \
			goto err;                                              \
		if (args[1][0] == '#') {                                       \
			imm = atoi(args[1] + 1);                               \
			if (imm < 0 || imm > 255)                              \
				goto err;                                      \
			return cond | 0x3500000 | (opc << 21) | (Rn << 16) |   \
			       imm;                                            \
		} else {                                                       \
			Rm = parse_reg(args[1]);                               \
			if (Rm < 0)                                            \
				goto err;                                      \
			return cond | 0x1500000 | (opc << 21) | (Rn << 16) |   \
			       Rm;                                             \
		}                                                              \
	}
		    CMP(10, "cmp") CMP(11, "cmn") CMP(8, "tst") CMP(9, "teq")
#undef CMP
	    /* Multiply, Multiply-Accumulate */
	    if (strcmp(op_base, "mul") == 0 && nargs == 3) {
		Rd = parse_reg(args[0]);
		Rm = parse_reg(args[1]);
		Rs = parse_reg(args[2]);
		if (Rd < 0 || Rm < 0 || Rs < 0)
			goto err;
		return cond | 0x90 << 20 | (Rd << 16) | (Rs << 8) | (Rm << 0) |
		       0x9 << 4;
	}
	if (strcmp(op_base, "mla") == 0 && nargs == 4) {
		Rd = parse_reg(args[0]);
		Rm = parse_reg(args[1]);
		Rs = parse_reg(args[2]);
		Rn = parse_reg(args[3]);
		if (Rd < 0 || Rm < 0 || Rs < 0 || Rn < 0)
			goto err;
		return cond | 0x92 << 20 | (Rd << 16) | (Rn << 12) | (Rs << 8) |
		       (Rm << 0) | 0x9 << 4;
	}
	/* LDR/STR/LDRB/STRB with pre/post/shifted addressing */
	if (strcmp(op_base, "ldr") == 0 || strcmp(op_base, "str") == 0 ||
	    strcmp(op_base, "ldrb") == 0 || strcmp(op_base, "strb") == 0) {
		int is_ldr = (op_base[0] == 'l');
		int is_byte = (strlen(op_base) > 3 && op_base[3] == 'b');
		if (nargs < 2)
			goto err;
		Rd = parse_reg(args[0]);
		if (Rd < 0)
			goto err;
		/* Support LDR Rd, =imm as a pseudo-instruction */
		if (args[1][0] == '=') {
			imm = atoi(args[1] + 1);
			return cond | 0x3A00000 | (Rd << 12) | imm;
		}
		if (args[1][0] == '[') {
			/* Parse all forms: [Rn], [Rn, #imm], [Rn], #imm, [Rn,
			 * Rm], [Rn, Rm, lsl #2], [Rn, #imm]! etc. */
			char inner[64], *after_bracket, *comma, *immstr,
			    *shift = NULL;
			int P = 1, U = 1, W = 0, offset = 0, is_reg_offset = 0;
			strncpy(inner, args[1] + 1, sizeof(inner) - 1);
			inner[sizeof(inner) - 1] = 0;
			char *close = strchr(inner, ']');
			if (!close)
				goto err;
			*close = 0;
			after_bracket = args[1] + (close - inner) + 2;
			comma = strchr(inner, ',');
			if (!comma) {
				Rn = parse_reg(inner);
				if (Rn < 0)
					goto err;
				if (after_bracket && after_bracket[0] == ',') {
					/* Post-indexed: [Rn], #imm */
					P = 0;
					W = 0;
					immstr = after_bracket + 1;
					while (*immstr && (isspace(*immstr) ||
							   *immstr == ','))
						++immstr;
					if (*immstr == '#') {
						U = 1;
						imm = atoi(immstr + 1);
						if (*(immstr + 1) == '-')
							U = 0,
							imm = atoi(immstr + 2);
					} else
						goto err;
					offset = imm;
				} else if (after_bracket &&
					   after_bracket[0] == '!') {
					/* Writeback: [Rn]! */
					P = 1;
					W = 1;
				}
			} else {
				*comma = 0;
				Rn = parse_reg(inner);
				if (Rn < 0)
					goto err;
				immstr = comma + 1;
				while (*immstr &&
				       (isspace(*immstr) || *immstr == ','))
					++immstr;
				/* Pre-indexed immediate [Rn, #imm] or register
				 * [Rn, Rm] */
				if (*immstr == '#') {
					U = 1;
					if (*(immstr + 1) == '-')
						U = 0, imm = atoi(immstr + 2);
					else
						imm = atoi(immstr + 1);
					offset = imm;
				} else if (immstr[0] == 'r' ||
					   immstr[0] == 'R') {
					is_reg_offset = 1;
					Rm = parse_reg(immstr);
					if (Rm < 0)
						goto err;
					char *shift_ptr = strchr(immstr, ',');
					if (shift_ptr) {
						*shift_ptr = 0;
						shift = shift_ptr + 1;
						while (*shift &&
						       (isspace(*shift) ||
							*shift == ','))
							++shift;
					}
				} else
					goto err;
				if (after_bracket && after_bracket[0] == '!')
					P = 1, W = 1;
			}
			unsigned int instr =
			    cond | ((is_ldr ? 0x41 : 0x40) << 20) |
			    (is_byte ? (1 << 22) : 0) | (P << 24) | (U << 23) |
			    (W << 21) | (Rn << 16) | (Rd << 12);
			if (is_reg_offset) {
				instr |= encode_shift_operand(immstr, shift);
			} else {
				instr |= offset & 0xFFF;
			}
			return instr;
		}
	}
	/* STM/LDM (multiple registers, with optional writeback) */
	if ((strcmp(op_base, "stm") == 0 || strcmp(op_base, "ldm") == 0) &&
	    nargs >= 2) {
		int is_ldm = (op_base[0] == 'l');
		Rn = parse_reg(args[0]);
		if (Rn < 0)
			goto err;
		char *regs = args[1];
		if (regs[0] == '{') {
			reglist = 0;
			char *p = regs + 1, tok[8];
			while (*p && *p != '}') {
				int len = 0;
				while (*p && *p != ',' && *p != '}')
					tok[len++] = *p++;
				tok[len] = 0;
				trim(tok);
				int r = parse_reg(tok);
				if (r < 0)
					goto err;
				reglist |= 1 << r;
				while (*p == ',' || isspace(*p))
					++p;
			}
		}
		char *ex = strchr(args[0], '!');
		if (ex)
			wbit = 1;
		pbit = 0;
		ubit = 0;
		lbit = is_ldm;
		return cond | (pbit << 24) | (ubit << 23) | (wbit << 21) |
		       (lbit << 20) | (Rn << 16) | reglist;
	}
	/* PUSH/POP as STMFD/LDMFD sp! */
	if (strcmp(op_base, "push") == 0 && nargs == 1) {
		reglist = 0;
		char *p = args[0] + 1, tok[8];
		while (*p && *p != '}') {
			int len = 0;
			while (*p && *p != ',' && *p != '}')
				tok[len++] = *p++;
			tok[len] = 0;
			trim(tok);
			int r = parse_reg(tok);
			if (r < 0)
				goto err;
			reglist |= 1 << r;
			while (*p == ',' || isspace(*p))
				++p;
		}
		return cond | 0x92D << 16 | (13 << 16) | reglist;
	}
	if (strcmp(op_base, "pop") == 0 && nargs == 1) {
		reglist = 0;
		char *p = args[0] + 1, tok[8];
		while (*p && *p != '}') {
			int len = 0;
			while (*p && *p != ',' && *p != '}')
				tok[len++] = *p++;
			tok[len] = 0;
			trim(tok);
			int r = parse_reg(tok);
			if (r < 0)
				goto err;
			reglist |= 1 << r;
			while (*p == ',' || isspace(*p))
				++p;
		}
		return cond | 0x8BD << 16 | (13 << 16) | reglist;
	}
	/* Branches (B, BL) with label patching */
	if (strcmp(op_base, "b") == 0 && nargs == 1) {
		add_patch(args[0], current_sec,
			  (current_sec == SEC_TEXT ? text_count : data_count),
			  0, cond);
		return cond | 0xA000000;
	}
	if (strcmp(op_base, "bl") == 0 && nargs == 1) {
		add_patch(args[0], current_sec,
			  (current_sec == SEC_TEXT ? text_count : data_count),
			  1, cond);
		return cond | 0xB000000;
	}
	/* BX (branch and exchange) */
	if (strcmp(op_base, "bx") == 0 && nargs == 1) {
		Rm = parse_reg(args[0]);
		if (Rm < 0)
			goto err;
		return cond | 0x12FFF10 | Rm;
	}
	/* Software interrupt */
	if (strcmp(op_base, "swi") == 0 && nargs == 1) {
		imm = atoi(args[0]);
		if (imm < 0 || imm > 0xFFFFFF)
			goto err;
		return cond | 0xF000000 | imm;
	}
err:
	sprintf(errmsg, "Cannot encode: %s %s %s %s", op,
		nargs > 0 ? args[0] : "", nargs > 1 ? args[1] : "",
		nargs > 2 ? args[2] : "");
	return 0;
}

/* --- Parse a line into label/op/args --- */
void parse_line(char *line, char *label, char *op, char **args, int *nargs) {
	*label = 0;
	*op = 0;
	*nargs = 0;
	char *p = line, *q;
	trim(p);
	if (*p == 0)
		return;
	q = strchr(p, ':');
	if (q && (q - p) < 63) {
		strncpy(label, p, q - p);
		label[q - p] = 0;
		p = q + 1;
		trim(p);
	}
	q = p;
	while (*q && !isspace(*q))
		++q;
	strncpy(op, p, q - p);
	op[q - p] = 0;
	p = q;
	trim(p);
	*nargs = 0;
	while (*p) {
		while (*p == ',' || isspace(*p))
			++p;
		if (*p == 0)
			break;
		args[*nargs] = p;
		while (*p && *p != ',' && !isspace(*p))
			++p;
		if (*p) {
			*p = 0;
			++p;
		}
		(*nargs)++;
		if (*nargs > 4)
			break;
	}
}

/* --- ELF32 headers and segment structures --- */
typedef struct {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type, e_machine;
	uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
	    e_shstrndx;
} Elf32_Ehdr;
typedef struct {
	uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags,
	    p_align;
} Elf32_Phdr;

int main(int argc, char **argv) {
	char line[MAX_LINE], label[64], op[16], *args[5], errmsg[128];
	FILE *fin, *fout;
	int i, n, nargs, lineno = 0;
	enum section lsec;
	unsigned int loff, pc, instr;

	if (argc < 3) {
		printf("Usage: %s <input.s> <output.elf>\n", argv[0]);
		return 1;
	}
	fin = fopen(argv[1], "r");
	if (!fin) {
		perror("input");
		return 1;
	}

	/* --- Pass 1: Parse sections, labels, store instructions and data ---
	 */
	text_count = data_count = bss_count = 0;
	label_count = 0;
	patch_count = 0;
	current_sec = SEC_TEXT;
	while (fgets(line, MAX_LINE, fin)) {
		lineno++;
		strip_comment(line);
		trim(line);
		if (line[0] == 0)
			continue;
		parse_line(line, label, op, args, &nargs);
		if (label[0]) {
			if (current_sec == SEC_TEXT)
				add_label(label, SEC_TEXT, text_count * 4);
			else if (current_sec == SEC_DATA)
				add_label(label, SEC_DATA, data_count * 4);
			else if (current_sec == SEC_BSS)
				add_label(label, SEC_BSS, bss_count * 4);
		}
		if (op[0] == 0)
			continue;
		/* Section and assembler directives */
		if (op[0] == '.') {
			if (strcasecmp(op, ".section") == 0 && nargs == 1) {
				if (strcasecmp(args[0], ".text") == 0)
					current_sec = SEC_TEXT;
				else if (strcasecmp(args[0], ".data") == 0)
					current_sec = SEC_DATA;
				else if (strcasecmp(args[0], ".bss") == 0)
					current_sec = SEC_BSS;
			} else if (strcasecmp(op, ".text") == 0)
				current_sec = SEC_TEXT;
			else if (strcasecmp(op, ".data") == 0)
				current_sec = SEC_DATA;
			else if (strcasecmp(op, ".bss") == 0)
				current_sec = SEC_BSS;
			else if (strcasecmp(op, ".global") == 0 && nargs == 1) {
				/* No effect for flat assembler */
			} else if (strcasecmp(op, ".word") == 0 && nargs == 1) {
				if (current_sec == SEC_TEXT) {
					if (isdigit(args[0][0]))
						text_buf[text_count++] =
						    atoi(args[0]);
					else {
						add_patch(args[0], current_sec,
							  text_count, 1, 0);
						text_buf[text_count++] = 0;
					}
				} else if (current_sec == SEC_DATA) {
					if (isdigit(args[0][0]))
						data_buf[data_count++] =
						    atoi(args[0]);
					else {
						add_patch(args[0], current_sec,
							  data_count, 1, 0);
						data_buf[data_count++] = 0;
					}
				}
			} else if (strcasecmp(op, ".space") == 0 &&
				   nargs == 1) {
				/* .space N in .bss increases bss size by N
				 * bytes */
				if (current_sec == SEC_BSS)
					bss_count += atoi(args[0]) / 4;
			}
			continue;
		}
		/* Text and data instructions and words */
		if (current_sec == SEC_TEXT) {
			instr = encode_instr(op, args, nargs, lineno, errmsg);
			if (instr == 0) {
				fprintf(stderr, "Line %d: %s\n", lineno,
					errmsg);
				text_buf[text_count++] = 0;
			} else
				text_buf[text_count++] = instr;
		} else if (current_sec == SEC_DATA) {
			if (strcasecmp(op, ".word") == 0 && nargs == 1) {
				if (isdigit(args[0][0]))
					data_buf[data_count++] = atoi(args[0]);
				else {
					add_patch(args[0], current_sec,
						  data_count, 1, 0);
					data_buf[data_count++] = 0;
				}
			}
		} else if (current_sec == SEC_BSS) {
			bss_count++;
		}
	}
	fclose(fin);

	/* --- Pass 2: Patch up branches and symbol references --- */
	for (i = 0; i < patch_count; ++i) {
		if (find_label(patches[i].name, &lsec, &loff)) {
			if (patches[i].instr_type == 0 ||
			    patches[i].instr_type == 1) {
				int here = patches[i].offset * 4;
				int dest = loff;
				if (lsec == SEC_TEXT)
					dest += BASE_VADDR;
				else if (lsec == SEC_DATA)
					dest += BASE_VADDR + text_count * 4;
				else if (lsec == SEC_BSS)
					dest += BASE_VADDR + text_count * 4 +
						data_count * 4;
				if (patches[i].instr_type == 0 ||
				    patches[i].instr_type == 1) {
					/* Patch branch offset */
					int offset =
					    ((dest - (BASE_VADDR + here) - 8) >>
					     2) &
					    0x00ffffff;
					text_buf[patches[i].offset] |= offset;
				} else if (patches[i].instr_type == 1) {
					text_buf[patches[i].offset] = dest;
				}
			} else if (patches[i].instr_type == 1) {
				if (patches[i].sec == SEC_TEXT)
					text_buf[patches[i].offset] = loff;
				else if (patches[i].sec == SEC_DATA)
					data_buf[patches[i].offset] = loff;
			}
		} else {
			fprintf(stderr, "Unresolved symbol: %s\n",
				patches[i].name);
		}
	}

	/* --- Find entry point (_start label in .text) --- */
	for (i = 0; i < label_count; ++i)
		if (strcmp(labels[i].name, "_start") == 0 &&
		    labels[i].sec == SEC_TEXT)
			entry_addr = BASE_VADDR + labels[i].offset;

	/* --- Write ELF output (minimal: header, program headers, sections) ---
	 */
	fout = fopen(argv[2], "wb");
	if (!fout) {
		perror("output");
		return 1;
	}
	unsigned int text_size = text_count * 4, data_size = data_count * 4,
		     bss_size = bss_count * 4;
	unsigned int file_off = sizeof(Elf32_Ehdr) + 3 * sizeof(Elf32_Phdr);
	unsigned int data_off = file_off + text_size;
	Elf32_Ehdr eh = {0};
	eh.e_ident[0] = 0x7f;
	eh.e_ident[1] = 'E';
	eh.e_ident[2] = 'L';
	eh.e_ident[3] = 'F';
	eh.e_ident[4] = ELFCLASS32;
	eh.e_ident[5] = ELFDATA2LSB;
	eh.e_ident[6] = EV_CURRENT;
	eh.e_type = ET_EXEC;
	eh.e_machine = EM_ARM;
	eh.e_version = EV_CURRENT;
	eh.e_entry = entry_addr;
	eh.e_phoff = sizeof(Elf32_Ehdr);
	eh.e_flags = 0x5000002;
	eh.e_ehsize = sizeof(Elf32_Ehdr);
	eh.e_phentsize = sizeof(Elf32_Phdr);
	eh.e_phnum = 3;
	Elf32_Phdr ph_text = {0};
	ph_text.p_type = PT_LOAD;
	ph_text.p_offset = file_off;
	ph_text.p_vaddr = BASE_VADDR;
	ph_text.p_paddr = BASE_VADDR;
	ph_text.p_filesz = text_size;
	ph_text.p_memsz = text_size;
	ph_text.p_flags = PF_R | PF_X;
	ph_text.p_align = 0x1000;
	Elf32_Phdr ph_data = {0};
	ph_data.p_type = PT_LOAD;
	ph_data.p_offset = data_off;
	ph_data.p_vaddr = BASE_VADDR + text_size;
	ph_data.p_paddr = BASE_VADDR + text_size;
	ph_data.p_filesz = data_size;
	ph_data.p_memsz = data_size;
	ph_data.p_flags = PF_R | PF_W;
	ph_data.p_align = 0x1000;
	Elf32_Phdr ph_bss = {0};
	ph_bss.p_type = PT_LOAD;
	ph_bss.p_offset = 0;
	ph_bss.p_vaddr = BASE_VADDR + text_size + data_size;
	ph_bss.p_paddr = ph_bss.p_vaddr;
	ph_bss.p_filesz = 0;
	ph_bss.p_memsz = bss_size;
	ph_bss.p_flags = PF_R | PF_W;
	ph_bss.p_align = 0x1000;
	fwrite(&eh, sizeof(eh), 1, fout);
	fwrite(&ph_text, sizeof(ph_text), 1, fout);
	fwrite(&ph_data, sizeof(ph_data), 1, fout);
	fwrite(&ph_bss, sizeof(ph_bss), 1, fout);
	fwrite(text_buf, 1, text_size, fout);
	fwrite(data_buf, 1, data_size, fout);
	fclose(fout);
	printf("ELF written: .text %u bytes, .data %u bytes, .bss %u bytes, "
	       "entry 0x%08x\n",
	       text_size, data_size, bss_size, entry_addr);
	return 0;
}