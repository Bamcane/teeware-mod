// WarioWare mod by Headshotnoby

#ifndef _MICROGAME_TAKETRAIN_H
#define _MICROGAME_TAKETRAIN_H

#include "../microgame.h"

/*
	take the train.
*/

class MGTakeTrain : public Microgame
{
public:
	MGTakeTrain(CGameContext* pGameServer, CGameControllerWarioWare* pController);
	~MGTakeTrain() {}

	void Start();
	void End();
	void Tick();

	void OnCharacterDamage(int Victim, int Killer, int Dmg, int Weapon);
	void OnBotInput(CNetObj_PlayerInput* Input);

	int m_StartTick;

	bool m_Moved; // there is a delay before the bot is moved ingame, to sync with music. this bool makes sure the bot isn't moved twice.
	int m_startTick;

	// AI stuff
	int m_FireTick; // ticks to wait before firing. give the player time to react!
	int m_Target; // current player target
	int m_SwitchTargetTick; // ticks before switching to another target
	bool m_PathFound; // if executed pathfind function
};

#endif // _MICROGAME_HITENEMY_H