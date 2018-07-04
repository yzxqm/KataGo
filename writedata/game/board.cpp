/*
 * fastboard.cpp
 * Originally from an unreleased project back in 2010, modified since.
 * Authors: brettharrison (original), David Wu (original and later modificationss).
 */

#include <cassert>
#include <iostream>
#include <cstring>
#include <vector>
#include <algorithm>
#include "core/rand.h"
#include "fastboard.h"

//STATIC VARS-----------------------------------------------------------------------------
bool FastBoard::IS_ZOBRIST_INITALIZED = false;
Hash128 FastBoard::ZOBRIST_SIZE_X_HASH[MAX_LEN+1];
Hash128 FastBoard::ZOBRIST_SIZE_Y_HASH[MAX_LEN+1];
Hash128 FastBoard::ZOBRIST_BOARD_HASH[MAX_ARR_SIZE][4];
Hash128 FastBoard::ZOBRIST_PLAYER_HASH[4];
Hash128 FastBoard::ZOBRIST_KO_LOC_HASH[MAX_ARR_SIZE];

//CONSTRUCTORS AND INITIALIZATION----------------------------------------------------------

FastBoard::FastBoard()
{
  init(19,19,false);
}

FastBoard::FastBoard(int x, int y, bool multiStoneSuicideLegal)
{
  init(x,y,multiStoneSuicideLegal);
}


FastBoard::FastBoard(const FastBoard& other)
{
  x_size = other.x_size;
  y_size = other.y_size;

  int arr_size = (x_size+1)*(y_size+2)+1;

  memcpy(colors, other.colors, sizeof(Color)*arr_size);
  memcpy(chain_data, other.chain_data, sizeof(ChainData)*arr_size);
  memcpy(chain_head, other.chain_head, sizeof(Loc)*arr_size);
  memcpy(next_in_chain, other.next_in_chain, sizeof(Loc)*arr_size);

  ko_loc = other.ko_loc;

  empty_list = other.empty_list;

  pos_hash = other.pos_hash;

  memcpy(adj_offsets, other.adj_offsets, sizeof(short)*8);

  isMultiStoneSuicideLegal = other.isMultiStoneSuicideLegal;
}

void FastBoard::init(int xS, int yS, bool multiStoneSuicideLegal)
{
  assert(IS_ZOBRIST_INITALIZED);
  assert(xS <= MAX_LEN && yS <= MAX_LEN);

  x_size = xS;
  y_size = yS;

  for(int i = 0; i < MAX_ARR_SIZE; i++)
    colors[i] = C_WALL;

  for(int y = 0; y < y_size; y++)
  {
    for(int x = 0; x < x_size; x++)
    {
      Loc loc = (x+1) + (y+1)*(x_size+1);
      colors[loc] = C_EMPTY;
      empty_list.add(loc);
    }
  }

  ko_loc = NULL_LOC;

  pos_hash = ZOBRIST_SIZE_X_HASH[x_size] ^ ZOBRIST_SIZE_Y_HASH[y_size];

  Location::getAdjacentOffsets(adj_offsets,x_size);

  isMultiStoneSuicideLegal = multiStoneSuicideLegal;
}

void FastBoard::initHash()
{
  if(IS_ZOBRIST_INITALIZED)
    return;
  Rand rand("FastBoard::initHash()");

  auto nextHash = [&rand]() {
    uint64_t h0 = rand.nextUInt64();
    uint64_t h1 = rand.nextUInt64();
    return Hash128(h0,h1);
  };

  for(int i = 0; i<MAX_ARR_SIZE; i++)
  {
    for(Color j = 0; j<4; j++)
    {
      if(j == C_EMPTY || j == C_WALL)
        ZOBRIST_BOARD_HASH[i][j] = Hash128();
      else
        ZOBRIST_BOARD_HASH[i][j] = nextHash();
    }
    ZOBRIST_KO_LOC_HASH[i] = nextHash();
  }
  for(int i = 0; i<4; i++)
    ZOBRIST_PLAYER_HASH[i] = nextHash();
  for(int i = 0; i<MAX_LEN+1; i++)
    ZOBRIST_SIZE_X_HASH[i] = nextHash();
  for(int i = 0; i<MAX_LEN+1; i++)
    ZOBRIST_SIZE_Y_HASH[i] = nextHash();

  IS_ZOBRIST_INITALIZED = true;
}

void FastBoard::setMultiStoneSuicideLegal(bool b) {
  isMultiStoneSuicideLegal = b;
}

void FastBoard::clearSimpleKoLoc() {
  ko_loc = NULL_LOC;
}


//Gets the number of liberties of the chain at loc. Assertion: location must be black or white.
int FastBoard::getNumLiberties(Loc loc) const
{
  assert(colors[loc] == C_BLACK || colors[loc] == C_WHITE);
  return chain_data[chain_head[loc]].num_liberties;
}

//Check if moving here would be a self-capture
bool FastBoard::isSuicide(Loc loc, Player pla) const
{
  Player opp = getOpp(pla);
  for(int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];

    if(colors[adj] == C_EMPTY)
      return false;
    else if(colors[adj] == pla)
    {
      if(getNumLiberties(adj) > 1)
        return false;
    }
    else if(colors[adj] == opp)
    {
      if(getNumLiberties(adj) == 1)
        return false;
    }
  }

  return true;
}

//Check if moving here is would be an illegal self-capture
bool FastBoard::isIllegalSuicide(Loc loc, Player pla) const
{
  Player opp = getOpp(pla);
  for(int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];

    if(colors[adj] == C_EMPTY)
      return false;
    else if(colors[adj] == pla)
    {
      if(isMultiStoneSuicideLegal || getNumLiberties(adj) > 1)
        return false;
    }
    else if(colors[adj] == opp)
    {
      if(getNumLiberties(adj) == 1)
        return false;
    }
  }

  return true;
}

//Returns the number of liberties a new stone placed here would have, or max if it would be >= max.
int FastBoard::getNumLibertiesAfterPlay(Loc loc, Player pla, int max) const
{
  Player opp = getOpp(pla);

  int numLibs = 0;
  Loc libs[max];
  int numCapturedGroups = 0;
  Loc capturedGroupHeads[4];

  //First, count immediate liberties and groups that would be captured
  for(int i = 0; i < 4; i++) {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == C_EMPTY) {
      libs[numLibs++] = adj;
      if(numLibs >= max)
        return max;
    }
    else if(colors[adj] == opp && getNumLiberties(adj) == 1) {
      libs[numLibs++] = adj;
      if(numLibs >= max)
        return max;

      Loc head = chain_head[adj];
      bool alreadyFound = false;
      for(int j = 0; j<numCapturedGroups; j++) {
        if(capturedGroupHeads[j] == head)
        {alreadyFound = true; break;}
      }
      if(!alreadyFound)
        capturedGroupHeads[numCapturedGroups++] = head;
    }
  }

  auto wouldBeEmpty = [numCapturedGroups,&capturedGroupHeads,this,opp](Loc loc) {
    if(this->colors[loc] == C_EMPTY)
      return true;
    if(this->colors[loc] == opp) {
      for(int i = 0; i<numCapturedGroups; i++)
        if(capturedGroupHeads[i] == this->chain_head[loc])
          return true;
    }
    return false;
  };

  //Next, walk through all stones of all surrounding groups we would connect with and count liberties, avoiding overlap.
  int numConnectingGroups = 0;
  Loc connectingGroupHeads[4];
  for(int i = 0; i<4; i++) {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == pla) {
      Loc head = chain_head[adj];
      bool alreadyFound = false;
      for(int j = 0; j<numConnectingGroups; j++) {
        if(connectingGroupHeads[j] == head)
        {alreadyFound = true; break;}
      }
      if(!alreadyFound) {
        connectingGroupHeads[numConnectingGroups++] = head;

        Loc cur = adj;
        do
        {
          for(int k = 0; k < 4; k++) {
            Loc possibleLib = cur + adj_offsets[k];
            if(possibleLib != loc && wouldBeEmpty(possibleLib)) {
              bool alreadyCounted = false;
              for(int l = 0; l<numLibs; l++) {
                if(libs[l] == possibleLib)
                {alreadyCounted = true; break;}
              }
              if(!alreadyCounted) {
                libs[numLibs++] = possibleLib;
                if(numLibs >= max)
                  return max;
              }
            }
          }

          cur = next_in_chain[cur];
        } while (cur != adj);
      }
    }
  }
  return numLibs;
}

//Check if moving here is illegal due to simple ko
bool FastBoard::isKoBanned(Loc loc) const
{
  return loc == ko_loc;
}

bool FastBoard::isOnBoard(Loc loc) const {
  return loc >= 0 && loc < MAX_ARR_SIZE && colors[loc] != C_WALL;
}

//Check if moving here is illegal.
bool FastBoard::isLegal(Loc loc, Player pla) const
{
  if(pla != P_BLACK && pla != P_WHITE)
    return false;
  return loc == PASS_LOC || (loc >= 0 && loc < MAX_ARR_SIZE && (colors[loc] == C_EMPTY) && !isKoBanned(loc) && !isIllegalSuicide(loc, pla));
}

//Check if this location contains a simple eye for the specified player.
bool FastBoard::isSimpleEye(Loc loc, Player pla) const
{
  if(colors[loc] != C_EMPTY)
    return false;

  bool against_wall = false;

  //Check that surounding points are owned
  for(int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == C_WALL)
      against_wall = true;
    else if(colors[adj] != pla)
      return false;
  }

  //Check that opponent does not own too many diagonal points
  Player opp = getOpp(pla);
  int num_opp_corners = 0;
  for(int i = 4; i < 8; i++)
  {
    Loc corner = loc + adj_offsets[i];
    if(colors[corner] == opp)
      num_opp_corners++;
  }

  if(num_opp_corners >= 2 || (against_wall && num_opp_corners >= 1))
    return false;

  return true;
}

bool FastBoard::wouldBeKoCapture(Loc loc, Player pla) const {
  if(colors[loc] != C_EMPTY)
    return false;
  //Check that surounding points are are all opponent owned and exactly one of them is capturable
  Player opp = getOpp(pla);
  Loc oppCapturableLoc = NULL_LOC;
  for(int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] != C_WALL && colors[adj] != opp)
      return false;
    if(colors[adj] == opp && getNumLiberties(adj) == 1) {
      if(oppCapturableLoc != NULL_LOC)
        return false;
      oppCapturableLoc = adj;
    }
  }
  if(oppCapturableLoc == NULL_LOC)
    return false;

  //Check that the capturable loc has exactly one stone
  if(chain_data[chain_head[oppCapturableLoc]].num_locs != 1)
    return false;
  return true;
}

bool FastBoard::setStone(Loc loc, Color color)
{
  if(loc < 0 || loc >= MAX_ARR_SIZE || colors[loc] == C_WALL)
    return false;
  if(color != C_BLACK && color != C_WHITE && color != C_EMPTY)
    return false;

  if(colors[loc] == color)
  {}
  else if(colors[loc] == C_EMPTY)
    playMoveAssumeLegal(loc,color);
  else if(color == C_EMPTY)
    removeSingleStone(loc);
  else {
    removeSingleStone(loc);
    if(!isSuicide(loc,color))
      playMoveAssumeLegal(loc,color);
  }

  ko_loc = NULL_LOC;
  return true;
}


//Attempts to play the specified move. Returns true if successful, returns false if the move was illegal.
bool FastBoard::playMove(Loc loc, Player pla)
{
  if(isLegal(loc,pla))
  {
    playMoveAssumeLegal(loc,pla);
    return true;
  }
  return false;
}

//Plays the specified move, assuming it is legal, and returns a MoveRecord for the move
FastBoard::MoveRecord FastBoard::playMoveRecorded(Loc loc, Player pla)
{
  MoveRecord record;
  record.loc = loc;
  record.pla = pla;
  record.ko_loc = ko_loc;
  record.capDirs = 0;

  Player opp = getOpp(pla);
  for(int i = 0; i < 4; i++)
  {
    int adj = loc + adj_offsets[i];
    if(colors[adj] == opp && getNumLiberties(adj) == 1)
      record.capDirs |= (((uint8_t)1) << i);
  }
  if(record.capDirs == 0 && isSuicide(loc,pla))
    record.capDirs = 0x10;
  playMoveAssumeLegal(loc, pla);
  return record;
}

//Undo the move given by record. Moves MUST be undone in the order they were made.
//Undos will NOT typically restore the precise representation in the board to the way it was. The heads of chains
//might change, the order of the circular lists might change, etc.
void FastBoard::undo(FastBoard::MoveRecord record)
{
  ko_loc = record.ko_loc;

  Loc loc = record.loc;
  if(loc == PASS_LOC)
    return;

  //Re-fill stones in all captured directions
  for(int i = 0; i<4; i++)
  {
    int adj = loc + adj_offsets[i];
    if(record.capDirs & (1 << i))
    {
      if(colors[adj] == C_EMPTY)
        addChain(adj, getOpp(record.pla));
    }
  }
  //Re-fill suicided stones
  if(record.capDirs == 0x10) {
    assert(colors[loc] == C_EMPTY);
    addChain(loc,record.pla);
  }

  //Delete the stone played here.
  pos_hash ^= ZOBRIST_BOARD_HASH[loc][colors[loc]];
  colors[loc] = C_EMPTY;
  empty_list.add(loc);

  //Uneat opp liberties
  changeSurroundingLiberties(loc, getOpp(record.pla),+1);

  //If this was not a single stone, we need to recompute the chain from scratch
  if(chain_data[chain_head[loc]].num_locs > 1)
  {
    //Run through the whole chain and make their heads point to nothing
    Loc cur = loc;
    do
    {
      chain_head[cur] = NULL_LOC;
      cur = next_in_chain[cur];
    } while (cur != loc);

    //Rebuild each chain adjacent now
    for(int i = 0; i<4; i++)
    {
      int adj = loc + adj_offsets[i];
      if(colors[adj] == record.pla && chain_head[adj] == NULL_LOC)
        rebuildChain(adj, record.pla);
    }
  }
}

Hash128 FastBoard::getPosHashAfterMove(Loc loc, Player pla) const {
  if(loc == PASS_LOC)
    return pos_hash;
  assert(loc != NULL_LOC);

  Hash128 hash = pos_hash;
  hash ^= ZOBRIST_BOARD_HASH[loc][pla];

  Player opp = getOpp(pla);

  //Count immediate liberties and groups that would be captured
  bool wouldBeSuicide = true;
  int numCapturedGroups = 0;
  Loc capturedGroupHeads[4];
  for(int i = 0; i < 4; i++) {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == C_EMPTY)
      wouldBeSuicide = false;
    else if(colors[adj] == pla && getNumLiberties(adj) > 1)
      wouldBeSuicide = false;
    else if(colors[adj] == opp) {
      //Capture!
      if(getNumLiberties(adj) == 1) {
        //Make sure we haven't already counted it
        Loc head = chain_head[adj];
        bool alreadyFound = false;
        for(int j = 0; j<numCapturedGroups; j++) {
          if(capturedGroupHeads[j] == head)
          {alreadyFound = true; break;}
        }
        if(!alreadyFound) {
          capturedGroupHeads[numCapturedGroups++] = head;
          wouldBeSuicide = false;

          //Now iterate through the group to update the hash
          Loc cur = adj;
          do {
            hash ^= ZOBRIST_BOARD_HASH[cur][opp];
            cur = next_in_chain[cur];
          } while (cur != adj);
        }
      }
    }
  }

  //Update hash for suicidal moves
  if(wouldBeSuicide) {
    assert(numCapturedGroups == 0);

    for(int i = 0; i < 4; i++) {
      Loc adj = loc + adj_offsets[i];
      //Suicide capture!
      if(colors[adj] == pla && getNumLiberties(adj) == 1) {
        //Make sure we haven't already counted it
        Loc head = chain_head[adj];
        bool alreadyFound = false;
        for(int j = 0; j<numCapturedGroups; j++) {
          if(capturedGroupHeads[j] == head)
          {alreadyFound = true; break;}
        }
        if(!alreadyFound) {
          capturedGroupHeads[numCapturedGroups++] = head;

          //Now iterate through the group to update the hash
          Loc cur = adj;
          do {
            hash ^= ZOBRIST_BOARD_HASH[cur][pla];
            cur = next_in_chain[cur];
          } while (cur != adj);
        }
      }
    }

    //Don't forget the stone we'd place would also die
    hash ^= ZOBRIST_BOARD_HASH[loc][pla];
  }

  return hash;
}

//Plays the specified move, assuming it is legal.
void FastBoard::playMoveAssumeLegal(Loc loc, Player pla)
{
  //Pass?
  if(loc == PASS_LOC)
  {
    ko_loc = NULL_LOC;
    return;
  }

  Player opp = getOpp(pla);

  //Add the new stone as an independent group
  colors[loc] = pla;
  pos_hash ^= ZOBRIST_BOARD_HASH[loc][pla];
  chain_data[loc].owner = pla;
  chain_data[loc].num_locs = 1;
  chain_data[loc].num_liberties = getNumImmediateLiberties(loc);
  chain_head[loc] = loc;
  next_in_chain[loc] = loc;
  empty_list.remove(loc);

  //Merge with surrounding friendly chains and capture any necessary opp chains
  int num_captured = 0; //Number of stones captured
  Loc possible_ko_loc = NULL_LOC;  //What location a ko ban might become possible in
  int num_opps_seen = 0;  //How many opp chains we have seen so far
  Loc opp_heads_seen[4];   //Heads of the opp chains seen so far

  for(int i = 0; i < 4; i++)
  {
    int adj = loc + adj_offsets[i];

    //Friendly chain!
    if(colors[adj] == pla)
    {
      //Already merged?
      if(chain_head[adj] == chain_head[loc])
        continue;

      //Otherwise, eat one liberty and merge them
      chain_data[chain_head[adj]].num_liberties--;
      mergeChains(adj,loc);
    }

    //Opp chain!
    else if(colors[adj] == opp)
    {
      Loc opp_head = chain_head[adj];

      //Have we seen it already?
      bool seen = false;
      for(int j = 0; j<num_opps_seen; j++)
        if(opp_heads_seen[j] == opp_head)
        {seen = true; break;}

      if(seen)
        continue;

      //Not already seen! Eat one liberty from it and mark it as seen
      chain_data[opp_head].num_liberties--;
      opp_heads_seen[num_opps_seen++] = opp_head;

      //Kill it?
      if(getNumLiberties(adj) == 0)
      {
        num_captured += removeChain(adj);
        possible_ko_loc = adj;
      }
    }
  }

  //We have a ko if 1 stone was captured and the capturing move is one isolated stone
  //And the capturing move itself now only has one liberty
  if(num_captured == 1 && chain_data[chain_head[loc]].num_locs == 1 && chain_data[chain_head[loc]].num_liberties == 1)
    ko_loc = possible_ko_loc;
  else
    ko_loc = NULL_LOC;

  //Handle suicide
  if(getNumLiberties(loc) == 0)
    removeChain(loc);
}

int FastBoard::getNumImmediateLiberties(Loc loc) const
{
  int num_libs = 0;
  for(int i = 0; i < 4; i++)
    if(colors[loc + adj_offsets[i]] == C_EMPTY)
      num_libs++;

  return num_libs;
}

int FastBoard::countHeuristicConnectionLibertiesX2(Loc loc, Player pla) const
{
  int num_libsX2 = 0;
  for(int i = 0; i < 4; i++) {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == pla) {
      int libs = chain_data[chain_head[adj]].num_liberties;
      if(libs > 1)
        num_libsX2 += libs * 2 - 3;
    }
  }
  return num_libsX2;
}

//Loc is a liberty of head's chain if loc is empty and adjacent to a stone of head.
//Assumes loc is empty
bool FastBoard::isLibertyOf(Loc loc, Loc head) const
{
  for(int i = 0; i<4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == colors[head] && chain_head[adj] == head)
      return true;
  }
  return false;
}

void FastBoard::mergeChains(Loc loc1, Loc loc2)
{
  //Find heads
  Loc head1 = chain_head[loc1];
  Loc head2 = chain_head[loc2];

  assert(head1 != head2);
  assert(chain_data[head1].owner == chain_data[head2].owner);

  //Make sure head2 is the smaller chain.
  if(chain_data[head1].num_locs < chain_data[head2].num_locs)
  {
    Loc temp = head1;
    head1 = head2;
    head2 = temp;
  }

  //Iterate through each stone of head2's chain to make it a member of head1's chain.
  //Count new liberties for head1 as we go.
  chain_data[head1].num_locs += chain_data[head2].num_locs;
  int numNewLiberties = 0;
  Loc loc = head2;
  while(true)
  {
    //Any adjacent liberty is a new liberty for head1 if it is not adjacent to a stone of head1
    for(int i = 0; i<4; i++)
    {
      Loc adj = loc + adj_offsets[i];
      if(colors[adj] == C_EMPTY && !isLibertyOf(adj,head1))
        numNewLiberties++;
    }

    //Now, add this stone to head1.
    chain_head[loc] = head1;

    //If are not back around, we are done.
    if(next_in_chain[loc] != head2)
      loc = next_in_chain[loc];
    else
      break;
  }

  //Add in the liberties
  chain_data[head1].num_liberties += numNewLiberties;

  //We link up (head1 -> next1 -> ... -> last1 -> head1) and (head2 -> next2 -> ... -> last2 -> head2)
  //as: head1 -> head2 -> next2 -> ... -> last2 -> next1 -> ... -> last1 -> head1
  //loc is now last_2
  next_in_chain[loc] = next_in_chain[head1];
  next_in_chain[head1] = head2;
}

//Returns number of stones captured
int FastBoard::removeChain(Loc loc)
{
  int num_stones_removed = 0; //Num stones removed
  Player opp = getOpp(colors[loc]);

  //Walk around the chain...
  Loc cur = loc;
  do
  {
    //Empty out this location
    pos_hash ^= ZOBRIST_BOARD_HASH[cur][colors[cur]];
    colors[cur] = C_EMPTY;
    num_stones_removed++;
    empty_list.add(cur);

    //For each distinct opp chain around, add a liberty to it.
    changeSurroundingLiberties(cur,opp,+1);

    cur = next_in_chain[cur];

  } while (cur != loc);

  return num_stones_removed;
}

//Remove a single stone, even a stone part of a larger group.
void FastBoard::removeSingleStone(Loc loc)
{
  Player pla = colors[loc];

  //Save the entire chain's stone locations
  int num_locs = chain_data[chain_head[loc]].num_locs;
  int locs[num_locs];
  int idx = 0;
  Loc cur = loc;
  do
  {
    locs[idx++] = cur;
    cur = next_in_chain[cur];
  } while (cur != loc);
  assert(idx == num_locs);

  //Delete the entire chain
  removeChain(loc);

  //Then add all the other stones back one by one.
  for(int i = 0; i<num_locs; i++) {
    if(locs[i] != loc)
      playMoveAssumeLegal(locs[i],pla);
  }
}

//Add a chain of the given player to the given region of empty space, floodfilling it.
//Assumes that this region does not border any chains of the desired color already
void FastBoard::addChain(Loc loc, Player pla)
{
  chain_data[loc].num_liberties = 0;
  chain_data[loc].num_locs = 0;
  chain_data[loc].owner = pla;

  //Add a chain with links front -> ... -> loc -> loc with all head pointers towards loc
  Loc front = addChainHelper(loc, loc, loc, pla);

  //Now, we make loc point to front, and that completes the circle!
  next_in_chain[loc] = front;
}

//Floodfill a chain of the given color into this region of empty spaces
//Make the specified loc the head for all the chains and updates the chainData of head with the number of stones.
//Does NOT connect the stones into a circular list. Rather, it produces an linear linked list with the tail pointing
//to tailTarget, and returns the head of the list. The tail is guaranteed to be loc.
Loc FastBoard::addChainHelper(Loc head, Loc tailTarget, Loc loc, Player pla)
{
  //Add stone here
  colors[loc] = pla;
  pos_hash ^= ZOBRIST_BOARD_HASH[loc][pla];
  chain_head[loc] = head;
  chain_data[head].num_locs++;
  next_in_chain[loc] = tailTarget;
  empty_list.remove(loc);

  //Eat opp liberties
  changeSurroundingLiberties(loc,getOpp(pla),-1);

  //Recursively add stones around us.
  Loc nextTailTarget = loc;
  for(int i = 0; i<4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == C_EMPTY)
      nextTailTarget = addChainHelper(head,nextTailTarget,adj,pla);
  }
  return nextTailTarget;
}

//Floods through a chain of the specified player already on the board
//rebuilding its links and counting its liberties as we go.
//Requires that all their heads point towards
//some invalid location, such as NULL_LOC or a location not of color.
//The head of the chain will be loc.
void FastBoard::rebuildChain(Loc loc, Player pla)
{
  chain_data[loc].num_liberties = 0;
  chain_data[loc].num_locs = 0;
  chain_data[loc].owner = pla;

  //Rebuild chain with links front -> ... -> loc -> loc with all head pointers towards loc
  Loc front = rebuildChainHelper(loc, loc, loc, pla);

  //Now, we make loc point to front, and that completes the circle!
  next_in_chain[loc] = front;
}

//Does same thing as addChain, but floods through a chain of the specified color already on the board
//rebuilding its links and also counts its liberties as we go. Requires that all their heads point towards
//some invalid location, such as NULL_LOC or a location not of color.
Loc FastBoard::rebuildChainHelper(Loc head, Loc tailTarget, Loc loc, Player pla)
{
  //Count new liberties
  for(int i = 0; i<4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == C_EMPTY && !isLibertyOf(adj,head))
      chain_data[head].num_liberties++;
  }

  //Add stone here to the chain by setting its head
  chain_head[loc] = head;
  chain_data[head].num_locs++;
  next_in_chain[loc] = tailTarget;

  //Recursively add stones around us.
  Loc nextTailTarget = loc;
  for(int i = 0; i<4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == pla && chain_head[adj] != head)
      nextTailTarget = rebuildChainHelper(head,nextTailTarget,adj,pla);
  }
  return nextTailTarget;
}

//Apply the specified delta to the liberties of all adjacent groups of the specified color
void FastBoard::changeSurroundingLiberties(Loc loc, Player pla, int delta)
{
  int num_seen = 0;  //How many opp chains we have seen so far
  Loc heads_seen[4];   //Heads of the opp chains seen so far
  for(int i = 0; i < 4; i++)
  {
    int adj = loc + adj_offsets[i];
    if(colors[adj] == pla)
    {
      Loc head = chain_head[adj];

      //Have we seen it already?
      bool seen = false;
      for(int j = 0; j<num_seen; j++)
        if(heads_seen[j] == head)
        {seen = true; break;}

      if(seen)
        continue;

      //Not already seen! Eat one liberty from it and mark it as seen
      chain_data[head].num_liberties += delta;
      heads_seen[num_seen++] = head;
    }
  }
}

ostream& operator<<(ostream& out, const FastBoard& board)
{
  out << "HASH: " << board.pos_hash << "\n";
  for(int y = 0; y < board.y_size; y++)
  {
    // for(int x = 0; x < board.x_size; x++)
    // {
    //   Loc loc = Location::getLoc(x,y,board.x_size);
    //   //char s = getCharOfColor(board.colors[loc]);
    //   char s = board.colors[loc] == C_EMPTY ? '.' : '0' + board.chain_data[board.chain_head[loc]].num_liberties;

    //   out << s << ' ';
    // }
    // out << " ";
    for(int x = 0; x < board.x_size; x++)
    {
      Loc loc = Location::getLoc(x,y,board.x_size);
      char s = getCharOfColor(board.colors[loc]);
      out << s << ' ';
    }

    out << "\n";
  }
  out << "\n";
  return out;
}


FastBoard::PointList::PointList()
{
  std::memset(list_, NULL_LOC, sizeof(list_));
  std::memset(indices_, -1, sizeof(indices_));
  size_ = 0;
}

FastBoard::PointList::PointList(const FastBoard::PointList& other)
{
  std::memcpy(list_, other.list_, sizeof(list_));
  std::memcpy(indices_, other.indices_, sizeof(indices_));
  size_ = other.size_;
}

void FastBoard::PointList::operator=(const FastBoard::PointList& other)
{
  std::memcpy(list_, other.list_, sizeof(list_));
  std::memcpy(indices_, other.indices_, sizeof(indices_));
  size_ = other.size_;
}

void FastBoard::PointList::add(Loc loc)
{
  //assert (size_ < MAX_PLAY_SIZE);
  list_[size_] = loc;
  indices_[loc] = size_;
  size_++;
}

void FastBoard::PointList::remove(Loc loc)
{
  //assert(size_ >= 0);
  int index = indices_[loc];
  //assert(index >= 0 && index < size_);
  //assert(list_[index] == loc);
  Loc end_loc = list_[size_-1];
  list_[index] = end_loc;
  list_[size_-1] = NULL_LOC;
  indices_[end_loc] = index;
  indices_[loc] = -1;
  size_--;
}

int FastBoard::PointList::size() const
{
  return size_;
}

Loc& FastBoard::PointList::operator[](int n)
{
  assert (n < size_);
  return list_[n];
}

bool FastBoard::PointList::contains(Loc loc) const {
  return indices_[loc] != -1;
}

Loc Location::getLoc(int x, int y, int x_size)
{
  return (x+1) + (y+1)*(x_size+1);
}
int Location::getX(Loc loc, int x_size)
{
  return (loc % (x_size+1)) - 1;
}
int Location::getY(Loc loc, int x_size)
{
  return (loc / (x_size+1)) - 1;
}
void Location::getAdjacentOffsets(short adj_offsets[8], int x_size)
{
  adj_offsets[0] = -(x_size+1);
  adj_offsets[1] = -1;
  adj_offsets[2] = 1;
  adj_offsets[3] = (x_size+1);
  adj_offsets[4] = -(x_size+1)-1;
  adj_offsets[5] = -(x_size+1)+1;
  adj_offsets[6] = (x_size+1)-1;
  adj_offsets[7] = (x_size+1)+1;
}

bool Location::isAdjacent(Loc loc0, Loc loc1, int x_size)
{
  return loc0 == loc1 - (x_size+1) || loc0 == loc1 - 1 || loc0 == loc1 + 1 || loc0 == loc1 + (x_size+1);
}

string Location::toString(Loc loc, int x_size)
{
  if(loc == FastBoard::PASS_LOC)
    return string("pass");
  if(loc == FastBoard::NULL_LOC)
    return string("null");
  char buf[128];
  sprintf(buf,"(%d,%d)",getX(loc,x_size),getY(loc,x_size));
  return string(buf);
}

int Location::locToTensorPos(Loc loc, int bSize, int maxBSize) {
  if(loc == FastBoard::PASS_LOC)
    return maxBSize * maxBSize;
  else if(loc == FastBoard::NULL_LOC)
    return maxBSize * (maxBSize + 1);
  int offset = (maxBSize - bSize)/2;
  return (getY(loc,bSize) + offset) * maxBSize + (getX(loc,bSize) + offset);
}
Loc Location::tensorPosToLoc(int pos, int bSize, int maxBSize) {
  if(pos == maxBSize * maxBSize)
    return FastBoard::PASS_LOC;
  int offset = (maxBSize - bSize)/2;
  int x = pos % maxBSize - offset;
  int y = pos / maxBSize - offset;
  if(x < 0 || x >= bSize || y < 0 || y >= bSize)
    return FastBoard::NULL_LOC;
  return getLoc(x,y,bSize);
}



//TACTICAL STUFF--------------------------------------------------------------------

//Helper, find liberties of group at loc. Fills in buf, returns the number of captures.
//bufStart is where to start checking to avoid duplicates. bufIdx is where to start actually writing.
int FastBoard::findLiberties(Loc loc, vector<Loc>& buf, int bufStart, int bufIdx) const {
  int numFound = 0;
  Loc cur = loc;
  do
  {
    for(int i = 0; i < 4; i++) {
      Loc lib = cur + adj_offsets[i];
      if(colors[lib] == C_EMPTY) {
        //Check for dups
        bool foundDup = false;
        for(int j = bufStart; j < bufIdx+numFound; j++) {
          if(buf[j] == lib) {
            foundDup = true;
            break;
          }
        }
        if(!foundDup) {
          if(bufIdx+numFound >= buf.size())
            buf.resize(buf.size() * 3/2 + 64);
          buf[bufIdx+numFound] = lib;
          numFound++;
        }
      }
    }

    cur = next_in_chain[cur];
  } while (cur != loc);

  return numFound;
}

//Helper, find captures that gain liberties for the group at loc. Fills in result, returns the number of captures.
//bufStart is where to start checking to avoid duplicates. bufIdx is where to start actually writing.
int FastBoard::findLibertyGainingCaptures(Loc loc, vector<Loc>& buf, int bufStart, int bufIdx) const {
  Player opp = getOpp(colors[loc]);

  //For performance, avoid checking for captures on any chain twice
  int arrSize = x_size*y_size;
  Loc chainHeadsChecked[arrSize];
  int numChainHeadsChecked = 0;

  int numFound = 0;
  Loc cur = loc;
  do
  {
    for(int i = 0; i < 4; i++) {
      Loc adj = cur + adj_offsets[i];
      if(colors[adj] == opp) {
        Loc head = chain_head[adj];
        if(chain_data[head].num_liberties == 1) {
          bool alreadyChecked = false;
          for(int j = 0; j<numChainHeadsChecked; j++) {
            if(chainHeadsChecked[j] == head) {
              alreadyChecked = true;
              break;
            }
          }
          if(!alreadyChecked) {
            //Capturing moves are precisely the liberties of the groups around us with 1 liberty.
            numFound += findLiberties(adj, buf, bufStart, bufIdx+numFound);
            chainHeadsChecked[numChainHeadsChecked++] = head;
          }
        }
      }
    }

    cur = next_in_chain[cur];
  } while (cur != loc);

  return numFound;
}

//Helper, does the group at loc have at least one opponent group adjacent to it in atari?
bool FastBoard::hasLibertyGainingCaptures(Loc loc) const {
  Player opp = getOpp(colors[loc]);
  Loc cur = loc;
  do
  {
    for(int i = 0; i < 4; i++) {
      Loc adj = cur + adj_offsets[i];
      if(colors[adj] == opp) {
        Loc head = chain_head[adj];
        if(chain_data[head].num_liberties == 1)
          return true;
      }
    }
    cur = next_in_chain[cur];
  } while (cur != loc);

  return false;
}

bool FastBoard::searchIsLadderCapturedAttackerFirst2Libs(Loc loc, vector<Loc>& buf, vector<Loc>& workingMoves) {
  if(loc < 0 || loc >= MAX_ARR_SIZE)
    return false;
  if(colors[loc] != C_BLACK && colors[loc] != C_WHITE)
    return false;
  if(chain_data[chain_head[loc]].num_liberties != 2)
    return false;

  //Make it so that pla is always the defender
  Player pla = colors[loc];
  Player opp = getOpp(pla);

  int numLibs = findLiberties(loc,buf,0,0);
  assert(numLibs == 2);

  Loc move0 = buf[0];
  Loc move1 = buf[1];
  bool move0Works = false;
  bool move1Works = false;

  if(isLegal(move0,opp)) {
    MoveRecord record = playMoveRecorded(move0,opp);
    move0Works = searchIsLadderCaptured(loc,true,buf);
    undo(record);
  }
  if(isLegal(move1,opp)) {
    MoveRecord record = playMoveRecorded(move1,opp);
    move1Works = searchIsLadderCaptured(loc,true,buf);
    undo(record);
  }

  if(move0Works || move1Works) {
    workingMoves.clear();
    if(move0Works)
      workingMoves.push_back(move0);
    if(move1Works)
      workingMoves.push_back(move1);
    return true;
  }
  return false;
}

bool FastBoard::searchIsLadderCaptured(Loc loc, bool defenderFirst, vector<Loc>& buf) {
  if(loc < 0 || loc >= MAX_ARR_SIZE)
    return false;
  if(colors[loc] != C_BLACK && colors[loc] != C_WHITE)
    return false;

  if(chain_data[chain_head[loc]].num_liberties > 2 || (defenderFirst && chain_data[chain_head[loc]].num_liberties > 1))
    return false;

  //Make it so that pla is always the defender
  Player pla = colors[loc];
  Player opp = getOpp(pla);

  //Clear the ko loc for the defender at the root node - assume all kos work for the defender
  Loc ko_loc_saved = ko_loc;
  if(defenderFirst)
    ko_loc = NULL_LOC;

  //Stack for the search. These point to lists of possible moves to search at each level of the stack, indices refer to indices in [buf].
  int arrSize = x_size*y_size*3/2+1; //A bit bigger due to paranoia about recaptures making the sequence longer.
  int moveListStarts[arrSize]; //Buf idx of start of list
  int moveListLens[arrSize]; //Len of list
  int moveListCur[arrSize]; //Current move list idx searched, equal to -1 if list has not been generated.
  MoveRecord records[arrSize]; //Records so that we can undo moves as we search back up.
  int stackIdx = 0;

  moveListCur[0] = -1;
  moveListStarts[0] = 0;
  moveListLens[0] = 0;
  bool returnValue = false;
  bool returnedFromDeeper = false;
  // bool print = true;

  while(true) {
    // if(print) cout << ": " << stackIdx << " " << moveListCur[stackIdx] << " " << moveListStarts[stackIdx] << " " << moveListLens[stackIdx] << " " << returnValue << " " << returnedFromDeeper << endl;

    //Returned from the root - so that's the answer
    if(stackIdx <= -1) {
      assert(stackIdx == -1);
      ko_loc = ko_loc_saved;
      return returnValue;
    }

    //If we hit the stack limit, just consider it a failed ladder.
    if(stackIdx >= arrSize-1) {
      returnValue = true; returnedFromDeeper = true; stackIdx--; continue;
    }

    bool isDefender = (defenderFirst && (stackIdx % 2) == 0) || (!defenderFirst && (stackIdx % 2) == 1);

    //We just entered this level?
    if(moveListCur[stackIdx] == -1) {
      int libs = chain_data[chain_head[loc]].num_liberties;

      //Base cases.
      //If we are the attacker and the group has only 1 liberty, we already win.
      if(!isDefender && libs <= 1) { returnValue = true; returnedFromDeeper = true; stackIdx--; continue; }
      //If we are the attacker and the group has 3 liberties, we already lose.
      if(!isDefender && libs >= 3) { returnValue = false; returnedFromDeeper = true; stackIdx--; continue; }
      //If we are the defender and the group has 2 liberties, we already win.
      if(isDefender && libs >= 2) { returnValue = false; returnedFromDeeper = true; stackIdx--; continue; }
      //If we are the defender and the attacker left a simple ko point, assume we already win
      //because we don't want to say yes on ladders that depend on kos
      //This should also hopefully prevent any possible infinite loops - I don't know of any infinite loop
      //that would come up in a continuous atari sequence that doesn't ever leave a simple ko point.
      if(isDefender && ko_loc != NULL_LOC) { returnValue = false; returnedFromDeeper = true; stackIdx--; continue; }

      //Otherwise we need to keep searching.
      //Generate the move list. Attacker and defender generate moves on the group's liberties, but only the defender
      //generates moves on surrounding capturable opposing groups.
      int start = moveListStarts[stackIdx];
      if(isDefender) {
        moveListLens[stackIdx] = findLibertyGainingCaptures(loc,buf,start,start);
        moveListLens[stackIdx] += findLiberties(loc,buf,start,start+moveListLens[stackIdx]);
      }
      else {
        moveListLens[stackIdx] += findLiberties(loc,buf,start,start);
        assert(moveListLens[stackIdx] == 2);

        int libs0 = getNumImmediateLiberties(buf[start]);
        int libs1 = getNumImmediateLiberties(buf[start+1]);

        //If we are the attacker and we're in a double-ko death situation, then assume we win.
        //Both defender liberties must be ko mouths, connecting either ko mouth must not increase the defender's
        //liberties, and none of the attacker's surrounding stones can currently be in atari.
        //This is not complete - there are situations where the defender's connections increase liberties, or where
        //the attacker has stones in atari, but where the defender is still in inescapable atari even if they have
        //a large finite number of ko threats. But it's better than nothing. 
        if(libs0 == 0 && libs1 == 0 && wouldBeKoCapture(buf[start],opp) && wouldBeKoCapture(buf[start+1],opp)) {
          if(getNumLibertiesAfterPlay(buf[start],pla,3) <= 2 && getNumLibertiesAfterPlay(buf[start+1],pla,3) <= 2) {
            if(!hasLibertyGainingCaptures(loc))
            { returnValue = true; returnedFromDeeper = true; stackIdx--; continue; }
          }
        }
        
        //Early quitouts if the liberties are not adjacent
        //(so that filling one doesn't fill an immediate liberty of the other)
        if(!Location::isAdjacent(buf[start],buf[start+1],x_size)) {
          //We lose automatically if both escapes get the defender too many libs
          if(libs0 >= 3 && libs1 >= 3)
          { returnValue = false; returnedFromDeeper = true; stackIdx--; continue; }
          //Move 1 is not possible, so shrink the list
          else if(libs0 >= 3)
          { moveListLens[stackIdx] = 1; }
          //Move 0 is not possible, so swap and shrink the list
          else if(libs1 >= 3)
          { buf[start] = buf[start+1]; moveListLens[stackIdx] = 1; }
        }
        //Order the two moves based on a simple heuristic - for each neighboring group with any liberties
        //count that the opponent could connect to, count liberties - 1.5.
        if(moveListLens[stackIdx] > 1) {
          libs0 = libs0 * 2 + countHeuristicConnectionLibertiesX2(buf[start],pla);
          libs1 = libs1 * 2 + countHeuristicConnectionLibertiesX2(buf[start+1],pla);
          if(libs1 > libs0) {
            int tmp = buf[start];
            buf[start] = buf[start+1];
            buf[start+1] = tmp;
          }
        }
      }

      //And indicate to begin search on the first move generated.
      moveListCur[stackIdx] = 0;
    }
    //Else, we returned from a deeper level (or the same level, via illegal move)
    else {
      assert(moveListCur[stackIdx] >= 0);
      assert(moveListCur[stackIdx] < moveListLens[stackIdx]);
      //If we returned from deeper we need to undo the move we made
      if(returnedFromDeeper)
        undo(records[stackIdx]);

      //Defender has a move that is not ladder captured?
      if(isDefender && !returnValue) {
        //Return! (returnValue is still false, as desired)
        returnedFromDeeper = true;
        stackIdx--;
        continue;
      }
      //Attacker has a move that does ladder capture?
      if(!isDefender && returnValue) {
        //Return! (returnValue is still true, as desired)
        returnedFromDeeper = true;
        stackIdx--;
        continue;
      }

      //Move on to the next move to search
      moveListCur[stackIdx]++;
    }

    //If there is no next move to search, then we lose.
    if(moveListCur[stackIdx] >= moveListLens[stackIdx]) {
      //For a defender, that means a ladder capture.
      //For an attacker, that means no ladder capture found.
      returnValue = isDefender;
      returnedFromDeeper = true;
      stackIdx--;
      continue;
    }

    //Otherwise we do have an next move to search. Grab it.
    Loc move = buf[moveListStarts[stackIdx] + moveListCur[stackIdx]];
    Player p = (isDefender ? pla : opp);

    // if(print) cout << "play " << Location::getX(move,19) << " " << Location::getY(move,19) << " " << p << endl;

    //Illegal move - treat it the same as a failed move, but don't return up a level so that we
    //loop again and just try the next move.
    if(!isLegal(move,p)) {
      returnValue = isDefender;
      returnedFromDeeper = false;
      // if(print) cout << "illegal " << endl;
      continue;
    }

    //Play and record the move!
    records[stackIdx] = playMoveRecorded(move,p);

    //And recurse to the next level
    stackIdx++;
    moveListCur[stackIdx] = -1;
    moveListStarts[stackIdx] = moveListStarts[stackIdx-1] + moveListLens[stackIdx-1];
    moveListLens[stackIdx] = 0;
  }

}

//If a point in [result] is a pass-alive stone or pass-alive territory for a color, mark it that color, otherwise mark it C_EMPTY.
//Pass-alive assumes suicide is possible (even if according to the rules it would not be)
void FastBoard::calculatePassAliveTerritory(Color* result) const {
  for(int i = 0; i<MAX_ARR_SIZE; i++)
    result[i] = C_EMPTY;
  calculatePassAliveTerritoryForPla(P_BLACK,result);
  calculatePassAliveTerritoryForPla(P_WHITE,result);
}

void FastBoard::calculatePassAliveTerritoryForPla(Player pla, Color* result) const {
  Color opp = getOpp(pla);

  //First compute all empty-or-opp regions

  //For each loc, if it's empty or opp, the head of the region
  Loc regionHeadByLoc[MAX_ARR_SIZE];
  //For each loc, if it's empty or opp, the next empty or opp belonging to the same region
  Loc nextEmptyOrOpp[MAX_ARR_SIZE];
  //Does this border a pla group that has been marked as not pass alive?
  bool bordersNonPassAlivePlaByHead[MAX_ARR_SIZE];

  //A list for reach region head, indicating which pla group heads the region is vital for.
  //A region is vital for a pla group if all its spaces are adjacent to that pla group.
  //All lists are concatenated together, the most we can have is bounded by (MAX_LEN * MAX_LEN+1) / 2
  //independent regions, each one vital for at most 4 pla groups, add some extra just in case.
  int maxRegions = (MAX_LEN * MAX_LEN + 1)/2 + 1;
  int vitalForPlaHeadsListsMaxLen = maxRegions * 4;
  Loc vitalForPlaHeadsLists[vitalForPlaHeadsListsMaxLen];
  int vitalForPlaHeadsListsTotal = 0;

  //A list of region heads
  int numRegions = 0;
  Loc regionHeads[maxRegions];
  //Start indices and list lengths in vitalForPlaHeadsLists
  uint16_t vitalStart[maxRegions];
  uint16_t vitalLen[maxRegions];
  //For each head of a region, 0, 1, or 2+ spaces of that region not bordering any pla
  uint8_t numInternalSpacesMax2[maxRegions];

  for(int i = 0; i<MAX_ARR_SIZE; i++) {
    regionHeadByLoc[i] = NULL_LOC;
    nextEmptyOrOpp[i] = NULL_LOC;
    bordersNonPassAlivePlaByHead[i] = false;
  }

  auto isAdjacentToPlaHead = [pla,this](Loc loc, Loc plaHead) {
    for(int i = 0; i<4; i++) {
      Loc adj = loc + adj_offsets[i];
      if(colors[adj] == pla && chain_head[adj] == plaHead)
        return true;
    }
    return false;
  };

  std::function<Loc(Loc,Loc,Loc,int)> buildRegion;
  buildRegion = [pla,opp,
                 &regionHeadByLoc,
                 &vitalForPlaHeadsLists,
                 &vitalStart,&vitalLen,&numInternalSpacesMax2,
                 this,
                 &isAdjacentToPlaHead,&nextEmptyOrOpp,&buildRegion](Loc head, Loc tailTarget, Loc loc, int regionIdx) -> Loc {
    if(regionHeadByLoc[loc] != NULL_LOC)
      return tailTarget;
    regionHeadByLoc[loc] = head;

    //First, filter out any pla heads it turns out we're not vital for because we're not adjacent to them
    //In the case where suicide is allowed, we only do this filtering on intersections that are actually empty
    {
      if(isMultiStoneSuicideLegal || colors[loc] == C_EMPTY) {
        uint16_t vStart = vitalStart[regionIdx];
        uint16_t oldVLen = vitalLen[regionIdx];
        uint16_t newVLen = 0;
        for(uint16_t i = 0; i<oldVLen; i++) {
          if(isAdjacentToPlaHead(loc,vitalForPlaHeadsLists[vStart+i])) {
            vitalForPlaHeadsLists[vStart+newVLen] = vitalForPlaHeadsLists[vStart+i];
            newVLen += 1;
          }
        }
        vitalLen[regionIdx] = newVLen;
      }
    }

    //Determine if this point is internal
    {
      bool isInternal = true;
      for(int i = 0; i<4; i++)
      {
        Loc adj = loc + adj_offsets[i];
        if(colors[adj] == pla) {
          isInternal = false;
          break;
        }
      }
      if(isInternal && numInternalSpacesMax2[regionIdx] < 2)
        numInternalSpacesMax2[regionIdx] += 1;
    }

    //Next, recurse everywhere
    nextEmptyOrOpp[loc] = tailTarget;
    Loc nextTailTarget = loc;
    for(int i = 0; i<4; i++)
    {
      Loc adj = loc + adj_offsets[i];
      if(colors[adj] == C_EMPTY || colors[adj] == opp)
        nextTailTarget = buildRegion(head,nextTailTarget,adj,regionIdx);
    }
    return nextTailTarget;
  };

  for(int y = 0; y < y_size; y++) {
    for(int x = 0; x < x_size; x++) {
      Loc loc = Location::getLoc(x,y,x_size);
      if(regionHeadByLoc[loc] != NULL_LOC)
        continue;
      if(colors[loc] != C_EMPTY)
        continue;
      int regionIdx = numRegions;
      numRegions++;
      assert(numRegions <= maxRegions);

      //Initialize region metadata
      Loc head = loc;
      regionHeads[regionIdx] = head;
      vitalStart[regionIdx] = vitalForPlaHeadsListsTotal;
      vitalLen[regionIdx] = 0;
      numInternalSpacesMax2[regionIdx] = 0;

      //Fill in all adjacent pla heads as vital, which will get filtered during buildRegion
      {
        uint16_t vStart = vitalStart[regionIdx];
        assert(vStart + 4 <= vitalForPlaHeadsListsMaxLen);
        uint16_t initialVLen = 0;
        for(int i = 0; i<4; i++) {
          Loc adj = loc + adj_offsets[i];
          if(colors[adj] == pla) {
            Loc head = chain_head[adj];
            bool alreadyPresent = false;
            for(int j = 0; j<initialVLen; j++) {
              if(vitalForPlaHeadsLists[vStart+j] == head) {
                alreadyPresent = true;
                break;
              }
            }
            if(!alreadyPresent) {
              vitalForPlaHeadsLists[vStart+initialVLen] = head;
              initialVLen += 1;
            }
          }
        }
        vitalLen[regionIdx] = initialVLen;
      }
      Loc tailTarget = buildRegion(head,head,loc,regionIdx);
      nextEmptyOrOpp[head] = tailTarget;

      assert(numInternalSpacesMax2[regionIdx] == 0 || vitalLen[regionIdx] == 0);
      vitalForPlaHeadsListsTotal += vitalLen[regionIdx];

      // for(int k = 0; k<vitalLen[regionIdx]; k++)
      //   cout << Location::toString(head,x_size) << "is vital for" << Location::toString(vitalForPlaHeadsLists[vitalStart[regionIdx]+k],x_size) << endl;
    }
  }

  //Also accumulate all player heads
  int numPlaHeads = 0;
  Loc allPlaHeads[MAX_PLAY_SIZE];
  {
    //Accumulate with duplicates
    for(int y = 0; y < y_size; y++) {
      for(int x = 0; x < x_size; x++) {
        Loc loc = Location::getLoc(x,y,x_size);
        if(colors[loc] == pla) {
          allPlaHeads[numPlaHeads++] = chain_head[loc];
        }
      }
    }
    //Filter duplicates
    std::sort(allPlaHeads,allPlaHeads+numPlaHeads);
    int newNumPlaHeads = 0;
    Loc prevHead = NULL_LOC;
    for(int i = 0; i<numPlaHeads; i++) {
      if(allPlaHeads[i] != prevHead) {
        allPlaHeads[newNumPlaHeads] = allPlaHeads[i];
        newNumPlaHeads++;
      }
      prevHead = allPlaHeads[i];
    }
    numPlaHeads = newNumPlaHeads;
  }
  bool plaHasBeenKilled[numPlaHeads];
  for(int i = 0; i<numPlaHeads; i++)
    plaHasBeenKilled[i] = false;

  //Now, we can begin the benson iteration
  uint16_t vitalCountByPlaHead[MAX_ARR_SIZE];
  while(true) {
    //Zero out vital liberties by head
    for(int i = 0; i<numPlaHeads; i++)
      vitalCountByPlaHead[allPlaHeads[i]] = 0;

    //Walk all regions that are still bordered only by pass-alive stuff and accumulate a vital liberty to each pla it is vital for.
    for(int i = 0; i<numRegions; i++) {
      if(numInternalSpacesMax2[i] > 0)
        continue;
      Loc head = regionHeads[i];
      if(bordersNonPassAlivePlaByHead[head])
        continue;

      int vStart = vitalStart[i];
      int vLen = vitalLen[i];
      for(int j = 0; j<vLen; j++) {
        Loc plaHead = vitalForPlaHeadsLists[vStart+j];
        vitalCountByPlaHead[plaHead] += 1;
      }
    }

    //Walk all player heads and kill them if they haven't accumulated at least 2 vital liberties
    bool killedAnything = false;
    for(int i = 0; i<numPlaHeads; i++) {
      //Already killed - skip
      if(plaHasBeenKilled[i])
        continue;

      Loc plaHead = allPlaHeads[i];
      if(vitalCountByPlaHead[plaHead] < 2) {
        plaHasBeenKilled[i] = true;
        killedAnything = true;
        //Walk the pla chain to update bordering regions
        Loc cur = plaHead;
        do {
          for(int j = 0; j<4; j++) {
            Loc adj = cur + adj_offsets[j];
            if(colors[adj] == C_EMPTY || colors[adj] == opp)
              bordersNonPassAlivePlaByHead[regionHeadByLoc[adj]] = true;
          }
          cur = next_in_chain[cur];
        } while (cur != plaHead);
      }
    }

    if(!killedAnything)
      break;
  }

  //Mark result with pass-alive groups
  for(int i = 0; i<numPlaHeads; i++) {
    if(!plaHasBeenKilled[i]) {
      Loc plaHead = allPlaHeads[i];
      Loc cur = plaHead;
      do {
        result[cur] = pla;
        cur = next_in_chain[cur];
      } while (cur != plaHead);
    }
  }

  //Mark result with pass-alive territory
  for(int i = 0; i<numRegions; i++) {
    if(numInternalSpacesMax2[i] > 1)
      continue;
    Loc head = regionHeads[i];
    if(bordersNonPassAlivePlaByHead[head])
      continue;
    Loc cur = head;
    do {
      result[cur] = pla;
      cur = nextEmptyOrOpp[cur];
    } while (cur != head);
  }

  return;
}


void FastBoard::checkConsistency() const {
  const char* errLabel = "FastBoard::checkConsistency()";

  auto checkChainConsistency = [errLabel,this](Loc loc) {
    Player pla = colors[loc];
    Loc head = chain_head[loc];
    Loc cur = loc;
    int stoneCount = 0;
    int pseudoLibs = 0;
    bool foundChainHead = false;
    do {
      if(colors[cur] != pla)
        throw StringError(errLabel,"Chain is not all the same color");
      if(chain_head[cur] != head)
        throw StringError(errLabel,"Chain does not all have the same head");

      stoneCount++;
      pseudoLibs += getNumImmediateLiberties(cur);
      if(cur == head)
        foundChainHead = true;
      
      if(stoneCount > MAX_PLAY_SIZE)
        throw StringError(errLabel,"Chain exceeds size of board - broken circular list?");
      cur = next_in_chain[cur];
    } while (cur != loc);

    if(!foundChainHead)
      throw StringError(errLabel,"Chain loop does not contain head");
    
    const ChainData& data = chain_data[head];
    if(data.owner != pla)
      throw StringError(errLabel,"Chain data owner does not match stones");
    if(data.num_locs != stoneCount)
      throw StringError(errLabel,"Chain data num_locs does not match actual stone count");
    if(data.num_liberties > pseudoLibs)
      throw StringError(errLabel,"Chain data liberties exceeds pseudoliberties");    
    if(data.num_liberties <= 0)
      throw StringError(errLabel,"Chain data liberties is nonpositive");    
  };

  Hash128 tmp_pos_hash = ZOBRIST_SIZE_X_HASH[x_size] ^ ZOBRIST_SIZE_Y_HASH[y_size];
  int emptyCount = 0;
  for(Loc loc = 0; loc < MAX_ARR_SIZE; loc++) {
    int x = Location::getX(loc,x_size);
    int y = Location::getY(loc,x_size);
    if(x < 0 || x >= x_size || y < 0 || y >= y_size) {
      if(colors[loc] != C_WALL)
        throw StringError(errLabel,"Non-WALL value outside of board legal area");
    }
    else {
      if(colors[loc] == C_BLACK || colors[loc] == C_WHITE) {
        checkChainConsistency(loc);
        if(empty_list.contains(loc))
          throw StringError(errLabel,"Empty list contains filled location");

        tmp_pos_hash ^= ZOBRIST_BOARD_HASH[loc][colors[loc]];
        tmp_pos_hash ^= ZOBRIST_BOARD_HASH[loc][C_EMPTY];
      }
      else if(colors[loc] == C_EMPTY) {
        if(!empty_list.contains(loc))
          throw StringError(errLabel,"Empty list doesn't contain empty location");
        emptyCount += 1;
      }
      else
        throw StringError(errLabel,"Non-(black,white,empty) value within board legal area");
    }
  }

  if(pos_hash != tmp_pos_hash)
    throw StringError(errLabel,"Pos hash does not match expected");

  if(empty_list.size_ != emptyCount)
    throw StringError(errLabel,"Empty list size is not the number of empty points");
  for(int i = 0; i<emptyCount; i++) {
    Loc loc = empty_list.list_[i];
    int x = Location::getX(loc,x_size);
    int y = Location::getY(loc,x_size);
    if(x < 0 || x >= x_size || y < 0 || y >= y_size)
      throw StringError(errLabel,"Invalid empty list loc");
    if(empty_list.indices_[loc] != i)
      throw StringError(errLabel,"Empty list index for loc in index i is not i");
  }

  if(ko_loc != NULL_LOC) {
    int x = Location::getX(ko_loc,x_size);
    int y = Location::getY(ko_loc,x_size);
    if(x < 0 || x >= x_size || y < 0 || y >= y_size)
      throw StringError(errLabel,"Invalid simple ko loc");
    if(getNumImmediateLiberties(ko_loc) != 0)
      throw StringError(errLabel,"Simple ko loc has immediate liberties");
  }
  
  short tmpAdjOffsets[8];
  Location::getAdjacentOffsets(tmpAdjOffsets,x_size);
  for(int i = 0; i<8; i++)
    if(tmpAdjOffsets[i] != adj_offsets[i])
        throw StringError(errLabel,"Corrupted adj_offsets array");
 
}

