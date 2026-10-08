#pragma once

#include <vector>

namespace fm {

// GUIドラッグスクロールの「慣性」(docs/ideas/IDEAS.mdの「猫の手」
// スクロール要望)における、離した時の速度→1ドット刻みの遅延への
// マッピング。CLAUDE.mdの2026-07-30の訂正の通り、作者自身の以前の
// FM-TOWNSアプリ「cb」を直接モデルにしている: cbの速度制御は、
// 最も遅い2段階にはusleep()を、それより速い全段階にはビジーループ
// によるスピン(固定sleepではなく反復回数)を使っていた――OSのsleep
// 粒度は、速い段階が必要とするサブミリ秒単位の1ドットあたりの遅延を
// 確実には出せないため。ここでの各ScrollStepは1フレームあたりの
// 遅延ではなく、スクロールの「1ドット」(pixelOffsetが1動く)あたりの
// 遅延である――これが、この仕組みを「ジャンプさせてぼかしと呼ぶ」
// 近似ではなく本物にしている点である: 呼び出し側がこれらのステップを
// 多数、タイトなループで実行すると、cbが持っていた「次の行のグリフ
// 画像が、現在の行がスクロールし終わるのとちょうど同時に完成する」
// という効果を、手作業のVRAM行シフトの代わりにGraphicalDisplay::
// renderScrolled()のピクセルオフセット合成で駆動しながら再現する。
//
// 意図的にどのマシンでも同じ感触になるようキャリブレーションして
// いない: cbの最高速度が判読できないほど速かったこと自体、それが
// 動いていたi486SX固有の性質であり、オリジナル側がそれを均そうと
// した性質のものではなかった(ユーザー自身の話によれば、これは
// バグではなく好評だった)――より高速な現代のハードウェアでは、
// 最高段階はただ単にさらに速くなる。それはその由来と一貫しており、
// 修正すべきものではない。
struct ScrollStep {
  bool busyLoop;        // false: ドットの間usleep(microseconds); true: 代わりにbusyIterationsだけスピン
  long microseconds;     // !busyLoopの時に使う
  long busyIterations;   // busyLoopの時に使う
};

// インデックス0はプレースホルダ({false,0,0})であり、アクティブな
// 速度として使われることは決して無い――呼び出し側はレベル0を
// 「慣性で動いていない」を意味するものとして使う
// (levelForReleaseDistance()参照)ので、これを実在する(不活性な)
// テーブルの要素として保持しておくことで、全ての呼び出し箇所での
// 1個ずれを避けている。
//
// (このファイルの他の全てとは異なり)意図的にconstにしていない:
// 下のapplyScrollSpeedOverride()(「-u」CLIフラグ、app/main.cpp)が、
// 実際にスクロールが起きる前の起動時に、このテーブルの要素(どれが
// usleep駆動でどれがビジーループ駆動かも含む、そちら自身のコメント
// 参照)をその場で書き換える――すぐ上のUSILICSCROLLDEBUGと同じ
// 「再コンパイル無しで調整する」という動機によるものだが、こちらは
// どの段階が選ばれるかではなく実際の遅延値についてのもの。
extern std::vector<ScrollStep> kScrollSpeedLevels;

// レベル1..values.size()を`values`で上書きする。レベル1から順に
// 割り当てる(インデックス0は不活性なプレースホルダで一切触れない)。
// 対象となった各レベルは、元々何であったかに関わらず、無条件に
// usleep駆動(busyLoop=false、microseconds=value)へ強制変換される
// ――これはビジーループの範囲(5以降)にも及び、既定でusleepである
// 4段階だけではない――理由はapplyScrollSpeedOverride()自身の.cpp側
// コメント参照(実際のユーザー自身の「-u 90000 70000 50000 30000
// 10000」、5つの値が、より狭い「レベル1..4のみ」版が知覚できる
// 変化を生まなかった後にこう広げられた理由だった: 実際のフリック/
// ドラッグが、最初の版では全く手が届かなかったレベルに着地して
// いた)。values.size()を超えるレベルはコンパイル時の既定値のまま
// 残る。`values`が空なら何もしない。0以下の値はスキップされる
// (そのレベルのコンパイル時既定値を残す)――usleep()に渡さない
// のは、0を「一切遅延無し」として扱うため。それ自体は実在する
// レベルだが、既に最速のビジーループ段階経由で到達可能なもので
// あり、呼び出し側が不正な/プレースホルダの値を静かに渡した結果
// 事故で着地すべきものではないため。
void applyScrollSpeedOverride(const std::vector<long> &values);

// 横書きの画面の慣性スクロール(矢印キー、ドラッグで始めたものも)の段ごとの速さ(usilicrcのARROW、
// 2026-10-08)。applyArrowSpeedOverride()で値を与えるまではkScrollSpeedLevels(SPEED)と同じ。0以下の値の段は
// SPEEDの段のまま。縦書きのEPUBの左右はkScrollSpeedLevelsを使う。
void applyArrowSpeedOverride(const std::vector<long> &values);
const ScrollStep &arrowSpeedStep(int level);
// 2026-10-08、直接のユーザー依頼("マウスでは設定を2つずらして25000を最低速の開始としてみてほしい...矢印キーに
// よる速度調整も慣性スクロール中に限っては設定を2つ速い方へズラしてから調整を行う"): ARROWの段をいくつ速い方へ
// ずらして使うか。マウスのドラッグで始めた慣性はkDragArrowSpeedOffset、矢印キーで始めた慣性は0(慣性の開始の
// たびに呼び出し側が設定する)。ARROWの値は10個より多くてもよく、arrowSpeedMaxLevel()がずらした後の最速の段。
constexpr int kDragArrowSpeedOffset = 2;
void setArrowSpeedOffset(int offset);
int arrowSpeedMaxLevel();

// ドラッグが離される直前の短い時間窓で移動したピクセル距離の合計を、
// 離散的なレベルへマッピングする: 0(フリックと数えるには小さすぎる
// ――残留する動きの無い普通のリリースはそのまま止まる、実際の
// トラックパッドに指を置いたままにするのと同じ)から
// kScrollSpeedLevels.size()-1(最速)まで。閾値はcellHeightPixelsに
// 比例してスケールするため、同じ物理的なフリックはフォントサイズに
// 関わらず同じように速く感じられる。方向は呼び出し側の仕事(同じ
// 合計距離の符号)――この関数は大きさだけを見る。
//
// 下のapplyReleaseDistanceOverride()が明示的な閾値を設定している
// 場合、この組み込みの倍化/除数/バイアスの式の代わりに、そちらが
// そのまま使われる――そちら自身のコメント参照。
int levelForReleaseDistance(int pixels, int cellHeightPixels);

// 上のapplyScrollSpeedOverride()の「距離」側の対応物――意図的に
// 別の関数/CLIフラグにしてある(2026-08-21、「-i」、「-u」自身の
// レベルごとの「速度」調整がこちらもカバーしていると誤解された後の、
// ユーザーからの直接の明確化: 「-uオプションはマウスホイールの
// 回転角度とスクロール速度との関係。それは解決済みだが、今イチなのは
// マウスドラッグを解除したときのカーソル移動幅とスクロール速度との
// 関係」)。「-u」は、既に選択された各レベルが「どれだけ速く」動く
// かだけを調整する。こちらは、与えられたリリース距離がそもそも
// 「どのレベル」を選ぶかを調整する――levelForReleaseDistance()自身の
// 組み込みの曲線(レベル1〜4は倍化、5以降はkUpperTierGrowthDivisorで
// 除算、さらに一律+kReleaseLevelBiasのシフト、全て実際のマシン/手
// 1つに対して一度だけ調整されたハードコードの定数)は、まさに報告が
// 「合っていない」と指摘した「カーソル移動幅⇔結果の速度」のマッピング
// であり、離した時のドラッグ自身の感じられる「勢い」(momentum)を
// 失わせている。
//
// `thresholds[k-1]`は、リリースがレベルkに到達するために必要な
// 「最小」ピクセル量(1始まり、kScrollSpeedLevels/
// applyScrollSpeedOverride()と同じ慣習――インデックス0が閾値になる
// ことは無く、レベル0は常に「thresholds[0]未満、そもそもフリックで
// はない」を意味する)。大きさがthresholds[0]未満ならレベル0を返す。
// 最後に与えられた閾値以上なら、到達した閾値の数を返す(組み込みの
// 式と同様kScrollSpeedLevels.size()-1で頭打ち)。設定されている場合
// (空でない場合)、levelForReleaseDistance()自身の式を完全に
// 「置き換える」(その上にさらにkReleaseLevelBiasのシフトを重ねる
// ことはしない――与えられた閾値には、呼び出し側が各レベルをどこへ
// 着地させたいかが既にエンコードされており、その上に2つ目の調整を
// 重ねるとかえって値の意味が追いにくくなるだけ)。`thresholds`が
// 空なら何もしない(組み込みの式へフォールバックする)。
// applyScrollSpeedOverride()自身の「値ごとに<=0ならスキップ」とは
// 異なり、ここでは全ての値が与えられた通りに使われる――距離側には
// サブマイクロ秒粒度の懸念を回避する必要が無いため、呼び出し側が
// 与えた0や負の閾値もそのまま額面通り受け取る(意味のある挙動には
// 各閾値が厳密に増加している必要があるが、それは「-u」に妥当な値を
// 選ぶのと同様、呼び出し側自身の責任である)。
void applyReleaseDistanceOverride(const std::vector<long> &thresholds);
// 2026-10-02追加(usilicrcのDRAG、-Dでの書き込み用): 今の閾値(applyReleaseDistanceOverride()で
// 入れたもの、既定はユーザーが実機で調整した値)。
const std::vector<long> &currentReleaseDistances();

// 1「ピクセル」あたりのScrollStep(kScrollSpeedLevels参照、
// GraphicalDisplayのドット単位のrenderScrolled()向けに調整済み)を、
// サブ行アドレス指定が全く無い実端末のコースト(app/main.cppの
// runTerminalCoast/runCoastLoop)向けの1「行」あたりの遅延へ
// スケールアップする――そちらでは1行をまたぐことがcellHeightPixels
// 個のステップではなく「1つの」ステップになるため、GUIモードでの
// ピクセルテーブルと同じ「1秒あたりの行数」レートを出すには、その
// 遅延をおおよそdotsPerRow倍長くする必要がある。dotsPerRowの既定は
// 16、このプロジェクト全体がオリジナルのFM-TOWNS「cb」以来使って
// いる「文字は16ドットの高さ」という慣習(CLAUDE.md参照)――実際の
// 端末では本物のフォントメトリクスは入手できない(フレームバッファ
// アクセスが一切無い、GraphicalDisplayが存在する理由についての
// CLAUDE.mdの2026-07-30の記述参照)ため、これは必然的に近似である。
// ユーザーが、端末のコーストの最低速度が、彼らが「ちょうどいい」と
// 呼んだGUIのマウスドラッグ自身の最低速度と比べてはるかに速く
// 感じられると報告したのを受けて発見・追加された――スケール前は、
// レベル1の90msの遅延が、1ドットではなく行全体に費やされて
// いた。つまりおよそ16倍速すぎた。
ScrollStep scaledForRow(const ScrollStep &pixelStep, int dotsPerRow = 16);

}  // namespace fm
