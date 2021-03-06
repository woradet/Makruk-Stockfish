/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2020 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cassert>

#include "bitboard.h"
#include "endgame.h"
#include "movegen.h"
#include "uci.h"

using std::string;

namespace {

  // Table used to drive the king towards the edge of the board
  // in KX vs K endgames.
  constexpr int PushToEdges[SQUARE_NB] = {
    100, 90, 80, 70, 70, 80, 90, 100,
     90, 70, 60, 50, 50, 60, 70,  90,
     80, 60, 40, 30, 30, 40, 60,  80,
     70, 50, 30, 20, 20, 30, 50,  70,
     70, 50, 30, 20, 20, 30, 50,  70,
     80, 60, 40, 30, 30, 40, 60,  80,
     90, 70, 60, 50, 50, 60, 70,  90,
    100, 90, 80, 70, 70, 80, 90, 100
  };

  // Corn
  constexpr int PushToCorn[SQUARE_NB] = {
    200, 150, 100, 70, 70, 100, 150, 200,
    150,  70,  60, 50, 50,  60,  70, 150,
    100,  60,  40, 30, 30,  40,  60, 100,
     70,  50,  30, 20, 20,  30,  50,  70,
     70,  50,  30, 20, 20,  30,  50,  70,
    100,  60,  40, 30, 30,  40,  60, 100,
    150,  70,  60, 50, 50,  60,  70, 150,
    200, 150, 100, 70, 70, 100, 150, 200
  };

  // Table used to drive the king towards the edge of the board
  // in KBQ vs K.
  constexpr int PushToOpposingSideEdges[SQUARE_NB] = {
     30,  5,  3,  0,  0,  3,  5,  30,
     40, 20,  5,  0,  0,  5, 20,  40,
     50, 30, 10,  3,  3, 10, 30,  50,
     60, 40, 20,  7,  7, 20, 40,  60,
     70, 50, 30, 20, 20, 30, 50,  70,
     80, 60, 40, 30, 30, 40, 60,  80,
     90, 70, 60, 50, 50, 60, 70,  90,
    100, 90, 80, 70, 70, 80, 90, 100
  };

  // Table used to drive the king towards a corner
  // of the same color as the queen in KNQ vs K endgames.
  constexpr int PushToQueenCorners[SQUARE_NB] = {
    100, 90, 80, 70, 50, 30,  0,   0,
     90, 70, 60, 50, 30, 10,  0,   0,
     80, 60, 40, 30, 10,  0, 10,  30,
     70, 50, 30, 10,  0, 10, 30,  50,
     50, 30, 10,  0, 10, 30, 50,  70,
     30, 10,  0, 10, 30, 40, 60,  80,
      0,  0, 10, 30, 50, 60, 70,  90,
      0,  0, 30, 50, 70, 80, 90, 100
  };

  // Tables used to drive a piece towards or away from another piece
  constexpr int PushClose[8] = { 0, 0, 100, 80, 60, 40, 20, 10 };
  constexpr int PushAway [8] = { 0, 5, 20, 40, 60, 80, 90, 100 };
  constexpr int PushWin[8] = { 0, 120, 100, 80, 60, 40, 20, 10 };

#ifndef NDEBUG
  bool verify_material(const Position& pos, Color c, Value npm, int pawnsCnt) {
    return pos.non_pawn_material(c) == npm && pos.count<PAWN>(c) == pawnsCnt;
  }
#endif

} // namespace


/// Endgames members definitions

Endgames::Endgames() {

  add<KNNK>("KNNK");
  add<KQQK>("KMMK");
  add<KQPK>("KMPK");
  add<KPPK>("KPPK");
  add<KNK>("KNK");
  add<KBK>("KSK");
  add<KQK>("KMK");
  add<KPK>("KPK");
  add<KNKB>("KNKS");
  add<KNKQ>("KNKM");
  add<KBKQ>("KSKM");
  add<KNKP>("KNKP");
  add<KBKP>("KSKP");
  add<KQKP>("KMKP");
  add<KBQK>("KSMK");
  add<KNQK>("KNMK");
  add<KRKN>("KRKN");
}


/// Mate with KX vs K. This function is used to evaluate positions with
/// king and plenty of material vs a lone king. It simply gives the
/// attacking side a bonus for driving the defending king towards the edge
/// of the board, and for keeping the distance between the two kings small.
template<>
Value Endgame<KXK>::operator()(const Position& pos) const {

  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  // Stalemate detection with lone king
  if (pos.side_to_move() == weakSide && !MoveList<LEGAL>(pos).size())
      return VALUE_DRAW;

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToEdges[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if(pos.count<BISHOP>(strongSide) >= 1)
    result += PushToEdges[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide) >= 1)
    result += PushToEdges[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  if (   (pos.count<ROOK>(strongSide) >= 1)
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1) && (pos.count<BISHOP>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1) && (pos.count<BISHOP>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<BISHOP>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<BISHOP>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<BISHOP>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<BISHOP>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1))
      || (pos.count<BISHOP>(strongSide) == 2)
      || ((pos.count<BISHOP>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<KNIGHT>(strongSide) == 1) && (pos.count<QUEEN>(strongSide) >= 2))
      || ((pos.count<KNIGHT>(strongSide) == 2) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count< QUEEN>(strongSide) >= 3)
        && ( DarkSquares & pos.pieces(strongSide, QUEEN))
        && (~DarkSquares & pos.pieces(strongSide, QUEEN))))
        result = std::min(result + VALUE_KNOWN_WIN, VALUE_MATE_IN_MAX_PLY - 1);
  {
      bool dark  =  DarkSquares & pos.pieces(strongSide, QUEEN);
      bool light = ~DarkSquares & pos.pieces(strongSide, QUEEN);

      if ((pos.count< QUEEN>(strongSide) >= 3)
          && !pos.count<ROOK>(strongSide)
          && !pos.count<KNIGHT>(strongSide)
          && !pos.count<BISHOP>(strongSide)
          &&(!dark || !light))
          return VALUE_DRAW; // pr0rp
  }

  return strongSide == pos.side_to_move() ? result : -result;
}


/// KQsPs vs K.
template<>
Value Endgame<KQsPsK>::operator()(const Position& pos) const {

  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                - pos.count<PAWN>(weakSide) * PawnValueEg;

  if ( pos.count<QUEEN>(strongSide) >= 3
      && ( DarkSquares & pos.pieces(strongSide, QUEEN))
      && (~DarkSquares & pos.pieces(strongSide, QUEEN)))
      result += PushToEdges[loserKSq];
  else if (pos.count<QUEEN>(strongSide) + pos.count<PAWN>(strongSide) < 3)
      return VALUE_DRAW;
  else
  {
      bool dark  =  DarkSquares & pos.pieces(strongSide, QUEEN);
      bool light = ~DarkSquares & pos.pieces(strongSide, QUEEN);

      // Determine the color of queens from promoting pawns
      Bitboard b = pos.pieces(strongSide, PAWN);
      while (b && (!dark || !light))
      {
          if (file_of(pop_lsb(&b)) % 2 == (strongSide == WHITE ? 0 : 1))
              light = true;
          else
              dark = true;
      }
      if (!dark || !light)
          return VALUE_DRAW; // we can not checkmate with same colored queens
  }

  return strongSide == pos.side_to_move() ? result : -result;
}


//Opponent King + Pawn pieces
template<>
Value Endgame<KXKP>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + PushToCorn[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//Opponent King + Queen pieces
template<>
Value Endgame<KXKQ>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + PushToCorn[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//Opponent King + Bishop pieces
template<>
Value Endgame<KXKB>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + PushToCorn[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//Opponent King + Knight pieces
template<>
Value Endgame<KXKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + PushToCorn[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//Opponent King + Rook pieces
template<>
Value Endgame<KXKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + PushToCorn[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


/// Mate with KBQ vs K.
template<>
Value Endgame<KBQK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  BishopValueEg
                + QueenValueEg
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + 4 * RookValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


/// Mate with KNQ vs K.
template<>
Value Endgame<KNQK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq  = pos.square<KING>(weakSide);
  Square queenSq   = pos.square<QUEEN>(strongSide);
  Square knightSq  = pos.square<KNIGHT>(strongSide);

  // tries to drive toward corners A1 or H8. If we have a
  // queen that cannot reach the above squares, we flip the kings in order
  // to drive the enemy toward corners A8 or H1.
  if (opposite_colors(queenSq, SQ_A1))
  {
      winnerKSq = ~winnerKSq;
      loserKSq  = ~loserKSq;
      knightSq  = ~knightSq;
  }

  // Weak king not in queen's corner could be draw
  if (distance(SQ_A1, loserKSq) >= 4 && distance(SQ_H8, loserKSq) >= 4)
      return Value(PushClose[distance(winnerKSq, loserKSq)]);

  // King and knight on the winnng square ? 
  Value winValue = (  distance(winnerKSq, distance(SQ_A1, loserKSq) < 4 ? SQ_A1 : SQ_H8) <= 4 
                    && popcount(pos.attacks_from<KING>(loserKSq) & pos.attacks_from<KNIGHT>(knightSq)) > 0) ? 
                    PawnValueMg : VALUE_ZERO;

  Value result =  winValue
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToQueenCorners[loserKSq];

  return strongSide == pos.side_to_move() ? result : -result;
}


/// KR vs KN. The attacking side has slightly better winning chances than
/// in KR vs KB, particularly if the king and the knight are far apart.
template<>
Value Endgame<KRKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Square bksq = pos.square<KING>(weakSide);
  Square bnsq = pos.square<KNIGHT>(weakSide);
  Value result = Value(PushToEdges[bksq] + PushAway[distance(bksq, bnsq)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


/// Some cases of trivial draws
template<> Value Endgame<KNNK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KQQK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KQPK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KPPK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KNK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KBK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KQK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KPK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KNKB>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KNKQ>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KBKQ>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KNKP>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KBKP>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KQKP>::operator()(const Position&) const { return VALUE_DRAW; }
