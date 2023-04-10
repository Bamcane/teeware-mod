// WarioWare mod by Headshotnoby

#include <engine/shared/config.h>
#include <game/server/faketee.h>
#include "infect.h"

MGInfect::MGInfect(CGameContext* pGameServer, CGameControllerWarioWare* pController) : Microgame(pGameServer, pController)
{
	m_microgameName = "感染";
	m_boss = true;
}

void MGInfect::Start()
{
	// count online players
	std::vector<int> online;

	for (int i=0; i<MAX_CLIENTS-1; i++)
	{
		CPlayer *Player = GameServer()->m_apPlayers[i];
		CCharacter *Char = (Player) ? Player->GetCharacter() : 0;

		m_IsInfect[i] = false;
		if (not Char) continue;

		Player->SetInfoLock(true); // prevent skin change
		Char->SetHitOthers(false);
		Char->SetHookOthers(false);
		Char->SetCollideOthers(false);
		if (str_comp_nocase(Server()->ClientName(i), "感染者") == 0) // remove fake
			Server()->SetClientName(i, "假的");
		Player->m_TeeInfos.m_UseCustomColor = 0;
		Char->m_ForcedTuneZone = 4; // no, it won't crash here. see above on the online vector; this is guaranteed an online player

		Controller()->teleportPlayer(i, 22);

		Controller()->g_Complete[i] = (GameServer()->GetPlayerChar(i));
	}

	int bot_tele = 20;
	int Num = Controller()->m_TeleOuts[bot_tele-1].size();

	Server()->SetClientName(MAX_CLIENTS-1, "感染者");
	str_copy(GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_SkinName, "cammo", sizeof(GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_SkinName));
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_UseCustomColor = 1;
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_ColorBody = 3866368;
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_ColorFeet = 184;

	GameServer()->m_apPlayers[MAX_CLIENTS-1]->SetTeam(0, false); // move to game
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->ForceSpawn(Controller()->m_TeleOuts[bot_tele-1][(!Num)?Num:rand() % Num]);
	

	m_IsInfect[MAX_CLIENTS-1] = true;
	GameServer()->SendBroadcast("不要被碰到感染!(向右抵达终点)", -1);
	Controller()->setPlayerTimers(g_Config.m_WwSndMgInfect_Offset, g_Config.m_WwSndMgInfect_Length);
}

void MGInfect::End()
{
	// move bot back to spec
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->SetTeam(TEAM_SPECTATORS, false);
	str_copy(GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_SkinName, "itsabot", sizeof(GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_SkinName));
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_UseCustomColor = 0;
	Server()->SetClientName(MAX_CLIENTS-1, "bot");

	// reset player healths
	for (int i=0; i<MAX_CLIENTS-1; i++)
	{
		if(!Controller()->g_Complete[i])
			Controller()->killAndLoseMicroGame(i)
			
		CPlayer *Player = GameServer()->m_apPlayers[i];
		CCharacter *Char = (Player) ? Player->GetCharacter() : 0;

		if (not Player) continue;
		Player->SetInfoLock(false);
		str_copy(Player->m_TeeInfos.m_SkinName, Player->original_skin, sizeof(Player->m_TeeInfos.m_SkinName));
		Player->m_TeeInfos.m_UseCustomColor = Player->original_color;
		Player->m_TeeInfos.m_ColorBody = Player->original_body_color;

		if (not Char) continue;
		Controller()->teleportPlayerToSpawn(i);
		Char->m_ForcedTuneZone = -1;
	}
}

void MGInfect::Tick()
{
	if (Server()->Tick() - m_startTick > 225 and not m_Moved)
	{
		Server()->SetClientName(MAX_CLIENTS-1, "感染者");
		GameServer()->m_apPlayers[MAX_CLIENTS-1]->SetTeam(0, false); // move to game
		
		m_Moved = true;

		m_Target = -1;
		m_SwitchTargetTick = 0;
		m_FireTick = 20;
	}
	else if (m_Moved) // bot tick
	{
		CCharacter *Bot = GameServer()->GetPlayerChar(MAX_CLIENTS-1);
		if (Bot)
		{
			float timeLeft = Controller()->getTimeLength() - Controller()->getTimer();
			if (timeLeft < 3000)
			{
				Bot->SetEmoteType(EMOTE_SURPRISE);
				Bot->SetEmoteStop(Server()->Tick() + Server()->TickSpeed());
			}
		}
	}
	int alive = 0;
	for (unsigned i=0; i<MAX_CLIENTS-1; i++)
	{
		CCharacter *Char = GameServer()->GetPlayerChar(i);
		if (Char)
		{
			if(!m_IsInfect[i])
				alive++;
		}
	}

	if (not alive) // no twintris alive
	{
		for (unsigned i=0; i<MAX_CLIENTS-1; i++)
		{
			CCharacter *Char = GameServer()->GetPlayerChar(i);
			if (Char)
				Controller()->killAndLoseMicroGame(i);
		}
	}
	
	{
		for (unsigned i=0; i<MAX_CLIENTS-1; i++)
		{
			int client = i;
			CCharacter *Char = GameServer()->GetPlayerChar(client);
			if(!Char)
				return;

			if(!m_IsInfect[i])
				return;

			CCharacter *aEnts[MAX_CLIENTS];
			float Radius = Char->m_ProximityRadius * 1.5f;
			int Num = GameServer()->m_World.FindEntities(Char->m_Pos, Radius, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int ii = 0; ii < Num; ii++)
			{
				if (aEnts[ii] == Char)
					continue;
				
				// check so we are sufficiently close
				if (distance(aEnts[ii]->m_Pos, Char->m_Pos) > Radius)
					continue;

				int ClientID = aEnts[ii]->GetPlayer()->GetCID();
				if (m_IsInfect[ClientID])
					continue;
				
				m_IsInfect[ClientID] = true;
				str_copy(GameServer()->m_apPlayers[ClientID]->m_TeeInfos.m_SkinName, "cammo", sizeof(GameServer()->m_apPlayers[ClientID]->m_TeeInfos.m_SkinName));
				GameServer()->m_apPlayers[ClientID]->m_TeeInfos.m_UseCustomColor = 1;
				GameServer()->m_apPlayers[ClientID]->m_TeeInfos.m_ColorBody = 3866368;
				aEnts[ii]->m_ForcedTuneZone = -1;
			}
		}
	}
}

bool MGInfect::OnWinMicrogame(int client, int winTile)
{
	if (winTile == TILE_WARIOWARE_REACHEND_NADE1_WIN) // kill all infects
	{
		if(m_IsInfect[client])
			return false;

		for (unsigned i=0; i<MAX_CLIENTS-1; i++)
		{
			CCharacter *Char = GameServer()->GetPlayerChar(i);
			if (not Char) continue;
			if (not m_IsInfect[i]) continue;

			float timeLeft = Controller()->getTimeLength() - Controller()->getTimer();
			Char->Die(i, WEAPON_WORLD, timeLeft/1000.f);
		}

		// all twintris win
		for (unsigned i=0; i<MAX_CLIENTS-1; i++)
		{
			CCharacter *Char = GameServer()->GetPlayerChar(i);
			if (not Char) continue;
			if (m_IsInfect[i])
				continue;
			Controller()->winMicroGame(i);
		}
		
		return true;
	}
	return false;
}

void MGInfect::OnBotInput(CNetObj_PlayerInput* Input)
{
	CCharacter *Bot = GameServer()->GetPlayerChar(MAX_CLIENTS-1);
	CCharacter *Target = GameServer()->GetPlayerChar(m_Target);

	if (not Bot) return;

	int InfectNum = 0;
	for(int i = 0;i < MAX_CLIENTS-1;i ++)
	{
		CCharacter *Char = GameServer()->GetPlayerChar(i);
		if(m_IsInfect[i] || !Char)
			InfectNum++;
	}

	if(InfectNum == MAX_CLIENTS-1)
	{
		for(int i = 0;i < MAX_CLIENTS-1;i ++)
		{
			Controller()->killAndLoseMicroGame(i);
		}
		Controller()->nextWarioState(); // force the microgame to end
		return;
	}

	if (m_SwitchTargetTick <= 0 or not Target or m_IsInfect[m_Target])
	{
		int loops = 0;
		m_PathFound = false;
		do
		{
			m_Target = rand() % (MAX_CLIENTS-1);
			loops++;
		}
		while (loops < 300 and not (Target = GameServer()->GetPlayerChar(m_Target)) and not (m_IsInfect[m_Target]));

		if (loops == 300) // everyone died
		{
			for (int i=0; i<MAX_CLIENTS-1; i++)
			{
				if (not GameServer()->m_apPlayers[i] or GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS)
					continue;

				GameServer()->m_apPlayers[i]->Respawn();
			}
			Controller()->nextWarioState(); // force the microgame to end
			return;
		}

		m_SwitchTargetTick = 175; // 3.5 secs
	}

	m_SwitchTargetTick--;

	bool Wall = (GameServer()->Collision()->IntersectLine(Bot->m_Pos, Target->m_Pos, NULL, NULL, false) != 0);
	int dirX = sign(Target->m_Pos.x - Bot->m_Pos.x);
	//int dirY = sign(Target->m_Pos.y - Bot->m_Pos.y);

	if (not Wall)
	{
		Input->m_Hook = 0;
		Input->m_Direction = dirX;
		Input->m_TargetX = Target->m_Pos.x - Bot->m_Pos.x;
		Input->m_TargetY = Target->m_Pos.y - Bot->m_Pos.y;

		if(Target->m_Pos.y < Bot->m_Pos.y && Bot->IsGrounded())
		{
			Input->m_Jump = 1;
		}else Input->m_Jump = 0;
	}
	else
	{
		if (not m_PathFound)
		{
			FakeTee::pathFind(GameServer(), 0, Target->m_Pos);
			m_PathFound = true;
		}
		else
		{
			FakeTee::pathFindMove(GameServer(), 0, Input);
		}
	}

	CCharacter *aEnts[MAX_CLIENTS];
	float Radius = Bot->m_ProximityRadius * 1.5f;
	int Num = GameServer()->m_World.FindEntities(Bot->m_Pos, Radius, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

	for (int ii = 0; ii < Num; ii++)
	{
		if (aEnts[ii] == Bot)
			continue;
		
		// check so we are sufficiently close
		if (distance(aEnts[ii]->m_Pos, Bot->m_Pos) > Radius)
			continue;

		int ClientID = aEnts[ii]->GetPlayer()->GetCID();
		if (m_IsInfect[ClientID])
			continue;
		
		m_IsInfect[ClientID] = true;
		str_copy(GameServer()->m_apPlayers[ClientID]->m_TeeInfos.m_SkinName, "cammo", sizeof(GameServer()->m_apPlayers[ClientID]->m_TeeInfos.m_SkinName));
		GameServer()->m_apPlayers[ClientID]->m_TeeInfos.m_UseCustomColor = 1;
		GameServer()->m_apPlayers[ClientID]->m_TeeInfos.m_ColorBody = 3866368;
		aEnts[ii]->m_ForcedTuneZone = -1;
	}
}
