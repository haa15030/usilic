#include "pipe_buffer.hpp"

#include <utility>

namespace fm {

PipeBuffer::PipeBuffer(Charset::Encoding encoding, int width, long maxBytes)
    : encoding_(encoding), width_(width), maxBytes_(maxBytes) {}

void PipeBuffer::feed(const std::string &bytes) {
  rawPending_.append(bytes);
  processPending();
}

void PipeBuffer::flush() {
  if (!rawPending_.empty()) {
    wrapLine(convertChunk(rawPending_));
    rawPending_.clear();
  }
  if (!wrapText_.empty() || !wrapAttrs_.empty()) {
    lines_.push_back({wrapText_, wrapAttrs_, false, nextRawLineNumber_});
    usedBytes_ += static_cast<long>(wrapText_.size());
    if (usedBytes_ > maxBytes_) overBudget_ = true;
    wrapText_.clear();
    wrapAttrs_.clear();
    wrapCol_ = 0;
  }
}

const std::vector<StyledLine> &PipeBuffer::lines() const { return lines_; }

bool PipeBuffer::overBudget() const { return overBudget_; }

// rawPending_から完全な生の行(各'\n'を含むところまで)を消費し、
// それぞれを文字コード変換・幅折り返しする。最後の'\n'より後は
// 次のfeed()呼び出しのためにrawPending_へ残る――これにより、feed()
// を(例えばライブのパイプからバイトが届くたびのような)任意に
// 小さなチャンクで呼んでも、行が完全に受信される前に文字コード
// 判定や折り返しを行ってしまうことが決して無い。
void PipeBuffer::processPending() {
  std::size_t start = 0;
  while (true) {
    std::size_t eol = rawPending_.find('\n', start);
    if (eol == std::string::npos) break;
    std::string rawLine = rawPending_.substr(start, eol - start + 1);
    wrapLine(convertChunk(rawLine));
    start = eol + 1;
  }
  rawPending_.erase(0, start);
}

// linein()の文字コード判定/変換からの移植で、今ではlinein()自身の
// init_code()/toeuc()/jistoeuc()/euctoutf8_buf()呼び出しの代わりに、
// 既に移植済みのCharsetクラスの上に構築されている。MIMEエンコード
// (base64/quoted-printable)されたコンテンツは扱わない――Charset
// 自体がそれに対応していない(charset.hpp参照: そのクラスを設計
// した時点で、ユーザーの「そんな機能すら覚えてない」という判断に
// より除外済み)。
std::string PipeBuffer::convertChunk(const std::string &rawChunk) {
  if (encoding_ == Charset::Encoding::Ascii) return rawChunk;

  const auto *buf = reinterpret_cast<const unsigned char *>(rawChunk.data());
  Charset::Encoding detected = Charset::detect(buf, static_cast<int>(rawChunk.size()), encoding_);

  std::string euc;
  switch (detected) {
    case Charset::Encoding::Sjis: {
      auto out = Charset::sjis_to_euc(buf, static_cast<int>(rawChunk.size()));
      euc.assign(out.begin(), out.end());
      break;
    }
    case Charset::Encoding::Jis: {
      auto out = Charset::jis_to_euc(buf, static_cast<int>(rawChunk.size()));
      euc.assign(out.begin(), out.end());
      break;
    }
    case Charset::Encoding::Euc:
      euc = rawChunk;
      break;
    default:
      return rawChunk;  // Ascii/Utf8/Binary: そのまま通す
  }

  if (encoding_ == Charset::Encoding::Utf8) {
    auto out = Charset::euc_to_utf8(reinterpret_cast<const unsigned char *>(euc.data()),
                                     static_cast<int>(euc.size()));
    return std::string(out.begin(), out.end());
  }
  return euc;
}

int PipeBuffer::byteLengthAt(const std::string &s, int pos) const {
  if (pos >= static_cast<int>(s.size())) return 1;
  unsigned char c = static_cast<unsigned char>(s[pos]);
  if (encoding_ == Charset::Encoding::Utf8) return Charset::sequence_length(c);
  if (encoding_ == Charset::Encoding::Euc) {
    if (c == 0x8e) return 2;    // SS2半角カナ
    if (c >= 0xa1) return 2;    // 通常の2バイトEUCペア
    return 1;
  }
  return 1;
}

int PipeBuffer::widthAt(const std::string &s, int pos, int charBytes) const {
  if (encoding_ == Charset::Encoding::Utf8)
    return Charset::display_width(reinterpret_cast<const unsigned char *>(s.data()) + pos, charBytes);
  if (encoding_ == Charset::Encoding::Euc && charBytes == 2 &&
      static_cast<unsigned char>(s[pos]) == 0x8e)
    return 1;  // 半角カナ: 2バイトだが幅は1カラムだけ
  return charBytes;  // EUC 2バイト漢字: 2バイト=2カラム、ASCII: 1バイト=1カラム
}

// 2026-09-04追加、直接のユーザー依頼("禁則処理（句読点が行の冒頭に
// 来ない）を実施"): 行頭禁則(句読点・閉じ括弧・小書きの仮名等が
// 折り返しの結果として行の先頭に来ないようにする、日本語組版の
// 基本ルールの1つ)。UTF-8限定(HELP自身のテキストは常にUTF-8――
// help_text.hpp自身のコメント参照。EUCコンテンツについては、この
// 一覧をEUCバイト列でも二重に持つ手間に見合うほどの実際の要望が
// まだ無いため、今回は対象外のままにしてある)。
//
// 2026-09-05訂正、実機での実際の再現(QUICK.txt由来のexplanationを
// 実際に表示して発覚、直接のユーザー報告"禁則処理が利いていないの
// と、一文字欠落がある")を受けて、「追い出し」の実装方針を作り直した:
// 以前は単に「たとえその行が予約列幅を1文字分超えることになっても
// 押し込む」(幅超過を許容)方式だったが、-g(GraphicalDisplay)は
// columns()ぴったりの大きさのセル格子しか持たず、その超過分のセルへ
// は本質的に一切描画できない(境界チェックで黙って切り捨てられる)と
// 判明した。今は本来の日本語組版の「追い出し」――直前の「普通の」
// 文字を今の行から取り除いて次の行へ戻し、その分だけ空いた幅へ禁則
// 対象の文字を収める――に作り直してある(popLastCharForKinsoku()
// 参照)。これにより、行がwidth_を超えることはもう無い。連続する
// 2文字以上の禁則文字(例えば"」。")までは追いかけない(1段階の
// 押し出しのみ)――実用上ほぼ現れない組み合わせのための複雑化を
// 避ける単純化。
static bool isForbiddenAtLineStart(const std::string &s, int pos, int len) {
  static const char *const kForbidden[] = {
      // 句読点・区切り記号
      "、", "。", "，", "．", "・", "：", "；", "？", "！", "ー",
      // 閉じ括弧・引用符
      "）", "〕", "］", "｝", "〉", "》", "」", "』", "】", "〙", "〗", "〟", "’", "”", "｠", "»",
      // 小書きの仮名
      "ぁ", "ぃ", "ぅ", "ぇ", "ぉ", "っ", "ゃ", "ゅ", "ょ", "ゎ",
      "ァ", "ィ", "ゥ", "ェ", "ォ", "ッ", "ャ", "ュ", "ョ", "ヮ", "ヵ", "ヶ",
  };
  for (const char *f : kForbidden) {
    auto flen = static_cast<int>(std::char_traits<char>::length(f));
    if (flen == len && s.compare(static_cast<std::size_t>(pos), static_cast<std::size_t>(len), f) == 0) return true;
  }
  return false;
}

// alloc_pipe()の文字ごとのループ(タブ/幅の折り返し)とsave_attr()
// (オーバーストライク検出)からの移植で、legacyの反応的なチェック
// (「今バックスペースを見た、直前の文字はその一部だったか?」)では
// なく、前方を見るチェック(「『次』のものはオーバーストライク文字
// のように見えるか?」)を軸に再構成してある。これにより、既に出力
// 済みの位置を後からパッチする必要無く、検出した各範囲を1ステップで
// 出力できる。移植されているパターン:
//   "_\bX"     -> 下線(legacyのA_us)
//   "__\b\bX"  -> 下線、幅広の元データバリアント(legacyのA_us2)
//   "X\bX"     -> 太字、Xは1バイト(ASCII)文字(legacyのA_md)
//   "X\b\bX"   -> 太字、Xは幅2カラムの文字――2バイトEUC漢字、または
//                 (UTF-8モードでは)マルチバイトCJK文字
//                 (legacyのA_md2、CLAUDE.mdの作業ログ8番のUTF-8修正
//                 を含む)
// legacyのA_md3("X\bY"でYが単一の非ASCIIバイト)は移植していない
// ――クラスコメント参照。
void PipeBuffer::wrapLine(const std::string &convertedLine) {
  int n = static_cast<int>(convertedLine.size());
  int i = 0;

  // 2026-09-05追加、isForbiddenAtLineStart()自身のコメント参照
  // (禁則の「追い出し」実装の作り直し): 直近に配置した1文字を、
  // 必要なら丸ごと取り消して次の行へ差し戻せるように、その1文字
  // 「だけ」についての最小限の記録を保つ。finalize()のたびに(新しい
  // 行には取り消す対象が何も無いので)無効化する。
  int lastCharSeqStart = -1;      // その文字の処理がconvertedLine中で始まった位置(このループのトップでのi)
  int lastCharRenderedBytes = 0;  // wrapText_へ実際に書き込まれたバイト数
  int lastCharWidth = 0;          // その文字の表示カラム数
  int lastCharAttrCount = 0;      // その文字のためだけにwrapAttrs_へ積んだ要素数(太字/下線+斜体で最大2)

  auto finalize = [&](bool endsWithNewline) {
    lines_.push_back({wrapText_, wrapAttrs_, endsWithNewline, nextRawLineNumber_});
    if (endsWithNewline) nextRawLineNumber_++;
    usedBytes_ += static_cast<long>(wrapText_.size());
    if (usedBytes_ > maxBytes_) overBudget_ = true;
    wrapText_.clear();
    wrapAttrs_.clear();
    wrapCol_ = 0;
    lastCharSeqStart = -1;
  };
  // 直近に配置した1文字を取り消し、その処理を始めた位置まで`i`を
  // 巻き戻す――呼び出し元がすぐ後にfinalize()+continueすることで、
  // メインループが次の反復でその文字を、真新しい(まだ何も置かれて
  // いない)行の先頭として一から処理し直す。取り消す対象が無ければ
  // (このwrapLine()呼び出し内で一度も文字を置いていない、または
  // 既に一度使って無効化済み)false。「太字/下線+斜体で最大2つ」
  // 以上のAttributeRunがこの1文字に対して積まれることは無い
  // (currentItalic_自身のコメント参照)ので、lastCharAttrCount個
  // だけ末尾からpop_back()すれば正確に取り消せる。
  auto popLastCharForKinsoku = [&]() -> bool {
    if (lastCharSeqStart < 0) return false;
    wrapText_.resize(wrapText_.size() - static_cast<std::size_t>(lastCharRenderedBytes));
    for (int k = 0; k < lastCharAttrCount && !wrapAttrs_.empty(); k++) wrapAttrs_.pop_back();
    wrapCol_ -= lastCharWidth;
    i = lastCharSeqStart;
    lastCharSeqStart = -1;
    return true;
  };
  // 2026-09-04訂正、実機での実際の再現(初回のsrc/HELP.txt全面改稿を
  // 実際に流し込んで確認して発覚): 上のfinalize()自身が新しい行を
  // currentIndent_ぶんのスペースで「即座に」種付けしていたのが
  // 誤りだった――<indentN>を可読性のためタグだけの行に置く著者の
  // 書き方(例: "<indent4>\n^H : ...\n</indent>")では、
  // "</indent>"がすぐ後に続く行(継続行にすべき本物のコンテンツが
  // 一切無い行)にまで先回りしてパディングだけを種付けしてしまい、
  // </indent>で幅0のまま閉じた後、次の実際の改行でその「パディング
  // だけの」行が単独でlines_へ押し出されていた――見た目には章1の
  // 冒頭で意図しない余分な空行(かつ完全な空行ではなくスペースだけを
  // 含む行)として現れた。修正: 種付けを「即座」ではなく「遅延」に
  // する――下の実際の文字配置(オーバーストライク文字・素の文字の
  // 両方の配置箇所)の直前で、wrapCol_==0(=新しい行の最初の1文字目)
  // かつcurrentIndent_>0の場合にだけ、その場でパディングを継ぎ足す。
  // これにより、改行や</indent>それ自体(幅を持たない)しか続かない
  // 行では一切パディングが入らなくなる――本物の可視文字が実際に
  // 置かれようとしている時だけ発動するため。
  // 2026-09-12修正、直接のユーザー依頼("indentタグのスペース数指定を
  // 全角ではなく半角にしてほしい(<indent2>は全角ではなく半角スペース
  // が２個)"): <indentN>のNは、それまで「全角スペースN個」(1個あたり
  // 表示幅2、U+3000)を意味していたが、この依頼によりN「半角スペース」
  // (1個あたり表示幅1、ASCII 0x20)へ変更した。expandHelpMarkup()/
  // wrapLine()自身の\x01+数字1桁というエンコーディング自体(help_text.cpp
  // 参照)は変更していない――digitの意味付け(1個あたりの実際の見た目幅)
  // だけがここで変わる。既存のsrc/HELP.txt/QUICK.txt側のNの値自体は、
  // 半分の見た目幅を意図した新しい値へ著者側で調整し直す必要がある
  // (このコミット自体はレンダラのみを変更、本文側の調整は別途)。
  auto seedIndentIfAtLineStart = [&]() {
    if (wrapCol_ != 0 || currentIndent_ <= 0) return;
    for (int k = 0; k < currentIndent_; k++) {
      wrapText_ += " ";
      wrapCol_ += 1;
    }
  };

  // 2026-09-04追加、実機での実際の再現(初回のsrc/HELP.txt全面改稿を
  // 実際に流し込んで発覚): 幅ちょうどで満杯になった直後の位置から、
  // (もしあれば)\x01/\x02の制御バイト対を読み飛ばして、その先が
  // 本物の改行かどうかを覗き見る――禁則処理の「追い出し」(自身の
  // コメント参照)によって、段落の最後の文字がちょうど行の残り幅を
  // 使い切ってしまうことがあり、その場合、幅超過finalize(false)の
  // すぐ後に本物の改行がもう1つ続いてしまい、既に幅超過finalizeで
  // 全て吐き出し済みの(空の)wrapText_に対してその改行がfinalize(true)
  // を起こし、見せかけの空行を生んでいた。読み飛ばした制御バイト
  // 自身の状態更新(currentIndent_/currentItalic_)は、メインループが
  // 本来行うのと同じ効果になるようここでも適用する――呼び出し元が
  // 改行を見つけられず結局そのままメインループへ処理を戻す場合でも、
  // 同じ制御バイトを再度たどるだけなので安全(冪等)。改行が見つかれば
  // その位置を返す(呼び出し元はfinalize(true)扱いにしてそこまで
  // 読み進める)、見つからなければ-1。
  // 2026-09-12修正、直接のユーザー依頼("<indentN>を<indentNN>
  // （二桁対応）にして"): \x01(字下げ)の直後は、expandHelpMarkup()
  // 自身のコメント参照の通り、元のタグの数字を1〜2文字そのまま転写
  // したASCII数字「文字」列のまま(値そのものを1バイトの生バイトに
  // する案は、本物のCtrl-A(0x01)がたまたま非数字の後続バイトを伴う
  // 場合に誤認してしまうため撤回した)。ここで最大2文字の連続した
  // 数字を読んで10進数として組み立てる――1文字も数字が続かなければ
  // 自分のマーカーではない(本物のCtrl-A等)とみなし読み飛ばさない。
  // 戻り値: {読み取った値, 数字列の直後の位置}。1文字も数字が続か
  // なければ{-1, pos}(呼び出し元は「自分のマーカーではない」と判断
  // する)。
  auto readIndentDigits = [&](int pos) -> std::pair<int, int> {
    int value = -1, p = pos;
    for (int k = 0; k < 2 && p < n && convertedLine[p] >= '0' && convertedLine[p] <= '9'; k++, p++) {
      if (value < 0) value = 0;
      value = value * 10 + (convertedLine[p] - '0');
    }
    return {value, p};
  };
  auto skipMarkersFrom = [&](int pos) -> int {
    while (pos + 1 < n) {
      unsigned char mc = static_cast<unsigned char>(convertedLine[pos]);
      if (mc == '\x01') {
        auto [value, digitsEnd] = readIndentDigits(pos + 1);
        if (value < 0) break;
        currentIndent_ = value;
        pos = digitsEnd;
        continue;
      }
      unsigned char value = static_cast<unsigned char>(convertedLine[pos + 1]);
      if (mc == '\x02' && value >= '0' && value <= '9') {
        currentItalic_ = (value == '1');
      } else {
        break;
      }
      pos += 2;
    }
    return pos;
  };
  auto peekNewlineAfterMarkers = [&](int pos) -> int {
    int p = skipMarkersFrom(pos);
    return (p < n && convertedLine[p] == '\n') ? p : -1;
  };
  // 2026-09-05追加、直接のユーザー報告(実際のQUICK.txt由来の
  // explanationを流し込んで発覚、スクリーンショット――「。」が
  // 単独の行として現れる): isForbiddenAtLineStart()自身の禁則
  // チェック(上のkeepStart側/下のプレーン文字側、どちらも)は、
  // 「この文字を置くと幅を超える」場合にしか発動しない――しかし
  // もっとありふれた実際のケースは、直前の文字がちょうど残り幅を
  // 使い切ってfinalize()され、次の(禁則対象の)文字が真新しい行の
  // 先頭(wrapCol_==0)から始まる場合であり、この時はどの文字を
  // 置いても「幅を超える」ことには一度もならない(0+2>widthは
  // 通常false)ので、上のどちらのkinsoku分岐も一度も発動しない
  // ――まさにこのすり抜けにより「。」が新しい行の先頭に単独で
  // 残っていた。ここでは、ちょうど幅ぴったりで満杯になった直後の
  // finalize()を行う「前」に、次の(マーカーを読み飛ばした先の)
  // 本物の文字が禁則対象かどうかを覗き見る――対象なら、ここでは
  // 確定させず、そのままメインループの次の反復へ委ねる: そちらは
  // wrapCol_が既にwidth_に達しているので、次の文字は
  // 「wrapCol_+w>width_」の本物のオーバーフロー分岐に必ず入り、
  // 既存の禁則の「追い出し」ロジックがそのまま正しく処理する。
  auto nextIsForbiddenAtLineStart = [&](int pos) -> bool {
    if (encoding_ != Charset::Encoding::Utf8) return false;
    int p = skipMarkersFrom(pos);
    if (p >= n || convertedLine[p] == '\n') return false;
    int len = byteLengthAt(convertedLine, p);
    if (p + len > n) return false;
    return isForbiddenAtLineStart(convertedLine, p, len);
  };

  while (i < n) {
    int placementStart = i;  // popLastCharForKinsoku()自身のコメント参照
    unsigned char c = static_cast<unsigned char>(convertedLine[i]);
    // 2026-09-04追加(currentIndent_自身のコメント参照): expandHelpMarkup()
    // が<indentN>/</indent>から生成する制御バイト\x01+1バイト――幅を
    // 持たないゼロ幅の指示なので、wrapText_/wrapAttrs_には一切書き
    // 込まず、currentIndent_を更新するだけで読み飛ばす。2026-09-12
    // 修正(readIndentDigits()自身のコメント参照、二桁対応): 直後の
    // 1〜2バイトはASCII数字文字のままなので、それらを読んで10進数を
    // 組み立てる――数字が1つも続かなければ(本物のCtrl-A等)このまま
    // 素通りさせる。
    if (c == '\x01' && i + 1 < n) {
      auto [value, digitsEnd] = readIndentDigits(i + 1);
      if (value >= 0) {
        currentIndent_ = value;
        i = digitsEnd;
        continue;
      }
    }
    // currentItalic_自身のコメント参照(pipe_buffer.hpp): expandHelpMarkup()
    // が<i>/</i>から生成する制御バイト\x02+数字1桁――\x01と全く同じ
    // 「ゼロ幅、状態だけ更新して読み飛ばす」形。
    if (c == '\x02' && i + 1 < n) {
      unsigned char digit = static_cast<unsigned char>(convertedLine[i + 1]);
      if (digit == '0' || digit == '1') {
        currentItalic_ = (digit == '1');
        i += 2;
        continue;
      }
    }
    if (c == '\n') {
      finalize(true);
      i++;
      continue;
    }

    // 2026-09-10更新、直接のユーザー提案: 行のどの位置のタブも同じ
    // 「U+2060(WORD JOINER、幅0)を1つ差し込んでから、従来通りの
    // 個数のスペースを展開する」という扱いへ統一した。U+2060は幅0
    // (Utf8::display_width()、2026-09-10にU+200Bから差し替え済み――
    // 実端末での目視確認で真に不可視と確認済み、そちら自身のコメント
    // 参照)なので、見た目の桁揃えは今まで通り「スペースでタブを
    // 偽装している」ままで一切変わらない。その一方、生のU+2060バイト
    // (e2 81 a0)がwrapText_という、GREP/CUT等が検索対象にする
    // まさにそのバッファに実際に残るため、^Q^Iで打ち込んだタブが
    // 内部でU+2060へ変換されて検索されれば(GrepMode::search()側の
    // 対応、未実装ならそちらで対応)、一致するようになる――ディレクトリ
    // 一覧のls出力等、もともと本物のタブを含むテキストであれば、
    // 何もしなくても「タブらしく見える」まま検索可能になるという
    // 設計。以前(2026-09-05追加)は「行末のタブだけ生バイトのまま
    // 残し、行途中のタブはスペースへ展開して情報を失う」という
    // 非対称な特例だった(直接のユーザー報告"^Q^Iでは入ってもgrepで
    // ヒットせず"を受けての場当たり的な対処)が、全位置のタブを同じ
    // U+2060方式に統一したことでこの特例自体が不要になったため、
    // 行末/行途中の区別(旧: peekNewlineAfterMarkers()によるlookahead)
    // ごと削除した。
    if (c == '\t') {
      // disp.c自身の「'\t'を' 'へ変換する」というコメント(そちら自身の
      // count_h()/count_i()による列数カウント処理も、何かがdisplay層へ
      // 到達する前に同じ置換を行っている)からの移植――生のタブバイト
      // 自体は残さない。ContentView::renderLine()は0x20未満のバイトを、
      // 反転表示が必要な制御コードとして扱う(K_at/K_dlの慣習、自身の
      // コメント参照)ため、生のタブバイトがwrapText_まで生き残ると、
      // 空白ではなく目に見える反転表示のグリフとして表示されてしまう
      // (実際のユーザー報告、2026-08-02、スクリーンショットにより発見)。
      wrapText_.append("\xE2\x81\xA0");  // U+2060 WORD JOINER(幅0)
      int step = 8 - (wrapCol_ % 8);
      if (wrapCol_ + step > width_) step = width_ - wrapCol_;
      step = std::max(0, step);
      wrapCol_ += step;
      wrapText_.append(static_cast<std::size_t>(step), ' ');
      i++;
      if (wrapCol_ >= width_) {
        // 2026-09-10追加: 旧来の(タブがちょうど行末まで埋めたとき、
        // 直後の本物の改行を別建てのfinalize(true)にせず同じfinalizeへ
        // 吸収する)挙動を維持する――以前は行末のタブだけがここを通らず
        // 下の一般文字パスへフォールスルーしていた(すぐ上のコメント
        // 参照)ためこの処理を自然に受け取れていたが、全位置のタブを
        // この専用分岐へ統一した今、ここで明示的に同じ
        // peekNewlineAfterMarkers()を呼ばないと、空行が1行余分に
        // 出力されてしまう(下の一般文字パス自身の同じ呼び出し参照)。
        int nlPos = peekNewlineAfterMarkers(i);
        if (nlPos >= 0) {
          i = nlPos + 1;
          finalize(true);
        } else {
          finalize(false);
        }
      }
      continue;
    }

    int keepStart = -1, keepBytes = 0;
    Display::Attribute attr = Display::Attribute::Bold;

    if (c == '_' && i + 1 < n && static_cast<unsigned char>(convertedLine[i + 1]) == 0x08) {
      if (i + 3 < n && convertedLine[i + 2] == '_' &&
          static_cast<unsigned char>(convertedLine[i + 3]) == 0x08) {
        int pos = i + 4;
        int len = byteLengthAt(convertedLine, pos);
        if (pos + len <= n) { keepStart = pos; keepBytes = len; attr = Display::Attribute::Underline; }
      } else if (i + 2 < n) {
        int pos = i + 2;
        int len = byteLengthAt(convertedLine, pos);
        if (pos + len <= n) { keepStart = pos; keepBytes = len; attr = Display::Attribute::Underline; }
      }
    } else {
      int charBytes = byteLengthAt(convertedLine, i);
      if (i + charBytes < n && static_cast<unsigned char>(convertedLine[i + charBytes]) == 0x08) {
        if (i + charBytes + 1 < n &&
            static_cast<unsigned char>(convertedLine[i + charBytes + 1]) == 0x08) {
          int pos = i + charBytes + 2;
          int len = (encoding_ == Charset::Encoding::Utf8) ? byteLengthAt(convertedLine, pos) : 2;
          if (pos + len <= n) { keepStart = pos; keepBytes = len; attr = Display::Attribute::Bold; }
        } else {
          int pos = i + charBytes + 1;
          // 2026-09-12修正、直接のユーザー報告("QUICK.txt:237 ↑↓に
          // ^Hコードが混入"): expandHelpMarkup()(help_text.cpp)自身の
          // <b>/<u>エンコーディングは、文字の実バイト幅に関わらず常に
          // 単一のバックスペース1個で重ね打ちを表す("1文字ごとに重ね
          // 打ちを生成する"、そちら自身のコメント参照)――legacy由来の
          // EUC規約(1バイト文字は単一BS、2バイト幅文字は二重BS)とは
          // 別物。単一BSの直後を0x80未満(ASCII)限定にしていたこの
          // 分岐は、UTF-8のマルチバイト文字(例: <b>↑</b>が生成する
          // "↑\b↑")を認識できずに素通しさせてしまい、生のバックスペース
          // がそのまま反転表示の制御グリフとして画面に現れていた。
          // UTF-8エンコーディング時に限り(legacyのEUC規約自体には
          // 一切手を付けない――下のelse分岐)、単一BSの直後も実際の
          // 1文字分のバイト長を読み取るよう広げる。
          if (encoding_ == Charset::Encoding::Utf8) {
            int len = byteLengthAt(convertedLine, pos);
            if (pos + len <= n) {
              keepStart = pos;
              keepBytes = len;
              attr = Display::Attribute::Bold;
            }
          } else if (pos < n && static_cast<unsigned char>(convertedLine[pos]) < 0x80) {
            keepStart = pos;
            keepBytes = 1;
            attr = Display::Attribute::Bold;
          }
        }
      }
    }

    if (keepStart >= 0) {
      seedIndentIfAtLineStart();  // 上のオーバーフロー判定より前――そちら自身のコメント参照
      int w = widthAt(convertedLine, keepStart, keepBytes);
      if (wrapCol_ + w > width_) {
        // isForbiddenAtLineStart()自身のコメント参照(禁則処理、
        // 2026-09-04追加、2026-09-05に「追い出し」の実装方針を作り
        // 直した): この文字を新しい行の先頭にする代わりに、直前に
        // 置いたばかりの1文字を取り消して次の行へ差し戻し、空いた
        // 分の幅にこの文字を(改めて真新しい行の上で)収める。
        // wrapCol_ > 0の条件は、そもそも行の先頭で発生した(=差し戻す
        // 先の「前の文字」が無い)場合の無意味な適用を避けるため。
        bool kinsoku = wrapCol_ > 0 && encoding_ == Charset::Encoding::Utf8 &&
                       isForbiddenAtLineStart(convertedLine, keepStart, keepBytes);
        if (!kinsoku) { finalize(false); continue; }
        if (popLastCharForKinsoku()) { finalize(false); continue; }
        // popLastCharForKinsoku()が取り消す対象を持たなかった(通常は
        // 起こらない――wrapCol_>0が既に何か置かれていることを保証
        // する)場合のみ、フォールバックとして以前通りの幅超過を
        // 許容する: 隙間より文字化けの方がまし、という既存のトレード
        // オフの選択(既存のScrollbar系コメント等、随所で繰り返されて
        // いる判断)に戻る。
      }
      int start = static_cast<int>(wrapText_.size());
      wrapText_.append(convertedLine, keepStart, keepBytes);
      wrapAttrs_.push_back({start, start + keepBytes, attr});
      // 2026-09-04追加、直接のユーザー報告("-cでは斜体にならない"=fk)
      // を調査して発見した実際のバグの修正: <b>/<u>のオーバーストライク
      // 検出(この分岐)は、下の素のプレーン文字パス(currentItalic_自身の
      // コメント参照)と違い、これまでcurrentItalic_を一切見ておらず、
      // <i><b>fgrep</b>(1)</i>のように<i>の中に<b>/<u>がネストした場合、
      // 「fgrep」側のBold用AttributeRunだけが積まれ、Italic用の
      // AttributeRunが丸ごと欠落していた――実機再現の生バイト列
      // キャプチャで確認済み: "\x1b[1mfgrep\x1b(B\x1b[m\x1b[3m(1)\x1b[23m"
      // となっており、"fgrep"の直前にあるべき"\x1b[3m"(斜体ON)が
      // どこにも無い。複数のAttributeRunが同じバイト範囲に重なること
      // 自体は元々想定内(ContentView::renderLine()自身の消費ループは
      // 全runを走査してBold/Underline/Standout/Italicそれぞれ独立に
      // ORするだけ)なので、単にここでも同じ範囲へItalic用の
      // AttributeRunをもう1つ積むだけでよい。
      if (currentItalic_) wrapAttrs_.push_back({start, start + keepBytes, Display::Attribute::Italic});
      wrapCol_ += w;
      i = keepStart + keepBytes;
      lastCharSeqStart = placementStart;
      lastCharRenderedBytes = keepBytes;
      lastCharWidth = w;
      lastCharAttrCount = currentItalic_ ? 2 : 1;
      if (wrapCol_ >= width_) {
        // popLastCharForKinsoku()自身のコメント参照(禁則処理の作り
        // 直し): ちょうど幅ぴったりで満杯になった直後、次の(マーカー
        // を読み飛ばした先の)本物の文字が禁則対象なら、今置いた
        // ばかりのこの1文字を取り消して次の行へ差し戻す――こうすれば
        // 禁則対象の文字は、取り消された文字と一緒に真新しい行の上で
        // 収まる(禁則対象自身が行の先頭に来ることに変わりは無いが、
        // その直前に取り消された文字が「先に」来るので、実際には
        // 行の2文字目になる)。
        if (nextIsForbiddenAtLineStart(i) && popLastCharForKinsoku()) {
          finalize(false);
          continue;
        }
        // peekNewlineAfterMarkers()自身のコメント参照。
        int nlPos = peekNewlineAfterMarkers(i);
        if (nlPos >= 0) {
          i = nlPos + 1;
          finalize(true);
        } else {
          finalize(false);
        }
      }
      continue;
    }

    int charBytes = byteLengthAt(convertedLine, i);
    if (i + charBytes > n) charBytes = n - i;  // チャンク末尾で切り詰められたシーケンス
    seedIndentIfAtLineStart();  // 上のkeepStart側の呼び出し箇所と全く同じ理由・同じ位置(オーバーフロー判定より前)
    int w = widthAt(convertedLine, i, charBytes);
    if (wrapCol_ + w > width_) {
      // isForbiddenAtLineStart()自身のコメント参照(禁則処理)――上の
      // keepStart側の分岐と全く同じ考え方。
      bool kinsoku = wrapCol_ > 0 && encoding_ == Charset::Encoding::Utf8 &&
                     isForbiddenAtLineStart(convertedLine, i, charBytes);
      if (!kinsoku) { finalize(false); continue; }
      if (popLastCharForKinsoku()) { finalize(false); continue; }
      // keepStart側の同じ分岐自身のコメント参照(popLastCharForKinsoku()
      // が取り消す対象を持たなかった場合のフォールバック)。
    }
    // currentItalic_自身のコメント参照: 太字/下線自身の1文字ごとの
    // AttributeRunの粒度に合わせている(隣接runのまとめはこのクラス
    // 自身の責務ではない――decode_help.cpp自身のコメント参照)。
    int charAttrCount = 0;
    if (currentItalic_) {
      int start = static_cast<int>(wrapText_.size());
      wrapAttrs_.push_back({start, start + charBytes, Display::Attribute::Italic});
      charAttrCount = 1;
    }
    wrapText_.append(convertedLine, i, charBytes);
    wrapCol_ += w;
    i += charBytes;
    lastCharSeqStart = placementStart;
    lastCharRenderedBytes = charBytes;
    lastCharWidth = w;
    lastCharAttrCount = charAttrCount;
    if (wrapCol_ >= width_) {
      // keepStart側の同じ分岐自身のコメント参照(禁則処理の作り直し)。
      if (nextIsForbiddenAtLineStart(i) && popLastCharForKinsoku()) {
        finalize(false);
        continue;
      }
      // peekNewlineAfterMarkers()自身のコメント参照。
      int nlPos = peekNewlineAfterMarkers(i);
      if (nlPos >= 0) {
        i = nlPos + 1;
        finalize(true);
      } else {
        finalize(false);
      }
    }
  }
}

}  // namespace fm
