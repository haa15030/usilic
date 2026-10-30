#include "mac_cursor.hpp"

// 非Darwin環境ではmac_cursor.mmの代わりにこちらがコンパイルされる
// (MakefileのDarwin限定の差し替えを参照) -- NSCursor/AppKitは他の
// プラットフォームには存在しないため。GraphicalDisplay::setHandCursor()は
// 結果を使う前にCursorImage::widthを確認するので、ここで返す空
// (全項目デフォルト)のCursorImageは「抽出不可」を示す無害な信号となり、
// カーソルは元のままになる。

namespace fm {

CursorImage extractOpenHandCursorImage() { return CursorImage{}; }

void systemBeep() {}

void activateThisApplication() {}

void forceActivateThisApplication() {}

}  // namespace fm
