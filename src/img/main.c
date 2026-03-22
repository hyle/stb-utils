#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"
#include "stb_image_write.h"

static void print_usage(FILE *stream) {
    fprintf(stream,
            "usage: stb-img [input] [output] [--resize WxH]\n"
            "  input       source image (any stb_image-supported format)\n"
            "  output      output image (.png, .jpg, .jpeg, .bmp, .tga)\n"
            "  --resize    resize to WxH before writing\n");
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

static double clamp_double(double value, double min_value, double max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
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

static int resize_image_bilinear(const unsigned char *src, int src_w, int src_h, int comp,
                                 unsigned char *dst, int dst_w, int dst_h) {
    size_t dst_stride;

    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return 0;
    }
    if (comp < 1 || comp > 4) {
        return 0;
    }

    dst_stride = (size_t)dst_w * (size_t)comp;
    if (src_w == dst_w && src_h == dst_h) {
        size_t row_bytes = (size_t)src_w * (size_t)comp;
        for (int y = 0; y < src_h; y++) {
            memcpy(dst + (size_t)y * dst_stride, src + (size_t)y * row_bytes, row_bytes);
        }
        return 1;
    }

    for (int y = 0; y < dst_h; y++) {
        double src_y = ((double)y + 0.5) * (double)src_h / (double)dst_h - 0.5;
        int y0;
        int y1;
        double fy;

        src_y = clamp_double(src_y, 0.0, (double)(src_h - 1));
        y0 = (int)src_y;
        y1 = y0 + 1;
        if (y1 >= src_h) {
            y1 = src_h - 1;
        }
        fy = src_y - (double)y0;

        for (int x = 0; x < dst_w; x++) {
            double src_x = ((double)x + 0.5) * (double)src_w / (double)dst_w - 0.5;
            int x0;
            int x1;
            double fx;
            double w00, w10, w01, w11;
            size_t out_index;
            const unsigned char *p00;
            const unsigned char *p10;
            const unsigned char *p01;
            const unsigned char *p11;

            src_x = clamp_double(src_x, 0.0, (double)(src_w - 1));
            x0 = (int)src_x;
            x1 = x0 + 1;
            if (x1 >= src_w) {
                x1 = src_w - 1;
            }
            fx = src_x - (double)x0;

            w00 = (1.0 - fx) * (1.0 - fy);
            w10 = fx * (1.0 - fy);
            w01 = (1.0 - fx) * fy;
            w11 = fx * fy;

            p00 = src + ((size_t)y0 * (size_t)src_w + (size_t)x0) * (size_t)comp;
            p10 = src + ((size_t)y0 * (size_t)src_w + (size_t)x1) * (size_t)comp;
            p01 = src + ((size_t)y1 * (size_t)src_w + (size_t)x0) * (size_t)comp;
            p11 = src + ((size_t)y1 * (size_t)src_w + (size_t)x1) * (size_t)comp;
            out_index = (size_t)y * dst_stride + (size_t)x * (size_t)comp;

            if (comp == 2 || comp == 4) {
                int alpha_index = comp - 1;
                double alpha = w00 * p00[alpha_index] + w10 * p10[alpha_index]
                             + w01 * p01[alpha_index] + w11 * p11[alpha_index];

                if (alpha > 0.0) {
                    for (int c = 0; c < alpha_index; c++) {
                        double premultiplied =
                            w00 * (double)p00[c] * (double)p00[alpha_index] +
                            w10 * (double)p10[c] * (double)p10[alpha_index] +
                            w01 * (double)p01[c] * (double)p01[alpha_index] +
                            w11 * (double)p11[c] * (double)p11[alpha_index];
                        dst[out_index + (size_t)c] = round_to_u8(premultiplied / alpha);
                    }
                } else {
                    for (int c = 0; c < alpha_index; c++) {
                        dst[out_index + (size_t)c] = 0;
                    }
                }
                dst[out_index + (size_t)alpha_index] = round_to_u8(alpha);
            } else {
                for (int c = 0; c < comp; c++) {
                    double value = w00 * p00[c] + w10 * p10[c] + w01 * p01[c] + w11 * p11[c];
                    dst[out_index + (size_t)c] = round_to_u8(value);
                }
            }
        }
    }

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
        if ((size_t)w > SIZE_MAX / (size_t)h) {
            return 0;
        }
        pixel_count = (size_t)w * (size_t)h;
        converted = malloc(pixel_count);
        if (!converted) {
            return 0;
        }
        for (size_t i = 0; i < pixel_count; i++) {
            converted[i] = data[i * 2];
        }
        ok = stbi_write_jpg(path, w, h, 1, converted, 90);
        free(converted);
        return ok;
    }

    if (comp == 4) {
        size_t pixel_count;
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
        for (size_t i = 0; i < pixel_count; i++) {
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
        if (!resize_image_bilinear(input_pixels, input_w, input_h, comp,
                                   output_pixels, output_w, output_h)) {
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
