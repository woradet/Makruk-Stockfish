/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2019 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#include <algorithm> // For std::min
#include <cassert>
#include <cstring>   // For std::memset

#include "material.h"
#include "thread.h"

using namespace std;

namespace {

  // Polynomial material imbalance parameters

  constexpr int QuadraticOurs[][PIECE_TYPE_NB] = {
    //            OUR PIECES
    // Q-pair pawn queen bishop knight rook
    { 1000                                }, // Q-pair
    {   40,    0                          }, // Pawn
    {    0,   69,   -1                    }, // Queen
    {    0,  104,   33,  -105             }, // Bishop
    {   32,  255,    2,     4,   -3       }, // Knight      OUR PIECES
    {  -26,   -2,   52,   110,   47, -150 }  // Rook
  };

  constexpr int QuadraticTheirs[][PIECE_TYPE_NB] = {
    //           THEIR PIECES
    // Q-pair pawn queen bishop knight rook
    {    0                                }, // Q-pair
    {   36,    0                          }, // Pawn
    {   40,   50,     0                   }, // Queen
    {   59,   65,    25,     0            }, // Bishop
    {    9,   63,     7,    42,    0      }, // Knight      OUR PIECES
    {   46,   39,    -8,   -24,  240,   0 }  // Rook
  };

  // Endgame evaluation and scaling functions are accessed directly and not through
  // the function maps because they correspond to more than one material hash key.
  Endgame<KXK>     EvaluateKXK[]     = { Endgame<KXK>(WHITE),     Endgame<KXK>(BLACK) };
  Endgame<KQsPsK>  EvaluateKQsPsK[]  = { Endgame<KQsPsK>(WHITE),  Endgame<KQsPsK>(BLACK) };
  Endgame<KXKP>    EvaluateKXKP[]    = { Endgame<KXKP>(WHITE),    Endgame<KXKP>(BLACK) };
  Endgame<KXKQ>    EvaluateKXKQ[]    = { Endgame<KXKQ>(WHITE),    Endgame<KXKQ>(BLACK) };
  Endgame<KXKB>    EvaluateKXKB[]    = { Endgame<KXKB>(WHITE),    Endgame<KXKB>(BLACK) };
  Endgame<KXKN>    EvaluateKXKN[]    = { Endgame<KXKN>(WHITE),    Endgame<KXKN>(BLACK) };
  Endgame<KXKR>    EvaluateKXKR[]    = { Endgame<KXKR>(WHITE),    Endgame<KXKR>(BLACK) };
  
  // Helper used to detect a given material distribution
  bool is_KXK(const Position& pos, Color us) {
    return  !more_than_one(pos.pieces(~us))
          && pos.non_pawn_material(us) >= BishopValueEg + QueenValueEg ;
  }

  bool is_KQsPsK(const Position& pos, Color us) {
    return   !more_than_one(pos.pieces(~us))
          && (pos.count<QUEEN >(us) || pos.count<PAWN>(us))
          && !pos.count<ROOK  >(us)
          && !pos.count<BISHOP>(us)
          && !pos.count<KNIGHT>(us);
  }

  bool is_KXKP(const Position& pos, Color us) {
    return  !pos.count<PAWN>(us)
          && pos.count<PAWN>(~us) == 1
		  && pos.non_pawn_material(us) - pos.non_pawn_material(~us) >= BishopValueEg + QueenValueEg ;
  }

  bool is_KXKQ(const Position& pos, Color us) {
    return  !pos.count<PAWN>(us)
	      && pos.non_pawn_material(~us) == QueenValueMg
          && pos.count<QUEEN>(~us)  == 1
		  && pos.non_pawn_material(us) - pos.non_pawn_material(~us) >= BishopValueEg + QueenValueEg ;
  }

  bool is_KXKB(const Position& pos, Color us) {
    return  !pos.count<PAWN>(us)
	      && pos.non_pawn_material(~us) == BishopValueMg
          && pos.count<BISHOP>(~us)  == 1
		  && pos.non_pawn_material(us) - pos.non_pawn_material(~us) >= BishopValueEg + QueenValueEg ;
  }

  bool is_KXKN(const Position& pos, Color us) {
    return  !pos.count<PAWN>(us)
	      && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us)  == 1
		  && pos.non_pawn_material(us) - pos.non_pawn_material(~us) >= BishopValueEg + QueenValueEg ;
  }

  bool is_KXKR(const Position& pos, Color us) {
    return  !pos.count<PAWN>(us)
	      && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us)  == 1
		  && pos.non_pawn_material(us) - pos.non_pawn_material(~us) >= BishopValueEg + QueenValueEg ;
  }

  /// imbalance() calculates the imbalance by comparing the piece count of each
  /// piece type for both colors.
  template<Color Us>
  int imbalance(const int pieceCount[][PIECE_TYPE_NB]) {

    constexpr Color Them = (Us == WHITE ? BLACK : WHITE);

    int bonus = 0;

    // Second-degree polynomial material imbalance, by Tord Romstad
    for (int pt1 = NO_PIECE_TYPE; pt1 <= ROOK; ++pt1)
    {
        if (!pieceCount[Us][pt1])
            continue;

        int v = 0;

        for (int pt2 = NO_PIECE_TYPE; pt2 <= pt1; ++pt2)
            v +=  QuadraticOurs[pt1][pt2] * pieceCount[Us][pt2]
                + QuadraticTheirs[pt1][pt2] * pieceCount[Them][pt2];

        bonus += pieceCount[Us][pt1] * v;
    }

    return bonus;
  }

} // namespace

namespace Material {

/// Material::probe() looks up the current position's material configuration in
/// the material hash table. It returns a pointer to the Entry if the position
/// is found. Otherwise a new Entry is computed and stored there, so we don't
/// have to recompute all when the same material configuration occurs again.

Entry* probe(const Position& pos) {

  Key key = pos.material_key();
  Entry* e = pos.this_thread()->materialTable[key];

  if (e->key == key)
      return e;

  std::memset(e, 0, sizeof(Entry));
  e->key = key;
  e->factor[WHITE] = e->factor[BLACK] = (uint8_t)SCALE_FACTOR_NORMAL;

  Value npm_w = pos.non_pawn_material(WHITE);
  Value npm_b = pos.non_pawn_material(BLACK);
  Value npm = std::max(EndgameLimit, std::min(npm_w + npm_b, MidgameLimit));

  // Map total non-pawn material into [PHASE_ENDGAME, PHASE_MIDGAME]
  e->gamePhase = Phase(((npm - EndgameLimit) * PHASE_MIDGAME) / (MidgameLimit - EndgameLimit));

  // Let's look if we have a specialized evaluation function for this particular
  // material configuration. Firstly we look for a fixed configuration one, then
  // for a generic one if the previous search failed.
  if ((e->evaluationFunction = pos.this_thread()->endgames.probe<Value>(key)) != nullptr)
      return e;

  for (Color c = WHITE; c <= BLACK; ++c)
      if (is_KXK(pos, c))
      {
          e->evaluationFunction = &EvaluateKXK[c];
          return e;
      }

  // Only queens and pawns against bare king
  for (Color c = WHITE; c <= BLACK; ++c)
      if (is_KQsPsK(pos, c))
      {
          e->evaluationFunction = &EvaluateKQsPsK[c];
          return e;
      }

  for (Color c = WHITE; c <= BLACK; ++c)
      if (is_KXKP(pos, c))
      {
          e->evaluationFunction = &EvaluateKXKP[c];
          return e;
      }
	  
  for (Color c = WHITE; c <= BLACK; ++c)
      if (is_KXKQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKXKQ[c];
          return e;
      }

  for (Color c = WHITE; c <= BLACK; ++c)
      if (is_KXKB(pos, c))
      {
          e->evaluationFunction = &EvaluateKXKB[c];
          return e;
      }

  for (Color c = WHITE; c <= BLACK; ++c)
      if (is_KXKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKXKN[c];
          return e;
      }

  for (Color c = WHITE; c <= BLACK; ++c)
      if (is_KXKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKXKR[c];
          return e;
      }

  // OK, we didn't find any special evaluation function for the current material
  // configuration. Is there a suitable specialized scaling function?
  EndgameBase<ScaleFactor>* sf;

  if ((sf = pos.this_thread()->endgames.probe<ScaleFactor>(key)) != nullptr)
  {
      e->scalingFunction[sf->strongSide] = sf; // Only strong color assigned
      return e;
  }

  // Evaluate the material imbalance. We use PIECE_TYPE_NONE as a place holder
  // for the bishop pair "extended piece", which allows us to be more flexible
  // in defining bishop pair bonuses.
  const int pieceCount[COLOR_NB][PIECE_TYPE_NB] = {
  { pos.queen_pair(WHITE), pos.count<PAWN>(WHITE), pos.count<QUEEN >(WHITE),
    pos.count<BISHOP>(WHITE), pos.count<KNIGHT>(WHITE), pos.count<ROOK>(WHITE) },
  { pos.queen_pair(BLACK), pos.count<PAWN>(BLACK), pos.count<QUEEN >(BLACK),
    pos.count<BISHOP>(BLACK), pos.count<KNIGHT>(BLACK), pos.count<ROOK>(BLACK) } };

  e->value = int16_t((imbalance<WHITE>(pieceCount) - imbalance<BLACK>(pieceCount)) / 16);
  return e;
}

} // namespace Material
