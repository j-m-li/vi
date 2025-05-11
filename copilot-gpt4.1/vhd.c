#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FOOTER_SIZE 512

// Helper to write the VHD footer (little-endian for integers)
void write_footer(FILE *fp, unsigned long long total_size) {
    unsigned char footer[FOOTER_SIZE];
    memset(footer, 0, FOOTER_SIZE);

    // Cookie
    memcpy(footer + 0, "conectix", 8);

    // Features
    footer[8] = 0x00; footer[9] = 0x00; footer[10] = 0x00; footer[11] = 0x02;

    // File Format Version
    footer[12] = 0x00; footer[13] = 0x01; footer[14] = 0x00; footer[15] = 0x00;

    // Data Offset (fixed disk: 0xFFFFFFFFFFFFFFFF)
    memset(footer + 16, 0xFF, 8);

    // Timestamp (seconds since Jan 1, 2000)
    time_t now = time(NULL);
    unsigned int vhd_time = (unsigned int)(now - 946684800U);
    footer[24] = (vhd_time >> 24) & 0xFF;
    footer[25] = (vhd_time >> 16) & 0xFF;
    footer[26] = (vhd_time >> 8) & 0xFF;
    footer[27] = vhd_time & 0xFF;

    // Creator Application "C90 "
    memcpy(footer + 28, "C90 ", 4);

    // Creator Version
    footer[32] = 0x00; footer[33] = 0x01; footer[34] = 0x00; footer[35] = 0x00;

    // Creator OS "Wi2k"
    memcpy(footer + 36, "Wi2k", 4);

    // Original Size (8 bytes)
    footer[40] = (total_size >> 56) & 0xFF;
    footer[41] = (total_size >> 48) & 0xFF;
    footer[42] = (total_size >> 40) & 0xFF;
    footer[43] = (total_size >> 32) & 0xFF;
    footer[44] = (total_size >> 24) & 0xFF;
    footer[45] = (total_size >> 16) & 0xFF;
    footer[46] = (total_size >> 8) & 0xFF;
    footer[47] = total_size & 0xFF;

    // Current Size (8 bytes)
    memcpy(footer + 48, footer + 40, 8);

    // Disk Geometry (Cylinder/Heads/Sectors, fake but legal: 16383/16/63)
    footer[56] = 0x3F; // Sectors
    footer[57] = 0x10; // Heads
    footer[58] = 0x3F; footer[59] = 0xFF; // Cylinders

    // Disk Type (2 = fixed)
    footer[60] = 0x00; footer[61] = 0x00; footer[62] = 0x00; footer[63] = 0x02;

    // Checksum (set to zero, then fill in below)
    memset(footer + 64, 0, 4);

    // Unique ID (16 random bytes)
    int i;
    srand((unsigned int)now);
    for (i = 0; i < 16; ++i)
        footer[68 + i] = rand() & 0xFF;

    // Saved State (0)
    footer[84] = 0;

    // Calculate checksum (one's complement of the sum of all bytes)
    unsigned int sum = 0;
    for (i = 0; i < FOOTER_SIZE; ++i)
        if (i < 64 || i > 67) // Exclude checksum field itself
            sum += footer[i];
    sum = ~sum;
    footer[64] = (sum >> 24) & 0xFF;
    footer[65] = (sum >> 16) & 0xFF;
    footer[66] = (sum >> 8) & 0xFF;
    footer[67] = sum & 0xFF;

    // Write footer
    fseek(fp, 0, SEEK_END);
    fwrite(footer, 1, FOOTER_SIZE, fp);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <blocks_512B> <output.vhd>\n", argv[0]);
        return 1;
    }

    unsigned long long blocks = strtoull(argv[1], NULL, 10);
    if (blocks == 0) {
        printf("Error: blocks must be > 0\n");
        return 1;
    }
    unsigned long long disk_size = blocks * 512ULL;

    // Open file for writing
    FILE *fp = fopen(argv[2], "wb");
    if (!fp) {
        printf("Error: Cannot open output file.\n");
        return 1;
    }

    // Allocate zeros for the disk
    char zeros[512];
    memset(zeros, 0, 512);

    unsigned long long i;
    for (i = 0; i < blocks; ++i) {
        if (fwrite(zeros, 1, 512, fp) != 512) {
            printf("Error: Write failed.\n");
            fclose(fp);
            return 1;
        }
    }

    // Write the VHD footer at the end
    write_footer(fp, disk_size);

    fclose(fp);
    printf("Created %s with size %llu bytes (%llu blocks of 512 bytes)\n", argv[2], disk_size + FOOTER_SIZE, blocks);
    return 0;
}