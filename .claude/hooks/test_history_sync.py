#!/usr/bin/env python3
"""history_sync.py のユニットテスト。`python3 -m unittest`で実行する。

実データ(docs/HISTORY.md・実セッションJSONL)には一切触れず、すべて
一時ディレクトリ上の合成フィクスチャで完結させる。
"""

from __future__ import annotations

import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import history_sync as hs


def entry(
    *,
    etype: str,
    timestamp: str,
    text: str | list[dict] | None = None,
    is_sidechain: bool = False,
    is_meta: bool = False,
) -> dict:
    """テスト用JSONLエントリを組み立てる小さなヘルパー。"""
    if etype == "user":
        content: object = text
    else:
        content = text if isinstance(text, list) else [{"type": "text", "text": text}]
    e = {
        "type": etype,
        "isSidechain": is_sidechain,
        "timestamp": timestamp,
        "message": {"role": etype, "content": content},
    }
    if is_meta:
        e["isMeta"] = True
    return e


class StripPastedContentTests(unittest.TestCase):
    def test_removes_wrapper_tags_keeps_inner_text(self):
        raw = '\n\n<pasted_content id="dcca">\n調査してほしい\n</pasted_content id="dcca">\n'
        self.assertEqual(hs.strip_pasted_content_tags(raw), "調査してほしい")

    def test_plain_text_untouched(self):
        self.assertEqual(hs.strip_pasted_content_tags("LOG.txt自動化から先に進めて"), "LOG.txt自動化から先に進めて")


class SyntheticDetectionTests(unittest.TestCase):
    def test_task_notification_detected(self):
        self.assertTrue(hs.is_synthetic("<task-notification>\n<task-id>x</task-id>\n</task-notification>"))

    def test_genuine_text_not_synthetic(self):
        self.assertFalse(hs.is_synthetic("LOG.txt自動化から先に進めて"))


class JstDateTests(unittest.TestCase):
    def test_same_utc_day_different_jst_day(self):
        # UTC 23:30 -> JST(+9) は翌日08:30。日またぎ変換の検証。
        self.assertEqual(hs.jst_date("2026-09-19T23:30:00.000Z"), "2026-09-20")

    def test_before_jst_midnight_stays_same_day(self):
        self.assertEqual(hs.jst_date("2026-09-19T14:00:00.000Z"), "2026-09-19")


class ExtractQualifyingTests(unittest.TestCase):
    def test_human_plain_text(self):
        e = entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="こんにちは")
        self.assertEqual(hs.extract_qualifying(e), ("user", "こんにちは", "2026-09-20"))

    def test_human_pasted_content(self):
        raw = '\n\n<pasted_content id="dcca">\n調査してほしい\n</pasted_content id="dcca">\n'
        e = entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text=raw)
        self.assertEqual(hs.extract_qualifying(e), ("user", "調査してほしい", "2026-09-20"))

    def test_task_notification_excluded(self):
        e = entry(
            etype="user",
            timestamp="2026-09-19T22:00:00.000Z",
            text="<task-notification>\n<task-id>x</task-id>\n</task-notification>",
        )
        self.assertIsNone(hs.extract_qualifying(e))

    def test_meta_skill_load_excluded(self):
        e = entry(
            etype="user",
            timestamp="2026-09-19T22:00:00.000Z",
            text=[{"type": "text", "text": "Base directory for this skill: ..."}],
            is_meta=True,
        )
        self.assertIsNone(hs.extract_qualifying(e))

    def test_tool_result_content_excluded(self):
        e = {
            "type": "user",
            "isSidechain": False,
            "timestamp": "2026-09-19T22:00:00.000Z",
            "message": {"content": [{"type": "tool_result", "content": "..."}]},
        }
        self.assertIsNone(hs.extract_qualifying(e))

    def test_sidechain_user_excluded(self):
        e = entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="サブエージェントへの指示", is_sidechain=True)
        self.assertIsNone(hs.extract_qualifying(e))

    def test_sidechain_assistant_excluded(self):
        e = entry(etype="assistant", timestamp="2026-09-19T22:00:00.000Z", text="サブエージェントの返答", is_sidechain=True)
        self.assertIsNone(hs.extract_qualifying(e))

    def test_assistant_text_blocks_joined_tool_use_and_thinking_dropped(self):
        blocks = [
            {"type": "thinking", "thinking": "内部の思考"},
            {"type": "text", "text": "調査します。"},
            {"type": "tool_use", "name": "Bash", "input": {}},
        ]
        e = entry(etype="assistant", timestamp="2026-09-19T22:00:00.000Z", text=blocks)
        self.assertEqual(hs.extract_qualifying(e), ("assistant", "調査します。", "2026-09-20"))

    def test_assistant_tool_use_only_entry_excluded(self):
        blocks = [{"type": "tool_use", "name": "Bash", "input": {}}]
        e = entry(etype="assistant", timestamp="2026-09-19T22:00:00.000Z", text=blocks)
        self.assertIsNone(hs.extract_qualifying(e))


class BuildTurnsTests(unittest.TestCase):
    def test_alternating_speakers_become_separate_turns(self):
        entries = [
            entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="質問です"),
            entry(etype="assistant", timestamp="2026-09-19T22:01:00.000Z", text="回答します"),
        ]
        turns, used, skipped = hs.build_turns(entries)
        self.assertEqual([t.speaker for t in turns], ["user", "assistant"])
        self.assertEqual(used, 2)
        self.assertEqual(skipped, 0)

    def test_task_notification_does_not_break_assistant_continuation(self):
        entries = [
            entry(etype="assistant", timestamp="2026-09-19T22:00:00.000Z", text="調査中です。"),
            entry(
                etype="user",
                timestamp="2026-09-19T22:05:00.000Z",
                text="<task-notification>\n<task-id>x</task-id>\n</task-notification>",
            ),
            entry(etype="assistant", timestamp="2026-09-19T22:06:00.000Z", text="調査結果です。"),
        ]
        turns, used, skipped = hs.build_turns(entries)
        self.assertEqual(len(turns), 1)
        self.assertEqual(turns[0].speaker, "assistant")
        self.assertEqual(turns[0].text, "調査中です。\n\n調査結果です。")
        self.assertEqual(used, 2)
        self.assertEqual(skipped, 1)

    def test_sidechain_entries_never_appear(self):
        entries = [
            entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="サブエージェントへの指示", is_sidechain=True),
            entry(etype="assistant", timestamp="2026-09-19T22:01:00.000Z", text="サブエージェントの返答", is_sidechain=True),
            entry(etype="assistant", timestamp="2026-09-19T22:02:00.000Z", text="本来の返答"),
        ]
        turns, used, skipped = hs.build_turns(entries)
        self.assertEqual(len(turns), 1)
        self.assertEqual(turns[0].text, "本来の返答")


class FormatTurnsTests(unittest.TestCase):
    def test_fresh_state_emits_date_and_prefix(self):
        turns = [hs.Turn(speaker="user", text="こんにちは", date_jst="2026-09-20")]
        text, speaker, date = hs.format_turns(turns, None, None)
        self.assertEqual(text, "\n2026-09-20\n\n<こんにちは")
        self.assertEqual(speaker, "user")
        self.assertEqual(date, "2026-09-20")

    def test_continuation_from_previous_invocation_no_new_prefix(self):
        # 直前のフック実行時点で assistant/2026-09-20 まで記録済みだったとする。
        turns = [hs.Turn(speaker="assistant", text="続きです。", date_jst="2026-09-20")]
        text, speaker, date = hs.format_turns(turns, "assistant", "2026-09-20")
        self.assertEqual(text, "\n続きです。")
        self.assertEqual(speaker, "assistant")
        self.assertEqual(date, "2026-09-20")

    def test_speaker_change_gets_new_prefix(self):
        turns = [hs.Turn(speaker="user", text="次の質問", date_jst="2026-09-20")]
        text, speaker, date = hs.format_turns(turns, "assistant", "2026-09-20")
        self.assertEqual(text, "\n<次の質問")
        self.assertEqual(speaker, "user")

    def test_date_change_starts_fresh_even_if_same_speaker(self):
        turns = [hs.Turn(speaker="assistant", text="翌日の続き", date_jst="2026-09-21")]
        text, speaker, date = hs.format_turns(turns, "assistant", "2026-09-20")
        self.assertEqual(text, "\n2026-09-21\n\n>翌日の続き")
        self.assertEqual(date, "2026-09-21")

    def test_empty_turns_produce_no_text(self):
        text, speaker, date = hs.format_turns([], "user", "2026-09-20")
        self.assertEqual(text, "")
        self.assertEqual((speaker, date), ("user", "2026-09-20"))


class ReadNewEntriesTests(unittest.TestCase):
    def setUp(self):
        # read_new_entries()はoffset超過/JSON壊れ時にlog()を呼ぶ。実リポジトリの
        # .claude/hooks/history_sync.logを汚さないよう、必ず一時ファイルへ差し替える。
        self.tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmpdir.cleanup)
        patch = mock.patch.object(hs, "LOG_FILE", Path(self.tmpdir.name) / "log.txt")
        patch.start()
        self.addCleanup(patch.stop)

    def test_reads_only_new_lines_since_offset(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "session.jsonl"
            e1 = entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="最初")
            e2 = entry(etype="assistant", timestamp="2026-09-19T22:01:00.000Z", text="次")
            path.write_text(json.dumps(e1, ensure_ascii=False) + "\n", encoding="utf-8")
            entries, offset1 = hs.read_new_entries(path, 0)
            self.assertEqual(len(entries), 1)

            with path.open("a", encoding="utf-8") as fh:
                fh.write(json.dumps(e2, ensure_ascii=False) + "\n")
            entries2, offset2 = hs.read_new_entries(path, offset1)
            self.assertEqual(len(entries2), 1)
            self.assertEqual(entries2[0]["message"]["content"][0]["text"], "次")
            self.assertGreater(offset2, offset1)

    def test_offset_beyond_file_size_falls_back_to_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "session.jsonl"
            e1 = entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="最初")
            path.write_text(json.dumps(e1, ensure_ascii=False) + "\n", encoding="utf-8")
            entries, offset = hs.read_new_entries(path, 999999)
            self.assertEqual(len(entries), 1)

    def test_malformed_json_line_is_skipped_not_fatal(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "session.jsonl"
            e1 = entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="最初")
            path.write_text(
                "{not valid json\n" + json.dumps(e1, ensure_ascii=False) + "\n",
                encoding="utf-8",
            )
            entries, _ = hs.read_new_entries(path, 0)
            self.assertEqual(len(entries), 1)


class SyncOnceEndToEndTests(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmpdir.cleanup)
        base = Path(self.tmpdir.name)
        self.history_md = base / "HISTORY.md"
        self.state_file = base / "state.json"
        self.log_file = base / "log.txt"
        self.transcript = base / "session.jsonl"

        self._patches = [
            mock.patch.object(hs, "HISTORY_MD", self.history_md),
            mock.patch.object(hs, "STATE_FILE", self.state_file),
            mock.patch.object(hs, "LOG_FILE", self.log_file),
        ]
        for p in self._patches:
            p.start()
            self.addCleanup(p.stop)

    def _write_entries(self, entries, mode="w"):
        with self.transcript.open(mode, encoding="utf-8") as fh:
            for e in entries:
                fh.write(json.dumps(e, ensure_ascii=False) + "\n")

    def test_basic_round_trip_matches_history_md_format(self):
        self._write_entries(
            [
                entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="質問です"),
                entry(etype="assistant", timestamp="2026-09-19T22:01:00.000Z", text="回答します"),
            ]
        )
        state: dict = {}
        hs.sync_once(self.transcript, state)
        content = self.history_md.read_text(encoding="utf-8")
        self.assertEqual(content, "\n2026-09-20\n\n<質問です\n\n>回答します\n")

    def test_second_run_with_no_new_entries_is_idempotent(self):
        self._write_entries(
            [entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="質問です")]
        )
        state: dict = {}
        hs.sync_once(self.transcript, state)
        first = self.history_md.read_text(encoding="utf-8")
        hs.sync_once(self.transcript, state)
        second = self.history_md.read_text(encoding="utf-8")
        self.assertEqual(first, second)

    def test_continuation_across_two_hook_invocations(self):
        # 1回目: ユーザー発言のみ記録。
        self._write_entries(
            [entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="質問です")]
        )
        state: dict = {}
        hs.sync_once(self.transcript, state)

        # 2回目: task-notificationを挟んでアシスタントが2回に分けて応答
        # (実運用でありうる、タスク通知を挟んだ複数ターンの結合を検証)。
        self._write_entries(
            [
                entry(etype="assistant", timestamp="2026-09-19T22:01:00.000Z", text="調査中です。"),
                entry(
                    etype="user",
                    timestamp="2026-09-19T22:02:00.000Z",
                    text="<task-notification>\n<task-id>x</task-id>\n</task-notification>",
                ),
                entry(etype="assistant", timestamp="2026-09-19T22:03:00.000Z", text="調査結果です。"),
            ],
            mode="a",
        )
        hs.sync_once(self.transcript, state)

        content = self.history_md.read_text(encoding="utf-8")
        self.assertEqual(
            content,
            "\n2026-09-20\n\n<質問です\n\n>調査中です。\n\n調査結果です。\n",
        )

    def test_dry_run_does_not_write_history_md_or_state(self):
        self._write_entries(
            [entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="質問です")]
        )
        state: dict = {}
        result = hs.sync_once(self.transcript, state, dry_run=True)
        self.assertFalse(self.history_md.exists())
        self.assertFalse(self.state_file.exists())
        self.assertIn("質問です", result.appended_text)

    def test_date_crossing_within_single_run(self):
        self._write_entries(
            [
                entry(etype="user", timestamp="2026-09-19T14:00:00.000Z", text="今日の質問"),
                entry(etype="assistant", timestamp="2026-09-19T15:30:00.000Z", text="翌日扱いの回答"),
            ]
        )
        state: dict = {}
        hs.sync_once(self.transcript, state)
        content = self.history_md.read_text(encoding="utf-8")
        self.assertIn("2026-09-19\n\n<今日の質問", content)
        self.assertIn("2026-09-20\n\n>翌日扱いの回答", content)


class MainAndErrorHandlingTests(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmpdir.cleanup)
        base = Path(self.tmpdir.name)
        self.history_md = base / "HISTORY.md"
        self.state_file = base / "state.json"
        self.log_file = base / "log.txt"
        self.transcript = base / "session.jsonl"

        self._patches = [
            mock.patch.object(hs, "HISTORY_MD", self.history_md),
            mock.patch.object(hs, "STATE_FILE", self.state_file),
            mock.patch.object(hs, "LOG_FILE", self.log_file),
            mock.patch.object(hs, "REPO_ROOT", base),
        ]
        for p in self._patches:
            p.start()
            self.addCleanup(p.stop)

    def _write_entries(self, entries):
        with self.transcript.open("w", encoding="utf-8") as fh:
            for e in entries:
                fh.write(json.dumps(e, ensure_ascii=False) + "\n")

    def test_cli_transcript_and_dry_run_writes_stdout_not_file(self):
        self._write_entries(
            [entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="質問です")]
        )
        with mock.patch("sys.stdout", io.StringIO()) as out:
            rc = hs.main(["--transcript", str(self.transcript), "--dry-run"])
        self.assertEqual(rc, 0)
        self.assertIn("質問です", out.getvalue())
        self.assertFalse(self.history_md.exists())

    def test_main_missing_transcript_path_in_hook_input_is_noop(self):
        with mock.patch("sys.stdin", io.StringIO(json.dumps({"cwd": str(hs.REPO_ROOT)}))):
            rc = hs.main([])
        self.assertEqual(rc, 0)
        self.assertFalse(self.history_md.exists())

    def test_main_cwd_mismatch_skips_processing(self):
        self._write_entries(
            [entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="質問です")]
        )
        hook_input = {"transcript_path": str(self.transcript), "cwd": "/some/other/project"}
        with mock.patch("sys.stdin", io.StringIO(json.dumps(hook_input))):
            rc = hs.main([])
        self.assertEqual(rc, 0)
        self.assertFalse(self.history_md.exists())

    def test_main_cwd_subdirectory_of_repo_root_still_processes(self):
        # 2026-09-20追記: Bashの`cd`でリポジトリ配下のサブディレクトリ
        # (例: src/cpp/src)へ移動したままの状態でStopフックが発火すると、
        # 以前はcwdの完全一致比較により「他プロジェクト」と誤判定され、
        # 実際のやり取りがdocs/HISTORY.mdへ無言で記録漏れしていた
        # (docs/HISTORY.md:19299直後で発覚)。サブディレクトリは処理対象に
        # 含めるべき、という回帰テスト。
        self._write_entries(
            [entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="質問です")]
        )
        subdir = hs.REPO_ROOT / "src" / "cpp" / "src"
        subdir.mkdir(parents=True)
        hook_input = {"transcript_path": str(self.transcript), "cwd": str(subdir)}
        with mock.patch("sys.stdin", io.StringIO(json.dumps(hook_input))):
            rc = hs.main([])
        self.assertEqual(rc, 0)
        self.assertIn("質問です", self.history_md.read_text(encoding="utf-8"))

    def test_main_nonexistent_transcript_is_noop(self):
        rc = hs.main(["--transcript", str(Path(self.tmpdir.name) / "missing.jsonl")])
        self.assertEqual(rc, 0)
        self.assertFalse(self.history_md.exists())

    def test_main_malformed_stdin_json_is_noop_not_fatal(self):
        with mock.patch("sys.stdin", io.StringIO("{not valid json")):
            rc = hs.main([])
        self.assertEqual(rc, 0)

    def test_run_safely_catches_exception_and_logs(self):
        with mock.patch.object(hs, "main", side_effect=RuntimeError("boom")):
            rc = hs.run_safely(["--transcript", "irrelevant"])
        self.assertEqual(rc, 0)
        self.assertIn("boom", self.log_file.read_text(encoding="utf-8"))

    def test_hook_stdin_end_to_end_appends_to_history(self):
        self._write_entries(
            [entry(etype="user", timestamp="2026-09-19T22:00:00.000Z", text="質問です")]
        )
        hook_input = {"transcript_path": str(self.transcript), "cwd": str(hs.REPO_ROOT)}
        with mock.patch("sys.stdin", io.StringIO(json.dumps(hook_input))):
            rc = hs.main([])
        self.assertEqual(rc, 0)
        self.assertIn("質問です", self.history_md.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
