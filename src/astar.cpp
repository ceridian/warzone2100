/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2010  Warzone Resurrection Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/** @file
 *  A* based path finding
 *  See http://en.wikipedia.org/wiki/A*_search_algorithm for more information.
 *  How this works:
 *  * First time (in a given tick)  that some droid  wants to pathfind  to a particular
 *    destination,  the A*  algorithm from source to  destination is used.  The desired
 *    destination,  and the nearest  reachable point  to the  destination is saved in a
 *    Context.
 *  * Second time (in a given tick)  that some droid wants to  pathfind to a particular
 *    destination,  the appropriate  Context is found,  and the A* algorithm is used to
 *    find a path from the nearest reachable point to the destination  (which was saved
 *    earlier), to the source.
 *  * Subsequent times  (in a given tick) that some droid wants to pathfind to a parti-
 *    cular destination,  the path is looked up in appropriate Context.  If the path is
 *    not already known,  the A* weights are adjusted, and the previous A*  pathfinding
 *    is continued until the new source is reached.  If the new source is  not reached,
 *    the droid is  on a  different island than the previous droid,  and pathfinding is
 *    restarted from the first step.
 *  Up to 30 pathfinding maps from A* are cached, in a LRU list. The PathNode heap con-
 *  tains the  priority-heap-sorted  nodes which are to be explored.  The path back  is
 *  stored in the PathExploredTile 2D array of tiles.
 */

#ifndef WZ_TESTING
#include "lib/framework/frame.h"

#include "astar.h"
#include "map.h"
#endif

#include <list>
#include <vector>
#include <algorithm>

/// A coordinate.
struct PathCoord
{
	PathCoord() {}
	PathCoord(int16_t x_, int16_t y_) : x(x_), y(y_) {}
	bool operator ==(PathCoord const &z) const { return x == z.x && y == z.y; }
	bool operator !=(PathCoord const &z) const { return !(*this == z); }

	int16_t x, y;
};

/** The structure to store a node of the route in node table
 *
 *  @ingroup pathfinding
 */
struct PathNode
{
	bool operator <(PathNode const &z) const
	{
		// Sort decending est, fallback to ascending dist, fallback to sorting by position.
		return est  > z.est ||
		      (est == z.est &&
		       dist  < z.dist ||
		      (dist == z.dist &&
		       p.x  < z.p.x ||
		      (p.x == z.p.x &&
		       p.y  < z.p.y)));
	}

	PathCoord p;                    // Map coords.
	unsigned  dist, est;            // Distance so far and estimate to end.
};
struct PathExploredTile
{
	PathExploredTile() : iteration(0xFFFF) {}

	uint16_t iteration;
	int8_t   dx, dy;                // Offset from previous point in the route.
	unsigned dist;                  // Shortest known distance to tile.
	bool     visited;
};

// Data structures used for pathfinding, can contain cached results.
struct PathfindContext
{
	PathfindContext() : iteration(0) { clear(); }
	bool isBlocked(int x, int y) const { return fpathBaseBlockingTile(x, y, propulsion, owner, moveType); }
	bool matches(PROPULSION_TYPE propulsion_, int owner_, FPATH_MOVETYPE moveType_, PathCoord tileS_) const { return propulsion == propulsion_ && owner == owner_ && moveType == moveType_ && tileS == tileS_ && gameTime == myGameTime; }
	void assign(PROPULSION_TYPE propulsion_, int owner_, FPATH_MOVETYPE moveType_, PathCoord tileS_)
	{
		propulsion = propulsion_; owner = owner_; moveType = moveType_; tileS = tileS_;
		myGameTime = gameTime;
		nodes.clear();

		// Make the iteration not match any value of iteration in map.
		if (++iteration == 0xFFFF)
		{
			map.clear();  // There are no values of iteration guaranteed not to exist in map, so clear the map.
			iteration = 0;
		}
		map.resize(mapWidth * mapHeight);  // Allocate space for map, if needed.
	}
	void clear() { assign(PROPULSION_TYPE_NUM, -1, FMT_MOVE, PathCoord(0, 0)); }

	// Type of object which needs a path.
	PROPULSION_TYPE propulsion;
	int             owner;
	FPATH_MOVETYPE  moveType;

	PathCoord       tileS;                // Start tile for pathfinding. (May be either source or target tile.)
	uint32_t        myGameTime;

	PathCoord       nearestCoord;         // Nearest reachable tile to destination.

	/** Counter to implement lazy deletion from map.
	 *
	 *  @see fpathTableReset
	 */
	uint16_t        iteration;

	std::vector<PathNode> nodes;        ///< Edge of explored region of the map.
	std::vector<PathExploredTile> map;  ///< Map, with paths leading back to tileS.
};

/// Last recently used list of contexts.
static std::list<PathfindContext> fpathContexts;

// Convert a direction into an offset
// dir 0 => x = 0, y = -1
static const Vector2i aDirOffset[] =
{
	{ 0, 1},
	{-1, 1},
	{-1, 0},
	{-1,-1},
	{ 0,-1},
	{ 1,-1},
	{ 1, 0},
	{ 1, 1},
};

void fpathHardTableReset()
{
	fpathContexts.clear();
}

/** Get the nearest entry in the open list
 */
/// Takes the current best node, and removes from the node heap.
static inline PathNode fpathTakeNode(std::vector<PathNode> &nodes)
{
	// find the node with the lowest distance
	// if equal totals, give preference to node closer to target
	PathNode ret = nodes.front();

	// remove the node from the list
	std::pop_heap(nodes.begin(), nodes.end());  // Move the best node from the front of nodes to the back of nodes, preserving the heap properties, setting the front to the next best node.
	nodes.pop_back();                           // Pop the best node (which we will be returning).

	return ret;
}

/** Estimate the distance to the target point
 */
static inline unsigned WZ_DECL_CONST fpathEstimate(PathCoord s, PathCoord f)
{
	// Cost of moving horizontal/vertical = 70, cost of moving diagonal = 99, 99/70 = 1.41428571... ≈ √2 = 1.41421356...
	unsigned xDelta = abs(s.x - f.x), yDelta = abs(s.y - f.y);
	return std::min(xDelta, yDelta)*(99 - 70) + std::max(xDelta, yDelta)*70;
}

/** Generate a new node
 */
static inline void fpathNewNode(PathfindContext &context, PathCoord dest, PathCoord pos, unsigned prevDist, PathCoord prevPos)
{
	ASSERT((unsigned)pos.x < (unsigned)mapWidth && (unsigned)pos.y < (unsigned)mapHeight, "X (%d) or Y (%d) coordinate for path finding node is out of range!", pos.x, pos.y);

	// Create the node.
	PathNode node;
	node.p = pos;
	node.dist = prevDist + fpathEstimate(prevPos, pos);
	node.est = node.dist + fpathEstimate(pos, dest);

	PathExploredTile &expl = context.map[pos.x + pos.y*mapWidth];
	if (expl.iteration == context.iteration && (expl.visited || expl.dist <= node.dist))
	{
		return;  // Already visited this tile. Do nothing.
	}

	// Remember where we have been, and remember the way back.
	expl.iteration = context.iteration;
	expl.dx = pos.x - prevPos.x;
	expl.dy = pos.y - prevPos.y;
	expl.dist = node.dist;
	expl.visited = false;

	// Add the node to the node heap.
	context.nodes.push_back(node);                               // Add the new node to nodes.
	std::push_heap(context.nodes.begin(), context.nodes.end());  // Move the new node to the right place in the heap.
}

/// Recalculates estimates to new tileF tile.
static void fpathAStarReestimate(PathfindContext &context, PathCoord tileF)
{
	for (std::vector<PathNode>::iterator node = context.nodes.begin(); node != context.nodes.end(); ++node)
	{
		node->est = node->dist + fpathEstimate(node->p, tileF);
	}

	// Changing the estimates breaks the heap ordering. Fix the heap ordering.
	std::make_heap(context.nodes.begin(), context.nodes.end());
}

/// Returns nearest explored tile to tileF.
static PathCoord fpathAStarExplore(PathfindContext &context, PathCoord tileF)
{
	PathCoord       nearestCoord(0, 0);
	unsigned        nearestDist = 0xFFFFFFFF;

	// search for a route
	while (!context.nodes.empty())
	{
		PathNode node = fpathTakeNode(context.nodes);
		if (context.map[node.p.x + node.p.y*mapWidth].visited)
		{
			continue;  // Already been here.
		}
		context.map[node.p.x + node.p.y*mapWidth].visited = true;

		// note the nearest node to the target so far
		if (node.est - node.dist < nearestDist)
		{
			nearestCoord = node.p;
			nearestDist = node.est - node.dist;
		}

		if (node.p == tileF)
		{
			// reached the target
			nearestCoord = node.p;
			break;
		}

		// loop through possible moves in 8 directions to find a valid move
		for (unsigned dir = 0; dir < ARRAY_SIZE(aDirOffset); ++dir)
		{
			/*
			   5  6  7
			     \|/
			   4 -I- 0
			     /|\
			   3  2  1
			   odd:orthogonal-adjacent tiles even:non-orthogonal-adjacent tiles
			*/
			if (dir % 2 != 0)
			{
				int x, y;

				// We cannot cut corners
				x = node.p.x + aDirOffset[(dir + 1) % 8].x;
				y = node.p.y + aDirOffset[(dir + 1) % 8].y;
				if (context.isBlocked(x, y))
				{
					continue;
				}
				x = node.p.x + aDirOffset[(dir + 7) % 8].x;
				y = node.p.y + aDirOffset[(dir + 7) % 8].y;
				if (context.isBlocked(x, y))
				{
					continue;
				}
			}

			// Try a new location
			int x = node.p.x + aDirOffset[dir].x;
			int y = node.p.y + aDirOffset[dir].y;

			// See if the node is a blocking tile
			if (context.isBlocked(x, y))
			{
				// tile is blocked, skip it
				continue;
			}

			// Now insert the point into the appropriate list, if not already visited.
			fpathNewNode(context, tileF, PathCoord(x, y), node.dist, node.p);
		}
	}

	return nearestCoord;
}

static void fpathInitContext(PathfindContext &context, PROPULSION_TYPE propulsion, int owner, FPATH_MOVETYPE moveType, PathCoord tileS, PathCoord tileRealS, PathCoord tileF)
{
	context.assign(propulsion, owner, moveType, tileS);

	// Add the start point to the open list
	fpathNewNode(context, tileF, tileRealS, 0, tileRealS);
	ASSERT(!context.nodes.empty(), "fpathNewNode failed to add node.");
}

SDWORD fpathAStarRoute(MOVE_CONTROL *psMove, PATHJOB *psJob)
{
	int             retval = ASR_OK;

	bool            mustReverse = true;

	const PathCoord tileOrig(map_coord(psJob->origX), map_coord(psJob->origY));
	const PathCoord tileDest(map_coord(psJob->destX), map_coord(psJob->destY));

	PathCoord endCoord;  // Either nearest coord (mustReverse = true) or orig (mustReverse = false).

	std::list<PathfindContext>::iterator contextIterator = fpathContexts.begin();
	for (contextIterator = fpathContexts.begin(); contextIterator != fpathContexts.end(); ++contextIterator)
	{
		if (!contextIterator->matches(psJob->propulsion, psJob->owner, psJob->moveType, tileDest))
		{
			// This context is not for the same droid type and same destination.
			continue;
		}

		// We have tried going to tileDest before.

		if (contextIterator->map[tileOrig.x + tileOrig.y*mapWidth].iteration == contextIterator->iteration
		 && contextIterator->map[tileOrig.x + tileOrig.y*mapWidth].visited)
		{
			// Already know the path from orig to dest.
			endCoord = tileOrig;
		}
		else
		{
			// Need to find the path from orig to dest, continue previous exploration.
			fpathAStarReestimate(*contextIterator, tileOrig);
			endCoord = fpathAStarExplore(*contextIterator, tileOrig);
		}

		if (endCoord != tileOrig)
		{
			// orig turned out to be on a different island than what this context was used for, so can't use this context data after all.
			continue;
		}

		mustReverse = false;  // We have the path from the nearest reachable tile to dest, to orig.
		break;  // Found the path! Don't search more contexts.
	}

	if (contextIterator == fpathContexts.end())
	{
		// We did not find an appropriate context. Make one.

		if (fpathContexts.size() < 30)
		{
			fpathContexts.push_back(PathfindContext());
		}
		--contextIterator;

		// Init a new context, overwriting the oldest one if we are caching too many.
		// We will be searching from orig to dest, since we don't know where the nearest reachable tile to dest is.
		fpathInitContext(*contextIterator, psJob->propulsion, psJob->owner, psJob->moveType, tileOrig, tileOrig, tileDest);
		endCoord = fpathAStarExplore(*contextIterator, tileDest);
		contextIterator->nearestCoord = endCoord;
	}

	PathfindContext &context = *contextIterator;

	// return the nearest route if no actual route was found
	if (context.nearestCoord != tileDest)
	{
		retval = ASR_NEAREST;
	}

	// Get route, in reverse order.
	static std::vector<Vector2i> path;  // Declared static to save allocations.
	path.clear();

	for (PathCoord p = endCoord; p != context.tileS; p = PathCoord(p.x - context.map[p.x + p.y*mapWidth].dx, p.y - context.map[p.x + p.y*mapWidth].dy))
	{
		ASSERT(tileOnMap(p.x, p.y), "Assigned XY coordinates (%d, %d) not on map!", (int)p.x, (int)p.y);

		Vector2i v = {world_coord(p.x) + TILE_UNITS / 2, world_coord(p.y) + TILE_UNITS / 2};
		path.push_back(v);
	}
	if (path.empty())
	{
		// We are probably already in the destination tile. Go to the exact coordinates.
		Vector2i v = {psJob->destX, psJob->destY};
		path.push_back(v);
	}
	else if (retval == ASR_OK)
	{
		// Found exact path, so use exact coordinates for last point, no reason to lose precision
		Vector2i v = {psJob->destX, psJob->destY};
		if (mustReverse)
		{
			path.front() = v;
		}
		else
		{
			path.back() = v;
		}
	}

	// TODO FIXME once we can change numPoints to something larger than uint16_t
	psMove->numPoints = std::min<int>(UINT16_MAX, path.size());

	// Allocate memory
	psMove->asPath = static_cast<Vector2i *>(malloc(sizeof(*psMove->asPath) * path.size()));
	ASSERT(psMove->asPath, "Out of memory");
	if (!psMove->asPath)
	{
		fpathHardTableReset();
		return ASR_FAILED;
	}

	// get the route in the correct order
	// If as I suspect this is to reverse the list, then it's my suspicion that
	// we could route from destination to source as opposed to source to
	// destination. We could then save the reversal. to risky to try now...Alex M
	//
	// The idea is impractical, because you can't guarentee that the target is
	// reachable. As I see it, this is the reason why psNearest got introduced.
	// -- Dennis L.
	//
	// If many droids are heading towards the same destination, then destination
	// to source would be faster if reusing the information in nodeArray. --Cyp
	if (mustReverse)
	{
		// Copy the list, in reverse.
		std::copy(path.rbegin(), path.rend(), psMove->asPath);

		if (!context.isBlocked(tileOrig.x, tileOrig.y))  // If blocked, searching from tileDest to tileOrig wouldn't find the tileOrig tile.
		{
			// Next time, search starting from nearest reachable tile to the destination.
			fpathInitContext(context, psJob->propulsion, psJob->owner, psJob->moveType, tileDest, context.nearestCoord, tileOrig);
		}
	}
	else
	{
		// Copy the list.
		std::copy(path.begin(), path.end(), psMove->asPath);
	}

	// Move context to beginning of last recently used list.
	if (contextIterator != fpathContexts.begin())  // Not sure whether or not the splice is a safe noop, if equal.
	{
		fpathContexts.splice(fpathContexts.begin(), fpathContexts, contextIterator);
	}

	psMove->DestinationX = psMove->asPath[path.size() - 1].x;
	psMove->DestinationY = psMove->asPath[path.size() - 1].y;

	return retval;
}