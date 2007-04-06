/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * $Id$
 *
 */

#include "../../includes/win32def.h"
#include "TableMgr.h"
#include "ResourceMgr.h"
#include "SoundMgr.h" //pst (react to death sounds)
#include "Actor.h"
#include "Interface.h"
#include "../../includes/strrefs.h"
#include "Item.h"
#include "Spell.h"
#include "Projectile.h"
#include "Game.h"
#include "GameScript.h"
#include "ScriptEngine.h"
#include "GSUtils.h" //needed for DisplayStringCore
#include "Video.h"
#include <cassert>
#include "damages.h"

extern Interface* core;
#ifdef WIN32
extern HANDLE hConsole;
#endif

static Color green = {
	0x00, 0xff, 0x00, 0xff
};
static Color red = {
	0xff, 0x00, 0x00, 0xff
};
static Color yellow = {
	0xff, 0xff, 0x00, 0xff
};
static Color cyan = {
	0x00, 0xff, 0xff, 0xff
};
static Color magenta = {
	0xff, 0x00, 0xff, 0xff
};

static int sharexp = SX_DIVIDE;
static int classcount = -1;
static char **clericspelltables = NULL;
static char **wizardspelltables = NULL;
static int FistRows = -1;
typedef ieResRef FistResType[MAX_LEVEL+1];

static FistResType *fistres = NULL;
static ieResRef DefaultFist = {"FIST"};

//item usability array
struct ItemUseType {
	ieResRef table; //which table contains the stat usability flags
	ieByte stat;	//which actor stat we talk about
	ieByte mcol;	//which column should be matched against the stat
	ieByte vcol;	//which column has the bit value for it
	ieByte which;	//which item dword should be used (1 = kit)
};

static ItemUseType *itemuse = NULL;
static int usecount = -1;
static int fiststat = IE_CLASS;

static ActionButtonRow *GUIBTDefaults = NULL; //qslots row count
ActionButtonRow DefaultButtons = {ACT_TALK, ACT_WEAPON1, ACT_WEAPON2,
 ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
 ACT_NONE, ACT_INNATE};
static int QslotTranslation = false;

static char iwd2gemrb[32] = {
	0,0,20,2,22,25,0,14,
	15,23,13,0,1,24,8,21,
	0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0
};
static char gemrb2iwd[32] = {
	11,12,3,71,72,73,0,0, //0
	14,80,83,82,81,10,7,8, //8
	0,0,0,0,2,15,4,9, //16
	13,5,0,0,0,0,0,0 //24
};

//letters for char sound resolution bg1/bg2
static char csound[VCONST_COUNT];

static void InitActorTables();

static ieDword TranslucentShadows;

#define DAMAGE_LEVELS 13
#define ATTACKROLL    20
#define SAVEROLL      20
#define DEFAULTAC     10

static ieResRef d_main[DAMAGE_LEVELS] = {
	//slot 0 is not used in the original engine
	"BLOODCR","BLOODS","BLOODM","BLOODL", //blood
	"SPFIRIMP","SPFIRIMP","SPFIRIMP",     //fire
	"SPSHKIMP","SPSHKIMP","SPSHKIMP",     //spark
	"SPFIRIMP","SPFIRIMP","SPFIRIMP",     //ice
};
static ieResRef d_splash[DAMAGE_LEVELS] = {
	"","","","",
	"SPBURN","SPBURN","SPBURN", //flames
	"SPSPARKS","SPSPARKS","SPSPARKS", //sparks
	"","","",
};

#define BLOOD_GRADIENT 19
#define FIRE_GRADIENT 19
#define ICE_GRADIENT 71
#define STONE_GRADIENT 93

static int d_gradient[DAMAGE_LEVELS] = {
	BLOOD_GRADIENT,BLOOD_GRADIENT,BLOOD_GRADIENT,BLOOD_GRADIENT,
	FIRE_GRADIENT,FIRE_GRADIENT,FIRE_GRADIENT,
	-1,-1,-1,
	ICE_GRADIENT,ICE_GRADIENT,ICE_GRADIENT,
};
//the possible hardcoded overlays (they got separate stats)
#define OVERLAY_COUNT  8
#define OV_ENTANGLE    0
#define OV_SANCTUARY   1
#define OV_MINORGLOBE  2
#define OV_SHIELDGLOBE 3
#define OV_GREASE      4
#define OV_WEB         5
#define OV_BOUNCE      6  //bouncing
#define OV_BOUNCE2     7  //bouncing activated
static ieResRef overlay[OVERLAY_COUNT]={"SPENTACI","SANCTRY","MINORGLB","SPSHIELD",
"GREASED","WEBENTD","SPTURNI2","SPTURNI"};

//for every game except IWD2 we need to reverse TOHIT
static bool REVERSE_TOHIT=true;
static bool CHECK_ABILITIES=false;

//internal flags for calculating to hit
#define WEAPON_FIST        0
#define WEAPON_MELEE       1
#define WEAPON_RANGED      2
#define WEAPON_STYLEMASK   15
#define WEAPON_LEFTHAND    16
#define WEAPON_USESTRENGTH 32

void ReleaseMemoryActor()
{
	if (fistres) {
		delete [] fistres;
		fistres = NULL;
	}
	if (itemuse) {
		delete [] itemuse;
		itemuse = NULL;
	}
	FistRows = -1;
}

Actor::Actor()
	: Movable( ST_ACTOR )
{
	int i;

	for (i = 0; i < MAX_STATS; i++) {
		BaseStats[i] = 0;
		Modified[i] = 0;
	}
	SmallPortrait[0] = 0;
	LargePortrait[0] = 0;

	anims = NULL;
	ShieldRef[0]=0;
	HelmetRef[0]=0;
	WeaponRef[0]=0;
	for (i = 0; i < EXTRA_ACTORCOVERS; ++i)
		extraCovers[i] = NULL;

	LongName = NULL;
	ShortName = NULL;
	LongStrRef = (ieStrRef) -1;
	ShortStrRef = (ieStrRef) -1;

	LastProtected = 0;
	LastFollowed = 0;
	LastCommander = 0;
	LastHelp = 0;
	LastSeen = 0;
	LastMarked = 0;
	LastHeard = 0;
	PCStats = NULL;
	LastCommand = 0; //used by order
	LastShout = 0; //used by heard
	LastDamage = 0;
	LastDamageType = 0;
	HotKey = 0;
	attackcount = 0;
	initiative = 0;
	InTrap = 0;

	inventory.SetInventoryType(INVENTORY_CREATURE);
	fxqueue.SetOwner( this );
	inventory.SetOwner( this );
	if (classcount<0) {
		InitActorTables();

		TranslucentShadows = 0;
		core->GetDictionary()->Lookup("Translucent Shadows",
			TranslucentShadows);
	}
	TalkCount = 0;
	InteractCount = 0; //numtimesinteracted depends on this
	appearance = 0xffffff; //might be important for created creatures
	version = 0;
	//these are used only in iwd2 so we have to default them
	for(i=0;i<7;i++) {
		BaseStats[IE_HATEDRACE2+i]=0xff;
	}
	//this one is saved only for PC's
	ModalState = 0;
	//this one is saved, but not loaded?
	localID = globalID = 0;
}

Actor::~Actor(void)
{
	unsigned int i;

	if (anims) {
		delete( anims );
	}
	core->FreeString( LongName );
	core->FreeString( ShortName );
	if (PCStats) {
		delete PCStats;
	}
	for (i = 0; i < vvcOverlays.size(); i++) {
		if (vvcOverlays[i]) {
			delete vvcOverlays[i];
			vvcOverlays[i] = NULL;
		}
	}
	for (i = 0; i < vvcShields.size(); i++) {
		if (vvcShields[i]) {
			delete vvcShields[i];
			vvcShields[i] = NULL;
		}
	}
}

void Actor::SetFistStat(ieDword stat)
{
	fiststat = stat;
}

void Actor::SetDefaultActions(bool qslot, ieByte slot1, ieByte slot2, ieByte slot3)
{
	QslotTranslation=qslot;
	DefaultButtons[0]=slot1;
	DefaultButtons[1]=slot2;
	DefaultButtons[2]=slot3;
}

void Actor::SetText(char* ptr, unsigned char type)
{
	size_t len = strlen( ptr ) + 1;
	//32 is the maximum possible length of the actor name in the original games
	if (len>32) len=33;
	if (type!=2) {
		LongName = ( char * ) realloc( LongName, len );
		memcpy( LongName, ptr, len );
	}
	if (type!=1) {
		ShortName = ( char * ) realloc( ShortName, len );
		memcpy( ShortName, ptr, len );
	}
}

void Actor::SetText(int strref, unsigned char type)
{
	if (type!=2) {
		if (LongName) free(LongName);
		LongName = core->GetString( strref );
	}
	if (type!=1) {
		if (ShortName) free(ShortName);
		ShortName = core->GetString( strref );
	}
}

void Actor::SetAnimationID(unsigned int AnimID)
{
	//if the palette is locked, then it will be transferred to the new animation
	Palette *recover = NULL;

	if (anims) {
		if (anims->lockPalette) {
			recover = anims->palette[PAL_MAIN];
		}
		// Take ownership so the palette won't be deleted
		if (recover) {
			recover->IncRef();
		}
		delete( anims );
	}
	//hacking PST no palette
	if (core->HasFeature(GF_ONE_BYTE_ANIMID) ) {
		if ((AnimID&0xf000)==0xe000) {
			if (BaseStats[IE_COLORCOUNT]) {
				printMessage("Actor"," ",YELLOW);
				printf("Animation ID %x is supposed to be real colored (no recoloring), patched creature\n", AnimID);
			}
			BaseStats[IE_COLORCOUNT]=0;
		}
	}
	anims = new CharAnimations( AnimID&0xffff, BaseStats[IE_ARMOR_TYPE]);
	if (anims) {
		anims->SetOffhandRef(ShieldRef);
		anims->SetHelmetRef(HelmetRef);
		anims->SetWeaponRef(WeaponRef);

		//if we have a recovery palette, then set it back
		assert(anims->palette[PAL_MAIN] == 0);
		anims->palette[PAL_MAIN] = recover;
		if (recover) {
			anims->lockPalette = true;
		}
		//bird animations are not hindered by searchmap
		//only animtype==7 (bird) uses this feature
		//this is a hardcoded hack, but works for all engine type
		if (anims->GetAnimType()!=IE_ANI_BIRD) {
			BaseStats[IE_DONOTJUMP]=0;
		} else {
			BaseStats[IE_DONOTJUMP]=DNJ_BIRD;
		}
		SetCircleSize();
		anims->SetColors(BaseStats+IE_COLORS);
	} else {
		printMessage("Actor", " ",LIGHT_RED);
		printf("Missing animation for %s\n",LongName);
	}
}

CharAnimations* Actor::GetAnims()
{
	return anims;
}

/** Returns a Stat value (Base Value + Mod) */
ieDword Actor::GetStat(unsigned int StatIndex) const
{
	if (StatIndex >= MAX_STATS) {
		return 0xdadadada;
	}
	return Modified[StatIndex];
}

void Actor::SetCircleSize()
{
	Color* color;
	int color_index;

	if (Modified[IE_UNSELECTABLE]) {
		color = &magenta;
		color_index = 4;
	} else if (Modified[IE_STATE_ID] & STATE_PANIC) {
		color = &yellow;
		color_index = 5;
	} else {
		switch (Modified[IE_EA]) {
			case EA_PC:
			case EA_FAMILIAR:
			case EA_ALLY:
			case EA_CONTROLLED:
			case EA_CHARMED:
			case EA_EVILBUTGREEN:
			case EA_GOODCUTOFF:
				color = &green;
				color_index = 0;
				break;

			case EA_ENEMY:
			case EA_GOODBUTRED:
			case EA_EVILCUTOFF:
				color = &red;
				color_index = 1;
				break;
			default:
				color = &cyan;
				color_index = 2;
				break;
		}
	}

	if (!anims)
		return;
	int csize = anims->GetCircleSize() - 1;
	if (csize >= MAX_CIRCLE_SIZE)
		csize = MAX_CIRCLE_SIZE - 1;

	SetCircle( anims->GetCircleSize(), *color, core->GroundCircles[csize][color_index], core->GroundCircles[csize][(color_index == 0) ? 3 : color_index] );
}

//call this when morale or moralebreak changed
void pcf_morale (Actor *actor, ieDword /*Value*/)
{
	if (actor->Modified[IE_MORALE]<=actor->Modified[IE_MORALEBREAK] ) {
		actor->Panic();
	}
	//for new colour
	actor->SetCircleSize();
}

void pcf_ea (Actor *actor, ieDword /*Value*/)
{
	actor->SetCircleSize();
}

//this is a good place to recalculate level up stuff
void pcf_level (Actor *actor, ieDword /*Value*/)
{
	ieDword sum =
		actor->BaseStats[IE_LEVELFIGHTER]+
		actor->BaseStats[IE_LEVELMAGE]+
		actor->BaseStats[IE_LEVELTHIEF]+
		actor->BaseStats[IE_LEVELBARBARIAN]+
		actor->BaseStats[IE_LEVELBARD]+
		actor->BaseStats[IE_LEVELCLERIC]+
		actor->BaseStats[IE_LEVELDRUID]+
		actor->BaseStats[IE_LEVELMONK]+
		actor->BaseStats[IE_LEVELPALADIN]+
		actor->BaseStats[IE_LEVELRANGER]+
		actor->BaseStats[IE_LEVELSORCEROR];
	actor->SetBase(IE_CLASSLEVELSUM,sum);
	actor->SetupFist();
}

void pcf_class (Actor *actor, ieDword Value)
{
	actor->InitButtons(Value);
}

void pcf_animid(Actor *actor, ieDword Value)
{
	actor->SetAnimationID(Value);
}

static void SetLockedPalette(Actor *actor, ieDword *gradients)
{
	CharAnimations *anims = actor->GetAnims();
	if (!anims) return; //cannot apply it (yet)
	if (anims->lockPalette) return;
	//force initialisation of animation
	anims->SetColors( gradients );
	anims->GetAnimation(0,0);
	if (anims->palette[PAL_MAIN]) {
		anims->lockPalette=true;
	}
}

static void UnlockPalette(Actor *actor)
{
	CharAnimations *anims = actor->GetAnims();
	if (anims) {
		anims->lockPalette=false;
		anims->SetColors(actor->Modified+IE_COLORS);
	}
}

ieDword fullwhite[7]={ICE_GRADIENT,ICE_GRADIENT,ICE_GRADIENT,ICE_GRADIENT,ICE_GRADIENT,ICE_GRADIENT,ICE_GRADIENT};

ieDword fullstone[7]={STONE_GRADIENT,STONE_GRADIENT,STONE_GRADIENT,STONE_GRADIENT,STONE_GRADIENT,STONE_GRADIENT,STONE_GRADIENT};

void pcf_state(Actor *actor, ieDword Value)
{
	if (Value & STATE_PETRIFIED) {
		SetLockedPalette( actor, fullstone);
		return;
	}
	if (Value & STATE_FROZEN) {
		SetLockedPalette(actor, fullwhite);
		return;
	}
	UnlockPalette(actor);
}

void pcf_hitpoint(Actor *actor, ieDword Value)
{
	if ((signed) Value>(signed) actor->Modified[IE_MAXHITPOINTS]) {
		Value=actor->Modified[IE_MAXHITPOINTS];
	}
	if ((signed) Value<(signed) actor->Modified[IE_MINHITPOINTS]) {
		Value=actor->Modified[IE_MINHITPOINTS];
	}
	if ((signed) Value<=0) {
		actor->Die(NULL);
	}
	actor->Modified[IE_MINHITPOINTS]=Value;
}

void pcf_maxhitpoint(Actor *actor, ieDword Value)
{
	if ((signed) Value<(signed) actor->Modified[IE_HITPOINTS]) {
		actor->Modified[IE_HITPOINTS]=Value;
		pcf_hitpoint(actor,Value);
	}
}

void pcf_minhitpoint(Actor *actor, ieDword Value)
{
	if ((signed) Value>(signed) actor->Modified[IE_HITPOINTS]) {
		actor->Modified[IE_HITPOINTS]=Value;
		pcf_hitpoint(actor,Value);
	}
}

void pcf_con(Actor *actor, ieDword Value)
{
	if ((signed) Value<=0) {
		actor->Die(NULL);
	}
	pcf_hitpoint(actor, actor->Modified[IE_HITPOINTS]);
}

void pcf_stat(Actor *actor, ieDword Value)
{
	if ((signed) Value<=0) {
		actor->Die(NULL);
	}
}

void pcf_gold(Actor *actor, ieDword /*Value*/)
{
	//this function will make a party member automatically donate their
	//gold to the party pool, not the same as in the original engine
	if (actor->InParty) {
		Game *game = core->GetGame();
		game->AddGold ( actor->BaseStats[IE_GOLD] );
		actor->BaseStats[IE_GOLD]=0;
	}
}

//de/activates the entangle overlay
void pcf_entangle(Actor *actor, ieDword Value)
{
	if (Value) {
		if (actor->HasVVCCell(overlay[OV_ENTANGLE]))
			return;
		ScriptedAnimation *sca = core->GetScriptedAnimation(overlay[OV_ENTANGLE], false);
		actor->AddVVCell(sca);
	} else {
		actor->RemoveVVCell(overlay[OV_ENTANGLE], true);
	}
}

//de/activates the sanctuary overlay
//the sanctuary effect draws the globe half transparent

void pcf_sanctuary(Actor *actor, ieDword Value)
{
	if (Value) {
		if (!actor->HasVVCCell(overlay[OV_SANCTUARY])) {
			ScriptedAnimation *sca = core->GetScriptedAnimation(overlay[OV_SANCTUARY], false);
			actor->AddVVCell(sca);
		}
		SetLockedPalette(actor, fullwhite);
		return;
	}
	actor->RemoveVVCell(overlay[OV_SANCTUARY], true);
	UnlockPalette(actor);
}

//de/activates the prot from missiles overlay
void pcf_shieldglobe(Actor *actor, ieDword Value)
{
	if (Value) {
		if (actor->HasVVCCell(overlay[OV_SHIELDGLOBE]))
			return;
		ScriptedAnimation *sca = core->GetScriptedAnimation(overlay[OV_SHIELDGLOBE], false);
		actor->AddVVCell(sca);
	} else {
		actor->RemoveVVCell(overlay[OV_SHIELDGLOBE], true);
	}
}

//de/activates the globe of invul. overlay
void pcf_minorglobe(Actor *actor, ieDword Value)
{
	if (Value) {
		if (actor->HasVVCCell(overlay[OV_MINORGLOBE]))
			return;
		ScriptedAnimation *sca = core->GetScriptedAnimation(overlay[OV_MINORGLOBE], false);
		actor->AddVVCell(sca);
	} else {
		actor->RemoveVVCell(overlay[OV_MINORGLOBE], true);
	}
}

//de/activates the grease background
void pcf_grease(Actor *actor, ieDword Value)
{
	if (Value) {
		if (actor->HasVVCCell(overlay[OV_GREASE]))
			return;
		actor->add_animation(overlay[OV_GREASE], -1, -1, 0);
	} else {
		actor->RemoveVVCell(overlay[OV_GREASE], true);
	}
}

//de/activates the web overlay
//the web effect also immobilizes the actor!
void pcf_web(Actor *actor, ieDword Value)
{
	if (Value) {
		if (actor->HasVVCCell(overlay[OV_WEB]))
			return;
		actor->add_animation(overlay[OV_WEB], -1, 0, 0);
	} else {
		actor->RemoveVVCell(overlay[OV_WEB], true);
	}
}

//de/activates the spell bounce background
void pcf_bounce(Actor *actor, ieDword Value)
{
	if (Value) {
		actor->add_animation(overlay[OV_BOUNCE], -1, -1, 0);
	} else {
		//it seems we have to remove it abruptly
		actor->RemoveVVCell(overlay[OV_BOUNCE], false);
	}
}

//no separate values (changes are permanent)
void pcf_fatigue(Actor *actor, ieDword Value)
{
	actor->BaseStats[IE_FATIGUE]=Value;
}

//no separate values (changes are permanent)
void pcf_intoxication(Actor *actor, ieDword Value)
{
	actor->BaseStats[IE_INTOXICATION]=Value;
}

void pcf_color(Actor *actor, ieDword /*Value*/)
{
	CharAnimations *anims = actor->GetAnims();
	if (anims) {
		anims->SetColors(actor->Modified+IE_COLORS);
	}
}

void pcf_armorlevel(Actor *actor, ieDword Value)
{
	CharAnimations *anims = actor->GetAnims();
	if (anims) {
		anims->SetArmourLevel(Value);
	}
}

static int maximum_values[256]={
32767,32767,20,100,100,100,100,25,5,25,25,25,25,25,100,100,//0f
100,100,100,100,100,100,100,100,100,200,200,200,200,200,100,100,//1f
200,200,MAX_LEVEL,255,25,100,25,25,25,25,25,999999999,999999999,999999999,25,25,//2f
200,255,200,100,100,200,200,25,5,100,1,1,100,1,1,1,//3f
1,1,1,1,MAX_LEVEL,MAX_LEVEL,1,9999,25,100,100,255,1,20,20,25,//4f
25,1,1,255,25,25,255,255,25,5,5,5,5,5,5,5,//5f
5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,//6f
5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,//7f
5,5,5,5,5,5,5,100,100,100,255,5,5,255,1,1,//8f
1,25,25,30,1,1,1,25,-1,100,100,1,255,255,255,255,//9f
255,255,255,255,255,255,20,255,255,1,20,255,999999999,999999999,1,1,//af
999999999,999999999,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//bf
0,0,0,0,0,0,0,25,25,255,255,255,255,65535,-1,-1,//cf - 207
-1,-1,-1,-1,-1,-1,-1,-1,MAX_LEVEL,255,65535,3,255,255,255,255,//df - 223
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,//ef - 239
MAX_LEVEL,MAX_LEVEL,MAX_LEVEL,MAX_LEVEL, MAX_LEVEL,MAX_LEVEL,MAX_LEVEL,MAX_LEVEL, //0xf7 - 247
MAX_LEVEL,MAX_LEVEL,MAX_LEVEL,MAX_LEVEL, 255,65535,65535,15//ff
};

typedef void (*PostChangeFunctionType)(Actor *actor, ieDword Value);
static PostChangeFunctionType post_change_functions[256]={
pcf_hitpoint, pcf_maxhitpoint, NULL, NULL, NULL, NULL, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL, //0f
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, NULL, pcf_fatigue, pcf_intoxication, //1f
NULL,NULL,pcf_level,NULL, pcf_stat, NULL, pcf_stat, pcf_stat,
pcf_stat,pcf_con,NULL,NULL, NULL, pcf_gold, pcf_morale, NULL, //2f
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, NULL, pcf_entangle, pcf_sanctuary, //3f
pcf_minorglobe, pcf_shieldglobe, pcf_grease, pcf_web, pcf_level, pcf_level, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL, //4f
NULL,NULL,NULL,pcf_minhitpoint, NULL, NULL, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL, //5f
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL, //6f
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL, //7f
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL, //8f
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL, //9f
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL, //af
NULL,NULL,NULL,NULL, pcf_morale, pcf_bounce, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL, //bf
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL,
NULL,NULL,NULL,NULL, NULL, pcf_animid,pcf_state, NULL, //cf
pcf_color,pcf_color,pcf_color,pcf_color, pcf_color, pcf_color, pcf_color, NULL,
NULL,NULL,NULL,pcf_armorlevel, NULL, NULL, NULL, NULL, //df
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL,
pcf_class,NULL,pcf_ea,NULL, NULL, NULL, NULL, NULL, //ef
pcf_level,pcf_level,pcf_level,pcf_level, pcf_level, pcf_level, pcf_level, pcf_level,
NULL,NULL,NULL,NULL, NULL, NULL, NULL, NULL //ff
};

/** call this from ~Interface() */
void Actor::ReleaseMemory()
{
	int i;

	if (classcount>=0) {
		if (clericspelltables) {
			for (i=0;i<classcount;i++) {
				if (clericspelltables[i]) {
					free (clericspelltables[i]);
				}
			}
			free(clericspelltables);
		}
		if (wizardspelltables) {
			for (i=0;i<classcount;i++) {
				if (wizardspelltables[i]) {
					free(wizardspelltables[i]);
				}
			}
			free(wizardspelltables);
		}
	}
	if (GUIBTDefaults) {
		free (GUIBTDefaults);
		GUIBTDefaults=NULL;
	}
	classcount=-1;
}

#define COL_HATERACE      0   //ranger type racial enemy
#define COL_CLERIC_SPELL  1   //cleric spells
#define COL_MAGE_SPELL    2   //mage spells
#define COL_STARTXP       3   //starting xp
#define COL_BARD_SKILL    4   //bard skills
#define COL_THIEF_SKILL   5   //thief skills

#define COL_MAIN       0
#define COL_SPARKS     1
#define COL_GRADIENT   2

static void InitActorTables()
{
	int i;

	if (core->HasFeature(GF_CHALLENGERATING)) {
		sharexp=SX_DIVIDE|SX_CR;
	} else {
		sharexp=SX_DIVIDE;
	}
	REVERSE_TOHIT=(bool) core->HasFeature(GF_REVERSE_TOHIT);
	CHECK_ABILITIES=(bool) core->HasFeature(GF_CHECK_ABILITIES);

	//this table lists skill groups assigned to classes
	//it is theoretically possible to create hybrid classes
	int table = core->LoadTable( "clskills" );
	TableMgr *tm = core->GetTable( table );
	if (tm) {
		classcount = tm->GetRowCount();
		clericspelltables = (char **) calloc(classcount, sizeof(char*));
		wizardspelltables = (char **) calloc(classcount, sizeof(char*));
		for(i = 0; i<classcount; i++) {
			const char *spelltablename = tm->QueryField( i, 1 );
			if (spelltablename[0]!='*') {
				clericspelltables[i]=strdup(spelltablename);
			}
			spelltablename = tm->QueryField( i, 2 );
			if (spelltablename[0]!='*') {
				wizardspelltables[i]=strdup(spelltablename);
			}
		}
		core->DelTable( table );
	} else {
		classcount = 0; //well
	}

	i = core->GetMaximumAbility();
	maximum_values[IE_STR]=i;
	maximum_values[IE_INT]=i;
	maximum_values[IE_DEX]=i;
	maximum_values[IE_CON]=i;
	maximum_values[IE_CHR]=i;
	maximum_values[IE_WIS]=i;

	//initializing the vvc resource references
	table = core->LoadTable( "damage" );
	tm = core->GetTable( table );
	if (tm) {
		for (i=0;i<DAMAGE_LEVELS;i++) {
			const char *tmp = tm->QueryField( i, COL_MAIN );
			strnlwrcpy(d_main[i], tmp, 8);
			if (d_main[i][0]=='*') {
				d_main[i][0]=0;
			}
			tmp = tm->QueryField( i, COL_SPARKS );
			strnlwrcpy(d_splash[i], tmp, 8);
			if (d_splash[i][0]=='*') {
				d_splash[i][0]=0;
			}
			tmp = tm->QueryField( i, COL_GRADIENT );
			d_gradient[i]=atoi(tmp);
		}
		core->DelTable( table );
	}

	table = core->LoadTable( "overlay" );
	tm = core->GetTable( table );
	if (tm) {
		for (i=0;i<OVERLAY_COUNT;i++) {
			const char *tmp = tm->QueryField( i, 0 );
			strnlwrcpy(overlay[i], tmp, 8);
		}
		core->DelTable( table );
	}

	//csound for bg1/bg2
	memset(csound,0,sizeof(csound));
	if (!core->HasFeature(GF_SOUNDFOLDERS)) {
		table = core->LoadTable( "csound" );
		tm = core->GetTable( table );
		if (tm) {
			for(i=0;i<VCONST_COUNT;i++) {
				const char *tmp = tm->QueryField( i, 0 );
				if (tmp[0]!='*') {
					csound[i]=tmp[0];
				}
			}
			core->DelTable( table );
		}
	}

	table = core->LoadTable( "qslots" );
	tm = core->GetTable( table );
	GUIBTDefaults = (ActionButtonRow *) calloc( classcount,sizeof(ActionButtonRow) );

	for (i = 0; i < classcount; i++) {
		memcpy(GUIBTDefaults+i, &DefaultButtons, sizeof(ActionButtonRow));
		if (tm) {
			for (int j=0;j<MAX_QSLOTS;j++) {
				GUIBTDefaults[i][j+3]=(ieByte) atoi( tm->QueryField(i,j) );
			}
		}
	}
	if (tm) {
		core->DelTable( table );
	}

	table = core->LoadTable( "itemuse" );
	tm = core->GetTable( table );
	if (tm) {
		usecount = tm->GetRowCount();
		itemuse = new ItemUseType[usecount];
		for (i = 0; i < usecount; i++) {
			itemuse[i].stat = (ieByte) atoi( tm->QueryField(i,0) );
			strnlwrcpy(itemuse[i].table, tm->QueryField(i,1),8 );
			itemuse[i].mcol = (ieByte) atoi( tm->QueryField(i,2) );
			itemuse[i].vcol = (ieByte) atoi( tm->QueryField(i,3) );
			itemuse[i].which = (ieByte) atoi( tm->QueryField(i,4) );
			//limiting it to 0 or 1 to avoid crashes
			if (itemuse[i].which!=1) {
				itemuse[i].which=0;
			}
		}
		core->DelTable( table );
	}
}

void Actor::add_animation(const ieResRef resource, int gradient, int height, int flags)
{
	ScriptedAnimation *sca = core->GetScriptedAnimation(resource, false);
	if (!sca)
		return;
	sca->ZPos=height;
	if (flags&AA_PLAYONCE) {
		sca->PlayOnce();
	}
	if (flags&&AA_BLEND) {
		//pst anims need this?
		sca->SetBlend();
	}
	if (gradient!=-1) {
		sca->SetPalette(gradient, 4);
	}
	AddVVCell(sca);
}

void Actor::PlayDamageAnimation(int type)
{
	int i;

	switch(type) {
		case 0: case 1: case 2: case 3: //blood
			i = (int) GetStat(IE_ANIMATION_ID)>>16;
			if (!i) i = d_gradient[type];
			add_animation(d_main[type], i, 0, AA_PLAYONCE);
			break;
		case 4: case 5: case 7: //fire
			add_animation(d_main[type], d_gradient[type], 0, AA_PLAYONCE);
			for(i=DL_FIRE;i<=type;i++) {
				add_animation(d_splash[i], d_gradient[i], 0, AA_PLAYONCE);
			}
			break;
		case 8: case 9: case 10: //electricity
			add_animation(d_main[type], d_gradient[type], 0, AA_PLAYONCE);
			for(i=DL_ELECTRICITY;i<=type;i++) {
				add_animation(d_splash[i], d_gradient[i], 0, AA_PLAYONCE);
			}
			break;
		case 11: case 12: case 13://cold
			add_animation(d_main[type], d_gradient[type], 0, AA_PLAYONCE);
			break;
		case 14: case 15: case 16://acid
			add_animation(d_main[type], d_gradient[type], 0, AA_PLAYONCE);
			break;
		case 17: case 18: case 19://disintegrate
			add_animation(d_main[type], d_gradient[type], 0, AA_PLAYONCE);
			break;
	}
}

bool Actor::SetStat(unsigned int StatIndex, ieDword Value, int pcf)
{
	if (StatIndex >= MAX_STATS) {
		return false;
	}
	if ( (signed) Value<-100) {
		Value = (ieDword) -100;
	}
	else {
		if ( maximum_values[StatIndex]>0) {
			if ( (signed) Value>maximum_values[StatIndex]) {
				Value = (ieDword) maximum_values[StatIndex];
			}
		}
	}

	if (pcf && Modified[StatIndex]!=Value) {
		Modified[StatIndex] = Value;
		PostChangeFunctionType f = post_change_functions[StatIndex];
		if (f) (*f)(this, Value);
	} else {
		Modified[StatIndex] = Value;
	}
	return true;
}

int Actor::GetMod(unsigned int StatIndex)
{
	if (StatIndex >= MAX_STATS) {
		return 0xdadadada;
	}
	return (signed) Modified[StatIndex] - (signed) BaseStats[StatIndex];
}
/** Returns a Stat Base Value */
ieDword Actor::GetBase(unsigned int StatIndex)
{
	if (StatIndex >= MAX_STATS) {
		return 0xffff;
	}
	return BaseStats[StatIndex];
}

/** Sets a Stat Base Value */
/** If required, modify the modified value and run the pcf function */
bool Actor::SetBase(unsigned int StatIndex, ieDword Value)
{
	if (StatIndex >= MAX_STATS) {
		return false;
	}
	ieDword diff = Modified[StatIndex]-BaseStats[StatIndex];

	BaseStats[StatIndex] = Value;
	//if already initialized, then the modified stats
	//need to run the post change function (stat change can kill actor)
	SetStat (StatIndex, Value+diff, InternalFlags&IF_INITIALIZED);
	return true;
}

bool Actor::SetBaseBit(unsigned int StatIndex, ieDword Value, bool setreset)
{
	if (StatIndex >= MAX_STATS) {
		return false;
	}
	if (setreset) {
		BaseStats[StatIndex] |= Value;
	} else {
		BaseStats[StatIndex] &= ~Value;
	}
	//if already initialized, then the modified stats
	//need to run the post change function (stat change can kill actor)
	if (setreset) {
		SetStat (StatIndex, Modified[StatIndex]|Value, InternalFlags&IF_INITIALIZED);
	} else {
		SetStat (StatIndex, Modified[StatIndex]&~Value, InternalFlags&IF_INITIALIZED);
	}
	return true;
}

const unsigned char *Actor::GetStateString()
{
	if (!PCStats) {
		return NULL;
	}
	ieByte *tmp = PCStats->PortraitIconString;
	ieWord *Icons = PCStats->PortraitIcons;
	int j=0;
	for (int i=0;i<MAX_PORTRAIT_ICONS;i++) {
		if (!(Icons[i]&0xff00)) {
			tmp[j++]=(ieByte) ((Icons[i]&0xff)+65);
		}
	}
	tmp[j]=0;
	return tmp;
}

void Actor::AddPortraitIcon(ieByte icon)
{
	if (!PCStats) {
		return;
	}
	ieWord *Icons = PCStats->PortraitIcons;

	for(int i=0;i<MAX_PORTRAIT_ICONS;i++) {
		if (Icons[i]==0xffff) {
			Icons[i]=icon;
			return;
		}
		if (icon == (Icons[i]&0xff)) {
			return;
		}
	}
}

void Actor::DisablePortraitIcon(ieByte icon)
{
	if (!PCStats) {
		return;
	}
	ieWord *Icons = PCStats->PortraitIcons;
	int i;

	for(i=0;i<MAX_PORTRAIT_ICONS;i++) {
		if (icon == (Icons[i]&0xff)) {
			Icons[i]=0xff00|icon;
			return;
		}
	}
}

/** call this after load, to apply effects */
void Actor::RefreshEffects()
{
	ieDword previous[MAX_STATS];

	bool first = !(InternalFlags&IF_INITIALIZED);

	if (PCStats) {
		memset( PCStats->PortraitIcons, -1, sizeof(PCStats->PortraitIcons) );
	}
	if (first) {
		InternalFlags|=IF_INITIALIZED;
	} else {
		memcpy( previous, Modified, MAX_STATS * sizeof( *Modified ) );
	}
	memcpy( Modified, BaseStats, MAX_STATS * sizeof( *Modified ) );

	CharAnimations* anims = GetAnims();
	if (anims) {
		unsigned int location;
		for (location = 0; location < 32; ++location) {
			anims->ColorMods[location].type = RGBModifier::NONE;
			anims->ColorMods[location].speed = 0;
		}
	}

	fxqueue.ApplyAllEffects( this );

	//calculate hp bonus
	int bonus;

	//fighter or not (still very primitive model, we need multiclass)
	if (Modified[IE_CLASS]==2) {
		bonus = core->GetConstitutionBonus(STAT_CON_HP_WARRIOR,Modified[IE_CON]);
	} else {
		bonus = core->GetConstitutionBonus(STAT_CON_HP_NORMAL,Modified[IE_CON]);
	}
	bonus *= GetXPLevel( true );

	NewBase(IE_MORALE,1,MOD_ADDITIVE);
	if (bonus<0 && (Modified[IE_MAXHITPOINTS]+bonus)<=0) {
		bonus=1-Modified[IE_MAXHITPOINTS];
	}
	Modified[IE_MAXHITPOINTS]+=bonus;
	Modified[IE_HITPOINTS]+=bonus;

	for (unsigned int i=0;i<MAX_STATS;i++) {
		if (first || (Modified[i]!=previous[i]) ) {
			PostChangeFunctionType f = post_change_functions[i];
			if (f) {
				(*f)(this, Modified[i]);
			}
		}
	}
}

void Actor::RollSaves()
{
	if (InternalFlags&IF_USEDSAVE) {
		SavingThrow[0]=(ieByte) core->Roll(1,SAVEROLL,0);
		SavingThrow[1]=(ieByte) core->Roll(1,SAVEROLL,0);
		SavingThrow[2]=(ieByte) core->Roll(1,SAVEROLL,0);
		SavingThrow[3]=(ieByte) core->Roll(1,SAVEROLL,0);
		SavingThrow[4]=(ieByte) core->Roll(1,SAVEROLL,0);
		InternalFlags&=~IF_USEDSAVE;
	}
}

/** returns true if actor made the save against saving throw type */
bool Actor::GetSavingThrow(ieDword type, int modifier)
{
	assert(type<5);
	InternalFlags|=IF_USEDSAVE;
	int ret = SavingThrow[type];
	if (ret == 1) return false;
	if (ret == SAVEROLL) return true;
	ret += modifier;
	return ret > (int) GetStat(IE_SAVEVSDEATH+type);
}

/** implements a generic opcode function, modify modified stats
 returns the change
*/
int Actor::NewStat(unsigned int StatIndex, ieDword ModifierValue, ieDword ModifierType)
{
	int oldmod = Modified[StatIndex];

	switch (ModifierType) {
		case MOD_ADDITIVE:
			//flat point modifier
			SetStat(StatIndex, Modified[StatIndex]+ModifierValue, 0);
			break;
		case MOD_ABSOLUTE:
			//straight stat change
			SetStat(StatIndex, ModifierValue, 0);
			break;
		case MOD_PERCENT:
			//percentile
			SetStat(StatIndex, BaseStats[StatIndex] * ModifierValue / 100, 0);
			break;
	}
	return Modified[StatIndex] - oldmod;
}

int Actor::NewBase(unsigned int StatIndex, ieDword ModifierValue, ieDword ModifierType)
{
	int oldmod = BaseStats[StatIndex];

	switch (ModifierType) {
		case MOD_ADDITIVE:
			//flat point modifier
			SetBase(StatIndex, BaseStats[StatIndex]+ModifierValue);
			break;
		case MOD_ABSOLUTE:
			//straight stat change
			SetBase(StatIndex, ModifierValue);
			break;
		case MOD_PERCENT:
			//percentile
			SetBase(StatIndex, BaseStats[StatIndex] * ModifierValue / 100);
			break;
	}
	return BaseStats[StatIndex] - oldmod;
}

inline int CountElements(const char *s, char separator)
{
	int ret = 1;
	while(*s) {
		if (*s==separator) ret++;
		s++;
	}
	return ret;
}

void Actor::ReactToDeath(const char * deadname)
{
	int table = core->LoadTable( "death" );
	TableMgr *tm = core->GetTable( table );
	if (!tm) return;
	// lookup value based on died's scriptingname and ours
	// if value is 0 - use reactdeath
	// if value is 1 - use reactspecial
	// if value is string - use playsound instead (pst)
	const char *value = tm->QueryField (scriptName, deadname);
	switch (value[0]) {
	case '0':
		DisplayStringCore(this, VB_REACT, DS_CONSOLE|DS_CONST );
		break;
	case '1':
		DisplayStringCore(this, VB_REACT_S, DS_CONSOLE|DS_CONST );
		break;
	default:
		{
			int count = CountElements(value,',');
			if (count<=0) break;
			count = core->Roll(1,count,-1);
			ieResRef resref;
			while(count--) {
				while(*value && *value!=',') value++;
				if (*value==',') value++;
			}
			strncpy(resref, value, 8);
			for(count=0;count<8 && resref[count]!=',';count++);
			resref[count]=0;

			ieDword len = core->GetSoundMgr()->Play( resref );
			ieDword counter = ( AI_UPDATE_TIME * len ) / 1000;
			if (counter != 0)
				SetWait( counter );
			break;
		}
	}
	core->DelTable(table);
}

//call this only from gui selects
void Actor::SelectActor()
{
	DisplayStringCore(this, VB_SELECT, DS_CONSOLE|DS_CONST );
}

void Actor::Panic()
{
	if (GetStat(IE_STATE_ID)&STATE_PANIC) {
		//already in panic
		return;
	}
	SetBaseBit(IE_STATE_ID, STATE_PANIC, true);
	DisplayStringCore(this, VB_PANIC, DS_CONSOLE|DS_CONST );
}

void Actor::SetMCFlag(ieDword arg, int op)
{
	ieDword tmp = BaseStats[IE_MC_FLAGS];
	switch (op) {
	case BM_SET: tmp = arg; break;
	case BM_OR: tmp |= arg; break;
	case BM_NAND: tmp &= ~arg; break;
	case BM_XOR: tmp ^= arg; break;
	case BM_AND: tmp &= arg; break;
	}
	SetBase(IE_MC_FLAGS, tmp);
}

void Actor::DialogInterrupt()
{
	//if dialoginterrupt was set, no verbal constant
	if ( Modified[IE_MC_FLAGS]&MC_NO_TALK)
		return;
	if (Modified[IE_EA]>=EA_EVILCUTOFF) {
		DisplayStringCore(this, VB_HOSTILE, DS_CONSOLE|DS_CONST );
	} else {
		DisplayStringCore(this, VB_DIALOG, DS_CONSOLE|DS_CONST );
	}
}

//returns actual damage
int Actor::Damage(int damage, int damagetype, Actor *hitter)
{
	//recalculate damage based on resistances and difficulty level
	//the lower 2 bits are actually modifier types
	NewBase(IE_HITPOINTS, (ieDword) -damage, damagetype&3);
	NewBase(IE_MORALE, (ieDword) -1, MOD_ADDITIVE);
	//this is just a guess, probably morale is much more complex
	//add lastdamagetype up
	LastDamageType|=damagetype;
	LastDamage=damage;
	LastHitter=hitter->GetID();
	InternalFlags|=IF_ACTIVE;
	int chp = (signed) Modified[IE_HITPOINTS];
	int damagelevel = 3;
	if (damage<5) {
		damagelevel = 1;
	} else if (damage<10) {
		damagelevel = 2;
	} else {
		if (chp<-10) {
			damagelevel = 0; //chunky death
		}
		else {
			damagelevel = 3;
		}
	}
	if (damagetype & (DAMAGE_FIRE|DAMAGE_MAGICFIRE) ) {
		PlayDamageAnimation(DL_FIRE+damagelevel);
	} else if (damagetype & (DAMAGE_COLD|DAMAGE_MAGICCOLD) ) {
		PlayDamageAnimation(DL_COLD+damagelevel);
	} else if (damagetype & (DAMAGE_ELECTRICITY) ) {
		PlayDamageAnimation(DL_ELECTRICITY+damagelevel);
	} else if (damagetype & (DAMAGE_ACID) ) {
		PlayDamageAnimation(DL_ACID+damagelevel);
	} else if (damagetype & (DAMAGE_MAGIC) ) {
		PlayDamageAnimation(DL_DISINTEGRATE+damagelevel);
	} else {
		PlayDamageAnimation(damagelevel);
	}
	printMessage("Actor"," ",GREEN);
	printf("%d damage taken.\n", LastDamage);
	DisplayStringCore(this, VB_DAMAGE, DS_CONSOLE|DS_CONST );
	if (InParty) {
		if (chp<(signed) Modified[IE_MAXHITPOINTS]/10) {
			core->Autopause(AP_WOUNDED);
		}
		if (damage>0) {
			core->Autopause(AP_HIT);
		}
	}
	return damage;
}

void Actor::DebugDump()
{
	unsigned int i;

	printf( "Debugdump of Actor %s:\n", LongName );
	printf ("Scripts:");
	for (i = 0; i < MAX_SCRIPTS; i++) {
		const char* poi = "<none>";
		if (Scripts[i] && Scripts[i]->script) {
			poi = Scripts[i]->GetName();
		}
		printf( " %.8s", poi );
	}
	printf( "\nArea:       %.8s   ", Area );
	printf( "Dialog:     %.8s\n", Dialog );
	printf( "Global ID:  %d   Local ID:  %d\n", globalID, localID);
	printf( "Script name:%.32s\n", scriptName );
	printf( "TalkCount:  %d   ", TalkCount );
	printf( "PartySlot:  %d\n", InParty );
	printf( "Allegiance: %d   current allegiance:%d\n", BaseStats[IE_EA], Modified[IE_EA] );
	printf( "Class:      %d   current class:%d\n", BaseStats[IE_CLASS], Modified[IE_CLASS] );
	printf( "Race:       %d   current race:%d\n", BaseStats[IE_RACE], Modified[IE_RACE] );
	printf( "Gender:     %d   current gender:%d\n", BaseStats[IE_SEX], Modified[IE_SEX] );
	printf( "Specifics:  %d   current specifics:%d\n", BaseStats[IE_SPECIFIC], Modified[IE_SPECIFIC] );
	printf( "Alignment:  %x   current alignment:%x\n", BaseStats[IE_ALIGNMENT], Modified[IE_ALIGNMENT] );
	printf( "Morale:     %d   current morale:%d\n", BaseStats[IE_MORALE], Modified[IE_MORALE] );
	printf( "Moralebreak:%d   Morale recovery:%d\n", Modified[IE_MORALEBREAK], Modified[IE_MORALERECOVERYTIME] );
	printf( "Visualrange:%d (Explorer: %d)\n", Modified[IE_VISUALRANGE], Modified[IE_EXPLORE] );
	printf( "Mod[IE_ANIMATION_ID]: 0x%04X\n", Modified[IE_ANIMATION_ID] );
	printf( "Colors:    ");
	if (core->HasFeature(GF_ONE_BYTE_ANIMID) ) {
		for(i=0;i<Modified[IE_COLORCOUNT];i++) {
			printf("   %d", Modified[IE_COLORS+i]);
		}
	}
	else {
		for(i=0;i<7;i++) {
			printf("   %d", Modified[IE_COLORS+i]);
		}
	}
	printf ("\nAnimate ID: %x\n", Modified[IE_ANIMATION_ID]);
	printf( "WaitCounter: %d\n", (int) GetWait());
	printf( "LastTarget: %d %s\n", LastTarget, GetActorNameByID(LastTarget));
	printf( "LastTalked: %d %s\n", LastTalkedTo, GetActorNameByID(LastTalkedTo));
	inventory.dump();
	spellbook.dump();
	fxqueue.dump();
}

const char* Actor::GetActorNameByID(ieDword ID)
{
	Actor *actor = GetCurrentArea()->GetActorByGlobalID(ID);
	if (!actor) {
		return "<NULL>";
	}
	return actor->GetScriptName();
}

void Actor::SetMap(Map *map, ieWord LID, ieWord GID)
{
	Scriptable::SetMap(map);
	localID = LID;
	globalID = GID;
}

void Actor::SetPosition(Point &position, int jump, int radius)
{
	ClearPath();
	Point p;
	p.x = position.x/16;
	p.y = position.y/12;
	if (jump && !(Modified[IE_DONOTJUMP] & DNJ_FIT) && size ) {
		GetCurrentArea()->AdjustPosition( p, radius );
	}
	p.x = p.x * 16 + 8;
	p.y = p.y * 12 + 6;
	MoveTo( p );
}

/* this is returning the level of the character for xp calculations
	 later it could calculate with dual/multiclass,
	 also with iwd2's 3rd ed rules, this is why it is a separate function */
ieDword Actor::GetXPLevel(int modified) const
{
	if (modified) {
		return Modified[IE_LEVEL];
	}
	return BaseStats[IE_LEVEL];
}

/** maybe this would be more useful if we calculate with the strength too
*/
int Actor::GetEncumbrance()
{
	return inventory.GetWeight();
}

//receive turning
void Actor::Turn(Scriptable *cleric, ieDword turnlevel)
{
	//this is safely hardcoded i guess
	if (Modified[IE_GENERAL]!=GEN_UNDEAD) {
		return;
	}
	//determine if we see the cleric (distance)

	//determine alignment (if equals, then no turning)

	//determine panic or destruction
	//we get the modified level
	if (turnlevel>GetXPLevel(true)) {
		Die(cleric);
	} else {
		Panic();
	}
}

void Actor::Resurrect()
{
	if (!(Modified[IE_STATE_ID ] & STATE_DEAD)) {
		return;
	}
	InternalFlags&=IF_FROMGAME; //keep these flags (what about IF_INITIALIZED)
	InternalFlags|=IF_ACTIVE|IF_VISIBLE; //set these flags
	SetBase(IE_STATE_ID, 0);
	SetBase(IE_MORALE, 20);
	SetBase(IE_HITPOINTS, BaseStats[IE_MAXHITPOINTS]);
	ClearActions();
	ClearPath();
	SetStance(IE_ANI_EMERGE);
	//clear effects?
}

void Actor::Die(Scriptable *killer)
{
	if (InternalFlags&IF_REALLYDIED) {
		return; //can die only once
	}

	int minhp=Modified[IE_MINHITPOINTS];
	if (minhp) { //can't die
		SetBase(IE_HITPOINTS, minhp);
		return;
	}
	//Can't simply set Selected to false, game has its own little list
	Game *game = core->GetGame();
	game->SelectActor(this, false, SELECT_NORMAL);
	game->OutAttack(GetID());

	ClearPath();
	SetModal( 0 );
	DisplayStringCore(this, VB_DIE, DS_CONSOLE|DS_CONST );

	//JUSTDIED will be removed when the Die() trigger executed
	//otherwise it is the same as REALLYDIED
	InternalFlags|=IF_REALLYDIED|IF_JUSTDIED;
	SetStance( IE_ANI_DIE );

	if (InParty) {
		game->PartyMemberDied(this);
		core->Autopause(AP_DEAD);
	} else {
		Actor *act=NULL;
		if (!killer) {
			killer = area->GetActorByGlobalID(LastHitter);
		}

		if (killer) {
			if (killer->Type==ST_ACTOR) {
				act = (Actor *) killer;
			}
		}
		if (act && act->InParty) {
			//adjust game statistics here
			//game->KillStat(this, killer);
			InternalFlags|=IF_GIVEXP;
		}
	}
	//a plot critical creature has died (iwd2)
	if (BaseStats[IE_MC_FLAGS]&MC_PLOT_CRITICAL) {
		core->GetGUIScriptEngine()->RunFunction("DeathWindowPlot", false);
	}
	//ensure that the scripts of the actor will run as soon as possible
	ImmediateEvent();
}

void Actor::SetPersistent(int partyslot)
{
	InParty = (ieByte) partyslot;
	InternalFlags|=IF_FROMGAME;
	//if an actor is coming from a game, it should have these too
	CreateStats();
}

void Actor::DestroySelf()
{
	InternalFlags|=IF_CLEANUP;
}

bool Actor::CheckOnDeath()
{
	if (InternalFlags&IF_CLEANUP) {
		return true;
	}
	if (InternalFlags&IF_JUSTDIED) {
		if (GetNextAction()) {
			return false; //actor is currently dying, let him die first
		}
	}
	if (!(InternalFlags&IF_REALLYDIED) ) {
		return false;
	}
	//don't mess with the already deceased
	if (BaseStats[IE_STATE_ID]&STATE_DEAD) {
		return false;
	}
	//we need to check animID here, if it has not played the death
	//sequence yet, then we could return now
	ClearActions();
	//missed the opportunity of Died()
	InternalFlags&=~IF_JUSTDIED;

	Game *game = core->GetGame();
	if (InternalFlags&IF_GIVEXP) {
		//give experience to party
		game->ShareXP(Modified[IE_XPVALUE], sharexp );
		//handle reputation here
		//
	}

	if (KillVar[0]) {
		ieDword value = 0;
		if (core->HasFeature(GF_HAS_KAPUTZ) ) {
			game->kaputz->Lookup(KillVar, value);
			game->kaputz->SetAt(KillVar, value+1);
		} else {
			game->locals->Lookup(KillVar, value);
			game->locals->SetAt(KillVar, value+1);
		}
	}
	if (scriptName[0]) {
		ieVariable varname;
		ieDword value = 0;

		if (core->HasFeature(GF_HAS_KAPUTZ) ) {
			snprintf(varname, 32, "%s_DEAD", scriptName);
			game->kaputz->Lookup(varname, value);
			game->kaputz->SetAt(varname, value+1);
		} else {
			snprintf(varname, 32, "SPRITE_IS_DEAD%s", scriptName);
			game->locals->Lookup(varname, value);
			game->locals->SetAt(varname, value+1);
		}
	}

	DropItem("",0);
	//remove all effects that are not 'permanent after death' here
	//permanent after death type is 9
	SetBaseBit(IE_STATE_ID, STATE_DEAD, true);
	if (Modified[IE_MC_FLAGS]&MC_REMOVE_CORPSE) return true;
	if (Modified[IE_MC_FLAGS]&MC_KEEP_CORPSE) return false;
	//if chunked death, then return true
	if (LastDamageType&DAMAGE_CHUNKING) {
		//play chunky animation
		//chunks are projectiles
		return true;
	}
	return false;
}

/* this will create a heap at location, and transfer the item(s) */
void Actor::DropItem(const ieResRef resref, unsigned int flags)
{
	if (inventory.DropItemAtLocation( resref, flags, area, Pos )) {
		ReinitQuickSlots();
	}
}

void Actor::DropItem(int slot , unsigned int flags)
{
	if (inventory.DropItemAtLocation( slot, flags, area, Pos )) {
		ReinitQuickSlots();
	}
}

/** returns quick item data */
/** if header==-1 which is a 'use quickitem' action */
/** if header is set, then which is the absolute slot index, */
/** and header is the header index */
void Actor::GetItemSlotInfo(ItemExtHeader *item, int which, int header)
{
	ieWord idx;
	ieWord headerindex;

	memset(item, 0, sizeof(ItemExtHeader) );
	if (header<0) {
		if (!PCStats) return; //not a player character
		PCStats->GetSlotAndIndex(which,idx,headerindex);
		if (headerindex==0xffff) return; //headerindex is invalid
	} else {
		idx=(ieWord) which;
		headerindex=(ieWord) header;
	}
	CREItem *slot = inventory.GetSlotItem(idx);
	if (!slot) return; //quick item slot is empty
	Item *itm = core->GetItem(slot->ItemResRef);
	if (!itm) return; //quick item slot contains invalid item resref
	ITMExtHeader *ext_header = itm->GetExtHeader(headerindex);
	//item has no extended header, or header index is wrong
	if (!ext_header) return;
	memcpy(item->itemname, slot->ItemResRef, sizeof(ieResRef) );
	item->slot = idx;
	item->headerindex = headerindex;
	memcpy(&(item->AttackType), &(ext_header->AttackType),
 ((char *) &(item->itemname)) -((char *) &(item->AttackType)) );
	if (headerindex>=CHARGE_COUNTERS) {
		item->Charges=0;
	} else {
		item->Charges=slot->Usages[headerindex];
	}
	core->FreeItem(itm,slot->ItemResRef, false);
}

void Actor::ReinitQuickSlots()
{
	if (!PCStats) {
		return;
	}

	// Note: (wjp, 20061226)
	// This function needs some rethinking.
	// It tries to satisfy two things at the moment:
	//   Fill quickslots when they are empty and an item is placed in the
	//      inventory slot corresponding to the quickslot
	//   Reset quickslots when an item is removed
	// Currently, it resets all slots when items are removed,
	// but it only refills the ACT_QSLOTn slots, not the ACT_WEAPONx slots.
	//
	// Refilling a weapon slot is possible, but essentially duplicates a lot
	// of code from Inventory::EquipItem() which performs the same steps for
	// the Inventory::Equipped slot.
	// Hopefully, weapons/arrows are never added to inventory slots without
	// EquipItem being called.

	int i=sizeof(PCStats->QSlots);
	while (i--) {
		int slot;
		int which;
		if (i<0) which = ACT_WEAPON4+i+1;
		else which = PCStats->QSlots[i];
		switch (which) {
			case ACT_WEAPON1:
			case ACT_WEAPON2:
			case ACT_WEAPON3:
			case ACT_WEAPON4:
				CheckWeaponQuickSlot(which-ACT_WEAPON1);
				slot = 0;
				break;
				//WARNING:this cannot be condensed, because the symbols don't come in order!!!
			case ACT_QSLOT1: slot = inventory.GetQuickSlot(); break;
			case ACT_QSLOT2: slot = inventory.GetQuickSlot()+1; break;
			case ACT_QSLOT3: slot = inventory.GetQuickSlot()+2; break;
			case ACT_QSLOT4: slot = inventory.GetQuickSlot()+3; break;
			case ACT_QSLOT5: slot = inventory.GetQuickSlot()+4; break;
			default:
				slot = 0;
		}
		if (!slot) continue;
		//if magic items are equipped the equipping info doesn't change
		//(afaik)

		// Note: we're now in the QSLOTn case
		// If slot is empty, reset quickslot to 0xffff/0

		if (!inventory.HasItemInSlot("", slot)) {
			SetupQuickSlot(which, 0xffff, 0);
		} else {
			ieWord idx;
			ieWord headerindex;
			PCStats->GetSlotAndIndex(which,idx,headerindex);
			if (idx != slot || headerindex == 0xffff) {
				// If slot just became filled, set it to filled
				SetupQuickSlot(which,slot,0);
			}
		}
	}

	//these are always present
	CheckWeaponQuickSlot(0);
	CheckWeaponQuickSlot(1);
	//disabling quick weapon slots for certain classes
	for(i=0;i<2;i++) {
		int which = ACT_WEAPON3+i;
		// Assuming that ACT_WEAPON3 and 4 are always in the first two spots
		if (PCStats->QSlots[i]!=which) {
			SetupQuickSlot(which, 0xffff, 0);
		}
	}
}

void Actor::CheckWeaponQuickSlot(unsigned int which)
{
	if (!PCStats) return;

	bool empty = false;
	// If current quickweaponslot doesn't contain an item, reset it to fist
	int slot = PCStats->QuickWeaponSlots[which];
	int header = PCStats->QuickWeaponHeaders[which];
	if (!inventory.HasItemInSlot("", slot) || header == 0xffff) {
		empty = true;
	} else {
		// If current quickweaponslot contains ammo, and bow not found, reset

		if (core->QuerySlotEffects(slot) == SLOT_EFFECT_MISSILE) {
			CREItem *slotitm = inventory.GetSlotItem(slot);
			assert(slotitm);
			Item *itm = core->GetItem(slotitm->ItemResRef);
			assert(itm);
			ITMExtHeader *ext_header = itm->GetExtHeader(header);
			if (ext_header) {
				int type = ext_header->ProjectileQualifier;
				int weaponslot = inventory.FindTypedRangedWeapon(type);
				if (weaponslot == inventory.GetFistSlot()) {
					empty = true;
				}
			} else {
				empty = true;
			}
			core->FreeItem(itm,slotitm->ItemResRef, false);
		}
	}

	if (empty)
		SetupQuickSlot(ACT_WEAPON1+which, inventory.GetFistSlot(), 0);
}


void Actor::SetupQuickSlot(unsigned int which, int slot, int headerindex)
{
	if (!PCStats) return;
	PCStats->InitQuickSlot(which, (ieWord) slot, (ieWord) headerindex);
}

bool Actor::ValidTarget(int ga_flags)
{
	if (Immobile()) return false;
	switch(ga_flags&GA_ACTION) {
	case GA_PICK:
		if (Modified[IE_STATE_ID] & STATE_CANTSTEAL) return false;
		break;
	case GA_TALK:
		//can't talk to dead
		if (Modified[IE_STATE_ID] & STATE_CANTLISTEN) return false;
		//can't talk to hostile
		if (Modified[IE_EA]>=EA_EVILCUTOFF) return false;
		break;
	}
	if (ga_flags&GA_NO_DEAD) {
		if (InternalFlags&IF_JUSTDIED) return false;
		if (Modified[IE_STATE_ID] & STATE_DEAD) return false;
	}
	if (ga_flags&GA_SELECT) {
		if (Modified[IE_UNSELECTABLE]) return false;
	}
	return true;
}

//returns true if it won't be destroyed with an area
//in this case it shouldn't be saved with the area either
//it will be saved in the savegame
bool Actor::Persistent()
{
	if (InParty) return true;
	if (InternalFlags&IF_FROMGAME) return true;
	return false;
}

//this is a reimplementation of cheatkey a/s of bg2
//cycling through animation/stance
// a - get next animation, s - get next stance

void Actor::GetNextAnimation()
{
	int RowNum = anims->AvatarsRowNum - 1;
	if (RowNum<0)
		RowNum = CharAnimations::GetAvatarsCount() - 1;
	int NewAnimID = CharAnimations::GetAvatarStruct(RowNum)->AnimID;
	printf ("AnimID: %04X\n", NewAnimID);
	SetBase( IE_ANIMATION_ID, NewAnimID);
	//SetAnimationID ( NewAnimID );
}

void Actor::GetPrevAnimation()
{
	int RowNum = anims->AvatarsRowNum + 1;
	if (RowNum>=CharAnimations::GetAvatarsCount() )
		RowNum = 0;
	int NewAnimID = CharAnimations::GetAvatarStruct(RowNum)->AnimID;
	printf ("AnimID: %04X\n", NewAnimID);
	SetBase( IE_ANIMATION_ID, NewAnimID);
	//SetAnimationID ( NewAnimID );
}

//slot is the projectile slot
int Actor::GetRangedWeapon(ITMExtHeader *&which)
{
	unsigned int slot = inventory.FindRangedWeapon();
	CREItem *wield = inventory.GetSlotItem(slot);
	if (!wield) {
		return 0;
	}
	Item *item = core->GetItem(wield->ItemResRef);
	if (!item) {
		return 0;
	}
	which = item->GetWeaponHeader(true);
	core->FreeItem(item, wield->ItemResRef, false);
	return 0;
}

//returns weapon header currently used
//if range is nonzero, then the returned header is valid
unsigned int Actor::GetWeapon(ITMExtHeader *&which, bool leftorright)
{
	CREItem *wield = inventory.GetUsedWeapon(leftorright);
	if (!wield) {
		return 0;
	}
	Item *item = core->GetItem(wield->ItemResRef);
	if (!item) {
		return 0;
	}

	//select first weapon header
	which = item->GetWeaponHeader(false);
	//make sure we use 'false' in this freeitem
	//so 'which' won't point into invalid memory
	core->FreeItem(item, wield->ItemResRef, false);
	if (!which) {
		return 0;
	}
	if (which->Location!=ITEM_LOC_WEAPON) {
		return 0;
	}
	return which->Range+1;
}

void Actor::GetNextStance()
{
	static int Stance = IE_ANI_AWAKE;

	if (--Stance < 0) Stance = MAX_ANIMS-1;
	printf ("StanceID: %d\n", Stance);
	SetStance( Stance );
}

int Actor::LearnSpell(const ieResRef spellname, ieDword flags)
{
	if (spellbook.HaveSpell(spellname, 0) ) {
		return LSR_KNOWN;
	}
	Spell *spell = core->GetSpell(spellname);
	if (!spell) {
		return LSR_INVALID; //not existent spell
	}
	//from now on, you must delete spl if you don't push back it
	CREKnownSpell *spl = new CREKnownSpell();
	strncpy(spl->SpellResRef, spellname, 8);
	spl->Type = spell->SpellType;
	if ( spl->Type == IE_SPELL_TYPE_INNATE ) {
		spl->Level = 0;
	}
	else {
		spl->Level = (ieWord) (spell->SpellLevel-1);
	}
	bool ret=spellbook.AddKnownSpell(spl->Type, spl->Level, spl);
	if (!ret) {
		delete spl;
		return LSR_INVALID;
	}
	if (flags&LS_ADDXP) {
		AddExperience(spl->Level*100);
	}
	return LSR_OK;
}

const char *Actor::GetDialog(bool checks) const
{
	if (!checks) {
		return Dialog;
	}
	if (Modified[IE_EA]>=EA_EVILCUTOFF) {
		return NULL;
	}

	if ( (InternalFlags & IF_NOINT) && CurrentAction) {
		core->DisplayConstantString(STR_TARGETBUSY,0xff0000);
		return NULL;
	}
	return Dialog;
}

void Actor::CreateStats()
{
	if (!PCStats) {
		PCStats = new PCStatsStruct();
	}
}

const char* Actor::GetScript(int ScriptIndex) const
{
	return Scripts[ScriptIndex]->GetName();
}

void Actor::SetModal(ieDword newstate)
{
	switch(newstate) {
		case MS_NONE:
			break;
		case MS_BATTLESONG:
			break;
		case MS_DETECTTRAPS:
			break;
		case MS_STEALTH:
			break;
		case MS_TURNUNDEAD:
			break;
		default:
			return;
	}
	//come here only if success
	ModalState = newstate;
}

//this is just a stub function for now, attackstyle could be melee/ranged
//even spells got this attack style
int Actor::GetAttackStyle()
{
	return WEAPON_MELEE;
}

void Actor::SetTarget( Scriptable *target)
{
	if (target->Type==ST_ACTOR) {
		Actor *tar = (Actor *) target;
		LastTarget = tar->GetID();
		tar->LastAttacker = GetID();
		//we tell the game object that this creature
		//must be added to the list of combatants
		core->GetGame()->InAttack(tar->LastAttacker);
	}
	SetOrientation( GetOrient( target->Pos, Pos ), false );
	SetWait( 1 );
}

//in case of LastTarget = 0
void Actor::StopAttack()
{
	SetStance(IE_ANI_READY);
	core->GetGame()->OutAttack(GetID());
	InternalFlags|=IF_TARGETGONE; //this is for the trigger!
	if (InParty) {
		core->Autopause(AP_NOTARGET);
	}
}

int Actor::Immobile()
{
	if (GetStat(IE_CASTERHOLD)) {
		return 1;
	}
	if (GetStat(IE_HELD)) {
		return 1;
	}
	return 0;
}

//calculate how many attacks will be performed
//in the next round
//only called when Game thinks we are in attack
//so it is safe to do cleanup here (it will be called only once)
void Actor::InitRound(ieDword gameTime, bool secondround)
{
	attackcount = 0;

	if (InternalFlags&IF_STOPATTACK) {
		core->GetGame()->OutAttack(GetID());
		return;
	}

	if (!LastTarget) {
		StopAttack();
		return;
	}

	//if held or disabled, etc, then cannot continue attacking
	ieDword state = GetStat(IE_STATE_ID);
	if (state&STATE_CANTMOVE) {
		return;
	}
	if (Immobile()) {
		return;
	}

	SetStance(IE_ANI_ATTACK);
	//last chance to disable attacking
	//
	attackcount = GetStat(IE_NUMBEROFATTACKS);
	if (secondround) {
		attackcount++;
	}
	attackcount/=2;

	//d10
	int tmp = core->Roll(1, 10, 0);// GetStat(IE_WEAPONSPEED)-GetStat(IE_PHYSICALSPEED) );
	if (state & STATE_SLOWED) tmp <<= 1;
	if (state & STATE_HASTED) tmp >>= 1;

	if (tmp<0) tmp=0;
	else if (tmp>0x10) tmp=0x10;

	initiative = (ieDword) (gameTime+tmp);
}

int Actor::GetToHit(int bonus, ieDword Flags)
{
	int tohit = GetStat(IE_TOHIT);
	if (REVERSE_TOHIT) {
		tohit = ATTACKROLL-tohit;
	}
	tohit += bonus;

	if (Flags&WEAPON_LEFTHAND) {
		tohit += GetStat(IE_HITBONUSLEFT);
	} else {
		tohit += GetStat(IE_HITBONUSRIGHT);
	}
	//get attack style (melee or ranged)
	switch(Flags&WEAPON_STYLEMASK) {
		case WEAPON_MELEE:
			tohit += GetStat(IE_MELEEHIT);
			break;
		case WEAPON_FIST:
			tohit += GetStat(IE_FISTHIT);
			break;
		case WEAPON_RANGED:
			tohit += GetStat(IE_MISSILEHITBONUS);
			//add dexterity bonus
			break;
	}
	//add strength bonus if we need
	if (Flags&WEAPON_USESTRENGTH) {
		tohit += core->GetStrengthBonus(0,GetStat(IE_STR), GetStat(IE_STREXTRA) );
	}
	return tohit;
}

void Actor::PerformAttack(ieDword gameTime)
{
	if (!attackcount) {
		if (initiative) {
			if (InParty) {
				core->Autopause(AP_ENDROUND);
			}
			initiative = 0;
		}
		return;
	}
	if (initiative>gameTime) {
		return;
	}
	attackcount--;

	if (InternalFlags&IF_STOPATTACK) {
		core->GetGame()->OutAttack(GetID());
		return;
	}

	if (!LastTarget) {
		StopAttack();
		return;
	}
	//get target
	Actor *target = area->GetActorByGlobalID(LastTarget);

	if (target && (target->GetStat(IE_STATE_ID)&STATE_DEAD)) {
		target = NULL;
	}

	if (!target) {
		LastTarget = 0;
		return;
	}
	//which hand is used
	bool leftorright = (bool) (attackcount&1);
	ITMExtHeader *header;
	//can't reach target, zero range shouldn't be allowed
	if (GetWeapon(header,leftorright)*10<PersonalDistance(this, target)+1) {
		return;
	}
	ieDword Flags;
	ITMExtHeader *rangedheader = NULL;
	switch(header->AttackType) {
	case ITEM_AT_MELEE:
		Flags = WEAPON_MELEE;
		break;
	case ITEM_AT_PROJECTILE: //throwing weapon
		Flags = WEAPON_RANGED;
		break;
	case ITEM_AT_BOW:
		if (!GetRangedWeapon(rangedheader)) {
			//out of ammo event
			//try to refill
			SetStance(IE_ANI_READY);
			return;
		}
		SetStance(IE_ANI_READY);
		return;
	default:
		//item is unsuitable for fight
		SetStance(IE_ANI_READY);
		return;
	}//melee or ranged
	if (leftorright) Flags|=WEAPON_LEFTHAND;
	if (header->RechargeFlags&IE_ITEM_USESTRENGTH) Flags|=WEAPON_USESTRENGTH;

	//second parameter is left or right hand flag
	int tohit = GetToHit(header->THAC0Bonus, Flags);

	int roll = core->Roll(1,ATTACKROLL,0);
	if (roll==1) {
		//critical failure
		return;
	}
	//damage type is?
	//modify defense with damage type
	ieDword damagetype = header->DamageType;
	printMessage("Attack"," ",GREEN);
	int damage = core->Roll(header->DiceThrown, header->DiceSides, header->DamageBonus);
	printf("Damage %dd%d%+d = %d\n",header->DiceThrown, header->DiceSides, header->DamageBonus, damage);
	int damageluck = (int) GetStat(IE_DAMAGELUCK);
	if (damage<damageluck) {
		damage = damageluck;
	}

	if (roll>=ATTACKROLL-(int) GetStat(IE_CRITICALHITBONUS) ) {
		//critical success
		DealDamage (target, damage, damagetype, true);
		return;
	}
	tohit += roll;

	//get target's defense against attack
	int defense = target->GetStat(IE_ARMORCLASS);
	defense += core->GetDexterityBonus(STAT_DEX_AC, target->GetStat(IE_DEX) );
	if (REVERSE_TOHIT) {
		defense = DEFAULTAC - defense;
	}

	if (tohit<defense) {
		//hit failed
		return;
	}
	DealDamage (target, damage, damagetype, false);
}

static int weapon_damagetype[] = {DAMAGE_CRUSHING, DAMAGE_PIERCING,
	DAMAGE_CRUSHING, DAMAGE_SLASHING, DAMAGE_MISSILE, DAMAGE_STUNNING};

void Actor::DealDamage(Actor *target, int damage, int damagetype, bool critical)
{
	if (damage<0) damage = 0;
	if (critical) {
		//a critical surely raises the morale?
		NewBase(IE_MORALE, 1, MOD_ADDITIVE);
		int head = inventory.GetHeadSlot();
		if ((head!=-1) && target->inventory.HasItemInSlot("",(ieDword) head)) {
			//critical hit is averted by helmet
			core->DisplayConstantString(STR_NO_CRITICAL,0xffffff);
		} else {
			damage <<=1; //critical damage is always double?
			core->timer->SetScreenShake(2,2,2);
		}
	}
	ieDword tmp = target->Modified[IE_MINHITPOINTS];
	if (damagetype>5) {
		//hack for nonlethal damage (this round only)
		target->Modified[IE_MINHITPOINTS]=1;
		damagetype = 0;
	}
	target->Damage(damage, weapon_damagetype[damagetype], this);
	target->Modified[IE_MINHITPOINTS]=tmp;
}

//idx could be: 0-6, 16-22, 32-38, 48-54
//the colors are stored in 7 dwords
//maybe it would be simpler to store them in 28 bytes (without using stats?)
void Actor::SetColor( ieDword idx, ieDword grd)
{
	ieByte gradient = (ieByte) (grd&255);
	ieByte index = (ieByte) (idx&15);
	ieByte shift = (ieByte) (idx/16);
	ieDword value;

	//invalid value, would crash original IE
	if (index>6) {
		return;
	}
	if (shift == 15) {
		value = 0;
		for (index=0;index<4;index++) {
			value |= gradient<<=8;
		}
		for (index=0;index<7;index++) {
			Modified[IE_COLORS+index] = value;
		}
	} else {
		//invalid value, would crash original IE
		if (shift>3) {
			return;
		}
		shift *= 8;
		value = gradient << shift;
		value |= Modified[IE_COLORS+index] & ~(255<<shift);
		Modified[IE_COLORS+index] = value;
	}
/*
	if (anims) {
		anims->SetColors(Modified+IE_COLORS);
	}
*/
}

void Actor::SetColorMod( int location, RGBModifier::Type type, int speed,
						 unsigned char r, unsigned char g, unsigned char b,
						 int phase)
{
	CharAnimations* ca = GetAnims();
	if (!ca) return;
	if (location >= 32) return;

	if (location == -1) {
		ca->GlobalColorMod.type = type;
		ca->GlobalColorMod.speed = speed;
		ca->GlobalColorMod.rgb.r = r;
		ca->GlobalColorMod.rgb.g = g;
		ca->GlobalColorMod.rgb.b = b;
		if (phase >= 0)
			ca->GlobalColorMod.phase = phase;
		else
			ca->GlobalColorMod.phase = 0;
	} else {
		ca->ColorMods[location].type = type;
		ca->ColorMods[location].speed = speed;
		ca->ColorMods[location].rgb.r = r;
		ca->ColorMods[location].rgb.g = g;
		ca->ColorMods[location].rgb.b = b;
		// keep phase as-is, to prevent the phase being reset each AI cycle
	}
}

void Actor::SetLeader(Actor *actor, int xoffset, int yoffset)
{
	LastFollowed = actor->GetID();
	FollowOffset.x = xoffset;
	FollowOffset.y = yoffset;
}

//if days == 0, it means full healing
void Actor::Heal(int days)
{
	if (days) {
		SetBase(IE_HITPOINTS, BaseStats[IE_HITPOINTS]+days*2);
	} else {
		SetBase(IE_HITPOINTS, BaseStats[IE_MAXHITPOINTS]);
	}
}

//this function should handle dual classing and multi classing
void Actor::AddExperience(int exp)
{
	SetBase(IE_XP,BaseStats[IE_XP]+exp);
}

bool Actor::Schedule(ieDword gametime)
{
	if (!(InternalFlags&IF_VISIBLE) ) {
		return false;
	}

	//check for schedule
	ieDword bit = 1<<(gametime%7200/300);
	if (appearance & bit) {
		return true;
	}
	return false;
}

void Actor::NewPath()
{
	Point tmp = Destination;
	ClearPath();
	Movable::WalkTo(tmp, size );
}

void Actor::SetInTrap(ieDword setreset)
{
	InTrap = setreset;
	if (setreset) {
		InternalFlags |= IF_INTRAP;
	} else {
		InternalFlags &= ~IF_INTRAP;
	}
}

void Actor::SetRunFlags(ieDword flags)
{
	InternalFlags &= ~IF_RUNFLAGS;
	InternalFlags |= (flags & IF_RUNFLAGS);
}

void Actor::WalkTo(Point &Des, ieDword flags, int MinDistance)
{
	if (InternalFlags&IF_REALLYDIED) {
		return;
	}
	SetRunFlags(flags);
	// is this true???
	if (Des.x==-2 && Des.y==-2) {
		Point p((ieWord) Modified[IE_SAVEDXPOS], (ieWord) Modified[IE_SAVEDYPOS] );
		Movable::WalkTo(p, MinDistance);
	} else {
		Movable::WalkTo(Des, MinDistance);
	}
}

//there is a similar function in Map for stationary vvcs
void Actor::DrawVideocells(Region &screen, vvcVector &vvcCells, Color &tint)
{
	Map* area = GetCurrentArea();

	for (unsigned int i = 0; i < vvcCells.size(); i++) {
		ScriptedAnimation* vvc = vvcCells[i];
/* we don't allow holes anymore
		if (!vvc)
			continue;
*/

		// actually this is better be drawn by the vvc
		bool endReached = vvc->Draw(screen, Pos, tint, area, WantDither(), GetOrientation());
		if (endReached) {
			delete vvc;
			vvcCells.erase(vvcCells.begin()+i);
			continue;
		}
	}
}

void Actor::DrawActorSprite(Region &screen, int cx, int cy, Region& bbox,
							SpriteCover*& sc, Animation** anims,
							unsigned char Face, Color& tint)
{
	CharAnimations* ca = GetAnims();
	int PartCount = ca->GetTotalPartCount();
	Video* video = core->GetVideoDriver();
	Region vp = video->GetViewport();

	// display current frames in the right order
	const int* zOrder = ca->GetZOrder(Face);
	for (int part = 0; part < PartCount; ++part) {
		int partnum = part;
		if (zOrder) partnum = zOrder[part];
		Animation* anim = anims[partnum];
		Sprite2D* nextFrame = 0;
		if (anim)
			nextFrame = anim->GetFrame(anim->GetCurrentFrame());
		if (nextFrame && bbox.InsideRegion( vp ) ) {
			if (!sc || !sc->Covers(cx, cy, nextFrame->XPos, nextFrame->YPos, nextFrame->Width, nextFrame->Height)) {
				// the first anim contains the animarea for
				// the entire multi-part animation
				sc = area->BuildSpriteCover(cx, cy, -anims[0]->animArea.x, -anims[0]->animArea.y, anims[0]->animArea.w, anims[0]->animArea.h, WantDither() );
			}
			assert(sc->Covers(cx, cy, nextFrame->XPos, nextFrame->YPos, nextFrame->Width, nextFrame->Height));

			unsigned int flags = TranslucentShadows ? BLIT_TRANSSHADOW : 0;
			if (!ca->lockPalette) flags|=BLIT_TINTED;

			video->BlitGameSprite( nextFrame, cx + screen.x, cy + screen.y,
				 flags, tint, sc, ca->GetPartPalette(partnum), &screen);
		}
	}
}


int OrientdX[16] = { 0, -4, -7, -9, -10, -9, -7, -4, 0, 4, 7, 9, 10, 9, 7, 4 };
int OrientdY[16] = { 10, 9, 7, 4, 0, -4, -7, -9, -10, -9, -7, -4, 0, 4, 7, 9 };
unsigned int MirrorImageLocation[8] = { 4, 12, 8, 0, 6, 14, 10, 2 };
unsigned int MirrorImageZOrder[8] = { 2, 4, 6, 0, 1, 7, 5, 3 };

void Actor::Draw(Region &screen)
{
	Map* area = GetCurrentArea();

	int cx = Pos.x;
	int cy = Pos.y;
	int explored = Modified[IE_DONOTJUMP]&DNJ_UNHINDERED;
	//check the deactivation condition only if needed
	//this fixes dead actors disappearing from fog of war (they should be permanently visible)
	if ((!area->IsVisible( Pos, explored) || (InternalFlags&IF_REALLYDIED) ) &&	(InternalFlags&IF_ACTIVE) ) {
//    if ((!area->IsVisible( Pos, explored) || (InternalFlags&IF_JUSTDIED) ) &&	(InternalFlags&IF_ACTIVE) ) {
		//finding an excuse why we don't hybernate the actor
		if (Modified[IE_ENABLEOFFSCREENAI])
			return;
		if (LastTarget) //currently attacking someone
			return;
		if (CurrentAction)
			return;
		if (GetNextStep())
			return;
		if (GetNextAction())
			return;
		if (GetWait()) //would never stop waiting
			return;
		//turning actor inactive if there is no action next turn
		InternalFlags|=IF_IDLE;
	}

	if (InTrap) {
		area->ClearTrap(this, InTrap-1);
	}

	//visual feedback
	CharAnimations* ca = GetAnims();
	if (!ca)
		return;

	if (Modified[IE_AVATARREMOVAL]) {
		return;
	}

	//explored or visibilitymap (bird animations are visible in fog)
	//0 means opaque
	int Trans = Modified[IE_TRANSLUCENT];
	//int Trans = Modified[IE_TRANSLUCENT] * 255 / 100;
	if (Trans>255) {
		Trans=255;
	}
	int Frozen = Immobile();
	int State = Modified[IE_STATE_ID];
	if (State&STATE_STILL) {
		Frozen = 1;
	}

	//adjust invisibility for enemies
	if (Modified[IE_EA]>EA_GOODCUTOFF) {
		if (State&STATE_INVISIBLE) {
			Trans = 256;
		}
	}

	//can't move this, because there is permanent blur state where
	//there is no effect (just state bit)
	if ((State&STATE_BLUR) && Trans < 128) {
		Trans = 128;
	}
	Color tint = area->LightMap->GetPixel( cx / 16, cy / 12);
	tint.a = (ieByte) (255-Trans);

	//don't use cy for area map access beyond this point
	cy-=area->HeightMap->GetPixelIndex( cx / 16, cy / 12);

	//draw videocells under the actor
	DrawVideocells(screen, vvcShields, tint);

	Video* video = core->GetVideoDriver();
	Region vp = video->GetViewport();

	if (( !Modified[IE_NOCIRCLE] ) && ( !( State & STATE_DEAD ) )) {
		DrawCircle(vp);
		DrawTargetPoint(vp);
	}

	unsigned char StanceID = GetStance();
	unsigned char Face = GetNextFace();
	Animation** anims = ca->GetAnimation( StanceID, Face );
	if (anims) {
		// update bounding box and such
		int PartCount = ca->GetTotalPartCount();
		Sprite2D* nextFrame = 0;
		nextFrame = anims[0]->GetFrame(anims[0]->GetCurrentFrame());
		if (Frozen) {
			if (Selected!=0x80) {
				Selected = 0x80;
				core->GetGame()->SelectActor(this, false, SELECT_NORMAL);
			}
		}
		if (nextFrame && lastFrame != nextFrame) {
			Region newBBox;
			if (PartCount == 1) {
				newBBox.x = cx - nextFrame->XPos;
				newBBox.w = nextFrame->Width;
				newBBox.y = cy - nextFrame->YPos;
				newBBox.h = nextFrame->Height;
			} else {
				// FIXME: currently using the animarea instead
				// of the real bounding box of this (multi-part) frame.
				// Shouldn't matter much, though. (wjp)
				newBBox.x = cx + anims[0]->animArea.x;
				newBBox.y = cy + anims[0]->animArea.y;
				newBBox.w = anims[0]->animArea.w;
				newBBox.h = anims[0]->animArea.h;
			}
			lastFrame = nextFrame;
			SetBBox( newBBox );
		}

		// Drawing the actor:
		// * mirror images:
		//     Drawn without transparency, unless fully invisible.
		//     Order: W, E, N, S, NW, SE, NE, SW
		//     Uses extraCovers 3-10
		// * blurred copies (3 of them)
		//     Drawn with transparency.
		//     distance between copies depends on IE_MOVEMENTRATE
		//     TODO: actually, the direction is the real movement direction,
		//           not the (rounded) direction given Face
		//     Uses extraCovers 0-2
		// * actor itself
		//     Uses main spritecover


		SpriteCover *sc = 0, *newsc = 0;
		int blurx = cx;
		int blury = cy;
		int blurdx = (OrientdX[Face]*(int)Modified[IE_MOVEMENTRATE])/20;
		int blurdy = (OrientdY[Face]*(int)Modified[IE_MOVEMENTRATE])/20;
		Color mirrortint = tint;
		//mirror images are also half transparent when invis
		//if (mirrortint.a > 0) mirrortint.a = 255;

		int i;

		// mirror images behind the actor
		for (i = 0; i < 4; ++i) {
			unsigned int m = MirrorImageZOrder[i];
			if (m < Modified[IE_MIRRORIMAGES]) {
				Region sbbox = BBox;
				int dir = MirrorImageLocation[m];
				int icx = cx + 3*OrientdX[dir];
				int icy = cy + 3*OrientdY[dir];
				Point iPos(icx, icy);
				if (area->GetBlocked(iPos) & (PATH_MAP_PASSABLE|PATH_MAP_ACTOR)) {
					sbbox.x += 3*OrientdX[dir];
					sbbox.y += 3*OrientdY[dir];
					newsc = sc = extraCovers[3+m];
					DrawActorSprite(screen, icx, icy, sbbox, newsc,
									anims, Face, mirrortint);
					if (newsc != sc) {
						delete sc;
						extraCovers[3+m] = newsc;
					}
				}
			} else {
				delete extraCovers[3+m];
				extraCovers[3+m] = NULL;
			}
		}

		// blur sprites behind the actor
		if (State & STATE_BLUR) {
			if (Face < 4 || Face >= 12) {
				Region sbbox = BBox;
				sbbox.x -= 4*blurdx; sbbox.y -= 4*blurdy;
				blurx -= 4*blurdx; blury -= 4*blurdy;
				for (i = 0; i < 3; ++i) {
					sbbox.x += blurdx; sbbox.y += blurdy;
					blurx += blurdx; blury += blurdy;
					newsc = sc = extraCovers[i];
					DrawActorSprite(screen, blurx, blury, sbbox, newsc,
									anims, Face, tint);
					if (newsc != sc) {
						delete sc;
						extraCovers[i] = newsc;
					}
				}
			}
		}

		// actor itself
		newsc = sc = GetSpriteCover();
		DrawActorSprite(screen, cx, cy, BBox, newsc, anims, Face, tint);
		if (newsc != sc) SetSpriteCover(newsc);

		// blur sprites in front of the actor
		if (State & STATE_BLUR) {
			if (Face >= 4 && Face < 12) {
				Region sbbox = BBox;
				for (i = 0; i < 3; ++i) {
					sbbox.x -= blurdx; sbbox.y -= blurdy;
					blurx -= blurdx; blury -= blurdy;
					newsc = sc = extraCovers[i];
					DrawActorSprite(screen, blurx, blury, sbbox, newsc,
									anims, Face, tint);
					if (newsc != sc) {
						delete sc;
						extraCovers[i] = newsc;
					}
				}
			}
		}

		// mirror images in front of the actor
		for (i = 4; i < 8; ++i) {
			unsigned int m = MirrorImageZOrder[i];
			if (m < Modified[IE_MIRRORIMAGES]) {
				Region sbbox = BBox;
				int dir = MirrorImageLocation[m];
				int icx = cx + 3*OrientdX[dir];
				int icy = cy + 3*OrientdY[dir];
				Point iPos(icx, icy);
				if (area->GetBlocked(iPos) & (PATH_MAP_PASSABLE|PATH_MAP_ACTOR)) {
					sbbox.x += 3*OrientdX[dir];
					sbbox.y += 3*OrientdY[dir];
					newsc = sc = extraCovers[3+m];
					DrawActorSprite(screen, icx, icy, sbbox, newsc,
									anims, Face, mirrortint);
					if (newsc != sc) {
						delete sc;
						extraCovers[3+m] = newsc;
					}
				}
			} else {
				delete extraCovers[3+m];
				extraCovers[3+m] = NULL;
			}
		}

		// advance animations one frame (in sync)
		if (Frozen)
			anims[0]->LastFrame();
		else
			anims[0]->NextFrame();

		for (int part = 1; part < PartCount; ++part) {
			if (anims[part])
				anims[part]->GetSyncedNextFrame(anims[0]);
		}

		if (anims[0]->endReached) {
			if (HandleActorStance() ) {
				anims[0]->endReached = false;
			}
		}

		ca->PulseRGBModifiers();
	}

	//draw videocells over the actor
	DrawVideocells(screen, vvcOverlays, tint);

	//text feedback
	DrawOverheadText(screen);
}

/* Handling automatic stance changes */
bool Actor::HandleActorStance()
{
	CharAnimations* ca = GetAnims();
	int StanceID = GetStance();

	if (ca->autoSwitchOnEnd) {
		SetStance( ca->nextStanceID );
		ca->autoSwitchOnEnd = false;
		return true;
	}
	int x = rand()%1000;
	if ((StanceID==IE_ANI_AWAKE) && !x ) {
		SetStance( IE_ANI_HEAD_TURN );
		return true;
	}
	if ((StanceID==IE_ANI_READY) && !GetNextAction()) {
		SetStance( IE_ANI_AWAKE );
		return true;
	}
	if (StanceID == IE_ANI_ATTACK || StanceID == IE_ANI_ATTACK_JAB ||
		StanceID == IE_ANI_ATTACK_SLASH || StanceID == IE_ANI_ATTACK_BACKSLASH)
	{
		SetStance( IE_ANI_ATTACK );
		return true;
	}
	return false;
}

void Actor::ResolveStringConstant(ieResRef Sound, unsigned int index)
{
	TableMgr * tab;

	//resolving soundset (bg1/bg2 style)
	if (PCStats && PCStats->SoundSet[0]&& csound[index]) {
		snprintf(Sound, sizeof(ieResRef), "%s%c", PCStats->SoundSet, csound[index]);
		return;
	}

	Sound[0]=0;
	int table=core->LoadTable( anims->ResRef );

	if (table<0) {
		return;
	}
	tab = core->GetTable( table );
	if (!tab) {
		goto end;
	}

	switch (index) {
		case VB_ATTACK:
			index = 0;
			break;
		case VB_DAMAGE:
			index = 8;
			break;
		case VB_DIE:
			index = 10;
			break;
		case VB_SELECT:
			index = 36+rand()%4;
			break;
	}
	strnlwrcpy(Sound, tab->QueryField (index, 0), 8);
end:
	core->DelTable( table );

}

void Actor::SetActionButtonRow(ActionButtonRow &ar)
{
	for(int i=0;i<MAX_QSLOTS;i++) {
		ieByte tmp = ar[i+3];
		if (QslotTranslation) {
			tmp=gemrb2iwd[tmp];
		}
		PCStats->QSlots[i]=tmp;
	}
}

//the first 3 buttons are untouched by this function
void Actor::GetActionButtonRow(ActionButtonRow &ar)
{
	if (PCStats->QSlots[0]==0xff) {
		InitButtons(GetStat(IE_CLASS));
	}
	for(int i=0;i<GUIBT_COUNT-3;i++) {
		ieByte tmp=PCStats->QSlots[i];
		if (QslotTranslation) {
			if (tmp>=90) { //quick weapons
				tmp=16+tmp%10;
			} else if (tmp>=80) { //quick items
				tmp=9+tmp%10;
			} else if (tmp>=70) { //quick spells
				tmp=3+tmp%10;
			} else {
				tmp=iwd2gemrb[tmp];
			}
		}
		ar[i+3]=tmp;
	}
	memcpy(ar,DefaultButtons,3*sizeof(ieByte) );
}

void Actor::SetSoundFolder(const char *soundset)
{
	if (core->HasFeature(GF_SOUNDFOLDERS)) {
		char filepath[_MAX_PATH];

		strnlwrcpy(PCStats->SoundFolder, soundset, 32);
		PathJoin(filepath,core->GamePath,"sounds",PCStats->SoundFolder,0);
		char *fp = FindInDir(filepath, "?????01", true);
		if (fp) {
			fp[5] = 0;
		} else {
			fp = FindInDir(filepath, "????01", true);
			if (fp) {
				fp[4] = 0;
			}
		}
		if (fp) {
			strnlwrcpy(PCStats->SoundSet, fp, 6);
			free(fp);
		}
	} else {
		strnlwrcpy(PCStats->SoundSet, soundset, 6);
		PCStats->SoundFolder[0]=0;
	}
}

bool Actor::HasVVCCell(const ieResRef resource)
{
	int j = true;
	vvcVector *vvcCells=&vvcShields;
retry:
	size_t i=vvcCells->size();
	while (i--) {
		ScriptedAnimation *vvc = (*vvcCells)[i];
		if (vvc == NULL) {
			continue;
		}
		if ( strnicmp(vvc->ResName, resource, 8) == 0) {
			return true;
		}
	}
	vvcCells=&vvcOverlays;
	if (j) { j = false; goto retry; }
	return false;
}

void Actor::RemoveVVCell(const ieResRef resource, bool graceful)
{
	bool j = true;
	vvcVector *vvcCells=&vvcShields;
retry:
	size_t i=vvcCells->size();
	while (i--) {
		ScriptedAnimation *vvc = (*vvcCells)[i];
		if (vvc == NULL) {
			continue;
		}
		if ( strnicmp(vvc->ResName, resource, 8) == 0) {
			if (graceful) {
				vvc->SetPhase(P_RELEASE);
			} else {
				delete vvc;
				vvcCells->erase(vvcCells->begin()+i);
			}
		}
	}
	vvcCells=&vvcOverlays;
	if (j) { j = false; goto retry; }
}

void Actor::AddVVCell(ScriptedAnimation* vvc)
{
	vvcVector *vvcCells;

	//if the vvc was not created, don't try to add it
	if (!vvc) {
		return;
	}
	if (vvc->ZPos<0) {
		vvcCells=&vvcShields;
	} else {
		vvcCells=&vvcOverlays;
	}
	size_t i=vvcCells->size();
	while (i--) {
		if ((*vvcCells)[i] == NULL) {
			(*vvcCells)[i] = vvc;
			return;
		}
	}
	vvcCells->push_back( vvc );
}

//returns restored spell level
int Actor::RestoreSpellLevel(ieDword maxlevel, ieDword type)
{
	int typemask;

	switch (type) {
		case 0: //allow only mage
			typemask = ~1;
			break;
		case 1: //allow only cleric
			typemask = ~2;
			break;
		default:
			typemask = 0;
	}
	for (int i=maxlevel;i>0;i--) {
		CREMemorizedSpell *cms = spellbook.FindUnchargedSpell(typemask, maxlevel);
		if (cms) {
			spellbook.ChargeSpell(cms);
			return i;
		}
	}
	return 0;
}

//replenishes spells, cures fatigue
void Actor::Rest(int hours)
{
	if (hours) {
		//do remove effects
		int remaining = hours*10;
		//removes hours*10 fatigue points
		NewStat (IE_FATIGUE, -remaining, MOD_ADDITIVE);
		NewStat (IE_INTOXICATION, -remaining, MOD_ADDITIVE);
		//restore hours*10 spell levels
		//rememorization starts with the lower spell levels?
		for (int level = 1; level<16; level++) {
			if (level<remaining) {
				break;
			}
			while (remaining>0) {
				remaining -= RestoreSpellLevel(level,0);
			}
		}
	} else {
		SetBase (IE_FATIGUE, 0);
		SetBase (IE_INTOXICATION, 0);
		spellbook.ChargeAllSpells ();
	}
}

//returns the actual slot from the quickslot
int Actor::GetQuickSlot(int slot)
{
	assert(slot<8);
	if (inventory.HasItemInSlot("",inventory.GetMagicSlot())) {
		return inventory.GetMagicSlot();
	}
	if (!PCStats) {
		return slot+inventory.GetWeaponSlot();
	}
	return PCStats->QuickWeaponSlots[slot];
}

//marks the quickslot as equipped
int Actor::SetEquippedQuickSlot(int slot)
{
	//creatures and such
	if (!PCStats) {
		if (inventory.SetEquippedSlot(slot)) {
			return 0;
		}
		return STR_MAGICWEAPON;
	}

	//player characters
	if (inventory.SetEquippedSlot(PCStats->QuickWeaponSlots[slot]-inventory.GetWeaponSlot())) {
		return 0;
	}
	return STR_MAGICWEAPON;
}

//if target is a non living scriptable, then we simply shoot for its position
//the fx should get a NULL target, and handle itself by using the position
//(shouldn't crash when target is NULL)
bool Actor::UseItemPoint(int slot, ieDword header, Point &target, bool silent)
{
	CREItem *item = inventory.GetSlotItem(slot);
	if (!item)
		return false;
	Item *itm = core->GetItem(item->ItemResRef);
	if (!itm) return false; //quick item slot contains invalid item resref
	if ((header<CHARGE_COUNTERS) && !item->Usages[header]) {
		return false;
	}

	Projectile *pro = itm->GetProjectile(header);
	pro->SetCaster(globalID);
	GetCurrentArea()->AddProjectile(pro, Pos, target);
	//in fact this should build a projectile and hurl it at the target
	//this is just a temporary solution
	/*
	EffectQueue *fx = itm->GetEffectBlock(header);
	if (!fx)
		return false;
	Actor *tar=GetCurrentArea()->GetActor(target, 10);
	if (tar) {
		fx->SetOwner(this);
		fx->AddAllEffects(tar);
	}
	*/
	//
	ChargeItem(slot, header, item, itm, silent);
	core->FreeItem(itm,item->ItemResRef, false);
	return true;
}

bool Actor::UseItem(int slot, ieDword header, Scriptable* target, bool silent)
{
	if (target->Type!=ST_ACTOR) {
		return UseItemPoint(slot, header, target->Pos, silent);
	}

	Actor *tar = (Actor *) target;
	CREItem *item = inventory.GetSlotItem(slot);
	if (!item)
		return false;
	Item *itm = core->GetItem(item->ItemResRef);
	if (!itm) return false; //quick item slot contains invalid item resref
	if ((header<CHARGE_COUNTERS) && !item->Usages[header]) {
		return false;
	}
	Projectile *pro = itm->GetProjectile(header);
	if (pro) {
		pro->SetCaster(globalID);
		GetCurrentArea()->AddProjectile(pro, Pos, tar->globalID);
	}
	//in fact this should build a projectile and hurl it at the target
	//this is just a temporary solution
	/*
	EffectQueue *fx = itm->GetEffectBlock(header);
	if (!fx)
		return false;
	fx->SetOwner(this);
	fx->AddAllEffects(tar);
	*/
	//
	ChargeItem(slot, header, item, itm, silent);
	core->FreeItem(itm,item->ItemResRef, false);
	return true;
}

void Actor::ChargeItem(int slot, ieDword header, CREItem *item, Item *itm, bool silent)
{
	if (!itm) {
		item = inventory.GetSlotItem(slot);
		if (!item)
			return;
		itm = core->GetItem(item->ItemResRef);
	}
	if (!itm) return; //quick item slot contains invalid item resref

	switch(itm->UseCharge(item->Usages, header)) {
		case CHG_NOSOUND: //remove item
			inventory.BreakItemSlot(slot);
			break;
		case CHG_BREAK: //both
			if (!silent) {
				core->PlaySound(DS_ITEM_GONE);
			}
			inventory.BreakItemSlot(slot);
			break;
		default: //don't do anything
			break;
	}
}

bool Actor::IsReverseToHit()
{
	return REVERSE_TOHIT;
}

void Actor::InitButtons(ieDword cls)
{
	if (!PCStats) {
		return;
	}
	ActionButtonRow myrow;
	if ((int) cls >= classcount) {
		memcpy(&myrow, &DefaultButtons, sizeof(ActionButtonRow));
	} else {
		memcpy(&myrow, GUIBTDefaults+cls, sizeof(ActionButtonRow));
	}
	SetActionButtonRow(myrow);
}

void Actor::SetFeat(unsigned int feat, int mode)
{
	if (feat>3*sizeof(ieDword)) {
		return;
	}
	ieDword mask = 1<<(feat&31);
	ieDword idx = feat>>5;
	switch (mode) {
		case BM_SET: case BM_OR:
			BaseStats[IE_FEATS1+idx]|=mask;
			break;
		case BM_NAND:
			BaseStats[IE_FEATS1+idx]&=~mask;
			break;
		case BM_XOR:
			BaseStats[IE_FEATS1+idx]^=mask;
			break;
	}
}

int Actor::GetFeat(unsigned int feat)
{
	if (feat>3*sizeof(ieDword)) {
		return -1;
	}
	if (Modified[IE_FEATS1+(feat>>5)]&(1<<(feat&31)) ) {
		return 1;
	}
	return 0;
}

void Actor::SetUsedWeapon(char* AnimationType, ieWord* MeleeAnimation, int wt)
{
	memcpy(WeaponRef, AnimationType, sizeof(WeaponRef) );
	if (wt != -1) WeaponType = wt;
	if (!anims)
		return;
	anims->SetWeaponRef(AnimationType);
	anims->SetWeaponType(WeaponType);
	SetAttackMoveChances(MeleeAnimation);
}

void Actor::SetUsedShield(char* AnimationType, int wt)
{
	memcpy(ShieldRef, AnimationType, sizeof(ShieldRef) );
	if (wt != -1) WeaponType = wt;
	if (AnimationType[0] == ' ' || AnimationType[0] == 0)
		if (WeaponType == IE_ANI_WEAPON_2W)
			WeaponType = IE_ANI_WEAPON_1H;

	if (!anims)
		return;
	anims->SetOffhandRef(AnimationType);
	anims->SetWeaponType(WeaponType);
}

void Actor::SetUsedHelmet(char* AnimationType)
{
	memcpy(HelmetRef, AnimationType, sizeof(HelmetRef) );
	if (!anims)
		return;
	anims->SetHelmetRef(AnimationType);
}

//set up stuff here, like attack number, turn undead level
//and similar derived stats that change with level
void Actor::SetupFist()
{
	int slot = core->QuerySlot( 0 );
	assert (core->QuerySlotEffects(slot)==SLOT_EFFECT_FIST);
	int row = GetBase(fiststat);
	int col = GetXPLevel(false);

	if (FistRows<0) {
		FistRows=0;
		int table = core->LoadTable( "fistweap" );
		TableMgr *fist = core->GetTable( table );
		if (fist) {
			//default value
			strnlwrcpy( DefaultFist, fist->QueryField( (unsigned int) -1), 8);
			FistRows = fist->GetRowCount();
			fistres = new FistResType[FistRows];
			for (int i=0;i<FistRows;i++) {
				int maxcol = fist->GetColumnCount(i)-1;
				for (int cols = 0;cols<MAX_LEVEL;cols++) {
					strnlwrcpy( fistres[i][cols], fist->QueryField( i, cols>maxcol?maxcol:cols ), 8);
				}
				*(int *) fistres[i] = atoi(fist->GetRowName( i));
			}
		}
		core->DelTable( table );
	}
	if (col>MAX_LEVEL) col=MAX_LEVEL;
	if (col<1) col=1;

	const char *ItemResRef = DefaultFist;
	for (int i = 0;i<FistRows;i++) {
		if (*(int *) fistres[i] == row) {
			ItemResRef = fistres[i][col];
		}
	}
	inventory.SetSlotItemRes(ItemResRef, slot);
}

static ieDword ResolveTableValue(const char *resref, ieDword stat, ieDword mcol, ieDword vcol) {
	long ret = 0;
	//don't close this table, it can mess with the guiscripts
	int table = core->LoadTable(resref);
	TableMgr *tm = core->GetTable(table);
	if (tm) {
		unsigned int row;
		if (mcol == 0xff) {
			row = stat;
		} else {
			row = tm->FindTableValue(mcol, stat);
			if (row==0xffffffff) {
				return 0;
			}
		}
		if (valid_number(tm->QueryField(row, vcol), ret)) {
			return (ieDword) ret;
		}
	}

	return 0;
}

//checks usability only
int Actor::Unusable(Item *item) const
{
	if (GetStat(IE_CANUSEANYITEM)) {
		return 0;
	}

	ieDword itembits[2]={item->UsabilityBitmask, item->KitUsability};

	for (int i=0;i<usecount;i++) {
		ieDword itemvalue = itembits[itemuse[i].which];
		ieDword stat = ResolveTableValue(itemuse[i].table, GetStat(itemuse[i].stat), itemuse[i].mcol, itemuse[i].vcol);
		if (stat&itemvalue) {
			return 1;
		}
	}

	if (!CHECK_ABILITIES) {
		return 0;
	}

	if (item->MinLevel>GetXPLevel(true)) {
		return 1;
	}

	if (item->MinStrength>GetStat(IE_STR)) {
		return 1;
	}
	if (item->MinStrength==18) {
		if (item->MinStrengthBonus>GetStat(IE_STREXTRA)) {
			return 1;
		}
	}

	if (item->MinIntelligence>GetStat(IE_INT)) {
		return 1;
	}
	if (item->MinDexterity>GetStat(IE_DEX)) {
		return 1;
	}
	if (item->MinWisdom>GetStat(IE_WIS)) {
		return 1;
	}
	if (item->MinConstitution>GetStat(IE_CON)) {
		return 1;
	}
	if (item->MinCharisma>GetStat(IE_CHR)) {
		return 1;
	}
	//note, weapon proficiencies shouldn't be checked here
	//missing proficiency causes only attack penalty
	return 0;
}
