// WarioWare mod by Headshotnoby

#include <engine/shared/config.h>
#include "hitenemy.h"

MGHitEnemy::MGHitEnemy(CGameContext* pGameServer, CGameControllerWarioWare* pController) : Microgame(pGameServer, pController)
{
	m_microgameName = "锤击别人";
	m_boss = false;
}

void MGHitEnemy::Start()
{
	GameServer()->SendBroadcast("锤击别人!", -1);
	Controller()->setPlayerTimers(g_Config.m_WwSndMgHitEnemy_Offset, g_Config.m_WwSndMgHitEnemy_Length);
}

void MGHitEnemy::End()
{
	// nothing to clean
}

void MGHitEnemy::Tick()
{
	// nothing to tick
}

void MGHitEnemy::OnCharacterDamage(int Victim, int Killer, int Dmg, int Weapon)
{
	Controller()->killPlayer(Victim, Killer, Weapon);
}

int MGHitEnemy::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	Controller()->g_Complete[pKiller->GetCID()] = true;
	return 0;
}