#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"
#include "stb_image_write.h"

typedef enum resize_filter_kind {
    RESIZE_FILTER_DEFAULT = 0,
    RESIZE_FILTER_POINT,
    RESIZE_FILTER_TRIANGLE,
    RESIZE_FILTER_MITCHELL,
    RESIZE_FILTER_CATMULLROM,
    RESIZE_FILTER_BOX,
    RESIZE_FILTER_CUBICBSPLINE,
} resize_filter_kind;

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

static int parse_filter_name(const char *arg, resize_filter_kind *filter_out) {
    struct filter_spec {
        const char *name;
        resize_filter_kind filter;
    };
    static const struct filter_spec filters[] = {
        { "default", RESIZE_FILTER_DEFAULT },
        { "point", RESIZE_FILTER_POINT },
        { "triangle", RESIZE_FILTER_TRIANGLE },
        { "mitchell", RESIZE_FILTER_MITCHELL },
        { "catmullrom", RESIZE_FILTER_CATMULLROM },
        { "box", RESIZE_FILTER_BOX },
        { "cubicbspline", RESIZE_FILTER_CUBICBSPLINE },
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

static unsigned char round_to_u8(double value) {
    if (value <= 0.0) {
        return 0;
    }
    if (value >= 255.0) {
        return 255;
    }
    return (unsigned char)(value + 0.5);
}

static int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static double sample_center(int dst_index, int src_size, int dst_size) {
    return ((double)dst_index + 0.5) * (double)src_size / (double)dst_size - 0.5;
}

static double filter_support(resize_filter_kind filter) {
    switch (filter) {
        case RESIZE_FILTER_DEFAULT:
        case RESIZE_FILTER_TRIANGLE:
            return 1.0;
        case RESIZE_FILTER_POINT:
        case RESIZE_FILTER_BOX:
            return 0.5;
        case RESIZE_FILTER_MITCHELL:
        case RESIZE_FILTER_CATMULLROM:
        case RESIZE_FILTER_CUBICBSPLINE:
            return 2.0;
    }
    return 1.0;
}

static double cubic_weight(double x, double b, double c) {
    double ax = fabs(x);

    if (ax < 1.0) {
        return ((12.0 - 9.0 * b - 6.0 * c) * ax * ax * ax +
                (-18.0 + 12.0 * b + 6.0 * c) * ax * ax +
                (6.0 - 2.0 * b)) / 6.0;
    }
    if (ax < 2.0) {
        return ((-b - 6.0 * c) * ax * ax * ax +
                (6.0 * b + 30.0 * c) * ax * ax +
                (-12.0 * b - 48.0 * c) * ax +
                (8.0 * b + 24.0 * c)) / 6.0;
    }
    return 0.0;
}

static double filter_weight(resize_filter_kind filter, double x) {
    double ax = fabs(x);

    switch (filter) {
        case RESIZE_FILTER_DEFAULT:
        case RESIZE_FILTER_TRIANGLE:
            return ax < 1.0 ? 1.0 - ax : 0.0;
        case RESIZE_FILTER_BOX:
            return ax <= 0.5 ? 1.0 : 0.0;
        case RESIZE_FILTER_MITCHELL:
            return cubic_weight(x, 1.0 / 3.0, 1.0 / 3.0);
        case RESIZE_FILTER_CATMULLROM:
            return cubic_weight(x, 0.0, 0.5);
        case RESIZE_FILTER_CUBICBSPLINE:
            return cubic_weight(x, 1.0, 0.0);
        case RESIZE_FILTER_POINT:
            return ax <= 0.5 ? 1.0 : 0.0;
    }
    return 0.0;
}

static int resize_image_point(const unsigned char *src, int src_w, int src_h, int comp,
                              unsigned char *dst, int dst_w, int dst_h) {
    size_t dst_stride;
    size_t src_stride;
    int y;

    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return 0;
    }
    if (comp < 1 || comp > 4) {
        return 0;
    }

    dst_stride = (size_t)dst_w * (size_t)comp;
    src_stride = (size_t)src_w * (size_t)comp;

    if (src_w == dst_w && src_h == dst_h) {
        for (y = 0; y < src_h; y++) {
            memcpy(dst + (size_t)y * dst_stride, src + (size_t)y * src_stride, src_stride);
        }
        return 1;
    }

    for (y = 0; y < dst_h; y++) {
        int src_y = clamp_int((int)floor(sample_center(y, src_h, dst_h) + 0.5), 0, src_h - 1);
        int x;

        for (x = 0; x < dst_w; x++) {
            int src_x = clamp_int((int)floor(sample_center(x, src_w, dst_w) + 0.5), 0, src_w - 1);
            const unsigned char *in = src + ((size_t)src_y * (size_t)src_w + (size_t)src_x) * (size_t)comp;
            unsigned char *out = dst + (size_t)y * dst_stride + (size_t)x * (size_t)comp;
            int c;

            for (c = 0; c < comp; c++) {
                out[c] = in[c];
            }
        }
    }

    return 1;
}

static void resample_row(const unsigned char *src_row, int src_w, int comp,
                         int alpha_index, int dst_w, resize_filter_kind filter,
                         double *tmp_row) {
    double scale = (double)dst_w / (double)src_w;
    double stretch = scale < 1.0 ? 1.0 / scale : 1.0;
    double support = filter_support(filter) * stretch;
    int x;

    for (x = 0; x < dst_w; x++) {
        double center = sample_center(x, src_w, dst_w);
        int start = clamp_int((int)ceil(center - support), 0, src_w - 1);
        int end = clamp_int((int)floor(center + support), 0, src_w - 1);
        double total = 0.0;
        size_t base = (size_t)x * (size_t)comp;
        int c;
        int sx;

        for (c = 0; c < comp; c++) {
            tmp_row[base + (size_t)c] = 0.0;
        }

        for (sx = start; sx <= end; sx++) {
            double weight = filter_weight(filter, (center - (double)sx) / stretch) / stretch;
            const unsigned char *p;

            if (weight == 0.0) {
                continue;
            }
            total += weight;
            p = src_row + (size_t)sx * (size_t)comp;
            if (alpha_index >= 0) {
                double alpha = (double)p[alpha_index];
                for (c = 0; c < alpha_index; c++) {
                    tmp_row[base + (size_t)c] += weight * (double)p[c] * alpha;
                }
                tmp_row[base + (size_t)alpha_index] += weight * alpha;
            } else {
                for (c = 0; c < comp; c++) {
                    tmp_row[base + (size_t)c] += weight * (double)p[c];
                }
            }
        }

        if (total != 0.0) {
            for (c = 0; c < comp; c++) {
                tmp_row[base + (size_t)c] /= total;
            }
        } else {
            int src_x = clamp_int((int)floor(center + 0.5), 0, src_w - 1);
            const unsigned char *p = src_row + (size_t)src_x * (size_t)comp;

            if (alpha_index >= 0) {
                double alpha = (double)p[alpha_index];
                for (c = 0; c < alpha_index; c++) {
                    tmp_row[base + (size_t)c] = (double)p[c] * alpha;
                }
                tmp_row[base + (size_t)alpha_index] = alpha;
            } else {
                for (c = 0; c < comp; c++) {
                    tmp_row[base + (size_t)c] = (double)p[c];
                }
            }
        }
    }
}

static int resize_image_filtered(const unsigned char *src, int src_w, int src_h, int comp,
                                 unsigned char *dst, int dst_w, int dst_h,
                                 resize_filter_kind filter) {
    int alpha_index = (comp == 2 || comp == 4) ? comp - 1 : -1;
    size_t tmp_len;
    double *tmp_row = NULL;
    double *accum_row = NULL;
    double scale_y;
    double stretch_y;
    double support_y;
    int y;

    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return 0;
    }
    if (comp < 1 || comp > 4) {
        return 0;
    }
    if (filter == RESIZE_FILTER_POINT) {
        return resize_image_point(src, src_w, src_h, comp, dst, dst_w, dst_h);
    }
    if (filter == RESIZE_FILTER_DEFAULT) {
        filter = RESIZE_FILTER_TRIANGLE;
    }

    tmp_len = (size_t)dst_w * (size_t)comp;
    if (tmp_len == 0 || tmp_len > SIZE_MAX / sizeof(double)) {
        return 0;
    }
    tmp_row = malloc(tmp_len * sizeof(double));
    accum_row = malloc(tmp_len * sizeof(double));
    if (!tmp_row || !accum_row) {
        free(tmp_row);
        free(accum_row);
        return 0;
    }

    scale_y = (double)dst_h / (double)src_h;
    stretch_y = scale_y < 1.0 ? 1.0 / scale_y : 1.0;
    support_y = filter_support(filter) * stretch_y;

    for (y = 0; y < dst_h; y++) {
        double center_y = sample_center(y, src_h, dst_h);
        int start_y = clamp_int((int)ceil(center_y - support_y), 0, src_h - 1);
        int end_y = clamp_int((int)floor(center_y + support_y), 0, src_h - 1);
        double total_y = 0.0;
        size_t x;
        int sy;

        for (x = 0; x < tmp_len; x++) {
            accum_row[x] = 0.0;
        }

        for (sy = start_y; sy <= end_y; sy++) {
            double weight_y = filter_weight(filter, (center_y - (double)sy) / stretch_y) / stretch_y;
            const unsigned char *src_row;

            if (weight_y == 0.0) {
                continue;
            }
            total_y += weight_y;
            src_row = src + (size_t)sy * (size_t)src_w * (size_t)comp;
            resample_row(src_row, src_w, comp, alpha_index, dst_w, filter, tmp_row);
            for (x = 0; x < tmp_len; x++) {
                accum_row[x] += tmp_row[x] * weight_y;
            }
        }

        if (total_y == 0.0) {
            int nearest_y = clamp_int((int)floor(center_y + 0.5), 0, src_h - 1);
            const unsigned char *src_row = src + (size_t)nearest_y * (size_t)src_w * (size_t)comp;
            resample_row(src_row, src_w, comp, alpha_index, dst_w, filter, tmp_row);
            memcpy(accum_row, tmp_row, tmp_len * sizeof(double));
            total_y = 1.0;
        }

        for (x = 0; x < (size_t)dst_w; x++) {
            size_t base = x * (size_t)comp;
            unsigned char *out = dst + ((size_t)y * (size_t)dst_w + x) * (size_t)comp;
            int c;

            if (alpha_index >= 0) {
                double alpha = accum_row[base + (size_t)alpha_index] / total_y;
                if (alpha > 0.0) {
                    for (c = 0; c < alpha_index; c++) {
                        out[c] = round_to_u8((accum_row[base + (size_t)c] / total_y) / alpha);
                    }
                } else {
                    for (c = 0; c < alpha_index; c++) {
                        out[c] = 0;
                    }
                }
                out[alpha_index] = round_to_u8(alpha);
            } else {
                for (c = 0; c < comp; c++) {
                    out[c] = round_to_u8(accum_row[base + (size_t)c] / total_y);
                }
            }
        }
    }

    free(tmp_row);
    free(accum_row);
    return 1;
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
    resize_filter_kind resize_filter = RESIZE_FILTER_DEFAULT;
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
        if (!resize_image_filtered(input_pixels, input_w, input_h, comp,
                                   output_pixels, output_w, output_h,
                                   resize_filter)) {
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
    printf("wrote %s (%dx%d, %d channel%s)\n",
           output_path,
           output_w,
           output_h,
           comp,
           comp == 1 ? "" : "s");
    return 0;
}
