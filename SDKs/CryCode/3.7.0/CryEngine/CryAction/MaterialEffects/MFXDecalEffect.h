////////////////////////////////////////////////////////////////////////////
//
//  Crytek Engine Source File.
//  Copyright (C), Crytek Studios, 2006.
// -------------------------------------------------------------------------
//  File name:   IMFXEffect.h
//  Version:     v1.00
//  Created:     28/11/2006 by JohnN/AlexL
//  Compilers:   Visual Studio.NET
//  Description: Decal effect
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////
#ifndef __MFXDECALEFFECT_H__
#define __MFXDECALEFFECT_H__

#pragma once

#include "MFXEffectBase.h"

struct SMFXDecalParams 
{
	string material;
	float  minscale;
	float  maxscale;
	float  rotation;
	float  lifetime;
	float  growTime;
	bool   assemble;
	bool   forceedge;
};

class CMFXDecalEffect :
	public CMFXEffectBase
{
public:
	CMFXDecalEffect();
	virtual ~CMFXDecalEffect();

	// IMFXEffect
	virtual void Execute(const SMFXRunTimeEffectParams& params) OVERRIDE;
	virtual void LoadParamsFromXml(const XmlNodeRef& paramsNode) OVERRIDE;
	virtual void GetResources(SMFXResourceList& resourceList) const OVERRIDE;
	virtual void PreLoadAssets() OVERRIDE;
	virtual void ReleasePreLoadAssets() OVERRIDE;
	virtual void GetMemoryUsage( ICrySizer *pSizer ) const OVERRIDE;
	//~IMFXEffect

private:

	void ReleaseMaterial();

	SMFXDecalParams       m_decalParams;
	_smart_ptr<IMaterial> m_material;
};

#endif
