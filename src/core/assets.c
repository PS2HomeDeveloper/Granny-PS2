#include "assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPAK_VERSION 2u
#define GPAK_HEADER_SIZE 24u
#define GPAK_PATH_SIZE 256u
#define GPAK_ENTRY_SIZE 272u
#define GPAK_MAX_ENTRIES 32768u
#define GPAK_MAX_FILE_SIZE 0x7fffffffu

typedef struct AssetEntry {
    char path[GPAK_PATH_SIZE];
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
    unsigned char *data;
    GrAsset asset;
} AssetEntry;

static FILE *package_file;
static AssetEntry *entries;
static uint32_t entry_count;
static uint32_t package_size;
static char error_text[192] = "resource package is not initialized";
static uint32_t crc_table[256];
static int crc_table_ready;

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t crc32_bytes(const unsigned char *data, uint32_t size)
{
    uint32_t crc = 0xffffffffu;
    uint32_t i;
    if (!crc_table_ready) {
        unsigned n;
        for (n = 0; n < 256; ++n) {
            uint32_t value = n;
            unsigned bit;
            for (bit = 0; bit < 8; ++bit)
                value = (value >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(value & 1u));
            crc_table[n] = value;
        }
        crc_table_ready = 1;
    }
    for (i = 0; i < size; ++i) {
        crc = crc_table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffu;
}

static int read_exact(FILE *file, void *buffer, size_t size)
{
    return size == 0 || fread(buffer, 1, size, file) == size;
}

static int valid_path(const char *path)
{
    const char *part = path;
    if (!path || !*path || path[0] == '/' || path[0] == '\\') return 0;
    while (*part) {
        const char *end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if (!length || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.')) return 0;
        if (!end) break;
        part = end + 1;
    }
    return 1;
}

static int load_entry(AssetEntry *entry)
{
    unsigned char *data;
    if (entry->data) return 1;
    if (!package_file || entry->offset > package_size ||
        entry->size > package_size - entry->offset ||
        entry->offset > GPAK_MAX_FILE_SIZE) {
        snprintf(error_text, sizeof(error_text), "invalid package range for %.120s", entry->path);
        return 0;
    }
    data = (unsigned char *)malloc(entry->size ? entry->size : 1u);
    if (!data) {
        snprintf(error_text, sizeof(error_text), "not enough memory to load %.120s (%u bytes)",
                 entry->path, (unsigned)entry->size);
        return 0;
    }
    if (fseek(package_file, (long)entry->offset, SEEK_SET) != 0 ||
        !read_exact(package_file, data, entry->size)) {
        free(data);
        snprintf(error_text, sizeof(error_text), "failed to read %.120s from DATA.PAK", entry->path);
        return 0;
    }
    if (crc32_bytes(data, entry->size) != entry->crc32) {
        free(data);
        snprintf(error_text, sizeof(error_text), "CRC mismatch in DATA.PAK resource %.120s", entry->path);
        return 0;
    }
    entry->data = data;
    entry->asset.name = entry->path;
    entry->asset.data = data;
    entry->asset.size = entry->size;
    return 1;
}

void assets_shutdown(void)
{
    uint32_t i;
    if (entries) {
        for (i = 0; i < entry_count; ++i)
            free(entries[i].data);
        free(entries);
    }
    if (package_file) fclose(package_file);
    package_file = NULL;
    entries = NULL;
    entry_count = 0;
    package_size = 0;
}

int assets_init_external(const char *package_path)
{
    unsigned char header[GPAK_HEADER_SIZE];
    unsigned char *raw_index = NULL;
    uint32_t count, index_offset, entry_size, declared_size, index_bytes, data_start, expected_offset, i;
    long measured_size;
    AssetEntry *new_entries = NULL;

    assets_shutdown();
    snprintf(error_text, sizeof(error_text), "resource package is not initialized");
    if (!package_path || !*package_path) {
        snprintf(error_text, sizeof(error_text), "DATA.PAK path is empty");
        return 0;
    }
    package_file = fopen(package_path, "rb");
    if (!package_file) {
        snprintf(error_text, sizeof(error_text), "cannot open %s", package_path);
        return 0;
    }
    if (fseek(package_file, 0, SEEK_END) != 0 || (measured_size = ftell(package_file)) < 0 ||
        measured_size > (long)GPAK_MAX_FILE_SIZE || fseek(package_file, 0, SEEK_SET) != 0 ||
        !read_exact(package_file, header, sizeof(header))) {
        snprintf(error_text, sizeof(error_text), "cannot read DATA.PAK header or file is too large");
        assets_shutdown();
        return 0;
    }
    if (memcmp(header, "GPAK", 4) != 0 || read_u32(header + 4) != GPAK_VERSION) {
        snprintf(error_text, sizeof(error_text), "unsupported DATA.PAK signature or version");
        assets_shutdown();
        return 0;
    }
    count = read_u32(header + 8);
    index_offset = read_u32(header + 12);
    entry_size = read_u32(header + 16);
    declared_size = read_u32(header + 20);
    if (!count || count > GPAK_MAX_ENTRIES || entry_size != GPAK_ENTRY_SIZE ||
        declared_size != (uint32_t)measured_size || index_offset < GPAK_HEADER_SIZE ||
        index_offset > declared_size || count > (declared_size - index_offset) / entry_size) {
        snprintf(error_text, sizeof(error_text), "DATA.PAK index metadata is invalid");
        assets_shutdown();
        return 0;
    }
    index_bytes = count * entry_size;
    data_start = index_offset + index_bytes;
    if (data_start < index_offset || data_start > declared_size ||
        fseek(package_file, (long)index_offset, SEEK_SET) != 0) {
        snprintf(error_text, sizeof(error_text), "DATA.PAK index is outside the file");
        assets_shutdown();
        return 0;
    }
    expected_offset = data_start;
    raw_index = (unsigned char *)malloc(index_bytes);
    new_entries = (AssetEntry *)calloc(count, sizeof(*new_entries));
    if (!raw_index || !new_entries || !read_exact(package_file, raw_index, index_bytes)) {
        free(raw_index);
        free(new_entries);
        snprintf(error_text, sizeof(error_text), "not enough memory or cannot read DATA.PAK index");
        assets_shutdown();
        return 0;
    }
    for (i = 0; i < count; ++i) {
        const unsigned char *record = raw_index + i * GPAK_ENTRY_SIZE;
        const unsigned char *terminator = (const unsigned char *)memchr(record, 0, GPAK_PATH_SIZE);
        uint32_t offset = read_u32(record + GPAK_PATH_SIZE);
        uint32_t size = read_u32(record + GPAK_PATH_SIZE + 4);
        uint32_t crc = read_u32(record + GPAK_PATH_SIZE + 8);
        size_t path_len;
        if (!terminator) break;
        path_len = (size_t)(terminator - record);
        memcpy(new_entries[i].path, record, path_len);
        new_entries[i].path[path_len] = '\0';
        if (!valid_path(new_entries[i].path) || offset != expected_offset || offset > declared_size ||
            size > declared_size - offset || (i && strcmp(new_entries[i - 1].path, new_entries[i].path) >= 0))
            break;
        new_entries[i].offset = offset;
        new_entries[i].size = size;
        new_entries[i].crc32 = crc;
        expected_offset = offset + size;
    }
    free(raw_index);
    if (i != count || expected_offset != declared_size) {
        free(new_entries);
        snprintf(error_text, sizeof(error_text), "DATA.PAK has an invalid, unsafe, or duplicate path entry");
        assets_shutdown();
        return 0;
    }
    entries = new_entries;
    entry_count = count;
    package_size = declared_size;
    error_text[0] = '\0';
    return 1;
}

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

const GrAsset *assets_find(const char *path_or_unique_basename)
{
    uint32_t i, found = 0;
    AssetEntry *match = NULL;
    int is_path;
    if (!path_or_unique_basename || !entries) return NULL;
    is_path = strchr(path_or_unique_basename, '/') != NULL;
    for (i = 0; i < entry_count; ++i) {
        int same = is_path ? strcmp(entries[i].path, path_or_unique_basename) == 0 :
                   strcmp(basename_of(entries[i].path), path_or_unique_basename) == 0;
        if (same) {
            match = &entries[i];
            ++found;
            if (is_path) break;
        }
    }
    if (!match) {
        snprintf(error_text, sizeof(error_text), "resource not found: %.140s", path_or_unique_basename);
        return NULL;
    }
    if (found > 1) {
        snprintf(error_text, sizeof(error_text), "resource basename is ambiguous; use its folder path: %.130s",
                 path_or_unique_basename);
        return NULL;
    }
    if (!load_entry(match)) return NULL;
    error_text[0] = '\0';
    return &match->asset;
}

static int ascii_equal_fold(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

const GrAsset *assets_find_texture(const char *source_texture_name)
{
    char stem[GPAK_PATH_SIZE];
    const char *base, *dot;
    size_t len;
    uint32_t i;
    AssetEntry *best = NULL;
    unsigned best_rank = 0, best_count = 0;
    if (!source_texture_name || !entries) return NULL;
    base = basename_of(source_texture_name);
    dot = strrchr(base, '.');
    len = dot ? (size_t)(dot - base) : strlen(base);
    if (!len || len >= sizeof(stem)) {
        snprintf(error_text, sizeof(error_text), "invalid texture name in material");
        return NULL;
    }
    memcpy(stem, base, len);
    stem[len] = '\0';
    for (i = 0; i < entry_count; ++i) {
        const char *file = basename_of(entries[i].path);
        const char *ext = strrchr(file, '.');
        size_t file_stem;
        unsigned rank;
        char candidate[GPAK_PATH_SIZE];
        if (!ext || !(ascii_equal_fold(ext, ".tm2") || ascii_equal_fold(ext, ".tim2"))) continue;
        file_stem = (size_t)(ext - file);
        if (file_stem >= sizeof(candidate)) continue;
        memcpy(candidate, file, file_stem);
        candidate[file_stem] = '\0';
        if (!ascii_equal_fold(candidate, stem)) continue;
        rank = strncmp(entries[i].path, "3d/textures/", 12) == 0 ? 2u : 1u;
        if (rank > best_rank) {
            best = &entries[i];
            best_rank = rank;
            best_count = 1;
        } else if (rank == best_rank) {
            ++best_count;
        }
    }
    if (!best) {
        snprintf(error_text, sizeof(error_text), "TIM2 texture not found for material reference: %.130s",
                 source_texture_name);
        return NULL;
    }
    if (best_count != 1) {
        snprintf(error_text, sizeof(error_text), "TIM2 texture name is ambiguous: %.140s", source_texture_name);
        return NULL;
    }
    if (!load_entry(best)) return NULL;
    error_text[0] = '\0';
    return &best->asset;
}

const char *assets_error(void)
{
    return error_text[0] ? error_text : "unknown resource error";
}
