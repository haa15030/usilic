#pragma once

#include <optional>
#include <string>

namespace fm {

struct MouseSgrEvent {
  int column = 0;  // 0始まり(ワイヤ形式では1始まり)
  int row = 0;      // 0始まり
  int button = 0;   // Cb & 0x3: 0=左, 1=中, 2=右(motionOnlyかつボタン押下無しの時は無意味)
  bool motionOnly = false;  // Cb & 0x20: ドラッグ/ホバーの移動レポートであり、押下/離す境界ではない
  bool pressed = false;     // 末尾が'M'ならtrue(押下、またはボタン押下中の移動)、'm'ならfalse(離す)
  // 2026-10-01追加、直接のユーザー報告("マウスのホイールでスクロールを開始しようとするとドラッグ
  // していないのに画面がマウスの動きに追随する"): ホイールの1刻み(Cb & 0x40、64=上・65=下)。
  // -1は上(前の行へ)、1は下、0はホイールではない。以前はCb & 0x3だけを見て64を左ボタンの押下と
  // 読み違え、離す合図の来ないドラッグを始めていた。ホイールの時buttonは-1(どのボタンでもない)。
  int wheel = 0;
};

// xterm SGR拡張マウスレポート(モード\x1b[?1006h、ボタン押下無しの
// ホバーレポート用に\x1b[?1003hと組み合わせる)のパラメータ+終端部分を
// パースする: 呼び出し側は先頭の"\x1b[<"を既に読み終えており、そこから
// 最後の'M'/'m'バイトまでの全てをこの関数へ渡す。例えば列45・行12
// (ワイヤ上は1始まり、返り値はDisplay::moveCursor()自身の座標系に
// 合わせて0始まり)での左ボタン押下なら"0;45;12M"。`raw`が正しい形式
// でなければnulloptを返す――受信バイト列がそもそもマウスレポート
// なのかどうかを覗き見て判断する呼び出し側(app/main.cppの
// peekMouseSgr()参照)は、その場合そのバイト列を「マウスレポートで
// なかった」ものとして扱う責任を負う。
//
// 2026-07-30の実端末ドラッグスクロール契機(CLAUDE.md参照)のために
// 新設――legacyのusilicにはマウス入力をパースする仕組みは一切無い。
std::optional<MouseSgrEvent> parseMouseSgr(const std::string &raw);

}  // namespace fm
