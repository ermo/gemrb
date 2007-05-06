/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Id$
 *
 */

#include "../../includes/win32def.h"
#include "../Core/Interface.h"
#include "../Core/AnimationMgr.h"
#include "../Core/EffectMgr.h"
#include "ITMImp.h"

ITMImp::ITMImp(void)
{
	str = NULL;
	autoFree = false;
}

ITMImp::~ITMImp(void)
{
	if (str && autoFree) {
		delete( str );
	}
	str = NULL;
}

bool ITMImp::Open(DataStream* stream, bool autoFree)
{
	if (stream == NULL) {
		return false;
	}
	if (str && this->autoFree) {
		delete( str );
	}
	str = stream;
	this->autoFree = autoFree;
	char Signature[8];
	str->Read( Signature, 8 );
	if (strncmp( Signature, "ITM V1  ", 8 ) == 0) {
		version = 10;
	} else if (strncmp( Signature, "ITM V1.1", 8 ) == 0) {
		version = 11;
	} else if (strncmp( Signature, "ITM V2.0", 8 ) == 0) {
		version = 20;
	} else {
		printf( "[ITMImporter]: This file is not a valid ITM File\n" );
		return false;
	}

	return true;
}

Item* ITMImp::GetItem(Item *s)
{
	unsigned int i;
	ieByte k1,k2,k3,k4;

	if( !s) {
		return NULL;
	}
	str->ReadDword( &s->ItemName );
	str->ReadDword( &s->ItemNameIdentified );
	str->ReadResRef( s->ReplacementItem );
	str->ReadDword( &s->Flags );
	str->ReadWord( &s->ItemType );
	str->ReadDword( &s->UsabilityBitmask );
	str->Read( s->AnimationType,2 ); //intentionally not reading word!
	for (i=0;i<2;i++) {
		if (s->AnimationType[i]==' ') {
			s->AnimationType[i]=0;
		}
	}
	str->ReadWord( &s->MinLevel );
	str->Read( &s->MinStrength,1 );
	str->Read( &s->unknown2,1 );
	str->Read( &s->MinStrengthBonus,1 );
	str->Read( &k1,1 );
	str->Read( &s->MinIntelligence,1 );
	str->Read( &k2,1 );
	str->Read( &s->MinDexterity,1 );
	str->Read( &k3,1 );
	str->Read( &s->MinWisdom,1 );
	str->Read( &k4,1 );
	s->KitUsability=(k1<<24) | (k2<<16) | (k3<<8) | k4; //bg2/iwd2 specific
	str->Read( &s->MinConstitution,1 );
	str->Read( &s->WeaProf,1 ); //bg2 specific
	str->Read( &s->MinCharisma,1 );
	str->Read( &s->unknown3,1 );
	str->ReadDword( &s->Price );
	str->ReadWord( &s->StackAmount );
	str->ReadResRef( s->ItemIcon );
	str->ReadWord( &s->LoreToID );
	str->ReadResRef( s->GroundIcon );
	str->ReadDword( &s->Weight );
	str->ReadDword( &s->ItemDesc );
	str->ReadDword( &s->ItemDescIdentified );
	str->ReadResRef( s->DescriptionIcon );
	str->ReadDword( &s->Enchantment );
	str->ReadDword( &s->ExtHeaderOffset );
	str->ReadWord( &s->ExtHeaderCount );
	str->ReadDword( &s->FeatureBlockOffset );
	str->ReadWord( &s->EquippingFeatureOffset );
	str->ReadWord( &s->EquippingFeatureCount );

	s->Dialog[0] = 0;
	s->DialogName = 0;
	s->WieldColor = 0xffff;
	memset( s->unknown, 0, 26 );

	//skipping header data for iwd2
	if (version == ITM_VER_IWD2) {
		str->Read( s->unknown, 16 );
	}
	//pst data
	if (version == ITM_VER_PST) {
		str->ReadResRef( s->Dialog );
		str->ReadDword( &s->DialogName );
		ieWord WieldColor;
		str->ReadWord( &WieldColor );
		if (s->AnimationType[0]) {
			s->WieldColor = WieldColor;
		}
		str->Read( s->unknown, 26 );
	} else {
	//all non pst
		s->DialogName = core->GetItemDialStr(s->Name);
		core->GetItemDialRes(s->Name, s->Dialog);
	}
	s->ItemExcl=core->GetItemExcl(s->Name);

	s->ext_headers = core->GetITMExt( s->ExtHeaderCount );

	for (i = 0; i < s->ExtHeaderCount; i++) {
		str->Seek( s->ExtHeaderOffset + i * 56, GEM_STREAM_START );
		GetExtHeader( s, s->ext_headers + i );
	}

	//48 is the size of the feature block
	s->equipping_features = core->GetFeatures( s->EquippingFeatureCount);

	str->Seek( s->FeatureBlockOffset + 48*s->EquippingFeatureOffset,
			GEM_STREAM_START );
	for (i = 0; i < s->EquippingFeatureCount; i++) {
		GetFeature(s->equipping_features+i);
	}


	if (!core->IsAvailable( IE_BAM_CLASS_ID )) {
		printf( "[ITMImporter]: No BAM Importer Available.\n" );
		return NULL;
	}
	return s;
}

void ITMImp::GetExtHeader(Item *s, ITMExtHeader* eh)
{
	ieByte tmpByte;

	str->Read( &eh->AttackType,1 );
	str->Read( &eh->IDReq,1 );
	str->Read( &eh->Location,1 );
	str->Read( &eh->unknown1,1 );
	str->ReadResRef( eh->UseIcon );
	str->Read( &eh->Target,1 );
	str->Read( &tmpByte,1 );
	if (!tmpByte) {
		tmpByte = 1;
	}
	eh->TargetNumber = tmpByte;
	str->ReadWord( &eh->Range );
	str->ReadWord( &eh->ProjectileType );
	str->ReadWord( &eh->Speed );
	str->ReadWord( &eh->THAC0Bonus );
	str->ReadWord( &eh->DiceSides );
	str->ReadWord( &eh->DiceThrown );
	//if your compiler doesn't like this, then we need a ReadWordSigned
	str->ReadWord( (ieWord *) &eh->DamageBonus );
	str->ReadWord( &eh->DamageType );
	str->ReadWord( &eh->FeatureCount );
	str->ReadWord( &eh->FeatureOffset );
	str->ReadWord( &eh->Charges );
	str->ReadWord( &eh->ChargeDepletion );
	str->ReadDword( &eh->RechargeFlags );
	str->ReadWord( &eh->ProjectileAnimation );
	//for some odd reasons 0 and 1 are the same
	if (eh->ProjectileAnimation) {
		eh->ProjectileAnimation--;
	}

	unsigned int i; //msvc6.0 can't cope with index variable scope

	for (i = 0; i < 3; i++) {
		str->ReadWord( &eh->MeleeAnimation[i] );
	}
	ieWord tmp;

	i = 0;
	str->ReadWord( &tmp ); //arrow
	if (tmp) i|=PROJ_ARROW;
	str->ReadWord( &tmp ); //xbow
	if (tmp) i|=PROJ_BOLT;
	str->ReadWord( &tmp ); //bullet
	if (tmp) i|=PROJ_BULLET;
	//this hack is required for Nordom's crossbow in PST
	if (!i && (eh->AttackType==ITEM_AT_BOW || eh->AttackType==ITEM_AT_PROJECTILE) ) {
		i|=PROJ_BOLT;
	}
	eh->ProjectileQualifier=i;

	//48 is the size of the feature block
	eh->features = core->GetFeatures(eh->FeatureCount);
	str->Seek( s->FeatureBlockOffset + 48*eh->FeatureOffset, GEM_STREAM_START );
	for (i = 0; i < eh->FeatureCount; i++) {
		GetFeature(eh->features+i);
	}
}

void ITMImp::GetFeature(Effect *fx)
{
	EffectMgr* eM = ( EffectMgr* ) core->GetInterface( IE_EFF_CLASS_ID );
	eM->Open( str, false );
	eM->GetEffect( fx );
	core->FreeInterface( eM );
}
