// WarioWare mod by Headshotnoby

#include <engine/shared/config.h>
#include "simon.h"

const char *simonNames[] = {"大鬼", "小鬼"};
const char *simonModes[2][4] = {
	{"跳", "看头上", "看下面", "锤别人"},
	{"别跳", "别看头上", "别看下面", "不要锤别人"}
};
const float PI = 3.141592653589793f;


MGSimon::MGSimon(CGameContext* pGameServer, CGameControllerWarioWare* pController) : Microgame(pGameServer, pController)
{
	m_microgameName = "大鬼";
	m_boss = false;
}

void MGSimon::Start()
{
	m_Someone = rand() % 2; // simon/someone
	m_SimonNegative = rand() % 2; // simonModes[] array ind 1 [2] (do/don't)
	m_SimonMode = rand() % 4; // simonModes[] array ind 2 [3] (jump, look up, look down. hammer)
	
	for (int i=0; i<MAX_CLIENTS; i++)
	{
		Controller()->g_Complete[i] = (m_Someone or m_SimonNegative);
		m_SomeoneDontJump[i] = false;
		m_SomeoneDontHammer[i] = false;
	}
	
	char aBuf[96];
	str_format(aBuf, sizeof(aBuf), "%s说: %s!", simonNames[m_Someone], simonModes[m_SimonNegative][m_SimonMode]);
	GameServer()->SendBroadcast(aBuf, -1);

	Controller()->setPlayerTimers(g_Config.m_WwSndMgSimonSays_Offset, g_Config.m_WwSndMgSimonSays_Length);
}

void MGSimon::End()
{
	// nothing to clean
}

void MGSimon::Tick()
{
	float timeLeft = Controller()->getTimeLength() - Controller()->getTimer();

	if (timeLeft < 3200 and timeLeft > 200)
	{
		for (int i=0; i<MAX_CLIENTS; i++)
		{
			CCharacter *Char = GameServer()->GetPlayerChar(i);
			if (not Char) continue;

			CNetObj_PlayerInput* input = Char->GetInput();
			float angle = -atan2(input->m_TargetY, input->m_TargetX) / PI * 180;

			bool objective = (m_SimonMode == 0 and input->m_Jump&1) or // jump
							 (m_SimonMode == 1 and angle >= 75 and angle < 105) or // up
							 (m_SimonMode == 2 and angle <= -75 and angle > -105); // down

			if (objective)
			{
				if (m_Someone != m_SimonNegative) // someone says or simon says don't
				{
					Controller()->killAndLoseMicroGame(i);
					if (m_Someone)
						GameServer()->SendChatTarget(i, "小鬼说的不能信!...");
					else
						GameServer()->SendChatTarget(i, "大鬼说不能!...");
				}
				else
				{
					if (m_Someone and m_SimonNegative and m_SimonMode == 0 and input->m_Jump&1)
						m_SomeoneDontJump[i] = true;
					
					Controller()->winMicroGame(i);
				}
			}

			// reduced timer for someone says don't
			if (timeLeft < 2700 && m_SimonNegative && m_Someone) {
				if (!objective and !m_SomeoneDontJump[i] && !m_SomeoneDontHammer[i]) {
					Controller()->killAndLoseMicroGame(i);
					if (m_Someone)
						GameServer()->SendChatTarget(i, "小鬼说的不能信!...");
				}
			}
		}
	}
}

void MGSimon::OnCharacterDamage(int Victim, int Killer, int Dmg, int Weapon)
{
	if(m_SimonMode != 3)
		return;
	if(Weapon != WEAPON_HAMMER)
		return;
	if (m_Someone == m_SimonNegative) 
	{
		if (m_Someone and m_SimonNegative)
			m_SomeoneDontHammer[Killer] = true;
		Controller()->winMicroGame(Killer);
	} else{
		if (!Controller()->g_Complete[Killer]) {
			Controller()->killAndLoseMicroGame(Killer);
			if (m_Someone)
				GameServer()->SendChatTarget(Killer, "小鬼说的不能信!...");
		}
	}
}