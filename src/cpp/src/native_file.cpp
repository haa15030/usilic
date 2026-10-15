// native_file.hpp自身の宣言コメント参照。
#include "native_file.hpp"

#include <fstream>

namespace fm {

namespace {

bool startsWith(const std::string &s, std::size_t at, const std::string &magic) {
  return s.size() >= at + magic.size() && s.compare(at, magic.size(), magic) == 0;
}

struct Kind {
  const char *text;
  const char *mime;
};

// UTF-8として正しく、制御文字(タブ・改行・CR・ESC・FF以外)を含まないならテキスト。
const char *textKind(const std::string &head) {
  bool ascii = true;
  for (std::size_t i = 0; i < head.size();) {
    unsigned char c = static_cast<unsigned char>(head[i]);
    if (c < 0x80) {
      if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != 0x1b && c != '\f') return nullptr;
      if (c == 0x7f) return nullptr;
      i++;
      continue;
    }
    ascii = false;
    int len = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc2 ? 2 : 0;
    if (len == 0 || c > 0xf4) return nullptr;
    if (i + len > head.size()) break;  // 読んだ範囲の終わりで切れた文字は問わない
    for (int k = 1; k < len; k++)
      if ((static_cast<unsigned char>(head[i + k]) & 0xc0) != 0x80) return nullptr;
    i += len;
  }
  return ascii ? "ASCII text" : "Unicode text, UTF-8 text";
}

Kind detect(const std::string &h) {
  if (h.empty()) return {"empty", "inode/x-empty"};
  if (startsWith(h, 0, "PK\x03\x04")) {
    if (startsWith(h, 30, "mimetypeapplication/epub+zip")) return {"EPUB document", "application/epub+zip"};
    return {"Zip archive data", "application/zip"};
  }
  if (startsWith(h, 0, "PK\x05\x06")) return {"Zip archive data (empty)", "application/zip"};
  if (startsWith(h, 0, "%PDF-")) return {"PDF document", "application/pdf"};
  if (startsWith(h, 0, "\x89PNG\r\n\x1a\n")) return {"PNG image data", "image/png"};
  if (startsWith(h, 0, "\xff\xd8\xff")) return {"JPEG image data", "image/jpeg"};
  if (startsWith(h, 0, "GIF87a") || startsWith(h, 0, "GIF89a")) return {"GIF image data", "image/gif"};
  if (startsWith(h, 0, "BM") && h.size() >= 14) return {"PC bitmap", "image/bmp"};
  if (startsWith(h, 0, "II*\0") || startsWith(h, 0, std::string("MM\0*", 4)))
    return {"TIFF image data", "image/tiff"};
  if (startsWith(h, 0, "RIFF") && startsWith(h, 8, "WEBP")) return {"RIFF (little-endian) data, Web/P image", "image/webp"};
  if (startsWith(h, 0, "RIFF") && startsWith(h, 8, "WAVE")) return {"RIFF (little-endian) data, WAVE audio", "audio/x-wav"};
  if (startsWith(h, 0, "RIFF") && startsWith(h, 8, "AVI ")) return {"RIFF (little-endian) data, AVI", "video/x-msvideo"};
  if (startsWith(h, 4, "ftyp")) {
    if (startsWith(h, 8, "qt  ")) return {"Apple QuickTime movie", "video/quicktime"};
    if (startsWith(h, 8, "M4A ")) return {"ISO Media, Apple iTunes ALAC/AAC-LC (.M4A) Audio", "audio/x-m4a"};
    if (startsWith(h, 8, "heic") || startsWith(h, 8, "mif1")) return {"ISO Media, HEIF Image", "image/heic"};
    return {"ISO Media", "video/mp4"};
  }
  if (startsWith(h, 0, "ID3") || (h.size() >= 2 && static_cast<unsigned char>(h[0]) == 0xff &&
                                  (static_cast<unsigned char>(h[1]) & 0xe0) == 0xe0))
    return {"Audio file with ID3 / MPEG ADTS", "audio/mpeg"};
  if (startsWith(h, 0, "OggS")) return {"Ogg data", "audio/ogg"};
  if (startsWith(h, 0, "fLaC")) return {"FLAC audio bitstream data", "audio/flac"};
  if (startsWith(h, 0, "\x1a\x45\xdf\xa3")) return {"Matroska data", "video/x-matroska"};
  if (startsWith(h, 0, "\x1f\x8b")) return {"gzip compressed data", "application/gzip"};
  if (startsWith(h, 0, "BZh")) return {"bzip2 compressed data", "application/x-bzip2"};
  if (startsWith(h, 0, std::string("\xfd" "7zXZ\0", 6))) return {"XZ compressed data", "application/x-xz"};
  if (startsWith(h, 0, "\x28\xb5\x2f\xfd")) return {"Zstandard compressed data", "application/zstd"};
  if (startsWith(h, 0, "7z\xbc\xaf\x27\x1c")) return {"7-zip archive data", "application/x-7z-compressed"};
  if (startsWith(h, 0, "Rar!\x1a\x07")) return {"RAR archive data", "application/x-rar"};
  if (startsWith(h, 257, "ustar")) return {"POSIX tar archive", "application/x-tar"};
  if (startsWith(h, 0, "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1"))
    return {"Composite Document File V2 Document", "application/x-ole-storage"};
  if (startsWith(h, 0, "MZ")) return {"PE32+ executable (Windows)", "application/x-dosexec"};
  if (startsWith(h, 0, "\x7f" "ELF")) return {"ELF executable", "application/x-executable"};
  if (startsWith(h, 0, "\xcf\xfa\xed\xfe") || startsWith(h, 0, "\xca\xfe\xba\xbe"))
    return {"Mach-O executable", "application/x-mach-binary"};
  if (startsWith(h, 0, "SQLite format 3")) return {"SQLite 3.x database", "application/vnd.sqlite3"};
  if (startsWith(h, 0, "wOFF") || startsWith(h, 0, "wOF2")) return {"Web Open Font Format", "font/woff"};
  if (startsWith(h, 0, std::string("\0\x01\0\0", 4))) return {"TrueType Font data", "font/sfnt"};
  if (startsWith(h, 0, "OTTO")) return {"OpenType font data", "font/otf"};
  if (startsWith(h, 0, "\xef\xbb\xbf")) return {"Unicode text, UTF-8 (with BOM) text", "text/plain"};
  if (startsWith(h, 0, "\xff\xfe") || startsWith(h, 0, "\xfe\xff")) return {"Unicode text, UTF-16 text", "text/plain"};
  if (const char *text = textKind(h)) return {text, "text/plain"};
  return {"data", "application/octet-stream"};
}

}  // namespace

std::string nativeFileType(const std::string &head, bool mime) {
  Kind kind = detect(head);
  return mime ? kind.mime : kind.text;
}

int nativeFile(const std::vector<std::string> &args, std::string &output) {
  bool brief = false, mime = false;
  std::vector<std::string> paths;
  bool options = true;
  for (const auto &arg : args) {
    if (options && arg == "--") {
      options = false;
    } else if (options && (arg == "-b" || arg == "--brief")) {
      brief = true;
    } else if (options && (arg == "--mime-type" || arg == "-i" || arg == "--mime")) {
      mime = true;
    } else if (options && arg.size() > 1 && arg[0] == '-') {
      for (char c : arg.substr(1)) {
        if (c == 'b') brief = true;
        if (c == 'i') mime = true;
      }
    } else {
      paths.push_back(arg);
    }
  }
  for (const auto &path : paths) {
    if (!brief) output += path + ": ";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      output += mime ? "cannot open" : "cannot open `" + path + "' (No such file or directory)";
      output += '\n';
      continue;
    }
    std::string head(0x2000, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(in.gcount()));
    output += nativeFileType(head, mime) + '\n';
  }
  return 0;
}

}  // namespace fm
