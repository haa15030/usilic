#include "graphical_display.hpp"

#include "epub_vertical.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "scrollbar_idle_image.hpp"
#include "mac_cursor.hpp"
#include "utf8.hpp"
#include "content_view.hpp"  // middleEllipsisTruncateByWidth() -- renderScrolled()自身のコメント参照

namespace fm {

namespace {

struct Cell {
  std::string glyph;       // このセルのグリフのUTF-8バイト、空=空白
  bool bold = false;
  bool underline = false;
  bool standout = false;
  bool italic = false;  // 2026-09-04追加、直接のユーザー依頼("<i>プログラム名</i>")
  bool serif = false;   // 明朝体で描く(Display::Attribute::Serif参照)
  // 2026-09-10追加、直接のユーザー依頼("マッチ文字列をboldの代わりに
  // ハイライトでやってみて。色は変えられない？薄めの色とか。"):
  // GREPマッチ表示専用の色付き前景(他の属性と同じく独立、standout等
  // と自由に組み合わせ可能)。renderCell()自身のkHighlightColor参照。
  // 2026-09-19追記(kHighlightCurrentBgColor自身のコメント参照):
  // highlightは「他の全マッチ」、highlightCurrentは「現在位置の
  // 一致」――両者は互いに排他(同じスパンが両方trueになることは
  // 無い、TerminalDisplay::highlightVariant_と同じ理屈)。
  bool highlight = false;
  bool highlightCurrent = false;
  // 2026-09-26追加(Display::Attribute::Color自身のコメント参照): ANSIの
  // 色エスケープ由来の前景色/背景色(Display::kDefaultColorなら既定色)。
  int fgColor = Display::kDefaultColor;
  int bgColor = Display::kDefaultColor;
  bool continuation = false;  // 2列幅のグリフの「影」となる2つ目のセル――それ自体では描画されない
};

// TerminalDisplay::attributeEscape()のHighlight/HighlightCurrent
// (2026-09-10三度目の改訂、2026-09-19追記でisearch/lazy-highlightの
// 2種類に分離)に見た目を揃えるための-g側の色ペア。自前の「無難な
// 絶対色」の発明(基本16色版→拡張256色版、いずれも実機スクリーン
// ショットで判別しにくい/同化する実例が出た=fz/=ga)をやめ、GNU
// Emacs自身のisearch/lazy-highlight faceをそのまま拝借する方針へ
// 転換――当て推量ではなく、実際にEmacs 30.1をPTY経由で起動しC-s
// 検索時に送出される生のエスケープシーケンスをキャプチャして確認
// 済みの値。現在位置の一致(isearch face、旧kHighlightBgColor/
// kHighlightFgColorから改名)は前景=lightskyblue1 RGB(176,226,255)、
// 背景=magenta3 RGB(205,0,205)。他の全ての一致(lazy-highlight
// face)は背景=paleturquoise RGB(175,238,238)のみ(前景は変更しない
// ――TerminalDisplay::attributeEscape()自身のHighlightケースの
// コメント参照)。
constexpr SDL_Color kHighlightCurrentBgColor{205, 0, 205, 255};
constexpr SDL_Color kHighlightCurrentFgColor{176, 226, 255, 255};
constexpr SDL_Color kHighlightBgColor{175, 238, 238, 255};

// Display::Attribute::Color自身のコメント参照(2026-09-26追加): 色番号
// (256色パレット/24bit直接指定)をRGBへ。基本16色は、本物の端末と違い
// ウィンドウ側に利用者の配色設定が無いので、xtermの既定値を使う。
SDL_Color ansiColorToSdl(int color) {
  if (color & Display::kTrueColorFlag) {
    int rgb = color & 0xFFFFFF;
    return SDL_Color{static_cast<Uint8>((rgb >> 16) & 0xFF), static_cast<Uint8>((rgb >> 8) & 0xFF),
                     static_cast<Uint8>(rgb & 0xFF), 255};
  }
  static const SDL_Color kBasic[16] = {
      {0, 0, 0, 255},       {205, 0, 0, 255},     {0, 205, 0, 255},     {205, 205, 0, 255},
      {0, 0, 238, 255},     {205, 0, 205, 255},   {0, 205, 205, 255},   {229, 229, 229, 255},
      {127, 127, 127, 255}, {255, 0, 0, 255},     {0, 255, 0, 255},     {255, 255, 0, 255},
      {92, 92, 255, 255},   {255, 0, 255, 255},   {0, 255, 255, 255},   {255, 255, 255, 255}};
  if (color < 16) return kBasic[std::max(0, color)];
  if (color < 232) {
    static const Uint8 kLevels[6] = {0, 95, 135, 175, 215, 255};
    int n = color - 16;
    return SDL_Color{kLevels[n / 36], kLevels[(n / 6) % 6], kLevels[n % 6], 255};
  }
  Uint8 gray = static_cast<Uint8>(8 + 10 * (color - 232));
  return SDL_Color{gray, gray, gray, 255};
}

// `utf8Bytes`の先頭のUTF-8シーケンス(他所でfm::Utf8::
// sequence_length()によって既に正しい長さに分割済み)を、単一の
// Unicodeスカラーへデコードする。空文字列に対しては0を返す。
// fm::Utf8への追加ではなく、意図的に小さなローカルデコーダにして
// ある(それはバイト長/画面幅だけを必要とし、実際のコードポイント
// を必要としたことは一度も無かった)――これは、この移植全体の中で、
// フォントに「この特定の文字のグリフを持っているか」を尋ねる必要
// がある唯一の場所である。下のglyphProvidedByFont()参照。
unsigned int decodeUtf8Codepoint(const std::string &utf8Bytes) {
  if (utf8Bytes.empty()) return 0;
  auto b = [&](std::size_t i) -> unsigned int { return static_cast<unsigned char>(utf8Bytes[i]); };
  unsigned int lead = b(0);
  if (lead < 0x80) return lead;
  if ((lead & 0xE0) == 0xC0 && utf8Bytes.size() >= 2) return ((lead & 0x1F) << 6) | (b(1) & 0x3F);
  if ((lead & 0xF0) == 0xE0 && utf8Bytes.size() >= 3)
    return ((lead & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F);
  if ((lead & 0xF8) == 0xF0 && utf8Bytes.size() >= 4)
    return ((lead & 0x07) << 18) | ((b(1) & 0x3F) << 12) | ((b(2) & 0x3F) << 6) | (b(3) & 0x3F);
  return lead;  // 不正な形/切り詰められている――生の先導バイトへフォールバックする、TTF自身の「とにかく何か描画する」に合わせている
}

bool glyphProvidedByFont(TTF_Font *font, unsigned int codepoint) {
  if (!font || codepoint == 0 || codepoint > 0xFFFF) return false;  // TTF_GlyphIsProvidedはUCS-2のみ対応
  return TTF_GlyphIsProvided(font, static_cast<Uint16>(codepoint)) != 0;
}

// decodeUtf8Codepoint()の逆――下のcomposeDakutenCodepoint()が必要
// とする(composeGraphemeCluster()自身の呼び出し元が、合成された
// スカラーをセルの`glyph`文字列へ再エンコードし直す)。この移植が
// この方法で合成するコードポイントは全て0xFFFF以下(BMPのみの
// ひらがな/カタカナ)なので、4バイトのケースは実際には到達しないが、
// デコーダとの完全性/対称性のために含まれている。
std::string encodeUtf8Codepoint(unsigned int codepoint) {
  std::string out;
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return out;
}

// 2026-08-20の追加、U+3099/U+309Aのdisplay_width()の修正の後の
// 直接のユーザーによる追加依頼("=dsよりはマシだが=dvには負ける
// ...自前描画なら何とかなる？"): 結合する濁点/半濁点マークを、
// 生の、まだ分解されたままのUTF-8バイトとして「前の」セルの
// `glyph`文字列に統合すること(commitGlyph()の既存のwidth==0分岐)
// は、そのセルに「2コードポイント」の文字列("ト"+U+3099)を保持
// させ、それをTTF_RenderUTF8_Blended()が、1つの融合した「ド」の
// ように見せるために正しくシェイピングしなければならない――しかし
// SDL_ttf/FreeTypeは単純な線形のグリフレイアウトを行うだけで、
// (-c自身の本物の端末エミュレータであるCoreTextが既に行っている
// ようなGPOSマーク付着の位置決めが無い)本物のUnicodeテキスト
// シェイピングは行わないので、マーク自身の送り幅/位置が、基底
// グリフのちょうど上に着地することは保証されない。-gは自身の
// 描画を完全に所有している(生のバイトを本物のシェイピングエンジン
// へ渡すだけの-cとは異なる)ので、ここで利用可能な修正は、シェイ
// ピングを完全に回避することである: セルに格納する「前に」、その
// ペアを単一のNFCコードポイントへ事前合成する(例えば"ト"+U+3099
// -> "ド" U+30C9)。そうすればセルは、通常のグリフメトリクスを
// 持つ、完全に普通の1つの仮名文字を最終的に保持することになる
// ――結合マークのシェイピングが全く関与せず、他のどんな単一の
// 文字ともまさに同じように描画される。1つのケースだけのために
// 完全な正規化ライブラリを持ち込むのではなく、閉じた、変化しない
// 表(Unicodeが正規のNFC合成を定義している、全てのひらがな/
// カタカナの濁点/半濁点のペア――これは汎用のUnicode正規化では
// なく、単にこの1つの有限の、よく知られた対応関係である)。この
// 表の外側のどんな基底/マークの組み合わせに対しても、古い生の
// 追記の挙動へフォールバックする(0を返し、呼び出し元はその以前の
// 挙動を保つ)――display_width()によれば依然として幅0だが、この
// 移植が事前合成する方法を知らない何かであり、そのため分解された
// 形(不完全なシェイピングだが、Utf8::display_width()自身の幅0の
// 分類によれば見た目に別のグリフでもない)が、利用可能な最善の
// フォールバックである。
unsigned int composeDakutenCodepoint(unsigned int base, unsigned int mark) {
  if (mark != 0x3099 && mark != 0x309a) return 0;
  bool handakuten = mark == 0x309a;
  // う/ウ -> ゔ/ヴ(濁点のみ、半濁点の形は存在しない)
  if (!handakuten && base == 0x3046) return 0x3094;
  if (!handakuten && base == 0x30a6) return 0x30f4;
  // か行からと行まで(濁点のみ、半濁点の形は存在しない)、ひらがな
  // の後カタカナ(カタカナのコードポイントは、この範囲全体を通じて
  // 対応するひらがなから固定+0x60のオフセットであり、下でオフセット
  // 後にチェックするだけで十分一貫している。全カタカナコードポイント
  // を二重に列挙する必要はない)。
  if (!handakuten) {
    static const unsigned int kDakutenOnlyBases[] = {0x304b, 0x304d, 0x304f, 0x3051, 0x3053, 0x3055, 0x3057,
                                                       0x3059, 0x305b, 0x305d, 0x305f, 0x3061, 0x3064, 0x3066,
                                                       0x3068};
    for (unsigned int hiraganaBase : kDakutenOnlyBases) {
      if (base == hiraganaBase || base == hiraganaBase + 0x60) return base + 1;
    }
  }
  // は行(濁点と半濁点の両方の形が存在する)、ひらがなの後カタカナ
  // (同じ+0x60オフセット)。
  static const unsigned int kHaGyouBases[] = {0x306f, 0x3072, 0x3075, 0x3078, 0x307b};
  for (unsigned int hiraganaBase : kHaGyouBases) {
    if (base == hiraganaBase || base == hiraganaBase + 0x60) return base + (handakuten ? 2 : 1);
  }
  // 稀な/廃れたカタカナ(濁点のみ): ワヰヱヲ -> ヷヸヹヺ、そしてカタ
  // カナ/ひらがなの繰り返し記号ヽ/ゝ -> ヾ/ゞ。それらの基底から
  // 固定オフセットでは「ない」(他のケースが共有する連続したブロッ
  // クの外側)ので、明示的に列挙されている。
  if (!handakuten) {
    switch (base) {
      case 0x30ef: return 0x30f7;  // ワ -> ヷ
      case 0x30f0: return 0x30f8;  // ヰ -> ヸ
      case 0x30f1: return 0x30f9;  // ヱ -> ヹ
      case 0x30f2: return 0x30fa;  // ヲ -> ヺ
      case 0x30fd: return 0x30fe;  // ヽ -> ヾ
      case 0x309d: return 0x309e;  // ゝ -> ゞ
      default: break;
    }
  }
  return 0;
}

// 2026-08-30、直接のユーザー報告(GUI、実際のマウスドラッグ中に濁点
// が単体グリフとして分離/消失する件、ARCモードで再現、スクリーン
// ショット=ev/=ew): 上のcomposeDakutenCodepoint()は、Display::write()/
// putByte()経由の通常のセル単位描画(commitGlyph()自身のコメント
// 参照)だけが通る――GraphicalDisplay::renderScrolled()(通常の
// ContentView::redraw()を一切経由しない別の描画経路)自身の
// renderTextRun()は、生のNFD分解された文字列をコードポイント単位で
// そのまま描くだけで、この合成を一切行っていなかった。この関数は、
// commitGlyph()と同じ「基底文字の直後に続く濁点/半濁点マークを
// その場で吸収する」ロジックを、セル単位ではなく文字列全体に対して
// 一度だけ適用する――renderScrolled()がrenderTextRun()へ渡す前に
// 呼ぶ。table外の組み合わせ(composeDakutenCodepoint()が0を返す)は
// 元のまま(基底とマークを別々に)残す――commitGlyph()自身の
// フォールバックと同じ。
std::string composeDakutenInString(const std::string &utf8Text) {
  std::string out;
  std::size_t i = 0;
  while (i < utf8Text.size()) {
    int len = Utf8::sequence_length(static_cast<unsigned char>(utf8Text[i]));
    len = std::min<int>(len, static_cast<int>(utf8Text.size() - i));
    std::string cluster = utf8Text.substr(i, static_cast<std::size_t>(len));
    i += static_cast<std::size_t>(len);
    while (i < utf8Text.size()) {
      int markLen = Utf8::sequence_length(static_cast<unsigned char>(utf8Text[i]));
      markLen = std::min<int>(markLen, static_cast<int>(utf8Text.size() - i));
      unsigned int composed =
          composeDakutenCodepoint(decodeUtf8Codepoint(cluster), decodeUtf8Codepoint(utf8Text.substr(i, static_cast<std::size_t>(markLen))));
      if (!composed) break;
      cluster = encodeUtf8Codepoint(composed);
      i += static_cast<std::size_t>(markLen);
    }
    out += cluster;
  }
  return out;
}

// ウィンドウのポイント寸法とレンダラーの実際の描画領域(物理ピクセル)
// の比でレンダラーをスケーリングする――create()自身のコメント参照。
// 2026-09-25、直接のユーザー報告("macbookで...GUI起動では-cでの画面を
// 4等分したうちのひとつ分のサイズで表示された")を受けて括り出した:
// 以前はcreate()の中で1回だけ、まだSDL_WINDOW_HIDDENのウィンドウに
// 対して計算していた。非表示のウィンドウの描画領域が(環境によって)
// 1倍で報告されると倍率1のまま固定され、表示後に描画領域がRetinaの
// 2倍になっても1倍の座標で描き続けるため、左上4分の1(縦横半分)に
// しか描かれない。表示直後(open())・サイズ変更・ディスプレイ間の
// 移動のたびにも呼び直す。
void syncHighDpiRenderScale(SDL_Window *window, SDL_Renderer *renderer) {
  int windowWidthPoints = 0, windowHeightPoints = 0;
  int drawableWidthPixels = 0, drawableHeightPixels = 0;
  SDL_GetWindowSize(window, &windowWidthPoints, &windowHeightPoints);
  SDL_GetRendererOutputSize(renderer, &drawableWidthPixels, &drawableHeightPixels);
  if (windowWidthPoints > 0 && windowHeightPoints > 0 && drawableWidthPixels > 0 && drawableHeightPixels > 0)
    SDL_RenderSetScale(renderer, static_cast<float>(drawableWidthPixels) / windowWidthPoints,
                       static_cast<float>(drawableHeightPixels) / windowHeightPoints);
}

// グリフのサーフェスを、割り当てられたboxX/boxY..maxW/maxHのセル
// (複数)の中のどこに描くかを計算する。それがそうでなければ溢れて
// しまう場合、「一様に」(決して拡大せず、決してXとYで独立には
// ならないよう)縮小し、その後中央に置く。maxW/maxHをちょうど
// 満たすようサーフェス自身の自然なサイズを引き伸ばしていた以前の
// バージョンを置き換える――不均一な引き伸ばしはグリフのアスペクト
// 比を歪める。そしてCJKフォールバックフォントの自然なグリフの比率
// は、必ずしも(主フォントのラテン文字「M」から測定される)
// cellWidth*2 x cellHeightに一致するとは限らないので、その引き
// 伸ばしは、認識可能な漢字/仮名を、違う見た目の、依然として間違った
// 形へ歪めていた(実際の報告から確認済み: 日本語のファイル名が
// 文字化けする「具体的な」方法が、その引き伸ばしを追加した後に
// 変わったのであって、それによって修正されたのではなかった)。
// スケーリングは常に縮小だけを行い(自然に小さいグリフを拡大する
// ことは決して無く、それはそれをぼかしてしまう)、常にアスペクト
// 比を保つので、グリフは(既に収まる、よくあるケースでは)自然な
// サイズで鮮明に描画されるか、レターボックス付きで一様に縮小され
// る――決して歪まない。
//
// 2026-09-04追加、`allowWidthOverflow`: 直接のユーザー報告
// ("usilic(1)の（と）が明らかに違う"、スクリーンショット=fl)を
// 調査して発見した実際のバグの修正。TTF_STYLE_ITALIC(合成斜体)は
// FreeTypeにグリフの輪郭をせん断させるだけで、フォント自身の送り幅
// メトリクスは一切調整しない――本物の(専用にデザインされた)
// イタリック書体が、せん断で見た目上広がる分をあらかじめグリフ
// デザイン自体で見込んで詰めてあるのとは違う。結果として、せん断
// されたグリフのサーフェス幅は、素のグリフよりわずかに(せん断角度
// ×高さぶん)広くなり、単一セル幅の半角記号ではこれがちょうど
// maxWをわずかに超えることがある――その時、上の「常にアスペクト比
// を保つ」ロジックがmaxW側の比率で縮小するため、"("と")"のように
// せん断後の幅が微妙に異なる(グリフの自然な形が違うので当然)2文字
// が、"横方向の縮小のとばっちりで"それぞれ別の量だけ縦方向にも
// 縮小されてしまう――実測(=flのスクリーンショットを実際にピクセル
// 単位で計測): "("は高さ13px、")"は高さ18pxで表示されており、
// 同じフォント/同じポイントサイズの兄弟記号として明らかに不揃い
// だった。実際のGraphicalDisplayパイプラインを直接駆動する使い捨ての
// デバッグ計装で確認済み: surfaceW=16("(")/15(")")に対しmaxW=14(cellWidth)
// で、"("の方がわずかに幅が広いせいで強く縮小され(scale=0.875)、
// ")"は弱く縮小される(scale=0.933)――高さ方向には全く余裕
// (surfaceH=maxH=28)があったにも関わらず。斜体セルについては、
// 幅方向のはみ出しをそのまま許容し(几帳面な等幅グリッドの中で
// 斜体だけがわずかに隣のセルへ食い込むのは、本物の端末エミュレータの
// 斜体フォントでも普通に起こる見た目であり、無理に幅で縮めて高さを
// 犠牲にするより遥かに自然)、高さだけで縮小率を決める。
//
// 2026-09-14追加、直接のユーザー報告("-g: bold装飾文字はそうでない
// 文字に比べてサイズが小さい"、スクリーンショット=gq): 全く同じ
// 根本原因がTTF_STYLE_BOLD(合成太字)にも存在すると判明した――
// FreeTypeのemboldenはストロークを太らせるだけで送り幅メトリクスは
// 調整しないため、太らせた分だけグリフのサーフェス幅が素のグリフより
// 広くなり、単一セル幅の半角文字ではmaxWをわずかに超えて上と全く
// 同じ「横方向の縮小に高さも巻き込まれる」現象が起きる――太字の
// 文字だけが、同じフォント/ポイントサイズの非装飾文字より小さく
// (かつ字形ごとにバラバラの量だけ)描画されていた。斜体と全く同じ
// 理屈(几帳面な等幅グリッドの中で太字だけがわずかに隣のセルへ食み
// 出るのは、本物の端末エミュレータの太字フォントでも普通に起こる
// 見た目)で、太字セルについても幅方向のはみ出しを許容する。
SDL_Rect fitGlyphRect(int boxX, int boxY, int maxW, int maxH, int surfaceW, int surfaceH,
                       bool allowWidthOverflow = false) {
  float scale = allowWidthOverflow
                    ? std::min(1.0f, static_cast<float>(maxH) / std::max(surfaceH, 1))
                    : std::min(1.0f, std::min(static_cast<float>(maxW) / std::max(surfaceW, 1),
                                               static_cast<float>(maxH) / std::max(surfaceH, 1)));
  int drawW = static_cast<int>(surfaceW * scale);
  int drawH = static_cast<int>(surfaceH * scale);
  return SDL_Rect{boxX + (maxW - drawW) / 2, boxY + (maxH - drawH) / 2, drawW, drawH};
}

// 2026-08-11: 新しく描画されたグリフのサーフェスの、部分的に覆わ
// れた(アンチエイリアスされた端の)ピクセルを暗くし、フォントの
// ヒンティングやスタイルに触れることなく、細いストロークをより
// 視覚的に重く読ませる――項目264のTTF_HINTING_NONEの実験は、
// 「SF Mono Regular」に対しては特に的外れであることが判明した:
// フォントファイルを直接調べる(fontTools)と、fpgm/prepテーブルが
// 全く「無く」、全グリフを通じて命令が0バイトであることが分かった
// ので、そもそもTTF_HINTING_NORMAL/LIGHT/NONEの間で選択すべき
// ネイティブのヒントプログラムが一度も存在しなかった――ヒンティン
// グモードを切り替えてもこのフォントには全く見た目の変化が無い
// という実際の報告と一致する。
//
// TTF_RenderUTF8_Blended()は、RGBがどこでも平坦な前景色である
// サーフェスを返す。アルファチャネルだけが変化し、各ピクセルを
// グリフの輪郭がどれだけ覆っているかを符号化する(0=背景、255=
// ストロークの内側に完全にある、部分的な値=アンチエイリアスされた
// 端)。ガンマカーブを通してアルファを上げる(alpha' = 255 *
// (alpha/255)^gamma、gamma < 1)ことは、部分的に覆われた端の
// ピクセルを完全に不透明な方向へ押し上げる。これは、プロの
// ラスタライザが「ステムダークニング」と呼ぶのと同じ発想である
// ――ヒントされていない小さなテキストが薄すぎて読めることを補償
// するための意図的な増強で、(ネイティブでもFreeTypeのオートヒン
// タでも)どんなヒントプログラムに頼るのでもなく、ピクセルレベルの
// 後処理として適用される。そのため、フォントがそれを持っているか
// どうかに関わらず、同一に機能する。
void boostGlyphAlpha(SDL_Surface *surface, double gamma) {
  if (!surface) return;
  if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) return;
  Uint32 *pixels = static_cast<Uint32 *>(surface->pixels);
  int count = surface->w * surface->h;
  for (int i = 0; i < count; ++i) {
    Uint8 r, g, b, a;
    SDL_GetRGBA(pixels[i], surface->format, &r, &g, &b, &a);
    if (a == 0 || a == 255) continue;
    double boosted = 255.0 * std::pow(a / 255.0, gamma);
    Uint8 newAlpha = static_cast<Uint8>(std::min(255.0, std::max(0.0, boosted)));
    pixels[i] = SDL_MapRGBA(surface->format, r, g, b, newAlpha);
  }
  if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
}

// gamma < 1は部分的な被覆のアルファを上向きに(暗く)増強する。
// 特定の目標に対して測定されたのではなく、開始点として目分量で
// 選ばれた――boostGlyphAlpha()自身のコメント参照。
constexpr double kNarrowGlyphAlphaGamma = 0.65;

// 「主」フォント(main.cppによって解決される――デフォルトでは
// Menlo/Monaco/DejaVu/Liberationであり、どれも日本語の仮名/漢字を
// カバーしない)にグリフが欠けている時にフォールバックすべき、
// CJK対応フォントのベストエフォートの探索。実機での、日本語の
// ファイル名が「豆腐」プレースホルダーの箱の連続として描画される
// という報告の後に追加された(その報告のスクリーンショット自体
// から確認済み――一般的な文字化け/誤ったバイトのデコードではなく、
// main.cppがたまたま読み込んだフォントに単にグリフのカバレッジが
// 欠けていた)。
//
// 各候補のパスを順に試し、(a)そもそも読み込めて「かつ」(b)実際に
// U+3042('あ'、ひらがな――ひらがなのカバレッジは、そもそも日本語
// サポートをわざわざ同梱するどんなフォントでも確実に漢字のカバレ
// ッジも意味するが、逆は真ではない: 一部のCJKフォントは、仮名を
// カバーせずに中国語と共有される漢の表意文字をカバーする。それ
// だけでは報告されたファイル名は直らない、という理由で選ばれた)
// に対するグリフを持つ最初のものを保つ。Appleのヒラギノファミリー
// の正確なファイル名/パスはmacOSのバージョンをまたいで変化して
// おり、このプロジェクトにはそれらを実機に対して検証する方法が
// 無い(この移植のGUI作業全体を通じて記録されている同じサンド
// ボックスの制限)――まさにその理由でいくつかのもっともらしい
// 候補が列挙されている。ここでのマッチの失敗は、単にCJKフォール
// バックが無い(impl_->fallbackFontはnullptrのまま)ことを意味する
// だけで、起動の失敗ではない。通常のASCIIテキストはどちらにせよ
// 全く影響を受けない。
// 2026-09-29追加(Display::Attribute::Serif参照): 明朝体のフォント。候補を順に試し、
// 仮名を持つ最初のものを使う(loadCjkFallbackFont()と同じ考え方)。
// GraphicalDisplay::setFontFamilyCandidates()参照(usilicrcのSERIF/SANS-SERIF)。
std::vector<std::string> &userSerifCandidates() {
  static std::vector<std::string> candidates;
  return candidates;
}
std::vector<std::string> &userSansSerifCandidates() {
  static std::vector<std::string> candidates;
  return candidates;
}

// `user`(usilicrcの指定)を先に、`builtin`を後に試し、仮名を持つ最初のフォント。
TTF_Font *openFirstJapaneseFont(const std::vector<std::string> &user, const char *const *builtin, std::size_t count,
                               int pointSize) {
  std::vector<std::string> paths = user;
  for (std::size_t k = 0; k < count; k++) paths.push_back(builtin[k]);
  for (const auto &path : paths) {
    TTF_Font *candidate = TTF_OpenFont(path.c_str(), std::max(pointSize, 1));
    if (!candidate) continue;
    if (glyphProvidedByFont(candidate, 0x3042)) return candidate;
    TTF_CloseFont(candidate);
  }
  return nullptr;
}

TTF_Font *loadSerifFont(int pointSize) {
  static const char *const candidates[] = {
      "/System/Library/Fonts/ヒラギノ明朝 ProN.ttc",
      "/System/Library/Fonts/Supplemental/Hiragino Mincho ProN.ttc",
      "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
      "/usr/share/fonts/truetype/noto/NotoSerifCJK-Regular.ttc",
      "/usr/share/fonts/opentype/ipaexfont-mincho/ipaexm.ttf",
      "/usr/share/fonts/truetype/fonts-japanese-mincho.ttf",
  };
  return openFirstJapaneseFont(userSerifCandidates(), candidates, sizeof(candidates) / sizeof(candidates[0]),
                               pointSize);
}

TTF_Font *loadCjkFallbackFont(int pointSize) {
  static const char *const candidates[] = {
      "/System/Library/Fonts/ヒラギノ角ゴシック W4.ttc",
      "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
      "/System/Library/Fonts/ヒラギノ角ゴシック W6.ttc",
      "/System/Library/Fonts/Hiragino Sans GB.ttc",
      "/System/Library/Fonts/PingFang.ttc",
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
  };
  return openFirstJapaneseFont(userSansSerifCandidates(), candidates, sizeof(candidates) / sizeof(candidates[0]),
                               pointSize);
}

// グリフのテクスチャキャッシュ(2026-08-24、docs/CHANGES.md項目428)
// ――これが存在する理由の完全な経緯についてはcachedGlyphTexture()
// 自身のコメント(GraphicalDisplay::Implを必要とするので、Implの
// 定義の下)参照。`font`は不透明な識別子として保存/比較される
// (impl_->font対impl_->fallbackFont)。渡され得る2つのTTF_Font*の
// 値に正確に一致する――このキーを通じて決して逆参照されないので、
// ポインタが指すフォントをその場で変異させるTTF_SetFontSize()
// 呼び出しをまたいでも、比較として有効なままである(これがまさに、
// それが起きるたびにclearGlyphCache()が依然として呼ばれなければ
// ならない理由である: そのポインタは、そのポインタの新しいサイズ
// にとって「もはや正しいピクセルではない」古いエントリと比較して
// 等しいままになってしまう)。
struct GlyphCacheKey {
  std::string glyph;
  const void *font;
  int style;
  int hinting;
  unsigned char r, g, b;
  bool narrow;  // boostGlyphAlpha()が適用されたかどうか――キャッシュされるピクセルに影響する
  bool operator<(const GlyphCacheKey &other) const {
    return std::tie(glyph, font, style, hinting, r, g, b, narrow) <
           std::tie(other.glyph, other.font, other.style, other.hinting, other.r, other.g, other.b, other.narrow);
  }
};
struct CachedGlyph {
  SDL_Texture *texture = nullptr;
  int w = 0, h = 0;
};

}  // namespace

struct GraphicalDisplay::Impl {
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  TTF_Font *font = nullptr;
  TTF_Font *fallbackFont = nullptr;  // loadCjkFallbackFont()自身のコメント参照。見つからなければnullptr

  int rows = 0, columns = 0;
  int cellWidth = 0, cellHeight = 0;
  // フォント自身の「本当の」、決して上書きされないネイティブな
  // メトリクス――create()内で「一度だけ」、create()自身のcellWidth/
  // cellHeightのローカル変数の種になっているのと「同じ」計測から
  // 捕捉される(matchWindowSize()が実行され、代わりに本物の端末
  // 自身の算術的に導出された目標値で上のcellWidth/cellHeightを
  // 上書きし始める前に)。matchWindowSize()自身のフォント再スケー
  // リング(自身のコメント参照)と並んで2026-08-10に追加された。
  // 特に、そのメソッドが、複数回の呼び出し(セッション途中での
  // ライブなフォントサイズ変更、`^Og`の再入場等)にわたっても、
  // 安定した基準線から常に「目標サイズがフォントの『元々の』
  // ネイティブサイズからどれだけずれたか」を計算できるようにする
  // ためである――代わりにこのために上のcellWidth/cellHeightを
  // 読むと、複合してしまう: 最初のmatchWindowSize()呼び出しが
  // 既にそれらを目標値で置き換えた後、2回目の呼び出しはその後、
  // フォントの本当の元々のサイズではなく「前の」目標に対して
  // 相対的にスケーリングし、呼び出しごとにさらにずれていく。
  int baseFontPointSize = 16;
  int baseFontMetricCellWidth = 0, baseFontMetricCellHeight = 0;
  // フォントの「現在の」生きたポイントサイズ(2026-08-24、項目426)
  // ――baseFontPointSize(create()の後は決して上書きされない。
  // matchWindowSize()は、それがフォントの本当の元々のもののまま
  // であることに頼っている)とは異なり、これはadjustFontSize()
  // (Cmd+=/Cmd+-/Cmd+0)が呼ばれるにつれて「動く」。そのため、
  // 繰り返しの押下は、毎回元々のものからではなく、ユーザーが最後に
  // 残した場所から進む。create()ではbaseFontPointSizeと等しい値で
  // 始まる。
  int currentFontPointSize = 16;
  // 下のImpl::kColorPresetsのうちどれをcycleColorPreset()が最後に
  // 適用したか――-1は「依然としてcreate()自身のハードコードされた
  // デフォルト/setColors()が最後に直接何で呼ばれたか(例えば
  // 「実はすでにGUI」の起動時の端末色一致)のまま」を意味するので、
  // 最初のCmd+K押下はプリセット0を飛ばすのではなくそこに着地する。
  int colorPresetIndex = -1;
  // 本物の端末エミュレータのウィンドウ(Terminal.app、iTerm、
  // GNOME Terminal...)は全て、文字グリッドの周りに、テキストを
  // ウィンドウの端にぴったり突き当てるのではなく、ウィンドウの
  // 背景の控えめな境界を確保する――このクラスは元々そうしていな
  // かったので、それ以外は同一に見えるウィンドウでも、本物の端末
  // と並べると依然として目に見えて「おかしい」と読めていた
  // (実際のユーザー報告、2026-08-03、ユーザーが書かれた通りに実装
  // するよう依頼したGeminiが起草した技術的な回答経由: 全ての側に
  // マージンのピクセルを確保し、その分だけウィンドウを大きくし、
  // 全ての描画位置をそれだけずらす)。固定のピクセル値(例えば
  // cellWidth/cellHeightの分数ではなく)である理由は、それが本物の
  // 端末自身のマージンがそうであるものだからである――それはフォント
  // サイズと共に目に見えて大きくなったり小さくなったりしない。
  // Geminiが元々提案した15ではなく10: 実際の並べたスクリーン
  // ショット比較(2026-08-03の追加依頼、"余白はやや広め")が、
  // 本物のTerminal.appウィンドウ自身のマージンを直接測定した
  // (このサンドボックスには生きたウィンドウを測定する画面キャプ
  // チャのアクセスが無いので、スクリーンショット自体をピクセル
  // スキャンした)――タイトルバーの下端と最初のテキスト行の間が
  // 約9px、ウィンドウの左端と最初の列の間が約11px――別々のX/Y値
  // ではなく1つの共有された値として扱うのに十分近い。
  static constexpr int kMarginX = 10;
  static constexpr int kMarginY = 10;
  // スクロールバーのオーバーレイのために右端に予約された帯
  // (renderScrolled()自身のdrawScrollbar()呼び出し参照)――
  // 2026-08-07、IDEAS.mdが2026-08-03に記録した機能の最初の一切れ
  // (docs/HISTORY.mdの521行目自身の、それを始めるという依頼):
  // 実際の並べたスクリーンショット比較が、本物の端末自身のネイティ
  // ブなスクロールバーを計上すると、GUIウィンドウがそれがオーバー
  // レイすることを意図している本物の端末より、おおよそスクロール
  // バー1本分狭いことを発見した。上のkMarginX/Y(実際のスクリーン
  // ショットからピクセルスキャンされた)とは「異なり」、この値は
  // 元々、大まかな、測定されていないプレースホルダー(15)だった。
  // 2026-08-09に17へ修正された: このプロジェクトが初期に撮影した
  // 埋め込みの参照スクリーンショット(scrollbar_idle_image.hpp、
  // 以来削除済み――updateScrollPosition()自身のコメント参照)は、
  // ちょうど17px幅と測定される本物の-c端末のスクロールバーの切り
  // 抜きだった(kScrollbarIdleImageWidth、そのファイル自身の定数)。
  // 完全に独立した同日の計測(後の、以来撤回された本物のNSScroller
  // ウィジェットの試みの間、`+[NSScroller
  // scrollerWidthForControlSize:scrollerStyle:]`を直接問い合わせる)
  // が、このマシンで「同じ」17ptを返した。2つの独立した実測値が
  // 一致することは、元の推測よりもはるかに強い根拠である。実際の
  // ユーザー依頼(2026-08-09、"今の張りぼて画像は縮小されていて
  // 本来はHome brewくらいなので、つまみ幅も同じくらいにして...
  // 色々使ってから正式に決めたい")が、つまみの幅をその元の、縮小
  // されていない参照サイズに合わせるよう求めた――暫定として扱わ
  // れており(ユーザー自身の言葉)、より多くの実運用を待つ、最終的
  // な計測ではない。
  // 2026-08-30、直接のユーザー依頼("スクロールバーのつまみ表示幅を
  // モードラインのハイライト高さにあわせられない？フォントサイズ
  // 変更に追随させたいので"): 上のコメントに書かれている通り、17は
  // 元々固定の実測値(本物のTerminal.appの17pt幅)として扱われて
  // いたが、ユーザー自身の言葉で「暫定」だったもの。cellHeight
  // (モードライン自身が占める1行分のハイライトの高さでもある)へ
  // 連動する値に変更し、create()/adjustFontSize()/matchWindowSize()
  // (cellHeightを設定するこの3箇所全て)でその都度更新する――もはや
  // コンパイル時定数ではないためstatic constexprではなくインスタンス
  // フィールドにした。17という初期値そのものに意味は無くなったが、
  // create()が最初のcellHeightで即座に上書きするので実質無害である。
  int scrollbarWidth = 17;
  // updateScrollPosition()(main.cppの全てのrunGuiPhase()の反復で
  // 呼ばれる)によってキャッシュされ、flush()が、削除された装飾的
  // なプレースホルダーの代わりに、drawScrollbar()経由で本物の、
  // 常に最新のつまみを描画できるようにする――
  // updateScrollPosition()自身のコメント参照。
  int scrollTopIndex = 0, scrollTotalLines = 0, scrollContentRows = 0;
  bool scrollbarHidden = false;  // setScrollbarHidden()参照
  // ウィンドウの作成から、何であれ「最初の」マウス操作まではfalse
  // (pumpEvents()でtrueに設定され、決してfalseに戻されない)――
  // 実際の依頼(2026-08-09、1回前のラウンドで削除されたアイドル
  // 画像の復元: "-g起動後、マウスに触れるまでは画像が必要（一度
  // でも触れればつまみが永続的に表示され、以後は画像も不要と
  // なる）")が、「実はすでにGUI」の幻想自身の理由付けを、以前より
  // もう1段階深く適用することを望んだ: それは、ユーザーが実際に
  // ウィンドウの操作を始める瞬間まで(本物のマウスタッチはその
  // 明確な証拠である)重要なだけであり、以前の、今は撤回された
  // 「ドラッグまで常に画像」バージョンがそれを扱っていたような
  // 単なる「起動時」ではない。その最初のタッチの前は、flush()は
  // 静的な参照画像を貼り付ける((それは本物のキャプチャされた
  // 端末のスクロールバー「である」ため)それと区別が付かない)。
  // その後、flush()は永続的に本物の、常に最新のつまみ
  // (drawScrollbar())へ切り替わる。renderScrolled()自身のアクティ
  // ブなドラッグの描画に合わせている。
  bool everInteracted = false;

  // 2026-09-14追加、直接のユーザー報告("-g: プロンプトに日本語が
  // 入らない"): SDL_TEXTEDITING(IME変換中のプレビュー、事前に一度も
  // 処理していなかった)のtextが空でない間、trueにする――日本語入力の
  // 実際の流れは、ローマ字入力→IMEが変換候補を編集(SDL_TEXTEDITINGの
  // 連続)→returnキーで確定(SDL側がIMEへその押下を渡し、IMEが確定
  // した完成形の文字列を1回のSDL_TEXTINPUTとして配送)。ここで
  // SDL_TEXTEDITINGを一切見ていなかったため、変換確定のreturn押下を
  // SDL自体が(IME用の内部処理と「並行して」)通常のSDL_KEYDOWN
  // (SDLK_RETURN)としても配送してしまうケースを区別できず、それが
  // そのままkeyQueueへ0x0dとして積まれ、feedPromptByte()の
  // `if (byte == kCarriageReturn) return submitPrompt();`により、
  // 確定後のSDL_TEXTINPUTがまだ届いてもいないうちにプロンプト自体が
  // 送信されてしまっていた――日本語が「入力される前に」プロンプトが
  // 閉じてしまうので、ユーザーの目には「日本語が入らない」ように映る。
  // IME変換中はSDL_KEYDOWN自体を丸ごと無視する(下のcase SDL_KEYDOWN
  // 冒頭のガード参照)ことで、Return/Backspace/矢印キー等をIME自身に
  // 完全に譲る――本物の端末がIMEとの間で同じ排他関係を保っているのと
  // 同じ発想。
  bool imeComposing = false;
  std::vector<Cell> grid;

  SDL_Color fg{220, 220, 220, 255};
  SDL_Color bg{0, 0, 0, 255};

  int cursorRow = 0, cursorCol = 0;
  int savedRow = 0, savedCol = 0;
  bool standoutActive = false, boldActive = false, underlineActive = false, italicActive = false,
       highlightActive = false, highlightCurrentActive = false, serifActive = false;
  // 明朝体(Display::Attribute::Serif参照)。最初に明朝の字を描く時にloadSerifFont()で
  // 読み込む(見つからなければserifFontMissingを立て、以後はふだんのフォントで描く)。
  TTF_Font *serifFont = nullptr;
  bool serifFontMissing = false;
  // setContentOffsetX()参照。
  int contentOffsetX = 0;
  int contentOffsetRows = 0;
  // 縦書きを描いている間だけ(左右の慣性の途中で横書きの範囲へ入った瞬間に、横書きの画面が
  // ずれて描かれないように)。
  int offsetXForRow(int row) const { return (verticalText && row < contentOffsetRows) ? contentOffsetX : 0; }
  // Display::beginContentEdgeCapture()参照。edgeGrid[0]が先(左)、[1]が前(右)の裏の画面。
  // 取り込み中は書き込み先のgridと入れ替え、カーソル位置・属性はsaved*に退避する。
  std::vector<Cell> edgeGrid[2];
  std::vector<Cell> edgeSavedGrid;
  bool edgeCapturing = false;
  int edgeSavedRow = 0, edgeSavedCol = 0;
  bool edgeSavedFlags[7] = {};
  int edgeSavedFg = 0, edgeSavedBg = 0;
  int fgColorActive = Display::kDefaultColor, bgColorActive = Display::kDefaultColor;  // setColor()

  // renderCursor()が実際に描く「論理的な」カーソル位置――上の
  // cursorRow/cursorCol(純粋に「次のバイトがどこに書かれるか」で
  // あり、ContentView::redraw()自身の行ごと/モードラインの書き込み
  // によって絶えず動かされる)とは意図的に別である。
  // setVisibleCursor()だけがこれに触れる。なぜこの分割が存在する
  // のかについてはDisplay::setVisibleCursor()自身のコメント参照。
  // hasVisibleCursorはfalseで始まる。そのため、どの呼び出し元も
  // カーソルを明示的に求める前は何も描画されない(ウィンドウが開いた
  // 瞬間、例えばSHELLやプロンプトがどこか意味のある場所に1つを位置
  // 決めする前に、デフォルトの(0,0)にはぐれた箱が出るのを避ける)。
  bool hasVisibleCursor = false;
  int visibleCursorRow = 0, visibleCursorCol = 0;

  std::string pendingUtf8;  // 不完全なマルチバイトシーケンスのバイト、putByte()参照

  bool outputEnabled = true;
  bool smoothScroll = false;
  bool verticalText = false;  // setVerticalTextHint()参照
  // 2026-09-12追加、項目⑫: display.hpp自身のmodeLineVisible()宣言
  // コメント参照。GraphicalDisplayには独立したモードライン専用の
  // 描画要素が無い(ContentViewが渡す行データをそのままセルグリッド
  // へ描くだけ)ため、このフラグ自体はここに保持するだけで、実際の
  // 表示/非表示はApplication::layoutViews()のcontentHeight計算と
  // ContentView::renderModeLine()の抑制ロジックだけで完結する。
  bool modeLineVisible = true;

  std::deque<MouseClick> mouseQueue;
  // 2026-09-14修正、直接のユーザー報告("左クリックでの選択ができ
  // ない"、実機で確認)を受けて調査した結果、2026-09-10に導入した
  // 「ボタン解放まで保留し、途中で動きがあれば破棄する」という
  // タイミング/モーションベースの仕組みは、実は全く同じ問題空間で
  // 過去に2回試して2回とも実機検証の結果ユーザー自身の手で明示的に
  // 撤回されていた設計だったと判明した(HISTORY.md 2026-08-05、
  // コミット`e1a00d2`→`8658ae4`、「実マウス/トラックパッドの微小な
  // ジッターでクリックが効かなくなる」)。最終的に採用され実機確認
  // 済み("動作確認OK")なのはコミット`a73c052`のタイミング非依存な
  // 空間スコープ方式――スコープ判定(Application::handleMouseClick()
  // 側のContentView::isColumnWithinLineScope())自体は変わらず押下の
  // 瞬間に決まる。
  //
  // 2026-09-15追加、直接のユーザー依頼("試しでいいので、左クリックが
  // 解放された時点で開く、にしてみてくれないか。可能かどうかを
  // まず確認したい"): スコープ判定はそのまま(押下時に決まる)で、
  // 「実行(mouseQueueへ積む)」だけをボタン解放まで遅らせる実験。
  // 前回撤回した仕組みとの違いは、動きが1回でも届いたら破棄する
  // というモーション判定を一切持たないこと――そのため2026-08-05の
  // ジッター問題は原理上再発しない。ただし裏を返すと、押下がスコープ
  // 内で始まったドラッグ(今までは押下即座にクリックとして処理され
  // つつ同時にドラッグも走っていた)は、解放の瞬間に「押下位置での
  // クリック」も追加で発火するようになる――例えばファイル名の上で
  // 押してそのままドラッグでスクロールし、離した場合、離した位置とは
  // 無関係に「押した時のファイル」が開いてしまう。これが実用上どう
  // 感じられるかを含めて、まずは実機で試してもらうための実装。
  std::optional<MouseClick> pendingLeftClick;
  std::deque<KeyEvent> keyQueue;
  // consumeDroppedFiles()自身の宣言コメント参照。
  std::vector<std::string> pendingDrop;       // 受け渡しの途中
  std::vector<std::string> completedDrop;     // 揃った一式(consumeDroppedFiles()が返す)
  bool dropInProgress = false;                // SDL_DROPBEGIN〜SDL_DROPCOMPLETEの間
  std::deque<DragEvent> dragQueue;
  std::deque<HoverEvent> hoverQueue;
  // 2026-09-20追加、直接のユーザー報告("^[97^M e で戻った直後、マウス
  // には一切触れず^Lしたのに98になる"、GraphicalDisplay::pumpEvents()
  // 自身のSDL_MOUSEMOTIONケース参照): 外部エディタから戻ってウィンドウ
  // が再びINPUT_FOCUSを得た直後、ポインタが物理的には全く動いていない
  // のに、SDLが「フォーカスを得た」ことの副作用として同じ座標のまま
  // もう一度SDL_MOUSEMOTIONを配送してくることがある(USILICGUIDEBUG
  // 実機ログで確認: 同一のx/yで、focus=0のイベントの直後にfocus=1の
  // イベントが続けて届いていた)――以前はfocus判定さえ通ればそのまま
  // hoverQueueへ積んでいたため、この「動いていないのに届いた」1発が
  // 本物のホバー移動と区別できず、直前に^[Nで正しく合わせたカーソル
  // 位置を黙って上書きしていた。座標が前回と変わっていない場合は
  // hoverQueueへ積まない(genuine movementの時だけ積む)ことで、
  // フォーカス復帰の副作用と実際のマウス移動を区別する。
  int lastHoverEligibleMotionX = -1;
  int lastHoverEligibleMotionY = -1;
  std::deque<WheelEvent> wheelQueue;
  std::deque<ScrollbarEvent> scrollbarQueue;
  // スクロールバーの帯の内側で始まった左ボタン押下から、対応する
  // ボタン解放までtrue――ScrollbarEvent自身のコメント参照。
  // SDL_MOUSEMOTION自体は「元々の」ボタン押下がどこだったかの
  // 記憶を全く運ばないので、scrollbarQueueへルーティングされる
  // べきMOTIONイベント(つまみのドラッグとして始まった)と、通常の
  // 内容のdragQueue/hoverQueueへ行くべきものとを区別する。
  bool scrollbarDragActive = false;
  // consumeNeedsRedraw()自身の裏付けとなるフラグ――その宣言コメント
  // (graphical_display.hpp)参照。trueで始まるので、SDL_WINDOWEVENT_
  // EXPOSEDが何らかの理由で決して発火しなくても、構築後のまさに
  // 最初のフレームは依然として再描画される(念のための保険であり、
  // 実際の修正にとって必須ではない――修正は、後続の全ての発生の
  // たびにEXPOSEDがこれを再びtrueに設定することである)。
  bool needsRedraw = true;
  // consumeSizeChanged()自身の裏付けとなるフラグ(2026-08-24、
  // 項目425)――pumpEvents()のSDL_WINDOWEVENT_RESIZED処理(ユーザー
  // がウィンドウ自身の端をドラッグする)によって設定され、
  // runGuiPhase()によって、本物の端末のSIGWINCHがそうするのと同じ
  // 方法でApplication::handleResize()を呼ぶために消費される――
  // consumeSizeChanged()自身の宣言コメント(graphical_display.hpp)
  // 参照。
  bool sizeChanged = false;
  // handCursorActive: 「内容自体」をドラッグする(「猫の手」スクロ
  // ールジェスチャー)ための、開いた手のカーソル(下のhandCursor)が
  // 現在表示されているかどうかを追跡する。pointingCursor: SDLの
  // システムの指差し(人差し指)カーソルで、スクロールバーのつまみの
  // ドラッグに使われる――setPointingCursor()自身のコメント参照。
  // 実際の依頼("画面ドラッグでは手の形をこれにして欲しい...現状の
  // 人差し指は、スクロールバーのドラッグ...のみでの表示よろしく")
  // が、単一の「あらゆるドラッグに指差しの手」というカーソルは、
  // 本物のトラックパッド/マウスのOSの慣習(内容をパンしている間は
  // 開いた手、スクロールバーのようなクリック可能なコントロールに
  // 対してだけ指差しの手)が行うようには2つのジェスチャーを区別
  // しないことを指摘した後、2026-08-10に2つのカーソルへ分割された。
  //
  // handCursor: 本物のmacOSの開いた手のカーソルのピクセル
  // (mac_cursor.hppのextractOpenHandCursorImage())から(遅延的に、
  // 最初の使用時に)構築されるSDL_Cursorであり、下のpointingCursor
  // と全く同じようにSDL_SetCursor()経由で駆動される――生の
  // [[NSCursor openHandCursor] set]呼び出しでは「ない」。このフィー
  // ルドの以前のバージョンはそれを直接呼び、SDLを完全に飛ばして
  // いた。それはマウスダウン時には正しく表示されたが、ドラッグが
  // 実際に動き始めた瞬間にプレーンな矢印に戻ってしまった(実際の
  // 報告: "画面ドラッグを開始したとたんに矢印に戻る")。SDL自身の
  // Cocoaバックエンドが、マウス移動イベントのたびに「それ自身が」
  // 追跡しているSDL_Cursorを再主張し、SDLが知らない裏チャネル経由
  // で設定されたどんなカーソルも踏みつぶしてしまうためである。
  // pointingCursorと同様SDL_SetCursor()経由でそれをルーティング
  // することは、pointingCursorがそもそも一度もその問題を持たなか
  // ったのと同じ方法でそれを修正する――setHandCursor()自身の
  // コメント参照。
  bool handCursorActive = false;
  SDL_Cursor *handCursor = nullptr;
  SDL_Cursor *pointingCursor = nullptr;
  bool pointingCursorActive = false;

  // 完全な経緯についてはcachedGlyphTexture()自身のコメント(この
  // 構造体のすぐ下)参照――全てのエントリが常に「現在の」フォント/
  // サイズ/色に正確に一致するよう、キー付け/クリアされている。
  std::map<GlyphCacheKey, CachedGlyph> glyphCache;

  Cell &cellAt(int row, int col) { return grid[row * columns + col]; }
};

namespace {

// キャッシュされたグリフのテクスチャを返す(最初の使用時にだけ
// ラスタライズしGPUへアップロードする)。TTF_RenderUTF8_Blended()
// 自体が失敗した場合はnullptr。renderCell()/renderCursor()/
// renderTextRun()で共有される――以前は3つとも、前のフレームから
// 内容が全く変わっていないセルに対してでさえ、文字通り全てのグリフ
// に対して、文字通り全ての単一のフレームで、TTF_RenderUTF8_Blended()
// +SDL_CreateTextureFromSurface()(FreeType経由のCPUラスタライズ、
// その後新しいGPUテクスチャのアップロード)を呼んでいた。この
// プロジェクト自身のApple Siliconの開発マシンでは、これは不可視で
// あるほど十分速いままだが、実際のmpi(Intel Mac、i5-9400+統合
// グラフィックス)からの、一般的なGUIのもっさり感――特に、その基礎
// となるpixelOffsetの計算が本当に連続的で、行未満で、任意の位置
// であるにも関わらず(2026-08-24)、ぎこちない/「行単位」に見える
// 慣性スクロールの動き――の報告は、まさにここを指している:
// renderScrolled()単独で、約16msの惰性物理演算の一切れごとに
// 可視の全ての行を再描画するので、全画面のスクロールは、1秒あた
// り何千もの個別のテクスチャの生成+アップロード+破棄を意味する
// ことがある。より弱い/古いGPUがその量のchurnで詰まると、アプリ
// 自身の内部状態はフレームからフレームへなめらかに進んでいるに
// も関わらず、まさに報告された症状(実際に完了する数少ないフレー
// ムの間の、大きな、稀なジャンプ)を生み出すだろう。(グリフ文字列、
// フォントの識別子、スタイル、ヒンティング、色、細いグリフの
// アルファ増強フラグ)によるキャッシュ――結果のピクセルに実際に
// 影響する全て――は、定常状態のスクロール(同じ可視の文字が行から
// 行へ滑る)が、毎フレームゼロから再ラスタライズするのではなく、
// 「同じ」既にアップロード済みのテクスチャを再利用することを
// 意味する。
//
// TTF_SetFontStyle()/TTF_SetFontHinting()は、今はキャッシュ
// 「ミス」時にだけ呼ばれる(それらが常にその前に呼ばれていた
// 本物のTTF_RenderUTF8_Blended()呼び出しの直前)――単に速いだけで
// なく正しい。それらの呼び出しは、そのフォントオブジェクトを通じた
// 「次の」描画にだけ影響するため。一度グリフがキャッシュされると、
// スタイル/ヒンティングは既にその保存されたテクスチャに焼き込まれ
// ており、それを再利用するために再主張する必要は無い。
// `ImplT`(推論される、実際には常にGraphicalDisplay::Impl)は、
// GraphicalDisplay::Implを直接名指しするのではない: それはプライ
// ベートなネストされた型であり、テンプレート引数の推論が、この
// 同じファイル内のフリー関数が、friendアクセスを必要とせずにそれ
// に対して操作できる標準的な方法である――アクセス制御は、プライ
// ベートなメンバーを明示的に「名指しする」ことに適用され、推論
// されたテンプレートパラメータには適用されない。
// 明朝体で描く字(Cell::serif)のフォント。明朝体のフォントが無い、またはその字を
// 持たなければnullptr(ふだんのフォントで描く)。Implを要するのでcachedGlyphTexture()と
// 同じくテンプレートにしてある。
template <typename ImplT>
TTF_Font *serifFontFor(ImplT &impl, unsigned int codepoint) {
  if (!impl.serifFont && !impl.serifFontMissing) {
    impl.serifFont = loadSerifFont(impl.currentFontPointSize);
    impl.serifFontMissing = impl.serifFont == nullptr;
  }
  if (!impl.serifFont || !glyphProvidedByFont(impl.serifFont, codepoint)) return nullptr;
  return impl.serifFont;
}

template <typename ImplT>
SDL_Texture *cachedGlyphTexture(ImplT &impl, TTF_Font *font, const std::string &glyph, int style, int hinting,
                                 SDL_Color color, bool narrow, int &outW, int &outH) {
  GlyphCacheKey key{glyph, font, style, hinting, color.r, color.g, color.b, narrow};
  auto it = impl.glyphCache.find(key);
  if (it != impl.glyphCache.end()) {
    outW = it->second.w;
    outH = it->second.h;
    return it->second.texture;
  }
  TTF_SetFontStyle(font, style);
  TTF_SetFontHinting(font, hinting);
  SDL_Surface *surface = TTF_RenderUTF8_Blended(font, glyph.c_str(), color);
  if (!surface) return nullptr;
  if (narrow) boostGlyphAlpha(surface, kNarrowGlyphAlphaGamma);
  SDL_Texture *texture = SDL_CreateTextureFromSurface(impl.renderer, surface);
  outW = surface->w;
  outH = surface->h;
  SDL_FreeSurface(surface);
  impl.glyphCache.emplace(std::move(key), CachedGlyph{texture, outW, outH});
  return texture;
}

// 全てのキャッシュされたテクスチャを破棄し、キャッシュを空にする
// ――キャッシュされたエントリが表すピクセルが、そのキーが依然として
// 等しいと比較されるにも関わらず古びる可能性がある時は常に呼ばれ
// 「なければならない」: 具体的には、全てのTTF_SetFontSize()呼び
// 出し(adjustFontSize()/matchWindowSize()、どちらも
// cachedGlyphTexture()自身のキーがフォントの識別子として使うのと
// 同じポインタの値でimpl_->font/fallbackFontをその場で変異させる
// ――GlyphCacheKey自身のコメント参照)と、デストラクタ(テクスチャは
// impl_->rendererがまだ有効な間に破棄されなければならないので、
// これはSDL_DestroyRenderer()の前に実行される)。色の変更
// (setColors()/cycleColorPreset())はこれを必要としない: 色は既に
// キャッシュキーの一部なので、古い色のエントリは単に間違って使わ
// れるのではなく単に使われなくなるだけである。
template <typename ImplT>
void clearGlyphCache(ImplT &impl) {
  for (auto &[key, cached] : impl.glyphCache) {
    if (cached.texture) SDL_DestroyTexture(cached.texture);
  }
  impl.glyphCache.clear();
}

}  // namespace

GraphicalDisplay::GraphicalDisplay() : impl_(std::make_unique<Impl>()) {}

std::unique_ptr<GraphicalDisplay> GraphicalDisplay::create(int rows, int columns, const std::string &fontPath,
                                                             int fontPointSize) {
  if (rows <= 0 || columns <= 0) return nullptr;
  // このヒントが無いと、SDL_RenderCopy()は最近傍サンプリングで
  // テクスチャをスケーリングする。fitGlyphRect()はほぼ全てのグリフ
  // のビットマップを何らかの小さな分数の係数でスケーリングする
  // (フォントで描画されたピクセルサイズは、本質的に目標のセルの
  // 箱にちょうど着地することは無い)ので、最近傍は、FreeTypeの既に
  // アンチエイリアスされたグリフを、目に見えてブロック状/階段状の
  // 端に変えてしまう――これが、-cのなめらかなCoreText描画と並べた
  // 時に「ベクターではない、細い半角のテキスト」として読めていた
  // ものである。「linear」は、代わりにSDL_RenderCopyのスケーリング
  // をなめらかにする。
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return nullptr;
  if (TTF_Init() != 0) {
    SDL_Quit();
    return nullptr;
  }

  TTF_Font *font = TTF_OpenFont(fontPath.c_str(), std::max(fontPointSize, 1));
  if (!font) {
    TTF_Quit();
    SDL_Quit();
    return nullptr;
  }
  int cellWidth = 0, cellHeight = 0;
  if (TTF_SizeUTF8(font, "M", &cellWidth, &cellHeight) != 0 || cellWidth <= 0 || cellHeight <= 0) {
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();
    return nullptr;
  }
  // 上のTTF_SizeUTF8()の高さは「M」自身のタイトなグリフの外接箱
  // (その特定のグリフだけのascent+descent)である――フォントの実際
  // の推奨「行」高さ(フォント全体を通じたascent+|descent|+行間、
  // 本物の端末エミュレータが行間隔に使うメトリクス)よりも狭い。
  // 実際の並べたスクリーンショット比較(2026-08-08、"行間が狭い"、
  // -c/-gのウィンドウを隣り合わせに置いた)が、このウィンドウの
  // 行間隔が本物の端末自身のものより目に見えて詰まっていることを
  // 確認した。TTF_FontHeight()はその適切な行高さのメトリクスを
  // 直接返す。(完全に置き換えるのではなく)2つのうち大きい方を
  // 取る。そうすれば、TTF_FontHeight()が何らかの理由で単一のグリフ
  // 自身の外接箱より小さく報告する珍しいフォントでも、そのグリフ
  // を切り取ってしまうことはない。
  cellHeight = std::max(cellHeight, TTF_FontHeight(font));

  // ウィンドウ/セルグリッドがサイズ決定される「前に」(後で遅延的
  // にではなく)読み込み、計測される。これにより、「別の」フォント
  // 用にサイズ決めされたグリッドに合わせるために、どちらかの
  // フォントのグリフを描画時にスケーリングするのではなく、両方の
  // フォントに合うようグリッドを前もって大きくできる。以前の
  // バージョンは後者を行っていた(一様に縮小して合わせる、非一様
  // な引き伸ばしの試みが目に見えてCJKグリフの形を歪めた後に追加
  // された)が、それでも「半分欠けた」日本語文字の実際の報告として
  // 現れた――フォールバックフォントのグリフを主フォントサイズの
  // セルへ縮小することは、依然として細いストロークを、不完全に
  // 見えるほど酷く切り取ったり細くしたりし得る。代わりにグリッド
  // を大きくすることは、両方のフォントが常にそれ自身の自然な、
  // 歪んでいない、切り取られていないサイズで描かれることを意味
  // する。
  TTF_Font *fallbackFont = loadCjkFallbackFont(fontPointSize);
  if (fallbackFont) {
    int cjkWidth = 0, cjkHeight = 0;
    if (TTF_SizeUTF8(fallbackFont, "\xE5\x9B\xBD" /* "国"、代表的な漢字 */, &cjkWidth, &cjkHeight) == 0) {
      // CJKのグリフは2列を占める(commitGlyph()の継続セルの慣習
      // 参照)ので、その自然な幅の半分が1列が保持する必要のあるもの
      // である。決してわずかに狭すぎないよう切り上げる。
      cellWidth = std::max(cellWidth, (cjkWidth + 1) / 2);
      cellHeight = std::max(cellHeight, cjkHeight);
    }
  }

  // 上のTTF_FontHeight()ベースの修正が、特定のフォントに対して
  // 本物の端末自身の行間隔とのギャップをまだ完全には埋めない場合
  // の逃げ道(行高さの慣習は、上のタイトルバーの高さのケースより
  // フォント/アプリによって大きく異なる)――
  // USILICTITLEBARHEIGHT/USILICSCROLLBARSCALEと同じ「もう1回の
  // コード変更+再ビルドのラウンドではなく環境変数経由で調整可能」
  // というパターン。設定されると、完全に計算されたcellHeight
  // (CJKフォールバックの最大値の後)を完全に上書きする。
  if (const char *env = std::getenv("USILICLINEHEIGHT")) {
    int parsed = std::atoi(env);
    if (parsed > 0) cellHeight = parsed;
  }

  // SDL_WINDOW_ALLOW_HIGHDPI: これが無いと、Retina/HiDPIディスプレイ
  // では、SDLはウィンドウの実際の描画領域を「論理的な」ポイント解像
  // 度で作成し、OSのコンポジタがその低解像度のフレームバッファを
  // 実際の物理ピクセルを満たすようアップスケールする――実際の報告
  // が説明した("文字がぼやけ気味")まさにその種の柔らかい/ぼやけた
  // テキストであり、このファイル内の全ての描画呼び出しにわたって
  // 一様である(renderCell()とrenderTextRun()の両方を読んで確認済み:
  // それらは他の点ではほぼ同一のTTF_RenderUTF8_Blended()+
  // fitGlyphRect()の呼び出しの並びなので、それらの間のテキストの
  // 鮮明さの違いは経路ごとの描画のバグではない――このコンポジタ
  // レベルのアップスケールは、どちらの経路も個別には引き起こしたり
  // 避けたりできない、ウィンドウ/レンダラー全体の効果である)。
  // SDL_WINDOW_SHOWNではなくSDL_WINDOW_HIDDEN――なぜウィンドウが、
  // ここではなくopen()が呼ばれるまで意図的に表示されないままにさ
  // れるのかについてはopen()自身のコメント参照。
  // SDL_WINDOW_RESIZABLE(2026-08-24、docs/CHANGES.md項目425、直接
  // のユーザー依頼): ウィンドウ自身の端をドラッグすることが今は
  // 動作する――それが発火した時実際に何が起こるかについては、この
  // ファイル内の下にあるpumpEvents()自身のSDL_WINDOWEVENT_RESIZED
  // 処理参照。matchWindowSize()自身のリサイズの意味論(「実はすでに
  // GUI」オーバーレイ自身の用途のために、rows/columnsを「固定」の
  // ままにし、外部から指示されたピクセルサイズに合わせてセル/
  // フォントサイズを再スケーリングする)とは意図的に「異なる」
  // ――ユーザーがこのウィンドウの端をドラッグする時、同じセルサイズ
  // で「より多くの」行/列が見えるようになることを期待する。どんな
  // 端末エミュレータも既に持っている通常のリサイズの挙動である。
  SDL_Window *window = SDL_CreateWindow(
      "usilic", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      cellWidth * columns + Impl::kMarginX * 2 + cellHeight, cellHeight * rows + Impl::kMarginY * 2,
      SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  if (!window) {
    if (fallbackFont) TTF_CloseFont(fallbackFont);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();
    return nullptr;
  }
  // 実質的な上限ではなく下限――モードライン自身の固定幅のフィールド
  // (バイト数、位置、等)が完全に判読不能な重なりへ引きずり込まれる
  // のを防ぐのにちょうど十分な程度であり、それでもどんな現実的な
  // 使用よりも余裕を持って小さい。セル単位で10列x4行。
  SDL_SetWindowMinimumSize(window, cellWidth * 10 + Impl::kMarginX * 2 + cellHeight,
                            cellHeight * 4 + Impl::kMarginY * 2);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) {
    SDL_DestroyWindow(window);
    if (fallbackFont) TTF_CloseFont(fallbackFont);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();
    return nullptr;
  }
  // ウィンドウは論理的な「ポイント」(cellWidth*columns等、このファイル
  // 全体でセル/行/列のピクセル計算に使われている単位)で作成された;
  // レンダラーの実際の描画領域は今や物理的な「ピクセル」(上のALLOW_
  // HIGHDPIのおかげでRetinaディスプレイでは2倍/3倍)である。レンダラーを
  // drawable/windowの比率でスケーリングすることで、このファイル内の
  // 既存のピクセル座標が全て「1論理ポイント」を意味したまま、SDLが
  // 内部的にはより高い物理解像度でレンダリングする――下流のピクセル
  // 計算を別の単位に書き換えることなくHiDPI対応を追加する、標準的な
  // SDL2のパターン。
  syncHighDpiRenderScale(window, renderer);

  // std::unique_ptr<GraphicalDisplay>(new ...): コンストラクタは
  // privateであり(呼び出し元はこのファクトリを経由する必要がある、
  // TerminalDisplay::fromEnvironment()自身の形と一致)、make_uniqueは
  // クラスの外からはそこへ到達できない。
  std::unique_ptr<GraphicalDisplay> display(new GraphicalDisplay());
  display->impl_->window = window;
  display->impl_->renderer = renderer;
  display->impl_->font = font;
  display->impl_->fallbackFont = fallbackFont;
  display->impl_->cellWidth = cellWidth;
  display->impl_->cellHeight = cellHeight;
  display->impl_->scrollbarWidth = cellHeight;  // Impl::scrollbarWidth自身のコメント参照
  display->impl_->baseFontPointSize = fontPointSize;
  display->impl_->currentFontPointSize = fontPointSize;
  display->impl_->baseFontMetricCellWidth = cellWidth;
  display->impl_->baseFontMetricCellHeight = cellHeight;
  display->impl_->rows = rows;
  display->impl_->columns = columns;
  display->impl_->grid.assign(static_cast<std::size_t>(rows) * columns, Cell{});
  SDL_StartTextInput();
  // ここでは意図的に表示/前面化しない(以前のバージョンは、まさに
  // この箇所でSDL_CreateWindow(SDL_WINDOW_SHOWN, ...)の直後に
  // SDL_RaiseWindow()を呼んでいた)――なぜウィンドウの表示が代わりに
  // open()の方へ先送りされるようになったのかについてはopen()自身の
  // コメント参照。
  return display;
}

GraphicalDisplay::~GraphicalDisplay() {
  // create()自身のSDL_StartTextInput()呼び出しに対応する(input methodで
  // 入力されたマルチバイトUTF-8を含む、SDL_TEXTINPUTイベントがそもそも
  // 届くために必要――pumpEvents()自身のコメント参照)。ウィンドウ/
  // レンダラーが破棄される前に対応するSDL_StopTextInput()が無いと、
  // macOSのInput Method Kitフレームワークは、まさに足元から消えようと
  // しているtext-inputクライアントへの参照を持ったままになる――実際の
  // 2026-08-04のユーザー報告(スクリーンショット)は、-gセッション終了
  // 直後に端末へこう出力されるのを示した: "error messaging the mach
  // port for IMKCFRunLoopWakeUpReliable"。無害(クラッシュも機能面への
  // 影響もどちらも無い――これはOSレベルの診断メッセージであり、usilic
  // 自身が検知・対処するものではない)だがうるさい; ここでの
  // SDL_StopTextInput()は、まさにこのIMK破棄順序に関する不満への文書化
  // された修正である。
  //
  // 2026-08-05の追跡報告では、メッセージが依然として出るが、特に
  // ウィンドウがtext-inputフォーカスをアクティブに持っている間に打たれた
  // 「キーボード」ジェスチャ(ESC-ESC-Enterまたは^X^C+y)経由で終了した
  // 場合に出る――と分かった――ウィンドウ自身の閉じるボタンや、単なる
  // マウス操作の場合とは対照的に。SDL_StopTextInput()だけでは、SDL/
  // Cocoaへの無効化をキューに積むだけで、その直後すぐに続く
  // SDL_Quit()/ウィンドウ破棄が進む前に、それを実際に実行させる強制力
  // は無い。もしOSのInput Method Kitがその通知をまだ完全に処理して
  // いなかったなら――アイドルなマウスクリックの後よりも、まさに生の
  // キーボード/IME入力を能動的に処理していた直後の方がありそうな話
  // だが――それが依然として文句を言う、もっともらしい理由になる。
  // SDL_PumpEvents()は、このデストラクタがウィンドウの破棄へ進む前に、
  // その通知を実際に配送するための、SDL自身のイベント/実行ループの
  // 仕組みへもう一巡分の明示的な機会を与える。このサンドボックスでは
  // 依然として独立に検証できていない(OSレベルのログ収集が利用できない、
  // このポート作業の他のGUI破棄関連の箇所全体で述べてきたのと同じ制約)
  // ――ビルドし直した後、特にESC-ESC-Enterと^X^C+y経由で再テストして
  // ほしい。
  SDL_StopTextInput();
  SDL_PumpEvents();
  if (impl_->handCursor) SDL_FreeCursor(impl_->handCursor);
  if (impl_->pointingCursor) SDL_FreeCursor(impl_->pointingCursor);
  clearGlyphCache(*impl_);  // impl_->rendererがまだ有効なうちに実行する必要がある――それ自身のコメント参照
  if (impl_->renderer) SDL_DestroyRenderer(impl_->renderer);
  if (impl_->window) SDL_DestroyWindow(impl_->window);
  if (impl_->serifFont) TTF_CloseFont(impl_->serifFont);
  if (impl_->fallbackFont) TTF_CloseFont(impl_->fallbackFont);
  if (impl_->font) TTF_CloseFont(impl_->font);
  TTF_Quit();
  SDL_Quit();
}

void GraphicalDisplay::write(const std::string &text) {
  for (char c : text) putByte(static_cast<unsigned char>(c));
}

void GraphicalDisplay::putChar(int c) { putByte(static_cast<unsigned char>(c)); }

void GraphicalDisplay::putByte(unsigned char c) {
  if (!impl_->pendingUtf8.empty()) {
    impl_->pendingUtf8.push_back(static_cast<char>(c));
    int len = Utf8::sequence_length(static_cast<unsigned char>(impl_->pendingUtf8[0]));
    if (static_cast<int>(impl_->pendingUtf8.size()) >= len) {
      int width = Utf8::display_width(reinterpret_cast<const unsigned char *>(impl_->pendingUtf8.data()), len);
      // 2026-08-20修正、commitGlyph()内のwidth==0の結合文字マージ
      // (元の09c6981修正と、同日のcomposeDakutenCodepoint()による
      // 追従の両方)がなぜ実際には一度も発火しなかったのか追跡する
      // ために追加した、一時的なグリフ単位のUSILICGUIDEBUGトレース
      // で発見: このstd::max(width, 1)によるクランプが、commitGlyph()
      // がそれを見る前に0を「消して」いた――トレースはU+3099がそこへ
      // `width=1`として届いていることを示した、`width=0`ではなく、
      // Utf8::display_width()自身は正しく0を返しているにもかかわらず
      // (単体のdoctestと、実際のアーカイブ自身の生バイトに対して直接
      // 実行した使い捨てのスタンドアロンプログラムの両方で独立に確認
      // 済み)。commitGlyph()は既に、そのNORMAL(非ゼロ幅)経路に自分
      // 自身のstd::max(width, 1)クランプを持っており、それはwidth==0
      // の特殊ケースが実行される機会を既に得た「後」に適用される――
      // その判定がその値を見るより前の「ここで」再度クランプすること
      // で、width==0の分岐全体が、この呼び出し箇所からは永久に到達
      // 不能になっていた(実際に計算されたwidthを渡す唯一の箇所;他の
      // 2つのcommitGlyph()呼び出し箇所は、プレーンなASCII/制御バイト
      // に対して常にリテラルの1を渡し、0を渡すことは無いため、この
      // バグはテスト用の呼び出しからは見えなかった)。widthをそのまま
      // 素通しすることで、commitGlyph()が元々作用するよう設計されて
      // いた信号を復元する。
      commitGlyph(impl_->pendingUtf8, width);
      impl_->pendingUtf8.clear();
    }
    return;
  }

  if (c == 7) {  // BEL――2026-09-25からsystemBeep()(macOSのNSBeep()、mac_cursor.hpp参照)で鳴らす
    systemBeep();
    return;
  }

  int len = Utf8::sequence_length(c);
  if (len > 1) {
    impl_->pendingUtf8.assign(1, static_cast<char>(c));
    return;
  }
  if (c < 0x20 || c == 0x7f) {
    commitGlyph(" ", 1);  // 制御バイトのプレースホルダー――クラスコメント参照
    return;
  }
  commitGlyph(std::string(1, static_cast<char>(c)), 1);
}

void GraphicalDisplay::commitGlyph(const std::string &utf8Bytes, int width) {
  if (impl_->cursorRow < 0 || impl_->cursorRow >= impl_->rows) return;
  if (impl_->cursorCol < 0 || impl_->cursorCol >= impl_->columns) return;  // 右端を超えた分を切り捨てる

  // 幅ゼロの結合文字(Utf8::display_width()自身の「結合文字(0幅)」
  // ケース――中でも主なのが濁点/半濁点記号のU+3099/U+309Aで、macOS
  // 自身のNFD分解されたファイル名でよく見られる、例えば「デスクトップ」
  // はディスク上では「テ」+U+3099+「スクトッフ」+U+309A+「...」として
  // 保存されている) -- 2026-08-20修正、実際の報告+スクリーンショット
  // (=dl): "久々に-g起動したら重ね打ちじゃなくなってる"、実際のディレ
  // クトリ一覧でまさにこれ(「デスクトップ」ではなく「テ゛スクトッフ゜」)
  // が表示されているのを示していた。下の各呼び出し箇所は
  // 以前は無条件でカーソルをstd::max(width, 1)分進めていた――純粋に
  // 幅0のマークでさえ自分「専用」のまっさらなセルを得ていたため、
  // 基底文字と結合する代わりに、それに隣接する独立した重ならない
  // グリフとして描画されていた。
  if (width == 0 && impl_->cursorCol > 0) {
    int prevCol = impl_->cursorCol - 1;
    // ワイド(CJK)文字自身の継続用シャドウセルは、それ自身の実際の
    // グリフを持たない(下のwidth==2の分岐参照)――その場合、この
    // マークが付くべき実際のグリフはさらに1列前にある。
    if (impl_->cellAt(impl_->cursorRow, prevCol).continuation && prevCol > 0) prevCol--;
    Cell &target = impl_->cellAt(impl_->cursorRow, prevCol);
    // 2026-08-20の追従(スクリーンショットを比較した直接のユーザー
    // 報告: "=dsよりはマシだが=dvには負ける...自前描画なら何とかなる？"
    // ――-c自身の完璧な結果と並べると、依然として見た目にはあまり
    // 融合しきっていない): この修正の「最初の」版は、単にここで
    // マークの生のUTF-8バイトを追加するだけで、セルのグリフを、
    // TTF_RenderUTF8_Blended()がシェーピングすべき、依然として分解
    // されたままの「2」コードポイントの文字列として残していた――
    // だがSDL_ttf/FreeTypeは単純な線形グリフレイアウトを行うだけで、
    // 本当のUnicodeテキストシェーピングは行わない(-cの本物の端末
    // エミュレータのシェーピングエンジンが既にやっているような
    // GPOSマーク付加位置調整は無い)ため、マーク自身の送り幅/位置が
    // 基底グリフにぴったり乗る保証は無い。composeDakutenCodepoint()
    // は、そのペアをまず単一のNFCコードポイントへ事前合成すること
    // (例えば「ト」+U+3099 -> 「ド」)で、シェーピングそのものを完全に
    // 回避する。そうすればセルは最終的に、通常のグリフのメトリクス
    // で描画される、完全に普通の1文字を保持することになり、結合
    // マークのシェーピングは一切関与しない。その有限で既知のテーブル
    // の外にあるペアについては、古い生の追加による挙動にフォール
    // バックする(display_width()による幅は依然として0のままだが、
    // このポート自身のテーブルが知らない合成というだけ)。
    unsigned int composed = composeDakutenCodepoint(decodeUtf8Codepoint(target.glyph), decodeUtf8Codepoint(utf8Bytes));
    if (composed) target.glyph = encodeUtf8Codepoint(composed);
    else target.glyph += utf8Bytes;
    return;
  }

  Cell &cell = impl_->cellAt(impl_->cursorRow, impl_->cursorCol);
  cell.glyph = utf8Bytes;
  cell.bold = impl_->boldActive;
  cell.underline = impl_->underlineActive;
  cell.standout = impl_->standoutActive;
  cell.italic = impl_->italicActive;
  cell.serif = impl_->serifActive;
  cell.highlight = impl_->highlightActive;
  cell.highlightCurrent = impl_->highlightCurrentActive;
  cell.fgColor = impl_->fgColorActive;
  cell.bgColor = impl_->bgColorActive;
  cell.continuation = false;

  if (width == 2 && impl_->cursorCol + 1 < impl_->columns) {
    Cell &shadow = impl_->cellAt(impl_->cursorRow, impl_->cursorCol + 1);
    shadow = Cell{};
    shadow.continuation = true;
    shadow.bold = cell.bold;
    shadow.underline = cell.underline;
    shadow.standout = cell.standout;
    shadow.italic = cell.italic;
    shadow.serif = cell.serif;
    shadow.highlight = cell.highlight;
    shadow.highlightCurrent = cell.highlightCurrent;
    shadow.fgColor = cell.fgColor;
    shadow.bgColor = cell.bgColor;
  }
  impl_->cursorCol += std::max(width, 1);
}

// このセルの背景の矩形だけを描く――下のグリフ描画から分離してある
// (そしてflush()内で、どのグリフ描画パスよりも前に、全セルにわたる
// それ自身の完全なパスとして実行される)ことで、ワイドグリフの
// 「2番目」(継続)のセルが、その左のセルからのグリフが既にそこへ
// 描画された「後」に自分の背景を塗ってしまうことが決して無いように
// する。以前はこの2つが単一のパスで列ごとに交互に行われており、
// それは: 基準セルのグリフが両方の列にまたがって描画される(設計通り、
// fitGlyphRect()のmaxW = cellWidth*2経由)――ところがその直後の
// renderCell()の呼び出し(継続セル用)が、返る前に無条件で「自分の」
// 列に背景の矩形を塗ってしまい、たった今描いたばかりのグリフの右半分
// を黙って消していた、ということを意味していた。これはまさに報告
// されていた「半分欠けた」/潰れたCJK文字(全ての倍幅グリフがその右
// 半分を欠いている)そのものに見え、display_width()の修正とフォント
// 選択の修正の両方を生き延びていた、なぜならそれはどちらとも無関係な、
// 純粋な描画「順序」のバグだったから――GraphicalDisplay::
// renderScrolled()(ドラッグ/慣性スクロール中にのみ使われ、セルごとで
// はなくフレームごとに画面「全体」の背景を一度だけ塗るためこのバグの
// 影響を受けない)が同一の内容を正しくレンダリングしていることに気づく
// ことで発見された、マウスをクリックして押さえている間は文字化けが
// 「直る」という実際の報告に基づく。
void GraphicalDisplay::setContentOffsetX(int pixels, int rowCount) {
  impl_->contentOffsetX = pixels;
  impl_->contentOffsetRows = rowCount;
}

void GraphicalDisplay::renderCellBackground(int row, int col) {
  if (!impl_->outputEnabled) return;
  const Cell &cell = impl_->cellAt(row, col);
  SDL_Rect box{Impl::kMarginX + col * impl_->cellWidth + impl_->offsetXForRow(row),
               Impl::kMarginY + row * impl_->cellHeight, impl_->cellWidth, impl_->cellHeight};
  // 2026-09-26: ANSIの色(Cell::fgColor/bgColor)を反映。反転表示なら入れ替える。
  SDL_Color bg = cell.bgColor != Display::kDefaultColor ? ansiColorToSdl(cell.bgColor) : impl_->bg;
  if (cell.standout) bg = cell.fgColor != Display::kDefaultColor ? ansiColorToSdl(cell.fgColor) : impl_->fg;
  // kHighlightCurrentBgColor自身のコメント参照――standoutの結果を
  // 上書きする(この用途では、たまたまカーソル/マークの反転表示と
  // 重なっても、マッチの背景ハイライトの方を優先して常に見える
  // ようにしたい)。highlight/highlightCurrentは互いに排他。
  if (cell.highlightCurrent) bg = kHighlightCurrentBgColor;
  else if (cell.highlight) bg = kHighlightBgColor;
  SDL_SetRenderDrawColor(impl_->renderer, bg.r, bg.g, bg.b, 255);
  SDL_RenderFillRect(impl_->renderer, &box);
}

void GraphicalDisplay::renderCell(int row, int col) {
  if (!impl_->outputEnabled) return;
  const Cell &cell = impl_->cellAt(row, col);
  SDL_Rect box{Impl::kMarginX + col * impl_->cellWidth + impl_->offsetXForRow(row),
               Impl::kMarginY + row * impl_->cellHeight, impl_->cellWidth, impl_->cellHeight};

  SDL_Color fg = cell.fgColor != Display::kDefaultColor ? ansiColorToSdl(cell.fgColor) : impl_->fg;
  SDL_Color bg = cell.bgColor != Display::kDefaultColor ? ansiColorToSdl(cell.bgColor) : impl_->bg;
  if (cell.standout) std::swap(fg, bg);
  // kHighlightCurrentBgColor自身のコメント参照(2026-09-10改訂: 前景色
  // だけの初版から、背景色+コントラストの取れた前景色のペアへ。
  // 2026-09-19追記: 現在位置の一致(highlightCurrent)だけがこのfg+bg
  // ペアを使い、他の全ての一致(highlight)は下のbgのみ)――standout
  // によるfg/bg入れ替えの「後」に両方まとめて上書きすることで、
  // marked行のUnderline/Standoutと共存していても常に一目で分かる
  // (renderCellBackground()が既に同じ背景色をこのセルへ塗っている
  // ので、ここでのbg上書きはグリフ描画のフォールバック/計算上の
  // 一貫性のためだけ――実際に画面へ効くのはfgの方)。
  if (cell.highlightCurrent) {
    fg = kHighlightCurrentFgColor;
    bg = kHighlightCurrentBgColor;
  } else if (cell.highlight) {
    bg = kHighlightBgColor;
  }

  // 空白セル(空/スペースのグリフ)は、下線が付いている場合は、それ
  // でも下の本物のTTF_RenderUTF8_Blended()呼び出しへフォールスルー
  // する必要がある――SDL_ttfは、そのグリフに何か目に見えるインクが
  // あるかどうかとは無関係に、グリフ自身の送り幅にわたって下線の
  // 装飾を描くため、TTF_STYLE_UNDERLINEを設定して" "を描画すると、
  // 本当に下線のストロークだけを含むサーフェスが生成される。この
  // 早期returnを、下線付きの空白「だけ」スキップする(例えば太字の
  // 場合はスキップしない、見えないインクを太字にする意味は無いため)
  // ことで、元々の性能上の意図(ほとんどの空白セルは描画すべきものが
  // 全く無い)を保ちつつ、実際に見つかった隙間を修正する: 実際の報告+
  // スクリーンショット(=ci)により、DirectoryMode自身の下線付き
  // エラーメッセージ(ModeLineFields::nameUnderlined)中の"Invalid"/
  // "mode"の間のスペースの下に、-g特有で下線が「無い」ことが判明し、
  // 本来1本の連続した線として読めるはずのものを分断していた――属性
  // に関わらず「あらゆる」空白セルに対して無条件だったこの早期return
  // が、まさに下のunderlineスタイルのコードへ到達する前に、それを
  // 黙って落としていた。
  bool blank = cell.glyph.empty() || cell.glyph == " ";
  // 2026-09-29追加、直接のユーザー報告("アンダーラインが細切れ"): 縦書きの下線
  // (リンク)は字ごとの横線ではなく、字の左の行間のセルにセルの高さいっぱいの縦線
  // (傍線)として描く――行間のセル自身が、左隣の字の下線を見て描く(同日訂正、直接の
  // ユーザー指定"Kindle、Kinoppy、ブックは皆、日本語は右側...メジャーな側に合わせて":
  // 字の右側。行間の桁の位置はepub_vertical.hppのrenderEpubVerticalWindow()参照)。
  bool underline = cell.underline && !impl_->verticalText;
  auto underlinedGlyphAt = [&](int c) {
    if (c < 0) return false;
    const Cell &left = impl_->cellAt(row, c);
    if (left.continuation) return c >= 1 && impl_->cellAt(row, c - 1).underline;
    return left.underline;
  };
  if (impl_->verticalText && blank && !cell.continuation && !cell.underline && underlinedGlyphAt(col - 1)) {
    int lineW = std::max(1, impl_->cellWidth / 8);
    SDL_Rect side{box.x + (impl_->cellWidth - lineW) / 2, box.y, lineW, impl_->cellHeight};
    SDL_SetRenderDrawColor(impl_->renderer, fg.r, fg.g, fg.b, fg.a);
    SDL_RenderFillRect(impl_->renderer, &side);
    return;
  }
  if (cell.continuation || (blank && !underline)) return;

  // このグリフが1セル分か2セル分か(CJK/全角文字、commitGlyph()の
  // 継続セルの規約参照)。
  bool isWide = (col + 1 < impl_->columns) && impl_->cellAt(row, col + 1).continuation;

  int style = TTF_STYLE_NORMAL;
  if (cell.bold) style |= TTF_STYLE_BOLD;
  if (underline) style |= TTF_STYLE_UNDERLINE;
  if (cell.italic) style |= TTF_STYLE_ITALIC;

  // メインフォントに欠けているグリフについては、CJK対応フォント
  // (loadCjkFallbackFont()自身のコメント参照)へフォールバックする
  // ――これが無いと、メインフォントに無い文字はどれも、代わりに空の
  // 「豆腐」プレースホルダーの箱として描画されてしまう(実際の報告
  // から確認済み: 日本語のファイル名が箱の連続として描画される)。
  //
  // 特に「ワイド」セルについては、メイン(ラテン等幅)フォントが
  // たまたまグリフを「も」提供している場合でも、フォールバックの方が
  // 優先される: 例えばMenloは、U+25BD(▽)と曲線的な引用符U+201C/
  // U+201Dについて自身の狭いグリフを同梱している――これはまさに、
  // fm::Utf8::display_width()がCJKの文脈では2列幅として分類する
  // 「曖昧な幅」の文字である(理由についてはCLAUDE.mdの2026-07-30の
  // 記述参照)。周囲のあらゆるセルが既に倍幅として扱っているスロット
  // に、Menloの狭く比率の異なるグリフを描画すると、CJKの隣人たちと
  // 見た目に一貫性が無くなる――TTF_GlyphIsProvided()経由で、Menlo.ttc
  // が本当にこの3つのコードポイントを持つと主張していることを確認
  // 済みで、実際の報告にあった(項目72以降も続く)まさにこれらの文字
  // まわりの文字化けはここに由来する。
  unsigned int codepoint = decodeUtf8Codepoint(cell.glyph);
  // Display::setVerticalTextHint()参照。2026-09-28改訂、直接のユーザー報告("ハイフンが
  // 太すぎる"、"「タクシー」の「ー」が太すぎる"、その前の"細くて長すぎる"): 自前の
  // 縦線をやめ、フォント自身の「ー」「―」を90度回して描く(太さ・長さがフォントの
  // 字と揃う)。「〝」「〟」は縦書きの約物の位置(Kinoppy等の表示と見比べた、ユーザー
  // 提示のスクリーンショット): どちらも「〞」(″)の形で、開きは右上、閉じは左下。
  std::string substituteGlyph;
  double rotation = 0.0;
  unsigned int cornerMark = 0;
  if (impl_->verticalText) {
    if (codepoint == 0x301D || codepoint == 0x301F) cornerMark = codepoint;
    if (codepoint == 0xFF5C) {
      substituteGlyph = "\xE3\x83\xBC";  // ー
      rotation = 90.0;
    } else if (codepoint == 0xFE31) {
      substituteGlyph = "\xE2\x80\x95";  // ―
      rotation = 90.0;
    } else if (codepoint == 0x301D || codepoint == 0x301F) {
      substituteGlyph = "\xE3\x80\x9E";  // 〞(開きは右上、閉じは左下、下のcornerMark参照)
    }
    if (!substituteGlyph.empty()) codepoint = decodeUtf8Codepoint(substituteGlyph);
  }
  TTF_Font *renderFont = impl_->font;
  bool fallbackHasIt = glyphProvidedByFont(impl_->fallbackFont, codepoint);
  if ((isWide && fallbackHasIt) || (!glyphProvidedByFont(impl_->font, codepoint) && fallbackHasIt))
    renderFont = impl_->fallbackFont;
  if (TTF_Font *serif = cell.serif ? serifFontFor(*impl_, codepoint) : nullptr) renderFont = serif;  // 明朝体
  // 2026-08-11: TTF_HINTING_LIGHT(項目263)もフォント依存だと判明した
  // ――実際の報告により、デフォルトプロファイルの「MS Gothic」の組み
  // 合わせでは完全に直る(fontTools経由で確認済み: msgothic.ttcのgasp
  // テーブルは{24: GASP_GRIDFIT, 65535: GASP_GRIDFIT|GASP_DOGRAY}であり、
  // 報告自身の"24ptで直るが18ptでは直らない"に正確に一致するppem=24
  // という閾値)が、RedSandsの「SF Mono Regular」の組み合わせでは目に
  // 見える違いを何も生まなかった。SF Mono自身のファイル
  // (SFNSMono.ttf)にはgaspテーブルが全く無い――現代のAppleシステム
  // フォントが一般にヒント無しで出荷されるのと符合する、なぜなら
  // CoreText自身のラスタライザ(-cが実際にそれを通してレンダリング
  // しているもの)は、初期のMac OS X以来TrueTypeのグリッドフィッティング
  // を使っておらず、ポイントサイズに関わらず純粋にアンチエイリアス/
  // サブピクセル配置されたアウトラインだけに頼っているため。
  // TTF_HINTING_LIGHTは、それでもFreeTypeの自動ヒンタ(このフォントが
  // 必要とするよう設計されたことのない合成ヒントプログラム)を実行
  // してしまい、その「グリッドフィッティングが全く無い」挙動を再現
  // しない――TTF_HINTING_NONE(ヒンティングを完全にスキップし、生の
  // アウトラインをスケールしてアンチエイリアスするだけ)の方が、どの
  // フォントについても、-cが実際にテキストをラスタライズする方法に
  // 構造的により近い。元の報告の範囲に合わせ、狭い(半角)グリフだけに
  // 適用する; ワイド(CJK)グリフは通常のヒンティングを維持する。
  // (TTF_SetFontStyle()/TTF_SetFontHinting()自体は、下で呼ばれる
  // cachedGlyphTexture()の中へ移動した――それ自身のコメント参照;
  // `style`/`isWide`は、キャッシュキーがそれらを必要とするため、
  // 依然としてここで計算している。)
  int hinting = isWide ? TTF_HINTING_NORMAL : TTF_HINTING_NONE;

  // 「空」のグリフ文字列は(実際の" "スペース文字とは違い)送り幅が
  // ゼロなので、上の空白セルの早期returnが下線付きセルについて緩和
  // された後でも、TTF_RenderUTF8_Blended()は下線の装飾がまたがる対象
  // が何も無い、幅ゼロのサーフェスを生成してしまう――下線を描く対象
  // となる実際のセル幅分の走りができるよう、本物のスペースに置き
  // 換える。
  // 横倒し(epub_vertical.hppのlayoutEpubVerticalColumns()のsideways参照、2026-09-29):
  // 縦書きで、同じ桁に上下2マス以上続く半角2字のマス(行間の右、桁x/x+1)は、全部を
  // 1つの文字列として90度回し、その並びの上から詰めて描く(2026-09-29改訂、直接の
  // ユーザー報告"文字間隔が不揃い"――以前はマスごとに2字ずつ回して中央に置いたため、
  // マスの中とマスの間で字の間隔が違っていた)。並びの一番上のマスがまとめて描き、
  // 他のマスは何もしない(背景は全マス描いた後に字を描くので上書きされない)。
  // 1マスだけなら縦中横(立てたまま、下の通常の描画)。
  if (impl_->verticalText) {
    // 2026-09-29訂正、直接のユーザー報告("閉じブラケットが行方不明。CUT一覧ではバッチリ"
    // =ho/=hp): マスの位置(列の左の桁)を中身(左隣が空いているか)から推し量っていたため、
    // 左隣が空の列(章の頭を揃える空の列等)だと1桁ずれた所を2字のマスと取り違え、":"
    // "]"を含む行が消えていた。縦書きの列の桁は決まっている(epubVerticalScreenColumn())
    // ので、それで見る。行末の空白は描く前に切り詰められて空のセルになるので、空も
    // 空白として扱う。
    auto columnStart = [&](int c) {
      int k = epubVerticalColumnAt(c, impl_->columns);
      return k >= 0 && epubVerticalScreenColumn(k, impl_->columns) == c;
    };
    auto halfAt = [&](int r, int c) {  // 半角1字(空白・空を含む)
      if (r < 0 || r >= impl_->rows || c < 0 || c >= impl_->columns) return false;
      const Cell &o = impl_->cellAt(r, c);
      return !o.continuation && (o.glyph.empty() || (o.glyph.size() == 1 && o.glyph[0] >= 0x20 && o.glyph[0] < 0x7F));
    };
    auto blank = [&](int r, int c) {
      const std::string &g = impl_->cellAt(r, c).glyph;
      return g.empty() || g == " ";
    };
    auto pairAt = [&](int r, int c) {
      return columnStart(c) && halfAt(r, c) && halfAt(r, c + 1) && (!blank(r, c) || !blank(r, c + 1));
    };
    int runColumn = pairAt(row, col) ? col : pairAt(row, col - 1) ? col - 1 : -1;
    if (runColumn >= 0) {
      int top = row, bottom = row;
      while (pairAt(top - 1, runColumn)) top--;
      while (pairAt(bottom + 1, runColumn)) bottom++;
      if (bottom > top) {
        if (row != top || col != runColumn) return;
        std::string run;
        for (int r = top; r <= bottom; r++)
          for (int c : {runColumn, runColumn + 1}) {
            const std::string &g = impl_->cellAt(r, c).glyph;
            run += g.empty() ? std::string(" ") : g;
          }
        while (!run.empty() && run.back() == ' ') run.pop_back();
        int pw = 0, ph = 0;
        SDL_Texture *runTexture = cachedGlyphTexture(*impl_, renderFont, run, style, hinting, fg, true, pw, ph);
        if (!runTexture) return;
        int boxW = impl_->cellWidth * 2, boxH = impl_->cellHeight * (bottom - top + 1);
        // 回した後の幅はph、高さはpw
        double scale =
            std::min({1.0, static_cast<double>(boxW) / std::max(ph, 1), static_cast<double>(boxH) / std::max(pw, 1)});
        int dw = static_cast<int>(pw * scale), dh = static_cast<int>(ph * scale);
        int centerX = box.x + boxW / 2, centerY = box.y + dw / 2;  // 上から詰める
        SDL_Rect dst{centerX - dw / 2, centerY - dh / 2, dw, dh};
        SDL_RenderCopyEx(impl_->renderer, runTexture, nullptr, &dst, 90.0, nullptr, SDL_FLIP_NONE);
        return;
      }
    }
  }
  const std::string &renderGlyph =
      !substituteGlyph.empty() ? substituteGlyph : cell.glyph.empty() ? std::string(" ") : cell.glyph;
  int w = 0, h = 0;
  SDL_Texture *texture = cachedGlyphTexture(*impl_, renderFont, renderGlyph, style, hinting, fg, !isWide, w, h);
  if (texture) {
    // fitGlyphRect()経由でセルグリッド(isWide ? 2 : 1セル)に合わせる
    // ――なぜこれがフォールバックフォントの(不一致な)本来のサイズへ
    // 引き伸ばすのではなく、縮小して中央寄せするのかについてはそれ
    // 自身のコメント参照。
    int maxW = isWide ? impl_->cellWidth * 2 : impl_->cellWidth;
    SDL_Rect dst = fitGlyphRect(box.x, box.y, maxW, impl_->cellHeight, w, h, cell.italic || cell.bold);
    if (impl_->verticalText) {
      // Display::setVerticalTextHint()参照。
      auto blankAt = [&](int c) {
        if (c < 0 || c >= impl_->columns) return true;
        const Cell &other = impl_->cellAt(row, c);
        return !other.continuation && (other.glyph.empty() || other.glyph == " ");
      };
      static const std::set<unsigned int> kSmallKana = {
          0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E, 0x3095, 0x3096,
          0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE, 0x30F5, 0x30F6};
      // 〝〟は回さない(2026-09-28、一度回したが直接のユーザー指定"〝〟は回転させずに
      // 「〝」「〟」をそのまま入ればいい"で戻した)
      static const std::set<unsigned int> kRotated = {0xFF1A, 0xFF1B, 0xFF1D, 0x301C, 0xFF5E, 0x2212, 0xFF0D,
                                                      ':',    ';',    '=',    '-',    '~'};
      if (kSmallKana.count(codepoint)) {
        dst.x += maxW / 8;  // 小書きの仮名は右上へ
        dst.y -= impl_->cellHeight / 8;
      } else if (!isWide && codepoint > 0x20 && codepoint < 0x7F && blankAt(col + 1) && blankAt(col - 1)) {
        dst.x += impl_->cellWidth / 2;  // 1字だけの半角は2セルの中央へ(縦中横の2字はそのまま)
      }
      // 2026-09-29修正、直接のユーザー報告(iBooksのスクリーンショット=hh): 〞を丸ごと
      // ずらすと、フォントの字の中での点の位置次第で隣の列へはみ出していた。点そのもの
      // (インクの範囲)だけを切り出し、開きはマスの右上、閉じは左下に置く。
      int minx = 0, maxx = 0, miny = 0, maxy = 0;
      if (cornerMark && TTF_GlyphMetrics32(renderFont, codepoint, &minx, &maxx, &miny, &maxy, nullptr) == 0 &&
          maxx > minx && maxy > miny) {
        int ascent = TTF_FontAscent(renderFont);
        SDL_Rect src{std::max(0, minx), std::max(0, ascent - maxy), maxx - minx, maxy - miny};
        src.w = std::min(src.w, w - src.x);
        src.h = std::min(src.h, h - src.y);
        double scale = static_cast<double>(dst.w) / std::max(w, 1);
        int inkW = static_cast<int>(src.w * scale), inkH = static_cast<int>(src.h * scale);
        int margin = impl_->cellHeight / 10;
        // 2026-09-29追記、直接のユーザー報告("「〟」の向きが上下逆じゃない？ 上下とも
        // 離れすぎ"、スクリーンショット=hi): 閉じは180度回し、上下の位置は字の側へ寄せる
        // (開きはマスの下寄り=続く字の側、閉じは上寄り=前の字の側。左右は右/左のまま)。
        bool open = cornerMark == 0x301D;
        SDL_Rect ink{open ? box.x + maxW - inkW - margin : box.x + margin,
                     open ? box.y + impl_->cellHeight - inkH - margin : box.y + margin, inkW, inkH};
        SDL_RenderCopyEx(impl_->renderer, texture, &src, &ink, open ? 0.0 : 180.0, nullptr, SDL_FLIP_NONE);
        return;
      }
      if (kRotated.count(codepoint)) rotation = 90.0;
      if (rotation != 0.0) {
        SDL_RenderCopyEx(impl_->renderer, texture, nullptr, &dst, rotation, nullptr, SDL_FLIP_NONE);
        return;
      }
    }
    SDL_RenderCopy(impl_->renderer, texture, nullptr, &dst);
  }
}

// (impl_->visibleCursorRow, impl_->visibleCursorCol)に、目に見える
// テキストカーソルを描く――setVisibleCursor()が設定する「論理的」な
// カーソル位置であり、impl_->cursorRow/cursorColでは「ない」(こちら
// は純粋にputByte()自身の「次のグリフはどこへ行くか」という記録で
// あり、あらゆる副次的な書き込みによって動き回る――なぜこの2つを
// 分離しなければならなかったのかの全ての経緯についてはDisplay::
// setVisibleCursor()自身のコメント参照)。
//
// このメソッドの最初の版(2026-08-04)は、-cの本物のハードウェア
// カーソルは既に、最後のmoveCursor()呼び出しが残した場所に目に見える
// 形で乗っているのだから、-gもそれに合わせるだけでよいという理屈で、
// impl_->cursorRow/cursorColを直接読んでいた。それは-c自身のイベント
// 駆動の実行ループ(1回のイテレーションにつき1つの入力が処理され、
// その後は他の何も画面に触れない)については成り立ったが、-gについて
// は成り立たなかった: runGuiPhase()は一定の約60Hzでポーリングして
// おり、Shellの^P/^N履歴ブラウズのような呼び出し元がカーソルを特定の
// 行へ明示的に動かしたのと「同じ」フレーム内で、その末尾が無条件に
// ContentView::redraw()を丸ごと再実行する(その内部の行ごと/モード
// ライン用のmoveCursor()呼び出しは純粋なグリフ配置であり、モードライン
// のテキストが最後に終わった場所に着地する)――1フレーム後には黙って
// 「カーソル」を最下行へ引き戻してしまう。実際の2026-08-04のユーザー
// 報告はまさにこれを説明していた: "-gのSHELLではプロンプトと同じく
// 一瞬出て消えるパターン...モードラインの行数は追随するも位置は
// 最下行のまま"。setVisibleCursor()だけが触れる「別個の」状態を読む
// ことは、特定のフレームでの呼び出し順序に関わらず、副次的な再描画が
// それを物理的に動かせないことを意味する――ユーザー自身の過去のGUI
// 作業(cb)から説明された「メインの再描画から切り離された、それ自身の
// ルーチンとして保たれるXORブロックカーソル」というのと同じ発想を、
// 本物のピクセルXORを必要とする代わりに、このポート自身が正本とする
// Cellグリッドを再利用するよう適合させたもの: カーソルの消去/再描画は
// 単にそのセルをCellの状態から普通に再レンダリングするだけであり、
// 文字通りのラスターXORのトグルはそれを近似していたに過ぎない。
//
// (実際の端末のデフォルトのブロックカーソルの見た目に合わせて)前景色
// の塗りつぶしたブロックとして描画し、そのセルに既に載っているどんな
// グリフも背景色で上に重ねて再描画することで、カーソルの下のテキスト
// が隠されるのではなく読めるままになる――これはCell::standout/
// renderCell()が反転表示に既に使っているのと同じ発想であり、この1
// セルについてだけ、その自身のstandout状態に関わらず強制的に有効に
// しているだけ(モードライン自身の既に反転表示済みのテキストの上に
// 乗っている時でさえ、カーソルは常に「カーソル」として読めるべき
// である)。
void GraphicalDisplay::renderCursor() {
  if (!impl_->outputEnabled) return;
  if (!impl_->hasVisibleCursor) return;
  int row = impl_->visibleCursorRow;
  int col = impl_->visibleCursorCol;
  if (row < 0 || row >= impl_->rows || col < 0 || col >= impl_->columns) return;

  // カーソルがワイド(CJK)グリフの「シャドウ」の2番目のセルに着地
  // した場合、代わりにグリフの実際の基準セルの上に、両方の列にまた
  // がるよう描く――実際の端末のブロックカーソルが、半分に切り取った
  // ように見えるのではなく、倍幅文字全体を覆うのに合わせている。
  int homeCol = col;
  if (impl_->cellAt(row, col).continuation && col > 0) homeCol = col - 1;
  bool isWide = (homeCol + 1 < impl_->columns) && impl_->cellAt(row, homeCol + 1).continuation;

  SDL_Rect box{Impl::kMarginX + homeCol * impl_->cellWidth + impl_->offsetXForRow(row), Impl::kMarginY + row * impl_->cellHeight,
               impl_->cellWidth * (isWide ? 2 : 1), impl_->cellHeight};
  SDL_SetRenderDrawColor(impl_->renderer, impl_->fg.r, impl_->fg.g, impl_->fg.b, 255);
  SDL_RenderFillRect(impl_->renderer, &box);

  const Cell &cell = impl_->cellAt(row, homeCol);
  if (cell.glyph.empty() || cell.glyph == " ") return;

  int style = TTF_STYLE_NORMAL;
  if (cell.bold) style |= TTF_STYLE_BOLD;
  if (cell.underline) style |= TTF_STYLE_UNDERLINE;
  if (cell.italic) style |= TTF_STYLE_ITALIC;

  // renderCell()と同じメイン/フォールバックフォントのグリフ有無の
  // 判定――それ自身のコメント参照。
  unsigned int codepoint = decodeUtf8Codepoint(cell.glyph);
  TTF_Font *renderFont = impl_->font;
  bool fallbackHasIt = glyphProvidedByFont(impl_->fallbackFont, codepoint);
  if ((isWide && fallbackHasIt) || (!glyphProvidedByFont(impl_->font, codepoint) && fallbackHasIt))
    renderFont = impl_->fallbackFont;
  if (TTF_Font *serif = cell.serif ? serifFontFor(*impl_, codepoint) : nullptr) renderFont = serif;  // 明朝体
  // renderCell()と同じ半角用ヒンティングの調整――それ自身のコメント
  // 参照。(TTF_SetFontStyle()/TTF_SetFontHinting()自体は、下で呼ばれる
  // cachedGlyphTexture()の中へ移動した――それ自身のコメント参照。)
  int hinting = isWide ? TTF_HINTING_NORMAL : TTF_HINTING_NONE;

  int w = 0, h = 0;
  SDL_Texture *texture = cachedGlyphTexture(*impl_, renderFont, cell.glyph, style, hinting, impl_->bg, !isWide, w, h);
  if (texture) {
    int maxW = isWide ? impl_->cellWidth * 2 : impl_->cellWidth;
    SDL_Rect dst = fitGlyphRect(box.x, box.y, maxW, impl_->cellHeight, w, h, cell.italic || cell.bold);
    SDL_RenderCopy(impl_->renderer, texture, nullptr, &dst);
  }
}

void GraphicalDisplay::flush() {
  if (!impl_->outputEnabled || impl_->edgeCapturing) return;
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) {
      std::fprintf(f, "---flush---\n");
      for (int row = 0; row < impl_->rows; row++) {
        std::string line;
        for (int col = 0; col < impl_->columns; col++) {
          const Cell &cell = impl_->cellAt(row, col);
          if (cell.continuation) line += "_";
          else if (cell.glyph.empty()) line += ".";
          else line += "[" + cell.glyph + "]";
        }
        std::fprintf(f, "%d: %s\n", row, line.c_str());
      }
      std::fclose(f);
    }
  }
  SDL_SetRenderDrawColor(impl_->renderer, impl_->bg.r, impl_->bg.g, impl_->bg.b, 255);
  SDL_RenderClear(impl_->renderer);
  for (int row = 0; row < impl_->rows; row++)
    for (int col = 0; col < impl_->columns; col++) renderCellBackground(row, col);
  for (int row = 0; row < impl_->rows; row++)
    for (int col = 0; col < impl_->columns; col++) renderCell(row, col);
  renderContentEdge();

  // 最初の実際のマウス操作までは静的な参照画像、それ以降は実際の
  // 常に最新のつまみ――Impl::everInteractedそれ自身のコメント参照。
  // 本物のつまみの経路が実行される時、effectiveTopはここでは単なる
  // intである(renderScrolled()自身のドラッグ/慣性経路の外では、行内
  // の副ピクセルオフセットは働いていない)、renderScrolled()自身の
  // 小数のeffectiveTopとは異なり。
  //
  // 2026-09-02、直接のユーザー報告(スクリーンショット=fd、"-g起動でも
  // 〔スクロールバー張りぼて画像が〕ズレるみたい")、および同日の訂正
  // ("スクロールバーの幅は関係ない。なぜなら右端に表示されるから。
  // なので張りぼても右端に表示されればOK")を受けて、一度は「現在の
  // scrollbarWidthが画像キャプチャ時の17pxと一致する場合だけ描画する」
  // というゲートをここに置いたが、これは誤った対処だった――
  // scrollbarWidthは2026-08-30以降cellHeightへ連動しており(Impl::
  // scrollbarWidth自身のコメント参照)、実機のフォントサイズがちょうど
  // cellHeight=17pxになることは実質無い(USILICGUIDEBUGでの実機ログ、
  // 2026-09-02、MS-Gothic 24pt→cellHeight 24〜25で確認)ため、この
  // ゲートは装飾画像そのものを事実上常に無効化してしまっていた
  // ("張りぼて出ず"というユーザー報告はこのゲートが原因)。
  //
  // 本当の原因はscrollbarWidthとの不一致ではなく、drawIdleScrollbarImage()
  // 自身のstripX計算がscrollbarStripLeft()(windowWidth-scrollbarWidth、
  // つまり「予約されている帯の左端」)を使っていたこと――画像自体は
  // 常に固定17px幅(kScrollbarIdleTop/Middle/BottomWidth)しかないのに、
  // cellHeight連動で帯自体が17pxより広くなると、画像は帯の左端に
  // 貼られたまま右端まで届かず、`scrollbarWidth-17`px分だけ本物の
  // ウィンドウ右端から左にズレて見える――これが「横方向に5mmほど
  // ズレている」の正体(下記drawIdleScrollbarImage()の修正参照:
  // 画像自身の実際の幅で測った右端フラッシュへ変更)。ここのゲートは
  // 不要なので撤去し、everInteractedだけで切り替える元の設計へ戻す。
  // 2026-09-25追加、直接のユーザー要望("-g起動ではスクロールバーが
  // フルの時は張りぼて復活"): 内容が1画面に収まっている(つまみが全長に
  // なる)間は、一度操作した後でも張りぼて画像へ戻す。
  if (impl_->scrollbarHidden) {
    // setScrollbarHidden()参照
  } else if (!impl_->everInteracted || impl_->scrollTotalLines <= impl_->scrollContentRows) {
    drawIdleScrollbarImage();
  } else {
    drawScrollbar(static_cast<double>(impl_->scrollTopIndex), impl_->scrollTotalLines, impl_->scrollContentRows);
  }

  renderCursor();
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    // 上のグリッドダンプと同じ環境変数: 実際にレンダリングされた
    // ピクセルも(スクリーンキャプチャではなく、レンダラー自身から
    // 読み戻す――Screen Recording権限も実際に見えるウィンドウも無く
    // 動く)"<value>.bmp"として保存し、flush()のたびに上書きするので
    // 常に最新のフレームを保持する。これがあったからこそ、実際の描画
    // バグ(renderCellBackground()の継続セルの塗りつぶしがワイドグリフ
    // の右半分を消していた件、それ自身のコメント参照)を、そもそも
    // このサンドボックス内から確認できた、screencapture自体がここでは
    // 利用できなかったため。
    // 論理的なセルグリッドではなく、レンダラー自身の「出力」(物理
    // ピクセル、SDL_WINDOW_ALLOW_HIGHDPI以降は
    // columns*cellWidth/rows*cellHeightの論理的な「ポイント」より
    // 大きくなり得る――create()自身のコメント参照)からサイズを決めて
    // いる: SDL_RenderReadPixels()は実際の描画領域を読むので、より
    // 小さい論理的な寸法向けにサイズを決めた出力先サーフェスは、
    // HiDPIディスプレイ上ではその自身のpitchに対してオーバーフロー/
    // 切り詰めが起きてしまう。
    int outputWidthPixels = 0, outputHeightPixels = 0;
    SDL_GetRendererOutputSize(impl_->renderer, &outputWidthPixels, &outputHeightPixels);
    SDL_Surface *shot =
        SDL_CreateRGBSurfaceWithFormat(0, outputWidthPixels, outputHeightPixels, 32, SDL_PIXELFORMAT_RGBA32);
    if (shot) {
      if (SDL_RenderReadPixels(impl_->renderer, nullptr, SDL_PIXELFORMAT_RGBA32, shot->pixels, shot->pitch) == 0)
        SDL_SaveBMP(shot, (std::string(dbg) + ".bmp").c_str());
      SDL_FreeSurface(shot);
    }
  }
  SDL_RenderPresent(impl_->renderer);
}

void GraphicalDisplay::moveCursor(int row, int col) {
  // 防御的なリセット(2026-08-21、実際の報告がこれの原因だと突き止めた
  // ContentView::renderLine()の修正と併せて――それ自身のwidthAware
  // コメント参照): pendingUtf8内の組み立て途中のバイト列は、新しい
  // 位置へのカーソルジャンプを絶対に生き延びてはならない。これが無い
  // と、マルチバイトシーケンスの途中で打ち切る他の呼び出し元(属性付き
  // のCJK行についてrenderLine()の古いバイト数カウント方式のループが
  // かつてそうしていたように)が、ここに余ったバイトを残してしまい、
  // それが新しい位置で書かれる「最初の」バイトへ黙って継ぎ足され、
  // 実際に放棄されたはずの不完全な文字を単に捨てる代わりに、そこの
  // 1つのグリフを壊してしまう。
  impl_->pendingUtf8.clear();
  impl_->cursorRow = row;
  impl_->cursorCol = col;
}

void GraphicalDisplay::setVisibleCursor(int row, int col) {
  impl_->hasVisibleCursor = true;
  impl_->visibleCursorRow = row;
  impl_->visibleCursorCol = col;
}

void GraphicalDisplay::hideVisibleCursor() { impl_->hasVisibleCursor = false; }

void GraphicalDisplay::cursorLeft() {
  if (impl_->cursorCol > 0) impl_->cursorCol--;
}

void GraphicalDisplay::saveCursor() {
  impl_->savedRow = impl_->cursorRow;
  impl_->savedCol = impl_->cursorCol;
}

void GraphicalDisplay::restoreCursor() {
  impl_->cursorRow = impl_->savedRow;
  impl_->cursorCol = impl_->savedCol;
}

void GraphicalDisplay::clearScreen() { impl_->grid.assign(impl_->grid.size(), Cell{}); }

void GraphicalDisplay::clearToEndOfLine() {
  if (impl_->cursorRow < 0 || impl_->cursorRow >= impl_->rows) return;
  for (int col = std::max(impl_->cursorCol, 0); col < impl_->columns; col++) impl_->cellAt(impl_->cursorRow, col) = Cell{};
}

void GraphicalDisplay::scrollForward() {
  for (int row = 0; row < impl_->rows - 1; row++)
    for (int col = 0; col < impl_->columns; col++) impl_->cellAt(row, col) = impl_->cellAt(row + 1, col);
  for (int col = 0; col < impl_->columns; col++) impl_->cellAt(impl_->rows - 1, col) = Cell{};
}

void GraphicalDisplay::scrollReverse() {
  for (int row = impl_->rows - 1; row > 0; row--)
    for (int col = 0; col < impl_->columns; col++) impl_->cellAt(row, col) = impl_->cellAt(row - 1, col);
  for (int col = 0; col < impl_->columns; col++) impl_->cellAt(0, col) = Cell{};
}

void GraphicalDisplay::setFontFamilyCandidates(std::vector<std::string> serif, std::vector<std::string> sansSerif) {
  userSerifCandidates() = std::move(serif);
  userSansSerifCandidates() = std::move(sansSerif);
}

void GraphicalDisplay::setAttribute(Attribute attr) {
  switch (attr) {
    case Attribute::Underline:
      impl_->underlineActive = true;
      break;
    case Attribute::UnderlineOff:
      impl_->underlineActive = false;
      break;
    case Attribute::Bold:
      impl_->boldActive = true;
      break;
    case Attribute::BoldOff:
      impl_->boldActive = false;
      break;
    case Attribute::Standout:
      impl_->standoutActive = true;
      break;
    case Attribute::StandoutOff:
      impl_->standoutActive = false;
      break;
    case Attribute::Italic:
      impl_->italicActive = true;
      break;
    case Attribute::ItalicOff:
      impl_->italicActive = false;
      break;
    case Attribute::Serif:
      impl_->serifActive = true;
      break;
    case Attribute::SerifOff:
      impl_->serifActive = false;
      break;
    case Attribute::Highlight:
      impl_->highlightActive = true;
      break;
    case Attribute::HighlightCurrent:
      impl_->highlightCurrentActive = true;
      break;
    case Attribute::HighlightOff:
      impl_->highlightActive = false;
      impl_->highlightCurrentActive = false;
      break;
    case Attribute::Color:
      break;  // setColor()経由でのみ適用
  }
}

void GraphicalDisplay::setColor(int fg, int bg) {
  impl_->fgColorActive = fg;
  impl_->bgColorActive = bg;
}

// ピクセルでレンダリングされるウィンドウにはエスケープシーケンスという
// 概念そのものが実在しない(legacyでの唯一の用途は、生のtermcapコード
// をbashのPS1文字列へ継ぎ込んで、bash自身にそれを出力させることだった
// ――本物の端末がそれを解釈しなければ意味を成さない)ので、これは常に
// 空文字列である――文書化され、明記された欠落(GUIモードが有効な間、
// Shellのプロンプトは太字マーカーを失う)であり、黙って省略している
// わけではない。
std::string GraphicalDisplay::attributeEscape(Attribute) const { return ""; }

int GraphicalDisplay::rows() const { return impl_->rows; }
int GraphicalDisplay::columns() const { return impl_->columns; }

void GraphicalDisplay::setSize(int rows, int columns) {
  if (rows <= 0 || columns <= 0) return;
  impl_->rows = rows;
  impl_->columns = columns;
  impl_->grid.assign(static_cast<std::size_t>(rows) * columns, Cell{});
  if (impl_->window)
    SDL_SetWindowSize(impl_->window, impl_->cellWidth * columns + Impl::kMarginX * 2 + impl_->scrollbarWidth,
                       impl_->cellHeight * rows + Impl::kMarginY * 2);
}

// 全ての経緯についてはそれ自身の宣言側のコメント(graphical_display.hpp)
// 参照――上のsetSize()/matchWindowSize()の「逆」: あの2つは目標の
// rows/columnsから「出発して」ピクセルサイズへ「至る」;こちらは
// 既に変化したピクセルサイズ(ユーザーによる実際のドラッグリサイズ)
// から「出発して」、同じセルサイズのまま新しいrows/columnsへ「至る」、
// cellWidth/cellHeightやフォントには意図的に一切触れない。
void GraphicalDisplay::handleWindowResized(int newPixelWidth, int newPixelHeight) {
  if (!impl_->window || impl_->cellWidth <= 0 || impl_->cellHeight <= 0) return;
  int contentWidth = newPixelWidth - Impl::kMarginX * 2 - impl_->scrollbarWidth;
  int contentHeight = newPixelHeight - Impl::kMarginY * 2;
  int newColumns = std::max(1, contentWidth / impl_->cellWidth);
  int newRows = std::max(1, contentHeight / impl_->cellHeight);
  if (newRows == impl_->rows && newColumns == impl_->columns) return;  // 何もすることが無い
  // 2026-09-20追加、"-gで今度は103ではなく100に来る"追跡用の一時診断
  // ログ(既存のUSILICGUIDEBUG慣習、この関数の少し上syncMouseTracking
  // 等と同じ): エディタ(vi)往復のあいだのgui.setVisible(false)/(true)
  // だけで、実際にSDL_WINDOWEVENT_RESIZED経由でここへ辿り着くことが
  // あるのかどうか自体が不明――ここを通ればFileMode::reflow()の
  // ログと合わせて実際の値の流れが追える。
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) {
      std::fprintf(f, "handleWindowResized: %dx%d -> columns %d->%d rows %d->%d\n", newPixelWidth, newPixelHeight,
                   impl_->columns, newColumns, impl_->rows, newRows);
      std::fclose(f);
    }
  }
  impl_->rows = newRows;
  impl_->columns = newColumns;
  impl_->grid.assign(static_cast<std::size_t>(newRows) * newColumns, Cell{});
  impl_->sizeChanged = true;
  impl_->needsRedraw = true;  // リサイズしたばかりのグリッドは完全に空白から始まる――全画面再描画を強制する
}

bool GraphicalDisplay::consumeSizeChanged() {
  bool value = impl_->sizeChanged;
  impl_->sizeChanged = false;
  return value;
}

void GraphicalDisplay::adjustFontSize(int deltaPoints) {
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) { std::fprintf(f, "adjustFontSize(%d) called, current=%d base=%d\n", deltaPoints, impl_->currentFontPointSize, impl_->baseFontPointSize); std::fclose(f); }
  }
  if (!impl_->window || !impl_->font) return;
  // なぜ0が「no-op」ではなく「リセット」を意味するのかについては
  // それ自身の宣言側のコメント(graphical_display.hpp)参照: Cmd+0は
  // リセットのジェスチャであり、3つのキーの組(Cmd+=/Cmd+-/Cmd+0)が
  // 1つのクランプ+再計測の実装を共有し続けられるよう、別のメソッドに
  // せずここに紐付けてある。
  int newPointSize = (deltaPoints == 0) ? impl_->baseFontPointSize
                                         : std::clamp(impl_->currentFontPointSize + deltaPoints, 6, 72);
  if (newPointSize == impl_->currentFontPointSize) return;
  TTF_SetFontSize(impl_->font, newPointSize);
  if (impl_->fallbackFont) TTF_SetFontSize(impl_->fallbackFont, newPointSize);
  if (impl_->serifFont) TTF_SetFontSize(impl_->serifFont, newPointSize);
  // 今リサイズされたばかりのフォントから再計測する、create()自身の
  // 「M」+CJKフォールバックグリフの計測に倣う(両方が必要な理由に
  // ついてはそれ自身のコメント参照)――TTF_SetFontSize()はフォント
  // 自身のグリフのメトリクスを変えるので、「旧」cellWidth/cellHeight
  // はもうそれを説明しない。
  int cellWidth = 0, cellHeight = 0;
  if (TTF_SizeUTF8(impl_->font, "M", &cellWidth, &cellHeight) != 0 || cellWidth <= 0 || cellHeight <= 0) {
    // フォントとimpl_->cellWidth/cellHeightがポイントサイズについて
    // 食い違ったままにするのではなく、巻き戻す。
    TTF_SetFontSize(impl_->font, impl_->currentFontPointSize);
    if (impl_->fallbackFont) TTF_SetFontSize(impl_->fallbackFont, impl_->currentFontPointSize);
    if (impl_->serifFont) TTF_SetFontSize(impl_->serifFont, impl_->currentFontPointSize);
    return;
  }
  cellHeight = std::max(cellHeight, TTF_FontHeight(impl_->font));
  if (impl_->fallbackFont) {
    int cjkWidth = 0, cjkHeight = 0;
    if (TTF_SizeUTF8(impl_->fallbackFont, "\xE5\x9B\xBD" /* "国" */, &cjkWidth, &cjkHeight) == 0) {
      cellWidth = std::max(cellWidth, (cjkWidth + 1) / 2);
      cellHeight = std::max(cellHeight, cjkHeight);
    }
  }
  impl_->currentFontPointSize = newPointSize;
  impl_->cellWidth = cellWidth;
  impl_->cellHeight = cellHeight;
  impl_->scrollbarWidth = cellHeight;  // Impl::scrollbarWidth自身のコメント参照
  // フォントオブジェクトは(上のTTF_SetFontSize()により)たった今
  // その場でリサイズされた――キャッシュされているテクスチャは全て
  // 今やそれに対して「間違った」ピクセルサイズになっている。
  // clearGlyphCache()自身のコメント参照。
  clearGlyphCache(*impl_);
  // setSize()が既に使っているのと同じウィンドウサイズの計算式:
  // rows/columnsは固定のまま、ピクセルサイズだけが変わる。この
  // SDL_SetWindowSize()呼び出し自体がSDL_WINDOWEVENT_RESIZEDを再度
  // 発火させるが、新しいウィンドウサイズはcellWidth/cellHeight*
  // rows/columnsからちょうど導かれているため、handleWindowResized()
  // は「同じ」rows/columnsを再計算し、それ自身の「何もすることが
  // 無い」早期returnを取る――フィードバックループにはならない。
  SDL_SetWindowSize(impl_->window, cellWidth * impl_->columns + Impl::kMarginX * 2 + impl_->scrollbarWidth,
                     cellHeight * impl_->rows + Impl::kMarginY * 2);
  SDL_SetWindowMinimumSize(impl_->window, cellWidth * 10 + Impl::kMarginX * 2 + impl_->scrollbarWidth,
                            cellHeight * 4 + Impl::kMarginY * 2);
  impl_->needsRedraw = true;
}

namespace {
// Cmd+Kのカラープリセット(2026-08-24、項目426) -- そうでなければ
// ユーザーがusilicrcを手で編集しなければならなかったであろう、明らか
// に「本物の端末のプロファイルらしい」選択肢をカバーする、小さな
// 固定の集合: このクラス自身のデフォルト(黒地に薄いグレー)、緑地に
// 黒の「CRT」風の見た目(よくある実際の端末プロファイルのプリセット、
// 例えばTerminal.app自身の「Pro」/「Homebrew」プロファイル)、黒地に
// アンバー(同様に、古いアンバー燐光モニタ風の見た目)、そして白地に
// 黒(明るい背景のプロファイル、他の3つとは逆)。設計上網羅的ではない
// ――より具体的な何かのためにはsetColors()が引き続き利用可能である
// (「実はすでにGUI」の起動時の端末色マッチは、既にこのリストを完全に
// バイパスして、任意の問い合わせた色でそれを直接呼んでいる)。
constexpr GraphicalDisplay::Color kColorPresetFg[] = {
    {220, 220, 220}, {80, 250, 80}, {255, 176, 0}, {20, 20, 20},
};
constexpr GraphicalDisplay::Color kColorPresetBg[] = {
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {245, 245, 245},
};
constexpr int kColorPresetCount = 4;
}  // namespace

void GraphicalDisplay::cycleColorPreset() {
  impl_->colorPresetIndex = (impl_->colorPresetIndex + 1) % kColorPresetCount;
  setColors(kColorPresetFg[impl_->colorPresetIndex], kColorPresetBg[impl_->colorPresetIndex]);
  impl_->needsRedraw = true;
}

std::string GraphicalDisplay::keySequence(Key) const { return ""; }

bool GraphicalDisplay::outputEnabled() const { return impl_->outputEnabled; }
void GraphicalDisplay::setOutputEnabled(bool enabled) { impl_->outputEnabled = enabled; }

bool GraphicalDisplay::smoothScrollEnabled() const { return impl_->smoothScroll; }
void GraphicalDisplay::setSmoothScrollEnabled(bool enabled) { impl_->smoothScroll = enabled; }
void GraphicalDisplay::setVerticalTextHint(bool vertical) { impl_->verticalText = vertical; }

void GraphicalDisplay::beginContentEdgeCapture() {
  Impl &d = *impl_;
  if (d.edgeCapturing) return;
  d.edgeCapturing = true;
  d.edgeSavedGrid = std::move(d.grid);
  d.grid.assign(d.edgeSavedGrid.size(), Cell{});
  d.edgeSavedRow = d.cursorRow;
  d.edgeSavedCol = d.cursorCol;
  bool *flags[7] = {&d.standoutActive, &d.boldActive,      &d.underlineActive,        &d.italicActive,
                    &d.highlightActive, &d.highlightCurrentActive, &d.serifActive};
  for (int i = 0; i < 7; i++) {
    d.edgeSavedFlags[i] = *flags[i];
    *flags[i] = false;
  }
  d.edgeSavedFg = d.fgColorActive;
  d.edgeSavedBg = d.bgColorActive;
  d.fgColorActive = d.bgColorActive = Display::kDefaultColor;
}

void GraphicalDisplay::endContentEdgeCapture(int side) {
  Impl &d = *impl_;
  if (!d.edgeCapturing) return;
  d.edgeCapturing = false;
  d.edgeGrid[side > 0 ? 0 : 1] = std::move(d.grid);
  d.grid = std::move(d.edgeSavedGrid);
  d.cursorRow = d.edgeSavedRow;
  d.cursorCol = d.edgeSavedCol;
  bool *flags[7] = {&d.standoutActive, &d.boldActive,      &d.underlineActive,        &d.italicActive,
                    &d.highlightActive, &d.highlightCurrentActive, &d.serifActive};
  for (int i = 0; i < 7; i++) *flags[i] = d.edgeSavedFlags[i];
  d.fgColorActive = d.edgeSavedFg;
  d.bgColorActive = d.edgeSavedBg;
}

// 本文を横にずらして画面の端に空いた所へ、裏の画面(beginContentEdgeCapture())の列を
// 描く。縦のスクロール(renderScrolled())が現れる側に1行余分に描くのと同じ考え方。裏の
// 画面はkEpubVerticalEdgeColumns列ずらして描いてあるので、その分(桁)だけ戻して置く。
// 左の端は、画面の左の余り(列の幅に満たない桁、本文は空)にかかる列も描く。
// 2026-09-30訂正、直接のユーザー報告("右端が変わらず。左端からは...ドット単位で現れる
// ものの右端が一度に一行消える瞬間の巻き添えで一瞬消える"): ずれている向きの片側だけ、
// ずれが0でない間だけ描いていたので、(1)先へ送る時に右の帯(スクロールバーの帯)へ
// はみ出していく右端の列が、1列送った瞬間(本文の列から外れた時)に消え、(2)1列送った
// 直後のずれが0のフレームでは左の余りにかかる列も消えていた。ドラッグ/慣性の間
// (contentOffsetRows > 0)は、ずれの向き・大きさにかかわらず両側を描く。
void GraphicalDisplay::renderContentEdge() {
  Impl &d = *impl_;
  if (!d.verticalText || d.contentOffsetRows <= 0) return;
  int pitch = epubVerticalColumnPitch();
  int shift = kEpubVerticalEdgeColumns * pitch;
  int leftover = d.columns - epubVerticalColumnsPerScreen(d.columns) * pitch;
  int rows = std::min(d.contentOffsetRows, d.rows);
  int savedOffset = d.contentOffsetX;
  for (int side = 0; side < 2; side++) {
    if (d.edgeGrid[side].size() != d.grid.size()) continue;  // リサイズ後、まだ描き直していない
    int lo = side == 0 ? 0 : std::max(0, d.columns - shift);
    int hi = side == 0 ? std::min(d.columns, shift + leftover) : d.columns;
    std::swap(d.grid, d.edgeGrid[side]);
    d.contentOffsetX = savedOffset + (side == 0 ? -shift : shift) * d.cellWidth;
    for (int row = 0; row < rows; row++)
      for (int col = lo; col < hi; col++) renderCellBackground(row, col);
    for (int row = 0; row < rows; row++)
      for (int col = lo; col < hi; col++) renderCell(row, col);
    d.contentOffsetX = savedOffset;
    std::swap(d.grid, d.edgeGrid[side]);
  }
}

bool GraphicalDisplay::modeLineVisible() const { return impl_->modeLineVisible; }
void GraphicalDisplay::setModeLineVisible(bool visible) { impl_->modeLineVisible = visible; }

// create()が非表示で作成したウィンドウを表示する(そして前面化/
// フォーカスする)――create()自身から意図的に分離してあるのは、
// ウィンドウを「先に」配置/リサイズする必要のある呼び出し元
// (setWindowPosition()/matchWindowSize()、「実はすでにGUI」オーバー
// レイの起動)が、それがまだ見えない間にそれを行い、それから既に
// 最終的な場所にある状態でそれを見せられるようにするため。以前の
// バージョンは、オーバーレイの起動元がそれを再配置/リサイズする
// 機会を得る前に、create()の内部で即座にウィンドウを表示
// (SDL_WINDOW_SHOWN)し前面化していた――実際の報告(2026-08-09:
// "横幅が狭いは解消されたものの重ね打ち画面が出る前に一旦は狭い画面
// が表示されてしまうので重ね打ちだとわかってしまう")が、これが目に
// 見えるちらつきであることを確認した: create()由来のフォントの
// メトリクスに基づくサイズの中央寄せウィンドウが一瞬画面に映り、
// それから少し後に本物の端末の位置/サイズに合わせて飛ぶ――まさに
// この機能全体が存在する目的である、避けるべき「継ぎ目」そのもの。
// 単純な`-g`と`^Og`の呼び出し元(合わせるべき本物の端末は無く、先に
// 再配置すべきものも無い)は、依然としてcreate()の直後に無条件で
// open()を呼んでいるので、それら自身の目に見える挙動はこの分離に
// よって変わらない。
//
// SDL_RaiseWindow()自体はcreate()自身の以前の呼び出しから変わって
// いない――そこにあった(今は削除された)コメント参照(依然として
// 当てはまる): これが無いと、実際の2026-08-03の報告により、単に
// 表示されただけのウィンドウは、プラットフォーム/WMによっては、
// それまでフォーカスされていたどのウィンドウからも確実には入力
// フォーカスを奪えないことが判明した。
void GraphicalDisplay::open() {
  if (!impl_->window) return;
  // 既に設定済みの背景色(setColors()、これより前にオーバーレイの
  // 起動元によって呼ばれている)でクリアし、ウィンドウを表示する
  // 「前」に一度presentする――SDL_CreateRenderer()は自分自身のバッファ
  // を暗黙にクリアすることは決して無いので、これが無いと、ウィンドウ
  // の生まれて初めての可視フレームは、本物の端末に合わせた背景では
  // なく、たまたまレンダラーのバックバッファに乗っていた未定義の
  // 何か(典型的には真っ黒だが保証は無い)になってしまう――このopen()
  // の分離全体(それ自身のコメント参照)が塞ごうとしている、まさに
  // その「正しい状態に落ち着く前に一瞬間違ったものを見せる」継ぎ目の
  // 小さな版。実際の文字グリッド自体はここでは描画されない
  // (flush()がそれを実行する; 「実はすでにGUI」の起動のこの時点では、
  // コンテンツはまだこのバックエンドへ結線すらされていない、main()
  // 自身の呼び出し箇所のコメント参照)――背景色だけであり、それは
  // 耳障りなちらつきを避けるには十分であり、しかも最初のプロンプト/
  // 一覧が描かれる瞬間の前には、本物の端末自身のアイドル時の背景も
  // どのみちそう見えるだけのものである。
  SDL_SetRenderDrawColor(impl_->renderer, impl_->bg.r, impl_->bg.g, impl_->bg.b, 255);
  SDL_RenderClear(impl_->renderer);
  SDL_RenderPresent(impl_->renderer);
  SDL_ShowWindow(impl_->window);
  syncHighDpiRenderScale(impl_->window, impl_->renderer);  // それ自身のコメント参照
  SDL_RaiseWindow(impl_->window);
}
void GraphicalDisplay::close() {}  // 代わりにデストラクタで破棄される、それ自身のコメント参照

namespace {

std::optional<Display::Key> translateScancode(SDL_Keycode key) {
  switch (key) {
    case SDLK_UP:
      return Display::Key::Up;
    case SDLK_DOWN:
      return Display::Key::Down;
    case SDLK_LEFT:
      return Display::Key::Left;
    case SDLK_RIGHT:
      return Display::Key::Right;
    case SDLK_PAGEUP:
      return Display::Key::PageUp;
    case SDLK_PAGEDOWN:
      return Display::Key::PageDown;
    default:
      return std::nullopt;
  }
}

}  // namespace

bool GraphicalDisplay::pumpEvents() {
  SDL_Event event;
  bool stillOpen = true;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT:
        stillOpen = false;
        break;
      // consumeDroppedFiles()自身の宣言コメント参照。
      case SDL_DROPBEGIN:
        impl_->dropInProgress = true;
        break;
      case SDL_DROPFILE:
        if (event.drop.file) {
          impl_->pendingDrop.emplace_back(event.drop.file);
          SDL_free(event.drop.file);
        }
        if (!impl_->dropInProgress) {  // DROPBEGIN無しの単発
          for (auto &path : impl_->pendingDrop) impl_->completedDrop.push_back(std::move(path));
          impl_->pendingDrop.clear();
        }
        break;
      case SDL_DROPCOMPLETE:
        impl_->dropInProgress = false;
        for (auto &path : impl_->pendingDrop) impl_->completedDrop.push_back(std::move(path));
        impl_->pendingDrop.clear();
        break;
      // consumeNeedsRedraw()自身の宣言側のコメント参照(2026-08-24の
      // 修正、docs/CHANGES.md項目422) -- SDL_WINDOWEVENT_EXPOSEDは
      // WindowServer自身の「このウィンドウは(再)描画される必要がある」
      // という信号である(ウィンドウが最初にマップされた後、他の
      // ウィンドウで覆われていたのが外れた後、等に発火する);
      // SDL_WINDOWEVENT_SHOWN(SDL_CreateWindow自身のSDL_WINDOW_SHOWN
      // が効果を発揮した直後に一度だけ発火する)も含まれている、
      // バンドルから起動されたアプリのウィンドウは、何かに覆われたり
      // 覆いが外れたりする機会をまだ得る前でさえ、まさに最初の描画を
      // やり直す必要があり得るため。RESIZED以外の他の全ての
      // SDL_WINDOWEVENTのサブタイプ(フォーカス、移動、...)は、ここ
      // では意図的に手を付けないままにしてある。
      //
      // SDL_WINDOWEVENT_RESIZED(2026-08-24の追加、docs/CHANGES.md
      // 項目425、直接のユーザー依頼――ウィンドウは今やSDL_WINDOW_
      // RESIZABLEで作成される、create()自身のコメント参照): ユーザー
      // が端をドラッグした場合でも、プログラムによる
      // SDL_SetWindowSize()呼び出しによる場合でも、実際にサイズが
      // 変わるたびに、ウィンドウ自身の「新しい」ピクセルサイズが既に
      // event.window.data1/data2に入った状態で発火する
      // (setSize()/matchWindowSize()の両方も既にそれ自身でこれを
      // 1つ発火させる――下のhandleWindowResized()は、再計算された
      // 行/列数が実際には変わらない場合には無害にno-opするよう書か
      // れているので、これはそのどちらとも競合しない)。
      case SDL_WINDOWEVENT:
        // syncHighDpiRenderScale()自身のコメント参照: 表示・サイズ変更・
        // (倍率の異なる)ディスプレイ間の移動のたびに倍率を取り直す。
        if (event.window.event == SDL_WINDOWEVENT_SHOWN || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
            event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
          syncHighDpiRenderScale(impl_->window, impl_->renderer);
          impl_->needsRedraw = true;
        }
        if (event.window.event == SDL_WINDOWEVENT_EXPOSED || event.window.event == SDL_WINDOWEVENT_SHOWN) {
          impl_->needsRedraw = true;
        } else if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
          handleWindowResized(event.window.data1, event.window.data2);
        }
        break;
      case SDL_MOUSEBUTTONDOWN: {
        // Impl::everInteracted自身のコメント参照――どんなボタン押下
        // (この後のswitchで無視される種類であっても)も「触れられた」
        // と数え、flush()を静的なアイドル画像から実際のつまみへ
        // 恒久的に切り替える。
        impl_->everInteracted = true;
        // 2026-09-02、直接のユーザー報告(=fg/=fh、マウスドラッグ後の
        // 明滅・「画面が入れ替わってる感じ」がhoverIndex_の固まりを
        // 直しても直らない件、ユーザー自身の仮説: "一旦画面が停止した
        // あともスクロールのスイッチは入っていて(クリック解除されて
        // ないのでドラッグ？)"): 実際にSDL_MOUSEBUTTONUPが正しく
        // 届いているか、SDL_MOUSEMOTION自身のLMASKビットが実機で
        // 期待通り立って/落ちているかを直接確認するための調査用ダンプ。
        if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
          FILE *f = std::fopen(dbg, "a");
          if (f) {
            std::fprintf(f, "---mouseButtonDown--- button=%d x=%d y=%d\n", event.button.button, event.button.x,
                         event.button.y);
            std::fclose(f);
          }
        }
        if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT) break;
        // スクロールバーの帯の中で始まる左クリックの押下は、つまみの
        // ドラッグのジェスチャであり、コンテンツのクリック/ドラッグ
        // ではない――代わりにscrollbarQueueへ回されるので、ファイルの
        // 選択/オープン(MouseClick)にもコンテンツのドラッグスクロール
        // (DragEvent)の開始にもならない。その帯での右クリックは、下の
        // 通常のMouseClickの経路へそのまま流れる(スクロールバーに
        // 右クリックの挙動は定義されていない;これは単にイベントを
        // 黙って落とすのを避けているだけ)。
        int scrollbarStripX = scrollbarStripLeft() - Impl::kMarginX;
        if (event.button.button == SDL_BUTTON_LEFT && event.button.x >= scrollbarStripX && !impl_->scrollbarHidden) {
          impl_->scrollbarDragActive = true;
          SDL_CaptureMouse(SDL_TRUE);
          impl_->scrollbarQueue.push_back(ScrollbarEvent{ScrollbarEventType::ButtonDown, event.button.y});
          break;
        }
        // 割る前にmax(0, ...): 余白の境界自体の中に着地したクリック
        // (kMarginX/kMarginY自身のコメント参照)は、そうしないとゼロ
        // を超えて引き算されてしまい、ほとんどのプラットフォームでは
        // floorではなくゼロ方向へ切り捨てられてしまう――クランプする
        // ことで、おかしな行/列に回り込む代わりに、余白全体を「最も
        // 近い端のセル」として扱う。
        int col = std::max(0, event.button.x - Impl::kMarginX) / std::max(impl_->cellWidth, 1);
        int row = std::max(0, event.button.y - Impl::kMarginY) / std::max(impl_->cellHeight, 1);
        // 2026-09-15修正、pendingLeftClick自身の宣言コメント参照:
        // 右クリックは従来通り押下即座にmouseQueueへ積む。左クリック
        // は、スコープ判定自体はこの押下位置(row/col)で確定させつつ、
        // mouseQueueへ積む(=実行する)のはSDL_MOUSEBUTTONUPまで
        // 遅らせる実験――モーションによる破棄は行わない。
        if (event.button.button == SDL_BUTTON_LEFT) {
          impl_->pendingLeftClick = MouseClick{row, col, false};
          SDL_CaptureMouse(SDL_TRUE);  // ドラッグがウィンドウを離れても動きの追跡を続ける
          impl_->dragQueue.push_back(DragEvent{DragEventType::ButtonDown, 0, row, col});
        } else {
          impl_->mouseQueue.push_back(MouseClick{row, col, true});
        }
        break;
      }
      case SDL_MOUSEBUTTONUP: {
        if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
          FILE *f = std::fopen(dbg, "a");
          if (f) {
            std::fprintf(f, "---mouseButtonUp--- button=%d scrollbarDragActive=%d\n", event.button.button,
                         impl_->scrollbarDragActive ? 1 : 0);
            std::fclose(f);
          }
        }
        if (event.button.button == SDL_BUTTON_LEFT) {
          SDL_CaptureMouse(SDL_FALSE);
          if (impl_->scrollbarDragActive) {
            impl_->scrollbarDragActive = false;
            impl_->scrollbarQueue.push_back(ScrollbarEvent{ScrollbarEventType::ButtonUp, event.button.y});
          } else {
            impl_->dragQueue.push_back(DragEvent{DragEventType::ButtonUp, 0});
          }
          // pendingLeftClick自身の宣言コメント参照: スコープ判定は
          // 押下時に確定済み、ここではその実行(mouseQueueへ積む)を
          // 解放の瞬間まで遅らせているだけ――動きが途中にあったか
          // どうかは問わない。scrollbarDragActiveの分岐に来る場合、
          // pendingLeftClickは(押下時のスクロールバー帯の早期return
          // により)そもそも設定されていないので、ここは常に安全。
          if (impl_->pendingLeftClick) {
            impl_->mouseQueue.push_back(*impl_->pendingLeftClick);
            impl_->pendingLeftClick.reset();
          }
        }
        break;
      }
      case SDL_MOUSEMOTION: {
        if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
          FILE *f = std::fopen(dbg, "a");
          if (f) {
            std::fprintf(f,
                         "---mouseMotion--- x=%d y=%d state=%d lmask=%d scrollbarDragActive=%d focus=%d\n",
                         event.motion.x, event.motion.y, event.motion.state,
                         (event.motion.state & SDL_BUTTON_LMASK) ? 1 : 0, impl_->scrollbarDragActive ? 1 : 0,
                         (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_INPUT_FOCUS) ? 1 : 0);
            std::fclose(f);
          }
        }
        if (impl_->scrollbarDragActive) {
          impl_->scrollbarQueue.push_back(ScrollbarEvent{ScrollbarEventType::Motion, event.motion.y});
        } else if (event.motion.state & SDL_BUTTON_LMASK) {
          // row/column(2026-09-07追加、DragEvent自身の宣言コメント
          // 参照): 下のhover分岐と全く同じピクセル→セル変換。
          int dragCol = std::max(0, event.motion.x - Impl::kMarginX) / std::max(impl_->cellWidth, 1);
          int dragRow = std::max(0, event.motion.y - Impl::kMarginY) / std::max(impl_->cellHeight, 1);
          impl_->dragQueue.push_back(
              DragEvent{DragEventType::Motion, event.motion.yrel, dragRow, dragCol, event.motion.xrel});
        } else if (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_INPUT_FOCUS) {
          // キーボード/入力フォーカスで判定している(SDL_WINDOW_MOUSE_
          // FOCUSでは「ない」、それだと、アクティブかどうかに関わらず、
          // たまたまカーソルが上に乗っているどのウィンドウについても
          // trueになってしまう――まさにこのチェックの意味そのもの)。
          // SDLは、実際にどのウィンドウがアクティブかに関わらず、
          // カーソルの下にあるウィンドウへSDL_MOUSEMOTIONを配送する、
          // 本物の端末自身のSGRマウスレポート(バックグラウンド/
          // フォーカスされていない端末はそもそも一切受け取らない)と
          // は異なり。実際の報告+スクリーンショット: "ウィンドウが
          // 非アクティブ（左上の赤黄緑がみな灰色）なのにマウスが
          // ウィンドウ内に入るとカーソルが追随" -- カーソル追随の
          // ハイライトは、このウィンドウが他のフォーカスされていない
          // ウィンドウの後ろに座っている間でさえ、マウスを追跡して
          // いた。
          // Impl::lastHoverEligibleMotionX/Y自身の宣言コメント参照
          // (2026-09-20追加): ポインタが物理的に動いていない(直前に
          // 記録した座標と同一の)MOUSEMOTIONは、実際のユーザー操作
          // ではなくフォーカス復帰の副作用である可能性が高いので、
          // hoverQueueへは積まない――座標が変わった時だけ「本物の
          // 移動」として扱う。
          bool actuallyMoved =
              event.motion.x != impl_->lastHoverEligibleMotionX || event.motion.y != impl_->lastHoverEligibleMotionY;
          impl_->lastHoverEligibleMotionX = event.motion.x;
          impl_->lastHoverEligibleMotionY = event.motion.y;
          if (actuallyMoved) {
            int col = std::max(0, event.motion.x - Impl::kMarginX) / std::max(impl_->cellWidth, 1);
            int row = std::max(0, event.motion.y - Impl::kMarginY) / std::max(impl_->cellHeight, 1);
            impl_->hoverQueue.push_back(HoverEvent{row, col});
          }
        }
        break;
      }
      case SDL_KEYDOWN: {
        // Impl::imeComposing自身のコメント参照: IME変換中はこの
        // イベント全体を無視し、Return/Backspace/矢印キー等の意味を
        // IMEに完全に譲る(SDL_TEXTEDITING/SDL_TEXTINPUT側だけで
        // 完結させる)。
        if (impl_->imeComposing) break;
        if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
          FILE *f = std::fopen(dbg, "a");
          if (f) { std::fprintf(f, "SDL_KEYDOWN sym=%d mod=%d GUI=%d CTRL=%d\n", event.key.keysym.sym, event.key.keysym.mod, (event.key.keysym.mod & KMOD_GUI) != 0, (event.key.keysym.mod & KMOD_CTRL) != 0); std::fclose(f); }
        }
        if (auto special = translateScancode(event.key.keysym.sym)) {
          impl_->keyQueue.push_back(KeyEvent{0, special});
          break;
        }
        // Cmd+=/Cmd+-/Cmd+0(フォントサイズ)とCmd+K(カラープリセット)
        // -- 2026-08-24、項目426: KeyEventへの「あらゆる」変換より前に
        // 「ここで」横取りして消費するので、Applicationはそれらを
        // 一切見ることがない(このファイルがウィンドウレベルのマウス
        // ジェスチャとアプリケーションレベルの入力の間で既に保っている
        // のと同じ分離)――それらはウィンドウ/レンダリングの調整で
        // あり、usilicのコマンドではないので、feedByte()へ絶対に届いて
        // '='/'-'/'0'/'k'に対する実際のモードごとのキー割り当てと衝突
        // する危険を冒してはならない。KMOD_GUIはmacOSにおけるCmdキーの
        // SDLでの呼び名である(他ではWindows/Superキー);下のKMOD_CTRL
        // より先にチェックしているのは、この2つは実際のキーボードでは
        // 互いに排他的であり、Cmdがこのペア自身の自然な居場所である
        // ため(adjustFontSize()のコメントが説明する、ブラウザ/エディタ
        // のCmd+Plus/Cmd+Minus/Cmd+0のズームの慣習に合わせている)。
        // SDLK_EQUALSは、素の"="と、Shift+"="(USキーボードでの"+"、
        // ユーザーが実際に手を伸ばすキー)の両方をカバーする、SDLが
        // 両者について同じkeysymを報告するため;SDLK_KP_PLUS/KP_MINUS
        // は数値キーパッドについて同様にカバーする。
        if (event.key.keysym.mod & KMOD_GUI) {
          SDL_Keycode sym = event.key.keysym.sym;
          // SDLK_KP_EQUALSがSDLK_EQUALS/KP_PLUSと並んで含まれている
          // ――直接のテスト(USILICGUIDEBUGのダンプ)により、合成的な
          // キー入力の注入(AppleScriptの`keystroke "=" using {command
          // down}`、このサンドボックスには本物のキーボードが無いため
          // このキー割り当てをそもそもテストするのに使っている)が、
          // Cmdが押されている間、素のSDLK_EQUALSではなく「この」
          // keysymを配送することが確認された――何らかのmacOSのキー
          // ボードレイアウト/修飾キーの相互作用がそれを付け替えている。
          // 本物の物理キーボードがこの癖を共有するかどうかは分からない;
          // 起きることが分かっている以上、どちらにしても頑健性のために
          // 残してある。
          // 2026-08-30、直接のユーザー報告: 実物のキーボードでは"Cmd -"
          // /"Cmd 0"(共にShift不要)は効くのに"Cmd +"(USキーボードで
          // Shift+"="が必要)だけ反応しなかった――上のAppleScript注入
          // (SDLK_KP_EQUALS)の癖とは別に、実機ではCmdを押しながら
          // Shiftも押した状態がSDLK_EQUALSではなくSDLK_PLUSとして
          // 届いている(macOS側がShift修飾を先に文字へ反映してから
          // SDLへ渡していると見られる)ため。
          // 2026-09-28追加、直接のユーザー報告("'='が入らなくて文字が大きくならない。
          // command + shift + - →文字が小さくなる"): JISキーボードでは"="はShift+"-"、
          // "+"はShift+";"の位置にあり、SDLにはそれぞれSDLK_MINUS/SDLK_SEMICOLON
          // (+Shift)として届く。Shift付きの"-"と";"(Shift無しの";"も、JISで
          // "+"の刻印のあるキーなので)は大きくする側に数える。
          bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
          if (sym == SDLK_EQUALS || sym == SDLK_PLUS || sym == SDLK_KP_PLUS || sym == SDLK_KP_EQUALS ||
              sym == SDLK_SEMICOLON || (sym == SDLK_MINUS && shift))
            adjustFontSize(1);
          else if (sym == SDLK_MINUS || sym == SDLK_KP_MINUS) adjustFontSize(-1);
          else if (sym == SDLK_0 || sym == SDLK_KP_0) adjustFontSize(0);
          else if (sym == SDLK_k) cycleColorPreset();
          break;
        }
        // コントロールキーの組み合わせ(Ctrl+A..Ctrl+_)は、SDL_TEXTINPUT
        // としてではなく、KMOD_CTRLが立ったSDLK_a..として届く――それら
        // はここで変換する;他の全て(input method経由のマルチバイト
        // UTF-8を含む、プレーンな文字)は代わりに下のSDL_TEXTINPUT経由
        // で届く、本物の端末が制御バイトと表示可能テキストを分離して
        // いるのに合わせている。
        if (event.key.keysym.mod & KMOD_CTRL) {
          SDL_Keycode sym = event.key.keysym.sym;
          if (sym >= SDLK_a && sym <= SDLK_z) {
            impl_->keyQueue.push_back(KeyEvent{(sym - SDLK_a) + 1, std::nullopt});
          } else if (sym == SDLK_LEFTBRACKET) {
            impl_->keyQueue.push_back(KeyEvent{0x1b, std::nullopt});  // Ctrl+[ は ESC と同じ
          } else if (sym == SDLK_SPACE) {
            // 2026-09-07追加、直接のユーザー報告(⑨のGUI限定「COPY
            // モードで^@によるハイライトが一切出ない」を切り分けた
            // 末に発覚した真因): Ctrl+Space(^@、application.cpp自身が
            // 一貫して0x80として扱う――application.cpp:2143自身のコメント
            // 参照、実端末では素のNULを読めないのでKeyInputが0x80へ
            // 変換して報告するのと同じ規約)がこの分岐に一件も無く、
            // 上のCtrl+A〜Z/Ctrl+[以外は無条件で読み捨てられていた――
            // Application::feedByte()へ一度も届かないため、COPYモードの
            // startMark()/その他^@に反応する全ての箇所がGUIでは丸ごと
            // 無反応だった(モードラインの"Rectangle-Mark"表示のような
            // 描画を全く伴わない副作用すら起きないことから、ハイライト
            // 計算やレンダリング側の問題ではなく、この最も手前の入力
            // 変換の欠落だと判明した)。
            impl_->keyQueue.push_back(KeyEvent{0x80, std::nullopt});
          }
          // 2026-09-08、直接のユーザー報告により撤回: 一時的にここへ
          // SDLK_MINUS(Ctrl+マイナス、^-)のケースを追加していたが
          // (application.cpp自身のfeedCopyByte()、0x00/0x80ケース
          // 自身のコメント参照)、Ubuntu実機のGNOME Terminalではこの
          // キー自体がフォントサイズ縮小のショートカットと衝突する
          // と判明したため撤去した。
          break;
        }
        if (event.key.keysym.sym == SDLK_RETURN) impl_->keyQueue.push_back(KeyEvent{0x0d, std::nullopt});
        else if (event.key.keysym.sym == SDLK_BACKSPACE) impl_->keyQueue.push_back(KeyEvent{0x7f, std::nullopt});
        else if (event.key.keysym.sym == SDLK_TAB) impl_->keyQueue.push_back(KeyEvent{0x09, std::nullopt});
        else if (event.key.keysym.sym == SDLK_ESCAPE) impl_->keyQueue.push_back(KeyEvent{0x1b, std::nullopt});
        break;
      }
      case SDL_TEXTEDITING: {
        // Impl::imeComposing自身のコメント参照。event.edit.textは、
        // 変換が確定/取り消された(=もう編集中の文字列が無い)瞬間、
        // 空文字列のイベントとして届く――これでフラグを下ろす。
        impl_->imeComposing = event.edit.text[0] != '\0';
        break;
      }
      case SDL_TEXTINPUT: {
        // IMEが確定した完成形の文字列がここへ届く時点で、変換自体は
        // もう終わっている(Impl::imeComposing自身のコメント参照)。
        impl_->imeComposing = false;
        for (const char *p = event.text.text; *p; p++) impl_->keyQueue.push_back(KeyEvent{static_cast<unsigned char>(*p), std::nullopt});
        break;
      }
      case SDL_MOUSEWHEEL: {
        impl_->everInteracted = true;  // Impl::everInteracted自身のコメント参照
        // SDL_MOUSEWHEEL_FLIPPEDは、OSレベルの「ナチュラルスクロール」
        // 設定が有効であることを意味する(macOSのトラックパッドでは
        // デフォルトで有効と確認済み)――SDLはどちらの場合でも生の
        // ハードウェアのデルタを報告し、符号の補正は呼び出し元に
        // 任せているので、ホイールの方向をユーザー自身のOSのスクロール
        // 方向設定が言う「上」に一致させるには、この符号反転は任意
        // ではなく必須である。
        int deltaY = event.wheel.y;
        int deltaX = event.wheel.x;
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
          deltaY = -deltaY;
          deltaX = -deltaX;
        }
        if (deltaY != 0 || deltaX != 0) impl_->wheelQueue.push_back(WheelEvent{deltaY, deltaX});
        break;
      }
      default:
        break;
    }
  }
  return stillOpen;
}

bool GraphicalDisplay::consumeNeedsRedraw() {
  bool value = impl_->needsRedraw;
  impl_->needsRedraw = false;
  return value;
}

std::optional<GraphicalDisplay::MouseClick> GraphicalDisplay::consumeMouseClick() {
  if (impl_->mouseQueue.empty()) return std::nullopt;
  MouseClick click = impl_->mouseQueue.front();
  impl_->mouseQueue.pop_front();
  return click;
}

std::optional<GraphicalDisplay::HoverEvent> GraphicalDisplay::consumeHoverEvent() {
  if (impl_->hoverQueue.empty()) return std::nullopt;
  HoverEvent hover = impl_->hoverQueue.front();
  impl_->hoverQueue.pop_front();
  return hover;
}

std::vector<std::string> GraphicalDisplay::consumeDroppedFiles() {
  std::vector<std::string> result;
  result.swap(impl_->completedDrop);
  return result;
}

std::optional<GraphicalDisplay::KeyEvent> GraphicalDisplay::consumeKeyEvent() {
  if (impl_->keyQueue.empty()) return std::nullopt;
  KeyEvent event = impl_->keyQueue.front();
  impl_->keyQueue.pop_front();
  return event;
}

std::optional<GraphicalDisplay::DragEvent> GraphicalDisplay::consumeDragEvent() {
  if (impl_->dragQueue.empty()) return std::nullopt;
  DragEvent event = impl_->dragQueue.front();
  impl_->dragQueue.pop_front();
  return event;
}

std::optional<GraphicalDisplay::WheelEvent> GraphicalDisplay::consumeWheelEvent() {
  if (impl_->wheelQueue.empty()) return std::nullopt;
  WheelEvent event = impl_->wheelQueue.front();
  impl_->wheelQueue.pop_front();
  return event;
}

std::optional<GraphicalDisplay::ScrollbarEvent> GraphicalDisplay::consumeScrollbarEvent() {
  if (impl_->scrollbarQueue.empty()) return std::nullopt;
  ScrollbarEvent event = impl_->scrollbarQueue.front();
  impl_->scrollbarQueue.pop_front();
  return event;
}

int GraphicalDisplay::cellWidthPixels() const { return impl_->cellWidth; }

int GraphicalDisplay::cellHeightPixels() const { return impl_->cellHeight; }
int GraphicalDisplay::marginYPixels() const { return Impl::kMarginY; }

void GraphicalDisplay::setColors(Color foreground, Color background) {
  impl_->fg = SDL_Color{foreground.r, foreground.g, foreground.b, 255};
  impl_->bg = SDL_Color{background.r, background.g, background.b, 255};
}

GraphicalDisplay::Color GraphicalDisplay::foregroundColor() const {
  return Color{impl_->fg.r, impl_->fg.g, impl_->fg.b};
}
GraphicalDisplay::Color GraphicalDisplay::backgroundColor() const {
  return Color{impl_->bg.r, impl_->bg.g, impl_->bg.b};
}
int GraphicalDisplay::fontPointSize() const { return impl_->currentFontPointSize; }

namespace {

// 通常のタイトル付き、閉じられる、リサイズ可能なウィンドウについての、
// macOSのタイトルバーの高さ(ポイント)の推定値。SDL_GetWindowBordersSize()
// 自体が失敗した時の「フォールバックとしてのみ」使う、実際の報告
// (2026-08-08、"変化無し"――境界サイズに基づく補正が実機で目に見える
// 変化を全く生まなかった)が、これを実際の、静かな根本原因として指し
// 示している: SDL2自身の文書化された挙動は、SDL_GetWindowBordersSize()
// 呼び出しが失敗すると出力を0/0/0/0のままにするというもので、この
// プロジェクト自身のweb調査でも、まさにこれが既知のCocoa/macOSの制約
// (「macOS Cocoaにおける-1という返り値は、そのプラットフォームでは
// ウィンドウの境界サイズ情報がサポートされていないことを示す」)として
// 出てきた――このマシン上でも直接確認済み(SDL_GetWindowBordersSize()
// を呼ぶ単体のドライバは`rc=-1 ... error=That operation is not
// supported`と出力した)。0にフォールバックする(この関数の最初の版)
// と、補正全体を黙ってスキップしてしまう――まさに報告された「変化
// 無し」という症状そのもの。この定数は、「権威ある」はずのOSへの
// 問い合わせ自体が機能しない場合でも、同じ2つの呼び出し箇所
// (setWindowPosition()/setWindowSize())が動作し続けるようにする。
//
// リテラルの値はAppleのドキュメントから来たものではない;この
// サンドボックス内では何も本物のmacOSのタイトルバーを直接計測できない
// ――完全に実際のユーザーのフィードバックから調整されたものである:
// 28(最初の見積もり)-> "縦が2,3ドット長い"(まだ2〜3pt「小さすぎ」で、
// ウィンドウ全体をその分だけ高くしすぎている)-> 31 ->
// USILICTITLEBARHEIGHT=32経由で32がちょうど正しいと確認済み(環境変数
// によるオーバーライド、このファイル/main.cppの他の箇所にある
// USILICSCROLLBARSCALE/-uと同じパターン、この種の調整のたびに新たな
// コード変更+再ビルドが要らないように、まさにそのために追加した)。
constexpr int kDefaultTitleBarHeight = 32;

int estimatedTitleBarHeight() {
  if (const char *env = std::getenv("USILICTITLEBARHEIGHT")) {
    int parsed = std::atoi(env);
    if (parsed > 0) return parsed;
  }
  return kDefaultTitleBarHeight;
}

// setWindowPosition()/setWindowSize()で共有される――どちらも、
// AppleScriptの「bounds of window」(外枠)の座標の慣習とSDL自身の
// (コンテンツ領域)のそれとの間を変換するのに、同じ上部境界(タイトル
// バー)の高さを必要とする。実際に利用可能な場合は、OSが報告する
// 本物の値(SDL_GetWindowBordersSize())を優先し、そうでなければ
// estimatedTitleBarHeight()にフォールバックする(それ自身のコメント
// 参照)。left/bottom/rightは、macOSでは実質的に常に0である(通常の
// タイトル付きウィンドウには、タイトルバー自体を超える目に見える
// 側面/下部のクロームは無い)が、完全性のため、そして将来の非macOSな
// Displayバックエンドがここでも黙って間違ったままにならないよう、
// それでも問い合わせ/返却する。
struct WindowBorders {
  int top = 0, left = 0, bottom = 0, right = 0;
};

WindowBorders realOrEstimatedWindowBorders(SDL_Window *window) {
  WindowBorders borders;
  if (window && SDL_GetWindowBordersSize(window, &borders.top, &borders.left, &borders.bottom, &borders.right) == 0 &&
      borders.top > 0) {
    return borders;
  }
  borders = WindowBorders{};
  borders.top = estimatedTitleBarHeight();
  return borders;
}

}  // namespace

// `x, y`は、AppleScriptの「bounds of window」が使うのと同じ「外枠、
// タイトルバーを含む」座標の慣習であることが期待されている
// (main.cppのqueryTerminalAppWindowBounds()自身のコメント参照)――
// SDL_SetWindowPosition()自身のネイティブな慣習では「ない」。SDL
// 自身の文書化された挙動は、position/sizeはウィンドウの「コンテンツ」
// 領域を表し、OSが描く装飾(タイトルバー)はその「外側/上に」追加
// される、というものである――外枠基準の`y`をそのまま変更せず渡す
// (この関数の以前の版)と、このウィンドウの「コンテンツ」の上端が、
// 本物の端末の「外枠」の上端(タイトルバー)がある場所に置かれてしまい、
// このウィンドウ自身のタイトルバーが、本物の端末のタイトルバーが実際
// にある場所より、タイトルバーの高さ分だけ浮いてしまう(実際の報告、
// 2026-08-07、スクリーンショット⑮⑯: "表示開始位置の縦方向へのズレが
// 共通してある"、どのTerminal.appのプロファイル/配色を使ってもマグニ
// チュードが同じで、プロファイル固有の何かというより固定的な幾何学的
// オフセットと一致する)。このウィンドウ自身の上部境界(タイトルバー)
// の高さを`y`に足す(なぜそれが単純にSDL_GetWindowBordersSize()の
// 生の結果ではないのかについてはrealOrEstimatedWindowBorders()自身の
// コメント参照)ことで、SDLに渡す前に補正する。
void GraphicalDisplay::setWindowPosition(int x, int y) {
  if (!impl_->window) return;
  WindowBorders borders = realOrEstimatedWindowBorders(impl_->window);
  x += borders.left;
  y += borders.top;
  SDL_SetWindowPosition(impl_->window, x, y);
}

// `width, height`は、setWindowPosition()が取るのと同じ「外枠、タイトル
// バーを含む」慣習であることが期待されている(それ自身のコメント参照)
// ――SDL_SetWindowSize()自身のネイティブな慣習では「ない」、こちら
// は(SetWindowPositionと同様に)「コンテンツ」領域だけを表す。この
// 関数の最初の版は`width`/`height`を変換せずそのまま渡していた、
// これは――setWindowPosition()による`y`への正しいタイトルバー高さの
// 加算と組み合わさって――このウィンドウの「外枠」を、本物の端末自身
// の外枠より、タイトルバーの高さ分だけ「高く」してしまっていた
// (実際の報告、2026-08-08、スクリーンショット: "ウィンドウ操作部の
// 分だけ縦長かつ表示行数が少なめ"): コンテンツの高さを本物の端末の
// 外枠の高さ(タイトルバーを含む)に設定すると、このウィンドウの
// コンテンツ領域自体が既に、必要な高さよりタイトルバーの高さ分だけ
// 高くなってしまい、結果としてウィンドウ全体(そのサイズ過剰な
// コンテンツ領域に、その上に乗るこのウィンドウ「自身の」タイトル
// バーが加わったもの)が、全体としてタイトルバー1つ分丸ごと高くなって
// しまう。setWindowPosition()がそれ自身の座標を変換するのと同じ方法
// で補正する: SDLに渡す前に、与えられた外枠寸法からrealOrEstimated
// WindowBorders()(それ自身のコメント参照)の境界サイズを差し引き、
// 外枠サイズをSDLが実際に求めているコンテンツ領域サイズへ変換する。
//
// その変換に加えて、これは依然として、このクラス自身のcellWidth*
// columns+margins+scrollbar-widthという計算式が計算したものを何で
// あれ上書きする――同じ2026-08-07の報告により、その計算式自身が
// 計算する幅は、試した全てのプロファイル(Basic/Homebrew/Ocean/Red
// Sands/Man Page)にわたって、本物の端末より常に1〜2ポイント狭いこと
// が判明した。その小さな、プロファイルを問わず一定の食い違いの正確な
// 発生源(cellWidthでのフォントメトリクスの丸め、あるいは1〜2ポイント
// ずれた固定のmargin/scrollbar-width定数)を追いかけるより、本物の
// 端末自身の既に計測済みのサイズに直接合わせる方が、単純でもあり、
// 確実に正確でもある、その代償として、わずかな空白の背景の帯(あるいは、
// 万が一本物の端末の方がなぜか小さい場合には、右/下端で1〜2ポイントの
// コンテンツが切り取られること)が生じ得るが――目に見えて食い違った
// ウィンドウサイズという代替案の隣に置けば、知覚できないほどのもの
// である。
void GraphicalDisplay::setWindowSize(int width, int height) {
  if (!impl_->window) return;
  WindowBorders borders = realOrEstimatedWindowBorders(impl_->window);
  width -= (borders.left + borders.right);
  height -= (borders.top + borders.bottom);
  if (width > 0 && height > 0) SDL_SetWindowSize(impl_->window, width, height);
}

// SDLウィンドウの外枠サイズと、このクラス自身のcellWidth/cellHeight
// の両方を上書きし、columns*cellWidth+margins+scrollbar-widthと
// rows*cellHeight+marginsが、与えられた外枠寸法をちょうど埋めるように
// する――create()内のTTF_FontHeight()ベースのcellHeight計算(と
// USILICLINEHEIGHT)が追加された、同じ「行間が狭い」という行間隔の
// 不一致に対する、より直接的な修正。実際の報告(2026-08-08、5つの
// 異なる本物のTerminal.appプロファイルにわたるフォントサイズ13/13/
// 14/18/14pt、"フォントの大きさによる。同サイズから適正値を一律に
// 算出できればUSILICLINEHEIGHTは不要かと")により、必要な補正はフォント
// サイズに応じてスケールするが、その様子は単一の固定定数――あるいは
// TTF_FontHeight()と生のグリフメトリクスの比較でさえも――異なる
// フォント/サイズにわたって確実には捉えられないことが判明した。
// 「実はすでにGUI」の起動は、本物の端末の正確な外枠のピクセル寸法
// (AppleScriptのboundsを介して)と、表示する必要がある正確な行/列数
// を既に「知っている」ので、これはフォントメトリクスから見積もる
// のを一切やめて、その算術から「直接」cellWidth/cellHeightを導出し、
// 本物の端末がたまたま使っているフォント/サイズが何であれ、ピクセル
// 単位で正確な一致を保証する。ウィンドウ自身の外枠サイズは、(整数
// 除算されるため、1〜2ピクセル切り捨てられている可能性のある)
// cellWidth/cellHeightから再計算するのではなく、生の(丸めていない)
// 目標寸法から設定される、そのためコンテンツ自身の右/下端に1〜2
// ピクセルの遊びが残っても、外枠は依然として正確に一致する――全体の
// ウィンドウサイズが食い違うことに比べれば知覚できないほどのもの
// である。
//
// フォントメトリクスに基づくcellWidth/cellHeight(create()自身の
// 計算)は、合わせるべき本物の端末が無い起動(素の`-g`、あるいは
// セッション途中の`^Og`)についての正しいフォールバックであり続ける
// ――このメソッドは「実はすでにGUI」オーバーレイの経路からのみ呼ば
// れ、そこでは本物の目標サイズが常に利用可能である。
void GraphicalDisplay::matchWindowSize(int outerWidth, int outerHeight) {
  if (!impl_->window || impl_->rows <= 0 || impl_->columns <= 0) return;
  WindowBorders borders = realOrEstimatedWindowBorders(impl_->window);
  int contentWidth = outerWidth - (borders.left + borders.right);
  int contentHeight = outerHeight - (borders.top + borders.bottom);
  if (contentWidth <= 0 || contentHeight <= 0) return;
  // 切り捨てではなく「最も近い」整数のセルサイズに丸める――実際の
  // 報告(2026-08-09、スクリーンショット「⑰」、Man Pageプロファイル、
  // 48行)により、以前の単純な切り捨て除算(contentHeight/rows)が、
  // この「実はすでにGUI」機能のsetWindowSize()がそもそも修正した
  // まさに同じ種類のバグ(docs/HISTORY.md 2026-08-07「⑯」: 本物の
  // 端末がこのウィンドウより短いために下にはみ出て見える)として再発
  // していることが判明した――ウィンドウ自身のサイズは今や常に
  // cellHeightから導かれるため(下記参照)、切り捨て(cellHeightを
  // 常に「下」へ丸める)は、結果として得られるグリッド、ひいては
  // ウィンドウ自体を、その除算の余りの全量(このコメントの以前の版が
  // 想定していた「最大でも1セル分」ではなく、rows-1pxまで――実測で
  // 確認済み: 48行で45pxの隙間が生じた)だけcontentHeightより短くして
  // しまう。代わりに最も近い値に丸めることで、|rows*cellHeight -
  // contentHeight|を直接最小化し(単一の一様な整数セル高さで達成できる
  // 最善)、食い違いをおおよそその半分に収める(最悪でも約rows/2px、
  // 実際にはそれよりずっと小さいことが多い――上記の実際のMan Pageの
  // 数値は13.9375を14に切り上げ、結果として本物の端末より45px「短く」
  // ではなく、わずか3px「高く」ウィンドウが着地する)。同点は切り上げる
  // (切り捨てる前に`+denom/2`)、ここではどちらの方向へずれても等しく
  // 許容できる数ピクセルの食い違いに過ぎないので問題ない。
  // cellHeightをcellWidthより先に計算するよう並べ替えてある
  // (2026-08-30、直接のユーザー依頼でscrollbarWidthがImpl::
  // kScrollbarWidthという固定定数からcellHeight連動の値になった
  // ため――Impl::scrollbarWidth自身のコメント参照): 以前は逆順で、
  // cellWidthの式が固定のkScrollbarWidthを差し引くだけで済んでいた
  // が、今はその「予約されたスクロールバーの帯の幅」自体がこの行で
  // 求めるcellHeightから決まるため、先に求めておく必要がある。
  int cellHeight = (contentHeight - Impl::kMarginY * 2 + impl_->rows / 2) / impl_->rows;
  int cellWidth = (contentWidth - Impl::kMarginX * 2 - cellHeight + impl_->columns / 2) / impl_->columns;
  // 上のcellWidth自身の最も近い整数への丸め(cellHeightの同一の丸め
  // についての数行下の大きなコメント参照)は、グリッド自身の右端
  // (kMarginX + columns*cellWidth)がscrollbarStripLeft()(contentWidth
  // - scrollbarWidth、2026-08-24以降、ウィンドウの実際の右端に
  // ぴったり付く――それ自身のコメント参照)を超えるほど「上」に丸め
  // てしまうことがある。scrollbarStripLeft()自身の修正(docs/
  // HISTORY.md 2026-08-24のmpi報告、項目429)は、それが起きた時、
  // スクロールバーの帯自体を本物のウィンドウの内側に完全に収める
  // ことは既に行っているが――「追跡の」実際のmpi報告("横が惜しい"、
  // 同日)により、このメソッドがスクロールバーの今や正しくなった
  // 位置を考慮してcellWidthを調整することが一切無いため、文字の
  // 「グリッド」がその同じ予約済みの帯の中まで依然として伸びている
  // ことが判明した: 実際のテキストの一番右の列(複数)が、スクロール
  // バー自身のビットマップ/つまみ(flush()内で後から描画されるため、
  // グリッドの上に塗られる)の「下に」レンダリングされ、事実上それら
  // の文字を隠してしまう。cellWidthを(決して大きくせず)小さくして
  // いき、グリッドとそれ自身の左マージンとスクロールバー自身の予約
  // 幅を合わせたものがcontentWidthの中に収まるようにすることで、
  // 両者が決して重ならないことを保証する、その代償として、グリッド
  // がスクロールバーの帯を覆う代わりに、そこから数pxだけ手前で終わる
  // (単なる背景の隙間になる)ことがあり得る――scrollbarStripLeft()
  // 自身が既に選んだのと同じ「文字化けより隙間」というトレードオフ
  // である。
  while (cellWidth > 1 && Impl::kMarginX + impl_->columns * cellWidth + cellHeight > contentWidth) {
    cellWidth--;
  }
  if (cellWidth <= 0 || cellHeight <= 0) return;
  // (create()で一度だけ取得される)安定したベースのフィールド
  // (Impl::baseFontMetricCellWidth/Height)から読み取る、
  // impl_->cellWidth/cellHeightからでは「ない」――後者は数行下で
  // 本物の端末自身の目標値によって上書きされてしまうので、
  // 「2回目」のmatchWindowSize()呼び出し(セッション途中でのフォント
  // サイズの生の変更、`^Og`の再入場、等)では、もはやフォント自身の
  // 本当のネイティブメトリクスを一切保持していないことになる。
  // 全体の理屈についてはImpl::baseFontMetricCellWidth自身のコメント
  // 参照。
  int fontMetricCellWidth = impl_->baseFontMetricCellWidth;
  int fontMetricCellHeight = impl_->baseFontMetricCellHeight;
  // cellWidthとcellHeightの「両方」が、今や単純な算術値であり、下限
  // でクランプされて「いない」――height自身のフォントメトリクスに
  // よる下限(元々は実際の報告、2026-08-08、5つの本物の端末プロファ
  // イルのうち2つが13ptで「文字化け/重なり、隣り合う行が互いに目に
  // 見えてにじむ」ため追加された)は、2026-08-10、width自身の下限が
  // 既にそうされたのと同じ日、同じ理由で削除された(その修正から
  // 移植した、クラス全体にわたる理屈は下記参照): 実際の報告
  // (スクリーンショット、ピクセルで計測: -gが1119x813、-cが1119x690、
  // 「同じ」22行のコンテンツ、実際の端末プロファイルの行間隔を0.8倍に
  // 設定)により、-cの実際の行間隔が正常に縮む一方で、-gが自分自身の
  // フォントメトリクスによる下限より縮むことを拒んでおり、-gが
  // オーバーレイするはずの本物の端末より目に見えて「縦長」になって
  // いることが判明した――24ptの「Novel」でwidthの下限が引き起こした
  // のとまさに同じ種類の食い違いだが、今回は大きなフォント「サイズ」
  // ではなく1.0未満の行間隔の「倍率」によって引き起こされている
  // (どちらも突き詰めれば「本物の端末自身の実際の行ごと/列ごとの
  // ピクセル計測値が、このクラス自身のフォントメトリクスに基づく
  // それの見積もりと異なる」ことを意味する)。
  //
  // 下限を取り除くことが、この下限が追加された「元々の」13ptの
  // 「にじみ」バグを再発させる危険を冒すかどうかは、想定するのでは
  // なく、これを行う前に確認済み: renderCell()自身のfitGlyphRect()
  // (描画される「あらゆる」グリフに使われる、ここで改めて読み直して
  // 確認済み)は、自身のセルより大きなグリフのサーフェスを常に一様に
  // 縮小する(scale = min(1.0, maxW/glyphW, maxH/glyphH)――widthに対して
  // と全く同じようにheightにも適用され、拡大は決してせず、切り取りも
  // 決してしない)――つまり、フォントメトリクスより小さいcellHeightは、
  // 各行自身のグリフをその行自身のボックスに収まるよう縮小するので
  // あって、一様でない位置決めがそうし得るように「隣接する」行のボックス
  // へにじみ出させることはない。元々の13ptの報告の実際の根本原因は、
  // 文字通りの危険なグリフのオーバーフローではなく、項目241のwidthの
  // バグ(とこのファイル自身のそれに対応するコメント)が判明したのと
  // 「まさに同じ」種類のウィンドウ/グリッドのサイズの食い違いで
  // あった可能性が非常に高い――本物の端末自身の計測済みのピクセルの
  // 高さに「正確に」合わせることで(このクラス自身が既に確立した
  // 「誤差ゼロ」の基準、項目227)、widthについて既にそうしたのと
  // 同じ方法で、その食い違いを構造的に取り除く。
  //
  // create()自身の元々のfontPointSizeに固定したままにするのではなく、
  // この目標cellHeightに(おおよそ)「正しい」サイズでフォント自体を
  // 再レンダリングする――2026-08-10追加、2つのサイズで-gと-cを比較
  // することで捕まえた実際の報告("18ドットだと行が重なる...24ドット
  // にしてみて文字が細すぎるからだとわかった。-gだとベクトルフォント
  // にならないのはなぜ？"): -gのテキストは24ptにおける-cの実際の
  // 端末のレンダリングより明らかに「薄く」/よりエイリアシングして
  // 見え、18ptでは全く判読不能だった――ヒンティング/AAの設定の違い
  // ではなく、まさにこのメソッド自身が原因だった: これより前は、
  // どのmatchWindowSize()呼び出しもimpl_->cellWidth/cellHeight
  // (「ウィンドウ」のサイズがそこから決まるレイアウトグリッド)を
  // 変えていたが、「フォント」自体は元々のcreate()時点のポイント
  // サイズのまま手を付けずにいた、そのため、目標のセルサイズ(今や
  // 常に本物の端末自身の正確な算術的に導出された値、項目254/258)
  // がその元々のサイズと「わずかにでも」異なるたびに、fitGlyphRect()
  // (renderCell()自身のグリフ合わせ用ヘルパー)は、既にラスタライズ
  // 済みのグリフのテクスチャをSDL_RenderCopy経由でビットマップとして
  // 再スケールせざるを得なかった――正しいサイズでの本物のベクトル
  // 再レンダリングではなく、ぼやけた/薄いラスターのリサイズであり、
  // 項目254/258がcellWidth/cellHeightをフォント自身のネイティブ
  // サイズまで下限でクランプするのをやめた今(以前はそれがscale==1.0
  // に、つまり実質的に常に再スケール無しになっていた)、はるかに
  // 頻繁に必要になっている。TTF_SetFontSize()(SDL_ttf 2.0.18+)は
  // フォント自身の内部グリフキャッシュをネイティブに新しいポイント
  // サイズで再レンダリングする――Impl::baseFontPointSize/
  // baseFontMetricCellHeight(フォント自身の安定した、決して上書き
  // されない元々の計測値、今や目標を保持しているimpl_->cellHeight
  // ではなく)からスケーリングすることで、上のfontMetricCellWidth/
  // Height自身の読み取りと同じ方法で、繰り返しの呼び出しにわたって
  // これを正確に保つ。widthではなくheightが比率を駆動するのは、
  // 行の高さの方が、ほとんどのフォントがポイントサイズをスケール
  // する元にする、より信頼できる/慣習的なメトリクスであるため;
  // 結果として得られるネイティブなwidthは必ずしも目標のcellWidthに
  // ぴったり乗るとは限らないが、fitGlyphRect()は依然としてその小さな
  // 残差を安全に吸収する、ただし以前よりはるかに劇的でなくなって
  // いるだけである。目標が既にフォント自身の現在のネイティブサイズ
  // と一致している場合はno-op(完全にスキップ)であり、フォントの
  // サイズについて実際には何も変える必要がない、まさにその
  // matchWindowSize()呼び出しのたびに、無駄なTTF_SetFontSize()呼び
  // 出し(タダではない――フォントのグリフキャッシュを再構築する)を
  // 避ける。
  int newPointSize = impl_->baseFontPointSize;
  if (fontMetricCellHeight > 0 && cellHeight != fontMetricCellHeight) {
    newPointSize = std::max(
        1, (impl_->baseFontPointSize * cellHeight + impl_->baseFontMetricCellHeight / 2) / impl_->baseFontMetricCellHeight);
    TTF_SetFontSize(impl_->font, newPointSize);
    if (impl_->fallbackFont) TTF_SetFontSize(impl_->fallbackFont, newPointSize);
    if (impl_->serifFont) TTF_SetFontSize(impl_->serifFont, newPointSize);
    // fontPointSize()自身の読み戻し(項目427、セッション終了時に
    // 実行中のGUI状態をHistory::GuiPreferencesへ永続化するのに使わ
    // れる)を、この起動経路についても正確に保つ――以前は、ここでは
    // 一切更新されず、create()が元々どんな値で呼ばれたかそのまま
    // 残っていた、これがまさに「実はすでにGUI」オーバーレイセッション
    // について本物の最終的なポイントサイズが決まる場所であるにも
    // 関わらず。
    impl_->currentFontPointSize = newPointSize;
    // フォントオブジェクトはたった今その場でリサイズされた――キャッシュ
    // されているテクスチャは全て今やそれに対して「間違った」ピクセル
    // サイズになっている。clearGlyphCache()自身のコメント参照。
    clearGlyphCache(*impl_);
  }
  impl_->cellWidth = cellWidth;
  impl_->cellHeight = cellHeight;
  impl_->scrollbarWidth = cellHeight;  // Impl::scrollbarWidth自身のコメント参照
  int width = contentWidth;
  int height = contentHeight;
  // flush()が既に使っているのと同じオプトインのUSILICGUIDEBUGファイル
  // を、まさに同じ理由で再利用する: このサンドボックスには比較対象
  // となる本物のTerminal.appウィンドウが無いので、実機の報告の根本
  // 原因を突き止める(上の45px残差のバグ、"Man Pageが余白大"、
  // 2026-08-08を見つけたのはまさにこのダンプである)ことは、当てず
  // っぽうではできない――将来のどんな食い違いの報告についても、
  // すぐに本物の数値と突き合わせられるよう、そのまま残してある。
  // flooredCellWidth/flooredCellHeightは、「純粋に」この診断行のため
  // だけにここで計算される(フォントメトリクスによる見積もりがどれ
  // だけ食い違っていたかを見るため)――どちらももう上のどの判断も
  // 駆動していない。
  int flooredCellWidth = std::max(cellWidth, fontMetricCellWidth);
  int flooredCellHeight = std::max(cellHeight, fontMetricCellHeight);
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) {
      int rescaledW = 0, rescaledH = 0;
      TTF_SizeUTF8(impl_->font, "M", &rescaledW, &rescaledH);
      std::fprintf(f,
                    "---matchWindowSize--- rows=%d columns=%d outer=%dx%d borders(t/l/b/r)=%d/%d/%d/%d "
                    "content=%dx%d arithCell=%dx%d fontMetricCell=%dx%d flooredCell=%dx%d finalWindow=%dx%d "
                    "newPointSize=%d rescaledFontCell=%dx%d\n",
                    impl_->rows, impl_->columns, outerWidth, outerHeight, borders.top, borders.left, borders.bottom,
                    borders.right, contentWidth, contentHeight, cellWidth, cellHeight, fontMetricCellWidth,
                    fontMetricCellHeight, flooredCellWidth, flooredCellHeight, width, height, newPointSize, rescaledW,
                    rescaledH);
      std::fclose(f);
    }
  }
  SDL_SetWindowSize(impl_->window, width, height);
}

void GraphicalDisplay::setVisible(bool visible) {
  if (!impl_->window) return;
  if (visible) SDL_ShowWindow(impl_->window);
  else SDL_HideWindow(impl_->window);
}

void GraphicalDisplay::raise() {
  if (!impl_->window) return;
  SDL_ShowWindow(impl_->window);
  SDL_RaiseWindow(impl_->window);
}

// NSCursor.openHand(mac_cursor.hppのextractOpenHandCursorImage())から
// 生きたまま抽出し、SDL_Cursorに包んだ「本物の」macOSシステムの
// open-handカーソル――手描きのSDL_CreateColorCursorビットマップでは
// なく、生の[[NSCursor openHandCursor] set]呼び出しでもない(この
// 関数の以前の版はそれを行っていたが、ドラッグが動き始めた瞬間に
// ただの矢印に戻ってしまった――下のsetPointingCursor()が既に行って
// いるように、SDL_SetCursor()を経由することがなぜ修正になったのかの
// 全ての経緯についてはmac_cursor.hpp自身のコメント参照)。この形を
// 自分たちで描こうとした、それ以来放棄された2回の以前の試み(まず
// フラットな長方形のシルエット、次に角の丸いカプセル型)は、どちら
// も実際に却下された("ロボットの手みたいなやつは却下"、それから
// "さっき却下のロボット指よりもひどくなった")――同日の鋭い指摘が、
// なぜどちらも決して正しく見えるはずがなかったのかを説明した: この
// プロジェクトがスクロールバーのドラッグについて「既に」表示している
// pointing-handカーソル(下のsetPointingCursor())は、そもそもこの
// プロジェクトによってもSDL2によっても実際には描かれたことが無かった
// ――それはmacOS自身の本物のシステムカーソルの資産である
// (SDL_SYSTEM_CURSOR_HANDはmacOSでは`[NSCursor pointingHandCursor]`
// に直接マップされる、SDL2自身の上流のソース、
// src/video/cocoa/SDL_cocoamouse.mを読んで確認済み)、なので、その
// 隣に座るどんな手描きの近似も、比較すれば常に見劣りするはずだった。
// 代わりに、小さなObjective-C++(.mm)のブリッジ(mac_cursor.mm)経由で
// NSCursor.openHand自身のピクセルを抽出する(このマシン上に、本物の
// 整形式のベクターカーソルとして存在することを確認済み:
// /System/Library/Frameworks/ApplicationServices.framework/Versions/A/
// Frameworks/HIServices.framework/Versions/A/Resources/cursors/
// openhand/cursor.pdf)ことで、まさに同じ本物の資産を代わりに見せる
// ――以前の「.mmは避ける」という好み(スクロールバー自身のNSScroller
// の作業)からは考え直したものだが、それはこの投機的な試みとは異なり、
// ここでの見返りが具体的で既に実証済みであるという特有の理由による。
void GraphicalDisplay::setHandCursor(bool active) {
  if (active == impl_->handCursorActive) return;
  impl_->handCursorActive = active;
  if (active) {
    if (!impl_->handCursor) {
      CursorImage img = extractOpenHandCursorImage();
      if (img.width > 0 && img.height > 0) {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
            img.rgba.data(), img.width, img.height, 32, img.width * 4, SDL_PIXELFORMAT_RGBA32);
        if (surface) {
          impl_->handCursor = SDL_CreateColorCursor(surface, img.hotspotX, img.hotspotY);
          SDL_FreeSurface(surface);
        }
      }
    }
    if (impl_->handCursor) SDL_SetCursor(impl_->handCursor);
  } else {
    SDL_SetCursor(SDL_GetDefaultCursor());
  }
}

// SDL自身のシステムのpointing-hand(人差し指)――実際の依頼
// (2026-08-10、setHandCursor()自身のコメント参照)がコンテンツの
// ドラッグを代わりに本物のmacOSのopen-handカーソルへ分離する前に、
// setHandCursor()が「あらゆる」ドラッグジェスチャについて表示して
// いたもの。今は、コンテンツを掴むというより本当にコントロールを
// クリック/操作するような感じのジェスチャのために予約されている:
// スクロールバーのつまみのドラッグ(main.cppにあるそれ自身の呼び出し
// 箇所参照)――そして、同じ依頼("可能ならファイルおよび文字列選択
// でのスコープ範囲内...のみでの表示")によれば、将来の選択可能な
// コンテンツにホバーした時のインジケータも、まだここには結線されて
// いない(それを駆動する、継続的なホバー対選択可能領域の追跡が
// まだ存在しない――追って対応、この依頼についての理屈はCLAUDE.md
// 自身の記述参照)。
void GraphicalDisplay::setPointingCursor(bool active) {
  if (active == impl_->pointingCursorActive) return;
  impl_->pointingCursorActive = active;
  if (active) {
    if (!impl_->pointingCursor) impl_->pointingCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    if (impl_->pointingCursor) SDL_SetCursor(impl_->pointingCursor);
  } else {
    SDL_SetCursor(SDL_GetDefaultCursor());
  }
}

// 既にグリフごとにUTF-8デコード済みの1行のテキストを、ピクセル位置
// (0, y)から描画する、renderScrolled()の1コンテンツ行ごとのループと
// その下のモードライン行の両方が使う――モードライン(2026-07-31追加)
// が、renderCell()が既に自分自身で持っているグリフ走査/フォント
// フォールバック/fitGlyphRect()のダンスの3つ目のコピーを必要としな
// いよう括り出してある(必然的に別物になっている、なぜこの経路全体
// が自己完結しているのかについてはrenderScrolled()自身のクラス
// コメント参照)。
void GraphicalDisplay::renderTextRun(int y, const std::string &line, Color fg, int startCol,
                                      const std::vector<Display::AttributeRun> &attributes) {
  SDL_Color sdlFg{fg.r, fg.g, fg.b, 255};
  int x = startCol;
  std::size_t pos = 0;
  while (pos < line.size() && x < impl_->columns) {
    unsigned char lead = static_cast<unsigned char>(line[pos]);
    int len = Utf8::sequence_length(lead);
    if (pos + len > line.size()) len = 1;
    int naturalWidth = Utf8::display_width(reinterpret_cast<const unsigned char *>(line.data() + pos), len);
    // 2026-09-25修正、直接のユーザー報告("~/Documents/A_jp.txtの70行目
    // あたり、-g起動では停止時とスクロール時でGETSEL=...の行が１文字
    // ズレる"、スクリーンショット=ha/=hb): PipeBuffer::wrapLine()は
    // タブを「U+2060 WORD JOINER(幅0)+スペース」へ置き換える(検索で
    // タブを見つけられるようにするため、自身のコメント参照)。静止時の
    // 描画はUtf8::display_width()どおり幅0として扱うが、ここは
    // std::max(1, ...)で最低1桁として数えていたため、タブを含む行だけ
    // スクロール中に1桁右へずれていた。幅0の文字は桁を進めずに飛ばす。
    if (naturalWidth == 0 && len > 1) {
      pos += len;
      continue;
    }
    int width = std::max(1, naturalWidth);
    std::string glyph = line.substr(pos, len);
    // 2026-08-21修正、直接のユーザー報告("マウスの左クリック（画面
    // ドラッグスタンバイ状態）中だと文字装飾が利かず"――HELP自身の
    // 太字/下線付きの単語、例えば"usilic"、"``USILIC''"が、ドラッグ/慣性
    // スクロールが有効な間はプレーンにレンダリングされる):これが
    // 対処する根本原因については、すぐ下のこの関数自身の
    // TTF_SetFontStyle()のコメント参照(以前は常にNORMALをレンダ
    // リングしており、グリフごとのスタイル情報がこの関数へ一切
    // 届いていなかった)。ContentView::renderLine()/TerminalDisplay::
    // writeAttributedLine()が既に使っているのと同じバイトオフセット
    // 範囲でのマッチング(`pos < run.start || pos >= run.end`――この
    // クラス自身の慣習は全体を通じて、プレーンなASCIIのHELPテキスト
    // (このように装飾される唯一のコンテンツ)についてはバイト
    // オフセット==画面上の列である)。
    bool wantBold = false, wantUnderline = false, wantStandout = false, wantItalic = false,
         wantHighlight = false, wantHighlightCurrent = false, wantSerif = false;
    int wantFg = Display::kDefaultColor, wantBg = Display::kDefaultColor;
    for (const auto &run : attributes) {
      if (static_cast<int>(pos) < run.start || static_cast<int>(pos) >= run.end) continue;
      if (run.attribute == Display::Attribute::Serif) wantSerif = true;
      else if (run.attribute == Display::Attribute::Bold) wantBold = true;
      else if (run.attribute == Display::Attribute::Underline) wantUnderline = true;
      else if (run.attribute == Display::Attribute::Standout) wantStandout = true;
      else if (run.attribute == Display::Attribute::Italic) wantItalic = true;
      else if (run.attribute == Display::Attribute::Highlight) wantHighlight = true;
      else if (run.attribute == Display::Attribute::HighlightCurrent) wantHighlightCurrent = true;
      else if (run.attribute == Display::Attribute::Color) {
        wantFg = run.fg;
        wantBg = run.bg;
      }
    }
    // 2026-09-26追加(Display::Attribute::Color自身のコメント参照): ANSIの
    // 色。renderCell()と同じく、反転表示なら前景/背景を入れ替える。
    SDL_Color runFg = wantFg != Display::kDefaultColor ? ansiColorToSdl(wantFg) : sdlFg;
    SDL_Color runBg = wantBg != Display::kDefaultColor ? ansiColorToSdl(wantBg) : impl_->bg;
    if (wantStandout) std::swap(runFg, runBg);
    if (!wantStandout && wantBg != Display::kDefaultColor) {
      SDL_Rect bgRect{Impl::kMarginX + x * impl_->cellWidth, Impl::kMarginY + y, width * impl_->cellWidth,
                      impl_->cellHeight};
      SDL_SetRenderDrawColor(impl_->renderer, runBg.r, runBg.g, runBg.b, 255);
      SDL_RenderFillRect(impl_->renderer, &bgRect);
    }
    // 2026-09-02追加、直接のユーザー依頼("ついでに透かしが消える件も
    // よろしく"): ContentView::scrollHighlightForLine()(main.cpp自身の
    // renderScrolled()呼び出し箇所のコメント参照)が注入するStandout
    // runのための反転表示――背景色を塗ってから、グリフ自身をfg/bg
    // 入れ替えて描く。ContentView::renderLine()自身のsetStandout()が
    // 端末のSGR反転表示コードで実現しているのと同じ見た目。空白セル
    // (下の`glyph != " "`ゲートで通常は完全にスキップされる)も、
    // Standout範囲内であれば背景を塗る必要がある――そのためこの背景
    // 塗りは`glyph != " "`のゲートより「前」、ループの全反復で行う。
    if (wantStandout) {
      SDL_Rect cellRect{Impl::kMarginX + x * impl_->cellWidth, Impl::kMarginY + y, width * impl_->cellWidth,
                         impl_->cellHeight};
      SDL_SetRenderDrawColor(impl_->renderer, runBg.r, runBg.g, runBg.b, 255);
      SDL_RenderFillRect(impl_->renderer, &cellRect);
    }
    // renderCellBackground()自身のkHighlightCurrentBgColorコメント
    // 参照――wantStandoutの背景塗りと同じ理屈だが、standoutと重なった
    // 場合はこちらを優先する(常に見えてほしいマッチハイライトなので、
    // 上のif/elseではなく単純に上書きする2回目の塗りにしてある)。
    // wantHighlight/wantHighlightCurrentは互いに排他。
    if (wantHighlight || wantHighlightCurrent) {
      SDL_Color hlBg = wantHighlightCurrent ? kHighlightCurrentBgColor : kHighlightBgColor;
      SDL_Rect cellRect{Impl::kMarginX + x * impl_->cellWidth, Impl::kMarginY + y, width * impl_->cellWidth,
                         impl_->cellHeight};
      SDL_SetRenderDrawColor(impl_->renderer, hlBg.r, hlBg.g, hlBg.b, 255);
      SDL_RenderFillRect(impl_->renderer, &cellRect);
    }
    if (glyph != " " && lead >= 0x20) {
      SDL_Color cellFg = runFg;
      // renderCell()自身のkHighlightCurrentFgColorコメント参照――
      // standoutによる入れ替え後に上書きする独立の色ペア。他の全ての
      // 一致(wantHighlight)は前景を変更しない。
      if (wantHighlightCurrent) cellFg = kHighlightCurrentFgColor;
      // なぜここでもフォールバックフォントが参照されるのかについては
      // renderCell()自身のコメント参照――下の`isWide`のチェックを
      // 含め、2026-08-10追加(実際の報告、"慣性スクロール中は日本語
      // もビットマップになる"): この関数には、メインフォント(ラテン)
      // が「たまたま」主張してもいる、小さな「曖昧な幅」のコードポイ
      // ントの集合(▽ U+25BD、曲線的な引用符U+201C/U+201D――これらを
      // CJKフォールバック専用として分類することがなぜ重要かについて
      // はrenderCell()自身のコメント参照:Menlo自身のそれらについての
      // 狭いグリフは見た目に一貫性が無く、fitGlyphRect()経由で2列分の
      // ボックス全体に引き伸ばされると、適切な比率のCJKグリフが必要
      // とするよりはるかに劇的に、目に見えて「よりぼやけた」/
      // より「ビットマップ」らしく見える――まさに報告された症状で
      // あり、「この」関数だけにその隙間があったため、スクロール中
      // の描画経路に特有のものだった)について、CJKフォールバックの
      // 代わりにメイン(ラテン)フォントを使ってしまう、
      // renderCell()自身の`isWide && fallbackHasIt`という優先ルール
      // が、この関数には欠けていた。
      TTF_Font *renderFont = impl_->font;
      unsigned int codepoint = decodeUtf8Codepoint(glyph);
      bool isWide = width >= 2;
      bool fallbackHasIt = glyphProvidedByFont(impl_->fallbackFont, codepoint);
      if ((isWide && fallbackHasIt) || (!glyphProvidedByFont(impl_->font, codepoint) && fallbackHasIt))
        renderFont = impl_->fallbackFont;
      if (TTF_Font *serif = wantSerif ? serifFontFor(*impl_, codepoint) : nullptr) renderFont = serif;  // 明朝体
      // renderCell()自身のもの(`if (cell.bold) style |= TTF_STYLE_BOLD;`
      // 等)と同じスタイルの計算――この関数は以前は`attributes`に
      // 関わらず常にTTF_STYLE_NORMALをレンダリングしていた(この関数
      // 自身の上の2026-08-21修正のコメント、wantBold/wantUnderlineが
      // 計算されるまさにその箇所参照)。
      int style = TTF_STYLE_NORMAL;
      if (wantBold) style |= TTF_STYLE_BOLD;
      if (wantUnderline) style |= TTF_STYLE_UNDERLINE;
      if (wantItalic) style |= TTF_STYLE_ITALIC;
      // renderCell()と同じ半角用ヒンティングの調整――それ自身のコメント
      // 参照(項目264、項目263のTTF_HINTING_LIGHTを置き換えたもの)。
      // (TTF_SetFontStyle()/TTF_SetFontHinting()自体は、下で呼ばれる
      // cachedGlyphTexture()の中へ移動した――それ自身のコメント参照。
      // これはcachedGlyphTexture()が担う3つの呼び出し箇所の中で最も
      // ホットなものである: アクティブなスクロール中の、見えている
      // 全ての行、約16msごとの慣性物理演算のスライス毎に呼ばれる――
      // それを追加する動機となったmpi/i5-9400のもっさり感の報告に
      // ついてはcachedGlyphTexture()自身のコメント参照。)
      int hinting = isWide ? TTF_HINTING_NORMAL : TTF_HINTING_NONE;
      int w = 0, h = 0;
      SDL_Texture *texture = cachedGlyphTexture(*impl_, renderFont, glyph, style, hinting, cellFg, !isWide, w, h);
      if (texture) {
        // fitGlyphRect()経由で`width`セルに合わせる――なぜこれが
        // 引き伸ばすのではなく縮小して中央寄せするのかについては
        // renderCell()自身のコメント参照。
        // wantBoldも(2026-09-25修正、直接のユーザー報告"-gの(ls)で右下の
        // 「usilic v1.5b」のフォントサイズが小さい不具合は修正済みだが
        // スクロールが始まると復活する"): c678ffbでrenderCell()側だけ
        // cell.italic || cell.boldへ広げ、ドラッグ/コースト中の描画経路
        // (renderScrolled()→ここ)は斜体のみのまま取り残されていた。
        SDL_Rect dst = fitGlyphRect(Impl::kMarginX + x * impl_->cellWidth, Impl::kMarginY + y,
                                     width * impl_->cellWidth, impl_->cellHeight, w, h, wantItalic || wantBold);
        SDL_RenderCopy(impl_->renderer, texture, nullptr, &dst);
      }
    }
    x += width;
    pos += len;
  }
}

int GraphicalDisplay::textRunDisplayWidth(const std::string &text) const {
  int width = 0;
  std::size_t pos = 0;
  while (pos < text.size()) {
    unsigned char lead = static_cast<unsigned char>(text[pos]);
    int len = Utf8::sequence_length(lead);
    if (pos + len > text.size()) len = 1;
    width += std::max(1, Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data() + pos), len));
    pos += len;
  }
  return width;
}

// スクロールバーオーバーレイの最初のスライス(docs/HISTORY.md
// 521行目/IDEAS.mdの2026-08-03の項目)――ドラッグ/慣性スクロールが
// 実際に有効な「間だけ」表示される、これがrenderScrolled()(スクロール
// 専用の描画経路)からのみ呼ばれるため;静的な(スクロールしていない)
// 場合にはDisplayインターフェースの拡張が必要、この関数自身の呼び
// 出し元のコメント参照。これは-c自身のリファレンスの挙動とも一致する、
// 実際のスクリーンショット(2026-08-07)から確認済み: 静的な、
// スクロールしていない-cのウィンドウもスクロールバーを一切表示
// しない。
//
// 見た目のスタイル: 単色のimpl_->fg(2026-08-07、以前の半透明の
// ニュートラルグレーのつまみ/トラック無しの版を置き換えたもの)、
// モードライン自身の反転表示のバー(renderModeLine()/下のモードライン
// 描画のブロック参照)と同じ色、同じ不透明度――ユーザー自身の理屈:
// 特にモードラインに合わせる(また別の固定のグレーを選ぶのではなく)
// ことで、このプロジェクトのデフォルトの緑地に黒だけでなく、「どんな」
// カラーテーマの下でも自動的に正しく見える。今回は角の丸みは無く
// プレーンな長方形――モードラインのバー自体がプレーンな長方形であり、
// どのみち色がそれに合わせられることになった以上、ユーザーは「同じ形、
// 同じ幅の長方形」を求めた。(impl_->fgを使う「最初の」試み、同日の
// 2026-08-07の少し前は、「単色の彩度の高い緑のブロック」に見えると
// して却下された――しかしその版はほぼ全高の「トラック」をそれで
// 塗りつぶしていた;モードライン自身のバーの高さの、小さなつまみ
// サイズの矩形には、同じ「色の壁」問題は無い。)
// スクロールバーの帯を、ここにあるすべての呼び出し箇所(このメソッド、
// drawIdleScrollbarImage()、そしてSDL_MOUSEBUTTONDOWNのスクロールバー
// あたり判定)がかつて個別に計算していたkMarginX + columns*cellWidth
// にではなく、ウィンドウ自身の「現在の」実際のピクセル幅
// (SDL_GetWindowSize())に固定する。この2つは通常は一致する、なぜ
// ならcreate()/手動リサイズの経路(項目425)はどちらもまさにその同じ
// 計算式「から」ウィンドウのサイズを決めるため――しかしmatchWindowSize()
// (「実はすでにGUI」オーバーレイの起動、このクラス自身の本物の端末
// のピクセルに合わせる機能)は、代わりに、kScrollbarWidthの予約領域を
// 一切意識せずに、ウィンドウを本物の端末自身の計測された外枠のピクセル
// サイズへ強制する。実際のmpi報告(docs/HISTORY.md 2026-08-24、"見た目、
// 最初から細い")と実測により、cellWidth自身の最も近い整数への丸め
// (matchWindowSize()自身のコメント)が、kMarginX*2 + columns*cellWidth
// + kScrollbarWidthが、ウィンドウの実際に合わせたcontentWidthを、
// kScrollbarWidth自体より「多く」超えるほどちょうど「上」に丸めて
// しまうことがあると判明した(そのプロファイルでは実際585対必要な597
// ――columns=80、cellWidth=7)――SDLはウィンドウ自身の境界の外には
// 決して描画しないので、予約されたスクロールバーの帯のほぼ「全体」が
// 黙って右端の外へ落ち、実際に見えるのはそのうちのわずか数pxだけに
// なっていた。代わりにウィンドウ自身の実際の幅から固定することで、
// 完全なkScrollbarWidthが常にウィンドウの内側に着地することを保証
// する、その代償として、はみ出るプロファイルでは、文字グリッドが
// その帯より数pxだけ手前で終わることがあり得る――単なる背景の隙間
// であり、スクロールバーが見えなくなることに比べればはるかに害が
// 少ない。
// ここでは末尾の`- Impl::kMarginX`の隙間は無い(2026-08-24削除、
// 「実はすでにGUI」オーバーレイをM1マシン自身の本物の端末と並べて
// 比較した実際のmpi報告、mpi4.png対mini.png: mpiのスクロールバーは
// ウィンドウの本当の右端より目に見えて数pxの隙間を残して座っていた
// のに対し、本物の端末自身のネイティブなスクロールバー――そしてそれ
// に合わせたmini自身のオーバーレイ――は隙間無くぴったり座っていた)。
// このメソッドの「以前の」版(とdrawScrollbar()自身の今は削除された
// コメント)は、その隙間を、2026-08-09の依頼("位置だけは元からある
// スクロールバーと同じにしたい")から意図的に加えていた――しかし
// オーバーレイ自身の「張りぼて」(見せかけ)のアイドル画像について
// 特に言えば、「どんな」目に見える隙間も、その目的全体を台無しに
// する: それは本物の端末自身のスクロールバーが既に座っている場所に、
// 単に「近い」のではなくピクセル単位で正確に着地しなければならない、
// さもないと、本物の端末のウィンドウと直接比較された瞬間に幻想が
// 崩れる(まさにこの報告が捕まえたもの)。このメソッドはアイドル画像、
// 実際のつまみ、そしてクリックのあたり判定に同様に共有されているので、
// 今や端にぴったり付けることは、オーバーレイの経路だけではなく一様
// に適用される。
int GraphicalDisplay::scrollbarStripLeft() const {
  int windowWidth = 0, windowHeight = 0;
  SDL_GetWindowSize(impl_->window, &windowWidth, &windowHeight);
  (void)windowHeight;
  return windowWidth - impl_->scrollbarWidth;
}

void GraphicalDisplay::drawScrollbar(double effectiveTop, int totalLines, int contentRows) {
  if (contentRows <= 0) return;
  int trackHeight = contentRows * impl_->cellHeight;

  // totalLines <= contentRowsは、文書全体が既に一度に画面に収まる
  // ことを意味する――つまみはトラック全体を埋める(スクロールする
  // 先が無い)、下で非正の範囲で割るのではなく、本物のスクロールバー
  // 自身の慣習に合わせる。
  int scrollableLines = std::max(totalLines - contentRows, 0);
  double visibleFraction = (totalLines > 0) ? std::min(1.0, static_cast<double>(contentRows) / totalLines) : 1.0;
  int thumbHeight = std::max(static_cast<int>(trackHeight * visibleFraction), std::min(trackHeight, 20));
  int maxThumbY = trackHeight - thumbHeight;
  int thumbY = (scrollableLines > 0)
                   ? std::clamp(static_cast<int>(maxThumbY * (effectiveTop / scrollableLines)), 0, maxThumbY)
                   : 0;
  // 2026-09-02訂正、直接のユーザー指摘: 以前ここにあった「2026-08-07
  // にkThumbInset=1(左右1pxずつの余白)で確定済み」という説明は誤り
  // だった――HISTORY.md:6618の「矩形採用で完了」は、その少し前の
  // やり取り(丸みを帯びたピル型に戻すべきか)についての、つまみの
  // 「形」の話であって「幅」の話ではない。つまみ幅をハイライト幅と
  // 「揃える」という実際の依頼はHISTORY.md:12711(2026-08-30、
  // "つまみ表示幅をモードラインのハイライト高さにあわせられない？")
  // であり、あの時の対応(scrollbarWidthをcellHeightへ連動させた)は
  // 予約帯自体の幅をハイライトへ揃えただけで、つまみ本体は依然として
  // 左右1pxずつのkThumbInsetの分だけ狭いままだった――この差分が
  // ユーザーに承認されたことは一度も無い。旧HISTORY.md:7118
  // ("以前はモードラインのハイライト幅で、と要望はしたけど文字サイズ
  // が変われば同幅も変わるので基準にはできない")という当時の懸念も、
  // scrollbarWidth自体が今やcellHeightへ連動している以上もはや
  // 当てはまらない。よってkThumbInsetは撤去し、つまみ幅を予約帯の
  // 全幅(scrollbarWidth、cellHeightそのもの)に一致させ、依頼通り
  // モードラインのハイライト幅と文字通り同じにする――帯の縁が見え
  // なくなる(元々1pxの余白を入れた理由)ことにはなるが、それを理由に
  // 幅の一致という明示的な依頼を上書きしない。
  constexpr int kThumbInset = 0;
  // scrollbarStripLeft()自身のコメント参照: ウィンドウの実際の右端に
  // ぴったり付ける、末尾の隙間は無い(2026-08-24)。
  int thumbX = scrollbarStripLeft() + kThumbInset;
  int thumbWidth = std::max(impl_->scrollbarWidth - kThumbInset * 2, 2);
  int thumbTop = Impl::kMarginY + thumbY;

  // プレーンな長方形――「最終」、ユーザー自身の明示的な2026-08-07の
  // 判断による("つまみは矩形採用で完了")。同日の角丸ピル形状の復活
  // (ここに一時的に再導入され、合成的な単体レンダリングで検証された)
  // は、誤読に基づいていた: その時のユーザー自身の質問("矩形描画
  // だけだと丸みを帯びたつまみ部が描けないのでは...SDL描画でも
  // できるの？")は、それと並んで議論されていた、別の、まだ作られて
  // いないアイドル状態の装飾についてのものであり、この既に決着済みの
  // 「アクティブな」つまみを見直すことについてではなかった――それが
  // 明らかになった時点で、プレーンな矩形に戻した。今後は角丸にせず
  // 据え置く;このつまみの形は、新たな明示的な依頼が無い限り、再び
  // 見直されるべきではない。
  SDL_Rect thumb{thumbX, thumbTop, thumbWidth, thumbHeight};
  SDL_SetRenderDrawColor(impl_->renderer, impl_->fg.r, impl_->fg.g, impl_->fg.b, 255);
  SDL_RenderFillRect(impl_->renderer, &thumb);
}

// 現在のスクロール位置をキャッシュする――flush()がそもそもなぜこれを
// 必要とするのかについては、このメソッド自身のヘッダコメント参照
// (renderScrolled()とは異なり、flush()自身はスクロール位置の
// パラメータを持たない)――Impl::everInteractedがtrueになった後
// にのみ使われる(それ自身のコメント参照);それより前は、flush()
// は代わりに下のdrawIdleScrollbarImage()を呼ぶ。同日の実際の報告
// ("マウス操作ON/OFFのたびに矩形つまみと張りぼて画像を切り替える
// のは無意味な気がする。つまみが消えてしまうので継続操作もできない")
// により、ドラッグの開始/停止のたびに無条件でこの2つを切り替える
// こと(このメソッドの最初の版)が実際に有害だと判明した;代わりに
// 「ユーザーがこれまでに一度でも触れたか」という一回限りのフラグで
// 切り替えを制御することで、意味が無くなる瞬間まで幻想を保ったまま
// にし(ユーザー自身の言葉、1ラウンド後にこの画像を復元しながら:
// "「実はすでにGUI」が意味を持つのは最初だけ")、その後は二度と
// 戻らない。
void GraphicalDisplay::setScrollbarHidden(bool hidden) { impl_->scrollbarHidden = hidden; }

void GraphicalDisplay::updateScrollPosition(int topIndex, int totalLines, int contentRows) {
  impl_->scrollTopIndex = topIndex;
  impl_->scrollTotalLines = totalLines;
  impl_->scrollContentRows = contentRows;
}

// 埋め込まれた3つのスライス(なぜ1つではなく3つなのかについては
// scrollbar_idle_image.hpp自身のクラスコメントで説明)を合成し、
// drawScrollbar()の実際のトラックが占めるのと「同じ」垂直方向の範囲
// (Impl::kMarginYからImpl::kMarginY + contentRows*cellHeightまで、
// 最後の行はモードラインのために予約されているためcontentRows ==
// rows-1――それ自身のtrackHeightの計算式)を埋める――元の単一画像版
// とは異なり、これは現在のプロファイル自身のコンテンツ高さを「常に」
// 正確に埋める、もはやオーバーフロー/隙間のトレードオフを受け入れる
// 必要は無い: プレーンで模様の無い中間のスライス(自身のネイティブ
// サイズより高いdstの矩形経由で描画され、SDL_RenderCopyにそれを
// スケーリングさせる)だけがサイズ変更され、一方上部/下部のスライス
// ――本物の細部(つまみ/グリップの領域、丸みを帯びたトラックの端)
// を持つ唯一の部分――は常にそれ自身のネイティブな、歪みの無い
// ピクセルサイズで描画される。
//
// drawSlice(): 小さなローカルのヘルパー(それ自身のメソッドではない
// ――ここでだけ、3回使われる)、元の2026-08-07の単一画像版の仕組みに
// 合わせている: 埋め込まれたBMPのバイト列を、SDL2のネイティブなBMP
// リーダー経由で呼び出しのたびに新たにデコードする(このファイルの
// 既存のフレームごとのテクスチャというスタイルに合わせている、
// renderCell()自身のグリフのテクスチャ参照)、実行時のファイルパスも
// 新しいライブラリ依存も無い。
void GraphicalDisplay::drawIdleScrollbarImage() {
  auto drawSlice = [&](const unsigned char *bmp, size_t bmpSize, const SDL_Rect &dst) {
    SDL_RWops *rw = SDL_RWFromConstMem(bmp, static_cast<int>(bmpSize));
    if (!rw) return;
    SDL_Surface *surface = SDL_LoadBMP_RW(rw, 1);  // 1 == rwの close/free はSDLが代わりに行う
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(impl_->renderer, surface);
    if (texture) {
      SDL_RenderCopy(impl_->renderer, texture, nullptr, &dst);
      SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
  };

  // 2026-09-02訂正: 以前はdrawScrollbar()自身のthumbXと同じ
  // scrollbarStripLeft()(windowWidth-scrollbarWidth、つまり「予約
  // されている帯」の左端)を使っていたが、これは間違い――
  // scrollbarWidthは2026-08-30以降cellHeightへ連動しており、この
  // 装飾画像自体は常に固定17px幅(kScrollbarIdleTop/Middle/
  // BottomWidth)しかない。帯の方がその17pxより広くなると、画像は
  // 帯の左端に貼られたまま右端まで届かず、ウィンドウの本物の右端から
  // `scrollbarWidth-17`px分だけ左にズレて見える――ユーザー報告の
  // "横方向に5mmほどズレている"はこれが原因(直接のユーザー訂正:
  // "スクロールバーの幅は関係ない。なぜなら右端に表示されるから。
  // 張りぼても右端に表示されればOK")。実際のつまみは帯を丸ごと塗る
  // ので幅に関わらず自然に右端フラッシュになるが、この固定幅画像は
  // 明示的に「自分自身の幅」で右端に合わせる必要がある――帯の幅
  // ではなく、測定済みwindowWidthNowとkScrollbarIdleTopWidth自身から
  // stripXを決める。
  int windowWidthNow = 0, windowHeightNow = 0;
  SDL_GetWindowSize(impl_->window, &windowWidthNow, &windowHeightNow);
  int stripX = windowWidthNow - kScrollbarIdleTopWidth;

  // kDefaultIdleImageYOffset: 実際の報告(2026-08-09、スクリーンショット、
  // "2ミリほど下がるけど下にはみ出さない")により、(当時は単一画像
  // だった)装飾が本来座るべき位置より数mm「低く」貼られていることが
  // 判明した――埋め込まれたスクリーンショット自身の上端が、目に見え
  // て「実体のある」部分より上に、既に本物のスクロールバー自身の
  // 丸みを帯びた/薄れたトラック上端の見た目を数ピクセル含んでいる
  // 可能性が高く、そのためそれをちょうどkMarginYから始めて貼ると、
  // そのような導入部を持たない本物の端末自身のスクロールバーより、
  // その分だけ低く座っているように読める。-11は、その元々の単一画像
  // について実機で正確に確認済み("USILICIDLEIMAGEYOFFSET=-11で
  // ピッタリ")である;この3スライス合成自身の「上」端に適用される
  // (上部スライスは、同じ元の画像自身の上部領域の直接の再切り出し
  // なので、同じ導入部の理屈が依然として当てはまる)――そして対称的
  // に「下」端にも(これを確認した実機のデータについては下の
  // trackBottom自身のコメント参照)。USILICIDLEIMAGEYOFFSET
  // (USILICTITLEBARHEIGHT/USILICSCROLLBARSCALEと同じ「もう一度の
  // コード変更+再ビルドの代わりに調整可能なつまみ」というパターン)
  // は、それでもこのデフォルトを完全に上書きする、これらの他のつまみ
  // と同じ理由で残されている: このサンドボックス自体では検証できない
  // ディスプレイ/フォント/プロファイルのばらつき。
  constexpr int kDefaultIdleImageYOffset = -11;
  int yOffset = kDefaultIdleImageYOffset;
  if (const char *env = std::getenv("USILICIDLEIMAGEYOFFSET")) {
    yOffset = std::atoi(env);
  }
  int trackTop = Impl::kMarginY + yOffset;
  // trackBottom -- ここに落ち着くまでに、2つの実際の報告
  // (スクリーンショット、2026-08-10)がこれを2つの異なる方法で間違え
  // ていた:
  //   1. 「以前の」版はtrackBottomをtrackTopから計算していた(つまり
  //      そこでもyOffsetを適用していた)、これは、上部スライスがそれ
  //      自身のグラデーションの導入部のためにずらされているのと同じ
  //      量だけ、下部スライスを上へ引きずってしまう――間違い、
  //      "bar-3位置が高め"。
  //   2. 「次の」版は、trackBottomを、トラックのプレーンなずらして
  //      いない下端(Impl::kMarginY + contentRows*cellHeight、最後の
  //      行はモードラインのために予約されているためcontentRows ==
  //      rows-1――drawScrollbar()自身のtrackHeightの計算式と一致)
  //      に、一切補正せずに固定していた――それでも間違い、"さっきより
  //      2,3ドット高い"、つまりbar-3.jpg自身の下端「も」、目に見えて
  //      「実体のある」部分の下に、本物のスクロールバーの導入部を
  //      数ピクセル持っている、yOffsetが上端で既に補正しているのと
  //      同じ種類のものである。
  // 2つの非常に異なるプロファイル(小さいフォントの方は+30が必要、
  // 24ptの「Novel」――それぞれcellHeight 19対29――は+40が必要)に
  // わたる実際のUSILICIDLEBOTTOMOFFSETの調整セッションにより、その
  // 補正は固定のピクセル数では全くなく、両方の場合において厳密に
  // `cellHeight - yOffset`であることが明らかになった(19-(-11)=30、
  // 29-(-11)=40、正確な一致であり、偶然ではない――元のソースの
  // スクリーンショットの両端は、おそらく同じ大きさの導入部の余白を
  // 持っており、そのため合成全体のkMarginY+rows*cellHeightという
  // 範囲を、両端対称に|yOffset|だけ外側へずらすことが、まさに両方の
  // 実データが独立に裏付けていることになる)。これにより、trackBottom
  // は単純に`Impl::kMarginY + rows*cellHeight - yOffset`になる
  // (同じことだが: モードライン行自身の上端に、contentRowsから完全な
  // 行数へ戻す分のcellHeightをもう1つ足し、yOffsetを引いたもの)
  // ――別の調整可能なつまみは不要;USILICIDLEIMAGEYOFFSETだけが今や
  // 両端を対称に駆動する。
  //
  // `Impl::kMarginY + rows*cellHeight`(ここでの「元の」計算式)は、
  // ウィンドウ自身の「設計上の」高さに過ぎず、それが実際の本物の
  // ピクセル高さと一致するのは、もっぱらcreate()/手動リサイズの経路
  // (項目425)がどちらもまさにその同じ計算式「から」ウィンドウの
  // サイズを決めているためである――scrollbarStripLeft()自身のコメント
  // が、matchWindowSize()(「実はすでにGUI」オーバーレイ、実際のmpi
  // 報告2026-08-24)の下でwidthについて既に破綻していると見つけた
  // のとまさに同じ想定である。「同じ」種類のバグがここにも当てはまる:
  // matchWindowSize()はウィンドウを本物の端末自身の計測されたピクセル
  // の「高さ」へ強制し、cellHeight自身の最も近い整数への丸め
  // (matchWindowSize()自身のコメント)は、真の小数値から十分離れた
  // ところに着地することがあり、rows*cellHeightが実際に利用可能な
  // 高さを超えてしまう――実際のmpi報告(この合成が、ウィンドウの上端
  // 付近に、それ自身の上部スライスだけとしてレンダリングされ、その
  // 下からモードラインまで何も無くなっていたプロファイル)により、
  // このメソッドがより多くの垂直方向の余地を想定していたために、
  // 中間/下部のスライスが、ウィンドウ自身の実際の下端より下、画面外
  // に配置されていたことが確認された。代わりに、ウィンドウ自身の
  // 「現在の」実際のピクセル高さ(SDL_GetWindowSize()、
  // scrollbarStripLeft()自身の修正に倣う)から固定することで、
  // trackBottomが本物のウィンドウ自身の下端を決して超えないように
  // する――ウィンドウの実際の高さがkMarginY*2+rows*cellHeightと
  // 一致する場合(通常のcreate()/手動リサイズの場合)は古い計算式と
  // 同一であり、matchWindowSize()自身の本物の端末とのピクセルの
  // 一致の下でのみ異なる。
  int trackBottom = (windowHeightNow - Impl::kMarginY) - yOffset;
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) {
      int windowHeight = Impl::kMarginY * 2 + impl_->rows * impl_->cellHeight;
      int contentRows = std::max(impl_->rows - 1, 0);
      std::fprintf(f,
                    "---drawIdleScrollbarImage--- rows=%d cellHeight=%d cellWidth=%d columns=%d "
                    "scrollbarWidth=%d kMarginX=%d yOffset=%d trackTop=%d "
                    "trackBottom=%d windowHeight=%d actualWindowHeight=%d modeLineRowTop=%d "
                    "stripX=%d windowWidthNow=%d\n",
                    impl_->rows, impl_->cellHeight, impl_->cellWidth, impl_->columns, impl_->scrollbarWidth,
                    Impl::kMarginX, yOffset, trackTop, trackBottom, windowHeight, windowHeightNow,
                    Impl::kMarginY + contentRows * impl_->cellHeight, stripX, windowWidthNow);
      std::fclose(f);
    }
  }

  SDL_Rect topDst{stripX, trackTop, kScrollbarIdleTopWidth, kScrollbarIdleTopHeight};
  drawSlice(kScrollbarIdleTopBmp, sizeof(kScrollbarIdleTopBmp), topDst);

  SDL_Rect bottomDst{stripX, trackBottom - kScrollbarIdleBottomHeight, kScrollbarIdleBottomWidth,
                      kScrollbarIdleBottomHeight};
  drawSlice(kScrollbarIdleBottomBmp, sizeof(kScrollbarIdleBottomBmp), bottomDst);

  // 上部スライスと下部スライスの間の隙間を、必要などんな高さにでも
  // プレーンな中間スライスを引き伸ばして埋める――負になる(上部/
  // 下部スライスが重なってしまう)のは、病的に短いウィンドウ(2つの
  // スライスを合わせた68pxをゆうに下回る)についてだけで、実際の
  // プロファイルではあり得ないので、クランプするのではなく単純に
  // スキップする。
  int middleTop = trackTop + kScrollbarIdleTopHeight;
  int middleHeight = (trackBottom - kScrollbarIdleBottomHeight) - middleTop;
  if (middleHeight > 0) {
    SDL_Rect middleDst{stripX, middleTop, kScrollbarIdleMiddleWidth, middleHeight};
    drawSlice(kScrollbarIdleMiddleBmp, sizeof(kScrollbarIdleMiddleBmp), middleDst);
  }
}

// ドラッグスクロールのプロトタイプの描画――なぜこれがwrite()/
// putChar()/moveCursor()の上に重ねるのではなく自己完結した経路なのか
// についてはクラスコメントとヘッダのコメント参照。`lines`(putByte()
// が使うのと同じfm::Utf8のヘルパー経由で、既にグリフごとにUTF-8
// デコード済み)の各要素を、それ自身の計算されたピクセルY位置に、
// pixelOffsetYだけずらして、どちらの端が現れつつあるかに応じて
// 1行余分に含めてレンダリングする。
//
// 最下行は`modeLineText`(ContentView自身の「現在/合計 NN%」という
// フォーマット、ModeLine参照)のために予約されており、スクロール
// されるコンテンツの一部には決してならない――ここではコンテンツは
// impl_->rows - 1行だけを占める(下のrowCountはそれに応じたサイズ)、
// あらゆるContentView自身のビューポートの高さ(Application::resize()の
// "contentHeight = rows()-1")と一致する。これより前は、コンテンツの
// ループはウィンドウの高さ「全体」にわたっていたため、ドラッグ/
// 慣性スクロールは、モードラインがあるべき場所をそのままスクロール
// して突き抜けていた(モードラインが存在してそれと衝突するようになる
// はるか前から、CLAUDE.mdに記されていた既知の隙間)。モードライン
// 自体は呼び出しのたびに新たに再描画される(pixelOffsetYではずらされ
// ない――固定の行である)ので、ドラッグ/慣性スクロール中もライブに
// 更新される、これがそもそもモードライン対応を求めた「元々の」動機
// だった(そこでスクロール状態を反映することについてはCLAUDE.mdの
// 2026-07-30の記述参照)。
void GraphicalDisplay::renderScrolled(const std::vector<Display::ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                                       const std::vector<Display::ModeLineSegment> &modeLineSegments) {
  if (!impl_->outputEnabled) return;
  SDL_SetRenderDrawColor(impl_->renderer, impl_->bg.r, impl_->bg.g, impl_->bg.b, 255);
  SDL_RenderClear(impl_->renderer);

  TTF_SetFontStyle(impl_->font, TTF_STYLE_NORMAL);
  if (impl_->fallbackFont) TTF_SetFontStyle(impl_->fallbackFont, TTF_STYLE_NORMAL);

  // 2026-09-13修正、直接のユーザー報告("-gでは慣性スクロール中、^Oh
  // トグル如何に関わらずモードラインが常時表示される"): このメソッドは
  // redraw()/ContentView::renderModeLine()とは完全に別の描画経路(下の
  // コメント群参照)のため、Application::layoutViews()のcontentHeight
  // 計算がmodeLineVisible()を尊重するようになった後も、ここの
  // contentRowsは無条件に1行を予約したままだった――ドラッグ/コースト中
  // の毎フレームがこの関数を使うため、その間だけ非表示設定が無視されて
  // 見えていた。
  int contentRows = impl_->modeLineVisible ? std::max(impl_->rows - 1, 0) : std::max(impl_->rows, 0);

  // コンテンツの描画をマージンの境界に厳密にクリップする
  // (2026-08-07、実際のユーザー報告): これが無いと、SDLのデフォルトの
  // クリップは描画対象全体になるため、部分的にスクロールした行の
  // グリフ(スクロール途中では、正当に負のyへ、あるいはコンテンツ/
  // モードラインの境界を超えて伸びることがある、下のループ参照)は、
  // 安定した縁が無いまま、薄いkMarginYの境界をそのまま突き抜けて
  // レンダリングされていた――ユーザーは最初これを「上端の行が早く
  // 消えすぎる」と読んでいたが、実際の端末と並べて比較した結果、
  // 実際に目にしていたのは、本来は固定された、決してスクロールされ
  // ないはずのマージンへコンテンツがにじみ出ていたことであり、それを
  // 知らせる目に見える境界が無かったのだと気づいた。ここで明示的な
  // クリップの矩形を設定することで、マージンを本物の安定した縁に
  // する――コンテンツは今や、pixelOffsetYに関わらず、その上/左に、
  // あるいはコンテンツ/モードラインの境界を超えて、決してレンダリング
  // されないことが保証される。
  SDL_Rect contentClip{Impl::kMarginX, Impl::kMarginY, impl_->columns * impl_->cellWidth,
                        contentRows * impl_->cellHeight};
  SDL_RenderSetClipRect(impl_->renderer, &contentClip);

  // 上方向に現れる時(pixelOffsetY > 0、コンテンツが下へずれる)は
  // 上に1行余分に、下方向に現れる時(pixelOffsetY < 0、コンテンツが
  // 上へずれる)は下に1行余分に――符号の慣習についてはヘッダの
  // コメント参照。
  int firstRow = (pixelOffsetY > 0) ? topIndex - 1 : topIndex;
  int rowCount = contentRows + 1;

  for (int i = 0; i < rowCount; i++) {
    int lineIndex = firstRow + i;
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) continue;
    int y = i * impl_->cellHeight + pixelOffsetY - ((pixelOffsetY > 0) ? impl_->cellHeight : 0);
    if (y <= -impl_->cellHeight || y >= contentRows * impl_->cellHeight) continue;  // 完全に画面外(あるいはモードラインの行に入り込む)
    // 2026-08-30、直接のユーザー報告(GUI、実際のマウスドラッグ中の
    // 表示崩れ、composeDakutenInString()/middleEllipsisTruncateByWidth()
    // 自身のコメント参照): 通常のContentView::renderLine()が既に行って
    // いる濁点合成と"..."省略切り詰めを、renderTextRun()へ渡す前に
    // ここで同じ順序(合成してから切り詰め、切り詰め自身の書記素
    // クラスタ境界判定は合成済みの方がより単純に正しく働く)で適用
    // する。
    // 2026-08-30、直接のユーザー報告(GUI、マウスクリック開放時に
    // 切り詰め位置が1文字分ずれて見える件): ContentView::renderLine()
    // 自身のellipsisBudget(cursorHighlightEnabled_有効時はcolumns()-1、
    // それ自身のコメント参照)に合わせている――Directory/Archive等の
    // 一覧系ビュー(cursorHighlightEnabled_が有効、実際にドラッグの
    // 対象になる主な相手)ではこの-1が既定であり、ここで単なる
    // impl_->columnsを使うと、ドラッグ中(このrenderScrolled()自身)と
    // ドラッグ終了直後の通常描画(redraw())とで切り詰め位置が1文字分
    // ずれ、離した瞬間に末尾付近の文字が入れ替わって見えていた。
    // 2026-09-02訂正、直接のユーザー報告(=fe/=ff、マウスクリック中に
    // DirectoryModeのls一覧行の途中へ"..."が誤挿入され、サイズ+日付
    // フィールドがファイル名と誤認されたように見える件、離した直後は
    // 末尾が欠ける件): 上のmiddleEllipsisTruncateByWidth()の呼び出しを
    // 「無条件に」全ての行へ適用していたのが誤り――「既に列内に収まっ
    // ている行には早期returnにより実質無害」という前提は、
    // PipeBuffer::wrapLine()で列幅ちょうどに事前折り返し済みのFileMode
    // 系の内容にしか成り立たず、DirectoryModeのls一覧行(折り返され
    // ない生の1行で、列幅を素で超えることが普通にある――ContentView::
    // Line::ellipsisEligible自身のコメント参照、"...DirectoryModeのls
    // 一覧/WORK/arc.Nバナーは、決してellipsisEligibleが設定されていな
    // かった")には成り立たない。ContentView::renderLine()自身は、この
    // フラグで省略記号の適用を条件分けしており(同関数のellipsisBudget
    // 判定参照)、非対象の行は"..."無しでプレーンに列幅まで描いて
    // 残りを切り落とすだけ――ここでも同じ条件分けに合わせる: 対象
    // (LocateMode/DuMode/ArchiveMode自身の離散的な名前/パス行)だけに
    // 適用し、非対象の行(DirectoryModeのls一覧、FileModeの通常内容
    // 双方を含む)は合成済みテキストをそのまま渡し、この関数が既に
    // 設定済みのSDLクリップ矩形(contentClip、上のコメント参照)による
    // 静かなピクセル単位の切り落としに任せる――2026-08-30より前の、
    // この呼び出し自体が無かった時の挙動と同じ。attributes(バイト
    // オフセット基準)は元の(未合成/未切り詰めの)ものをそのまま
    // 使う――ContentView::renderLine()自身も「切り詰め済みテキスト+
    // 元のline.attributes」という同じ組み合わせを使っており、この
    // 妥協は新しいものではない。
    std::string composed = composeDakutenInString(lines[lineIndex].text);
    if (lines[lineIndex].ellipsisEligible) {
      composed = middleEllipsisTruncateByWidth(composed, std::max(0, impl_->columns - 1));
    }
    renderTextRun(y, composed, Color{impl_->fg.r, impl_->fg.g, impl_->fg.b}, 0, lines[lineIndex].attributes);
  }

  SDL_RenderSetClipRect(impl_->renderer, nullptr);  // 下のモードライン行(と将来のどんなマージン装飾)もこれには縛られない

  // drawScrollbar()自身のコメント参照――effectiveTopは、つまみが
  // 完全な1行を超えるたびに飛ぶのではなく滑らかに追従するよう、
  // 現在の行内の副次的なpixelOffsetYで補正されたtopIndexである。
  double effectiveTop = topIndex - static_cast<double>(pixelOffsetY) / std::max(impl_->cellHeight, 1);
  // flush()自身の同じ分岐参照(収まっている間は張りぼて)。
  if (impl_->scrollbarHidden) {
    // setScrollbarHidden()参照
  } else if (static_cast<int>(lines.size()) <= contentRows) drawIdleScrollbarImage();
  else drawScrollbar(effectiveTop, static_cast<int>(lines.size()), contentRows);

  if (impl_->rows > 0 && impl_->modeLineVisible) {
    // 行全体にわたる1つの連続したバーとしてではなく、セグメントごと
    // に「別々の」反転表示の走りとして描く――これが修正する実際の
    // スクリーンショットのバグについてはDisplay::ModeLineSegment
    // 自身のコメント参照(以前は、ContentView::renderModeLine()自身の
    // フィールドごとの隙間とは異なり、組み立てられた文字列全体を
    // 1つの途切れないハイライトとして塗りつぶし+描画していた)。
    // 反転表示でないセグメント(composeModeLineSegments()がフィールド
    // の間に挿入する、プレーンな半角スペース1つ分の隙間)は塗りつぶし
    // を一切描かず、既にクリアされた背景をそのまま透けて見せる――
    // まさに参照スクリーンショットが示す小さな黒い隙間である。
    int modeLineY = contentRows * impl_->cellHeight;
    int col = 0;
    for (const auto &segment : modeLineSegments) {
      int segCols = textRunDisplayWidth(segment.text);
      if (segment.standout) {
        SDL_Rect segBox{Impl::kMarginX + col * impl_->cellWidth, Impl::kMarginY + modeLineY, segCols * impl_->cellWidth,
                         impl_->cellHeight};
        SDL_SetRenderDrawColor(impl_->renderer, impl_->fg.r, impl_->fg.g, impl_->fg.b, 255);
        SDL_RenderFillRect(impl_->renderer, &segBox);
        renderTextRun(modeLineY, segment.text, Color{impl_->bg.r, impl_->bg.g, impl_->bg.b}, col);
      } else if (segment.bold) {
        // 2026-09-03追加、直接のユーザー報告("右下透かしも消えたまま"):
        // ContentView::scrollModeLineSegments()自身が右下の"usilic
        // v1.5b"透かし用に注入するbold(反転なし)セグメント――
        // renderModeLine()自身の見た目(通常のfg色、太字)に合わせる。
        renderTextRun(modeLineY, segment.text, Color{impl_->fg.r, impl_->fg.g, impl_->fg.b}, col,
                      {{0, static_cast<int>(segment.text.size()), Display::Attribute::Bold}});
      }
      col += segCols;
    }
  }

  // flush()自身が持っているのと同じUSILICGUIDEBUGのピクセルダンプ
  // (なぜかについてはそれ自身のコメント参照: SDL_RenderReadPixels()
  // はScreen Recording権限も実際に見えるウィンドウも無く、ヘッドレスに
  // 動作するので、実際のピクセルを見る他の方法が無いサンドボックス
  // 内から描画のバグを検証するために特別に作られた)。renderScrolled()
  // はそれ自身のSDL_RenderPresent()を持つ完全に別の描画経路なので、
  // ここでも拡張された(2026-08-07)――flush()自身のダンプは、ドラッグ/
  // 慣性スクロールが有効な間は一切発火せず、これは、この仕組みが
  // 既に存在していたにも関わらず、同じセッションの少し前のスクロール
  // バー/モードラインのセグメント作業を、この方法で検証する手段が
  // 無かったことを意味していた。(flush()自身のプレーンな".bmp"に
  // 対して)".scrolled.bmp"という接尾辞にすることで、2つが互いを
  // 上書きしないようにしている。
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    int outputWidthPixels = 0, outputHeightPixels = 0;
    SDL_GetRendererOutputSize(impl_->renderer, &outputWidthPixels, &outputHeightPixels);
    SDL_Surface *shot =
        SDL_CreateRGBSurfaceWithFormat(0, outputWidthPixels, outputHeightPixels, 32, SDL_PIXELFORMAT_RGBA32);
    if (shot) {
      if (SDL_RenderReadPixels(impl_->renderer, nullptr, SDL_PIXELFORMAT_RGBA32, shot->pixels, shot->pitch) == 0)
        SDL_SaveBMP(shot, (std::string(dbg) + ".scrolled.bmp").c_str());
      SDL_FreeSurface(shot);
    }
  }

  SDL_RenderPresent(impl_->renderer);
}

}  // namespace fm
