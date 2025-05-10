/*
 * Minimal RISC-V RV32I assembler (C90)
 * Features:
 * - Supports RV32I base integer instructions (R/I/S/B/U/J types)
 * - Handles labels, .text/.data/.word/.asciiz/.align/.space/.globl
 * - Supports common pseudo-instructions (mv, li, j, ret)
 * - Simple symbol and relocation tables for (basic) linker support
 * - Error handling for common issues
 *
 * Usage:
 *   gcc -std=c90 -o riscv32_assembler riscv32_assembler.c
 *   ./riscv32_assembler input.asm output.bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 256
#define MAX_LABELS 256
#define MAX_SYMBOLS 256
#define MAX_LABEL_LENGTH 64
#define MEMORY_SIZE 65536
#define DATA_START (MEMORY_SIZE / 2)

typedef struct {
    char label[MAX_LABEL_LENGTH];
    int address;
    int is_global;
} Label;

typedef struct {
    char symbol[MAX_LABEL_LENGTH];
    int address;
} Reloc;

static Label symbol_table[MAX_LABELS];
static int label_count = 0;

static Reloc relocation_table[MAX_SYMBOLS];
static int reloc_count = 0;

static unsigned int memory[MEMORY_SIZE];
static int pc = 0;
static int data_pc = DATA_START;

static int in_data_segment = 0;
static int in_text_segment = 0;

void parse_line(const char *line, int pass);
void add_label(const char *label, int address, int is_global);
int get_label_address(const char *label);
unsigned int encode_instruction(const char *mnemonic, const char *args, int pass, int cur_pc);
void write_output(const char *filename, int code_size, int data_size);
void error(const char *msg, const char *detail);
void add_reloc(const char *label, int address);
void write_symbol_table(const char *filename);
void write_reloc_table(const char *filename);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.asm> <output.bin>\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *input_file = fopen(argv[1], "r");
    if (!input_file) {
        perror("Error opening input file");
        return EXIT_FAILURE;
    }

    char line[MAX_LINE_LENGTH];

    // Pass 1: Label collection
    rewind(input_file);
    pc = 0;
    data_pc = DATA_START;
    in_text_segment = 0;
    in_data_segment = 0;
    while (fgets(line, sizeof(line), input_file)) {
        parse_line(line, 1);
    }

    // Pass 2: Code generation
    rewind(input_file);
    pc = 0;
    data_pc = DATA_START;
    in_text_segment = 0;
    in_data_segment = 0;
    while (fgets(line, sizeof(line), input_file)) {
        parse_line(line, 2);
    }

    fclose(input_file);

    write_output(argv[2], pc, data_pc - DATA_START);
    write_symbol_table("symbols.txt");
    write_reloc_table("relocs.txt");
    return EXIT_SUCCESS;
}

void error(const char *msg, const char *detail) {
    if (detail)
        fprintf(stderr, "Error: %s (%s)\n", msg, detail);
    else
        fprintf(stderr, "Error: %s\n", msg);
    exit(EXIT_FAILURE);
}

void add_label(const char *label, int address, int is_global) {
    int i;
    for (i = 0; i < label_count; i++) {
        if (strcmp(symbol_table[i].label, label) == 0)
            return;
    }
    if (label_count >= MAX_LABELS)
        error("Too many labels", label);
    strncpy(symbol_table[label_count].label, label, MAX_LABEL_LENGTH-1);
    symbol_table[label_count].label[MAX_LABEL_LENGTH-1] = 0;
    symbol_table[label_count].address = address;
    symbol_table[label_count].is_global = is_global;
    label_count++;
}

int get_label_address(const char *label) {
    int i;
    for (i = 0; i < label_count; i++) {
        if (strcmp(symbol_table[i].label, label) == 0)
            return symbol_table[i].address;
    }
    return -1;
}

void add_reloc(const char *label, int address) {
    if (reloc_count >= MAX_SYMBOLS)
        error("Too many relocations", label);
    strncpy(relocation_table[reloc_count].symbol, label, MAX_LABEL_LENGTH-1);
    relocation_table[reloc_count].symbol[MAX_LABEL_LENGTH-1] = 0;
    relocation_table[reloc_count].address = address;
    reloc_count++;
}

void write_output(const char *filename, int code_size, int data_size) {
    FILE *output_file = fopen(filename, "wb");
    if (!output_file)
        error("Cannot open output file", filename);

    fwrite(memory, sizeof(unsigned int), code_size/4, output_file);
    fwrite(memory + DATA_START/4, sizeof(unsigned int), data_size/4, output_file);

    fclose(output_file);
}

void write_symbol_table(const char *filename) {
    int i;
    FILE *f = fopen(filename, "w");
    if (!f) return;
    for (i = 0; i < label_count; i++) {
        fprintf(f, "%s 0x%04x %s\n", symbol_table[i].label, symbol_table[i].address, symbol_table[i].is_global?"GLOBAL":"LOCAL");
    }
    fclose(f);
}

void write_reloc_table(const char *filename) {
    int i;
    FILE *f = fopen(filename, "w");
    if (!f) return;
    for (i = 0; i < reloc_count; i++) {
        fprintf(f, "%s 0x%04x\n", relocation_table[i].symbol, relocation_table[i].address);
    }
    fclose(f);
}

void parse_line(const char *orig_line, int pass) {
    char line[MAX_LINE_LENGTH], clean_line[MAX_LINE_LENGTH];
    char *ptr;
    char mnemonic[MAX_LABEL_LENGTH], args[MAX_LINE_LENGTH];
    int len, val, align, mask, is_label, is_global = 0;

    strncpy(line, orig_line, MAX_LINE_LENGTH-1);
    line[MAX_LINE_LENGTH-1] = 0;
    ptr = strchr(line, '#');
    if (ptr) *ptr = 0;

    for (ptr = line; *ptr; ptr++) if (!isspace((unsigned char)*ptr)) break;
    if (*ptr == 0) return;

    is_label = 0;
    len = strlen(line);
    if (len > 1 && line[len-2] != ' ' && line[len-1] == ':') {
        is_label = 1;
        line[len-1] = 0;
    }
    if (is_label) {
        for (ptr = line; isspace((unsigned char)*ptr); ptr++);
        if (pass == 1)
            add_label(ptr, in_text_segment ? pc : data_pc, 0);
        return;
    }

    for (ptr = line; isspace((unsigned char)*ptr); ptr++);
    strncpy(clean_line, ptr, MAX_LINE_LENGTH-1);
    clean_line[MAX_LINE_LENGTH-1] = 0;

    if (clean_line[0] == '.') {
        sscanf(clean_line, "%s %[^\n]", mnemonic, args);
        if (strcmp(mnemonic, ".data") == 0) {
            in_data_segment = 1;
            in_text_segment = 0;
        } else if (strcmp(mnemonic, ".text") == 0) {
            in_data_segment = 0;
            in_text_segment = 1;
        } else if (strcmp(mnemonic, ".align") == 0) {
            sscanf(args, "%d", &align);
            mask = (1 << align) - 1;
            if (in_data_segment)
                data_pc = (data_pc + mask) & ~mask;
            else
                pc = (pc + mask) & ~mask;
        } else if (strcmp(mnemonic, ".space") == 0) {
            sscanf(args, "%d", &val);
            if (in_data_segment)
                data_pc += val;
            else
                pc += val;
        } else if (strcmp(mnemonic, ".word") == 0) {
            sscanf(args, "%d", &val);
            if (pass == 2) {
                if (in_data_segment)
                    memory[data_pc/4] = val;
                else
                    memory[pc/4] = val;
            }
            if (in_data_segment)
                data_pc += 4;
            else
                pc += 4;
        } else if (strcmp(mnemonic, ".asciiz") == 0) {
            char *str = strchr(args, '\"');
            if (!str) error(".asciiz missing string", NULL);
            str++;
            char *endq = strrchr(str, '\"');
            if (endq) *endq = 0;
            if (pass == 2) {
                int i = 0;
                while (str[i]) {
                    if (in_data_segment)
                        ((char*)(&memory[0]))[data_pc + i - DATA_START] = str[i];
                    else
                        ((char*)(&memory[0]))[pc + i] = str[i];
                    i++;
                }
                if (in_data_segment)
                    ((char*)(&memory[0]))[data_pc + i - DATA_START] = 0;
                else
                    ((char*)(&memory[0]))[pc + i] = 0;
            }
            if (in_data_segment)
                data_pc += strlen(str) + 1;
            else
                pc += strlen(str) + 1;
        } else if (strcmp(mnemonic, ".globl") == 0) {
            is_global = 1;
        } else {
            error("Unknown directive", mnemonic);
        }
        return;
    }

    if (sscanf(clean_line, "%s %[^\n]", mnemonic, args) == 2) {
        // Pseudo-instructions
        if (strcmp(mnemonic, "li") == 0) {
            int rd, imm;
            sscanf(args, "x%d, %d", &rd, &imm);
            if (imm >= -2048 && imm <= 2047) {
                char buf[32];
                sprintf(buf, "x%d, x0, %d", rd, imm);
                if (pass == 2 && in_text_segment)
                    memory[pc/4] = encode_instruction("addi", buf, pass, pc);
                if (in_text_segment) pc += 4;
            } else {
                int upper = (imm + (1<<11)) >> 12;
                int lower = imm - (upper << 12);
                char buf1[32], buf2[32];
                sprintf(buf1, "x%d, %d", rd, upper);
                sprintf(buf2, "x%d, x%d, %d", rd, rd, lower);
                if (pass == 2 && in_text_segment) {
                    memory[pc/4] = encode_instruction("lui", buf1, pass, pc);
                    memory[pc/4+1] = encode_instruction("addi", buf2, pass, pc+4);
                }
                if (in_text_segment) pc += 8;
            }
            return;
        }
        if (strcmp(mnemonic, "mv") == 0) {
            int rd, rs;
            sscanf(args, "x%d, x%d", &rd, &rs);
            char buf[32];
            sprintf(buf, "x%d, x%d, 0", rd, rs);
            if (pass == 2 && in_text_segment)
                memory[pc/4] = encode_instruction("addi", buf, pass, pc);
            if (in_text_segment) pc += 4;
            return;
        }
        if (strcmp(mnemonic, "j") == 0) {
            char label[MAX_LABEL_LENGTH];
            sscanf(args, "%s", label);
            int addr = get_label_address(label);
            int offset = 0;
            if (addr == -1) {
                if (pass == 2) add_reloc(label, pc);
            }
            if (pass == 2 && addr != -1)
                offset = (addr - pc);
            char buf[32];
            sprintf(buf, "%d", offset);
            if (pass == 2 && in_text_segment)
                memory[pc/4] = encode_instruction("jal", buf, pass, pc);
            if (in_text_segment) pc += 4;
            return;
        }
        if (strcmp(mnemonic, "ret") == 0) {
            if (pass == 2 && in_text_segment)
                memory[pc/4] = encode_instruction("jalr", "x0, x1, 0", pass, pc);
            if (in_text_segment) pc += 4;
            return;
        }
        // Real instructions
        if (pass == 2 && in_text_segment)
            memory[pc/4] = encode_instruction(mnemonic, args, pass, pc);
        if (in_text_segment) pc += 4;
        return;
    } else if (sscanf(clean_line, "%s", mnemonic) == 1) {
        if (pass == 2 && in_text_segment)
            memory[pc/4] = encode_instruction(mnemonic, "", pass, pc);
        if (in_text_segment) pc += 4;
        return;
    }
}

unsigned int encode_instruction(const char *mnemonic, const char *args, int pass, int cur_pc) {
    int rd=0, rs1=0, rs2=0, imm=0, offset=0, label_off=0, address=0;
    char label[MAX_LABEL_LENGTH];

    // R-type
    if (strcmp(mnemonic, "add") == 0) {
        sscanf(args, "x%d, x%d, x%d", &rd, &rs1, &rs2);
        return (0<<25)|(rs2<<20)|(rs1<<15)|(0<<12)|(rd<<7)|0x33;
    }
    if (strcmp(mnemonic, "sub") == 0) {
        sscanf(args, "x%d, x%d, x%d", &rd, &rs1, &rs2);
        return (0x20<<25)|(rs2<<20)|(rs1<<15)|(0<<12)|(rd<<7)|0x33;
    }
    if (strcmp(mnemonic, "sll") == 0) {
        sscanf(args, "x%d, x%d, x%d", &rd, &rs1, &rs2);
        return (0<<25)|(rs2<<20)|(rs1<<15)|(1<<12)|(rd<<7)|0x33;
    }
    if (strcmp(mnemonic, "slt") == 0) {
        sscanf(args, "x%d, x%d, x%d", &rd, &rs1, &rs2);
        return (0<<25)|(rs2<<20)|(rs1<<15)|(2<<12)|(rd<<7)|0x33;
    }
    if (strcmp(mnemonic, "sltu") == 0) {
        sscanf(args, "x%d, x%d, x%d", &rd, &rs1, &rs2);
        return (0<<25)|(rs2<<20)|(rs1<<15)|(3<<12)|(rd<<7)|0x33;
    }
    if (strcmp(mnemonic, "xor") == 0) {
        sscanf(args, "x%d, x%d, x%d", &rd, &rs1, &rs2);
        return (0<<25)|(rs2<<20)|(rs1<<15)|(4<<12)|(rd<<7)|0x33;
    }
    if (strcmp(mnemonic, "srl") == 0) {
        sscanf(args, "x%d, x%d, x%d", &rd, &rs1, &rs2);
        return (0<<25)|(rs2<<20)|(rs1<<15)|(5<<12)|(rd<<7)|0x33;
    }
    if (strcmp(mnemonic, "sra") == 0) {
        sscanf(args, "x%d, x%d, x%d", &rd, &rs1, &rs2);
        return (0x20<<25)|(rs2<<20)|(rs1<<15)|(5<<12)|(rd<<7)|0x33;
    }
    if (strcmp(mnemonic, "or") == 0) {
        sscanf(args, "x%d, x%d, x%d", &rd, &rs1, &rs2);
        return (0<<25)|(rs2<<20)|(rs1<<15)|(6<<12)|(rd<<7)|0x33;
    }
    if (strcmp(mnemonic, "and") == 0) {
        sscanf(args, "x%d, x%d, x%d", &rd, &rs1, &rs2);
        return (0<<25)|(rs2<<20)|(rs1<<15)|(7<<12)|(rd<<7)|0x33;
    }
    // I-type
    if (strcmp(mnemonic, "addi") == 0) {
        sscanf(args, "x%d, x%d, %d", &rd, &rs1, &imm);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(0<<12)|(rd<<7)|0x13;
    }
    if (strcmp(mnemonic, "andi") == 0) {
        sscanf(args, "x%d, x%d, %d", &rd, &rs1, &imm);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(7<<12)|(rd<<7)|0x13;
    }
    if (strcmp(mnemonic, "ori") == 0) {
        sscanf(args, "x%d, x%d, %d", &rd, &rs1, &imm);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(6<<12)|(rd<<7)|0x13;
    }
    if (strcmp(mnemonic, "xori") == 0) {
        sscanf(args, "x%d, x%d, %d", &rd, &rs1, &imm);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(4<<12)|(rd<<7)|0x13;
    }
    if (strcmp(mnemonic, "slli") == 0) {
        sscanf(args, "x%d, x%d, %d", &rd, &rs1, &imm);
        return ((imm&0x1F)<<20)|(rs1<<15)|(1<<12)|(rd<<7)|0x13;
    }
    if (strcmp(mnemonic, "srli") == 0) {
        sscanf(args, "x%d, x%d, %d", &rd, &rs1, &imm);
        return ((imm&0x1F)<<20)|(rs1<<15)|(5<<12)|(rd<<7)|0x13;
    }
    if (strcmp(mnemonic, "srai") == 0) {
        sscanf(args, "x%d, x%d, %d", &rd, &rs1, &imm);
        return ((0x20| (imm&0x1F))<<20)|(rs1<<15)|(5<<12)|(rd<<7)|0x13;
    }
    if (strcmp(mnemonic, "slti") == 0) {
        sscanf(args, "x%d, x%d, %d", &rd, &rs1, &imm);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(2<<12)|(rd<<7)|0x13;
    }
    if (strcmp(mnemonic, "sltiu") == 0) {
        sscanf(args, "x%d, x%d, %d", &rd, &rs1, &imm);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(3<<12)|(rd<<7)|0x13;
    }
    if (strcmp(mnemonic, "jalr") == 0) {
        sscanf(args, "x%d, x%d, %d", &rd, &rs1, &imm);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(0<<12)|(rd<<7)|0x67;
    }
    if (strcmp(mnemonic, "lb") == 0) {
        sscanf(args, "x%d, %d(x%d)", &rd, &imm, &rs1);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(0<<12)|(rd<<7)|0x03;
    }
    if (strcmp(mnemonic, "lh") == 0) {
        sscanf(args, "x%d, %d(x%d)", &rd, &imm, &rs1);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(1<<12)|(rd<<7)|0x03;
    }
    if (strcmp(mnemonic, "lw") == 0) {
        sscanf(args, "x%d, %d(x%d)", &rd, &imm, &rs1);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(2<<12)|(rd<<7)|0x03;
    }
    if (strcmp(mnemonic, "lbu") == 0) {
        sscanf(args, "x%d, %d(x%d)", &rd, &imm, &rs1);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(4<<12)|(rd<<7)|0x03;
    }
    if (strcmp(mnemonic, "lhu") == 0) {
        sscanf(args, "x%d, %d(x%d)", &rd, &imm, &rs1);
        return ((imm&0xFFF)<<20)|(rs1<<15)|(5<<12)|(rd<<7)|0x03;
    }
    // S-type
    if (strcmp(mnemonic, "sb") == 0) {
        sscanf(args, "x%d, %d(x%d)", &rs2, &imm, &rs1);
        return (((imm&0xfe0)<<20)| (rs2<<20)|(rs1<<15)|(0<<12)|((imm&0x1f)<<7)|0x23);
    }
    if (strcmp(mnemonic, "sh") == 0) {
        sscanf(args, "x%d, %d(x%d)", &rs2, &imm, &rs1);
        return (((imm&0xfe0)<<20)| (rs2<<20)|(rs1<<15)|(1<<12)|((imm&0x1f)<<7)|0x23);
    }
    if (strcmp(mnemonic, "sw") == 0) {
        sscanf(args, "x%d, %d(x%d)", &rs2, &imm, &rs1);
        return (((imm&0xfe0)<<20)| (rs2<<20)|(rs1<<15)|(2<<12)|((imm&0x1f)<<7)|0x23);
    }
    // B-type
    if (strcmp(mnemonic, "beq") == 0) {
        sscanf(args, "x%d, x%d, %d", &rs1, &rs2, &offset);
        unsigned int imm = offset;
        return (((imm&0x1000)<<19)|((imm&0x7e0)<<20)|(rs2<<20)|(rs1<<15)|(0<<12)|((imm&0x1e)<<7)|((imm&0x800)>>4)|0x63);
    }
    if (strcmp(mnemonic, "bne") == 0) {
        sscanf(args, "x%d, x%d, %d", &rs1, &rs2, &offset);
        unsigned int imm = offset;
        return (((imm&0x1000)<<19)|((imm&0x7e0)<<20)|(rs2<<20)|(rs1<<15)|(1<<12)|((imm&0x1e)<<7)|((imm&0x800)>>4)|0x63);
    }
    if (strcmp(mnemonic, "blt") == 0) {
        sscanf(args, "x%d, x%d, %d", &rs1, &rs2, &offset);
        unsigned int imm = offset;
        return (((imm&0x1000)<<19)|((imm&0x7e0)<<20)|(rs2<<20)|(rs1<<15)|(4<<12)|((imm&0x1e)<<7)|((imm&0x800)>>4)|0x63);
    }
    if (strcmp(mnemonic, "bge") == 0) {
        sscanf(args, "x%d, x%d, %d", &rs1, &rs2, &offset);
        unsigned int imm = offset;
        return (((imm&0x1000)<<19)|((imm&0x7e0)<<20)|(rs2<<20)|(rs1<<15)|(5<<12)|((imm&0x1e)<<7)|((imm&0x800)>>4)|0x63);
    }
    if (strcmp(mnemonic, "bltu") == 0) {
        sscanf(args, "x%d, x%d, %d", &rs1, &rs2, &offset);
        unsigned int imm = offset;
        return (((imm&0x1000)<<19)|((imm&0x7e0)<<20)|(rs2<<20)|(rs1<<15)|(6<<12)|((imm&0x1e)<<7)|((imm&0x800)>>4)|0x63);
    }
    if (strcmp(mnemonic, "bgeu") == 0) {
        sscanf(args, "x%d, x%d, %d", &rs1, &rs2, &offset);
        unsigned int imm = offset;
        return (((imm&0x1000)<<19)|((imm&0x7e0)<<20)|(rs2<<20)|(rs1<<15)|(7<<12)|((imm&0x1e)<<7)|((imm&0x800)>>4)|0x63);
    }
    // U-type
    if (strcmp(mnemonic, "lui") == 0) {
        sscanf(args, "x%d, %d", &rd, &imm);
        return ((imm&0xFFFFF)<<12)|(rd<<7)|0x37;
    }
    if (strcmp(mnemonic, "auipc") == 0) {
        sscanf(args, "x%d, %d", &rd, &imm);
        return ((imm&0xFFFFF)<<12)|(rd<<7)|0x17;
    }
    // J-type
    if (strcmp(mnemonic, "jal") == 0) {
        sscanf(args, "x%d, %d", &rd, &imm);
        return (((imm&0x100000)<<11)|((imm&0xFF000)>>12)|((imm&0x800)>>9)|((imm&0x7FE)<<20)|(rd<<7)|0x6F);
    }
    // Single-argument jal (for 'j' pseudo-instruction)
    if (strcmp(mnemonic, "jal") == 0) {
        sscanf(args, "%d", &imm);
        return (((imm&0x100000)<<11)|((imm&0xFF000)>>12)|((imm&0x800)>>9)|((imm&0x7FE)<<20)|(0<<7)|0x6F);
    }
    // NOP
    if (strcmp(mnemonic, "nop") == 0) {
        return 0x13; // addi x0, x0, 0
    }
    error("Unknown or unimplemented instruction", mnemonic);
    return 0;
}