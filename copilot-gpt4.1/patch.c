/******************************************************************************
 *                       OS-3o3 operating system
 * 
 *                             patch utility
 *
 *            9 May MMXXV PUBLIC DOMAIN by Jean-Marc Lienher
 *
 *        The authors disclaim copyright and patents to this software.
 * 
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_BUFFER_SIZE 1024

/**
 * Load a file into memory.
 */
char *load_file(const char *path, size_t *size) {
    FILE *fp = fopen(path, "rb");
    char *buffer = NULL;

    if (!fp) {
        fprintf(stderr, "ERROR: Cannot open file %s\n", path);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    *size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    buffer = malloc(*size + 1);
    if (!buffer) {
        fprintf(stderr, "ERROR: Memory allocation failed\n");
        fclose(fp);
        return NULL;
    }

    fread(buffer, 1, *size, fp);
    buffer[*size] = '\0';

    fclose(fp);
    return buffer;
}

/**
 * Save a buffer to a file.
 */
int save_file(const char *path, const char *buffer, size_t size) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot open file %s for writing\n", path);
        return -1;
    }

    fwrite(buffer, 1, size, fp);
    fclose(fp);
    return 0;
}

/**
 * Apply a patch to a file.
 */
int apply_patch(const char *original_file, const char *patch_file, const char *output_file) {
    FILE *patch_fp = fopen(patch_file, "r");
    if (!patch_fp) {
        fprintf(stderr, "ERROR: Cannot open patch file %s\n", patch_file);
        return -1;
    }

    size_t original_size;
    char *original_content = load_file(original_file, &original_size);
    if (!original_content) {
        fclose(patch_fp);
        return -1;
    }

    char **lines = NULL;
    size_t line_count = 0;

    char line[LINE_BUFFER_SIZE];
    while (fgets(line, sizeof(line), patch_fp)) {
        if (line[0] == '-' || line[0] == '+') {
            lines = realloc(lines, (line_count + 1) * sizeof(char *));
            lines[line_count] = strdup(line);
            line_count++;
        }
    }

    fclose(patch_fp);

    // Apply patch logic
    char *patched_content = malloc(original_size + 1);
    if (!patched_content) {
        fprintf(stderr, "ERROR: Memory allocation failed\n");
        free(original_content);
        return -1;
    }

    size_t patched_size = 0;

    for (size_t i = 0; i < line_count; i++) {
        if (lines[i][0] == '-') {
            // Skip lines that are marked for deletion
        } else if (lines[i][0] == '+') {
            // Append new lines
            size_t len = strlen(lines[i] + 1);
            memcpy(patched_content + patched_size, lines[i] + 1, len);
            patched_size += len;
        } else {
            // Copy unchanged lines
            size_t len = strlen(lines[i]);
            memcpy(patched_content + patched_size, lines[i], len);
            patched_size += len;
        }
    }

    patched_content[patched_size] = '\0';

    // Save patched content
    int result = save_file(output_file, patched_content, patched_size);

    // Cleanup
    free(original_content);
    free(patched_content);
    for (size_t i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);

    return result;
}

/**
 * Main function for the patch utility.
 */
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <original_file> <patch_file> <output_file>\n", argv[0]);
        return -1;
    }

    const char *original_file = argv[1];
    const char *patch_file = argv[2];
    const char *output_file = argv[3];

    return apply_patch(original_file, patch_file, output_file);
}