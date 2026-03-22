#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb_image_write.h"
#include "stb_rect_pack.h"
#include "stb_truetype.h"

#define FONT_BUFFER_PADDING 1024u

typedef struct {
    size_t offset;
    size_t length;
    int found;
} sfnt_table;

static uint16_t read_u16be(const unsigned char *buf) {
    return (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
}

static uint32_t read_u32be(const unsigned char *buf) {
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           (uint32_t)buf[3];
}

static int has_sfnt_signature(const unsigned char *buf, size_t len) {
    if (len < 4) return 0;
    if (memcmp(buf, "\x00\x01\x00\x00", 4) == 0) return 1;
    if (memcmp(buf, "OTTO", 4) == 0) return 1;
    if (memcmp(buf, "true", 4) == 0) return 1;
    if (memcmp(buf, "typ1", 4) == 0) return 1;
    return 0;
}

static void record_table(sfnt_table *table, size_t offset, size_t length) {
    table->found = 1;
    table->offset = offset;
    table->length = length;
}

static int validate_sfnt_font(const unsigned char *buf, size_t len) {
    sfnt_table cmap = {0, 0, 0};
    sfnt_table head = {0, 0, 0};
    sfnt_table hhea = {0, 0, 0};
    sfnt_table hmtx = {0, 0, 0};
    sfnt_table maxp = {0, 0, 0};
    sfnt_table loca = {0, 0, 0};
    sfnt_table glyf = {0, 0, 0};
    sfnt_table cff = {0, 0, 0};
    sfnt_table cff2 = {0, 0, 0};
    uint16_t num_tables;
    size_t directory_size;
    uint16_t num_glyphs;
    uint16_t num_hmetrics;
    uint16_t index_to_loc_format;

    if (len < 12 || !has_sfnt_signature(buf, len)) {
        return 0;
    }

    num_tables = read_u16be(buf + 4);
    if (num_tables == 0) {
        return 0;
    }
    if ((size_t)num_tables > (len - 12u) / 16u) {
        return 0;
    }

    directory_size = 12u + (size_t)num_tables * 16u;
    if (directory_size > len) {
        return 0;
    }

    for (uint16_t i = 0; i < num_tables; i++) {
        const unsigned char *record = buf + 12u + (size_t)i * 16u;
        size_t offset = (size_t)read_u32be(record + 8);
        size_t length = (size_t)read_u32be(record + 12);

        if (offset > len || length > len - offset) {
            return 0;
        }

        if (memcmp(record, "cmap", 4) == 0) record_table(&cmap, offset, length);
        else if (memcmp(record, "head", 4) == 0) record_table(&head, offset, length);
        else if (memcmp(record, "hhea", 4) == 0) record_table(&hhea, offset, length);
        else if (memcmp(record, "hmtx", 4) == 0) record_table(&hmtx, offset, length);
        else if (memcmp(record, "maxp", 4) == 0) record_table(&maxp, offset, length);
        else if (memcmp(record, "loca", 4) == 0) record_table(&loca, offset, length);
        else if (memcmp(record, "glyf", 4) == 0) record_table(&glyf, offset, length);
        else if (memcmp(record, "CFF ", 4) == 0) record_table(&cff, offset, length);
        else if (memcmp(record, "CFF2", 4) == 0) record_table(&cff2, offset, length);
    }

    if (!cmap.found || !head.found || !hhea.found || !hmtx.found || !maxp.found) {
        return 0;
    }
    if (cmap.length < 4u || head.length < 54u || hhea.length < 36u || maxp.length < 6u) {
        return 0;
    }

    num_glyphs = read_u16be(buf + maxp.offset + 4u);
    num_hmetrics = read_u16be(buf + hhea.offset + 34u);
    index_to_loc_format = read_u16be(buf + head.offset + 50u);

    if (num_glyphs == 0 || num_hmetrics == 0 || num_hmetrics > num_glyphs) {
        return 0;
    }

    if (hmtx.length < (size_t)num_hmetrics * 4u + (size_t)(num_glyphs - num_hmetrics) * 2u) {
        return 0;
    }

    if (loca.found || glyf.found) {
        size_t entries = (size_t)num_glyphs + 1u;
        size_t entry_size;
        size_t previous = 0;

        if (!loca.found || !glyf.found) {
            return 0;
        }
        if (index_to_loc_format == 0u) {
            entry_size = 2u;
        } else if (index_to_loc_format == 1u) {
            entry_size = 4u;
        } else {
            return 0;
        }
        if (loca.length < entries * entry_size) {
            return 0;
        }

        for (size_t i = 0; i < entries; i++) {
            size_t glyph_offset;
            const unsigned char *entry = buf + loca.offset + i * entry_size;

            if (entry_size == 2u) {
                glyph_offset = (size_t)read_u16be(entry) * 2u;
            } else {
                glyph_offset = (size_t)read_u32be(entry);
            }
            if (glyph_offset < previous || glyph_offset > glyf.length) {
                return 0;
            }
            previous = glyph_offset;
        }
        return 1;
    }

    if (cff.found || cff2.found) {
        sfnt_table outlines = cff.found ? cff : cff2;
        return outlines.length >= 4u;
    }

    return 0;
}

static int is_utf8_continuation(unsigned char c) {
    return (c & 0xc0u) == 0x80u;
}

static size_t valid_utf8_sequence_length(const unsigned char *s) {
    unsigned char c0 = s[0];

    if (c0 < 0x80u) return 1u;

    if (c0 >= 0xc2u && c0 <= 0xdfu) {
        if (s[1] != '\0' && is_utf8_continuation(s[1])) return 2u;
        return 0u;
    }

    if (c0 == 0xe0u) {
        if (s[1] >= 0xa0u && s[1] <= 0xbfu && s[2] != '\0' && is_utf8_continuation(s[2])) return 3u;
        return 0u;
    }
    if (c0 >= 0xe1u && c0 <= 0xecu) {
        if (s[1] != '\0' && is_utf8_continuation(s[1]) && s[2] != '\0' && is_utf8_continuation(s[2])) return 3u;
        return 0u;
    }
    if (c0 == 0xedu) {
        if (s[1] >= 0x80u && s[1] <= 0x9fu && s[2] != '\0' && is_utf8_continuation(s[2])) return 3u;
        return 0u;
    }
    if (c0 >= 0xeeu && c0 <= 0xefu) {
        if (s[1] != '\0' && is_utf8_continuation(s[1]) && s[2] != '\0' && is_utf8_continuation(s[2])) return 3u;
        return 0u;
    }

    if (c0 == 0xf0u) {
        if (s[1] >= 0x90u && s[1] <= 0xbfu && s[2] != '\0' && is_utf8_continuation(s[2]) &&
            s[3] != '\0' && is_utf8_continuation(s[3])) return 4u;
        return 0u;
    }
    if (c0 >= 0xf1u && c0 <= 0xf3u) {
        if (s[1] != '\0' && is_utf8_continuation(s[1]) && s[2] != '\0' && is_utf8_continuation(s[2]) &&
            s[3] != '\0' && is_utf8_continuation(s[3])) return 4u;
        return 0u;
    }
    if (c0 == 0xf4u) {
        if (s[1] >= 0x80u && s[1] <= 0x8fu && s[2] != '\0' && is_utf8_continuation(s[2]) &&
            s[3] != '\0' && is_utf8_continuation(s[3])) return 4u;
        return 0u;
    }

    return 0u;
}

static void print_json_string(FILE *stream, const char *s) {
    const unsigned char *p = (const unsigned char *)s;

    fputc('"', stream);
    while (*p) {
        unsigned char c = *p;
        size_t utf8_len;

        if (c == '"') {
            fputs("\\\"", stream);
            p++;
            continue;
        }
        if (c == '\\') {
            fputs("\\\\", stream);
            p++;
            continue;
        }
        if (c == '\b') {
            fputs("\\b", stream);
            p++;
            continue;
        }
        if (c == '\f') {
            fputs("\\f", stream);
            p++;
            continue;
        }
        if (c == '\n') {
            fputs("\\n", stream);
            p++;
            continue;
        }
        if (c == '\r') {
            fputs("\\r", stream);
            p++;
            continue;
        }
        if (c == '\t') {
            fputs("\\t", stream);
            p++;
            continue;
        }
        if (c < 0x20u) {
            fprintf(stream, "\\u%04x", (unsigned int)c);
            p++;
            continue;
        }

        utf8_len = valid_utf8_sequence_length(p);
        if (utf8_len != 0u) {
            fwrite(p, 1u, utf8_len, stream);
            p += utf8_len;
            continue;
        }

        fprintf(stream, "\\u%04x", (unsigned int)c);
        p++;
    }
    fputc('"', stream);
}

static void print_usage(FILE *stream) {
    fprintf(stream,
            "usage: stb-atlas [font.ttf] [size] [output.png]\n"
            "  font.ttf    path to TrueType font\n"
            "  size        font size in pixels (default: 32.0)\n"
            "  output.png  output image filename (default: atlas.png)\n"
            "\n"
            "Metrics (JSON) are printed to stdout.\n");
}

int main(int argc, char **argv) {
    const char *font_path = NULL;
    float font_size = 32.0f;
    int font_size_set = 0;
    const char *out_path = "atlas.png";
    int atlas_w = 512, atlas_h = 512;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else if (!font_path) {
            font_path = argv[i];
        } else if (!font_size_set) {
            char *end;
            float parsed = strtof(argv[i], &end);
            if (end != argv[i] && *end == '\0' && isfinite(parsed) && parsed > 0.0f) {
                font_size = parsed;
                font_size_set = 1;
            } else {
                out_path = argv[i];
            }
        } else {
            out_path = argv[i];
        }
    }

    if (!font_path) {
        fprintf(stderr, "error: missing font.ttf\n");
        print_usage(stderr);
        return 1;
    }

    FILE *font_file = fopen(font_path, "rb");
    if (!font_file) {
        fprintf(stderr, "error: failed to open %s\n", font_path);
        return 1;
    }

    if (fseek(font_file, 0, SEEK_END) != 0) {
        fprintf(stderr, "error: cannot determine size of %s\n", font_path);
        fclose(font_file);
        return 1;
    }

    long file_size = ftell(font_file);
    if (file_size <= 0) {
        fprintf(stderr, "error: cannot determine size of %s\n", font_path);
        fclose(font_file);
        return 1;
    }
    if ((uintmax_t)file_size > SIZE_MAX) {
        fprintf(stderr, "error: font file too large: %s\n", font_path);
        fclose(font_file);
        return 1;
    }
    if (fseek(font_file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "error: failed to rewind %s\n", font_path);
        fclose(font_file);
        return 1;
    }

    size_t size = (size_t)file_size;
    if (size > SIZE_MAX - FONT_BUFFER_PADDING) {
        fprintf(stderr, "error: font file too large: %s\n", font_path);
        fclose(font_file);
        return 1;
    }

    unsigned char *ttf_buffer = calloc(1u, size + FONT_BUFFER_PADDING);
    if (!ttf_buffer || fread(ttf_buffer, 1, size, font_file) != size) {
        fprintf(stderr, "error: failed to read %s\n", font_path);
        free(ttf_buffer);
        fclose(font_file);
        return 1;
    }
    fclose(font_file);

    if (!validate_sfnt_font(ttf_buffer, size)) {
        fprintf(stderr, "error: %s is not a valid or supported TTF/OTF font file\n", font_path);
        free(ttf_buffer);
        return 1;
    }

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttf_buffer, 0)) {
        fprintf(stderr, "error: %s is not a valid or supported TTF/OTF font file\n", font_path);
        free(ttf_buffer);
        return 1;
    }

    unsigned char *bitmap = calloc((size_t)atlas_w * (size_t)atlas_h, 1);
    if (!bitmap) {
        fprintf(stderr, "error: failed to allocate bitmap\n");
        free(ttf_buffer);
        return 1;
    }

    stbtt_packedchar chardata[96];
    memset(chardata, 0, sizeof(chardata));
    stbtt_pack_context pc;

    if (!stbtt_PackBegin(&pc, bitmap, atlas_w, atlas_h, 0, 1, NULL)) {
        fprintf(stderr, "error: failed to initialize font packer\n");
        free(bitmap);
        free(ttf_buffer);
        return 1;
    }

    stbtt_PackSetOversampling(&pc, 2, 2);
    int packed_all = stbtt_PackFontRange(&pc, ttf_buffer, 0, font_size, 32, 95, chardata);
    stbtt_PackEnd(&pc);
    if (!packed_all) {
        fprintf(stderr, "error: not all characters fit in %dx%d atlas\n", atlas_w, atlas_h);
        free(bitmap);
        free(ttf_buffer);
        return 1;
    }

    if (!stbi_write_png(out_path, atlas_w, atlas_h, 1, bitmap, atlas_w)) {
        fprintf(stderr, "error: failed to write %s\n", out_path);
        free(bitmap);
        free(ttf_buffer);
        return 1;
    }

    printf("{\n");
    printf("  \"texture\": ");
    print_json_string(stdout, out_path);
    printf(",\n");
    printf("  \"width\": %d,\n  \"height\": %d,\n  \"size\": %.1f,\n", atlas_w, atlas_h, font_size);
    printf("  \"glyphs\": {\n");
    for (int i = 0; i < 95; i++) {
        int codepoint = 32 + i;
        char char_str[4] = {0};

        if (codepoint == '"') strcpy(char_str, "\\\"");
        else if (codepoint == '\\') strcpy(char_str, "\\\\");
        else {
            char_str[0] = (char)codepoint;
            char_str[1] = '\0';
        }

        printf("    \"%d\": {\"char\": \"%s\", \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d, \"xoff\": %.2f, \"yoff\": %.2f, \"xadvance\": %.2f}%s\n",
               codepoint, char_str,
               chardata[i].x0, chardata[i].y0,
               chardata[i].x1 - chardata[i].x0,
               chardata[i].y1 - chardata[i].y0,
               chardata[i].xoff, chardata[i].yoff, chardata[i].xadvance,
               (i == 94) ? "" : ",");
    }
    printf("  }\n}\n");

    free(bitmap);
    free(ttf_buffer);
    return 0;
}
