#!/usr/bin/env python3
"""Claude CodeセッションのJSONLトランスクリプトから、docs/HISTORY.mdへ
逐語ターンを差分追記する。

Stopフック(アシスタントが1ターンの応答を終えた時点)から自動起動される
想定(標準入力にフックのJSON、`transcript_path`/`cwd`等を含む)だが、
`--transcript`/`--dry-run`等のCLI引数でも同じコアロジックを手動実行できる。

設計の詳細は .steering/20260920-history-jsonl-automation/design.md 参照。
このスクリプトが失敗しても、Claude Code本体の応答を妨げてはならない
――呼び出し側(フック)から見た終了コードは常に0にする。
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import traceback
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo

HOOKS_DIR = Path(__file__).resolve().parent
REPO_ROOT = HOOKS_DIR.parent.parent
HISTORY_MD = REPO_ROOT / "docs" / "HISTORY.md"
STATE_FILE = HOOKS_DIR / ".history_sync_state.json"
LOG_FILE = HOOKS_DIR / "history_sync.log"
JST = ZoneInfo("Asia/Tokyo")

# 「type: user」で来るが人間の発言ではない、既知のラッパータグ(先頭一致)。
# ハーネス側の仕様変更で今後も増える前提で、この一箇所にまとめておく。
SYNTHETIC_PREFIXES = (
    "<task-notification>",
    "<local-command-stdout>",
    "<local-command-stderr>",
    "<bash-stdout>",
    "<bash-stderr>",
    "<system-reminder>",
)

PASTED_CONTENT_TAG_RE = re.compile(r"</?pasted_content[^>]*>")

PREFIX = {"user": "<", "assistant": ">"}


@dataclass
class Turn:
    speaker: str  # "user" or "assistant"
    text: str
    date_jst: str  # "YYYY-MM-DD"


@dataclass
class SyncResult:
    appended_text: str
    new_offset: int
    new_last_speaker: str | None
    new_last_date: str | None
    turns_count: int = 0
    skipped_count: int = 0


def strip_pasted_content_tags(text: str) -> str:
    return PASTED_CONTENT_TAG_RE.sub("", text).strip()


def is_synthetic(text: str) -> bool:
    stripped = text.lstrip()
    return any(stripped.startswith(p) for p in SYNTHETIC_PREFIXES)


def jst_date(timestamp: str) -> str:
    dt = datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
    return dt.astimezone(JST).strftime("%Y-%m-%d")


def extract_qualifying(entry: dict) -> tuple[str, str, str] | None:
    """entryから(speaker, text, date_jst)を返す。記録対象外ならNoneを返す。"""
    if entry.get("isSidechain"):
        return None
    if entry.get("isMeta"):
        return None
    timestamp = entry.get("timestamp")
    if not timestamp:
        return None

    etype = entry.get("type")
    message = entry.get("message")
    if not isinstance(message, dict):
        return None
    content = message.get("content")

    if etype == "user":
        if not isinstance(content, str):
            return None  # tool_resultブロック列等、人間の発言ではない
        cleaned = strip_pasted_content_tags(content)
        if not cleaned or is_synthetic(cleaned):
            return None
        return ("user", cleaned, jst_date(timestamp))

    if etype == "assistant":
        if not isinstance(content, list):
            return None
        texts = [
            block.get("text", "")
            for block in content
            if isinstance(block, dict) and block.get("type") == "text"
        ]
        joined = "".join(texts).strip()
        if not joined:
            return None
        return ("assistant", joined, jst_date(timestamp))

    return None


def build_turns(entries: list[dict]) -> tuple[list[Turn], int, int]:
    """entries(新規JSONL行)から、話者交代単位のTurn列を復元する。
    連続する同一話者の発言(タスク通知等を挟んでいても間に採用条件を満たす
    相手側の発言が無ければ)は1つのTurnへ"\n\n"区切りで結合する。
    (turns, 採用したentry数, 除外したentry数) を返す。
    """
    turns: list[Turn] = []
    used = 0
    skipped = 0
    for entry in entries:
        qualifying = extract_qualifying(entry)
        if qualifying is None:
            skipped += 1
            continue
        used += 1
        speaker, text, date_jst = qualifying
        if turns and turns[-1].speaker == speaker:
            turns[-1].text += "\n\n" + text
            turns[-1].date_jst = date_jst
        else:
            turns.append(Turn(speaker=speaker, text=text, date_jst=date_jst))
    return turns, used, skipped


def format_turns(
    turns: list[Turn], last_speaker: str | None, last_date: str | None
) -> tuple[str, str | None, str | None]:
    """turns を docs/HISTORY.md 用テキストへ整形する。
    直前(過去のフック実行含む)の話者/日付との連続性を踏まえ、
    (追記すべきテキスト, 新しいlast_speaker, 新しいlast_date) を返す。
    テキストが空文字列の場合は追記不要(turnsが空だった)ことを意味する。
    """
    if not turns:
        return "", last_speaker, last_date

    lines: list[str] = []
    speaker = last_speaker
    date = last_date
    for turn in turns:
        if turn.date_jst != date:
            lines.append("")
            lines.append(turn.date_jst)
            date = turn.date_jst
            speaker = None  # 日付が変われば必ず新しい接頭辞から始める
        if turn.speaker == speaker:
            lines.append("")
            lines.append(turn.text)
        else:
            lines.append("")
            lines.append(f"{PREFIX[turn.speaker]}{turn.text}")
            speaker = turn.speaker
    return "\n".join(lines), speaker, date


def load_state() -> dict:
    if not STATE_FILE.exists():
        return {}
    try:
        return json.loads(STATE_FILE.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as exc:
        log(f"状態ファイルの読み込みに失敗、初期状態から再開します: {exc}")
        return {}


def save_state(state: dict) -> None:
    STATE_FILE.write_text(
        json.dumps(state, ensure_ascii=False, indent=2), encoding="utf-8"
    )


def log(message: str) -> None:
    timestamp = datetime.now(JST).isoformat()
    try:
        with LOG_FILE.open("a", encoding="utf-8") as fh:
            fh.write(f"[{timestamp}] {message}\n")
    except OSError:
        pass  # ログ自体の失敗でさらに落ちることは避ける


def read_new_entries(transcript_path: Path, offset: int) -> tuple[list[dict], int]:
    """transcript_pathをoffsetバイト目から読み、新規JSONL行をパースする。
    (entries, 新しいoffset) を返す。壊れたJSON行は無視してログに残す。
    """
    size = transcript_path.stat().st_size
    if offset > size:
        log(
            f"状態のoffset({offset})がファイルサイズ({size})を超えている、"
            "0から読み直します。"
        )
        offset = 0

    entries: list[dict] = []
    with transcript_path.open("rb") as fh:
        fh.seek(offset)
        raw = fh.read()
    new_offset = offset + len(raw)

    for lineno, raw_line in enumerate(raw.splitlines(), start=1):
        line = raw_line.decode("utf-8", errors="replace").strip()
        if not line:
            continue
        try:
            entries.append(json.loads(line))
        except json.JSONDecodeError as exc:
            log(f"JSONL行のパースに失敗(行{lineno}相当)、無視します: {exc}")

    return entries, new_offset


def append_to_history(text: str) -> None:
    """textをdocs/HISTORY.mdの末尾へ追記する。
    textは通常format_turns()の出力そのもので、先頭に区切り用の空行
    (空文字列の行)を1行含む形で渡ってくる想定。
    docs/HISTORY.mdは18,000行超と大きいため、全文読み込みはせず、
    末尾1バイトだけを見て改行の要不要を判断してから追記する
    (design.md「パフォーマンス考慮事項」参照)。
    """
    if not text:
        return
    needs_leading_newline = False
    if HISTORY_MD.exists() and HISTORY_MD.stat().st_size > 0:
        with HISTORY_MD.open("rb") as fh:
            fh.seek(-1, 2)
            needs_leading_newline = fh.read(1) != b"\n"
    addition = text if text.endswith("\n") else text + "\n"
    with HISTORY_MD.open("a", encoding="utf-8") as fh:
        if needs_leading_newline:
            fh.write("\n")
        fh.write(addition)


def sync_once(
    transcript_path: Path,
    state: dict,
    *,
    dry_run: bool = False,
) -> SyncResult:
    key = str(transcript_path)
    session_state = state.get(key, {})
    offset = session_state.get("byte_offset", 0)
    last_speaker = session_state.get("last_speaker")
    last_date = session_state.get("last_date")

    entries, new_offset = read_new_entries(transcript_path, offset)
    turns, used, skipped = build_turns(entries)
    text, new_last_speaker, new_last_date = format_turns(turns, last_speaker, last_date)

    if not dry_run:
        append_to_history(text)
        state[key] = {
            "byte_offset": new_offset,
            "last_speaker": new_last_speaker,
            "last_date": new_last_date,
        }
        save_state(state)

    return SyncResult(
        appended_text=text,
        new_offset=new_offset,
        new_last_speaker=new_last_speaker,
        new_last_date=new_last_date,
        turns_count=len(turns),
        skipped_count=skipped,
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--transcript", type=Path, default=None)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--since-offset", type=int, default=None)
    args = parser.parse_args(argv)

    transcript_path = args.transcript
    cwd = None

    if transcript_path is None:
        try:
            hook_input = json.load(sys.stdin)
        except (json.JSONDecodeError, ValueError):
            hook_input = {}
        transcript_path = hook_input.get("transcript_path")
        cwd = hook_input.get("cwd")
        if transcript_path is not None:
            transcript_path = Path(transcript_path)

    if transcript_path is None:
        log("transcript_pathが得られなかったため何もしません。")
        return 0

    if cwd is not None:
        resolved_cwd = Path(cwd).resolve()
        repo_root = REPO_ROOT.resolve()
        # 2026-09-20追記(直接のユーザー指摘、docs/HISTORY.md:19299直後の
        # 欠落の原因調査): Bashツールで`cd`したサブディレクトリ(例:
        # `src/cpp/src`)がその後のコマンドにも persistする(ツール自身の
        # 仕様)ため、Stopフックのcwdがリポジトリ配下のサブディレクトリの
        # ままだった数ターンが、以前は単純な不等号比較により「他プロ
        # ジェクト」と誤判定され、ログすら残さず無言でスキップされて
        # いた(実際に4ターン分がdocs/HISTORY.mdへ一切記録されなかった)。
        # リポジトリ配下(REPO_ROOT自身またはそのサブディレクトリ)なら
        # 処理対象に含め、真に無関係な他プロジェクトの場合のみスキップ
        # する。スキップした場合もログへ残す(以前は無言だった)。
        if resolved_cwd != repo_root and repo_root not in resolved_cwd.parents:
            log(f"cwd({cwd})がリポジトリ({repo_root})外のため何もしません。")
            return 0

    if not transcript_path.exists():
        log(f"transcript_pathが存在しません: {transcript_path}")
        return 0

    state = load_state()
    if args.since_offset is not None:
        state.setdefault(str(transcript_path), {})["byte_offset"] = args.since_offset

    result = sync_once(transcript_path, state, dry_run=args.dry_run)

    if args.dry_run:
        sys.stdout.write(result.appended_text)
        if result.appended_text and not result.appended_text.endswith("\n"):
            sys.stdout.write("\n")

    return 0


def run_safely(argv: list[str]) -> int:
    """main()を例外から保護する。呼び出し元(フック)から見た終了コードは
    何が起きても常に0――Claude Code本体の応答をこのスクリプトの不具合で
    妨げないための最終防衛線。
    """
    try:
        return main(argv)
    except Exception:  # noqa: BLE001 - フックからClaude Code本体を止めない
        log("未捕捉の例外:\n" + traceback.format_exc())
        return 0


if __name__ == "__main__":
    sys.exit(run_safely(sys.argv[1:]))
