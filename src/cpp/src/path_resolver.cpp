#include "path_resolver.hpp"

#include <filesystem>

namespace fm {

PathResolver::PathResolver(std::string homeDirectory) : homeDirectory_(std::move(homeDirectory)) {}

ResolvedPath PathResolver::resolve(const std::string &rawPath, const std::string &baseDirectory) const {
  std::string path = rawPath;
  while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();

  // 2026-09-25追加、直接のユーザー報告("`^Xd/~^M`...が利かない"):
  // 末尾の"/~"は"~"単独と同じ扱い――legacy menu.c(M_ldir/M_lfile)の
  // `if (... cp[0] == '/' && cp[1] == '~') memmove(...)`から移植。
  // 下の"~/"検索は後ろにスラッシュが続く場合しか拾わないため、
  // これが無いと"/~"はリテラルな"/~"として解決されていた。
  if (path.size() >= 2 && path.compare(path.size() - 2, 2, "/~") == 0) path = "~";

  if (path == "~") {
    path = homeDirectory_;
  } else if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
    path = homeDirectory_ + path.substr(1);
  } else {
    std::size_t tilde = path.find("~/");
    if (tilde != std::string::npos) path = homeDirectory_ + path.substr(tilde + 1);
  }

  // 後から埋め込まれた絶対パスは、それより前に入力された全てを
  // 上書きする――emacsのread-file-name/expand-file-nameの慣習と
  // 同じ。実際の報告: 既に絶対パスが入力済み(例えば^X^Dのディレクトリ
  // プロンプトに"/Users/.../usilic/"が事前入力されている状態)の直後に
  // "/tmp"と入力すると、バッファの生テキストとしては".../usilic//tmp"
  // になるが、Enterを押した際はリテラルな"usilic/tmp"というサブ
  // ディレクトリを(失敗しつつ)開こうとするのではなく、/tmp自身の
  // 一覧へ着地すべきである。実際に"//"が二重になった場合(バッファが
  // 既に'/'で終わっている状態で新たに'/'が入力された場合)にのみ
  // 発動するため、"usilic/tmp"(スラッシュ1個)のような普通の相対的
  // 続きの入力には手を触れない。find ではなく rfind を使うことで
  // 「最後の」そのようなリセット箇所を選ぶため、後から入力された
  // 2つ目の絶対パスによる上書きが、先に入力された分より優先される。
  std::size_t lastDoubleSlash = path.rfind("//");
  if (lastDoubleSlash != std::string::npos) path = path.substr(lastDoubleSlash + 1);

  // まだ相対パスのまま(先頭が'/'でなく、上の~展開も効いていない)
  // 場合: 呼び出し側がbaseDirectoryを渡していればそこへ結合する
  // ――このパラメータ自身の宣言コメント参照。
  if (!path.empty() && path.front() != '/' && !baseDirectory.empty()) {
    std::string base = baseDirectory;
    if (base.back() != '/') base += '/';
    path = base + path;
  }

  ResolvedPath result;
  if (path.empty()) return result;

  std::error_code ec;
  bool isDir = std::filesystem::is_directory(path, ec) && !ec;
  bool exists = std::filesystem::exists(path, ec) && !ec;

  std::string normalized = std::filesystem::path(path).lexically_normal().string();
  if (isDir && (normalized.empty() || normalized.back() != '/')) normalized += '/';

  result.path = normalized;
  result.isDirectory = isDir;
  result.exists = exists;
  return result;
}

}  // namespace fm
