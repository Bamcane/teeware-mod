// WarioWare mod by Headshotnoby

#include <engine/shared/config.h>
#include "cow.h"

MGHitCow::MGHitCow(CGameContext* pGameServer, CGameControllerWarioWare* pController) : Microgame(pGameServer, pController)
{
	m_microgameName = "奶牛";
	m_boss = false;
}

void MGHitCow::Start()
{
	for (int i=0; i<MAX_CLIENTS-1; i++)
	{
		CPlayer *Player = GameServer()->m_apPlayers[i];
		CCharacter *Char = (Player) ? Player->GetCharacter() : 0;
		if (not Char) continue;

		Player->SetInfoLock(true); // prevent skin change
		if (str_comp(Player->m_TeeInfos.m_aSkinName, "giraffe") == 0) // remove fake
			str_copy(Player->m_TeeInfos.m_aSkinName, "default", sizeof(Player->m_TeeInfos.m_aSkinName));
		if (str_comp_nocase(Server()->ClientName(i), "奶牛") == 0) // remove fake
			Server()->SetClientName(i, "假的");

		Char->SetCollideOthers(false); // FATTIES
		Char->SetHookOthers(false); // trolls batting the cow away
		Controller()->teleportPlayer(i, 9);
	}

	// teleport the bot player
	int bot_tele = 10;
	int Num = Controller()->m_TeleOuts[bot_tele-1].size();
	Server()->SetClientName(MAX_CLIENTS-1, "奶牛");
	
	// moo skin
	str_copy(GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_aSkinName, "giraffe", sizeof(GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_aSkinName));
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_UseCustomColor = 1;
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_ColorBody = 194;
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_ColorFeet = 9801403;

	GameServer()->m_apPlayers[MAX_CLIENTS-1]->SetTeam(0, false); // move to game
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->ForceSpawn(Controller()->m_TeleOuts[bot_tele-1][(!Num)?Num:rand() % Num]);
	
	GameServer()->SendBroadcast("找到奶牛并且挤奶!", -1);
	Controller()->setPlayerTimers(g_Config.m_WwSndMgCow_Offset, g_Config.m_WwSndMgCow_Length);
}

void MGHitCow::End()
{
	for (int i=0; i<MAX_CLIENTS-1; i++)
	{
		CPlayer *Player = GameServer()->m_apPlayers[i];
		CCharacter *Char = (Player) ? Player->GetCharacter() : 0;

		if (not Player) continue;
		Player->SetInfoLock(false);
		str_copy(Player->m_TeeInfos.m_aSkinName, Player->original_skin, sizeof(Player->m_TeeInfos.m_aSkinName));

		if (not Char) continue;
		Controller()->teleportPlayerToSpawn(i);
	}

	// move bot back to spec
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->SetTeam(TEAM_SPECTATORS, false);
	str_copy(GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_aSkinName, "itsabot", sizeof(GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_aSkinName));
	GameServer()->m_apPlayers[MAX_CLIENTS-1]->m_TeeInfos.m_UseCustomColor = 0;
	Server()->SetClientName(MAX_CLIENTS-1, "bot");
}

void MGHitCow::Tick()
{
	// nothing to tick
}

void MGHitCow::OnCharacterDamage(int Victim, int Killer, int Dmg, int Weapon)
{
	if (Victim == MAX_CLIENTS-1 and Weapon == WEAPON_HAMMER)
		Controller()->winMicroGame(Killer);
}