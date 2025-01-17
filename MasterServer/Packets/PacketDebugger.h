/*************************************************************************
Copyright (C), chernecoff@gmail.com, 2014-2015
--------------------------------------------------------------------------
History:

- 17.08.2014   22:50 : Created by AfroStalin(chernecoff)
- 07.03.2015   02:43 : Edited by AfroStalin(chernecoff)
-------------------------------------------------------------------------

*************************************************************************/

#ifndef _PacketDebugger_
#define _PacketDebugger_

namespace PacketDebugger
{
	char* GetHexFromByte(char c);

	void Debug(char* buffer, int length, char* filename);
};

#endif