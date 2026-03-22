#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

static void print_usage(FILE *stream) {
    fprintf(stream,
            "usage: stb-img [input] [output] [--resize WxH] [--filter name]\n"
            "  input       source image (any stb_image-supported format)\n"
            "  output      output image (.png, .jpg, .jpeg, .bmp, .tga)\n"
            "  --resize    resize to WxH before writing\n"
            "  --filter    resize filter (default, point, triangle, mitchell,\n"
            "              catmullrom, box, cubicbspline); requires --resize\n");
}

static int parse_dimensions(const char *arg, int *w, int *h) {
    int parsed_w = 0;
    int parsed_h = 0;
    char tail = '\0';

    if (sscanf(arg, "%dx%d%c", &parsed_w, &parsed_h, &tail) != 2) {
        return 0;
    }
    if (parsed_w <= 0 || parsed_h <= 0) {
        return 0;
    }
    *w = parsed_w;
    *h = parsed_h;
    return 1;
}

static int ascii_equal_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int parse_filter_name(const char *arg, stbir_filter *filter_out) {
    struct filter_spec {
        const char *name;
        stbir_filter filter;
    };
    static const struct filter_spec filters[] = {
        { "default", STBIR_FILTER_DEFAULT },
        { "point", STBIR_FILTER_POINT_SAMPLE },
        { "triangle", STBIR_FILTER_TRIANGLE },
        { "mitchell", STBIR_FILTER_MITCHELL },
        { "catmullrom", STBIR_FILTER_CATMULLROM },
        { "box", STBIR_FILTER_BOX },
        { "cubicbspline", STBIR_FILTER_CUBICBSPLINE },
    };
    size_t i;

    for (i = 0; i < sizeof(filters) / sizeof(filters[0]); i++) {
        if (ascii_equal_ci(arg, filters[i].name)) {
            *filter_out = filters[i].filter;
            return 1;
        }
    }
    return 0;
}

static const char *path_extension(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path || dot[1] == '\0') {
        return NULL;
    }
    return dot + 1;
}

static int allocate_image_buffer(int w, int h, int comp, unsigned char **buffer_out) {
    size_t pixel_count;
    size_t total_bytes;

    if (w <= 0 || h <= 0 || comp <= 0) {
        return 0;
    }
    if ((size_t)w > SIZE_MAX / (size_t)h) {
        return 0;
    }
    pixel_count = (size_t)w * (size_t)h;
    if ((size_t)comp > SIZE_MAX / pixel_count) {
        return 0;
    }
    total_bytes = pixel_count * (size_t)comp;
    if (total_bytes > INT_MAX) {
        return 0;
    }

    *buffer_out = malloc(total_bytes);
    return *buffer_out != NULL;
}

static int write_jpeg(const char *path, int w, int h, int comp, const unsigned char *data) {
    unsigned char *converted = NULL;
    int ok = 0;

    if (comp == 1 || comp == 3) {
        return stbi_write_jpg(path, w, h, comp, data, 90);
    }

    if (comp == 2) {
        size_t pixel_count;
        size_t i;

        if ((size_t)w > SIZE_MAX / (size_t)h) {
            return 0;
        }
        pixel_count = (size_t)w * (size_t)h;
        converted = malloc(pixel_count);
        if (!converted) {
            return 0;
        }
        for (i = 0; i < pixel_count; i++) {
            converted[i] = data[i * 2u];
        }
        ok = stbi_write_jpg(path, w, h, 1, converted, 90);
        free(converted);
        return ok;
    }

    if (comp == 4) {
        size_t pixel_count;
        size_t i;

        if ((size_t)w > SIZE_MAX / (size_t)h) {
            return 0;
        }
        pixel_count = (size_t)w * (size_t)h;
        if (pixel_count > SIZE_MAX / 3u) {
            return 0;
        }
        converted = malloc(pixel_count * 3u);
        if (!converted) {
            return 0;
        }
        for (i = 0; i < pixel_count; i++) {
            converted[i * 3u + 0u] = data[i * 4u + 0u];
            converted[i * 3u + 1u] = data[i * 4u + 1u];
            converted[i * 3u + 2u] = data[i * 4u + 2u];
        }
        ok = stbi_write_jpg(path, w, h, 3, converted, 90);
        free(converted);
        return ok;
    }

    return 0;
}

static int write_image(const char *path, int w, int h, int comp, const unsigned char *data) {
    const char *ext = path_extension(path);

    if (!ext) {
        fprintf(stderr, "error: output filename must have an extension\n");
        return 0;
    }

    if (ascii_equal_ci(ext, "png")) {
        return stbi_write_png(path, w, h, comp, data, w * comp);
    }
    if (ascii_equal_ci(ext, "bmp")) {
        return stbi_write_bmp(path, w, h, comp, data);
    }
    if (ascii_equal_ci(ext, "tga")) {
        return stbi_write_tga(path, w, h, comp, data);
    }
    if (ascii_equal_ci(ext, "jpg") || ascii_equal_ci(ext, "jpeg")) {
        return write_jpeg(path, w, h, comp, data);
    }

    fprintf(stderr, "error: unsupported output format: .%s\n", ext);
    return 0;
}

int main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    int resize_w = 0;
    int resize_h = 0;
    int resize_requested = 0;
    int filter_requested = 0;
    stbir_filter resize_filter = STBIR_FILTER_DEFAULT;
    unsigned char *input_pixels = NULL;
    unsigned char *output_pixels = NULL;
    int input_w = 0;
    int input_h = 0;
    int comp = 0;
    int output_w;
    int output_h;
    int i;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(stdout);
            return 0;
        }
        if (strcmp(arg, "--resize") == 0 || strncmp(arg, "--resize=", 9) == 0) {
            const char *value = NULL;
            if (strcmp(arg, "--resize") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "error: missing value for --resize\n");
                    print_usage(stderr);
                    return 1;
                }
                value = argv[++i];
            } else {
                value = arg + 9;
            }
            if (!parse_dimensions(value, &resize_w, &resize_h)) {
                fprintf(stderr, "error: invalid --resize value: %s\n", value);
                return 1;
            }
            resize_requested = 1;
            continue;
        }
        if (strcmp(arg, "--filter") == 0 || strncmp(arg, "--filter=", 9) == 0) {
            const char *value = NULL;
            if (strcmp(arg, "--filter") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "error: missing value for --filter\n");
                    print_usage(stderr);
                    return 1;
                }
                value = argv[++i];
            } else {
                value = arg + 9;
            }
            if (!parse_filter_name(value, &resize_filter)) {
                fprintf(stderr, "error: invalid --filter value: %s\n", value);
                return 1;
            }
            filter_requested = 1;
            continue;
        }
        if (!input_path) {
            input_path = arg;
            continue;
        }
        if (!output_path) {
            output_path = arg;
            continue;
        }
        fprintf(stderr, "error: unexpected argument: %s\n", arg);
        print_usage(stderr);
        return 1;
    }

    if (!input_path || !output_path) {
        fprintf(stderr, "error: missing input or output path\n");
        print_usage(stderr);
        return 1;
    }
    if (filter_requested && !resize_requested) {
        fprintf(stderr, "error: --filter requires --resize\n");
        return 1;
    }

    input_pixels = stbi_load(input_path, &input_w, &input_h, &comp, 0);
    if (!input_pixels) {
        const char *reason = stbi_failure_reason();
        fprintf(stderr, "error: failed to load %s%s%s\n",
                input_path,
                reason ? ": " : "",
                reason ? reason : "");
        return 1;
    }
    if (comp < 1 || comp > 4) {
        fprintf(stderr, "error: unsupported channel count: %d\n", comp);
        stbi_image_free(input_pixels);
        return 1;
    }

    output_w = resize_requested ? resize_w : input_w;
    output_h = resize_requested ? resize_h : input_h;
    output_pixels = input_pixels;

    if (resize_requested) {
        if (!allocate_image_buffer(output_w, output_h, comp, &output_pixels)) {
            fprintf(stderr, "error: failed to allocate %dx%d resized image\n", output_w, output_h);
            stbi_image_free(input_pixels);
            return 1;
        }
        if (!stbir_resize(input_pixels, input_w, input_h, 0,
                          output_pixels, output_w, output_h, 0,
                          (stbir_pixel_layout)comp, STBIR_TYPE_UINT8,
                          STBIR_EDGE_CLAMP, resize_filter)) {
            fprintf(stderr, "error: resize failed\n");
            free(output_pixels);
            stbi_image_free(input_pixels);
            return 1;
        }
    }

    if (!write_image(output_path, output_w, output_h, comp, output_pixels)) {
        if (resize_requested) {
            free(output_pixels);
        }
        stbi_image_free(input_pixels);
        return 1;
    }

    if (resize_requested) {
        free(output_pixels);
    }
    stbi_image_free(input_pixels);
    return 0;
}
