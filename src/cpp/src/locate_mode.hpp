#pragma once

#include <string>
#include <vector>

#include <sys/types.h>  // pid_t

#include "configuration.hpp"
#include "content_view.hpp"
#include "list_marks.hpp"

namespace fm {

// LOCATEモード(T_locate): `locate <query>`を実行し一致したパスを
// 表示する。init.cのT_locateケース(fork_bin() +
// Configuration::commandFor(Locate) + クエリ文字列を引数として)
// からの移植――menu.c自身のm_locate()は純粋なモード遷移/ModeStack
// 隣接のディスパッチ配線であり、locate固有のロジックを一切持たない
// ため、そちら側に移植すべきものは何も無かった。理由についてはこの
// 移植の他の全`*Mode`クラスのクラスコメント参照(このクラスではなく
// Mode/Applicationの仕事)。
//
// DirectoryModeの`ls`出力とは異なり、`locate`の出力は一切パースが
// 不要――1行に1パスがそのまま――なので、このクラスはFileMode/
// ArchiveMode/DuModeよりもさらに薄い。
//
// 意図的に除外(将来の課題、この段階では対象外): 選択された結果を
// どうするか(ディレクトリかファイルかに応じてDirectoryModeまたは
// FileModeへ引き渡す)の決定: この移植の他の全`*Mode`クラスが既に
// 引いているのと同じ引き渡しの境界。
class LocateMode {
 public:
  LocateMode(ContentView &view, const Configuration &configuration);

  // `locate query`を実行し結果を表示する。コマンドが実行できな
  // かった場合(locateが未設定/未インストール)、またはqueryが空の
  // 場合はfalse。一致0件の検索成功は失敗ではない――その後results()は
  // 単に空になるだけ。
  //
  // 2026-08-22追加(docs/CHANGES.md 406番、ユーザーからの直接依頼:
  // 「両方検索してマージを希望」): ConfiguredRole::LocateUserDbも
  // 設定されている場合(macOS、startUpdateDb()自身のコメント参照)、
  // その結果がシステムデータベース自身の結果の後ろに、重複排除した
  // 上で追加される――両方に存在するパスは1回だけ表示され、順序が
  // 重複する場合はシステムデータベース側が優先される。個人用
  // データベースがまだ存在しない場合(初回ビルド前)は、失敗ではなく
  // 「一致0件」と同じ扱いになる――LocateMode::search()自身のコメント
  // 参照。
  bool search(const std::string &query);

  // `updatedb`(locateデータベースを更新するため、K-39、^Jl)を
  // 「バックグラウンド」で起動する――完了を待たない。逐語的な移植
  // ではない――legacyにこの機能が無いにもかかわらずこれが存在する
  // 理由についてはConfiguredRole::UpdateDb自身のコメント
  // (configuration.cpp)参照。query_/results_にもview_にも一切触れ
  // ない: これは検索ではなく副作用を伴う保守アクションである。
  // 2026-08-14変更: 元々はブロッキング呼び出し(ChildProcess::run())
  // であり、そのためApplication::runLocateUpdateDb()は、本物の
  // (数分かかりうる)updatedb実行が完全に終わるまで^Gに応答できな
  // かった――実際のユーザー報告(「フリーズ中ではないメッセージ
  // なので...常時^Gで...移行」)。代わりにデタッチして実行する
  // (ChildProcess::runDetached())。呼び出し側は、自身のキーボード
  // 入力ループと並行して、返されたpidに対しChildProcess::hasExited()
  // をポーリングする。コマンドが全く起動できなかった場合(未設定/
  // 未インストール)は-1を返す。2026-08-22追加(docs/CHANGES.md
  // 406番): ConfiguredRole::LocateUserDbBuildも設定されている場合
  // (macOS、Configuration::setUserLocateDatabasePath()――既存の
  // 全テストでは未設定なので、そちらでは純粋に無変化)、そのビルド
  // コマンドがシステム更新自身の後ろに連結され、両方とも1つの
  // デタッチされた`sh -c`子プロセス内に収まる――そのためこれは依然
  // としてちょうど1つのpidを返すだけで済み、Application::
  // awaitingLocateUpdateDb()/pollLocateUpdateDbCompletion()には一切
  // 変更が不要である。`&&`ではなく`;`で連結している: システム更新が
  // 完全に失敗した場合(例えば事前の手動`sudo`が無い、この移植が
  // そこを自分でsudoに包まない理由についてはConfiguredRole::
  // UpdateDb自身のコメント参照)でも、個人用データベースは(再)
  // ビルドされる――2つのデータベースは独立しており、片方の失敗は
  // もう片方をスキップする理由にはならない。
  pid_t startUpdateDb();

  const std::string &query() const;
  const std::vector<std::string> &results() const;
  const std::string *selectedResult() const;
  // 選択(2026-09-27、list_marks.hpp自身のコメント参照)。一覧を作り直すと
  // 消える。
  ListMarks &marks() { return marks_; }
  const ListMarks &marks() const { return marks_; }

 private:
  ContentView &view_;
  const Configuration &configuration_;

  std::string query_;
  std::vector<std::string> results_;
  ListMarks marks_;
};

}  // namespace fm
