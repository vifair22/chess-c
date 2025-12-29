import argparse
import sys
from pathlib import Path


def ensure_python_chess():
    try:
        import chess  # noqa: F401
        import chess.pgn  # noqa: F401
        return
    except ImportError:
        import subprocess

        subprocess.check_call([sys.executable, "-m", "pip", "install", "python-chess"])


def square_to_coords(square):
    file = square % 8
    rank_from_bottom = square // 8
    rank = 7 - rank_from_bottom  # engine rank 0 is top (black home rank)
    return file, rank


def board_rows(board):
    import chess

    rows = []
    for r in range(8):
        row = ""
        for f in range(8):
            sq = chess.square(f, 7 - r)  # chess uses rank 0 at bottom
            piece = board.piece_at(sq)
            row += piece.symbol() if piece else "."
        rows.append(row)
    return rows


def castle_string(board):
    s = ""
    import chess

    s += "K" if board.has_kingside_castling_rights(chess.WHITE) else ""
    s += "Q" if board.has_queenside_castling_rights(chess.WHITE) else ""
    s += "k" if board.has_kingside_castling_rights(chess.BLACK) else ""
    s += "q" if board.has_queenside_castling_rights(chess.BLACK) else ""
    return s or "-"


def main():
    ensure_python_chess()
    import chess.pgn

    ap = argparse.ArgumentParser(description="Convert PGN to .rep replay")
    ap.add_argument("--pgn", required=True, help="Input PGN file")
    ap.add_argument("--out", required=True, help="Output .rep file")
    ap.add_argument("--ticks-per-move", type=int, default=20, help="Timing spacing between clicks (ms)")
    ap.add_argument("--skip-expectations", action="store_true", help="Do not emit expect_* lines")
    ap.add_argument("--verify-exe", help="Optional path to chess-c.exe to verify the generated replay headlessly")
    args = ap.parse_args()

    pgn_path = Path(args.pgn)
    out_path = Path(args.out)

    with pgn_path.open() as f:
        game = chess.pgn.read_game(f)
    if game is None:
        print("No game found in PGN", file=sys.stderr)
        sys.exit(1)

    board = game.board()

    lines = []
    lines.append("CHESSC_REPLAY_V1")
    lines.append("mode chess")
    lines.append("phase play")
    lines.append(f"white_to_move {1 if board.turn else 0}")
    lines.append("turn 0")
    lines.append(f"castle {castle_string(board)}")
    ep = board.ep_square
    if ep is None:
        lines.append("en_passant -1 -1")
    else:
        ep_f, ep_r = square_to_coords(ep)
        lines.append(f"en_passant {ep_f} {ep_r}")
    lines.append("board")
    lines.extend(board_rows(board))

    # Collect events while applying moves to get final state.
    events = []
    san_moves = []
    t_ms = 0
    ply = 0
    for move in game.mainline_moves():
        f_from, r_from = square_to_coords(move.from_square)
        events.append(f"{t_ms} click {f_from} {r_from}")
        t_ms += args.ticks_per_move // 2
        f_to, r_to = square_to_coords(move.to_square)
        events.append(f"{t_ms} click {f_to} {r_to}")
        t_ms += args.ticks_per_move // 2
        san_moves.append(board.san(move))
        board.push(move)
        ply += 1

    # Expectations based on final board / PGN result.
    if not args.skip_expectations:
        lines.append(f"expect_turn {ply}")
        result_tag = game.headers.get("Result", "")
        if board.is_checkmate():
            lines.append("expect_phase checkmate")
        elif board.is_stalemate():
            lines.append("expect_phase stalemate")
        elif result_tag == "1-0":
            lines.append("expect_phase win_white_resign")
        elif result_tag == "0-1":
            lines.append("expect_phase win_black_resign")
        elif result_tag == "1/2-1/2":
            lines.append("expect_phase draw_agreed")
        else:
            lines.append("expect_phase play")
        lines.append("expect_board")
        lines.extend(board_rows(board))

    lines.append("events")
    lines.extend(events)
    lines.append("end")
    lines.append("")  # trailing newline

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote replay to {out_path} (ply={ply})")

    if args.verify_exe:
        import subprocess
        exe = str(Path(args.verify_exe))

        def run_rep(rep_path):
            cmd = [
                exe,
                "--replay",
                str(rep_path),
                "--headless",
                "--test",
            ]
            proc = subprocess.run(cmd, text=True, capture_output=True)
            out = proc.stdout + proc.stderr
            turn = None
            for line in out.splitlines():
                if line.startswith("REPLAY_TURN "):
                    try:
                        turn = int(line.split()[1])
                    except Exception:
                        pass
            return turn, out

        # First, run full replay.
        full_turn, out = run_rep(out_path)
        print(out.strip())
        if full_turn != ply:
            print(f"WARNING: full replay turn mismatch expected {ply} got {full_turn}")
            # Progressive search for first failing ply.
            tmp = Path(out_path.parent / (out_path.stem + "_tmp.rep"))
            header = []
            with out_path.open() as f:
                lines_all = f.read().splitlines()
            # header up to 'events'
            idx_events = lines_all.index("events")
            header = lines_all[:idx_events+1]
            event_lines = lines_all[idx_events+1:-1]  # exclude 'end'
            fail_at = None
            expected_turn = 0
            for i in range(2, len(event_lines)+1, 2):
                tmp_lines = header + event_lines[:i] + ["end", ""]
                tmp.write_text("\n".join(tmp_lines), encoding="utf-8")
                turn_i, _ = run_rep(tmp)
                expected_turn = i // 2
                if turn_i != expected_turn:
                    fail_at = expected_turn
                    break
            if fail_at is not None:
                print(f"First mismatch at ply {fail_at} (SAN: {san_moves[fail_at-1] if fail_at-1 < len(san_moves) else 'unknown'})")
            else:
                print("Could not isolate mismatch ply.")
            try:
                tmp.unlink()
            except Exception:
                pass


if __name__ == "__main__":
    main()
