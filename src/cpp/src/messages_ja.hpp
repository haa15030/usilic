#pragma once

#include <string>

namespace fm {

// mode-lineのメッセージ(英語)の日本語訳(messages_ja.cpp参照)。訳が無ければそのまま。
std::string localizeMessageJa(const std::string &english);
// メッセージを日本語にするか(Applicationが、HELPの言語を決めた・^Ojで切り替えた時に設定する。既定は英語)。
void setMessagesJapanese(bool japanese);
// setMessagesJapanese()で日本語ならlocalizeMessageJa()、英語ならそのまま。GrepMode等、Application以外からも使う。
std::string localizeMessage(const std::string &english);

}  // namespace fm
