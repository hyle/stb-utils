#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>
#include "stb_perlin.h"
#include "stb_image_write.h"

static void print_usage(FILE *stream) {
    fprintf(stream,
            "usage: stb-noise [WxH] [scale] [output.png] [--octaves N]\n"
            "  WxH         image size, default 512x512\n"
            "  scale       positive noise frequency, default 4.0\n"
            "  output      filename, default noise.png\n"
            "  --octaves N FBM octaves (>=1), default 1\n");
}

int main(int argc, char **argv) {
    int w = 512, h = 512;
    float scale = 4.0f;
    int octaves = 1;
    const char *out = "noise.png";

    // simple arg parsing: stb-noise [WxH] [scale] [out.png] [--octaves N]
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(stdout);
            return 0;
        }

        if (strcmp(arg, "--octaves") == 0 || strncmp(arg, "--octaves=", 10) == 0) {
            const char *value = NULL;
            if (strcmp(arg, "--octaves") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "missing value for --octaves\n");
                    print_usage(stderr);
                    return 1;
                }
                value = argv[++i];
            } else {
                value = arg + 10;
            }

            char *end = NULL;
            long parsed = strtol(value, &end, 10);
            if (end == value || *end != '\0' || parsed < 1 || parsed > 32) {
                fprintf(stderr, "invalid --octaves value: %s (expected 1-32)\n", value);
                return 1;
            }
            octaves = (int)parsed;
            continue;
        }

        int pw = 0, ph = 0;
        char tail = '\0';
        if (sscanf(arg, "%dx%d%c", &pw, &ph, &tail) == 2) {
            if (pw <= 0 || ph <= 0) {
                fprintf(stderr, "invalid dimensions: %s\n", arg);
                return 1;
            }
            w = pw;
            h = ph;
            continue;
        }

        {
            char *end = NULL;
            float parsed = strtof(arg, &end);
            if (end != arg && *end == '\0') {
                if (!isfinite(parsed) || parsed <= 0.0f) {
                    fprintf(stderr, "invalid scale value: %s (expected positive finite number)\n", arg);
                    return 1;
                }
                scale = parsed;
            } else {
                out = arg;
            }
        }
    }

    if ((size_t)w > SIZE_MAX / (size_t)h) {
        fprintf(stderr, "dimensions out of range: %dx%d\n", w, h);
        return 1;
    }
    size_t pixel_count = (size_t)w * (size_t)h;
    if (pixel_count > INT_MAX) {
        fprintf(stderr, "dimensions out of range: %dx%d (max %d pixels)\n", w, h, INT_MAX);
        return 1;
    }
    unsigned char *img = malloc(pixel_count);
    if (!img) {
        fprintf(stderr, "allocation failed for %dx%d image\n", w, h);
        return 1;
    }
    for (int y = 0; y < h; y++) {
        unsigned char *row = img + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            float nx = (float)x / w * scale;
            float ny = (float)y / h * scale;
            float v;
            if (octaves > 1) v = stb_perlin_fbm_noise3(nx, ny, 0.0f, 2.0f, 0.5f, octaves);
            else v = stb_perlin_noise3(nx, ny, 0.0f, 0, 0, 0);
            // expected approximately in [-1, 1], remap to [0, 255] with clamping.
            if (!isfinite(v)) v = 0.0f;
            if (v < -1.0f) v = -1.0f;
            if (v > 1.0f) v = 1.0f;
            row[x] = (unsigned char)((v + 1.0f) * 0.5f * 255.0f);
        }
    }

    if (!stbi_write_png(out, w, h, 1, img, w)) {
        fprintf(stderr, "failed to write %s\n", out);
        free(img);
        return 1;
    }
    free(img);
    fprintf(stderr, "wrote %s (%dx%d, scale=%.1f, octaves=%d)\n", out, w, h, scale, octaves);
    return 0;
}
