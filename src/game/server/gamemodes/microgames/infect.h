
#ifndef _MICROGAME_INFECT_H
#define _MICROGAME_INFECT_H

#include "../microgame.h"

/*
	pinkys must piggyback the twintris
	twintris must escape
*/


class MGInfect : public Microgame
{
public:
	MGInfect(CGameContext* pGameServer, CGameControllerWarioWare* pController);
	~MGInfect() {}

	void Start();
	void End();
	void Tick();

	void OnBotInput(CNetObj_PlayerInput* Input);
	bool OnWinMicrogame(int client, int winTile);

private:
	bool m_Moved; // there is a delay before the bot is moved ingame, to sync with music. this bool makes sure the bot isn't moved twice.
	int m_startTick;

	// AI stuff
	int m_FireTick; // ticks to wait before firing. give the player time to react!
	int m_Target; // current player target
	int m_SwitchTargetTick; // ticks before switching to another target
	bool m_PathFound; // if executed pathfind function

	bool m_IsInfect[MAX_CLIENTS];
};

#endif // _MICROGAME_PIGGYBACK_H