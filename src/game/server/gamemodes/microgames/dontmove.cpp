// WarioWare mod by Headshotnoby

#include <engine/shared/config.h>
#include "dontmove.h"

const char *modes[7][2] = {
	{"不要移动!", "不要停下!"},
	{"保持静止!", "不要保持静止!"},
	{"保持当前位置!", "保持移动!"},
	{"站立不动!", "移动你的身体!"},
	{"不能有人动!", "所有人，一起动!"},
	{"所有人，一起保持静止!", "不能有人停下!"},
	{"速速冻结!", "不停走路!"}
};


MGDontMove::MGDontMove(CGameContext* pGameServer, CGameControllerWarioWare* pController) : Microgame(pGameServer, pController)
{
	m_microgameName = "别动";
	m_boss = false;
}

void MGDontMove::Start()
{
	m_Phrase = rand() % 2;
	m_Mode = rand() % 2;			
	GameServer()->SendBroadcast(modes[m_Phrase][m_Mode], -1);
	Controller()->setPlayerTimers(g_Config.m_WwSndMgDontMove_Offset, g_Config.m_WwSndMgDontMove_Length);
	for (int i=0; i<MAX_CLIENTS; i++)
	{
		CCharacter *Char = GameServer()->GetPlayerChar(i);
		if (not Char) continue;

		Controller()->g_Complete[i] = true;
		Char->SetCollideOthers(false);
		Char->SetHookOthers(false);
	}
}

void MGDontMove::End()
{
	// nothing to clean
}

void MGDontMove::Tick()
{
	float timeLeft = Controller()->getTimeLength() - Controller()->getTimer();

	for (int i=0; i<MAX_CLIENTS; i++)
	{
		CCharacter *Char = GameServer()->GetPlayerChar(i);
		if (not Char) continue;

		if (timeLeft < 2750 and timeLeft > 200)
		{
			if ((not m_Mode and (Char->IsMoving() or Char->GetInput()->m_Hook&1 or Char->GetInput()->m_Fire&1 or Char->GetInput()->m_Jump==1)) or // don't move
				(m_Mode and (not Char->IsMoving() and (Char->Core()->m_Vel.x < 1 and Char->Core()->m_Vel.x > -1) and (Char->Core()->m_Vel.y < 1 and Char->Core()->m_Vel.y > -1)))) // don't stop moving
			{
				Controller()->killAndLoseMicroGame(i);
			}
		}
	}
}