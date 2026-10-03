// stb_image(third_party/stb_image.h、v2.30、パブリックドメイン/MIT、https://github.com/nothings/stb)の
// 実装をこの1か所で展開する(2026-10-03、EPUBの縦書きの画像ページ、.steering/20261003-epub-image-page/)。
// GraphicalDisplayが画像のバイト列をRGBAへ復号するのに使う。EPUBの挿絵の形式に絞る。
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#include "third_party/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
