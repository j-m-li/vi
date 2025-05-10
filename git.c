#include "tlse.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <zlib.h>
#include <stdint.h>

// ====================================================================
// --- GIT OBJECT CREATION, TREE, COMMIT, REFS ---
// ====================================================================

// SHA1 -> hex
void sha1_to_hex(const unsigned char sha1[20], char hex[41]) {
    for (int i = 0; i < 20; ++i) sprintf(hex + 2*i, "%02x", sha1[i]);
    hex[40] = 0;
}

// Write loose git object to .git/objects
void write_git_object(const unsigned char *object, size_t object_len, unsigned char sha1[20]) {
    char hex[41], dir[64], file[128];
    sha1_to_hex(sha1, hex);
    snprintf(dir, sizeof(dir), ".git/objects/%.2s", hex);
    snprintf(file, sizeof(file), ".git/objects/%.2s/%.38s", hex, hex+2);
    mkdir(".git", 0755);
    mkdir(".git/objects", 0755);
    mkdir(dir, 0755);
    FILE *f = fopen(file, "wb");
    if (f) {
        fwrite(object, 1, object_len, f);
        fclose(f);
        printf("Wrote object: %s\n", hex);
    } else {
        printf("Failed to write object file: %s\n", file);
    }
}

// Read HEAD ref into sha1 hex
int read_ref_head(char out_commit[41]) {
    FILE *f = fopen(".git/HEAD", "r");
    if (!f) return 0;
    char buf[256];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return 0; }
    fclose(f);
    if (strncmp(buf, "ref: ", 5) == 0) {
        char *ref = buf + 5;
        char *nl = strchr(ref, '\n');
        if (nl) *nl = 0;
        char ref_file[256];
        snprintf(ref_file, sizeof(ref_file), ".git/%s", ref);
        f = fopen(ref_file, "r");
        if (!f) return 0;
        if (!fgets(buf, sizeof(buf), f)) { fclose(f); return 0; }
        fclose(f);
        char *n2 = strchr(buf, '\n');
        if (n2) *n2 = 0;
        strncpy(out_commit, buf, 41);
        out_commit[40] = 0;
        return 1;
    }
    return 0;
}

// Update refs/heads/branch and HEAD
void update_refs(unsigned char commit_sha1[20], const char *branch) {
    char hex[41];
    sha1_to_hex(commit_sha1, hex);

    char path[128];
    snprintf(path, sizeof(path), ".git/refs/heads/%s", branch);
    mkdir(".git/refs", 0755);
    mkdir(".git/refs/heads", 0755);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s\n", hex);
        fclose(f);
        printf("Updated branch ref: %s -> %s\n", branch, hex);
    } else {
        printf("Failed to update ref: %s\n", path);
    }
    FILE *h = fopen(".git/HEAD", "w");
    if (h) {
        fprintf(h, "ref: refs/heads/%s\n", branch);
        fclose(h);
        printf("Updated HEAD to refs/heads/%s\n", branch);
    }
}

// Recursively create blobs and trees for all files/dirs
typedef struct tree_entry {
    char name[256];
    unsigned char sha1[20];
    unsigned int mode;
    int is_tree;
    struct tree_entry *next;
} tree_entry;

void create_blob(const char *filepath, unsigned char sha1[20]) {
    FILE *f = fopen(filepath, "rb");
    if (!f) { perror("blob fopen"); exit(1); }
    fseek(f, 0, SEEK_END);
    size_t filesize = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *filebuf = malloc(filesize);
    fread(filebuf, 1, filesize, f);
    fclose(f);

    char header[64];
    int header_len = snprintf(header, sizeof(header), "blob %zu%c", filesize, 0);

    unsigned char *object = malloc(header_len + filesize);
    memcpy(object, header, header_len);
    memcpy(object + header_len, filebuf, filesize);

    SHA1(object, header_len + filesize, sha1);
    write_git_object(object, header_len + filesize, sha1);

    free(filebuf);
    free(object);
}

// Write a tree object from entries
void create_tree_object(tree_entry *entries, int entry_count, unsigned char tree_sha1[20]) {
    // Build tree object
    char content[8192];
    int content_len = 0;
    for (tree_entry *e = entries; e; e = e->next) {
        int n = snprintf(content + content_len, sizeof(content) - content_len,
            "%o %s", e->mode, e->name);
        content_len += n;
        content[content_len++] = 0;
        memcpy(content + content_len, e->sha1, 20);
        content_len += 20;
    }
    char header[64];
    int header_len = snprintf(header, sizeof(header), "tree %d%c", content_len, 0);

    unsigned char *object = malloc(header_len + content_len);
    memcpy(object, header, header_len);
    memcpy(object + header_len, content, content_len);

    SHA1(object, header_len + content_len, tree_sha1);
    write_git_object(object, header_len + content_len, tree_sha1);
    free(object);
}

void free_tree_entries(tree_entry *head) {
    while (head) {
        tree_entry *next = head->next;
        free(head);
        head = next;
    }
}

// Recursively create tree for whole directory
void create_tree_recursive(const char *root_path, unsigned char tree_sha1[20]) {
    DIR *dir = opendir(root_path);
    if (!dir) {
        perror("tree opendir");
        exit(1);
    }
    struct dirent *entry;
    tree_entry *entries = NULL, **last = &entries;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".git") == 0)
            continue;
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", root_path, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) != 0) continue;

        tree_entry *te = calloc(1, sizeof(tree_entry));
        strncpy(te->name, entry->d_name, sizeof(te->name) - 1);

        if (S_ISDIR(st.st_mode)) {
            te->mode = 040000;
            te->is_tree = 1;
            create_tree_recursive(fullpath, te->sha1);
        } else if (S_ISREG(st.st_mode)) {
            te->mode = 0100644;
            te->is_tree = 0;
            create_blob(fullpath, te->sha1);
        } else {
            free(te);
            continue;
        }
        te->next = NULL;
        *last = te;
        last = &te->next;
    }
    closedir(dir);

    int entry_count = 0;
    for (tree_entry *te = entries; te; te = te->next) ++entry_count;
    create_tree_object(entries, entry_count, tree_sha1);

    free_tree_entries(entries);
}

// Create a commit object and write it
void create_commit(const char *tree_sha1_hex, const char *parent_sha1, const char *author, const char *message, unsigned char commit_sha1[20]) {
    time_t now = time(NULL);
    char timebuf[64];
    snprintf(timebuf, sizeof(timebuf), "%ld +0000", now);

    char commit_data[8192];
    int len = snprintf(commit_data, sizeof(commit_data),
        "tree %s\n"
        "%s"
        "author %s %s\n"
        "committer %s %s\n"
        "\n"
        "%s\n",
        tree_sha1_hex,
        (parent_sha1 && strlen(parent_sha1) == 40) ? (sprintf(timebuf+40, "parent %s\n", parent_sha1), timebuf+40) : "",
        author, timebuf,
        author, timebuf,
        message
    );

    char header[64];
    int header_len = snprintf(header, sizeof(header), "commit %d%c", len, 0);

    unsigned char *object = malloc(header_len + len);
    memcpy(object, header, header_len);
    memcpy(object + header_len, commit_data, len);

    SHA1(object, header_len + len, commit_sha1);
    write_git_object(object, header_len + len, commit_sha1);

    free(object);
}

// Make a commit from all files in a directory
void git_commit_tree(const char *rootdir, const char *author, const char *message, const char *branch) {
    unsigned char tree_sha1[20], commit_sha1[20];

    create_tree_recursive(rootdir, tree_sha1);

    char tree_sha1_hex[41];
    sha1_to_hex(tree_sha1, tree_sha1_hex);

    char parent_sha1[41];
    parent_sha1[0] = 0;
    int have_parent = read_ref_head(parent_sha1);

    create_commit(tree_sha1_hex, have_parent ? parent_sha1 : NULL, author, message, commit_sha1);

    update_refs(commit_sha1, branch);

    char commit_sha1_hex[41];
    sha1_to_hex(commit_sha1, commit_sha1_hex);
    printf("Commit created: %s\n", commit_sha1_hex);
}

// ====================================================================
// --- PACKFILE GENERATION FOR PUSH ---
// ====================================================================

// Helper: encode packfile object header
size_t write_pack_obj_hdr(unsigned char *buf, int type, size_t size) {
    size_t n = 0;
    unsigned char c = (type << 4) | (size & 0x0F);
    size >>= 4;
    if (size) c |= 0x80;
    buf[n++] = c;
    while (size) {
        c = size & 0x7F;
        size >>= 7;
        if (size) c |= 0x80;
        buf[n++] = c;
    }
    return n;
}

int git_obj_type(const char *objtype) {
    if (strcmp(objtype, "commit") == 0) return 1;
    if (strcmp(objtype, "tree") == 0)   return 2;
    if (strcmp(objtype, "blob") == 0)   return 3;
    if (strcmp(objtype, "tag") == 0)    return 4;
    return 0;
}

// Read a loose object and get type/uncompressed data/size
int read_loose_object(const char *objpath, char *out_type, unsigned char **out_data, size_t *out_size) {
    FILE *f = fopen(objpath, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *compressed = malloc(fsize);
    fread(compressed, 1, fsize, f);
    fclose(f);

    unsigned char *data = malloc(65536);
    z_stream s = {0};
    s.next_in = compressed;
    s.avail_in = fsize;
    s.next_out = data;
    s.avail_out = 65536;
    if (inflateInit(&s) != Z_OK) { free(compressed); free(data); return 0; }
    if (inflate(&s, Z_FINISH) != Z_STREAM_END) { free(compressed); free(data); inflateEnd(&s); return 0; }
    inflateEnd(&s);

    // Parse header: "<type> <size>\0"
    char *sp = memchr(data, ' ', s.total_out);
    char *zp = memchr(data, 0, s.total_out);
    if (!sp || !zp) { free(compressed); free(data); return 0; }
    size_t typelen = sp - (char*)data;
    strncpy(out_type, (char*)data, typelen); out_type[typelen] = 0;
    *out_data = malloc(s.total_out - (zp - (char*)data + 1));
    memcpy(*out_data, zp + 1, s.total_out - (zp - (char*)data + 1));
    *out_size = s.total_out - (zp - (char*)data + 1);
    free(compressed); free(data);
    return 1;
}

// Write a packfile with all loose objects in .git/objects/{??}/*
size_t create_packfile(unsigned char **pack_data_out) {
    DIR *d = opendir(".git/objects");
    if (!d) { perror("opendir .git/objects"); return 0; }
    unsigned char *pack = malloc(16 * 1024 * 1024); // 16MB buffer
    size_t packlen = 0, obj_count = 0;
    struct dirent *dent;

    // 1. Collect all object SHA1s
    char sha1s[1024][41]; int sha1cnt = 0;
    while ((dent = readdir(d))) {
        if (strlen(dent->d_name) != 2) continue;
        char subdir[128]; snprintf(subdir, sizeof(subdir), ".git/objects/%s", dent->d_name);
        DIR *sd = opendir(subdir);
        if (!sd) continue;
        struct dirent *sdent;
        while ((sdent = readdir(sd))) {
            if (strlen(sdent->d_name) != 38) continue;
            snprintf(sha1s[sha1cnt], 41, "%s%s", dent->d_name, sdent->d_name);
            sha1cnt++;
            if (sha1cnt > 1023) break;
        }
        closedir(sd);
    }
    closedir(d);

    // 2. Pack header
    pack[packlen++] = 'P'; pack[packlen++] = 'A'; pack[packlen++] = 'C'; pack[packlen++] = 'K';
    pack[packlen++] = 0; pack[packlen++] = 0; pack[packlen++] = 0; pack[packlen++] = 2; // version 2

    size_t obj_count_pos = packlen;
    pack[packlen++] = 0; pack[packlen++] = 0; pack[packlen++] = 0; pack[packlen++] = 0;

    // 3. For each object, read, compress, add to pack
    for (int i = 0; i < sha1cnt; ++i) {
        char path[128];
        snprintf(path, sizeof(path), ".git/objects/%.2s/%.38s", sha1s[i], sha1s[i] + 2);
        char objtype[16];
        unsigned char *data = NULL;
        size_t size = 0;
        if (!read_loose_object(path, objtype, &data, &size)) continue;
        int type = git_obj_type(objtype);

        // Object header (varint)
        unsigned char hdr[32];
        size_t hdrlen = write_pack_obj_hdr(hdr, type, size);
        memcpy(pack + packlen, hdr, hdrlen); packlen += hdrlen;

        // Compressed data
        unsigned char zbuf[65536];
        z_stream zs = {0};
        zs.next_in = data;
        zs.avail_in = size;
        zs.next_out = zbuf;
        zs.avail_out = sizeof(zbuf);
        deflateInit(&zs, Z_DEFAULT_COMPRESSION);
        deflate(&zs, Z_FINISH);
        deflateEnd(&zs);
        memcpy(pack + packlen, zbuf, zs.total_out); packlen += zs.total_out;

        free(data);
        obj_count++;
    }
    // Write actual obj_count
    pack[obj_count_pos+0] = (obj_count >> 24) & 0xff;
    pack[obj_count_pos+1] = (obj_count >> 16) & 0xff;
    pack[obj_count_pos+2] = (obj_count >> 8) & 0xff;
    pack[obj_count_pos+3] = obj_count & 0xff;

    // 4. SHA1 of the packfile contents
    unsigned char sha1[20];
    SHA1(pack, packlen, sha1);
    memcpy(pack + packlen, sha1, 20); packlen += 20;

    *pack_data_out = pack;
    return packlen;
}

// ====================================================================
// --- PACKFILE UNPACKING WITH DELTA SUPPORT FOR PULL ---
// ====================================================================

// Helper: parse variable-length int (for delta and header)
static size_t get_varint(const unsigned char **p);
// ... see above, already defined ...

// Structure for holding unpacked objects (for delta resolution)
// ... see above, already defined ...

// Find object by offset (for OFS_DELTA)
// ... see above, already defined ...

// Find object by SHA1 (for REF_DELTA)
// ... see above, already defined ...

// Apply a Git delta stream
// ... see above, already defined ...

// Write loose object to .git/objects/
// ... see above, already defined ...

// Unpack a .pack file into .git/objects/, resolving deltas
int unpack_packfile(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) { perror("fopen"); return 2; }
    fseek(f, 0, SEEK_END);
    size_t packlen = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *pack = malloc(packlen);
    fread(pack, 1, packlen, f);
    fclose(f);

    if (memcmp(pack, "PACK", 4) != 0) {
        printf("Not a packfile!\n");
        free(pack); return 2;
    }
    unsigned int version = (pack[4]<<24)|(pack[5]<<16)|(pack[6]<<8)|pack[7];
    unsigned int count = (pack[8]<<24)|(pack[9]<<16)|(pack[10]<<8)|pack[11];
    printf("Packfile: version %u, %u objects\n", version, count);

    size_t p = 12;
    unpacked_obj *objs = NULL, *last = NULL;

    for (unsigned int i = 0; i < count; ++i) {
        size_t obj_offset = p;

        // Parse type/size varint
        unsigned char c = pack[p++];
        int type = (c >> 4) & 7;
        size_t size = c & 0x0F;
        int shift = 4;
        while (c & 0x80) {
            c = pack[p++];
            size |= ((c & 0x7F) << shift);
            shift += 7;
        }

        char *typename = NULL;
        int is_delta = 0;
        unpacked_obj *base_obj = NULL;
        unsigned char base_sha1[20];
        size_t base_offset = 0;

        if (type == 1) typename = "commit";
        else if (type == 2) typename = "tree";
        else if (type == 3) typename = "blob";
        else if (type == 4) typename = "tag";
        else if (type == 6) { typename = NULL; is_delta = 1; } // OFS_DELTA
        else if (type == 7) { typename = NULL; is_delta = 2; } // REF_DELTA
        else {
            printf("Unsupported object type %d\n", type);
            free(pack); return 3;
        }

        // For delta objects, parse base reference
        if (is_delta == 1) {
            // OFS_DELTA: offset encoded as variable-length int
            size_t off = 0;
            int shift = 0;
            unsigned char c;
            c = pack[p++];
            off = c & 0x7f;
            while (c & 0x80) {
                off += 1;
                c = pack[p++];
                off = (off << 7) + (c & 0x7f);
            }
            base_offset = obj_offset - off;
        } else if (is_delta == 2) {
            memcpy(base_sha1, pack + p, 20);
            p += 20;
        }

        // Zlib decompress
        z_stream zs = {0};
        zs.next_in = pack + p;
        zs.avail_in = packlen - p;
        unsigned char *out = malloc(size + 65536);
        zs.next_out = out;
        zs.avail_out = size + 65536;
        inflateInit(&zs);
        int ret = inflate(&zs, Z_FINISH);
        size_t outlen = zs.total_out;
        p += zs.total_in;
        inflateEnd(&zs);
        if (ret != Z_STREAM_END) {
            printf("Decompression error\n");
            free(out); free(pack); return 4;
        }

        unsigned char *final_data = NULL;
        size_t final_size = 0;
        char out_type[8];

        if (!is_delta) {
            // Write as is, and remember for delta
            final_data = out;
            final_size = outlen;
            strncpy(out_type, typename, sizeof(out_type) - 1);
            out_type[sizeof(out_type)-1] = 0;
        } else {
            // Get base data
            unpacked_obj *base = NULL;
            if (is_delta == 1) {
                base = find_obj_by_offset(objs, base_offset);
            } else if (is_delta == 2) {
                base = find_obj_by_sha1(objs, base_sha1);
            }
            if (!base) {
                printf("Delta base object not found (offset=%zu, sha1=%02x...)\n", base_offset, base_sha1[0]);
                free(out); free(pack); return 5;
            }
            final_data = apply_delta(base->data, base->size, out, outlen, &final_size);
            if (!final_data) { free(out); free(pack); return 6; }
            strncpy(out_type, base->type, sizeof(out_type) - 1);
            out_type[sizeof(out_type)-1] = 0;
            free(out);
        }

        unsigned char sha1[20];
        write_git_object(final_data, final_size, sha1);

        // Remember for future deltas
        unpacked_obj *o = calloc(1, sizeof(unpacked_obj));
        strncpy(o->type, out_type, sizeof(o->type)-1);
        memcpy(o->sha1, sha1, 20);
        o->data = final_data;
        o->size = final_size;
        o->pack_offset = obj_offset;
        o->next = NULL;
        if (!objs) { objs = last = o; }
        else { last->next = o; last = o; }
    }

    // Clean up memory
    for (unpacked_obj *o = objs; o;) {
        unpacked_obj *n = o->next;
        free(o->data);
        free(o);
        o = n;
    }
    free(pack);
    printf("Unpack done!\n");
    return 0;
}

// ====================================================================
// --- HTTPS CLIENT HELPERS ---
// ====================================================================

int tcp_connect(const char *host, int port) {
    struct hostent *he = gethostbyname(host);
    if (!he) return -1;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock); return -1;
    }
    return sock;
}

int tls_http_request(const char *host, int port, const char *request, char *response, size_t response_sz) {
    int sock = tcp_connect(host, port);
    if (sock < 0) {
        fprintf(stderr, "Failed to connect to %s:%d\n", host, port);
        return -1;
    }
    struct TLSContext *ctx = tls_create_context(0, TLS_V12);
    ctx->fd = sock;
    if (tls_connect(ctx, sock, host, port) != 0) {
        fprintf(stderr, "TLS handshake failed\n");
        close(sock); tls_destroy(ctx); return -2;
    }
    tls_write(ctx, (const unsigned char*)request, strlen(request));
    int n = tls_read(ctx, (unsigned char*)response, response_sz-1);
    if (n > 0) response[n] = 0;
    else response[0] = 0;
    tls_destroy(ctx);
    close(sock);
    return n;
}

// ====================================================================
// --- PUSH AND PULL COMMANDS (with pack/unpack) ---
// ====================================================================

// Find branch SHA1 in info/refs
int find_branch_sha1_in_info_refs(const char *info_refs, const char *branch, char out_sha1[41]) {
    const char *line = info_refs;
    while ((line = strstr(line, branch))) {
        const char *p = line;
        while (p > info_refs && *(p-1) != '\n') --p;
        if (p + 40 <= line && sscanf(p, "%40[0-9a-f]", out_sha1) == 1) {
            out_sha1[40] = 0;
            return 1;
        }
        line += strlen(branch);
    }
    return 0;
}

// GIT PUSH LOGIC: full packfile, push to remote via HTTP
void git_push(const char *host, const char *repo_path, const char *branch) {
    // 1. GET remote refs
    char req[1024], resp[8192];
    snprintf(req, sizeof(req),
        "GET %s/info/refs?service=git-receive-pack HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: git/2.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n", repo_path, host);
    tls_http_request(host, 443, req, resp, sizeof(resp));
    printf("Remote refs:\n%s\n", resp);

    // 2. Get local commit to push
    char commit_sha1[41], parent_sha1[41];
    if (!read_ref_head(commit_sha1)) {
        printf("No commit to push!\n");
        return;
    }
    parent_sha1[0] = 0;

    // 3. Find old_sha for this branch in remote refs
    char *line = strstr(resp, branch);
    char old_sha[41];
    if (line && (line - resp) > 40) {
        strncpy(old_sha, line - 41, 40);
        old_sha[40] = 0;
    } else {
        strcpy(old_sha, "0000000000000000000000000000000000000000");
    }

    // 4. Build update pkt-line
    char update_line[128];
    snprintf(update_line, sizeof(update_line), "%s %s refs/heads/%s\x00report-status side-band-64k agent=git/2.0\n", old_sha, commit_sha1, branch);
    size_t update_line_len = strlen(update_line);
    char pkt_line[1024];
    int pkt_len = snprintf(pkt_line, sizeof(pkt_line), "%04x%s", (unsigned)(update_line_len + 4), update_line);
    strcat(pkt_line, "0000");

    // 5. Generate packfile with all objects
    unsigned char *pack_data;
    size_t pack_len = create_packfile(&pack_data);

    // 6. Compose POST body: pkt-line refs + flush + packfile
    size_t pkt_line_len = strlen(pkt_line);
    size_t payload_len = pkt_line_len + pack_len;
    unsigned char *payload = malloc(payload_len);
    memcpy(payload, pkt_line, pkt_line_len);
    memcpy(payload + pkt_line_len, pack_data, pack_len);

    // 7. HTTP POST to /repo_path/git-receive-pack
    snprintf(req, sizeof(req),
        "POST %s/git-receive-pack HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: git/2.0\r\n"
        "Accept: application/x-git-receive-pack-result\r\n"
        "Content-Type: application/x-git-receive-pack-request\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n", repo_path, host, payload_len);

    size_t req_len = strlen(req) + payload_len;
    unsigned char *full_req = malloc(req_len + 1);
    memcpy(full_req, req, strlen(req));
    memcpy(full_req + strlen(req), payload, payload_len);
    full_req[req_len] = 0;

    // 8. Send
    char push_resp[32768];
    tls_http_request(host, 443, (const char *)full_req, push_resp, sizeof(push_resp));
    printf("Push response:\n%s\n", push_resp);

    free(pack_data);
    free(payload);
    free(full_req);
}

// GIT PULL LOGIC: fetch packfile, unpack with delta support
void git_pull(const char *host, const char *repo_path, const char *branch) {
    char req[1024], resp[65536];
    // 1. GET /info/refs?service=git-upload-pack
    snprintf(req, sizeof(req),
        "GET %s/info/refs?service=git-upload-pack HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: git/2.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n", repo_path, host);

    if (tls_http_request(host, 443, req, resp, sizeof(resp)) <= 0) {
        printf("Failed to get remote refs\n");
        return;
    }
    printf("Remote refs:\n%s\n", resp);

    // 2. Find branch SHA1
    char want_sha1[41];
    if (!find_branch_sha1_in_info_refs(resp, branch, want_sha1)) {
        printf("Could not find branch '%s' in remote refs.\n", branch);
        return;
    }
    printf("Remote branch '%s' SHA1: %s\n", branch, want_sha1);

    // 3. Compose pkt-line "want"
    char want_line[64];
    snprintf(want_line, sizeof(want_line), "want %s\n", want_sha1);
    int want_len = strlen(want_line);
    char pkt_want[128];
    snprintf(pkt_want, sizeof(pkt_want), "%04x%s0000", (unsigned)(want_len + 4), want_line);

    // 4. POST to /git-upload-pack
    snprintf(req, sizeof(req),
        "POST %s/git-upload-pack HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: git/2.0\r\n"
        "Accept: application/x-git-upload-pack-result\r\n"
        "Content-Type: application/x-git-upload-pack-request\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n%s", repo_path, host, strlen(pkt_want), pkt_want);

    char *recv_buf = malloc(16 * 1024 * 1024);
    int n = tls_http_request(host, 443, req, recv_buf, 16 * 1024 * 1024);
    if (n <= 0) {
        printf("Failed to fetch packfile.\n");
        free(recv_buf);
        return;
    }

    // 5. Find start of "PACK"
    char *pack_start = memmem(recv_buf, n, "PACK", 4);
    if (!pack_start) {
        printf("No packfile found in response.\n");
        free(recv_buf);
        return;
    }
    size_t pack_offset = pack_start - recv_buf;
    size_t pack_size = n - pack_offset;
    FILE *f = fopen("received.pack", "wb");
    if (!f) {
        perror("fopen received.pack");
        free(recv_buf);
        return;
    }
    fwrite(pack_start, 1, pack_size, f);
    fclose(f);

    printf("Packfile received and saved as received.pack (%zu bytes)\n", pack_size);

    // 6. Unpack into .git/objects/
    printf("Unpacking received.pack ...\n");
    unpack_packfile("received.pack");

    free(recv_buf);
}

// ====================================================================
// --- MAIN PROGRAM ---
// ====================================================================

int main(int argc, char **argv) {
    if (argc >= 5 && strcmp(argv[1], "commit-tree") == 0) {
        git_commit_tree(argv[2], argv[3], argv[4], argv[5]);
        return 0;
    }
    if (argc >= 5 && strcmp(argv[1], "push") == 0) {
        git_push(argv[2], argv[3], argv[4]);
        return 0;
    }
    if (argc >= 5 && strcmp(argv[1], "pull") == 0) {
        git_pull(argv[2], argv[3], argv[4]);
        return 0;
    }
    printf("Usage:\n");
    printf("  %s commit-tree <directory> <author> <message> <branch>\n", argv[0]);
    printf("  %s push <host> <repo_path> <branch>\n", argv[0]);
    printf("  %s pull <host> <repo_path> <branch>\n", argv[0]);
    printf("Example: %s commit-tree . \"Your Name <you@host>\" \"msg\" master\n", argv[0]);
    printf("Example: %s push github.com /j-m-li/test-repo master\n", argv[0]);
    printf("Example: %s pull github.com /j-m-li/test-repo master\n", argv[0]);
    return 1;
}