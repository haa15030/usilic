#include "archive_mode.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>

#include "child_process.hpp"

namespace fm {

namespace {

std::unique_ptr<ArchiveReader> readerFor(ArchiveKind kind) {
  switch (kind) {
    case ArchiveKind::Tar:
    case ArchiveKind::Deb:
      return std::make_unique<TarReader>();
    case ArchiveKind::Zip:
      return std::make_unique<ZipReader>();
    case ArchiveKind::Lha:
      return std::make_unique<LhaReader>();
    case ArchiveKind::Rpm:
      return std::make_unique<RpmReader>();
    case ArchiveKind::Rar:
      return std::make_unique<RarReader>();
    case ArchiveKind::SevenZip:
      return std::make_unique<SevenZipReader>();
    case ArchiveKind::Bzip2:
    case ArchiveKind::None:
      return nullptr;
  }
  return nullptr;
}

// application.cpp自身のtruncatedTrailingPath()と全く同じ慣習(先頭
// "..."+末尾16文字)の複製――2026-09-16追加、下のModeLineFieldsProvider
// 自身のコメント参照。application.cppのものはその翻訳単位のみの
// 匿名名前空間で定義されており、共有ヘッダには出ていないため、
// この小さな(4行の)ヘルパーはこのプロジェクトの既存の慣習(小さな
// 複製を、新たな結合を導入するより優先する)通りここでも複製する。
constexpr int kTrailingPathTailChars = 16;
std::string truncatedTrailingPath(const std::string &path) {
  if (static_cast<int>(path.size()) <= kTrailingPathTailChars) return path;
  return "..." + path.substr(path.size() - kTrailingPathTailChars);
}

}  // namespace

ArchiveMode::ArchiveMode(ContentView &view, History &history, const Configuration &configuration)
    : view_(view), history_(history), configuration_(configuration) {
  // マークされた行は、カーソルが外れていてもStandoutで描画される
  // ――DirectoryMode自身と同じギャップ、同じ修正(ContentView::
  // setMarkedPredicate()自身のコメント参照)。
  view_.setMarkedPredicate([this](int index) { return isMarked(index); });
  // 2026-08-26の修正、実際の報告("選択ファイルがアンダーライン表示に
  // ならない"): DirectoryMode自身のコンストラクタも、マークされた行を
  // カーソル行自身のプレーンなStandoutと視覚的に区別しておくために
  // setMarkedPredicate()とsetMarkedRowUsesUnderline(true)を組にして
  // いる(自身のコメント参照)――この呼び出しは単にここに欠けていた、
  // ArchiveModeのマーキングサポートが最初に移植された時の見落とし
  // である。
  view_.setMarkedRowUsesUnderline(true);
  // モードラインフィールド(2026-09-16の再設計、直接のユーザー指摘:
  // "表示元の情報が戻り値にある点が問題であってMANのように画面内に
  // 情報があれば問題ない"): 2026-08-19に確立した「name="ARC"固定+
  // trailing3=currentArchive_(アーカイブ自身のフルパス)」という形は、
  // trailing3(戻り値欄、実際に^Gを押した時の遷移先を示すべきフィール
  // ド)へ「今どのアーカイブを見ているか」という識別情報を押し込んで
  // いた――しかしreturnFromArchive()が実際に^Gで戻る先はアーカイブ
  // ファイル自身ではなくその「親ディレクトリ」であり、両者は食い違って
  // いた。MAN("MAN: <page>"のように画面内で識別情報を示す)と同じ考え
  // 方で、識別情報(アーカイブのフルパス)はname欄へ、trailing3には
  // 本当の戻り先(親ディレクトリ)を置く形に改めた。"ARC"という固定
  // 文字列自体は、CP/MV/RM同様、実際に^Oa(ARC展開)のプロンプトが
  // 発動した時にだけ表示される(drawPromptLabel()/promptLabelText()
  // 経由、既に別途正しく実装済み――このコンストラクタの変更対象外)。
  // ――trailingは依然として一覧自身の合計バイト数(2026-08-15の追加、
  // 変更なし)。
  //
  // trailing2(2026-08-19の追加修正、同日: "戻り値が非ハイライト"
  // ――実際の報告+スクリーンショットにより、下のrightWatermarkに
  // 元々置かれていたアーカイブ自身のパスが、ここの他の全ての
  // ハイライトされたフィールドが使う反転表示ではなく、プレーンな
  // 太字テキストで描画されていることが判明した): rightWatermarkは、
  // legacy「自身」の「表示すべき前モードのテキストが無いので、
  // 代わりにプログラム名/バージョンを表示する」フォールバック
  // (自身の構造体コメント参照)である――それが「そこで」意図的に
  // 反転表示ではないのは、それが静的なブランディングであり、実際の
  // 移動先情報ではないため。アーカイブ自身のパスは実際の、意味のある
  // 「戻り値」の内容である(他の全モード自身のハイライトされた
  // trailing2と同じカテゴリ)ので、代わりにハイライトされたフィールド
  // に属する。
  //
  // trailing2/trailing3の分割(2026-08-19、同日2回目の追加修正:
  // "「(tar)」と「/Users/.../utf8.tar」の間に空白が無い"): この修正の
  // 「最初の」試みは、"(<command>)"バッジとパスを、1つのtrailing2
  // 文字列("(tar) " + path)へ結合した。その文字列は「本物の」ASCII
  // 空白を含んでいたが、renderModeLine()は1つのフィールドを1つの
  // 途切れない反転表示の連続として描画する(理由は自身のコメント参照
  // ――CUIのモードラインはフィールドの「間」に黒いギャップを必要と
  // し、1つの中にではない)ので、埋め込まれた空白は同色の反転表示の
  // 空白として描画された――ここの他の全てのフィールドのペアの間の
  // 本物の箱の区切りとは異なり、自身の暗い背景に対して見えない。
  // バッジ(trailing2、DirectoryMode自身の同一の"(ls)"慣習に合わせて
  // いる)とパス(trailing3、自身のコメント参照)を2つの「別々の」
  // フィールドに分割することで、renderModeLine()自身の既存の
  // フィールドごとの描画を通じて、それらに本物のプレーンモードの
  // ギャップを与えられる。新しい描画ロジックは不要。
  //
  // fillTrailingToEdge(2026-08-19、同日2回): ここでは最初falseに
  // 設定されていた。「その」時点ではtrailing3はまだ端までの埋め
  // つぶしをサポートしておらず、これをtrueのままにしておくと
  // trailing2("(tar)")が行の右端まで丸ごとパディングされ、trailing3
  // (パス)を画面外へ押し出してしまうため。実際の追加報告(=dj:
  // "戻り値が右端まで埋まってない")は、他の全モード自身の最後の
  // trailingフィールドと同じように、パス自体を端まで埋めることを
  // 望んだ――trailing3も今はfillTrailingToEdgeを尊重する(自身の
  // renderModeLine()コメント参照)ので、これはtrueに戻された。
  //
  // archiveCommand_(実際に設定されているプログラム、例えば
  // "(tar)"/"(unzip)"/"(lha)"/"(rpm)"、loadArchive()の時点で設定
  // される、自身のコメント参照)。「入れ子の」アーカイブの場合、
  // currentArchive_は、人間にとって意味のある何かではなく、
  // Application::descendToNestedArchive()自身のmkdtemp()が生成した
  // 一時パスである。まさに同じ理由で、古いベース名のみの`name`
  // フィールドが受け入れていたのと同じ既知のトレードオフであり、
  // ここでは解決されていない。ユーザー自身の報告が特にトップ
  // レベル(入れ子でない)アーカイブについてのものだったため。
  view_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    if (currentArchive_.empty()) return {};
    std::string badge = archiveCommand_.empty() ? "" : "(" + archiveCommand_ + ")";
    // 親ディレクトリ(2026-09-16追加、このコンストラクタ自身の直前の
    // コメント参照): returnFromArchive()が実際に^Gで着地する先。
    // 2026-09-16の実測訂正: 当初はDirectory/File/Man/Locate/Du同様
    // 未切り詰めのまま渡す設計にしたが、doctestで実際に踏んだ通り、
    // nameが今やARCHIVE自身のフルパス(以前の固定"ARC"よりずっと長い)
    // になったことで、withTruncatedTrailing3()自身の予算計算(name分を
    // 差し引く)がしばしば4文字未満まで落ち込み、trailing3が丸ごと
    // 消える(2026-08-27に一度直したのと同じ「戻り値=空白」症状)ことが
    // 判明した。CP/MV/DU等のdrawPromptLabel()自身の戻り値表示が既に
    // 使っているtruncatedTrailingPath()(先頭"..."+末尾16文字)と
    // 同じ慣習で、name側の長さに関わらずtrailing3が常に何かしら
    // 意味のある内容を保つようにする。
    std::string parentDirectory = std::filesystem::path(currentArchive_).parent_path().string();
    if (!parentDirectory.empty() && parentDirectory.back() != '/') parentDirectory += '/';
    ContentView::ModeLineFields fields{currentArchive_, std::to_string(view_.totalByteCount()),
                                        /*fillTrailingToEdge=*/true, /*rightWatermark=*/"",
                                        /*trailing2=*/badge};
    fields.trailing3 = truncatedTrailingPath(parentDirectory);
    // showEditError_(2026-08-26、reportError()自身のコメント参照):
    // DirectoryMode自身のプロバイダがCP/MV/RMの「Permission denied」/
    // 「Invalid mode」向けに確立した(directory_mode.cpp)、「name
    // フィールドだけを、以降が何もずれないようパディングされた下線
    // 付きメッセージに置き換える」という同じ慣習――ここでARC自身の
    // 移動先検証エラー(^X^M/^X a)向けに逐語的に再利用されている。
    if (showEditError_) {
      std::string message = editErrorMessage_;
      if (message.size() < fields.name.size()) message.resize(fields.name.size(), ' ');
      fields.name = message;
      fields.nameUnderlined = true;
    } else if (view_.grepInPlaceActive() || view_.grepNoMatchActive()) {
      // 2026-09-11追加、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも
      // 反映してほしい"): DirectoryMode等自身の同じ分岐のコメント参照。
      // 2026-09-16修正: 外側の既定がparentDirectoryへ変わったため、
      // ここでは明示的にcurrentArchive_(検索元そのもの)へ戻す――
      // 検索中に見せるべきは「^Gの着地先」ではなく「今何を検索して
      // いるか」であり、これはArchiveMode自身の(新しい)name欄が示す
      // のと同じ識別情報。
      fields.name = view_.grepInPlaceLabel();
      fields.trailing2 = view_.grepInPlaceProgramBadge();
      fields.trailing3 = truncatedTrailingPath(currentArchive_);
    }
    return fields;
  });
}

// 2026-08-19の再設計、直接のユーザー訂正: "パーミッションも=dd
// の一文字（d、-）も不要だが、「utf8/」は縦一列に揃えてほしい"。先頭の
// isDirectoryを表す'd'/'-'マーカーは完全に取り除かれ、`size`は今や
// 古い「サイズ + リテラル1文字の'\t'」という形ではなく、
// `sizeWidth`(この一覧に実際に存在する最も広いサイズ、呼び出し元が
// 全エントリにわたって一度計算する――loadArchive()自身のコメント
// 参照)に右揃えされる――ContentView::renderLine()は、生の'\t'バイト
// を(本物のタブストップ計算ではなく)常に「1つの」プレーンな空白に
// 変換するだけ(自身のコメント参照)なので、そろっているように見えて
// も、名前は実際には決して列として揃っていなかった。パディングされた
// サイズの後の固定2文字の空白は、それぞれのサイズが何桁であっても、
// 全エントリの名前が全く同じ列から始まるようにする。
std::string ArchiveMode::formatEntryLine(const ArchiveEntry &entry, std::size_t sizeWidth) {
  std::string sizeText = std::to_string(entry.size);
  std::string line(sizeWidth > sizeText.size() ? sizeWidth - sizeText.size() : 0, ' ');
  line += sizeText;
  line += "  ";
  line += entry.name;
  return line;
}

bool ArchiveMode::loadArchive(const std::string &path) {
  ArchiveKind kind = ArchiveKind::None;
  auto command = configuration_.programForSuffix(path, &kind);
  if (!command || kind == ArchiveKind::None) return false;

  // 2026-08-30、直接のユーザー報告(Finderからの直接ダブルクリック
  // 起動限定で再現): ChildProcessOptions::forceUtf8CTypeForListing
  // 自身のコメント参照――tar(1)/unzip(1)等はLC_CTYPEで非ASCIIバイトを
  // 表示可能扱いするかどうかを決めており、シェル環境を引き継がない
  // Finder起動では暗黙のCロケールへ落ちて日本語ファイル名が8進
  // エスケープされてしまう。パイプ形式(.tgz等)ではChildProcess::
  // runPipeline()の第2段階(実際にtar tvf -を実行する側、prepareChild()
  // 経由)だけがこれを必要とする――第1段階(gzip -dc等の展開)は
  // ファイル名を一切見ないため無関係。
  ChildProcess::Options options;
  options.forceUtf8CTypeForListing = true;
  ChildProcess::Result result;
  if (command->pipedFromArgv) {
    // パイプ形式(例えば.tgz: gzip -dc <file> | tar tvf -)は、
    // アーカイブファイルを2番目ではなく「最初の」段階の入力として
    // 読む。
    std::vector<std::string> firstStage = *command->pipedFromArgv;
    firstStage.push_back(path);
    result = ChildProcess::runPipeline(firstStage, command->argv, options);
  } else {
    std::vector<std::string> argv = command->argv;
    argv.push_back(path);
    result = ChildProcess::run(argv, options);
  }
  if (!result.ok) return false;
  return finishLoadFromOutput(path, result.output);
}

// finishLoadFromOutput()自身の宣言コメント参照。loadArchive()自身の
// 「コマンドを実行する」部分の「後」半分――結果をパースし、状態を
// 確定する。
bool ArchiveMode::finishLoadFromOutput(const std::string &path, const std::string &output) {
  ArchiveKind kind = ArchiveKind::None;
  auto command = configuration_.programForSuffix(path, &kind);
  if (!command || kind == ArchiveKind::None) return false;

  auto reader = readerFor(kind);
  if (!reader) return false;

  std::vector<ArchiveEntry> entries = reader->parseListing(output);

  currentArchive_ = path;
  archiveKind_ = kind;
  archiveCommand_ = command->argv.empty() ? "" : command->argv.front();
  entries_ = std::move(entries);
  markedIndices_.clear();  // マークはそれが行われた一覧にスコープされる、DirectoryMode自身の慣習に合わせている

  std::vector<ContentView::Line> lines;
  lines.reserve(entries_.size());
  std::size_t sizeWidth = 0;
  for (const auto &entry : entries_) sizeWidth = std::max(sizeWidth, std::to_string(entry.size).size());
  // highlightStartColumn(2026-08-26の追加、直接のユーザー依頼:
  // "ハイライト表示を（ls）同様ファイル・ディレクトリ名のみにして欲し
  // い（それより左側が画面ドラッグ可能領域"): formatEntryLine()は
  // 常に、`sizeWidth`桁の右揃え数字 + 固定2文字の空白 + 名前として
  // 行をレイアウトするので、名前自身の開始列は単にsizeWidth + 2で
  // ある――DirectoryMode自身が、カーソルの反転表示を名前だけに
  // スコープするために、この同じLineフィールド(自身の宣言コメント
  // 参照)を同一に使っているのに合わせている。サイズの列自体は、
  // プレーンな、ハイライトされない(そしてhandleMouseClick()の新しい
  // isColumnWithinLineScope()チェック経由で、クリックスルー/ドラッグ
  // 専用の)余白のままにする。
  int nameStartColumn = static_cast<int>(sizeWidth) + 2;
  // ellipsisEligible=true: Line::ellipsisEligible自身のコメント参照。
  for (const auto &entry : entries_)
    lines.push_back({formatEntryLine(entry, sizeWidth), std::nullopt, nameStartColumn, {}, true});
  view_.setLines(std::move(lines));

  if (auto pos = history_.positionFor(PositionCategory::Screen, currentArchive_)) {
    int idx = pos->row;
    int last = static_cast<int>(entries_.size()) - 1;
    if (idx < 0) idx = 0;
    if (idx > last) idx = (last < 0) ? 0 : last;
    view_.setCursorIndex(idx);
  } else {
    view_.setCursorIndex(0);
  }
  view_.redraw();
  return true;
}

const std::string &ArchiveMode::currentArchive() const { return currentArchive_; }
ArchiveKind ArchiveMode::archiveKind() const { return archiveKind_; }
const std::vector<ArchiveEntry> &ArchiveMode::entries() const { return entries_; }

const ArchiveEntry *ArchiveMode::selectedEntry() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[idx];
}

std::optional<std::vector<unsigned char>> ArchiveMode::extractSelected() {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return std::nullopt;
  if (entries_[idx].isDirectory) return std::nullopt;

  auto reader = readerFor(archiveKind_);
  if (!reader) return std::nullopt;

  std::vector<unsigned char> data;
  switch (archiveKind_) {
    case ArchiveKind::Tar: {
      // tarのエントリは、生の.tarファイル自体の中の、実際にパース
      // 可能なヘッダーオフセットに座っている(TarReader::
      // locateContent()は本物のtarヘッダーを辿る)――素の.tarには
      // 展開は不要(.tgz/.tar.bz2のARCHIVEディレクティブの
      // pipedFromArgvは、TarReaderが一覧を見る前に既にそれを展開
      // 済みだが、ここでの展開は素の.tarのケースだけを扱う。クラス
      // コメント参照)。
      std::ifstream in(currentArchive_, std::ios::binary);
      if (!in) return std::nullopt;
      data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      break;
    }
    case ArchiveKind::Zip:
    case ArchiveKind::Lha: {
      // tarとは異なり、zip/lhaのエントリは生のアーカイブ内で個別に
      // 圧縮されているので――legacy自身のalloc_unzip()/alloc_lha()
      // 設計に合わせている(ZipReader/LhaReader自身のドキュメント
      // コメント参照)――ここでのlocateContent()は、生のアーカイブ
      // ファイルのバイト列ではなく、その形式の標準出力への展開
      // コマンド(`unzip -p`/`lha p`)の「展開された、連結された」
      // 出力を辿ることを意図している。
      ConfiguredRole role =
          (archiveKind_ == ArchiveKind::Zip) ? ConfiguredRole::UnzipExtract : ConfiguredRole::LhaExtract;
      ProgramCommand extractCommand = configuration_.commandFor(role);
      if (extractCommand.argv.empty()) return std::nullopt;
      std::vector<std::string> argv = extractCommand.argv;
      argv.push_back(currentArchive_);
      ChildProcess::Result result = ChildProcess::run(argv);
      if (!result.ok) return std::nullopt;
      data.assign(result.output.begin(), result.output.end());
      break;
    }
    case ArchiveKind::Rar:
    case ArchiveKind::SevenZip: {
      // すぐ上のZip/Lhaと同じ「標準出力へ展開し、連結された出力を
      // 辿る」アプローチ――それぞれが実際のアーカイブに対してどう
      // 検証されたかについてはRarReader/SevenZipReader自身の
      // ドキュメントコメント参照。
      ConfiguredRole role = (archiveKind_ == ArchiveKind::Rar) ? ConfiguredRole::RarExtract
                                                                : ConfiguredRole::SevenZipExtract;
      ProgramCommand extractCommand = configuration_.commandFor(role);
      if (extractCommand.argv.empty()) return std::nullopt;
      std::vector<std::string> argv = extractCommand.argv;
      argv.push_back(currentArchive_);
      ChildProcess::Result result = ChildProcess::run(argv);
      if (!result.ok) return std::nullopt;
      data.assign(result.output.begin(), result.output.end());
      break;
    }
    default:
      // Rpm: 内容位置特定サポートが全く無い(RpmReader参照)。
      // Deb: その一覧は「外側の」`ar`コンテナ(control.tar.gz/
      // data.tar.gz/debian-binary)であり、内側のペイロードではない
      // ――data.tar.gz内の実際のファイルに到達するには複数段階の
      // 展開(`ar p file.deb data.tar.gz | gzip -dc | tar xf -`)が
      // 必要で、この一切れではそれを構築しない。推測で済ませるので
      // はなく将来の課題として残す。
      return std::nullopt;
  }

  reader->locateContent(data.data(), data.size(), entries_);

  const ArchiveEntry &entry = entries_[idx];
  if (entry.contentOffset < 0) return std::nullopt;

  std::size_t start = static_cast<std::size_t>(entry.contentOffset);
  if (start > data.size()) return std::nullopt;
  std::size_t len = static_cast<std::size_t>(entry.size);
  if (start + len > data.size()) len = data.size() - start;  // contentTruncatedと整合させる

  return std::vector<unsigned char>(data.begin() + static_cast<long>(start),
                                     data.begin() + static_cast<long>(start + len));
}

void ArchiveMode::saveCursorPosition() {
  if (currentArchive_.empty()) return;
  History::CursorPosition pos;
  pos.row = view_.cursorIndex();
  pos.totalRows = static_cast<int>(entries_.size());
  pos.top = 0;
  pos.lineNumber = -1;
  history_.setPosition(PositionCategory::Screen, currentArchive_, pos);
}

void ArchiveMode::toggleMark(int index) {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return;
  auto it = markedIndices_.find(index);
  if (it != markedIndices_.end())
    markedIndices_.erase(it);
  else
    markedIndices_.insert(index);
}

bool ArchiveMode::isMarked(int index) const { return markedIndices_.count(index) > 0; }

std::size_t ArchiveMode::markedCount() const { return markedIndices_.size(); }

void ArchiveMode::clearMarks() { markedIndices_.clear(); }

void ArchiveMode::markAll() {
  markedIndices_.clear();
  for (int i = 0; i < static_cast<int>(entries_.size()); i++) markedIndices_.insert(i);
}

std::vector<ArchiveEntry> ArchiveMode::targetEntries() const {
  std::vector<ArchiveEntry> result;
  if (!markedIndices_.empty()) {
    for (int idx : markedIndices_) result.push_back(entries_[idx]);  // std::setは昇順で走査する
    return result;
  }
  if (const ArchiveEntry *entry = selectedEntry()) result.push_back(*entry);
  return result;
}

void ArchiveMode::reportError(const std::string &message) {
  editErrorMessage_ = message;
  showEditError_ = true;
}

void ArchiveMode::clearEditError() { showEditError_ = false; }

}  // namespace fm
