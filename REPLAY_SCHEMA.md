# Replay File Format (CHESSC_REPLAY_V1)

This document describes the replay format consumed by the engine, including required sections, field meanings, and all supported event types (including assertions for testing).

## File structure
```
CHESSC_REPLAY_V1
mode <game_mode>
phase <game_phase>
white_to_move <0|1>
turn <number>
castle <rights>
en_passant <file> <rank>
[promote_white <piece>]
[promote_black <piece>]
board
<8 rows of 8 characters>
events
<event lines...>
end
```

Sections must appear in the order above. Lines are plain ASCII, newline-separated.

## Start state fields
- `mode`: string, e.g. `chess` or `checkers`.
- `phase`: game phase string, e.g. `play`, `check`, `checkmate`, `stalemate`, `win_white_resign`, etc.
- `white_to_move`: `1` if White to move, `0` otherwise.
- `turn`: integer turn counter.
- `castle`: castling rights; any combination of `KQkq`, or `-` if none.
- `en_passant`: target square for en passant; `file rank` (0–7) or `-1 -1` if none.
- `promote_white` (optional): default promotion piece for White (uppercase, e.g. `Q`).
- `promote_black` (optional): default promotion piece for Black (lowercase, e.g. `q`).
- `board`: exactly 8 lines after the keyword `board`. Each line is 8 chars: pieces or `.` for empty. Rank 0 is top line; file 0 is leftmost char.

## Events section
Events begin after the line `events` and end at `end`. Each event line format:
```
<t_ms> <type> [args...]
```
`t_ms` is an unsigned integer timestamp in milliseconds (relative to replay start). Events are processed in file order; timestamps are informative for pacing in live replay.

Supported `type` values and arguments:
- `hover <file> <rank>`: set hover (if enabled by caller).
- `click <file> <rank>`: simulate a click on a square.
- `move <from_file> <from_rank> <to_file> <to_rank> <promo>`: apply a move directly. `promo` is a piece char or `-`.
- `expect_illegal_move <file> <rank>`: click that must NOT change state (checksum/turn unchanged).
- `assert_turn <N>`: require `turn_number == N`.
- `assert_phase <PHASE>`: require `game_phase` string match.
- `assert_castle <rights>`: require castling rights match (same format as start).
- `assert_en_passant <file> <rank>`: require en passant target match.
- `assert_legal <sel_file> <sel_rank> allowed=<list> forbidden=<list>`: check generated legal moves for the selected piece.

### assert_legal lists
- Lists are comma-separated `file:rank` pairs (0–7). Example: `allowed=1:2,2:4`. Use `-` to indicate no entries.
- `allowed`: moves that must be present. If non-empty, any legal move outside this list fails.
- `forbidden`: moves that must be absent.
- Example: `0 assert_legal 3 4 allowed=1:5,2:6 forbidden=3:3`

### Notes on coords
- Files and ranks are 0-based: file 0 = column a, rank 0 = top row of the board section.
- All file/rank args must be in [0,7] unless explicitly documented as `-1` (en passant none).

## Expectations block (optional)
Before `events`, the loader also accepts optional expectations:
```
expect_phase <PHASE>
expect_turn <N>
expect_checksum <uint32>
expect_board
<8 rows>
```
These apply after all events. If present, the engine compares final state/board.

## Validation behavior
- Unknown event types are ignored during load.
- `expect_illegal_move` compares checksum and turn before/after the click.
- Assertion failures log via SDL_Log and abort replay application (test mode).
- If `allowed` list is provided for `assert_legal`, extra legal moves not listed cause failure.

## Examples
Minimal hover+click:
```
0 hover 4 6
10 click 4 4
```

Illegal move expectation:
```
0 expect_illegal_move 6 7
10 assert_turn 0
```

Legal set assertion:
```
0 assert_legal 3 4 allowed=1:5,2:6,4:6,5:5 forbidden=3:3,3:5
```
