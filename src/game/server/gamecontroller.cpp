

#include <engine/shared/config.h>
#include <game/mapitems.h>
#include <game/zones.h>

#include <game/generated/protocol.h>

#include "entities/character.h"
#include "entities/pickup.h"
#include "entities/special/koh.h"
#include "entities/lmb.h"
#include "gamecontroller.h"
#include "gamecontext.h"
#include "player.h"

IGameController::IGameController(class CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pServer = m_pGameServer->Server();
	m_pGameType = "MOD";

	DoWarmup(g_Config.m_SvWarmup);
	m_UnpauseTimer = 0;
	m_GameOverTick = -1;
	m_SuddenDeath = 0;
	m_RoundStartTick = Server()->Tick();
	m_RoundCount = 0;
	m_GameFlags = 0;
	m_aTeamscore[TEAM_RED] = 0;
	m_aTeamscore[TEAM_BLUE] = 0;
	m_aMapWish[0] = 0;

	m_UnbalancedTick = -1;
	m_ForceBalanced = false;

	m_aNumSpawnPoints[0] = 0;
	m_aNumSpawnPoints[1] = 0;
	m_aNumSpawnPoints[2] = 0;
}

IGameController::~IGameController()
{
}

float IGameController::EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos)
{
	float Score = 0.0f;
	CCharacter *pC = static_cast<CCharacter *>(GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER));
	for(; pC; pC = (CCharacter *)pC->TypeNext())
	{
		// team mates are not as dangerous as enemies
		float Scoremod = 1.0f;
		if(pEval->m_FriendlyTeam != -1 && pC->GetPlayer()->GetTeam() == pEval->m_FriendlyTeam)
			Scoremod = 0.5f;

		float d = distance(Pos, pC->m_Pos);
		Score += Scoremod * (d == 0 ? 1000000000.0f : 1.0f/d);
	}

	return Score;
}

void IGameController::EvaluateSpawnType(CSpawnEval *pEval, int Type)
{
	// get spawn point
	for(int i = 0; i < m_aNumSpawnPoints[Type]; i++)
	{
		// check if the position is occupado
		CCharacter *aEnts[MAX_CLIENTS];
		int Num = GameServer()->m_World.FindEntities(m_aaSpawnPoints[Type][i], 64, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		vec2 Positions[5] = { vec2(0.0f, 0.0f), vec2(-32.0f, 0.0f), vec2(0.0f, -32.0f), vec2(32.0f, 0.0f), vec2(0.0f, 32.0f) };	// start, left, up, right, down
		int Result = -1;
		for(int Index = 0; Index < 5 && Result == -1; ++Index)
		{
			Result = Index;
			for(int c = 0; c < Num; ++c)
				if(GameServer()->Collision()->CheckPoint(m_aaSpawnPoints[Type][i]+Positions[Index]) ||
					distance(aEnts[c]->m_Pos, m_aaSpawnPoints[Type][i]+Positions[Index]) <= aEnts[c]->m_ProximityRadius)
				{
					Result = -1;
					break;
				}
		}
		if(Result == -1)
			continue;	// try next spawn point

		vec2 P = m_aaSpawnPoints[Type][i]+Positions[Result];
		float S = EvaluateSpawnPos(pEval, P);
		if(!pEval->m_Got || pEval->m_Score > S)
		{
			pEval->m_Got = true;
			pEval->m_Score = S;
			pEval->m_Pos = P;
		}
	}
}

bool IGameController::CanSpawn(int Team, vec2 *pOutPos)
{
	CSpawnEval Eval;

	// spectators can't spawn
	if(Team == TEAM_SPECTATORS)
		return false;

	EvaluateSpawnType(&Eval, Team);
	// EvaluateSpawnType(&Eval, 0);

	*pOutPos = Eval.m_Pos;
	return Eval.m_Got;
}

bool IGameController::OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv, int TeleNum)
{
	vec2 Pos = (P0 + P1 + P2 + P3)/4.0f;
	int Type = -1;
	int SubType = 0;

	if(str_comp(pName, "spawn") == 0)
		m_aaSpawnPoints[0+TeleNum][m_aNumSpawnPoints[0+TeleNum]++] = Pos;
	else if(str_comp(pName, "redSpawn") == 0)
		m_aaSpawnPoints[1][m_aNumSpawnPoints[1]++] = Pos;
	else if(str_comp(pName, "buleSpawn") == 0)
		m_aaSpawnPoints[2][m_aNumSpawnPoints[2]++] = Pos;
	else if(str_comp(pName, "armor") == 0)
		Type = POWERUP_ARMOR;
	else if(str_comp(pName, "health") == 0)
		Type = POWERUP_HEALTH;
	else if(str_comp(pName, "shotgun") == 0)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_SHOTGUN;
	}
	else if(str_comp(pName, "grenade") == 0)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_GRENADE;
	}
	else if(str_comp(pName, "rifle") == 0)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_RIFLE;
	}
	else if(str_comp(pName, "ninja") == 0 && g_Config.m_SvPowerups)
	{
		Type = POWERUP_NINJA;
		SubType = WEAPON_NINJA;
	}
	if(Type != -1)
	{
		CPickup *pPickup = new CPickup(&GameServer()->m_World, Type, SubType, Pivot, Pos - Pivot, PosEnv);
		pPickup->m_Pos = Pos;
		return true;
	}

	if(str_comp(pName, "koh") == 0)
	{
		CKoh *pKoh = new CKoh(&GameServer()->m_World, Pos, 0);
		pKoh->m_Pos = Pos;
		return true;
	}

	if(str_comp(pName, "lmb") == 0)
	{
		new CLmb(&GameServer()->m_World);
		return true;
	}

	return false;
}

void IGameController::EndRound()
{
	if(m_Warmup) // game can't end when we are running warmup
		return;

	GameServer()->m_World.m_Paused = true;
	m_GameOverTick = Server()->Tick();
	m_SuddenDeath = 0;
}

void IGameController::ResetGame()
{
	GameServer()->m_World.m_ResetRequested = true;
}

const char *IGameController::GetTeamName(int Team)
{
	if(IsTeamplay())
	{
		if(Team == TEAM_RED)
			return "red team";
		else if(Team == TEAM_BLUE)
			return "blue team";
	}
	else
	{
		if(Team == 0)
			return "game";
	}

	return "spectators";
}

static bool IsSeparator(char c) { return c == ';' || c == ' ' || c == ',' || c == '\t'; }

void IGameController::StartRound()
{
	ResetGame();

	m_RoundStartTick = Server()->Tick();
	m_SuddenDeath = 0;
	m_GameOverTick = -1;
	GameServer()->m_World.m_Paused = false;
	m_aTeamscore[TEAM_RED] = 0;
	m_aTeamscore[TEAM_BLUE] = 0;
	m_ForceBalanced = false;
	Server()->DemoRecorder_HandleAutoStart();
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "start round type='%s' teamplay='%d'", m_pGameType, m_GameFlags&GAMEFLAG_TEAMS);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
}

void IGameController::ChangeMap(const char *pToMap)
{
	str_copy(m_aMapWish, pToMap, sizeof(m_aMapWish));
	EndRound();
}

void IGameController::CycleMap()
{
	if(m_aMapWish[0] != 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "rotating map to %s", m_aMapWish);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		str_copy(g_Config.m_SvMap, m_aMapWish, sizeof(g_Config.m_SvMap));
		m_aMapWish[0] = 0;
		m_RoundCount = 0;
		return;
	}
	if(!str_length(g_Config.m_SvMaprotation))
		return;

	if(m_RoundCount < g_Config.m_SvRoundsPerMap-1)
	{
		if(g_Config.m_SvRoundSwap)
			GameServer()->SwapTeams();
		return;
	}

	// handle maprotation
	const char *pMapRotation = g_Config.m_SvMaprotation;
	const char *pCurrentMap = g_Config.m_SvMap;

	int CurrentMapLen = str_length(pCurrentMap);
	const char *pNextMap = pMapRotation;
	while(*pNextMap)
	{
		int WordLen = 0;
		while(pNextMap[WordLen] && !IsSeparator(pNextMap[WordLen]))
			WordLen++;

		if(WordLen == CurrentMapLen && str_comp_num(pNextMap, pCurrentMap, CurrentMapLen) == 0)
		{
			// map found
			pNextMap += CurrentMapLen;
			while(*pNextMap && IsSeparator(*pNextMap))
				pNextMap++;

			break;
		}

		pNextMap++;
	}

	// restart rotation
	if(pNextMap[0] == 0)
		pNextMap = pMapRotation;

	// cut out the next map
	char aBuf[512] = {0};
	for(int i = 0; i < 511; i++)
	{
		aBuf[i] = pNextMap[i];
		if(IsSeparator(pNextMap[i]) || pNextMap[i] == 0)
		{
			aBuf[i] = 0;
			break;
		}
	}

	// skip spaces
	int i = 0;
	while(IsSeparator(aBuf[i]))
		i++;

	m_RoundCount = 0;

	char aBufMsg[256];
	str_format(aBufMsg, sizeof(aBufMsg), "rotating map to %s", &aBuf[i]);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	str_copy(g_Config.m_SvMap, &aBuf[i], sizeof(g_Config.m_SvMap));
}

void IGameController::PostReset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->Respawn();
			GameServer()->m_apPlayers[i]->m_Score = 0;
			GameServer()->m_apPlayers[i]->m_ScoreStartTick = Server()->Tick();
			// GameServer()->m_apPlayers[i]->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
		}
	}
}

void IGameController::OnPlayerInfoChange(class CPlayer *pP)
{
	const int aTeamColors[2] = {65387, 10223467};
	if(IsTeamplay())
	{
		pP->m_TeeInfos.m_UseCustomColor = 1;
		if(pP->GetTeam() >= TEAM_RED && pP->GetTeam() <= TEAM_BLUE)
		{
			pP->m_TeeInfos.m_ColorBody = aTeamColors[pP->GetTeam()];
			pP->m_TeeInfos.m_ColorFeet = aTeamColors[pP->GetTeam()];
		}
		else
		{
			pP->m_TeeInfos.m_ColorBody = 12895054;
			pP->m_TeeInfos.m_ColorFeet = 12895054;
		}
	}
}


int IGameController::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon, int Flags)
{
    CPlayer *m_pVictim = pVictim->GetPlayer();
    if(m_pVictim->m_LMBState == CPlayer::LMB_PLAYING)
    {
        m_pVictim->m_LMBState = CPlayer::LMB_STANDBY;
        m_pVictim->m_SpawnTeam = 0;
        GameServer()->SendChatTarget(m_pVictim->GetCID(), _("You are eliminated"));
    }

    // do scoreing
	if(!pKiller || pKiller == pVictim->GetPlayer() || Weapon == WEAPON_GAME)
		return 0;

    pKiller->m_Score++;

    if(pKiller->PlayerEvent() == CPlayer::EVENT_DUEL)
    {
        pKiller->m_DuelScore++;
        GameServer()->SendChatTarget(m_pVictim->GetCID(), _("'{str:Player}' scored!"), "Player" , Server()->ClientName(pKiller->GetCID()));
        GameServer()->SendChatTarget(pKiller->GetCID(), _("'{str:Player}' scored!"), "Player" , Server()->ClientName(pKiller->GetCID()));
        if(Flags&FLAG_ENDDUEL)
            pKiller->m_DuelScore = 10;
        if(GameServer()->m_apPlayers[pKiller->GetCID()]->m_DuelScore > 9)
        {

            char aBuf[256];
            str_format(aBuf, sizeof(aBuf), "'%s' won a duel against '%s' with result of %d:%d",
                Server()->ClientName(m_pVictim->m_DuelPlayer), Server()->ClientName(m_pVictim->GetCID()),
                GameServer()->m_apPlayers[m_pVictim->m_DuelPlayer]->m_DuelScore, m_pVictim->m_DuelScore);
            GameServer()->SendChatTarget(-1, _(aBuf));

            GameServer()->m_apPlayers[m_pVictim->m_DuelPlayer]->m_SpawnTeam = 0;
            m_pVictim->m_SpawnTeam = 0;
            GameServer()->m_apPlayers[m_pVictim->m_DuelPlayer]->m_DuelPlayer = -1;
            m_pVictim->m_DuelPlayer = -1;
        }
    }
	return 0;
}

void IGameController::OnCharacterSpawn(class CCharacter *pChr)
{
	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER, -1);
	pChr->GiveWeapon(WEAPON_GUN, 10);
}

void IGameController::DoWarmup(int Seconds)
{
	if(Seconds < 0)
		m_Warmup = 0;
	else
		m_Warmup = Seconds*Server()->TickSpeed();
}

void IGameController::TogglePause()
{
	if(IsGameOver())
		return;

	if(GameServer()->m_World.m_Paused)
	{
		// unpause
		if(g_Config.m_SvUnpauseTimer > 0)
			m_UnpauseTimer = g_Config.m_SvUnpauseTimer*Server()->TickSpeed();
		else
		{
			GameServer()->m_World.m_Paused = false;
			m_UnpauseTimer = 0;
		}
	}
	else
	{
		// pause
		GameServer()->m_World.m_Paused = true;
		m_UnpauseTimer = 0;
	}
}

int IGameController::VTeamDuel(int ClientID1, int ClientID2)
{
    int B = ClientID2>ClientID1 ? ClientID2 : ClientID1;
    int A = ClientID2>ClientID1 ? ClientID1 : ClientID2; // A is always less than B
    int R = ((A+B)*(A+B+A))/2+B;
    return R;
}

bool IGameController::IsFriendlyFire(int ClientID1, int ClientID2)
{
  if (ClientID1 == ClientID2)
    return false;

  if (IsTeamplay()) {
    if (!GameServer()->m_apPlayers[ClientID1] ||
        !GameServer()->m_apPlayers[ClientID2])
      return false;

    if (GameServer()->m_apPlayers[ClientID1]->GetTeam() ==
        GameServer()->m_apPlayers[ClientID2]->GetTeam())
      return true;
  }

  return false;
}


bool IGameController::IsForceBalanced()
{
	if(m_ForceBalanced)
	{
		m_ForceBalanced = false;
		return true;
	}
	else
		return false;
}

bool IGameController::CanBeMovedOnBalance(int ClientID)
{
	return true;
}

void IGameController::Tick()
{
	// do warmup
	if(!GameServer()->m_World.m_Paused && m_Warmup)
	{
		m_Warmup--;
		if(!m_Warmup)
			StartRound();
	}

	m_RainbowColor = g_Config.m_SvRainbowSpeed*Server()->Tick()%256 % 256;

	if(m_GameOverTick != -1)
	{
		// game over.. wait for restart
		if(Server()->Tick() > m_GameOverTick+Server()->TickSpeed()*10)
		{
			CycleMap();
			StartRound();
			m_RoundCount++;
		}
	}
	else if(GameServer()->m_World.m_Paused && m_UnpauseTimer)
	{
		--m_UnpauseTimer;
		if(!m_UnpauseTimer)
			GameServer()->m_World.m_Paused = false;
	}

	// game is Paused
	if(GameServer()->m_World.m_Paused)
		++m_RoundStartTick;

	// do team-balancing
	if(IsTeamplay() && m_UnbalancedTick != -1 && Server()->Tick() > m_UnbalancedTick+g_Config.m_SvTeambalanceTime*Server()->TickSpeed()*60)
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", "Balancing teams");

		int aT[2] = {0,0};
		float aTScore[2] = {0,0};
		float aPScore[MAX_CLIENTS] = {0.0f};
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			{
				aT[GameServer()->m_apPlayers[i]->GetTeam()]++;
				aPScore[i] = GameServer()->m_apPlayers[i]->m_Score*Server()->TickSpeed()*60.0f/
					(Server()->Tick()-GameServer()->m_apPlayers[i]->m_ScoreStartTick);
				aTScore[GameServer()->m_apPlayers[i]->GetTeam()] += aPScore[i];
			}
		}

		// are teams unbalanced?
		if(absolute(aT[0]-aT[1]) >= 2)
		{
			int M = (aT[0] > aT[1]) ? 0 : 1;
			int NumBalance = absolute(aT[0]-aT[1]) / 2;

			do
			{
				CPlayer *pP = 0;
				float PD = aTScore[M];
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!GameServer()->m_apPlayers[i] || !CanBeMovedOnBalance(i))
						continue;
					// remember the player who would cause lowest score-difference
					if(GameServer()->m_apPlayers[i]->GetTeam() == M && (!pP || absolute((aTScore[M^1]+aPScore[i]) - (aTScore[M]-aPScore[i])) < PD))
					{
						pP = GameServer()->m_apPlayers[i];
						PD = absolute((aTScore[M^1]+aPScore[i]) - (aTScore[M]-aPScore[i]));
					}
				}

				// move the player to the other team
				int Temp = pP->m_LastActionTick;
				pP->SetTeam(M^1);
				pP->m_LastActionTick = Temp;

				pP->Respawn();
				pP->m_ForceBalanced = true;
			} while (--NumBalance);

			m_ForceBalanced = true;
		}
		m_UnbalancedTick = -1;
	}

	// check for inactive players
	if(g_Config.m_SvInactiveKickTime > 0)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && !Server()->IsAuthed(i))
			{
				if(Server()->Tick() > GameServer()->m_apPlayers[i]->m_LastActionTick+g_Config.m_SvInactiveKickTime*Server()->TickSpeed()*60)
				{
					switch(g_Config.m_SvInactiveKick)
					{
					case 0:
						{
							// move player to spectator
							GameServer()->m_apPlayers[i]->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 1:
						{
							// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
							int Spectators = 0;
							for(int j = 0; j < MAX_CLIENTS; ++j)
								if(GameServer()->m_apPlayers[j] && GameServer()->m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS)
									++Spectators;
							if(Spectators >= g_Config.m_SvSpectatorSlots)
								Server()->Kick(i, "Kicked for inactivity");
							else
								GameServer()->m_apPlayers[i]->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 2:
						{
							// kick the player
							Server()->Kick(i, "Kicked for inactivity");
						}
					}
				}
			}
		}
	}

	//DoWincheck();
}


bool IGameController::IsTeamplay() const
{
	return m_GameFlags&GAMEFLAG_TEAMS;
}

void IGameController::Snap(int SnappingClient)
{
	CNetObj_GameInfo *pGameInfoObj = (CNetObj_GameInfo *)Server()->SnapNewItem(NETOBJTYPE_GAMEINFO, 0, sizeof(CNetObj_GameInfo));
	if(!pGameInfoObj)
		return;

	pGameInfoObj->m_GameFlags = m_GameFlags;
	pGameInfoObj->m_GameStateFlags = 0;
	if(m_GameOverTick != -1)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
	if(m_SuddenDeath)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
	if(GameServer()->m_World.m_Paused)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
	pGameInfoObj->m_RoundStartTick = m_RoundStartTick;
	pGameInfoObj->m_WarmupTimer = GameServer()->m_World.m_Paused ? m_UnpauseTimer : -1;

	pGameInfoObj->m_ScoreLimit = g_Config.m_SvScorelimit;
	pGameInfoObj->m_TimeLimit = g_Config.m_SvTimelimit;

	pGameInfoObj->m_RoundNum = (str_length(g_Config.m_SvMaprotation) && g_Config.m_SvRoundsPerMap) ? g_Config.m_SvRoundsPerMap : 0;
	pGameInfoObj->m_RoundCurrent = m_RoundCount+1;

	CNetObj_GameInfoEx *pGameInfoEx = (CNetObj_GameInfoEx *)Server()->SnapNewItem(32767, 0, 12);
	if(!pGameInfoEx)
		return;

	pGameInfoEx->m_Flags =	GAMEINFOFLAG_ALLOW_HOOK_COLL |  GAMEINFOFLAG_ALLOW_ZOOM;
	pGameInfoEx->m_Flags2 =	GAMEINFOFLAG2_HUD_DDRACE;
	if(~GameServer()->m_apPlayers[SnappingClient]->m_Settings&CPlayer::SETTINGS_PREDICTVANILLA)
	    pGameInfoEx->m_Flags |= GAMEINFOFLAG_PREDICT_DDRACE | GAMEINFOFLAG_PREDICT_DDRACE_TILES;
	pGameInfoEx->m_Version = 8;

	// This object needs to be snapped alongside pGameInfoObj for that object to work properly
	int *pUuidItem = (int *)Server()->SnapNewItem(0, 32767, 16); // NETOBJTYPE_EX
	if(pUuidItem)	{
		pUuidItem[0] = -1824658838;
		pUuidItem[1] = -629591830;
		pUuidItem[2] = -1450210576;
		pUuidItem[3] = 914991429;
	}
}

int IGameController::GetAutoTeam(int NotThisID)
{
	// this will force the auto balancer to work overtime aswell
	if(g_Config.m_DbgStress)
		return 0;

	int aNumplayers[2] = {0,0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && i != NotThisID)
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	int Team = 0;
	if(IsTeamplay())
		Team = aNumplayers[TEAM_RED] > aNumplayers[TEAM_BLUE] ? TEAM_BLUE : TEAM_RED;

	if(CanJoinTeam(Team, NotThisID))
		return Team;
	return -1;
}

bool IGameController::CanJoinTeam(int Team, int NotThisID)
{
	if(Team == TEAM_SPECTATORS || (GameServer()->m_apPlayers[NotThisID] && GameServer()->m_apPlayers[NotThisID]->GetTeam() != TEAM_SPECTATORS))
		return true;

	int aNumplayers[2] = {0,0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && i != NotThisID)
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	return (aNumplayers[0] + aNumplayers[1]) < Server()->MaxClients()-g_Config.m_SvSpectatorSlots;
}

bool IGameController::CheckTeamBalance()
{
	if(!IsTeamplay() || !g_Config.m_SvTeambalanceTime)
		return true;

	int aT[2] = {0, 0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(pP && pP->GetTeam() != TEAM_SPECTATORS)
			aT[pP->GetTeam()]++;
	}

	char aBuf[256];
	if(absolute(aT[0]-aT[1]) >= 2)
	{
		str_format(aBuf, sizeof(aBuf), "Teams are NOT balanced (red=%d blue=%d)", aT[0], aT[1]);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		if(GameServer()->m_pController->m_UnbalancedTick == -1)
			GameServer()->m_pController->m_UnbalancedTick = Server()->Tick();
		return false;
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "Teams are balanced (red=%d blue=%d)", aT[0], aT[1]);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		GameServer()->m_pController->m_UnbalancedTick = -1;
		return true;
	}
}

bool IGameController::CanChangeTeam(CPlayer *pPlayer, int JoinTeam)
{
	int aT[2] = {0, 0};

	if (!IsTeamplay() || JoinTeam == TEAM_SPECTATORS || !g_Config.m_SvTeambalanceTime)
		return true;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(pP && pP->GetTeam() != TEAM_SPECTATORS)
			aT[pP->GetTeam()]++;
	}

	// simulate what would happen if changed team
	aT[JoinTeam]++;
	if (pPlayer->GetTeam() != TEAM_SPECTATORS)
		aT[JoinTeam^1]--;

	// there is a player-difference of at least 2
	if(absolute(aT[0]-aT[1]) >= 2)
	{
		// player wants to join team with less players
		if ((aT[0] < aT[1] && JoinTeam == TEAM_RED) || (aT[0] > aT[1] && JoinTeam == TEAM_BLUE))
			return true;
		else
			return false;
	}
	else
		return true;
}

void IGameController::DoWincheck()
{

	// if(m_GameOverTick == -1 && !m_Warmup && !GameServer()->m_World.m_ResetRequested)
	// {
	// 	if(IsTeamplay())
	// 	{
	// 		// check score win condition
	// 		if((g_Config.m_SvScorelimit > 0 && (m_aTeamscore[TEAM_RED] >= g_Config.m_SvScorelimit || m_aTeamscore[TEAM_BLUE] >= g_Config.m_SvScorelimit)) ||
	// 			(g_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= g_Config.m_SvTimelimit*Server()->TickSpeed()*60))
	// 		{
	// 			if(m_aTeamscore[TEAM_RED] != m_aTeamscore[TEAM_BLUE])
	// 				EndRound();
	// 			else
	// 				m_SuddenDeath = 1;
	// 		}
	// 	}
	// 	else
	// 	{
	// 		// gather some stats
	// 		int Topscore = 0;
	// 		int TopscoreCount = 0;
	// 		for(int i = 0; i < MAX_CLIENTS; i++)
	// 		{
	// 			if(GameServer()->m_apPlayers[i])
	// 			{
	// 				if(GameServer()->m_apPlayers[i]->m_Score > Topscore)
	// 				{
	// 					Topscore = GameServer()->m_apPlayers[i]->m_Score;
	// 					TopscoreCount = 1;
	// 				}
	// 				else if(GameServer()->m_apPlayers[i]->m_Score == Topscore)
	// 					TopscoreCount++;
	// 			}
	// 		}

	// 		// check score win condition
	// 		if((g_Config.m_SvScorelimit > 0 && Topscore >= g_Config.m_SvScorelimit) ||
	// 			(g_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= g_Config.m_SvTimelimit*Server()->TickSpeed()*60))
	// 		{
	// 			if(TopscoreCount == 1)
	// 				EndRound();
	// 			else
	// 				m_SuddenDeath = 1;
	// 		}
	// 	}
	// }
}

int IGameController::ClampTeam(int Team)
{
	if(Team < 0)
		return TEAM_SPECTATORS;
	if(IsTeamplay())
		return Team&1;
	return 0;
}

double IGameController::GetTime()
{
	return static_cast<double>(Server()->Tick() - m_RoundStartTick)/Server()->TickSpeed();
}
