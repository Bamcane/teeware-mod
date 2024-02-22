/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_MESSAGE_H
#define ENGINE_MESSAGE_H

#include <engine/shared/packer.h>

class CMsgPacker : public CPacker
{
public:
	int m_MsgID;
	bool m_System;
	bool m_NoTranslate;
	CMsgPacker(int Type, bool System = false, bool NoTranslate = false) :
		m_MsgID(Type), m_System(System), m_NoTranslate(NoTranslate)
	{
		Reset();
		if(Type < 0 || Type > 0x3FFFFFFF)
		{
			return;
		}
		
		AddInt((Type << 1) | (System ? 1 : 0));
	}
};

#endif
