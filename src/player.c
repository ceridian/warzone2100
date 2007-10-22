/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2007  Warzone Resurrection Project

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
/*
 * Player.c
 *
 * Update code for computer player AI
 *
 */

#include <string.h>

#include "lib/framework/frame.h"
#include "objects.h"
#include "map.h"
#include "player.h"
#include "game.h"
#include "order.h"
#include "objectdef.h"


#include "multiplay.h"


/* The position for the computer players to attack */
#define ATTACKX		39
#define ATTACKY		42

//defined in StructureDef.h - AB 6/1/98
/* Quick hack to choose an assembly point for player droids */
/*struct _pos {
	UDWORD	x,y;
} aAssemblyPos[MAX_PLAYERS] =
{
	{ 39,39 },
	{ 34,23 },
	{ 26,37 },
	{ 34,23 },
	{ 34,23 },
	{ 34,23 },
	{ 26,37 },
	{ 34,23 },
};*/

struct _position2
{
	UDWORD x,y;
};
/* location of computer player bases */
struct _position2 aBasePos[MAX_PLAYERS] =
{
	{ 0,0 },
	{ 0,0 },
	{ 0,0 },
	{ 0,0 },
	{ 0,0 },
	{ 0,0 },
	{ 20,40 },
	{ 34,19 },
};

/* distance from base to check for enemy droids */
#define DROID_SCAN		7

/* Quick hack to choose droid templates to build */
//#define MAX_DROIDTYPES	3
/*struct _man_info {
	UDWORD		templIndex;		// which template to build
	UDWORD		points;			// number of points towards group total
} aManInfo[MAX_PLAYERS][MAX_DROIDTYPES] =
{
	{	// player 0
		{ 1, 1 }, { 3, 2 },	{ 2, 3 },
	},
	{	// player 1
		{ 1, 1 }, { 3, 2 },	{ 2, 3 },
	},
	{	// player 2
		{ 1, 1 }, { 3, 2 },	{ 2, 3 },
	},
	{	// player 3
		{ 1, 1 }, { 3, 2 },	{ 2, 3 },
	},
	{	// player 4
		{ 1, 1 }, { 3, 2 },	{ 2, 3 },
	},
	{	// player 5
		{ 1, 1 }, { 3, 2 },	{ 2, 3 },
	},
	{	// player 6
		{ 1, 1 }, { 4, 3 },	{ 2, 4 },
	},
	{	// player 7
		{ 1, 1 }, { 4, 3 },	{ 2, 4 },
	},
};

UDWORD	numManInfo[MAX_PLAYERS] =	// Number of the above structs valid for each player
{
	3, 3, 3, 3, 3, 3, 3, 3,			// first 3 was  a 0, changed for multiplayer (ajl)
};*/
UDWORD	attackPoints[MAX_PLAYERS] =	// Number of points for the attack group
{
	7, 7, 7, 7, 7, 7, 9, 9,			// first 7 was a 0, changed for multiplayer (ajl)
};

typedef struct _player_ai
{
	DROID	*psAttackGrp;		// The group of droids that have been built to attack the player
	UDWORD	groupPoints;
	BOOL	building;			// Whether the player is building an attack group
} PLAYER_AI;

PLAYER_AI	*asPlayerAI;


/* Initialise the player AI system */
BOOL playerInitialise(void)
{
	asPlayerAI = (PLAYER_AI *)malloc(sizeof(PLAYER_AI) * MAX_PLAYERS);
	if (!asPlayerAI)
	{
		debug( LOG_ERROR, "Out of memory" );
		abort();
		return FALSE;
	}
	memset(asPlayerAI, 0, sizeof(PLAYER_AI) * MAX_PLAYERS);

	return TRUE;
}


/* Reset the player AI after a load game */
void playerReset(void)
{
	memset(asPlayerAI, 0, sizeof(PLAYER_AI) * MAX_PLAYERS);
}


/* Shutdown the player AI system */
void playerShutDown(void)
{
	free(asPlayerAI);
	asPlayerAI = NULL;
}


/* Check a group list for dead droids */
/*static void playerCheckGroup(DROID **ppsDroid)
{
	DROID	*psPrev = NULL, *psCurr;;

	while (*ppsDroid && (*ppsDroid)->died)
	{
		*ppsDroid = (*ppsDroid)->sAI.psGroup;
	}
	for(psCurr = *ppsDroid; psCurr; psCurr = psCurr->sAI.psGroup)
	{
		if (psCurr->died)
		{
			psPrev->sAI.psGroup = psCurr->sAI.psGroup;
		}
		else
		{
			psPrev = psCurr;
		}
	}
}*/


/* Update the AI for a player */
void playerUpdate(UDWORD player)
{
//	DROID		*psCurr;
//	STRUCTURE	*psStruct;


	if ((!bMultiPlayer) && (player ==0))		// this'll have to come out to allow p1 AI
	{											// currently stops the whole thing crashing.
		return;
	}
	else if( bMultiPlayer && (isHumanPlayer(player)))
	{
		return;
	}


	// See if the human player has been seen by this player
/*	if (!asPlayerAI[player].building)
	{
		for(psCurr = apsDroidLists[0]; psCurr; psCurr = psCurr->psNext)
		{
			if (psCurr->visible[player])
			{
				// Seen one - start manufacturing droids to attack with
				playerStartManufacture(player);
				asPlayerAI[player].building = TRUE;
				break;
			}
		}
	}
	else
	{
		// Check the factory is still producing
		for(psStruct = apsStructLists[player]; psStruct; psStruct = psStruct->psNext)
		{
			if (psStruct->pStructureType->type == REF_FACTORY)
			{
				break;
			}
		}
		if (psStruct && ((FACTORY *)psStruct->pFunctionality)->psSubject == NULL)
		{
			// Factory isn't building - restart it
			playerStartManufacture(player);
		}
	}
*/
	// Check none of the droids in the attack group have died
//	playerCheckGroup(&asPlayerAI[player].psAttackGrp);
}

/* deal with a new droid being built by a factory */
void playerNewDroid(DROID *psDroid)
{
//	UDWORD			player;
//	UDWORD			newManu, templIndex;
//	SDWORD			xdiff, ydiff, radSquared;
//	DROID_TEMPLATE	*psTempl;
//	STRUCTURE		*psStruct;
//	DROID			*psCurr;
//	BOOL			foundDroid;
//	char			droidName[MAX_NAME_SIZE];

/*	if (!psDroid)
	{
		return;
	}

	// Check it isn't the human player
	player = psDroid->player;

	if( (!bMultiPlayer) && (player == 0))
	{
		return;
	}
	else if((bMultiPlayer) && (IsHumanPlayer(player)) )
	{
		return;
	}

	// Add it to the attack group
	psDroid->sAI.psGroup = asPlayerAI[player].psAttackGrp;
	asPlayerAI[player].psAttackGrp = psDroid;
*/
	//This now happens in buildDroid - AB 6/1/98
	//move droid to assembly point
	//orderDroidLoc(psDroid, DORDER_MOVE, world_coord(aAssemblyPos[player].x),
	//		world_coord(aAssemblyPos[player].y));

	//SCRIPTED NOW
	/* Move the droid to the assembly point
	if (asPlayerAI[player].groupPoints < attackPoints[player])
	{
		orderDroidLoc(psDroid, DORDER_MOVE,
					world_coord(aAssemblyPos[player].x),
					world_coord(aAssemblyPos[player].y));
	}
	else
	{
		// send the droids to attack -
		   first see if there are any droids close to the base
		foundDroid = FALSE;
		radSquared = DROID_SCAN*DROID_SCAN;
		for(psCurr = apsDroidLists[0]; psCurr; psCurr = psCurr->psNext)
		{
			xdiff = map_coord(psCurr->x) - aBasePos[player].x;
			ydiff = map_coord(psCurr->y) - aBasePos[player].y;
			if (xdiff*xdiff + ydiff*ydiff < radSquared)
			{
				foundDroid = TRUE;
				orderGroupObj(psDroid, DORDER_ATTACK, (BASE_OBJECT *)psCurr);
			}
		}
		if (!foundDroid)
		{
			orderGroupLoc(psDroid, DORDER_MOVE, world_coord(ATTACKX), world_coord(ATTACKY));
//				asPlayerAI[player].psAttackGrp = NULL;
			asPlayerAI[player].groupPoints = 0;
		}
	}
*/
	//THIS IS SCRIPTED NOW...
	/* Find the factory */
/*	for(psStruct = apsStructLists[player]; psStruct; psStruct = psStruct->psNext)
	{
		if (psStruct->pStructureType->type == REF_FACTORY)
		{
			break;
		}
	}

	// Choose a new droid to build
	newManu = rand() % numManInfo[player];
	templIndex = aManInfo[player][newManu].templIndex;
//	psTempl = apsDroidTemplates[player];
//	for(i=0; i != templIndex && psTempl; i++, psTempl = psTempl->psNext)
//		;
	switch (templIndex)
	{
	case 1:
		strcpy(droidName,"BaBa People");
		break;
	case 2:
		strcpy(droidName, "Barbarian Buggy");
		break;
	case 4:
		strcpy(droidName, "Barbarian Trike");
	}
	for (psTempl = apsDroidTemplates[player]; psTempl; psTempl = psTempl->psNext)
	{
		if (!strcmp(psTempl->pName, droidName))
		{
			break;
		}
	}
	if (psTempl)
	{
		// Start the production of the droid
		structSetManufacture(psStruct, psTempl, 1);
		asPlayerAI[player].groupPoints += aManInfo[player][newManu].points;
	}*/
}


/* Get a player to start manufacturing droids */
/*void playerStartManufacture(UDWORD	player)
{
	UDWORD			newManu, templIndex;
	STRUCTURE		*psStruct;
	DROID_TEMPLATE	*psTempl;
	char			droidName[MAX_NAME_SIZE];

	// Find the factory
	for(psStruct = apsStructLists[player]; psStruct; psStruct = psStruct->psNext)
	{
		if (psStruct->pStructureType->type == REF_FACTORY)
		{
			break;
		}
	}

	// Choose a new droid to build
	if (psStruct)
	{
		newManu = rand() % numManInfo[player];
		templIndex = aManInfo[player][newManu].templIndex;
//		psTempl = apsDroidTemplates[player];
//		for(i=0; i != templIndex && psTempl; i++, psTempl = psTempl->psNext)
//			;
		switch (templIndex)
		{
		case 1:
			strcpy(droidName,"BaBa People");
			break;
		case 2:
			strcpy(droidName, "Barbarian Buggy");
			break;
		case 4:
			strcpy(droidName, "Barbarian Trike");
		}
		for (psTempl = apsDroidTemplates[player]; psTempl; psTempl = psTempl->psNext)
		{
			if (!strcmp(psTempl->pName, droidName))
			{
				break;
			}
		}
		if (psTempl)
		{
			// Start the production of the droid
			structSetManufacture(psStruct, psTempl, 1);
			asPlayerAI[player].groupPoints += aManInfo[player][newManu].points;
		}
	}
}
*/
//New version in Structure.c - AB 6/1/98
/*sets the point new droids go to - x/y in world coords*/
/*void setAssemblyPoint(UDWORD x, UDWORD y, UDWORD player)
{
	aAssemblyPos[player].x = map_coord(x);
	aAssemblyPos[player].y = map_coord(y);
}*/

/* sends players droids to attack a specified x/y. Checks to see if any enemy
droids are near to the home base first*/
void attackLocation(UDWORD x, UDWORD y, UDWORD player)
{
/*	BOOL	foundDroid;
	DROID	*psDroid, *psCurr;
	SDWORD	xdiff, ydiff, radSquared;

	if( (!bMultiPlayer) && (player == 0))
	{
		return;
	}
	else if((bMultiPlayer) && (IsHumanPlayer(player)) )
	{
		return;
	}

	// send the droids to attack
	psDroid = asPlayerAI[player].psAttackGrp;

	foundDroid = FALSE;
	radSquared = DROID_SCAN*DROID_SCAN;

	//first see if there are any droids close to the base
	for(psCurr = apsDroidLists[0]; psCurr; psCurr = psCurr->psNext)
	{
		xdiff = map_coord(psCurr->x) - aBasePos[player].x;
		ydiff = map_coord(psCurr->y) - aBasePos[player].y;
		if (xdiff*xdiff + ydiff*ydiff < radSquared)
		{
			foundDroid = TRUE;
			orderGroupObj(psDroid, DORDER_ATTACK, (BASE_OBJECT *)psCurr);
		}
	}
	//send them to location if none near
	if (!foundDroid)
	{
		orderGroupLoc(psDroid, DORDER_MOVE, x, y);
//		asPlayerAI[player].groupPoints = 0;
	}*/
}

