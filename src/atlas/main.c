#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "stb_image_write.h"
#include "stb_rect_pack.h"
#include "stb_truetype.h"

static int has_ttf_signature(const unsigned char *buf, size_t len) {
    if (len < 4) return 0;
    if (memcmp(buf, "\x00\x01\x00\x00", 4) == 0) return 1; // TrueType
    if (memcmp(buf, "OTTO", 4) == 0) return 1;             // OpenType/CFF
    if (memcmp(buf, "true", 4) == 0) return 1;             // Apple TrueType
    if (memcmp(buf, "typ1", 4) == 0) return 1;             // Type 1 in sfnt wrapper
    return 0;
}

static void print_json_string(FILE *stream, const char *s) {
    fputc('"', stream);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (c == '"') fputs("\\\"", stream);
        else if (c == '\\') fputs("\\\\", stream);
        else if (c == '\b') fputs("\\b", stream);
        else if (c == '\f') fputs("\\f", stream);
        else if (c == '\n') fputs("\\n", stream);
        else if (c == '\r') fputs("\\r", stream);
        else if (c == '\t') fputs("\\t", stream);
        else if (c < 0x20) fprintf(stream, "\\u%04x", (unsigned int)c);
        else fputc((int)c, stream);
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

    // Arg parsing
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
                // don't set font_size_set — a size could still follow
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

    // Read TTF file into memory using robust POSIX fstat
    FILE *font_file = fopen(font_path, "rb");
    if (!font_file) {
        fprintf(stderr, "error: failed to open %s\n", font_path);
        return 1;
    }

    struct stat st;
    if (fstat(fileno(font_file), &st) != 0 || st.st_size <= 0) {
        fprintf(stderr, "error: cannot determine size of %s\n", font_path);
        fclose(font_file);
        return 1;
    }
    long size = st.st_size;

    unsigned char *ttf_buffer = malloc((size_t)size);
    if (!ttf_buffer || fread(ttf_buffer, 1, (size_t)size, font_file) != (size_t)size) {
        fprintf(stderr, "error: failed to read %s\n", font_path);
        if (ttf_buffer) free(ttf_buffer);
        fclose(font_file);
        return 1;
    }
    fclose(font_file);

    if (!has_ttf_signature(ttf_buffer, (size_t)size)) {
        fprintf(stderr, "error: %s is not a valid TTF/OTF font file\n", font_path);
        free(ttf_buffer);
        return 1;
    }

    // Allocate memory for the output atlas bitmap
    unsigned char *bitmap = calloc((size_t)atlas_w * (size_t)atlas_h, 1);
    if (!bitmap) {
        fprintf(stderr, "error: failed to allocate bitmap\n");
        free(ttf_buffer);
        return 1;
    }

    stbtt_packedchar chardata[96]; // ASCII 32..126 is 95 chars
    memset(chardata, 0, sizeof(chardata));
    stbtt_pack_context pc;

    // Pack the font
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

    // Write the PNG image
    if (!stbi_write_png(out_path, atlas_w, atlas_h, 1, bitmap, atlas_w)) {
        fprintf(stderr, "error: failed to write %s\n", out_path);
        free(bitmap);
        free(ttf_buffer);
        return 1;
    }

    // Write JSON metadata to stdout
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
