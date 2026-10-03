// One CPU-only stb implementation; only PNG/JPEG memory decoding is enabled.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#include <stb_image.h>
