#pragma once

#include <vector>

namespace fm {

// SJIS/EUC/JIS/UTF-8の判定と変換、および他の全クラスがテキストを
// 正しく描画するために必要とするバイト長/表示幅の問い合わせ。
// old/src/code.cからの移植。
//
// legacyのC実装(vt_charset(0)経由でグローバル状態から現在の端末
// 文字コードを読み取る)とは異なり、ここでの各メソッドは必要な文字
// コードの文脈を全て明示的なパラメータとして受け取る。Charsetには
// 隠れたグローバル依存が無いため、それ単体で完全にユニットテスト
// 可能である――これは意図的なもの: 「このバイト範囲は何という
// エンコーディングか」と「端末がたまたま今何を表示しているか」を
// 混同することこそ、C実装を繰り返し悩ませたバグの類(K_at/0x80の
// 衝突、count_kのバイト数と幅の取り違え、等)そのものだったため。
//
// スコープに関する注記: legacyのcode.cは、この同じ判定処理の中で
// MIMEのquoted-printable/base64エンコード済みワードヘッダー
// (`=?ISO-2022-JP?B?...?=`、古いメール閲覧対応)もインラインで
// デコードしていた。それは意図的にここでは移植していない――この
// クラスを設計した時点(2026-07-21)で未使用/不要と確認済み。単純な
// ISO-2022-JPのエスケープシーケンス判定/変換(ESC $ B ... ESC ( B、
// つまり下の`Jis`)はMIMEとは無関係なので残してある。
class Charset {
 public:
  enum class Encoding {
    Ascii,
    Sjis,
    Euc,
    Jis,
    Utf8,
    Binary,
  };

  // bufのエンコーディングを推測する。terminal_encodingは現在有効な
  // 文字コード(legacyのvt_charset(0))で、これがUtf8であり、かつbufが
  // 正しい形式のUTF-8であれば、SJIS/EUCのバイト頻度ヒューリスティック
  // が動く前に、それが無条件で優先される――この先行チェックが無いと、
  // ヒューリスティックが正しいUTF-8をSJIS/EUCと誤判定し、「変換」に
  // よって壊してしまうことがある(これは実際に以前出荷されていた
  // legacyのCのバグで、CLAUDE.mdのUTF-8対応ステップ2参照)。
  // terminal_encodingがAsciiの場合、返るのは常にAsciiかBinaryのみで、
  // legacyの挙動(素のASCIIモードで動いている間は文字コードの自動
  // 切り替えを行わない)と一致する。
  //
  // control_whitelistは、バイナリでないファイルに現れることが許容
  // される('\n'以外の)制御文字バイト値の一覧、例えば"\t\r\x08"
  // ――通常はユーザーのusilicrcのCODE設定から取得する。バックスペース
  // (0x08)がここで許可されている理由は、legacy自身の特別扱いよりも
  // 狭い: このクラスは元々(2026-07-21)、legacyのnroff風の
  // オーバーストライク太字処理(CLAUDE.mdの作業ログ8番参照)を、純粋に
  // 「表示」フォーマットの関心事(PipeBuffer自身のA_md/A_md2の仕事)
  // として扱い、エンコーディング判定とは無関係とする設計だった――
  // しかし0x08をこのホワイトリストから外したままにすると、それでも
  // ここでf_bin=trueが立ってしまい、太字装飾されたEUC-JPコンテンツ
  // に対するEUC/SJIS→UTF-8変換が静かに無効化されていた(実際の
  // ユーザー報告、2026-08-01で発見: HELP.jp――節番号にこのオーバー
  // ストライクの慣習をまさに全面的に使っている――がUSILICCHARSET=utf8
  // のとき、生のEUC-JPとUTF-8バイトが混ざった文字化けとして表示されて
  // いた。つまりCLAUDE.mdの8番でlegacyのCにおいて一度既に修正済みの
  // バグと全く同じ種類のものが、表示側ではなく判定側へのこの簡略化
  // 自身の副作用によって、ここでひっそりと再発していた)。0x08を既定で
  // ホワイトリストに入れることで、PipeBufferが既に別途正しく所有して
  // いるlegacy自身の表示側のオーバーストライク特別扱いを再導入する
  // ことなく、判定側だけを修正できる。
  static Encoding detect(const unsigned char *buf, int size,
                          Encoding terminal_encoding,
                          const char *control_whitelist = "\t\r\x08");

  // 先頭バイトcで始まるUTF-8シーケンスのバイト長(1〜4)。
  static int sequence_length(unsigned char lead_byte);

  // cpにあるlenバイトのUTF-8シーケンスの表示幅(0、1、または2カラム)。
  static int display_width(const unsigned char *cp, int len);

  // バイトオフセット`pos`がマルチバイト文字の途中に位置する場合に
  // true。その位置でbufを切り詰めるとその文字を壊してしまうことを
  // 意味する。このチェックに関する限りAscii/Binary/Jisは1単位=1バイト
  // として扱う(Jisの内容は表示上の切り詰めが適用される前に必ずEucへ
  // 変換される)。Sjis/Eucは同一パリティ切り替えのヒューリスティック
  // を使う(buf[0]から始まる上位ビット付きバイトの連続は2バイト組で
  // 現れる)。Utf8はsequence_length()を使う。
  static bool splits_character(const unsigned char *buf, int pos,
                                Encoding encoding);

  // SJIS -> EUC。EUC/JISと同じJIS X 0208の文字集合で、バイト
  // エンコーディングが違うだけなので、これは純粋な算術演算――
  // コードポイントのテーブルは不要。半角カナはEUCのSS2(0x8e)接頭辞
  // 付き形式としてそのまま通過する。
  static std::vector<unsigned char> sjis_to_euc(const unsigned char *buf, int len);

  // ISO-2022-JP("JIS") -> EUC。ESCシーケンス駆動のエンコーディング
  // (ESC $ B / ESC $ @で2バイトJISモードへ入り、ESC ( B / ESC ( J
  // でASCIIへ戻る。より古いSO/SI制御コードは半角カナへの/からの
  // 切り替え)。
  static std::vector<unsigned char> jis_to_euc(const unsigned char *buf, int len);

  // EUC-JP -> UTF-8。sjis<->euc<->jis(1つの文字集合、複数のバイト
  // エンコーディング、算術変換可能)とは異なり、EUC-JPとUTF-8は
  // 重なりつつも別個のUnicode時代の文字集合を符号化しているため、
  // 実際のコードポイントテーブル(euc_uni.h、JIS X 0208<->Unicode
  // マッピングから生成)が必要になる。
  static std::vector<unsigned char> euc_to_utf8(const unsigned char *buf, int len);
};

}  // namespace fm
