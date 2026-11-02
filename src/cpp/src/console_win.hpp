#pragma once
// Windowsのネイティブのコンソール(CUI、.steering/20261102-windows-console-cui/)。VT(エスケープシーケンス)で描き、
// キーもVTの列で受ける。GUIサブシステムのusilic.exeにはコンソールが無く、consoleAvailable()がfalse。
#ifdef _WIN32

namespace fm {

bool consoleAvailable();                      // このプロセスにコンソールがあるか
bool consolePrepareOutput();                  // VT出力・UTF-8・stdoutをバイナリに(終了時に戻す)
bool consoleEnterRawInput();                  // VT入力・行編集とエコー無し。キーを受け始める
void consoleLeaveRawInput();                  // 元のモードへ(外のプログラムの間・終了時)
bool consoleRawInputActive();
bool consoleWindowSize(int *rows, int *columns);
bool consoleInputReady(int timeoutMs);        // キーのバイトかコンソールのイベントがあるか
int consoleReadByte(int timeoutMs);           // 次のバイト(無ければ-1)
bool consoleTakeResize();                     // 窓の大きさが変わった知らせを受け取る

}  // namespace fm

#endif  // _WIN32
