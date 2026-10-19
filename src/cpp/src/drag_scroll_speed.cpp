#include "drag_scroll_speed.hpp"

#include <algorithm>
#include <cstdlib>

namespace fm {

// 実機での報告(「最低速から3段上あたりで急に変化する」)を受けて、
// 使用可能な7段階から10段階へ拡張(2026-07-30)――旧テーブルは、
// 正確に計時された35msのusleep()(レベル2)から、キャリブレーション
// されていない40万回反復のビジースピン(レベル3)へ一気に飛んで
// おり、実際にどのマシンで実行してみるまでその2つが実時間でどう
// 比較されるか知る手立てが無かった。拡張により、ビジーループの
// 範囲が始まる前にusleep()のステップを2つ追加し(依然として正確に
// 計時されており曖昧さは無い)、その範囲内でもより均等に間隔を
// 空けたビジーループのステップを増やした――ただしビジーループの
// 値自体は依然として手で選んだものであり、実ハードウェアに対して
// 計測されたものではない(このプロジェクトのサンドボックス全体に、
// それを行う手立てが無い。GUI検証全般についての同じ制約について
// 繰り返し述べているCLAUDE.mdの記述参照)。USILICSCROLLDEBUG
// (app/main.cppのloadScrollDebugLevelFromEnvironment()参照)は、
// テスト対象のレベルに確実に着地できるだけの速さの実際のフリック/
// 繰り返しキー入力を必要とせずに、これを実機に対して1段階ずつ
// さらに調整できるよう、まさにこのために存在する。
//
// 2026-08-21: ユーザー自身が実機で調整した値
// ("-u 90000 20000 10000 5000 3000 2000 1000 500 200 100"、その
// CLIオーバーライド経由で実際に手を動かしてテストした後に直接
// 提供されたもの――「-u」がこれらをどう適用するかの詳細は
// applyScrollSpeedOverride()自身のコメント参照)へ置き換えた――
// あらゆる実行のたびに手で渡さなければならないものとして残すのでは
// なく、新しいコンパイル時「既定値」として焼き込んだ(これまで通り
// いつでも「-u」でさらに上書き可能)。10段階全てが今ではusleep駆動
// である(全体を通じてbusyLoop=false): applyScrollSpeedOverride()が
// 実行時の「-u」オーバーライドに対して既に適用している「対象となった
// 全てのレベルはusleepへ変換される、以前既定でビジーループだった
// ものも含めて」というルールが、今では単純にコンパイル時テーブル
// 自体についても成り立つ――これらの値がまさにそのオーバーライド
// 自身の値そのものであるため。
std::vector<ScrollStep> kScrollSpeedLevels = {
    {false, 0, 0},          // 0: 未使用、「慣性で動いていない」
    {false, 90000, 0},      // 1: 最も遅い
    {false, 20000, 0},      // 2
    {false, 10000, 0},      // 3
    {false, 5000, 0},       // 4
    {false, 3000, 0},       // 5
    {false, 2000, 0},       // 6
    {false, 1000, 0},       // 7
    {false, 500, 0},        // 8
    {false, 200, 0},        // 9
    {false, 100, 0},        // 10: 最速
};

void applyScrollSpeedOverride(const std::vector<long> &values) {
  // インデックス1から始める: インデックス0は不活性な「慣性で動いて
  // いない」プレースホルダ(こちらも!busyLoopだが、アクティブな
  // 速度になることは決して無い――kScrollSpeedLevels自身のコメント
  // 参照)であり、実際に調整可能なレベルではない。この関数の最初の
  // 版とは異なり、値はインデックス1..values.size()へ「無条件に」
  // 割り当てられる――既定でビジーループである(5以降の)レベルも
  // 含む――上書きされるたびにそれぞれをusleep駆動へ変換する
  // (busyLoopをfalseに設定し、そのレベル自身のmicrosecondsを呼び
  // 出し側の値から設定する)。「-u 10000 20000 30000」(3つの値、
  // 既存のusleep範囲であるレベル1-4にしか届かない)が知覚できる
  // 変化を生まなかったという実際のユーザー報告を受けて、これが必要
  // だと判明した: ユーザーは具体的に、5段階全てusleepの正確に計時
  // されたカーブ(「usleep最低速から5段上まで」)を望んでおり、
  // ビジーループのレベルがそもそも存在する理由(OSのsleep粒度は
  // 数ミリ秒未満の遅延を確実には出せない、kScrollSpeedLevels自身の
  // コメント参照)は「既定」テーブルだけの懸念であって、絶対的な
  // ルールではない――usleepの粒度上のトレードオフを承知の上で
  // 明示的に5番目の正確に計時されたステップを求める呼び出し側には、
  // まさにそれが与えられる。values.size()を超えるレベルは、
  // コンパイル時の既定(ビジーループかどうかは、それぞれが既に
  // そうだった通り)のまま残る。
  for (size_t i = 1; i < kScrollSpeedLevels.size() && i - 1 < values.size(); i++) {
    long value = values[i - 1];
    if (value <= 0) continue;  // 0以下: このレベルはコンパイル時の既定のまま残す、ヘッダーのコメント参照
    kScrollSpeedLevels[i].busyLoop = false;
    kScrollSpeedLevels[i].microseconds = value;
  }
}

namespace {
std::vector<ScrollStep> gArrowSpeedLevels;  // 空ならkScrollSpeedLevels(applyArrowSpeedOverride()参照)
std::vector<long> gArrowSpeedValues;
}  // namespace

void applyArrowSpeedOverride(const std::vector<long> &values) { gArrowSpeedValues = values; gArrowSpeedLevels.clear(); }

int dragArrowSpeedOffset() { return gArrowSpeedValues.empty() ? 0 : kDragArrowSpeedOffset; }

int arrowSpeedMaxLevel() {
  int max = gArrowSpeedValues.empty() ? static_cast<int>(kScrollSpeedLevels.size()) - 1
                                      : static_cast<int>(gArrowSpeedValues.size());
  // 2026-10-15にWindowsのネイティブでは1ドットの待ちが2000usec未満の段を使わないようにしたが、2026-10-16に外した
  // (直接のユーザー依頼"スクロールの最高速が遅すぎるんで規制を解除してみて。-sで調整してみたい")。
  return max;
}

const ScrollStep &arrowSpeedStep(int level) {
  int last = static_cast<int>(kScrollSpeedLevels.size()) - 1;
  if (gArrowSpeedValues.empty()) return kScrollSpeedLevels[static_cast<size_t>(std::clamp(level, 0, last))];
  // SPEED(-s等)の上書きの後に作る(起動時の順序に依らないよう、最初に使う時に)。0以下の値の段はSPEEDの段。
  if (gArrowSpeedLevels.empty()) {
    gArrowSpeedLevels.assign(gArrowSpeedValues.size() + 1, kScrollSpeedLevels[0]);
    for (size_t i = 1; i <= gArrowSpeedValues.size(); i++) {
      gArrowSpeedLevels[i] = kScrollSpeedLevels[std::min(i, static_cast<size_t>(last))];
      if (gArrowSpeedValues[i - 1] <= 0) continue;
      gArrowSpeedLevels[i].busyLoop = false;
      gArrowSpeedLevels[i].microseconds = gArrowSpeedValues[i - 1];
    }
  }
  if (level <= 0) return gArrowSpeedLevels[0];
  int index = std::clamp(level, 1, static_cast<int>(gArrowSpeedValues.size()));
  return gArrowSpeedLevels[static_cast<size_t>(index)];
}

// applyReleaseDistanceOverride()自身のヘッダーコメント参照。
// 2026-08-21: 空ではなく、ユーザー自身が実機で調整した値
// ("-i 1 2 4 8 16 30 40 50 60 70"、実際に手を動かしてテストした後に
// 直接提供されたもの)であらかじめ埋められた状態で始まる――「-u」に
// ついてkScrollSpeedLevels自身のコメントが説明しているのと同じ
// 「CLIオーバーライド自身の値を新しいコンパイル時既定として焼き
// 込む」という扱い。これが置き換える組み込みのlevelForReleaseDistance()
// の式は、今ではどの起動経路からも実質的に到達不能になっている
// (依然として本物のコードであり、ここへ明示的に空のベクタを渡せば
// 到達可能――例えば将来の「組み込みへリセットする」を意味する「-i」
// の綴り方だが、現時点ではどのCLIフラグにも配線されていない。「-i」
// 自体は「-u」と同じパース形式で常に少なくとも1つの値を要求する
// ため)。kScrollSpeedLevels/「-u」と同様、いつでも「-i」でさらに
// 上書き可能。
std::vector<long> gReleaseDistanceOverride = {1, 2, 4, 8, 16, 30, 40, 50, 60, 70};

void applyReleaseDistanceOverride(const std::vector<long> &thresholds) { gReleaseDistanceOverride = thresholds; }

const std::vector<long> &currentReleaseDistances() { return gReleaseDistanceOverride; }

int levelForReleaseDistance(int pixels, int cellHeightPixels, bool releaseBias) {
  int magnitude = std::abs(pixels);
  int maxLevel = static_cast<int>(kScrollSpeedLevels.size()) - 1;
  if (!gReleaseDistanceOverride.empty()) {
    int level = 0;
    for (long threshold : gReleaseDistanceOverride) {
      if (magnitude < threshold) break;
      level++;
    }
    return std::min(level, maxLevel);
  }
  int height = std::max(cellHeightPixels, 1);
  if (magnitude < height / 4) return 0;
  // レベル1-4(このテーブル自身の、元々usleepでキャリブレーション
  // された、正確に計時された階層――kScrollSpeedLevels自身のコメント
  // 参照)はレベルごとに倍化し続ける(height*1、*2、*4、*8)。レベル5
  // 以降(常にキャリブレーションされていないビジーループだった階層)
  // はより緩やかに成長する――2026-08-06、強く速いフリックのコースト
  // でさえ離した瞬間に目に見えて速度が「落ちる」という実際のユーザー
  // 報告を受けて開始: 素の倍化カーブでは、最速の生のレベルが
  // kReleaseWindow(150ms)というわずかな時間内にcellHeightPixelsの
  // 「数百」倍もの大きさを要求していた――例えばレベル10は
  // 256*cellHeightPixelsを必要とし、150msで実際の数千ピクセルという、
  // 実際の人間の手では到達不能なリリース速度だった――そのため、
  // どれだけ強くても現実的なフリックがテーブルのおおよそ中程を
  // 超えて着地することは決して無かった。既に2回調整済み、どちらも
  // 2026-08-06のこと: +50%(kUpperTierGrowthDivisor 2)ではまだ
  // 実際の強いフリックで最高速に届かなかった。+25%(除数4)は最高速
  // に「届いた」が、実機での追加報告により、今度は差が「無さすぎる」
  // (「同期感が今ひとつ」――フリックの力の入れ具合が、もはや結果の
  // 速度に比例していると感じられない)と分かった――最高速へ到達
  // 可能にする同じ狭い帯が、ほとんどどんな中程度に強いフリックも
  // 最高値付近に着地させてしまい、レベル1-4(依然として倍化)が
  // 持っている、広く緩やかな感触を失わせていた。+33%(除数3)は、
  // その2つのデータ点の中間点である: 依然として無理なく到達可能
  // (最高速にはcellHeightPixels=20においてmagnitude >= 378が必要、
  // 除数4の312、除数2の540と比較)でありながら、各レベル自身の
  // バケツを幾分か再び広げてより知覚しやすい段差を作る。この一連の
  // 調整全体を通して、レベル1-4の倍化ロジックには一切触れていない
  // (同じユーザーが、下のkReleaseLevelBiasが追加された後、その部分
  // は既に正しく感じられると別途確認済み)。
  constexpr int kUpperTierGrowthDivisor = 3;
  int level = 1;
  int threshold = height;
  while (level < maxLevel && magnitude >= threshold) {
    level++;
    threshold += (level <= 4) ? threshold : threshold / kUpperTierGrowthDivisor;
  }
  // +kReleaseLevelBias、速い方への一律シフト(2026-08-06、ユーザー
  // 自身が提案した修正): 「離した時のコースト速度が遅すぎる」ことの
  // より深い根本原因診断を2回試みて失敗した後(-u経由で各レベル自身の
  // 1ドットあたりの速度を上げても効果無し――「選ばれたレベルが単に
  // 遅く設定されすぎている」を除外――そして、コーストの物理ループを
  // 並べ替えて最初のドットの起動時の一時停止を取り除いても効果無し
  // ――「最初の目に見えるドットの前に死んだ一時停止がある」を除外)、
  // このサンドボックスから実際のマウスドラッグのデータをテストして
  // さらに掘り下げる手立てが無いため、ユーザーは、典型的なフリックが
  // 感覚的にそうあるべきと感じるより低いレベルへ着地する正確な
  // 根本理由が何であれ、単に距離→レベルのマッピング自体を実用的な
  // 補正として速い方へ2段階分シフトすることを提案した。既に慣性で
  // 動いている結果にのみ適用される(レベル0、「そもそもフリックと
  // 数えるには小さすぎる」は変更しない――このバイアスが適用される
  // 前には、意図的に依然として本物のフリックが必要)。また、テーブル
  // の末尾を超えるレベルを決して選ばないようmaxLevelでクランプ
  // される。
  constexpr int kReleaseLevelBias = 2;
  return std::min(level + (releaseBias ? kReleaseLevelBias : 0), maxLevel);  // releaseBiasは宣言のコメント参照
}

int levelForTerminalReleaseDistance(int cells) {
  // 離す直前のkReleaseWindow(0.15秒)に動いた行数が、それぞれいくつ以上なら段1、段2 ...か。
  static const int kThresholds[] = {1, 2, 3, 5, 8, 12, 16, 20, 25, 30};
  int magnitude = std::abs(cells);
  int level = 0;
  for (int threshold : kThresholds) {
    if (magnitude < threshold) break;
    level++;
  }
  return level;
}

ScrollStep scaledForRow(const ScrollStep &pixelStep, int dotsPerRow) {
  int factor = std::max(dotsPerRow, 1);
  return {pixelStep.busyLoop, pixelStep.microseconds * factor, pixelStep.busyIterations * factor};
}

}  // namespace fm
