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

#include "types.h"

Value PieceValue[PHASE_NB][PIECE_NB] = {
  { VALUE_ZERO, PawnValueMg, QueenValueMg, BishopValueMg, KnightValueMg, RookValueMg },
  { VALUE_ZERO, PawnValueEg, QueenValueEg, BishopValueEg, KnightValueEg, RookValueEg }
};

namespace PSQT {

#define S(mg, eg) make_score(mg, eg)

// Bonus[PieceType][Square / 2] contains Piece-Square scores. For each piece
// type on a given square a (middlegame, endgame) score pair is assigned. Table
// is defined for files A..D and white side: it is symmetric for black side and
// second half of the files.
constexpr Score Bonus[][RANK_NB][int(FILE_NB) / 2] = {
  { },
  { // Pawn
   { S(  0, 0), S(  0, 0), S(  0, 0), S( 0, 0) },
   { S(  0, 0), S(  0, 0), S(  0, 0), S( 0, 0) },
   { S(-18,-4), S( -2,-5), S( 19, 5), S(24, 4) },
   { S(-17, 3), S(  1, 3), S( 20,-8), S(35,-3) },
   { S( -6, 8), S(  5, 9), S( 15, 7), S(29,-6) }
  },
  { // Queen
   { S(-141, 0), S(-76, 16), S(-60, 28), S(-53, 30) },
   { S( -33,16), S(-43, 55), S(  8, 80), S( 10, 95) },
   { S( -44,26), S(-17, 99), S(199,130), S(200,150) },
   { S(  -1,26), S(118, 99), S(199,130), S(200,150) },
   { S(   7,26), S(116, 99), S(199,130), S(200,150) },
   { S(  11,26), S(137, 99), S(199,130), S(200,150) },
   { S( -63,16), S( 20, 55), S(  5, 80), S( 14, 95) },
   { S(-120, 0), S(-57, 16), S(-32, 28), S(-20, 30) }
  },
  { // Bishop
   { S(-100, 0), S(-76, 16), S(-60, 28), S(-53, 30) },
   { S( -63,16), S(-33, 55), S(  8, 80), S(  6, 95) },
   { S(   7,26), S( 89, 99), S( 90,130), S( 99,150) },
   { S(   8,26), S(128, 99), S(133,130), S(200,150) },
   { S(   9,26), S(136, 99), S(128,130), S(200,150) },
   { S(  11,26), S(147, 99), S(146,130), S(200,150) },
   { S( -50,16), S( 29, 55), S( 35, 80), S( 54, 95) },
   { S(-120, 0), S(-57, 16), S(-32, 28), S(-20, 30) }
  },
  { // Knight
   { S(-161,-105), S(-96,-82), S(-80,-46), S(-73,-14) },
   { S( -83, -69), S(-43,-54), S(-21,-17), S(  0,  9) },
   { S( -71, -50), S(  3,-39), S(  4, -7), S(  9, 28) },
   { S( -25, -41), S( 18,-25), S( 43,  6), S( 47, 38) },
   { S( -26, -46), S( 16,-25), S( 38,  3), S( 50, 40) },
   { S( -11, -54), S( 37,-38), S( 56, -7), S( 65, 27) },
   { S( -63, -65), S(-19,-50), S(  5,-24), S( 14, 13) },
   { S(-195,-109), S(-67,-89), S(-42,-50), S(-29,-13) }
  },
  { // Rook
   { S(-25,-25), S(-16,-16), S(-16,-16), S(-9,-9) },
   { S(-21,-21), S( -8, -8), S( -3, -3), S( 0, 0) },
   { S(-21,-21), S( -9, -9), S( -4, -4), S( 2, 2) },
   { S(-22,-22), S( -6, -6), S( -1, -1), S( 2, 2) },
   { S(-22,-22), S( -7, -7), S(  0,  0), S( 1, 1) },
   { S(-21,-21), S( -7, -7), S(  0,  0), S( 2, 2) },
   { S(-12,-12), S(  4,  4), S(  8,  8), S(12,12) },
   { S(-23,-23), S(-15,-15), S(-11,-11), S(-5,-5) }
  },
  { // King
   { S(  0,  0), S(  0, 48), S( 64, 75), S(320, 84) },
   { S(155, 43), S(254, 92), S(201,143), S(280,132) },
   { S(  0, 83), S(176,138), S(200,167), S(245,165) },
   { S(  0,106), S(148,169), S(177,169), S(185,179) },
   { S(  0,108), S(115,163), S(149,200), S(177,203) },
   { S(  0, 95), S( 84,155), S(118,176), S(159,174) },
   { S(  0, 50), S( 63, 99), S( 87,122), S(128,139) },
   { S(  0,  9), S( 47, 55), S( 63, 80), S( 88, 90) }
  }
};

#undef S

Score psq[PIECE_NB][SQUARE_NB];

// init() initializes piece-square tables: the white halves of the tables are
// copied from Bonus[] adding the piece value, then the black halves of the
// tables are initialized by flipping and changing the sign of the white scores.
void init() {

  for (Piece pc = W_PAWN; pc <= W_KING; ++pc)
  {
      PieceValue[MG][~pc] = PieceValue[MG][pc];
      PieceValue[EG][~pc] = PieceValue[EG][pc];

      Score score = make_score(PieceValue[MG][pc], PieceValue[EG][pc]);

      for (Square s = SQ_A1; s <= SQ_H8; ++s)
      {
          File f = std::min(file_of(s), ~file_of(s));
          psq[ pc][ s] = score + Bonus[pc][rank_of(s)][f];
          psq[~pc][~s] = -psq[pc][s];
      }
  }
}

} // namespace PSQT
