

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "laser.h"

CLaser::CLaser(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int VTeam, int Strength)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = Pos;
	m_PrevPos = m_From;
	m_Owner = Owner;
	m_Energy = StartEnergy;
	m_Dir = Direction;
	m_Bounces = 0;
	m_EvalTick = 0;
	m_Strength = Strength;
	m_WTeam = VTeam;
	m_VTeam = VTeam;
	GameWorld()->InsertEntity(this);
	DoBounce();
}


bool CLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pHit = GameServer()->m_World.IntersectCharacter(m_Pos, To, 0.f, At);
	if(m_Bounces == 0) // first bounce
	    pHit = GameServer()->m_World.IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar);
	if(!pHit)
		return false;


	if(pHit->TakeDamage(vec2(0.f, 0.f), m_Owner, WEAPON_RIFLE))
	{ // simpler team detection
    	m_From = From;
    	m_Pos = At;
    	m_Energy = -1;
        if(!m_Strength)
       	    pHit->Freeze(0);
	} else return false;
	if(m_Strength)
	{
    	const vec2 &HitPos = pHit->GetCore().m_Pos;
    	vec2 newvel = vec2(normalize(/*pOwnerChar->GetCore().m_Pos*/m_PrevPos - HitPos) * m_Strength);
    	pHit->GetCore().m_Vel += newvel;
	}
	return true;
}

void CLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		GameServer()->m_World.DestroyEntity(this);
		return;
	}

	vec2 To = m_Pos + m_Dir * m_Energy;
	m_PrevPos = m_Pos;

	if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
	{
		if(!HitCharacter(m_Pos, To))
		{
			// intersected
			m_From = m_Pos;
			m_Pos = To;

			vec2 TempPos = m_Pos;
			vec2 TempDir = m_Dir * 4.0f;

			GameServer()->Collision()->MovePoint(&TempPos, &TempDir, 1.0f, 0);
			m_Pos = TempPos;
			m_Dir = normalize(TempDir);

			m_Energy -= distance(m_From, m_Pos) + GameServer()->Tuning()->m_LaserBounceCost;
			m_Bounces++;

			if(m_Bounces > GameServer()->Tuning()->m_LaserBounceNum)
				m_Energy = -1;

			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_BOUNCE);
		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy = -1;
		}
	}
}

void CLaser::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CLaser::Tick()
{
	if(Server()->Tick() > m_EvalTick+(Server()->TickSpeed()*GameServer()->Tuning()->m_LaserBounceDelay)/1000.0f)
		DoBounce();
}

void CLaser::TickPaused()
{
	++m_EvalTick;
}

void CLaser::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_From.x;
	pObj->m_FromY = (int)m_From.y;
	pObj->m_StartTick = m_EvalTick;
}
