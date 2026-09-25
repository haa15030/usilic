#pragma once

#include <vector>

namespace fm {

// 実際のmacOSシステムカーソルから抽出したピクセルデータ+クリック
// 位置("hotspot")、RGBA8888・左上原点、サイズはrgba.size() ==
// width*height*4。width==0は抽出失敗を意味する(呼び出し側はそこから
// ゴミのSDL_Cursorを組み立てるのではなく、カーソルを変更せずそのまま
// にすべき)。
struct CursorImage {
  std::vector<unsigned char> rgba;
  int width = 0;
  int height = 0;
  int hotspotX = 0;
  int hotspotY = 0;
};

// 実際のmacOSの「開いた手」カーソル(NSCursor.openHand)のピクセルを
// 実行時に抽出し、呼び出し側がそれをSDL_Cursor(SDL_CreateColorCursor)
// に包んでSDL自身のSDL_SetCursor()追跡機構経由で駆動できるようにする
// ――2026-08-10追加、[[NSCursor openHandCursor] set]を直接呼んでいた
// 「以前の」このファイルのバージョンを置き換えるもの。あの直接的な
// 方式はマウスダウン時には正しく表示されるが、実際にドラッグが動き
// 始めた瞬間に普通の矢印へ戻ってしまっていた(実際の報告:
// 「画面ドラッグを開始したとたんに矢印に戻る」): SDLのCocoaバックエンド
// 自身が、マウス移動イベントのたびに「自分が追跡しているSDL_Cursor」
// を再アサートするため、SDLが関知しない裏経路で設定したカーソルは
// 踏みつぶされてしまう。setPointingCursor()(graphical_display.cpp)は
// SDL_CreateSystemCursor()+SDL_SetCursor()がSDL自身の追跡状態と
// 同期を保つため、この問題を一度も抱えたことが無かった。開いた手の
// カーソルも同じSDL_SetCursor()経路に乗せることで同じように修正しつつ、
// 手描きの近似ではなく実際のシステム画像そのもの(稼働中のOSから
// その場で抽出)を表示できる――2回にわたる手描きの試み(角ばった
// 四角形、続いて角丸のカプセル)はどちらも実際にユーザーから却下
// されており("ロボットの手みたいなやつは却下"、続いて「さっき却下の
// ロボット指よりもひどくなった」)、同日中の鋭い指摘により、比較対象
// としていた基準カーソル(システムの指差しの手)自体、ずっと本物の、
// 描き起こしではないOS資産だったことが判明した(このマシン上で実際に
// 確認済み: /System/Library/Frameworks/ApplicationServices.framework/
// .../HIServices.framework/.../Resources/cursors/pointinghand/は
// 実在するディレクトリであり、.../cursors/openhand/も同様)。
//
// Appleのcursor.pdf(またはそのラスタライズ版)のコピーをこのリポジトリの
// どこかに埋め込むのではなく、意図的に実行時にピクセルを抽出している
// ――サードパーティのリポジトリ(いずれGitHub上で公開される)にApple
// 自身のUI資産のコピーを再配布するのはライセンス上の懸念があると
// 判断したため(2026-08-10)。起動のたびに稼働中のOSからその場で
// 読み取り・再現することで、これを完全に回避している。
//
// 他のあらゆるプラットフォームでは、空(width==0)のCursorImageを
// 返す何もしないスタブになる(mac_cursor_stub.cpp参照、そちらでは
// mac_cursor.mmの代わりにこれがコンパイルされる)――NSCursor/AppKitは
// 他のプラットフォームには単純に存在しないため。
CursorImage extractOpenHandCursorImage();

// 2026-09-25追加: GUIのビープ音。SDLにはベルの仕組みが無く、
// GraphicalDisplay::putByte()はBEL(0x07)を黙って捨てるため、macOSでは
// NSBeep()で鳴らす。macOS以外(mac_cursor_stub.cpp)では何もしない。
void systemBeep();

}  // namespace fm
