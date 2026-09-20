#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fm {

// モードラインの検索系注釈("GREP: pattern"/"CUT: pattern"/"DU: path"/
// "LOCATE: query"/"ARC: path"等)と、それに紐づく`^G`の行き先を1つに
// まとめた、軽量なpush/popスタック。
//
// 重量級ModeStack(mode_stack.hpp、cutBuffer/view等の実データを運ぶ、
// DIR降下・CUT再分割専用)とは意図的に別物――統合しない
// (.steering/20260920-modeline-annotation-stack/design.md「アーキテクチャ
// 概要」参照)。ペイロードは表示テキスト+種別(kind)のみで、実データ
// (バッファ・ビュー等)は一切持たない。
//
// 現状の`Application::setSearchAnnotation()`(値を設定=push、空文字列で
// クリア=pop)+`Application::lastDirectorySearchKind_`(付随する種別、
// 呼び出し元とは別の独立フィールドだったため「積み忘れ・消し忘れ」型の
// バグを繰り返し起こしていた)を置き換える。DirectoryMode(primary用/
// ARC用の2インスタンス)とFileMode(1インスタンス)がそれぞれ独立して
// 保持する――design.md「1系統ではなく2〜3系統が同時に生きうる」参照。
//
// 深さは実運用上ほぼ常に1(現状のset/clearが「前の値を上書き」する形で
// しか使われていないため)。それでもpush/popという対になった操作にする
// ことが目的(呼び出し元のどこか別の関数で"clear"し忘れる、という現状
// 最大のバグ原因を構造的に無くす)であり、`vector`ベースのスタックという
// 形自体は将来の拡張性のためにも変えない。
template <typename Kind>
class AnnotationStack {
 public:
  struct Frame {
    Kind kind;
    std::string text;  // trailing2相当、描画にそのまま使う完成形の文字列
    // FileMode::setSearchOrigin(label, detail)相当――設定時はnameフィールド
    // 自体を乗っ取る。DirectoryMode側の呼び出しでは常にnulloptのまま
    // (design.md「FileModeの統合」参照)。
    std::optional<std::string> nameOverride;
  };

  // 「Enterで選択した」等、注釈が意味を持つようになった箇所からの呼び出し。
  void push(Kind kind, std::string text) { frames_.push_back(Frame{kind, std::move(text), std::nullopt}); }

  // FileMode::setSearchOrigin()相当――nameフィールドも上書きする注釈。
  void pushWithNameOverride(Kind kind, std::string name, std::string detail) {
    frames_.push_back(Frame{kind, std::move(detail), std::move(name)});
  }

  // 「^G/キャンセルで抜けた」等、注釈がもう無効になった箇所からの呼び出し。
  // 空スタックへの呼び出しはno-op(現状のsetSearchAnnotation("")が既に
  // 空でも安全に呼べていた挙動を踏襲)。
  void pop() {
    if (!frames_.empty()) frames_.pop_back();
  }

  // 「新しいls」等、無条件の一括無効化。
  void clear() { frames_.clear(); }

  bool empty() const { return frames_.empty(); }

  std::optional<Kind> topKind() const {
    return frames_.empty() ? std::nullopt : std::optional<Kind>(frames_.back().kind);
  }

  const Frame *top() const { return frames_.empty() ? nullptr : &frames_.back(); }

  // トップフレームのtext(トップが無ければ空文字列)。
  std::string displayText() const { return frames_.empty() ? std::string() : frames_.back().text; }

 private:
  std::vector<Frame> frames_;
};

}  // namespace fm
