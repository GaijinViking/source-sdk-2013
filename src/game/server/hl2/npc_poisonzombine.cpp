//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Grabbing puke!
//
//	 Bite those in my face, 
//   Puke on any bigger prey nearby
//   Spit at any smaller prey nearby, or at anything far away
//
//=============================================================================//

#include "cbase.h"
#include "ai_basenpc.h"
#include "ai_default.h"
#include "ai_schedule.h"
#include "ai_hull.h"
#include "ai_motor.h"
#include "game.h"
#include "npcevent.h"
#include "entitylist.h"
#include "ai_task.h"
#include "activitylist.h"
#include "engine/IEngineSound.h"
#include "npc_BaseZombie.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define BREATH_VOL_MAX  0.6

envelopePoint_t envPoisonZombineMoanVolumeFast[] =
{
	{	1.0f, 1.0f,
		0.1f, 0.1f,
	},
	{	0.0f, 0.0f,
		0.2f, 0.3f,
	},
};

//
// Turns the breathing off for a second, then back on.
//
envelopePoint_t envPoisonZombineBreatheVolumeOffShort[] =
{
	{	0.0f, 0.0f,
		0.1f, 0.1f,
	},
	{	0.0f, 0.0f,
		2.0f, 2.0f,
	},
	{	BREATH_VOL_MAX, BREATH_VOL_MAX,
		1.0f, 1.0f,
	},
};

//
// Custom schedules.
//
enum
{
	SCHED_ZOMBINE_POISON_RANGE_ATTACK1 = LAST_BASE_ZOMBIE_SCHEDULE,
	SCHED_ZOMBINE_POISON_RANGE_ATTACK2,
};

int AE_ZOMBINE_POISON_SPIT;
int AE_ZOMBINE_POISON_PUKE;

//-----------------------------------------------------------------------------
// The model we use for our legs when we get blowed up.
//-----------------------------------------------------------------------------
static const char *s_szLegsModel = "models/zombie/classic_legs.mdl";

//-----------------------------------------------------------------------------
// The classname of the headcrab that jumps off of this kind of zombie.
//-----------------------------------------------------------------------------
static const char *s_szHeadcrabClassname = "npc_headcrab_poison";
static const char *s_szHeadcrabModel = "models/headcrabblack.mdl";

static const char *pMoanSounds[] =
{
	"NPC_PoisonZombine.Moan1",
};

//-----------------------------------------------------------------------------
// Skill settings.
//-----------------------------------------------------------------------------
ConVar sk_zombine_poison_health( "sk_zombine_poison_health", "0");
ConVar sk_zombine_poison_dmg_bite( "sk_zombine_poison_dmg_bite", "0");
ConVar sk_zombine_poison_dmg_puke( "sk_zombine_poison_dmg_puke", "0");
ConVar sk_zombine_poison_puke_near( "sk_zombine_poison_puke_near", "0");
ConVar sk_zombine_poison_puke_range( "sk_zombine_poison_puke_range", "0");
ConVar sk_zombine_poison_puke_max( "sk_zombine_poison_puke_max", "0");

class CNPC_PoisonZombine : public CAI_BlendingHost<CNPC_BaseZombie>
{
	DECLARE_CLASS( CNPC_PoisonZombine, CAI_BlendingHost<CNPC_BaseZombie> );

public:

	//
	// CBaseZombie implemenation.
	//
	virtual Vector HeadTarget( const Vector &posSrc );
	bool ShouldBecomeTorso( const CTakeDamageInfo &info, float flDamageThreshold );
	virtual bool IsChopped( const CTakeDamageInfo &info )	{ return false; }

	//
	// CAI_BaseNPC implementation.
	//
	virtual float MaxYawSpeed( void );

	virtual int RangeAttack1Conditions( float flDot, float flDist );
	virtual int RangeAttack2Conditions( float flDot, float flDist );

	virtual float GetClawAttackRange() const { return 70; }

	virtual void PrescheduleThink( void );
	virtual void BuildScheduleTestBits( void );
	virtual int SelectSchedule( void );
	virtual int SelectFailSchedule( int nFailedSchedule, int nFailedTask, AI_TaskFailureCode_t eTaskFailCode );
	virtual int TranslateSchedule( int scheduleType );

	virtual bool ShouldPlayIdleSound( void );

	//
	// CBaseAnimating implementation.
	//
	virtual void HandleAnimEvent( animevent_t *pEvent );

	//
	// CBaseEntity implementation.
	//
	virtual void Spawn( void );
	virtual void Precache( void );
	virtual void SetZombieModel( void );

	virtual Class_T Classify( void );
	virtual void Event_Killed( const CTakeDamageInfo &info );
	virtual int OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo );

	DECLARE_DATADESC();
	DEFINE_CUSTOM_AI;

	void PainSound( const CTakeDamageInfo &info );
	void AlertSound( void );
	void IdleSound( void );
	void AttackSound( void );
	void AttackHitSound( void );
	void AttackMissSound( void );
	void FootstepSound( bool fRightFoot );
	void FootscuffSound( bool fRightFoot ) {};

	virtual void StopLoopingSounds( void );

protected:

	virtual void MoanSound( envelopePoint_t *pEnvelope, int iEnvelopeSize );
	virtual bool MustCloseToAttack( void );

	virtual const char *GetMoanSound( int nSoundIndex );
	virtual const char *GetLegsModel( void );
	virtual const char *GetTorsoModel( void );
	virtual const char *GetHeadcrabClassname( void );
	virtual const char *GetHeadcrabModel( void );

private:

	void BreatheOffShort( void );

	CSoundPatch *m_pFastBreathSound;
	CSoundPatch *m_pSlowBreathSound;

	float m_flNextPainSoundTime;

 float m_flPuke

	bool m_bNearEnemy;
};

LINK_ENTITY_TO_CLASS( npc_poisonzombine, CNPC_PoisonZombine );

BEGIN_DATADESC( CNPC_PoisonZombine )

	DEFINE_SOUNDPATCH( m_pFastBreathSound ),
	DEFINE_SOUNDPATCH( m_pSlowBreathSound ),

	DEFINE_FIELD( m_flNextPainSoundTime, FIELD_TIME ),

	DEFINE_FIELD( m_bNearEnemy, FIELD_BOOLEAN ),

END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::Precache( void )
{
	PrecacheModel("models/zombie/poison.mdl");

	PrecacheScriptSound( "NPC_PoisonZombine.Die" );
	PrecacheScriptSound( "NPC_PoisonZombine.ThrowWarn" );
	PrecacheScriptSound( "NPC_PoisonZombine.Throw" );
	PrecacheScriptSound( "NPC_PoisonZombine.Idle" );
	PrecacheScriptSound( "NPC_PoisonZombine.Pain" );
	PrecacheScriptSound( "NPC_PoisonZombine.Alert" );
	PrecacheScriptSound( "NPC_PoisonZombine.FootstepRight" );
	PrecacheScriptSound( "NPC_PoisonZombine.FootstepLeft" );
	PrecacheScriptSound( "NPC_PoisonZombine.Attack" );

	PrecacheScriptSound( "NPC_PoisonZombine.FastBreath" );
	PrecacheScriptSound( "NPC_PoisonZombine.Moan1" );

	PrecacheScriptSound( "Zombie.AttackHit" );
	PrecacheScriptSound( "Zombie.AttackMiss" );

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::Spawn( void )
{
	Precache();

	m_fIsTorso = m_fIsHeadless = false;

	SetBloodColor( BLOOD_COLOR_ZOMBIE );

	m_iHealth = sk_zombine_poison_health.GetFloat();
	m_flFieldOfView = 0.2;

	CapabilitiesClear();
	CapabilitiesAdd( bits_CAP_MOVE_GROUND | bits_CAP_INNATE_MELEE_ATTACK1 | bits_CAP_INNATE_RANGE_ATTACK1 | bits_CAP_INNATE_RANGE_ATTACK2 );

	BaseClass::Spawn();

	CPASAttenuationFilter filter( this, ATTN_IDLE );
	m_pFastBreathSound = ENVELOPE_CONTROLLER.SoundCreate( filter, entindex(), CHAN_ITEM, "NPC_PoisonZombine.FastBreath", ATTN_IDLE );
	ENVELOPE_CONTROLLER.Play( m_pFastBreathSound, 0.0f, 100 );

	CPASAttenuationFilter filter2( this );
	m_pSlowBreathSound = ENVELOPE_CONTROLLER.SoundCreate( filter2, entindex(), CHAN_ITEM, "NPC_PoisonZombine.Moan1", ATTN_NORM );
	ENVELOPE_CONTROLLER.Play( m_pSlowBreathSound, BREATH_VOL_MAX, 100 );
}

//-----------------------------------------------------------------------------
// Purpose: Returns a moan sound for this class of zombie.
//-----------------------------------------------------------------------------
const char *CNPC_PoisonZombine::GetMoanSound( int nSound )
{
	return pMoanSounds[nSound % ARRAYSIZE( pMoanSounds )];
}

//-----------------------------------------------------------------------------
// Purpose: Returns the model to use for our legs ragdoll when we are blown in twain.
//-----------------------------------------------------------------------------
const char *CNPC_PoisonZombine::GetLegsModel( void )
{
	return s_szLegsModel;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
const char *CNPC_PoisonZombine::GetTorsoModel( void )
{
	return "models/zombie/classic_torso.mdl";
}

//-----------------------------------------------------------------------------
// Purpose: Returns the classname (ie "npc_headcrab") to spawn when our headcrab bails.
//-----------------------------------------------------------------------------
const char *CNPC_PoisonZombine::GetHeadcrabClassname( void )
{
	return s_szHeadcrabClassname;
}

const char *CNPC_PoisonZombine::GetHeadcrabModel( void )
{
	return s_szHeadcrabModel;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::StopLoopingSounds( void )
{
	ENVELOPE_CONTROLLER.SoundDestroy( m_pFastBreathSound );
	m_pFastBreathSound = NULL;

	ENVELOPE_CONTROLLER.SoundDestroy( m_pSlowBreathSound );
	m_pSlowBreathSound = NULL;

	BaseClass::StopLoopingSounds();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : info - 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::Event_Killed( const CTakeDamageInfo &info )
{
	if ( !( info.GetDamageType() & ( DMG_BLAST | DMG_ALWAYSGIB) ) ) 
	{
		EmitSound( "NPC_PoisonZombine.Die" );
	}

	if ( !m_fIsTorso )
	{
		EvacuateNest(info.GetDamageType() == DMG_BLAST, info.GetDamage(), info.GetAttacker() );
	}

	BaseClass::Event_Killed( info );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &inputInfo - 
// Output : int
//-----------------------------------------------------------------------------
int CNPC_PoisonZombine::OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo )
{
	//
	// Calculate what percentage of the creature's max health
	// this amount of damage represents (clips at 1.0).
	//
	float flDamagePercent = MIN( 1, inputInfo.GetDamage() / m_iMaxHealth );

	//
	// Throw one crab for every 20% damage we take.
	//
	if ( flDamagePercent >= 0.2 )
	{
		m_flNextCrabThrowTime = gpGlobals->curtime;
	}

	return BaseClass::OnTakeDamage_Alive( inputInfo );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CNPC_PoisonZombine::MaxYawSpeed( void )
{
	return BaseClass::MaxYawSpeed();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
Class_T	CNPC_PoisonZombine::Classify( void )
{
	return CLASS_ZOMBIE;
}

//-----------------------------------------------------------------------------
// Purpose: 
//
// NOTE: This function is still heavy with common code (found at the bottom).
//		 we should consider moving some into the base class! (sjb)
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::SetZombieModel( void )
{
	Hull_t lastHull = GetHullType();

	if ( m_fIsTorso )
	{
		SetModel( "models/zombie/classic_torso.mdl" );
		SetHullType(HULL_TINY);
	}
	else
	{
		SetModel( "models/zombie/poison.mdl" );
		SetHullType(HULL_HUMAN);
	}

	SetBodygroup( ZOMBIE_BODYGROUP_HEADCRAB, !m_fIsHeadless );

	SetHullSizeNormal( true );
	SetDefaultEyeOffset();
	SetActivity( ACT_IDLE );

	// hull changed size, notify vphysics
	// UNDONE: Solve this generally, systematically so other
	// NPCs can change size
	if ( lastHull != GetHullType() )
	{
		if ( VPhysicsGetObject() )
		{
			SetupVPhysicsHull();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns whether the enemy has been seen within the time period supplied
// Input  : flTime - Timespan we consider
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_PoisonZombine::SeenEnemyWithinTime( float flTime )
{
	float flLastSeenTime = GetEnemies()->LastTimeSeen( GetEnemy() );
	return ( flLastSeenTime != 0.0f && ( gpGlobals->curtime - flLastSeenTime ) < flTime );
}

Vector VecCheckThrowTolerance( CBaseEntity *pEdict, const Vector &vecSpot1, Vector vecSpot2, float flSpeed, float flTolerance )
{
	flSpeed = MAX( 1.0f, flSpeed );

	float flGravity = GetCurrentGravity();

	Vector vecGrenadeVel = (vecSpot2 - vecSpot1);

	// throw at a constant time
	float time = vecGrenadeVel.Length( ) / flSpeed;
	vecGrenadeVel = vecGrenadeVel * (1.0 / time);

	// adjust upward toss to compensate for gravity loss
	vecGrenadeVel.z += flGravity * time * 0.5;

	Vector vecApex = vecSpot1 + (vecSpot2 - vecSpot1) * 0.5;
	vecApex.z += 0.5 * flGravity * (time * 0.5) * (time * 0.5);


	trace_t tr;
	UTIL_TraceLine( vecSpot1, vecApex, MASK_SOLID, pEdict, COLLISION_GROUP_NONE, &tr );
	if (tr.fraction != 1.0)
	{
		// fail!
		if ( g_debug_poison_zombine.GetBool() )
		{
			NDebugOverlay::Line( vecSpot1, vecApex, 255, 0, 0, true, 5.0 );
		}

		return vec3_origin;
	}

	if ( g_debug_poison_zombine.GetBool() )
	{
		NDebugOverlay::Line( vecSpot1, vecApex, 0, 255, 0, true, 5.0 );
	}

	UTIL_TraceLine( vecApex, vecSpot2, MASK_SOLID_BRUSHONLY, pEdict, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction != 1.0 )
	{
		bool bFail = true;

		// Didn't make it all the way there, but check if we're within our tolerance range
		if ( flTolerance > 0.0f )
		{
			float flNearness = ( tr.endpos - vecSpot2 ).LengthSqr();
			if ( flNearness < Square( flTolerance ) )
			{
				if ( g_debug_poison_zombine.GetBool() )
				{
					NDebugOverlay::Sphere( tr.endpos, vec3_angle, flTolerance, 0, 255, 0, 0, true, 5.0 );
				}

				bFail = false;
			}
		}
		
		if ( bFail )
		{
			if ( g_debug_poison_zombine.GetBool() )
			{
				NDebugOverlay::Line( vecApex, vecSpot2, 255, 0, 0, true, 5.0 );
				NDebugOverlay::Sphere( tr.endpos, vec3_angle, flTolerance, 255, 0, 0, 0, true, 5.0 );
			}
			return vec3_origin;
		}
	}

	if ( g_debug_poison_zombine.GetBool() )
	{
		NDebugOverlay::Line( vecApex, vecSpot2, 0, 255, 0, true, 5.0 );
	}

	return vecGrenadeVel;
}

//-----------------------------------------------------------------------------
// Purpose: Get a toss direction that will properly lob spit to hit a target
// Input  : &vecStartPos - Where the spit will start from
//			&vecTarget - Where the spit is meant to land
//			*vecOut - The resulting vector to lob the spit
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_PoisonZombine::GetSpitVector( const Vector &vecStartPos, const Vector &vecTarget, Vector *vecOut )
{
	// Try the most direct route
	Vector vecToss = VecCheckThrowTolerance( this, vecStartPos, vecTarget, sk_antlion_worker_spit_speed.GetFloat(), (10.0f*12.0f) );

	// If this failed then try a little faster (flattens the arc)
	if ( vecToss == vec3_origin )
	{
		vecToss = VecCheckThrowTolerance( this, vecStartPos, vecTarget, sk_antlion_worker_spit_speed.GetFloat() * 1.5f, (10.0f*12.0f) );
		if ( vecToss == vec3_origin )
			return false;
	}

	// Save out the result
	if ( vecOut )
	{
		*vecOut = vecToss;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Checks conditions for letting a headcrab leap off our back at our enemy.
//-----------------------------------------------------------------------------
int CNPC_PoisonZombine::RangeAttack1Conditions( float flDot, float flDist )
{
	if ( GetNextAttack() > gpGlobals->curtime )
		return ;

	// If we can see the enemy, or we've seen them in the last few seconds just try to lob in there
	if ( !SeenEnemyWithinTime( 3.0f ) )
	 return ;

	Vector vSpitPos;
	GetAttachment( "mouth", vSpitPos );
		
	if ( !GetSpitVector( vSpitPos, targetPos, &m_vecSaveSpitVelocity ) )
  return ;

	return COND_CAN_RANGE_ATTACK1;
}

//-----------------------------------------------------------------------------
// Purpose: Checks conditions for throwing a headcrab leap at our enemy.
//-----------------------------------------------------------------------------
int CNPC_PoisonZombine::RangeAttack2Conditions( float flDot, float flDist )
{
	return COND_CAN_RANGE_ATTACK2;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Vector CNPC_PoisonZombine::HeadTarget( const Vector &posSrc )
{
	int iCrabAttachment = LookupAttachment( "headcrab1" );
	Assert( iCrabAttachment > 0 );

	Vector vecPosition;

	GetAttachment( iCrabAttachment, vecPosition );

	return vecPosition;
}

//-----------------------------------------------------------------------------
// Purpose: Turns off our breath so we can play another vocal sound.
//			TODO: pass in duration
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::BreatheOffShort( void )
{
	if ( m_bNearEnemy )
	{
		ENVELOPE_CONTROLLER.SoundPlayEnvelope( m_pFastBreathSound, SOUNDCTRL_CHANGE_VOLUME, envPoisonZombineBreatheVolumeOffShort, ARRAYSIZE(envPoisonZombineBreatheVolumeOffShort) );
	}
	else
	{
		ENVELOPE_CONTROLLER.SoundPlayEnvelope( m_pSlowBreathSound, SOUNDCTRL_CHANGE_VOLUME, envPoisonZombineBreatheVolumeOffShort, ARRAYSIZE(envPoisonZombineBreatheVolumeOffShort) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Catches the monster-specific events that occur when tagged animation
//			frames are played.
// Input  : pEvent - 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::HandleAnimEvent( animevent_t *pEvent )
{
	if ( pEvent->event == AE_ZOMBINE_POISON_BITE )
	{
		Vector forward;
		QAngle qaPunch( 45, random->RandomInt(-5, 5), random->RandomInt(-5, 5) );
		AngleVectors( GetLocalAngles(), &forward );
		forward = forward * 200;
		ClawAttack( GetClawAttackRange(), sk_zombine_poison_dmg_bite.GetFloat(), qaPunch, forward, ZOMBIE_BLOOD_BITE );
		return;
	}

	BaseClass::HandleAnimEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::PrescheduleThink( void )
{

	bool bNearEnemy = false;
	if ( GetEnemy() != NULL )
	{
		float flDist = (GetEnemy()->GetAbsOrigin() - GetAbsOrigin()).Length();
		if ( flDist < ZOMBIE_ENEMY_BREATHE_DIST )
		{
			bNearEnemy = true;
		}
	}

	if ( bNearEnemy )
	{
		if ( !m_bNearEnemy )
		{
			// Our enemy is nearby. Breathe faster.
			float duration = random->RandomFloat( 1.0f, 2.0f );
			ENVELOPE_CONTROLLER.SoundChangeVolume( m_pFastBreathSound, BREATH_VOL_MAX, duration );
			ENVELOPE_CONTROLLER.SoundChangePitch( m_pFastBreathSound, random->RandomInt( 100, 120 ), random->RandomFloat( 1.0f, 2.0f ) );

			ENVELOPE_CONTROLLER.SoundChangeVolume( m_pSlowBreathSound, 0.0f, duration );

			m_bNearEnemy = true;
		}
	}
	else if ( m_bNearEnemy )
	{
		// Our enemy is far away. Slow our breathing down.
		float duration = random->RandomFloat( 2.0f, 4.0f );
		ENVELOPE_CONTROLLER.SoundChangeVolume( m_pFastBreathSound, BREATH_VOL_MAX, duration );
		ENVELOPE_CONTROLLER.SoundChangeVolume( m_pSlowBreathSound, 0.0f, duration );
//		ENVELOPE_CONTROLLER.SoundChangePitch( m_pBreathSound, random->RandomInt( 80, 100 ), duration );

		m_bNearEnemy = false;
	}

	BaseClass::PrescheduleThink();
}

//-----------------------------------------------------------------------------
// Purpose: Allows for modification of the interrupt mask for the current schedule.
//			In the most cases the base implementation should be called first.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::BuildScheduleTestBits( void )
{
	BaseClass::BuildScheduleTestBits();

	if ( IsCurSchedule( SCHED_CHASE_ENEMY ) )
	{
		SetCustomInterruptCondition( COND_LIGHT_DAMAGE );
		SetCustomInterruptCondition( COND_HEAVY_DAMAGE );
	}
	else if ( IsCurSchedule( SCHED_RANGE_ATTACK1 ) || IsCurSchedule( SCHED_RANGE_ATTACK2 ) )
	{
		ClearCustomInterruptCondition( COND_LIGHT_DAMAGE );
		ClearCustomInterruptCondition( COND_HEAVY_DAMAGE );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CNPC_PoisonZombine::SelectFailSchedule( int nFailedSchedule, int nFailedTask, AI_TaskFailureCode_t eTaskFailCode )
{
	int nSchedule = BaseClass::SelectFailSchedule( nFailedSchedule, nFailedTask, eTaskFailCode );

	if ( nSchedule == SCHED_CHASE_ENEMY_FAILED && m_nCrabCount > 0 )
	{
		return SCHED_ESTABLISH_LINE_OF_FIRE;
	}

	return nSchedule;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CNPC_PoisonZombine::SelectSchedule( void )
{
	int nSchedule = BaseClass::SelectSchedule();

	if ( nSchedule == SCHED_SMALL_FLINCH )
	{
		 m_flNextFlinchTime = gpGlobals->curtime + random->RandomFloat( 1, 3 );
	}

	return nSchedule;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : scheduleType - 
// Output : int
//-----------------------------------------------------------------------------
int CNPC_PoisonZombine::TranslateSchedule( int scheduleType )
{
	if ( scheduleType == SCHED_RANGE_ATTACK2 )
	{
		return SCHED_ZOMBINE_POISON_RANGE_ATTACK2;
	}

	if ( scheduleType == SCHED_RANGE_ATTACK1 )
	{
		return SCHED_ZOMBINE_POISON_RANGE_ATTACK1;
	}

	if ( scheduleType == SCHED_COMBAT_FACE && IsUnreachable( GetEnemy() ) )
		return SCHED_TAKE_COVER_FROM_ENEMY;

	// We'd simply like to shamble towards our enemy
	if ( scheduleType == SCHED_MOVE_TO_WEAPON_RANGE )
		return SCHED_CHASE_ENEMY;

	return BaseClass::TranslateSchedule( scheduleType );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CNPC_PoisonZombine::ShouldPlayIdleSound( void )
{
	return CAI_BaseNPC::ShouldPlayIdleSound();
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack hit sound
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::AttackHitSound( void )
{
	EmitSound( "Zombie.AttackHit" );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack miss sound
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::AttackMissSound( void )
{
	EmitSound( "Zombie.AttackMiss" );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack sound.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::AttackSound( void )
{
	EmitSound( "NPC_PoisonZombine.Attack" );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random idle sound.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::IdleSound( void )
{
	// HACK: base zombie code calls IdleSound even when not idle!
	if ( m_NPCState != NPC_STATE_COMBAT )
	{
		BreatheOffShort();
		EmitSound( "NPC_PoisonZombine.Idle" );
		MakeAISpookySound( 360.0f );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Play a random pain sound.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::PainSound( const CTakeDamageInfo &info )
{
	// Don't make pain sounds too often.
	if ( m_flNextPainSoundTime <= gpGlobals->curtime )
	{	
		BreatheOffShort();
		EmitSound( "NPC_PoisonZombine.Pain" );
		m_flNextPainSoundTime = gpGlobals->curtime + random->RandomFloat( 4.0, 7.0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Play a random alert sound.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::AlertSound( void )
{
	BreatheOffShort();

	EmitSound( "NPC_PoisonZombine.Alert" );
}

//-----------------------------------------------------------------------------
// Purpose: Sound of a footstep
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::FootstepSound( bool fRightFoot )
{
	if( fRightFoot )
	{
		EmitSound( "NPC_PoisonZombine.FootstepRight" );
	}
	else
	{
		EmitSound( "NPC_PoisonZombine.FootstepLeft" );
	}

	if( ShouldPlayFootstepMoan() )
	{
		m_flNextMoanSound = gpGlobals->curtime;
		MoanSound( envPoisonZombineMoanVolumeFast, ARRAYSIZE( envPoisonZombineMoanVolumeFast ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Open a window and let a little bit of the looping moan sound
//			come through.
//-----------------------------------------------------------------------------
void CNPC_PoisonZombine::MoanSound( envelopePoint_t *pEnvelope, int iEnvelopeSize )
{
	if( !m_pMoanSound )
	{
		// Don't set this up until the code calls for it.
		const char *pszSound = GetMoanSound( m_iMoanSound );
		m_flMoanPitch = random->RandomInt( 98, 110 );

		CPASAttenuationFilter filter( this, 1.5 );
		//m_pMoanSound = ENVELOPE_CONTROLLER.SoundCreate( entindex(), CHAN_STATIC, pszSound, ATTN_NORM );
		m_pMoanSound = ENVELOPE_CONTROLLER.SoundCreate( filter, entindex(), CHAN_STATIC, pszSound, 1.5 );

		ENVELOPE_CONTROLLER.Play( m_pMoanSound, 0.5, m_flMoanPitch );
	}

	envPoisonZombineMoanVolumeFast[ 1 ].durationMin = 0.1;
	envPoisonZombineMoanVolumeFast[ 1 ].durationMax = 0.4;

	if ( random->RandomInt( 1, 2 ) == 1 )
	{
		IdleSound();
	}

	float duration = ENVELOPE_CONTROLLER.SoundPlayEnvelope( m_pMoanSound, SOUNDCTRL_CHANGE_VOLUME, pEnvelope, iEnvelopeSize );

	float flPitchShift = random->RandomInt( -4, 4 );
	ENVELOPE_CONTROLLER.SoundChangePitch( m_pMoanSound, m_flMoanPitch + flPitchShift, 0.3 );

	m_flNextMoanSound = gpGlobals->curtime + duration + 9999;
}

//-----------------------------------------------------------------------------
// Purpose: Overloaded so that explosions don't split the poison zombie in twain.
//-----------------------------------------------------------------------------
bool CNPC_PoisonZombine::ShouldBecomeTorso( const CTakeDamageInfo &info, float flDamageThreshold )
{
	return false;
}

AI_BEGIN_CUSTOM_NPC( npc_poisonzombine, CNPC_PoisonZombine )

	//Adrian: events go here
	DECLARE_ANIMEVENT( AE_ZOMBINE_POISON_SPIT )
 DECLARE_ANIMEVENT( AE_ZOMBINE_POISON_PUKE )

	DEFINE_SCHEDULE
	(
		SCHED_ZOMBINE_POISON_RANGE_ATTACK2,

		"	Tasks"
		"		TASK_STOP_MOVING						0"
		"		TASK_FACE_IDEAL							0"
		"		TASK_PLAY_PRIVATE_SEQUENCE_FACE_ENEMY	ACTIVITY:ACT_ZOMBIE_POISON_THREAT"
		"		TASK_FACE_IDEAL							0"
		"		TASK_RANGE_ATTACK2						0"

		"	Interrupts"
		"		COND_NO_PRIMARY_AMMO"
	)

	DEFINE_SCHEDULE
	(
		SCHED_ZOMBINE_POISON_RANGE_ATTACK1,

		"	Tasks"
		"		TASK_STOP_MOVING		0"
		"		TASK_FACE_ENEMY			0"
		"		TASK_ANNOUNCE_ATTACK	1"	// 1 = primary attack
		"		TASK_RANGE_ATTACK1		0"
		""
		"	Interrupts"
		"		COND_NO_PRIMARY_AMMO"
	)

AI_END_CUSTOM_NPC()
