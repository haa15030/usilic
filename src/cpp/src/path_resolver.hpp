#pragma once

#include <string>

namespace fm {

struct ResolvedPath {
  std::string path;
  bool isDirectory = false;
  bool exists = false;
};

// 生の(ユーザーが入力したかもしれない)パス文字列を、整形・分類済みの
// 形へ解決する: ~/~user展開、./および../セグメントの整理、実在する
// ディレクトリに対する末尾スラッシュの正規化。mode.cのM_ldir/M_lfile/
// M_ldir2/M_lfile2/T_locateのケースからの移植――入力されたパス
// (または選択されたLOCATE結果)がDirectoryModeとFileModeのどちらへ
// 着地すべきかを決めるための接着剤。
//
// これはsub.cのerase_dot()("./"/"../"セグメントの整理)の逐語的な
// 移植ではない。erase_dot()は手作りの、こまごまとしたmemmoveベースの
// 字句的パス正規化器である――このプロジェクトが、標準ライブラリの
// 同等品が存在する場合に手作りアルゴリズムを置き換えている、まさに
// その典型例だ(例: LISTのBSTがstd::mapになったのと同種)。ここでは
// それがstd::filesystem::path::lexically_normal()にあたる。
//
// 意図的に除外(将来の課題、この段階では対象外):
//   - 解決済みのパスを「どうするか」の決定(DirectoryModeへ入るか
//     FileModeへ入るか、ModeStackフレームをpushするか): Mode/
//     Applicationの仕事、この移植の他の全クラスが既に引いているのと
//     同じ引き渡しの境界。
//   - シンボリックリンク解決/realpath()風の正規化: lexically_normal()
//     は純粋にテキストベース(erase_dot()自身の純粋にテキストベースの
//     方式と同じ)であり、本物のrealpath()のようにシンボリックリンク
//     境界をまたいで構成された"."要素についてファイルシステムに
//     問い合わせることはしない。
class PathResolver {
 public:
  // homeDirectory: 呼び出し側が渡す$HOME(ここで隠れたgetenv()呼び出し
  // は行わない、Configuration/Shell等で既に確立されているこの
  // プロジェクトの「隠れたグローバル状態を持たない」という一貫した
  // 方針に合わせている)。
  explicit PathResolver(std::string homeDirectory);

  // baseDirectory: rawPathが絶対パスでも~始まりでもない場合に、その
  // 前へ結合される。例: resolve("plain.txt", "/tmp/sub") ->
  // "/tmp/sub/plain.txt"。空(既定値)にすると、このクラスの本来の
  // 挙動である「相対パスを入力された通りそのまま残す」動作になる
  // (exists()/is_directory()を実行する時点での「プロセス自身の」
  // カレントディレクトリを基準に解決される――「今画面上で閲覧中の
  // ディレクトリ」という概念とは違う。usilicは決してchdir()しない
  // ため(main.cpp/Application参照)、この2つは同じものではない)。
  // 実際のユーザー報告(2026-08-01)を受けて追加: 「^Xd/^Xfでは
  // ディレクトリ/ファイル名入力ができず」の原因はまさにこれだった
  // ――プロセスがたまたま起動された場所から別の場所へ移動した後に
  // 相対パスを入力すると、解決に静かに失敗していた(exists()がfalseを
  // 返すため、executePathInput()は目に見えるエラーも出さずに画面を
  // そのまま再描画するだけだった)。これはプロンプト自身のテキスト
  // 入力自体が壊れているわけでは無いのに、「入力が効かない」ように
  // 見えてしまう。
  ResolvedPath resolve(const std::string &rawPath, const std::string &baseDirectory = "") const;

 private:
  std::string homeDirectory_;
};

}  // namespace fm
