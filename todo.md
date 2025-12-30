
2) Per-piece coverage
- Positions: center, edge, corner for each piece.
- Blockers: friendly/enemy on each ray/offset (sliders); adjacent blockers for pawns; mixed blockers for knights (ignored).
- Assertions: use legal set checks for each variant.
- Examples: rook (blockers above/below/left/right), bishop (diagonals), queen (combine), knight (8/4/2 moves), king (exclude enemy-adjacent/in-check squares), pawn (double/single push, captures, en passant available/unavailable, no backward moves).

3) Castling
Illegals: in-check, through-check, destination in check, path blocked, king/rook moved then moved back (rights lost), rook captured (rights lost). Legals: both sides when clear. Assertions: assert_castle rights; legal set presence/absence for castling based on rights/attacks.

4) Promotion
Legal promotions to all allowed pieces (underpromotions). Illegal: wrong rank, invalid piece, blocked path. Assertions: final piece type/board; absence of illegal promotion destinations in legal set.

5) Pins / check prevention
Multiple pin scenarios (files/diagonals) with rook/bishop/queen pinning to king; assert pinned piece cannot expose king. Discovered check vs blocking. Double check: only king moves legal. Assert_phase check after checking moves; optionally legal set empty/non-empty for mate/stalemate.

6) Misc. rule state
En passant expiry and when other captures exist. King adjacency rule. Wrong-color/out-of-turn clicks leave state unchanged.
