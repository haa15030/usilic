#include "help_text.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace fm {

// help_text.hpp自身のコメント参照。<b>...</b>/<u>...</u>で囲まれた範囲を、
// PipeBuffer::wrapLine()が既に理解する重ね打ちバイト列(1文字ごとの
// "X\bX"=太字、"_\bX"=下線)へ展開する――逆(重ね打ちの解釈自体)は
// wrapLine()側に既にあるので、ここでは単純にその「生成」側を担う。
// タグの入れ子・重なりは想定しない(このヘルプ本文自身がそもそも
// 使っていない)ので、開始/終了タグを見つけたらその範囲だけを処理する
// 単純な逐次スキャンで十分。
std::string expandHelpMarkup(const std::string &text) {
  std::string out;
  out.reserve(text.size() * 2);
  std::size_t i = 0;
  // 2026-09-04追加、直接のユーザー報告("src/HELP.txtは冒頭でちゃんと
  // 分割文字列がマッチしているのに`--`エリアがあるのはおかしい"):
  // kHelpTextJa等の生文字列リテラルは、`R"HELPTXT(`の直後で改行してから
  // 本文(例えば"  1.  Introduction")を書き始める慣習(このファイルの
  // 全ての生文字列で一貫、可読性のため)のせいで、実際には常に先頭に
  // 1つだけ余分な空行を持っている――CutMode::split()自身の"--"区間
  // (kHelpPattern自身の最初のマッチより「前」に何らかの内容がある
  // 場合に生成される、cut_mode.cppのコメント参照)から見ると、これは
  // 「本物の、章1より前の内容」に見えてしまい、目次の最上段に
  // 見せかけの空の"--"エントリを作ってしまっていた――実際には章1が
  // 見た目上「冒頭で」始まっているように見えるにも関わらず。ここで
  // その先頭の1個の改行だけを読み飛ばすことで、この意図しない空行を
  // 本文の一部として扱わないようにする(2個以上の空行はそのまま――
  // 意図的な空行かもしれないので、あくまで1個だけを特別扱いする)。
  if (!text.empty() && text[0] == '\n') i = 1;
  const std::string underlineOpen = "<u>", underlineClose = "</u>";
  // 2026-09-04追加、直接のユーザー依頼("行頭にパディングするスペース
  // の個数を指定するだけのusilic特化マークアップ書式"、EPUBの
  // <div class="indent_1em">相当――折り返した継続行にも同じ幅の
  // 字下げを引き継ぐ): <indentN>...</indent>(Nはスペース個数、
  // 2026-09-12以降は半角スペース、pipe_buffer.cpp自身の
  // seedIndentIfAtLineStart()参照)。太字/下線(1文字ごとの重ね打ち
  // バイト列)とは別系統の制御バイト\x01+1バイトで表す――\x01<N>で
  // 開始(以後の折り返しでN個のスペースを引き継ぐ)、\x01\x00で終了
  // (引き継ぎをやめる)。対応するデコーダはPipeBuffer::wrapLine()
  // 自身のコメント参照。中身(段落本文)自体はここでは一切変換せず、
  // 開始/終了マーカーの間をこの関数自身の通常のループにそのまま
  // 委ねる(入れ子の<b>/<u>もそのまま機能する)。
  // 2026-09-12修正、直接のユーザー依頼("QUICK.txt検証用<indentN>の
  // 設定を<indentNN>（二桁対応）にして"): 半角化により同じ見た目の
  // 字下げ幅を得るにはNの値がおおむね倍必要になったため、1桁(0-9)
  // 限定だったNを1〜2桁(0-99)へ拡張した。当初は\x01の直後の1バイトを
  // 値そのもの(生バイト、0-99)にする案で実装したが、実機テスト
  // ("control bytes pass through as literal bytes"、test_pipe_buffer.cpp)
  // により、この案では本物のCtrl-A(0x01)がたまたま非数字の後続バイト
  // (例:'b'=0x62)を伴って現れた場合に、それを誤って自分のマーカーだと
  // 誤認してしまう(安全網が失われる)ことが判明したため撤回した。
  // 代わりに、後続バイトはあくまでASCII数字「文字」のまま(元のタグの
  // 数字を1〜2文字そのまま転写するだけ)にし、デコーダ側
  // (PipeBuffer::wrapLine()/skipMarkersFrom())が最大2文字の連続した
  // 数字を読んで10進数として組み立てる――「\x01の直後が数字文字で
  // なければ自分のマーカーではない」という既存の安全網はそのまま
  // 保たれる。斜体用の\x02(下記)は0/1の2値のみなので、元から1文字
  // 固定のまま変更していない。
  const std::string indentOpenPrefix = "<indent";
  const std::string indentClose = "</indent>";
  while (i < text.size()) {
    std::size_t numStart = i + indentOpenPrefix.size();
    std::size_t numEnd = numStart;
    while (numEnd < text.size() && numEnd < numStart + 2 && std::isdigit(static_cast<unsigned char>(text[numEnd])))
      numEnd++;
    bool isIndentOpen = text.compare(i, indentOpenPrefix.size(), indentOpenPrefix) == 0 && numEnd > numStart &&
                        numEnd < text.size() && text[numEnd] == '>';
    if (isIndentOpen) {
      out += '\x01';
      // 2026-09-21修正、直接のユーザー報告("<indent?>タグにバグが
      // ある。直後に数字があると正しく字下げしない。<indent2>1.
      // Introductionとか。特に<indent8>0x7fでは暴走"): 元の数字文字を
      // そのまま(1桁なら1文字だけ)転写していたため、デコーダ(pipe_
      // buffer.cpp自身のreadIndentDigits())の「\x01の直後、最大2文字
      // までの連続した数字を読む」という貪欲な読み取りが、タグの数字
      // が1桁だけの場合、直後に続く本文側の最初の文字がたまたま数字
      // だと、それをエンコードされた数字の2桁目だと誤読んでしまって
      // いた(例: <indent2>1. → \x01+"2"+"1. "を\x01+"21"と誤読、
      // 本文の"1"自体も字下げ値へ吸い込まれて消える)。<indent8>のような
      // 大きな値と本文側の数字が組み合わさると、字下げ幅が実際の画面幅
      // を超える巨大な値になり得て、折り返し処理の無限ループ(暴走)を
      // 引き起こしていた。固定2桁(0埋め)へ変更し、デコーダ側が常に
      // ちょうど2文字だけを読む(readIndentDigits()自身のk<2という
      // ループ上限により、これで曖昧さが無くなる)ようにする。
      std::string digits = text.substr(numStart, numEnd - numStart);
      if (digits.size() == 1) digits = "0" + digits;
      out += digits;
      i = numEnd + 1;  // "<indent" + 1〜2桁の数字 + ">"
      // 2026-09-04追加、実機での実際の再現(初回のsrc/HELP.txt全面
      // 改稿を実際に流し込んで発覚): 複数行の<indentN>...</indent>を
      // 読みやすく書く実際の著者の書き方("<indentN>\n本文...\n
      // </indent>")では、開始タグ自身の直後の改行、終了タグ自身の
      // 直前の改行は、いずれも純粋にソース側の可読性のための改行で
      // あり、意図した本文の改行ではない――このまま素通りさせると、
      // wrapLine()側でその改行「自体」が独立した(意図しない)空行を
      // 生んでしまう(pipe_buffer.cpp自身のseedIndentIfAtLineStart()
      // のコメント参照、この問題自体は既に一度そちらで別角度から
      // 修正済みだが、こちらの「タグ境界に隣接する改行」自体は
      // 残っていた)。開始タグの直後の改行はここで読み飛ばす――
      // 終了タグ側の対称な処理は下のisIndentClose分岐参照。
      if (i < text.size() && text[i] == '\n') i++;
      continue;
    }
    if (text.compare(i, indentClose.size(), indentClose) == 0) {
      // 上のisIndentOpen分岐自身のコメント参照(対称な処理): 終了タグ
      // 自身の直前の改行(=このタグが独立した行にある場合、その本文
      // 側の最後の実際の改行)も同じ理由で無視する――既に`out`へ
      // 出力済みなので、末尾がちょうど改行ならそれを取り除く。
      if (!out.empty() && out.back() == '\n') out.pop_back();
      out += '\x01';
      out += "00";  // 終了(値0)、isIndentOpen自身の2026-09-21修正と同じ固定2桁ゼロ埋め(単独の'0'だと直後の本文側の数字を2桁目として誤読み得るため)
      i += indentClose.size();
      continue;
    }
    // 2026-09-04追加、直接のユーザー依頼("今後は<i>プログラム名</i>
    // で記載したいので対応よろしく"): <i>...</i>(斜体)。太字/下線が
    // 使っている1文字ごとの重ね打ちバイト列("X\bX"等)はlegacyの
    // nroff規約からの移植であり、斜体にはそもそも対応する重ね打ち
    // 規約が存在しない――このC++移植自身の新規追加なので、<indentN>
    // と同じ「トグル式の制御バイト」方式(\x02+数字1桁、\x011桁は
    // 既に字下げに使用済みなので別系統)を採用する: \x021で開始、
    // \x020で終了。PipeBuffer::wrapLine()側は、この区間内の「素の」
    // (太字/下線の重ね打ちに該当しない)各文字ごとに、Display::
    // Attribute::Italicの1文字幅AttributeRunを生成する――太字/下線
    // 自身の1文字ごとのAttributeRunの粒度に合わせている(decode_help.cpp
    // 自身のコメント参照、隣接するrunは別ツールが後でまとめる)。
    // 中身自体はここでは一切変換せず、開始/終了マーカーの間を通常の
    // ループにそのまま委ねる(入れ子の<b>/<u>もそのまま機能するが、
    // 同じ文字が斜体と太字/下線を同時に持つケースはwrapLine()側の
    // 制約により非対応――そちら自身のコメント参照)。
    // 2026-09-25追加: "<lt>"は文字どおりの'<'――markdownToHelpMarkup()が
    // 元テキストの'<'をすべてこれへ置き換える(そちら自身のコメント参照)。
    // タグとして解釈されては困る"<b>"等を文字どおり表示するための
    // エスケープ。
    const std::string literalLt = "<lt>";
    if (text.compare(i, literalLt.size(), literalLt) == 0) {
      out += '<';
      i += literalLt.size();
      continue;
    }
    const std::string italicOpen = "<i>", italicClose = "</i>";
    if (text.compare(i, italicOpen.size(), italicOpen) == 0) {
      out += '\x02';
      out += '1';
      i += italicOpen.size();
      continue;
    }
    if (text.compare(i, italicClose.size(), italicClose) == 0) {
      out += '\x02';
      out += '0';
      i += italicClose.size();
      continue;
    }
    // 2026-09-22修正、直接のユーザー依頼("CommonMarkの強調解決アルゴリズム
    // 対応が普通なら実装してほしい"): <b>/<u>を、上の<i>と全く同じ
    // 「トグル式の制御バイト」方式(\x03=太字、\x04=下線)へ書き換えた。
    // 従来は開始〜終了タグの間を生の文字列として切り出し、1文字ずつ
    // "X\bX"/"_\bX"の重ね打ちバイト列へ変換していた(legacyのnroff規約
    // からの移植)――しかしこの方式は中に他のタグがあっても再帰的には
    // 解釈しないため、markdownToHelpMarkup()が生成し得る
    // `**太字 *斜体* まだ太字**`のような入れ子ができなかった(旧コメント
    // 「同じ文字が斜体と太字/下線を同時に持つケースは非対応」参照)。
    // トグル方式にすることで、<i>と全く同じ理由で入れ子が自然に機能する
    // ようになる(PipeBuffer::wrapLine()側のcurrentBold_/currentUnderline_
    // 参照)。本物のnroff重ね打ち("X\bX"/"_\bX")の検出――man/groff出力
    // 等、expandHelpMarkup()を一切経由しない実ファイルの内容向け――は
    // 別の、無関係なコードパスのままなので、このタグの書式変更の影響を
    // 受けない。
    const std::string boldOpenPlain = "<b>", boldClosePlain = "</b>";
    if (text.compare(i, boldOpenPlain.size(), boldOpenPlain) == 0) {
      out += '\x03';
      out += '1';
      i += boldOpenPlain.size();
      continue;
    }
    if (text.compare(i, boldClosePlain.size(), boldClosePlain) == 0) {
      out += '\x03';
      out += '0';
      i += boldClosePlain.size();
      continue;
    }
    // 2026-09-29追加: <serif>...</serif>、明朝体(Display::Attribute::Serif参照)。
    // \x05+数字1桁、<i>と同じトグル式。
    // 2026-09-30追加: <rb>...</rb>(ルビの親字、\x06)と<rt>...</rt>(ルビの読み、\x10)、
    // Display::Attribute::RubyBase/RubyText参照。<serif>と同じトグル式。
    // 2026-10-01追加: <big>...</big>(\x11)と<small>...</small>(\x12)、Display::Attribute::
    // Large/Small参照(EPUBのCSSのfont-size)。
    bool rubyTagMatched = false;
    for (const auto &[tag, byte] : {std::pair<const char *, char>{"rb", '\x06'}, {"rt", '\x10'},
                                    {"big", '\x11'}, {"small", '\x12'}}) {
      for (bool closing : {false, true}) {
        std::string markup = std::string(closing ? "</" : "<") + tag + ">";
        if (text.compare(i, markup.size(), markup) != 0) continue;
        out += byte;
        out += closing ? '0' : '1';
        i += markup.size();
        rubyTagMatched = true;
        break;
      }
      if (rubyTagMatched) break;
    }
    if (rubyTagMatched) continue;
    const std::string serifOpen = "<serif>", serifClose = "</serif>";
    if (text.compare(i, serifOpen.size(), serifOpen) == 0) {
      out += '\x05';
      out += '1';
      i += serifOpen.size();
      continue;
    }
    if (text.compare(i, serifClose.size(), serifClose) == 0) {
      out += '\x05';
      out += '0';
      i += serifClose.size();
      continue;
    }
    if (text.compare(i, underlineOpen.size(), underlineOpen) == 0) {
      out += '\x04';
      out += '1';
      i += underlineOpen.size();
      continue;
    }
    if (text.compare(i, underlineClose.size(), underlineClose) == 0) {
      out += '\x04';
      out += '0';
      i += underlineClose.size();
      continue;
    }
    out += text[i];
    i++;
  }
  return out;
}

}  // namespace fm
