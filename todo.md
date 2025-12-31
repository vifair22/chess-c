1) Draw rules (chess)
- Threefold repetition detection. (done)
- Fifty-move rule counter to draw. (done)
- Insufficient material (K vs K, K+B vs K, K+N vs K) auto-draw. (done)
- Stalemate with explicit legal-set assertion (empty set). (done)

2) King adjacency / illegal king moves (chess)
- Adjacent kings disallowed; king capture through guarded square forbidden. (done)
- Legal-set pruning around opposing king. (done)

3) En passant edge cases (chess)
- En passant exposes own king (should be illegal). (done)
- En passant capturing into check. (done)
- En-passant pawn pinned (move illegal). (done)

4) Castling nuances (chess)
- Castling after rook capture/recapture restoring empty squares. (done, see chess_castle_rook_recaptured.rep)
- Castling when rook is pinned but king path clear (should be illegal). (not applicable: rook being attacked/pinned does not invalidate castling if king's squares are safe)

5) Promotion extras (chess)
- Underpromotion choices while in check. (done via chess_promotion_underpromotion_in_check.rep)
- Promotion delivering immediate mate/stalemate. (done)
- Illegal promotion to king rejected. (done via chess_promotion_illegal_king.rep)

6) Check detection edges (chess)
- Discovered checks via underpromotion. (done via chess_underpromotion_discovered_check.rep)
- Pinned piece delivering check. (done via chess_pinned_rook_delivers_check.rep)
- Double-check with blocking pieces. (done via chess_double_check_block_attempt.rep)

7) Turn/state integrity
- Wrong-color click/move leaves state unchanged. (done via chess_wrong_color_noop.rep)
- Cancel/deselect paths don't mutate board/phase. (done via chess_deselect_noop.rep)

8) Legal-set assertions sweep
- For each scenario above, assert the full legal move set for side-to-move (not just result boards).

9) Checkers… (todo)
