#include "mac_font_resolver.hpp"

#ifdef __APPLE__

#include <fstream>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>

namespace fm {

namespace {

// まともに新しいmacOSであれば、Terminal.appの既定フォントはApple自身
// の「SF Mono」だが、下のCTFontDescriptorCreateWithNameAndSize()では
// これを絶対に解決できない(2026-08-03に実機確認済み): macOSはこの
// ファミリーに対する通常の名前ベースの検索を意図的に拒否している
// ――このマシン上での実際のライブテストでは、"SFMono-Regular"に対して
// 空のパスが返り(エラーは無く、静かに失敗するだけ)、ドット始まりの
// プライベート名バリアント".SFMono-Regular"に対しては
// */System/Library/Fonts/Supplemental/Times New Roman.ttf*という
// 奇妙な、全く無関係な「代替」フォントが返ってきた。Consoleのログ:
// "Client requested name '.SFMono-Regular', it will get
// TimesNewRomanPSMT rather than the intended font. All system UI font
// access should be through proper APIs such as
// CTFontCreateUIFontForLanguage() or +[NSFont systemFontOfSize:]."
// ――つまりこれは、一握りの予約済みシステムフォント名に対するApple
// 自身の意図的なAPI誤用防止策であり、このプロジェクト自身の検索
// ロジックのバグではない。
//
// resolveGuiFontPath()の呼び出し元には、この関数全体が失敗した場合の
// 汎用フォールバック(Menlo.ttc)が既にあるので、ここで失敗しても
// 即クラッシュにはならない――しかし実際のユーザー報告(2026-08-03、
// 「フォントの種類は合っているようだがサイズの取得がNG」): Menloは
// SF Mono「に似て見える」(どちらもApple自身の等幅書体)が、字形の
// 寸法までは同一ではない。そのため、Menloの「M」グリフから計算した
// GUIウィンドウ自身のcellWidth/cellHeight(実機確認: 14ptで8px)は、
// Terminal.app自身が実際に使っているSF Monoベースのセルサイズ
// (実機確認: 14ptで9px、実際のウィンドウが報告するピクセル寸法に
// はるかに近い一致)と一致しなかった――「同じフォントに見える」
// 程度には近いが、正しいサイズのウィンドウを生成するには不十分
// だった。
//
// 警告メッセージが指し示す正規のAPI(+[NSFont systemFontOfSize:]等)
// はObjective-C/AppKitのものであり、このプロジェクトの(それ以外は
// 素のC++の)コードベースにはまだそれへの橋渡しが無い――この1つの
// 検索のためだけにObjective-C++(.mm)ファイルを追加するのではなく、
// SF Mono自身のファイルは、それを持つあらゆるMacにおいて単純に
// 固定の、バージョンを通じて安定したシステムパスに常にインストール
// されている(`find /System/Library/Fonts -iname "*sfmono*"`で実機
// 確認済み)ため、そのパスを直接チェックすることで、ブロックされた
// APIを完全に迂回できる。Regularウェイトだけが自分専用の別ファイルを
// 持つ――Boldは、読み込まれたどのフォントに対しても実行時の
// TTF_STYLE_BOLDフラグとして適用される(GraphicalDisplay::renderCell()
// 参照)ものであり、別のフォントファイルでは無い。このプロジェクトが
// 読み込む他の全フォントの扱い方と同じ。
bool isSFMono(const std::string &fontName) {
  return fontName == "SFMono-Regular" || fontName == ".SFMono-Regular" || fontName == "SF Mono" ||
         fontName == "SF Mono Regular";
}

}  // namespace

std::string resolveMacFontPath(const std::string &fontName, double pointSize) {
  if (isSFMono(fontName)) {
    static const char *const path = "/System/Library/Fonts/SFNSMono.ttf";
    std::ifstream probe(path, std::ios::binary);
    if (probe.good()) return path;
  }

  CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, fontName.c_str(), kCFStringEncodingUTF8);
  if (!name) return "";
  CTFontDescriptorRef descriptor = CTFontDescriptorCreateWithNameAndSize(name, static_cast<CGFloat>(pointSize));
  CFRelease(name);
  if (!descriptor) return "";

  CFURLRef url = static_cast<CFURLRef>(CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute));
  CFRelease(descriptor);
  if (!url) return "";

  char pathBuf[1024];
  bool ok = CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<UInt8 *>(pathBuf), sizeof(pathBuf));
  CFRelease(url);
  return ok ? std::string(pathBuf) : "";
}

}  // namespace fm

#else

namespace fm {

std::string resolveMacFontPath(const std::string &, double) { return ""; }

}  // namespace fm

#endif
