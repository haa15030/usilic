#include "pipe_buffer.hpp"

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
    lines_.push_back({wrapText_, wrapAttrs_, false});
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
// まだ無いため、今回は対象外のままにしてある)。「追い出し」方式
// (この文字を、新しい行の先頭にするのではなく、直前の行の末尾へ、
// たとえその行が予約列幅を1文字分超えることになっても押し込む)
// ――このクラス自身の「隙間より文字化けの方がまし」というトレード
// オフの選択(既存のScrollbar系コメント等、随所で繰り返されている
// 判断)と同じ精神。連続する2文字以上の禁則文字(例えば"」。")までは
// 追いかけない(1段階の押し出しのみ)――実用上ほぼ現れない組み合わせ
// のための複雑化を避ける単純化。
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

  auto finalize = [&](bool endsWithNewline) {
    lines_.push_back({wrapText_, wrapAttrs_, endsWithNewline});
    usedBytes_ += static_cast<long>(wrapText_.size());
    if (usedBytes_ > maxBytes_) overBudget_ = true;
    wrapText_.clear();
    wrapAttrs_.clear();
    wrapCol_ = 0;
    // currentIndent_自身のコメント参照(pipe_buffer.hpp): 段落の
    // 最初の行はソース側の実文字の字下げをそのまま使うので、ここは
    // 「折り返した継続行」の場合にだけ意味を持つ――currentIndent_が
    // 0(既定)ならこのループは即座に終わり、以前と全く同じ挙動の
    // ままである。U+3000(全角スペース、UTF-8で0xE3 0x80 0x80、
    // 表示幅2)を直接埋め込む――kHelpTextJa自身の他の全ての漢字と
    // 同じくこの機能はHELPでしか使わない(expandHelpMarkup()だけが
    // \x01を生成する)ので、そちらと同じくUTF-8決め打ちで問題ない。
    for (int k = 0; k < currentIndent_; k++) {
      wrapText_ += "\xE3\x80\x80";
      wrapCol_ += 2;
    }
  };

  while (i < n) {
    unsigned char c = static_cast<unsigned char>(convertedLine[i]);
    // 2026-09-04追加(currentIndent_自身のコメント参照): expandHelpMarkup()
    // が<indentN>/</indent>から生成する制御バイト\x01+数字1桁――
    // 幅を持たないゼロ幅の指示なので、wrapText_/wrapAttrs_には一切
    // 書き込まず、currentIndent_を更新するだけで読み飛ばす。
    if (c == '\x01' && i + 1 < n) {
      unsigned char digit = static_cast<unsigned char>(convertedLine[i + 1]);
      if (digit >= '0' && digit <= '9') {
        currentIndent_ = digit - '0';
        i += 2;
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

    if (c == '\t') {
      // ここで`step`個のリテラルな空白へ展開し、生のタブバイトの
      // ままには「しない」――disp.c自身の「'\t'を' 'へ変換する」
      // というコメント(そちら自身のcount_h()/count_i()による列数
      // カウント処理も、何かがdisplay層へ到達する前に同じ置換を
      // 行っている)からの移植。ContentView::renderLine()は0x20未満
      // のバイトを、反転表示が必要な制御コードとして扱う(K_at/K_dl
      // の慣習、自身のコメント参照)――生のタブバイトがwrapText_まで
      // 生き残ると、以前は同じ経路に当たってしまい、空白ではなく
      // 目に見える反転表示のグリフとして表示されていた。実際の
      // ユーザー報告(2026-08-02、スクリーンショット)により発見:
      // 素の`ls`の列出力(実際のシェルは空白ではなくタブで揃える)
      // が、まさにSHELL自身のスクロールバック(この移植の中で唯一、
      // 完全に生の、加工されていない実コマンドの出力をこの同じ
      // PipeBufferへ流している箇所――FileMode/ManModeは他ではいずれも
      // 後処理済みのファイルテキストしか見ない)で、ファイル名の間に
      // 小さな反転表示の箱として表示されていた。
      int step = 8 - (wrapCol_ % 8);
      if (wrapCol_ + step > width_) step = width_ - wrapCol_;
      step = std::max(0, step);
      wrapCol_ += step;
      wrapText_.append(static_cast<std::size_t>(step), ' ');
      i++;
      if (wrapCol_ >= width_) finalize(false);
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
          if (pos < n && static_cast<unsigned char>(convertedLine[pos]) < 0x80) {
            keepStart = pos;
            keepBytes = 1;
            attr = Display::Attribute::Bold;
          }
        }
      }
    }

    if (keepStart >= 0) {
      int w = widthAt(convertedLine, keepStart, keepBytes);
      if (wrapCol_ + w > width_) {
        // isForbiddenAtLineStart()自身のコメント参照(禁則処理、
        // 2026-09-04追加): この文字を新しい行の先頭にする代わりに、
        // 直前の行の末尾へ追い出す――finalize()せずそのまま下の通常
        // 配置へ進み、配置後の`wrapCol_ >= width_`チェックでこの文字を
        // 含めた「後」に改行させる。wrapCol_ > 0の条件は、そもそも
        // 行の先頭で発生した(=追い出す先の「前の行」が無い)場合の
        // 無意味な適用を避けるため。
        bool kinsoku = wrapCol_ > 0 && encoding_ == Charset::Encoding::Utf8 &&
                       isForbiddenAtLineStart(convertedLine, keepStart, keepBytes);
        if (!kinsoku) { finalize(false); continue; }
      }
      int start = static_cast<int>(wrapText_.size());
      wrapText_.append(convertedLine, keepStart, keepBytes);
      wrapAttrs_.push_back({start, start + keepBytes, attr});
      wrapCol_ += w;
      i = keepStart + keepBytes;
      if (wrapCol_ >= width_) finalize(false);
      continue;
    }

    int charBytes = byteLengthAt(convertedLine, i);
    if (i + charBytes > n) charBytes = n - i;  // チャンク末尾で切り詰められたシーケンス
    int w = widthAt(convertedLine, i, charBytes);
    if (wrapCol_ + w > width_) {
      // isForbiddenAtLineStart()自身のコメント参照(禁則処理)――上の
      // keepStart側の分岐と全く同じ考え方。
      bool kinsoku = wrapCol_ > 0 && encoding_ == Charset::Encoding::Utf8 &&
                     isForbiddenAtLineStart(convertedLine, i, charBytes);
      if (!kinsoku) { finalize(false); continue; }
    }
    // currentItalic_自身のコメント参照: 太字/下線自身の1文字ごとの
    // AttributeRunの粒度に合わせている(隣接runのまとめはこのクラス
    // 自身の責務ではない――decode_help.cpp自身のコメント参照)。
    if (currentItalic_) {
      int start = static_cast<int>(wrapText_.size());
      wrapAttrs_.push_back({start, start + charBytes, Display::Attribute::Italic});
    }
    wrapText_.append(convertedLine, i, charBytes);
    wrapCol_ += w;
    i += charBytes;
    if (wrapCol_ >= width_) finalize(false);
  }
}

}  // namespace fm
