// CryEngine Source File.
// Copyright (C), Crytek, 1999-2014.


#include "StdAfx.h"
#include "LevelSystem.h"
#include "Level.h"
#include "ActorSystem.h"
#include "IMusicSystem.h"
#include "IMovieSystem.h"
#include "CryAction.h"
#include "IGameTokens.h"
#include "IDialogSystem.h"
#include "TimeOfDayScheduler.h"
#include "IGameRulesSystem.h"
#include "IMaterialEffects.h"
#include "IPlayerProfiles.h"
#include "ILiveCreateHost.h"
#include <IResourceManager.h>
#include <ILocalizationManager.h>
#include "Network/GameContext.h"
#include "ICooperativeAnimationManager.h"
#include "IDeferredCollisionEvent.h"
#include "IPlatformOS.h"
#include <ICustomActions.h>
#include "CustomEvents/CustomEventsManager.h"

#include <IGameVolumes.h>

#include "LoadSeq.h"

#ifdef WIN32
#include <windows.h>
#endif





#define LOCAL_WARNING(cond, msg)  do { if (!(cond)) { CryWarning(VALIDATOR_MODULE_GAME, VALIDATOR_WARNING, msg); } } while(0)
//#define LOCAL_WARNING(cond, msg)  CRY_ASSERT_MESSAGE(cond, msg)

int CLevelSystem::s_loadCount = 0;

//------------------------------------------------------------------------
CLevelRotation::CLevelRotation() :
  m_randFlags(ePRF_None),
	m_next(0),
	m_extInfoId(0),
	m_advancesTaken(0),
	m_hasGameModesDecks(false)
{
}

//------------------------------------------------------------------------
CLevelRotation::~CLevelRotation()
{
}

//------------------------------------------------------------------------
bool CLevelRotation::Load(ILevelRotationFile* file)
{
	return LoadFromXmlRootNode(file->Load(), NULL);
}

//------------------------------------------------------------------------
bool CLevelRotation::LoadFromXmlRootNode(const XmlNodeRef rootNode, const char* altRootTag)
{
	if (rootNode)
	{
		if (!stricmp(rootNode->getTag(), "levelrotation") || (altRootTag && !stricmp(rootNode->getTag(), altRootTag)))
		{
			Reset();

			TRandomisationFlags randFlags = ePRF_None;
			
			bool r = false;
			rootNode->getAttr("randomize",r);
			if( r )
			{
				randFlags |= ePRF_Shuffle;
			}

			bool pairs = false;
			rootNode->getAttr("maintainPairs", pairs);
			if( pairs )
			{
				randFlags |= ePRF_MaintainPairs;
			}

			SetRandomisationFlags( randFlags );

			bool includeNonPresentLevels = false;
			rootNode->getAttr("includeNonPresentLevels", includeNonPresentLevels);

			int n = rootNode->getChildCount();

			XmlNodeRef lastLevelNode;

			for( int i = 0; i < n; ++i )
			{
				XmlNodeRef child = rootNode->getChild(i);
				if (child && !stricmp(child->getTag(), "level"))
				{
					const char *levelName=child->getAttr("name");
					if (!levelName || !levelName[0])
					{
						continue;
					}

					if (!includeNonPresentLevels)
					{
						if (!CCryAction::GetCryAction()->GetILevelSystem()->GetLevelInfo(levelName)) //skipping non-present levels
						{
							continue;
						}
					}

					//capture this good level child node in case it is last
					lastLevelNode = child;

					AddLevelFromNode( levelName, child );
				}
			}

			//if we're a maintain pairs playlist, need an even number of entries
			//also if there's only one entry, we should increase to 2 for safety
			int nAdded = m_rotation.size();
			if( nAdded == 1 || (pairs && ( nAdded % 2) == 1) )
			{
				//so duplicate last
				const char *levelName = lastLevelNode->getAttr( "name" );	//we know there was at least 1 level node if we have an odd number
				AddLevelFromNode( levelName, lastLevelNode );
			}
			return true;
		}
	}

	return false;
}

//------------------------------------------------------------------------
void CLevelRotation::AddLevelFromNode( const char* levelName, XmlNodeRef& levelNode )
{
	int lvl = AddLevel( levelName );

	SLevelRotationEntry& level = m_rotation[ lvl ];

	const char *gameRulesName = levelNode->getAttr( "gamerules" );
	if( gameRulesName && gameRulesName[0] )
	{
		AddGameMode( level, gameRulesName );
	}
	else
	{
		//look for child Game Rules nodes
		int nModeNodes = levelNode->getChildCount();

		for( int iNode = 0; iNode < nModeNodes; ++iNode )
		{
			XmlNodeRef modeNode = levelNode->getChild( iNode );

			if( ! stricmp( modeNode->getTag(), "gameRules" ) )
			{
				gameRulesName = modeNode->getAttr( "name" );

				if( gameRulesName && gameRulesName[0] )
				{
					//found some
					AddGameMode( level, gameRulesName );
				}
			}
		}
	}
}

//------------------------------------------------------------------------
void CLevelRotation::Reset()
{
  m_randFlags = ePRF_None;
	m_next = 0;
	m_rotation.resize(0);
  m_shuffle.resize(0);
	m_extInfoId = 0;
	m_hasGameModesDecks = false;
}

//------------------------------------------------------------------------
int CLevelRotation::AddLevel(const char *level)
{
	SLevelRotationEntry entry;
	entry.levelName=level;
	entry.currentModeIndex = 0;

  int idx = m_rotation.size();
	m_rotation.push_back(entry);
	m_shuffle.push_back(idx);
  return idx;
}

//------------------------------------------------------------------------
int CLevelRotation::AddLevel( const char *level, const char* gameMode )
{
	int iLevel = AddLevel( level );
	AddGameMode( iLevel, gameMode );
	return iLevel;
}

//------------------------------------------------------------------------
void CLevelRotation::AddGameMode(int level, const char* gameMode)
{
	CRY_ASSERT(level>=0 && level<m_rotation.size());
	AddGameMode( m_rotation[level], gameMode );
}

//------------------------------------------------------------------------
void CLevelRotation::AddGameMode(SLevelRotationEntry& level, const char* gameMode)
{
	const char *pActualGameModeName = CCryAction::GetCryAction()->GetIGameRulesSystem()->GetGameRulesName(gameMode);
	if (pActualGameModeName)
	{
		int idx = level.gameRulesNames.size();
		level.gameRulesNames.push_back(pActualGameModeName);
		level.gameRulesShuffle.push_back( idx );

		if( level.gameRulesNames.size() > 1 )
		{
			//more than one game mode per level is a deck of game modes
			m_hasGameModesDecks = true;
		}
	}
}

//------------------------------------------------------------------------
bool CLevelRotation::First()
{
	Log_LevelRotation( "First called" );
	m_next = 0;
	return !m_rotation.empty();
}

//------------------------------------------------------------------------
bool CLevelRotation::Advance()
{
	Log_LevelRotation( "::Advance()" );
	++m_advancesTaken;

	if( m_next >= 0 && m_next < m_rotation.size() )
	{
		SLevelRotationEntry& currLevel = m_rotation[ (m_randFlags&ePRF_Shuffle) ? m_shuffle[m_next] : m_next ];

		int nModes = currLevel.gameRulesNames.size();
		
		if( nModes > 1 )
		{
			//also advance game mode for level
			currLevel.currentModeIndex++;

			Log_LevelRotation( " advanced level entry %d currentModeIndex to %d", m_next, currLevel.currentModeIndex );

			if( currLevel.currentModeIndex >= nModes )
			{
				Log_LevelRotation( " looped modes" );
				currLevel.currentModeIndex = 0;
			}
		}
	}

	++m_next;

	Log_LevelRotation( "CLevelRotation::Advance advancesTaken = %d, next = %d", m_advancesTaken, m_next );

  if (m_next>=m_rotation.size())
		return false;
	return true;
}


//------------------------------------------------------------------------
bool CLevelRotation::AdvanceAndLoopIfNeeded()
{
	bool looped = false;
	if( !Advance() )
	{
		Log_LevelRotation( "AdvanceAndLoopIfNeeded Looping" );
		looped = true;
		First();

		if( m_randFlags & ePRF_Shuffle )
		{
			ShallowShuffle();
		}
	}

	return looped;
}

//------------------------------------------------------------------------
const char *CLevelRotation::GetNextLevel() const
{
  if (m_next>=0 && m_next<m_rotation.size())
  {
    int next = (m_randFlags&ePRF_Shuffle) ? m_shuffle[m_next] : m_next;
		return m_rotation[next].levelName.c_str();
  }
	return 0;
}

//------------------------------------------------------------------------
const char *CLevelRotation::GetNextGameRules() const
{
	if (m_next>=0 && m_next<m_rotation.size())
	{
		int next = (m_randFlags&ePRF_Shuffle) ? m_shuffle[m_next] : m_next;

		const SLevelRotationEntry& nextLevel = m_rotation[next];

		Log_LevelRotation( "::GetNextGameRules() accessing level %d mode %d (shuffled %d)", next, nextLevel.currentModeIndex, nextLevel.gameRulesShuffle[ nextLevel.currentModeIndex ] );
		return nextLevel.GameModeName();
	}
	return 0;
}

//------------------------------------------------------------------------
const char*	CLevelRotation::GetLevel( uint32 idx, bool accessShuffled /*=true*/ ) const
{
	int realIndex = GetRealRotationIndex( idx, accessShuffled );

	if( realIndex >= 0 )
	{
		return m_rotation[ realIndex ].levelName.c_str();
	}
	
	return 0;
}

//------------------------------------------------------------------------
int CLevelRotation::GetNGameRulesForEntry( uint32 idx, bool accessShuffled /*=true*/ ) const
{
	int realIndex = GetRealRotationIndex( idx, accessShuffled );

	if( realIndex >= 0 )
	{
		return m_rotation[ realIndex ].gameRulesNames.size();
	}

	return 0;
}	

//------------------------------------------------------------------------
const char*	CLevelRotation::GetGameRules( uint32 idx, uint32 iMode, bool accessShuffled /*=true*/ ) const
{
	int realIndex = GetRealRotationIndex( idx, accessShuffled );

	if( realIndex >= 0 )
	{
		int nRules = m_rotation[ realIndex ].gameRulesNames.size();

		if( iMode < nRules )
		{
			if( accessShuffled && (m_randFlags & ePRF_Shuffle) )
			{
				iMode = m_rotation[ realIndex ].gameRulesShuffle[ iMode ];
			}

			return m_rotation[ realIndex ].gameRulesNames[ iMode ].c_str();
		}
	}

	return 0;
}

//------------------------------------------------------------------------
const char* CLevelRotation::GetNextGameRulesForEntry( int idx ) const
{
	int realIndex = GetRealRotationIndex( idx, true );

	if( realIndex >= 0 )
	{
#if LEVEL_ROTATION_DEBUG	
		const SLevelRotationEntry& curLevel = m_rotation[ realIndex ];
		const char* gameModeName =  curLevel.GameModeName();

		Log_LevelRotation("::GetNextGameRulesForEntry() idx %d, realIndex %d, level name %s, currentModeIndex %d, shuffled index %d, mode name %s",
			idx, realIndex, curLevel.levelName.c_str(), curLevel.currentModeIndex, curLevel.gameRulesShuffle[ curLevel.currentModeIndex ], gameModeName );
#endif //LEVEL_ROTATION_DEBUG

		return m_rotation[ realIndex ].GameModeName();
	}

	return 0;
}

//------------------------------------------------------------------------
const int CLevelRotation::NumAdvancesTaken() const
{
	return m_advancesTaken;
}

//------------------------------------------------------------------------
void CLevelRotation::ResetAdvancement()
{
	Log_LevelRotation( "::ResetAdvancement(), m_advancesTaken, m_next and currentModeIndex all set to 0" );

	m_advancesTaken = 0;
	m_next = 0;

	int nEntries = m_rotation.size();
	for( int iLevel = 0; iLevel < nEntries; ++iLevel )
	{
		m_rotation[ iLevel ].currentModeIndex = 0;
	}

}

//------------------------------------------------------------------------
int CLevelRotation::GetLength() const
{
	return (int)m_rotation.size();
}

int CLevelRotation::GetTotalGameModeEntries() const
{
	int nLevels = m_rotation.size();

	int nGameModes = 0;

	for( int iLevel = 0; iLevel < nLevels; ++iLevel )
	{
		nGameModes += m_rotation[ iLevel ].gameRulesNames.size();
	}

	return nGameModes;
}

//------------------------------------------------------------------------
int CLevelRotation::GetNext() const
{
	return m_next;
}

//------------------------------------------------------------------------
void CLevelRotation::SetRandomisationFlags(TRandomisationFlags randMode)
{
	//check there's nothing in here apart from our flags
	CRY_ASSERT( (randMode & ~ ( ePRF_Shuffle |ePRF_MaintainPairs ) ) == 0);
	
	m_randFlags = randMode;
}

//------------------------------------------------------------------------

bool CLevelRotation::IsRandom()const
{
  return ( m_randFlags & ePRF_Shuffle ) != 0;
}

//------------------------------------------------------------------------
void CLevelRotation::ChangeLevel(IConsoleCmdArgs* pArgs)
{
	SGameContextParams ctx;
	const char *nextGameRules=GetNextGameRules();
	if (nextGameRules && nextGameRules[0])
		ctx.gameRules = nextGameRules;
	else if (IEntity *pEntity=CCryAction::GetCryAction()->GetIGameRulesSystem()->GetCurrentGameRulesEntity())
		ctx.gameRules = pEntity->GetClass()->GetName();
	else if (ILevelInfo *pLevelInfo=CCryAction::GetCryAction()->GetILevelSystem()->GetLevelInfo(GetNextLevel()))
		ctx.gameRules = pLevelInfo->GetDefaultGameType()->name;

	ctx.levelName = GetNextLevel();

  if (CCryAction::GetCryAction()->StartedGameContext())
  {
		CCryAction::GetCryAction()->ChangeGameContext(&ctx);
  }
  else
  {
    gEnv->pConsole->ExecuteString( string("sv_gamerules ") + ctx.gameRules );

    string command = string("map ") + ctx.levelName;
    if (pArgs)
      for (int i = 1; i < pArgs->GetArgCount(); ++i)
        command += string(" ") + pArgs->GetArg(i);
		command += " s";
    gEnv->pConsole->ExecuteString(command);
  }

	
	if (!Advance())
		First();
}

static CRndGen g_randomGenerator;

//------------------------------------------------------------------------
void CLevelRotation::Initialise( int nSeed )
{
	Log_LevelRotation( "::Initalise()" );
	m_advancesTaken = 0;

	First();
	
	if( nSeed >= 0 )
	{
		g_randomGenerator.Seed(nSeed);
		Log_LevelRotation( " Called g_randomGenerator.seed( %d )", nSeed );
	}

	if( ! m_rotation.empty() && m_randFlags & ePRF_Shuffle )
	{
		ShallowShuffle();
		ModeShuffle();	
	}	

}

//------------------------------------------------------------------------
void CLevelRotation::ModeShuffle()
{
	if( m_hasGameModesDecks )
	{
		//shuffle every map's modes.
		int nEntries = m_rotation.size();

		for( int iLevel = 0; iLevel < nEntries; ++iLevel )
		{
			int nModes = 	m_rotation[ iLevel ].gameRulesNames.size();

			std::vector<int>& gameModesShuffle = m_rotation[ iLevel ].gameRulesShuffle;
			gameModesShuffle.resize( nModes );

			for( int iMode = 0; iMode < nModes; ++iMode )
			{
				gameModesShuffle[ iMode ] = iMode;
			}

			for( int iMode = 0; iMode < nModes; ++iMode )
			{
				int idx = g_randomGenerator.GenerateUint32() % nModes;
				Log_LevelRotation( "Called g_randomGenerator.Generate()" );
				std::swap( gameModesShuffle[ iMode ], gameModesShuffle[ idx ] );
			}

#if LEVEL_ROTATION_DEBUG
			Log_LevelRotation( "ModeShuffle Level entry %d Order", iLevel );

			for( int iMode = 0; iMode < nModes; ++iMode )
			{
				Log_LevelRotation( "Slot %d Mode %d", iMode, gameModesShuffle[ iMode ] );
			}
#endif //LEVEL_ROTATION_DEBUG
			m_rotation[ iLevel ].currentModeIndex = 0;
		}
	}
}

//------------------------------------------------------------------------
void CLevelRotation::ShallowShuffle()
{
	//shuffle levels only
	int nEntries = m_rotation.size();
	m_shuffle.resize( nEntries );

	for( int i = 0; i < nEntries; ++i )
	{
		m_shuffle[ i ] = i;
	}

	if( m_randFlags & ePRF_MaintainPairs )
	{
		Log_LevelRotation( "CLevelRotation::ShallowShuffle doing pair shuffle" );
		CRY_ASSERT_MESSAGE( nEntries % 2 == 0, "CLevelRotation::Shuffle Set to maintain pairs shuffle, require even number of entries, but we have odd. Should have been handled during initialisation" );

		//swap, but only even indices with even indices, and the ones after the same
		int halfEntries = nEntries / 2;

		for( int i = 0; i < nEntries; i+=2 )
		{
			int idx = (g_randomGenerator.GenerateUint32() % halfEntries) * 2;
			Log_LevelRotation( "Called g_randomGenerator.GenerateUint32()" );
			std::swap( m_shuffle[ i ], m_shuffle[ idx ] );
			std::swap( m_shuffle[ i+1 ], m_shuffle[idx+1 ] );
		}
	}
	else
	{
		for( int i = 0; i < nEntries; ++i )
		{
			int idx = g_randomGenerator.GenerateUint32() % nEntries;
			Log_LevelRotation( "Called g_randomGenerator.GenerateUint32()" );
			std::swap( m_shuffle[i], m_shuffle[idx] );
		}
	}


	Log_LevelRotation( "ShallowShuffle new order:" );

	for( int iShuff = 0; iShuff < m_shuffle.size(); ++iShuff )
	{
		Log_LevelRotation( " %d - %s", m_shuffle[ iShuff ], m_rotation[ m_shuffle[ iShuff ] ].levelName.c_str() );
	}

	
}

//------------------------------------------------------------------------
bool CLevelRotation::NextPairMatch() const
{
	bool match = true;
	if( m_next>=0 && m_next<m_rotation.size() )
	{
		bool shuffled = ( m_randFlags & ePRF_Shuffle ) != 0 ;

		int next = shuffled ? m_shuffle[ m_next ] : m_next;

		int nextPlus1 = m_next+1;
		if( nextPlus1 >= m_rotation.size() )
		{
			nextPlus1 = 0;
		}
		if( shuffled )
		{
			nextPlus1 = m_shuffle[ nextPlus1 ];
		}

		if( stricmp( m_rotation[ next ].levelName.c_str(), m_rotation[ nextPlus1 ].levelName.c_str() ) != 0 )
		{
			match = false;
		}
		else if( stricmp( m_rotation[ next ].GameModeName(), m_rotation[ nextPlus1 ].GameModeName() ) != 0 )
		{
			match = false;
		}
	}

	return match;
}

//------------------------------------------------------------------------
const char* CLevelRotation::SLevelRotationEntry::GameModeName() const
{
	if( currentModeIndex >= 0 && currentModeIndex < gameRulesNames.size() )
	{
		//we can always use the shuffle list. If we aren't shuffled, it will still contain ordered indicies from creation
		int iMode = gameRulesShuffle[ currentModeIndex ];
		
		return gameRulesNames[ iMode ].c_str();
	}

	return NULL;
}


//------------------------------------------------------------------------
bool CLevelInfo::SupportsGameType(const char *gameTypeName) const
{
	//read level meta data
  for(int i=0;i<m_gamerules.size();++i)
  {
    if(!stricmp(m_gamerules[i].c_str(),gameTypeName))
      return true;
  }
  return false;
}

//------------------------------------------------------------------------
const char* CLevelInfo::GetDisplayName() const
{
  return m_levelDisplayName.c_str();
}

//------------------------------------------------------------------------
bool CLevelInfo::GetAttribute(const char* name, TFlowInputData& val) const
{
	TAttributeList::const_iterator it = m_levelAttributes.find(name);
	if (it != m_levelAttributes.end())
	{
		val = it->second;
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CLevelInfo::ReadInfo()
{
	string levelPath = m_levelPath;
	//string paks = levelPath + string("/*.pak");









	string xmlFile = levelPath + string("/LevelInfo.xml");
	XmlNodeRef rootNode = GetISystem()->LoadXmlFromFile(xmlFile.c_str());

	if (rootNode)
	{
		const char* sandboxVersion = rootNode->getAttr("SandboxVersion");
		if (sandboxVersion)
		{
			int major, minor, bugfix;
			int buildNumber = 0;
			if (sscanf(sandboxVersion, "%i.%i.%i.%i", &major, &minor, &bugfix, &buildNumber) == 4)
			{
				const int leakedBuildNumber = 5620;
				if (buildNumber == leakedBuildNumber)
					return false;
			}
		}

		m_heightmapSize = atoi(rootNode->getAttr("HeightmapSize"));

		string dataFile = levelPath + string("/LevelDataAction.xml");
		XmlNodeRef dataNode = GetISystem()->LoadXmlFromFile(dataFile.c_str());
		if(!dataNode)
		{
			dataFile = levelPath + string("/LevelData.xml");
			dataNode = GetISystem()->LoadXmlFromFile(dataFile.c_str());
		}

		if (dataNode)
		{
			XmlNodeRef gameTypesNode = dataNode->findChild("Missions");

			if ((gameTypesNode!=0) && (gameTypesNode->getChildCount() > 0))
			{
				// I can't be certain from looking at the code that these aren't used outside of a level, so better put them on the global heap
				ScopedSwitchToGlobalHeap useGlobalHeap;
				m_gameTypes.clear();
				//m_musicLibs.clear();

				for (int i = 0; i < gameTypesNode->getChildCount(); i++)
				{
					XmlNodeRef gameTypeNode = gameTypesNode->getChild(i);

					if (gameTypeNode->isTag("Mission"))
					{
						const char *gameTypeName = gameTypeNode->getAttr("Name");

						if (gameTypeName)
						{
							ILevelInfo::TGameTypeInfo info;

							info.cgfCount = 0;
							gameTypeNode->getAttr("CGFCount", info.cgfCount);
							info.name = gameTypeNode->getAttr("Name");
							info.xmlFile = gameTypeNode->getAttr("File");
							m_gameTypes.push_back(info);
						}
					}
				}

        // Gets reintroduced when level specific music data loading is implemented.
				/*XmlNodeRef musicLibraryNode = dataNode->findChild("MusicLibrary");

				if ((musicLibraryNode!=0) && (musicLibraryNode->getChildCount() > 0))
				{
					for (int i = 0; i < musicLibraryNode->getChildCount(); i++)
					{
						XmlNodeRef musicLibrary = musicLibraryNode->getChild(i);

						if (musicLibrary->isTag("Library"))
						{
							const char *musicLibraryName = musicLibrary->getAttr("File");

							if (musicLibraryName)
							{
								m_musicLibs.push_back(string("music/") + musicLibraryName);
							}
						}
					}
				}*/
			}
		}
	}
	return rootNode != 0;
}

//////////////////////////////////////////////////////////////////////////
void CLevelInfo::ReadMetaData()
{
  string fullPath(GetPath());
  int slashPos = fullPath.find_last_of("\\/");
  string mapName = fullPath.substr(slashPos+1, fullPath.length()-slashPos);
  fullPath.append("/");
  fullPath.append(mapName);
  fullPath.append(".xml");
  m_levelAttributes.clear();

	m_levelDisplayName = string("@ui_") + mapName;

  if (!gEnv->pCryPak->IsFileExist(fullPath.c_str()))
  	return;

  XmlNodeRef mapInfo = GetISystem()->LoadXmlFromFile(fullPath.c_str());
  //retrieve the coordinates of the map
	bool foundMinimapInfo = false;
  if(mapInfo)
  {
    for(int n = 0; n < mapInfo->getChildCount(); ++n)
    {
      XmlNodeRef rulesNode = mapInfo->getChild(n);
      const char* name = rulesNode->getTag();
      if(!stricmp(name, "Gamerules"))
      {
        for(int a = 0; a < rulesNode->getNumAttributes(); ++a)
        {
          const char *key, *value;
          rulesNode->getAttributeByIndex(a, &key, &value);
          m_gamerules.push_back(value);
        }
      }
      else if(!stricmp(name,"Display"))
      {
        XmlString v;
        if(rulesNode->getAttr("Name",v))
          m_levelDisplayName = v.c_str();
      }
			else if (!stricmp(name, "PreviewImage"))
			{
				const char *pFilename = NULL;
				if (rulesNode->getAttr("Filename", &pFilename))
				{
					m_previewImagePath = pFilename;
				}
			}
			else if (!stricmp(name, "BackgroundImage"))
			{
				const char *pFilename = NULL;
				if (rulesNode->getAttr("Filename", &pFilename))
				{
					m_backgroundImagePath = pFilename;
				}
			}
			else if (!stricmp(name, "Minimap"))
			{
				foundMinimapInfo = true;

				const char* minimap_dds = "";
				foundMinimapInfo &= rulesNode->getAttr( "Filename", &minimap_dds );
				m_minimapImagePath = minimap_dds;
				m_minimapInfo.sMinimapName = GetPath();
				m_minimapInfo.sMinimapName.append("/");
				m_minimapInfo.sMinimapName.append(minimap_dds);

				foundMinimapInfo &= rulesNode->getAttr( "startX", m_minimapInfo.fStartX );
				foundMinimapInfo &= rulesNode->getAttr( "startY", m_minimapInfo.fStartY );
				foundMinimapInfo &= rulesNode->getAttr( "endX", m_minimapInfo.fEndX );
				foundMinimapInfo &= rulesNode->getAttr( "endY", m_minimapInfo.fEndY );
				foundMinimapInfo &= rulesNode->getAttr( "width", m_minimapInfo.iWidth );
				foundMinimapInfo &= rulesNode->getAttr( "height", m_minimapInfo.iHeight );
				m_minimapInfo.fDimX = m_minimapInfo.fEndX - m_minimapInfo.fStartX;
				m_minimapInfo.fDimY = m_minimapInfo.fEndY - m_minimapInfo.fStartY;
				m_minimapInfo.fDimX = m_minimapInfo.fDimX > 0 ? m_minimapInfo.fDimX : 1;
				m_minimapInfo.fDimY = m_minimapInfo.fDimY > 0 ? m_minimapInfo.fDimY : 1;
			}
			else if (!stricmp(name, "Tag"))
			{
				m_levelTag = ILevelSystem::TAG_UNKNOWN;
				SwapEndian(m_levelTag, eBigEndian);
				const char *pTag = NULL;
				if (rulesNode->getAttr("Value", &pTag))
				{
					strncpy((char*)&m_levelTag, pTag, 4);
				}
			}
			else if (!stricmp(name, "Attributes"))
			{
				for(int a = 0; a < rulesNode->getChildCount(); ++a)
				{
					XmlNodeRef attrib = rulesNode->getChild(a);
					assert(m_levelAttributes.find(attrib->getTag()) == m_levelAttributes.end());
					m_levelAttributes[attrib->getTag()] = TFlowInputData( string(attrib->getAttr("value")) );;
				}
			}
			else if (!stricmp(name, "LevelType"))
			{
				const char* levelType;
				if(rulesNode->getAttr("value", &levelType))
				{					
					m_levelTypeList.push_back( levelType );
				}
			}
    }
		m_bMetaDataRead = true;
  }
	if (!foundMinimapInfo)
	{
		gEnv->pLog->LogWarning("Map %s: Missing or invalid minimap info!", mapName.c_str());
	}
}

//------------------------------------------------------------------------
const ILevelInfo::TGameTypeInfo *CLevelInfo::GetDefaultGameType() const
{
	if (!m_gameTypes.empty())
	{
		return &m_gameTypes[0];
	}

	return 0;
};

/// Used by console auto completion.
struct SLevelNameAutoComplete : public IConsoleArgumentAutoComplete
{
	std::vector<string> levels;
	virtual int GetCount() const { return levels.size(); };
	virtual const char* GetValue( int nIndex ) const { return levels[nIndex].c_str(); };
};
// definition and declaration must be separated for devirtualization
SLevelNameAutoComplete g_LevelNameAutoComplete;

//------------------------------------------------------------------------
CLevelSystem::CLevelSystem(ISystem *pSystem, const char *levelsFolder)
: m_pSystem(pSystem),
	m_pCurrentLevel(0),
	m_pLoadingLevelInfo(0)
{
	LOADING_TIME_PROFILE_SECTION;
	CRY_ASSERT(pSystem);

	//Load user defined level types
	if (XmlNodeRef levelTypeNode = m_pSystem->LoadXmlFromFile("Libs/Levels/leveltypes.xml"))
	{
		for (unsigned int i = 0; i < levelTypeNode->getChildCount(); ++i)
		{
			XmlNodeRef child = levelTypeNode->getChild(i);
			const char* levelType;

			if(child->getAttr("value", &levelType))
			{
				m_levelTypeList.push_back(string(levelType));
			}
		}
	}
	
	//if (!gEnv->IsEditor())
		Rescan(levelsFolder, ILevelSystem::TAG_MAIN);

	// register with system to get loading progress events
	m_pSystem->SetLoadingProgressListener(this);
	m_fLastLevelLoadTime = 0;
	m_fFilteredProgress = 0;
	m_fLastTime = 0;
	m_bLevelLoaded = false;
	m_bRecordingFileOpens = false;

	m_levelLoadStartTime.SetValue(0);

	m_nLoadedLevelsCount = 0;

	m_extLevelRotations.resize(0);

	gEnv->pConsole->RegisterAutoComplete( "map",&g_LevelNameAutoComplete );
}

//------------------------------------------------------------------------
CLevelSystem::~CLevelSystem()
{
	// register with system to get loading progress events
	m_pSystem->SetLoadingProgressListener(0);
}

//------------------------------------------------------------------------
void CLevelSystem::Rescan(const char *levelsFolder, const uint32 tag)
{
	if (levelsFolder)
	{
		if (const ICmdLineArg *pModArg = m_pSystem->GetICmdLine()->FindArg(eCLAT_Pre,"MOD"))
		{
			if (m_pSystem->IsMODValid(pModArg->GetValue()))
			{
				m_levelsFolder.Format("Mods/%s/%s/%s", pModArg->GetValue(), PathUtil::GetGameFolder().c_str(), levelsFolder);
				ScanFolder(0, true, tag);
			}
		}

		m_levelsFolder = levelsFolder;
	}

	CRY_ASSERT(!m_levelsFolder.empty());

	m_levelInfos.reserve(64);
	ScanFolder(0, false, tag);

	g_LevelNameAutoComplete.levels.clear();
	for (int i = 0; i < (int)m_levelInfos.size(); i++)
	{
		g_LevelNameAutoComplete.levels.push_back( PathUtil::GetFileName(m_levelInfos[i].GetName()) );
	}
}

void CLevelSystem::LoadRotation()
{
  if (ICVar *pLevelRotation=gEnv->pConsole->GetCVar("sv_levelrotation"))
  {
    ILevelRotationFile* file = 0;
    IPlayerProfileManager *pProfileMan = CCryAction::GetCryAction()->GetIPlayerProfileManager();
    if(pProfileMan)
    {
      const char *userName = pProfileMan->GetCurrentUser();
      IPlayerProfile *pProfile = pProfileMan->GetCurrentProfile(userName);
      if(pProfile)
      {
        file = pProfile->GetLevelRotationFile(pLevelRotation->GetString());
      }
      else if (pProfile = pProfileMan->GetDefaultProfile())
      {
        file = pProfile->GetLevelRotationFile(pLevelRotation->GetString());
      }
    }
    bool ok = false;
    if(file)
    {
      ok = m_levelRotation.Load(file);
      file->Complete();
    }

    if(!ok)
      CryWarning(VALIDATOR_MODULE_GAME, VALIDATOR_WARNING, "Failed to load '%s' as level rotation!", pLevelRotation->GetString());
  }
}

//------------------------------------------------------------------------
void CLevelSystem::ScanFolder(const char *subfolder, bool modFolder, const uint32 tag)
{
	//CryLog("[DLC] ScanFolder:'%s' tag:'%.4s'", subfolder, (char*)&tag);
	string folder;
	if (subfolder && subfolder[0])
		folder = subfolder;

	string search(m_levelsFolder);
	if (!folder.empty())
		search += string("/") + folder;
	search += "/*.*";

	ICryPak *pPak = gEnv->pCryPak;

	_finddata_t fd;
	intptr_t handle = 0;

	//CryLog("[DLC] ScanFolder: search:'%s'", search.c_str());
	// --kenzo
	// allow this find first to actually touch the file system
	// (causes small overhead but with minimal amount of levels this should only be around 150ms on actual DVD Emu)
	handle = pPak->FindFirst(search.c_str(), &fd, 0, true);

	if (handle > -1)
	{
		do
		{
			if (!(fd.attrib & _A_SUBDIR) || !strcmp(fd.name, ".") || !strcmp(fd.name, ".."))
			{
				continue;
			}

			CLevelInfo levelInfo;

			string levelFolder = (folder.empty() ? "" : (folder + "/")) + string(fd.name);
			string levelPath = m_levelsFolder + "/" + levelFolder;
			string paks = levelPath + string("/*.pak");

			//CryLog("[DLC] ScanFolder fd:'%s' levelPath:'%s'", fd.name, levelPath.c_str());

			const string levelPakName = levelPath + "/level.pak";
			const string levelXmlName = levelPath + "/" + fd.name + ".xml";
#if 0
			if (pPak->IsFileExist(levelPakName.c_str(), ICryPak::eFileLocation_OnDisk))
			{
				CryLog("[DLC] %s found on disk", levelPakName.c_str());
			}
			if (pPak->IsFileExist(levelXmlName.c_str()))
			{
				CryLog("[DLC] %s found", levelXmlName.c_str());
			}
#endif

			if (!pPak->IsFileExist(levelPakName.c_str(), ICryPak::eFileLocation_OnDisk) && !pPak->IsFileExist(levelXmlName.c_str()))
			{
				ScanFolder(levelFolder.c_str(), modFolder, tag);
				continue;
			}

			//CryLog("[DLC] ScanFolder adding level:'%s'", levelPath.c_str()); 
			levelInfo.m_levelPath = levelPath;
			levelInfo.m_levelPaks = paks;
			levelInfo.m_levelName = levelFolder;
			levelInfo.m_levelName = UnifyName(levelInfo.m_levelName);
			levelInfo.m_isModLevel = modFolder;
			levelInfo.m_scanTag = tag;
			levelInfo.m_levelTag = ILevelSystem::TAG_UNKNOWN;

			SwapEndian(levelInfo.m_scanTag, eBigEndian);
			SwapEndian(levelInfo.m_levelTag, eBigEndian);


			CLevelInfo *pExistingInfo = GetLevelInfoInternal(levelInfo.m_levelName);
			if( pExistingInfo && pExistingInfo->MetadataLoaded() == false )
			{
				//Reload metadata if it failed to load
				pExistingInfo->ReadMetaData();
			}

			// Don't add the level if it is already in the list
			if ( pExistingInfo == NULL )
			{
				levelInfo.ReadMetaData();

				m_levelInfos.push_back(levelInfo);
			}
			else
			{
				// Update the scan tag
				pExistingInfo->m_scanTag = tag;
			}

		} while (pPak->FindNext(handle, &fd) >= 0);

		pPak->FindClose(handle);
	}
}

//------------------------------------------------------------------------
int CLevelSystem::GetLevelCount()
{
	return (int)m_levelInfos.size();
}

//------------------------------------------------------------------------
ILevelInfo *CLevelSystem::GetLevelInfo(int level)
{
	return GetLevelInfoInternal(level);
}

//------------------------------------------------------------------------
CLevelInfo *CLevelSystem::GetLevelInfoInternal(int level)
{
	if ((level >= 0) && (level < GetLevelCount()))
	{
		return &m_levelInfos[level];
	}

	return 0;
}

//------------------------------------------------------------------------
ILevelInfo *CLevelSystem::GetLevelInfo(const char *levelName)
{
	return GetLevelInfoInternal(levelName);
}

//------------------------------------------------------------------------
CLevelInfo *CLevelSystem::GetLevelInfoInternal(const char *levelName)
{
	// If level not found by full name try comparing with only filename
	for (std::vector<CLevelInfo>::iterator it = m_levelInfos.begin(); it != m_levelInfos.end(); ++it)
	{
		if (!strcmpi(it->GetName(), levelName))
		{
			return &(*it);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	for (std::vector<CLevelInfo>::iterator it = m_levelInfos.begin(); it != m_levelInfos.end(); ++it)
	{
		if (!strcmpi(PathUtil::GetFileName(it->GetName()), levelName))
		{
			return &(*it);
		}
	}

	// Try stripping out the folder to find the raw filename
	string sLevelName(levelName);
	size_t lastSlash = sLevelName.find_last_of('\\');
	if(lastSlash == string::npos)
		lastSlash = sLevelName.find_last_of('/');
	if(lastSlash != string::npos)
	{
		sLevelName = sLevelName.substr(lastSlash + 1, sLevelName.size() - lastSlash - 1);
		return GetLevelInfoInternal(sLevelName.c_str());
	}

	return 0;
}

//------------------------------------------------------------------------
void CLevelSystem::AddListener(ILevelSystemListener *pListener)
{
	std::vector<ILevelSystemListener *>::iterator it = std::find(m_listeners.begin(), m_listeners.end(), pListener);

	if (it == m_listeners.end())
	{
		m_listeners.reserve(12);
		m_listeners.push_back(pListener);
	}
}

//------------------------------------------------------------------------
void CLevelSystem::RemoveListener(ILevelSystemListener *pListener)
{
	std::vector<ILevelSystemListener *>::iterator it = std::find(m_listeners.begin(), m_listeners.end(), pListener);

	if (it != m_listeners.end())
	{
		m_listeners.erase(it);

		if (m_listeners.empty())
		{
			stl::free_container(m_listeners);
		}
	}
}

// a guard scope that ensured that LiveCreate commands are not executed while level is loading
#ifndef NO_LIVECREATE
class CLiveCreateLevelLoadingBracket
{
public:
	CLiveCreateLevelLoadingBracket()
	{
		gEnv->pLiveCreateHost->SuppressCommandExecution();
	}

	~CLiveCreateLevelLoadingBracket()
	{
		gEnv->pLiveCreateHost->ResumeCommandExecution();
	}
};
#endif

//------------------------------------------------------------------------
ILevel *CLevelSystem::LoadLevel(const char *_levelName)
{
#ifndef NO_LIVECREATE
	CLiveCreateLevelLoadingBracket liveCreateLoadingBracket;
#endif

	gEnv->pSystem->SetSystemGlobalState(ESYSTEM_GLOBAL_STATE_LEVEL_LOAD_START);

	MEMSTAT_CONTEXT_FMT(EMemStatContextTypes::MSC_Other, 0, "Level load (%s)", _levelName);

	CryLog ("Level system is loading \"%s\"", _levelName);
	INDENT_LOG_DURING_SCOPE();

	char levelName[256];
	string sNextLevel(_levelName);
	strcpy_s(levelName,sizeof(levelName),_levelName);

#ifdef PS3
	if(gPS3Env->bForce10HzFlip==1 && gEnv->pRenderer)
	{
		gEnv->pRenderer->ForceSwapBuffers();		
	}
#endif
	// Not remove a scope!!!
	{
		LOADING_TIME_PROFILE_SECTION;
		CRY_ASSERT(levelName);

		//m_levelLoadStartTime = gEnv->pTimer->GetAsyncTime();

		CLevelInfo *pLevelInfo = GetLevelInfoInternal(levelName);

		if (!pLevelInfo)
		{
			// alert the listener
			OnLevelNotFound(levelName);

			return 0;
		}

		m_bLevelLoaded = false;

		const bool bLoadingSameLevel = m_lastLevelName.compareNoCase(levelName) == 0;
		m_lastLevelName = levelName;

		delete m_pCurrentLevel;
		CLevel *pLevel = new CLevel();
		pLevel->m_levelInfo = *pLevelInfo;
		m_pCurrentLevel = pLevel;

		//////////////////////////////////////////////////////////////////////////
		// Read main level info.
		if (!pLevelInfo->ReadInfo())
		{
			OnLoadingError(pLevelInfo, "Failed to read level info (level.pak might be corrupted)!");
			return 0;
		}
		//[AlexMcC|19.04.10]: Update the level's LevelInfo
		pLevel->m_levelInfo = *pLevelInfo;
		//////////////////////////////////////////////////////////////////////////

		gEnv->pConsole->SetScrollMax(600);
		ICVar *con_showonload = gEnv->pConsole->GetCVar("con_showonload");
		if (con_showonload && con_showonload->GetIVal() != 0)
		{
			gEnv->pConsole->ShowConsole(true);
			ICVar *g_enableloadingscreen = gEnv->pConsole->GetCVar("g_enableloadingscreen");
			if (g_enableloadingscreen)
				g_enableloadingscreen->Set(0);
		}

		// Reset the camera to (0,0,0) which is the invalid/uninitialised state
		CCamera defaultCam;
		m_pSystem->SetViewCamera(defaultCam);

		m_pLoadingLevelInfo = pLevelInfo;
		OnLoadingStart(pLevelInfo);

		// ensure a physical global area is present
		IPhysicalWorld *pPhysicalWorld = gEnv->pPhysicalWorld;
		pPhysicalWorld->AddGlobalArea();

		ICryPak *pPak = gEnv->pCryPak;

		string levelPath = pLevelInfo->GetPath();

		/*
		ICVar *pFileCache = gEnv->pConsole->GetCVar("sys_FileCache");		CRY_ASSERT(pFileCache);

		if(pFileCache->GetIVal())
		{
		if(pPak->OpenPack("",pLevelInfo->GetPath()+string("/FileCache.dat")))
		gEnv->pLog->Log("FileCache.dat loaded");
		else
		gEnv->pLog->Log("FileCache.dat not loaded");
		}
		*/

		m_pSystem->SetThreadState(ESubsys_Physics, false);

		ICVar *pSpamDelay = gEnv->pConsole->GetCVar("log_SpamDelay");
		float spamDelay = 0.0f;
		if (pSpamDelay)
		{
			spamDelay = pSpamDelay->GetFVal();
			pSpamDelay->Set(0.0f);
		}

		IGameTokenSystem* pGameTokenSystem = CCryAction::GetCryAction()->GetIGameTokenSystem();

		// load all GameToken libraries this level uses incl. LevelLocal
		pGameTokenSystem->Reset();
		pGameTokenSystem->LoadLibs( pLevelInfo->GetPath() + string("/GameTokens/*.xml"));

		if (gEnv->pEntitySystem)
		{
			// load layer infos before load level by 3dEngine and EntitySystem
			gEnv->pEntitySystem->LoadLayers(pLevelInfo->GetPath() + string("/LevelData.xml"));
		}

		if (!gEnv->p3DEngine->LoadLevel(pLevelInfo->GetPath(), pLevelInfo->GetDefaultGameType()->name))
		{
			OnLoadingError(pLevelInfo, "3DEngine failed to handle loading the level");

			return 0;
		}
		gEnv->pSystem->SetSystemGlobalState(ESYSTEM_GLOBAL_STATE_LEVEL_LOAD_START_ENTITIES);
		if (!gEnv->pEntitySystem || !gEnv->pEntitySystem->OnLoadLevel(pLevelInfo->GetPath()))
		{
			OnLoadingError(pLevelInfo, "EntitySystem failed to handle loading the level");

			return 0;
		}

		// reset all the script timers
		gEnv->pScriptSystem->ResetTimers();

		if (gEnv->pAISystem)
		{
			gEnv->pAISystem->Reset(IAISystem::RESET_LOAD_LEVEL);
		}

		// Reset TimeOfDayScheduler
		CCryAction::GetCryAction()->GetTimeOfDayScheduler()->Reset();
		CCryAction::GetCryAction()->OnActionEvent(SActionEvent(eAE_loadLevel));

		CCryAction::GetCryAction()->CreatePhysicsQueues();

		// Reset dialog system
		if (gEnv->pDialogSystem)
		{
			gEnv->pDialogSystem->Reset(false);
			gEnv->pDialogSystem->Init();
		}

		// Parse level specific config data.
		CryFixedStringT<MAX_AUDIO_FILE_PATH_LENGTH> sAudioLevelPath(gEnv->pAudioSystem->GetConfigPath());
		sAudioLevelPath.append("levels/");
		string const sLevelNameOnly = PathUtil::GetFileName(levelName);
		sAudioLevelPath += sLevelNameOnly;
		SAudioManagerRequestData<eAMRT_PARSE_CONTROLS_DATA> oAMData(sAudioLevelPath, eADS_LEVEL_SPECIFIC);
		SAudioRequest oAudioRequestData;
		oAudioRequestData.nFlags = eARF_PRIORITY_HIGH | eARF_EXECUTE_BLOCKING; // Needs to be blocking so data is available for next preloading request!
		oAudioRequestData.pData = &oAMData;
		gEnv->pAudioSystem->PushRequest(oAudioRequestData);

		SAudioManagerRequestData<eAMRT_PARSE_PRELOADS_DATA> oAMData2(sAudioLevelPath, eADS_LEVEL_SPECIFIC);
		oAudioRequestData.pData = &oAMData2;
		gEnv->pAudioSystem->PushRequest(oAudioRequestData);

		TAudioPreloadRequestID nPreloadRequestID = INVALID_AUDIO_PRELOAD_REQUEST_ID;

		if (gEnv->pAudioSystem->GetAudioPreloadRequestID(sLevelNameOnly.c_str(), nPreloadRequestID))
		{
			SAudioManagerRequestData<eAMRT_PRELOAD_SINGLE_REQUEST> oAMData2(nPreloadRequestID);
			oAudioRequestData.pData = &oAMData2;
			gEnv->pAudioSystem->PushRequest(oAudioRequestData);
		}

		if (gEnv->pAISystem && gEnv->pAISystem->IsEnabled())
		{
			gEnv->pAISystem->FlushSystem();
			gEnv->pAISystem->LoadLevelData(pLevelInfo->GetPath(), pLevelInfo->GetDefaultGameType()->name);
		}

		gEnv->pGame->LoadExportedLevelData( pLevelInfo->GetPath(), pLevelInfo->GetDefaultGameType()->name.c_str() );

		ICustomActionManager* pCustomActionManager = gEnv->pGame->GetIGameFramework()->GetICustomActionManager();
		if (pCustomActionManager)
		{
			pCustomActionManager->LoadLibraryActions(CUSTOM_ACTIONS_PATH);
		}

    if (gEnv->pEntitySystem)
    {
			gEnv->pEntitySystem->ReserveEntityId(1);
			gEnv->pEntitySystem->ReserveEntityId(LOCAL_PLAYER_ENTITY_ID);
		}

		CCryAction::GetCryAction()->GetIGameRulesSystem()->CreateGameRules(CCryAction::GetCryAction()->GetGameContext()->GetRequestedGameRules());

		string missionXml = pLevelInfo->GetDefaultGameType()->xmlFile;
		string xmlFile = string(pLevelInfo->GetPath()) + "/" + missionXml;

		XmlNodeRef rootNode = m_pSystem->LoadXmlFromFile(xmlFile.c_str());
		if (rootNode)
		{
			INDENT_LOG_DURING_SCOPE (true, "Reading '%s'", xmlFile.c_str());
			const char *script = rootNode->getAttr("Script");

			if (script && script[0])
			{
				CryLog ("Executing script '%s'", script);
				INDENT_LOG_DURING_SCOPE();
				gEnv->pScriptSystem->ExecuteFile(script, true, true);
			}

			XmlNodeRef objectsNode = rootNode->findChild("Objects");

			if (objectsNode)
			{
				// Stop the network ticker thread before loading entities - otherwise the network queues
				// can get polled mid way through spawning an entity resulting in tasks being handled in
				// the wrong order.
				// Note: The network gets ticked after every 8 entity spawns so we are still ticking, just
				// not from the ticker thread.
				SCOPED_TICKER_LOCK;

				gEnv->pEntitySystem->LoadEntities(objectsNode, true);
			}
		}

		// Now that we've registered our AI objects, we can init
		if (gEnv->pAISystem)
			gEnv->pAISystem->Reset(IAISystem::RESET_ENTER_GAME);

		//////////////////////////////////////////////////////////////////////////
		// Movie system must be loaded after entities.
		//////////////////////////////////////////////////////////////////////////
		string movieXml = pLevelInfo->GetPath() + string("/moviedata.xml");
		IMovieSystem *movieSys = gEnv->pMovieSystem;
		if (movieSys != NULL)
		{
			movieSys->Load( movieXml, pLevelInfo->GetDefaultGameType()->name );
			movieSys->Reset(true,false); // bSeekAllToStart needs to be false here as it's only of interest in the editor (double checked with Timur Davidenko)
		}

		CCryAction::GetCryAction()->GetIMaterialEffects()->PreLoadAssets();

		gEnv->pFlowSystem->Reset(false);

		{
			LOADING_TIME_PROFILE_SECTION_NAMED("OnLoadingComplete");
			// Inform Level system listeners that loading of the level is complete.
			for (std::vector<ILevelSystemListener *>::const_iterator it = m_listeners.begin(); it != m_listeners.end(); ++it)
			{
				(*it)->OnLoadingComplete(m_pCurrentLevel);
			}
		}

		gEnv->pSystem->SetSystemGlobalState(ESYSTEM_GLOBAL_STATE_LEVEL_LOAD_START_PRECACHE);
		// Let gamerules precache anything needed
		if (IGameRules *pGameRules=CCryAction::GetCryAction()->GetIGameRulesSystem()->GetCurrentGameRules())
		{
			pGameRules->PrecacheLevel();
		}

		//////////////////////////////////////////////////////////////////////////
		// Notify 3D engine that loading finished
		//////////////////////////////////////////////////////////////////////////
		gEnv->p3DEngine->PostLoadLevel();

		if (gEnv->pScriptSystem)
		{
			// After level was loaded force GC cycle in Lua
			gEnv->pScriptSystem->ForceGarbageCollection();
		}
		//////////////////////////////////////////////////////////////////////////

		//////////////////////////////////////////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////
		gEnv->pConsole->SetScrollMax(600/2);

		pPak->GetResourceList(ICryPak::RFOM_NextLevel)->Clear();

		if (pSpamDelay)
			pSpamDelay->Set(spamDelay);

#if CAPTURE_REPLAY_LOG
		CryGetIMemReplay()->AddLabelFmt("loadEnd%d_%s", s_loadCount++, levelName);
#endif

		m_bLevelLoaded = true;
		gEnv->pSystem->SetSystemGlobalState(ESYSTEM_GLOBAL_STATE_LEVEL_LOAD_END);
	}

	GetISystem()->GetISystemEventDispatcher()->OnSystemEvent( ESYSTEM_EVENT_LEVEL_LOAD_END,0,0 );

	gEnv->pConsole->GetCVar("sv_map")->Set(levelName);

  gEnv->pSystem->GetISystemEventDispatcher()->OnSystemEvent(ESYSTEM_EVENT_LEVEL_PRECACHE_START, 0, 0);

	if (!gEnv->pSystem->IsSerializingFile() && gEnv->pEntitySystem)
	{
		// activate the default layers
		gEnv->pEntitySystem->EnableDefaultLayers();
	}

	{
		LOADING_TIME_PROFILE_SECTION_NAMED("ENTITY_EVENT_LEVEL_LOADED");

		//////////////////////////////////////////////////////////////////////////
		SEntityEvent loadingCompleteEvent(ENTITY_EVENT_LEVEL_LOADED);
		gEnv->pEntitySystem->SendEventToAll( loadingCompleteEvent );
		//////////////////////////////////////////////////////////////////////////
	}

	m_pSystem->SetThreadState(ESubsys_Physics, true);

	return m_pCurrentLevel;
}

//------------------------------------------------------------------------
ILevel* CLevelSystem::SetEditorLoadedLevel(const char *levelName, bool bReadLevelInfoMetaData)
{
	CLevelInfo *pLevelInfo = GetLevelInfoInternal(levelName);

	if (!pLevelInfo)
	{
		gEnv->pLog->LogError("Failed to get level info for level %s!", levelName);
		return 0;
	}

	if(bReadLevelInfoMetaData)
		pLevelInfo->ReadMetaData();

	m_lastLevelName = levelName;

	SAFE_DELETE(m_pCurrentLevel);
	CLevel *pLevel = new CLevel();
	pLevel->m_levelInfo = *pLevelInfo;
	m_pCurrentLevel = pLevel;
	m_bLevelLoaded = true;

	return m_pCurrentLevel;
}

//------------------------------------------------------------------------
// Search positions of all entities with class "PrecacheCamera" and pass it to the 3dengine
void CLevelSystem::PrecacheLevelRenderData()
{
	if (gEnv->IsDedicated())
		return;

//	gEnv->pSystem->GetISystemEventDispatcher()->OnSystemEvent(ESYSTEM_EVENT_LEVEL_PRECACHE_START, NULL, NULL);
#ifndef CONSOLE_CONST_CVAR_MODE
	ICVar *pPrecacheVar = gEnv->pConsole->GetCVar("e_PrecacheLevel");		CRY_ASSERT(pPrecacheVar);

	if(pPrecacheVar && pPrecacheVar->GetIVal()> 0)
	{

		if (gEnv->pGame)
			if(I3DEngine * p3DEngine = gEnv->p3DEngine)
			{
				std::vector<Vec3> arrCamOutdoorPositions;

				IEntitySystem *pEntitySystem = gEnv->pEntitySystem;
				IEntityItPtr pEntityIter = pEntitySystem->GetEntityIterator();

				IEntityClass *pPrecacheCameraClass = pEntitySystem->GetClassRegistry()->FindClass( "PrecacheCamera" );
				IEntity *pEntity = NULL;
				while (pEntity = pEntityIter->Next())
				{
					if (pEntity->GetClass() == pPrecacheCameraClass)
					{
						arrCamOutdoorPositions.push_back( pEntity->GetWorldPos() );
					}
				}
				Vec3 *pPoints = 0;
				if (arrCamOutdoorPositions.size() > 0)
				{
					pPoints = &arrCamOutdoorPositions[0];
				}

				p3DEngine->PrecacheLevel(true, pPoints,arrCamOutdoorPositions.size() );
			}
	}
#endif
//	gEnv->pSystem->GetISystemEventDispatcher()->OnSystemEvent(ESYSTEM_EVENT_LEVEL_PRECACHE_END, NULL, NULL);
}

//------------------------------------------------------------------------
void CLevelSystem::PrepareNextLevel(const char *levelName)
{
#ifdef PS3
	if(gEnv->pRenderer && gPS3Env->bForce10HzFlip == 2)
	{
		gEnv->pRenderer->ForceSwapBuffers();
	}
#endif
	m_levelLoadStartTime = gEnv->pTimer->GetAsyncTime();
	CLevelInfo *pLevelInfo = GetLevelInfoInternal(levelName);
	if (!pLevelInfo)
	{
		// alert the listener
		//OnLevelNotFound(levelName);
		return;
	}

	// force a Lua deep garbage collection
	{
		gEnv->pScriptSystem->ForceGarbageCollection();
	}

	// swap to the level heap
	CCryAction::GetCryAction()->SwitchToLevelHeap(levelName);

	// Open pak file for a new level.
	pLevelInfo->OpenLevelPak();

	// switched to level heap, so now imm start the loading screen (renderer will be reinitialized in the levelheap)
	gEnv->pSystem->GetISystemEventDispatcher()->OnSystemEvent( ESYSTEM_EVENT_LEVEL_LOAD_START_LOADINGSCREEN,(UINT_PTR)pLevelInfo,0 );
	gEnv->pSystem->SetSystemGlobalState(ESYSTEM_GLOBAL_STATE_LEVEL_LOAD_START_PREPARE);

	// Inform resource manager about loading of the new level.
	GetISystem()->GetIResourceManager()->PrepareLevel( pLevelInfo->GetPath(),pLevelInfo->GetName() );

	// Disable locking of resources to allow everything to be offloaded.

	//string filename = PathUtil::Make( pLevelInfo->GetPath(),"resourcelist.txt" );
	//gEnv->pCryPak->GetResourceList(ICryPak::RFOM_NextLevel)->Load( filename.c_str() );

#ifdef PS3
	if(gEnv->pRenderer && gPS3Env->bForce10HzFlip == 1)
	{
		gEnv->pRenderer->ForceSwapBuffers();
	}
#endif
}

//------------------------------------------------------------------------
void CLevelSystem::OnLevelNotFound(const char *levelName)
{
	for (std::vector<ILevelSystemListener *>::const_iterator it = m_listeners.begin(); it != m_listeners.end(); ++it)
	{
		(*it)->OnLevelNotFound(levelName);
	}
}

//------------------------------------------------------------------------
void CLevelSystem::OnLoadingStart(ILevelInfo *pLevelInfo)
{
	if(gEnv->pCryPak->GetRecordFileOpenList()==ICryPak::RFOM_EngineStartup)
		gEnv->pCryPak->RecordFileOpen(ICryPak::RFOM_Level);

	m_fFilteredProgress = 0.f;
	m_fLastTime = gEnv->pTimer->GetAsyncCurTime();

  GetISystem()->GetISystemEventDispatcher()->OnSystemEvent( ESYSTEM_EVENT_LEVEL_LOAD_START,0,0 );

	LOADING_TIME_PROFILE_SECTION(gEnv->pSystem);

#ifdef WIN32
	/*
	m_bRecordingFileOpens = GetISystem()->IsDevMode() && gEnv->pCryPak->GetRecordFileOpenList() == ICryPak::RFOM_Disabled;
	if (m_bRecordingFileOpens)
	{
		gEnv->pCryPak->GetResourceList(ICryPak::RFOM_Level)->Clear();
		gEnv->pCryPak->RecordFileOpen(ICryPak::RFOM_Level);
	}
	*/
#endif

	for (std::vector<ILevelSystemListener *>::const_iterator it = m_listeners.begin(); it != m_listeners.end(); ++it)
	{
		(*it)->OnLoadingStart(pLevelInfo);
	}

	BEGIN_LOADING(pLevelInfo->GetName());
}

//------------------------------------------------------------------------
void CLevelSystem::OnLoadingError(ILevelInfo *pLevelInfo, const char *error)
{
	if (!pLevelInfo)
		pLevelInfo = m_pLoadingLevelInfo;
	if (!pLevelInfo)
	{
		CRY_ASSERT(false);
		GameWarning("OnLoadingError without a currently loading level");
		return;
	}

	if(gEnv->pRenderer)
	{
		gEnv->pRenderer->SetTexturePrecaching(false);
	}

	for (std::vector<ILevelSystemListener *>::const_iterator it = m_listeners.begin(); it != m_listeners.end(); ++it)
	{
		(*it)->OnLoadingError(pLevelInfo, error);
	}

	END_LOADING(pLevelInfo->GetName());

	((CLevelInfo*)pLevelInfo)->CloseLevelPak();
}

//------------------------------------------------------------------------
void CLevelSystem::OnLoadingComplete(ILevel *pLevelInfo)
{
  PrecacheLevelRenderData();
	
	if (m_bRecordingFileOpens)
	{
		// Stop recoding file opens.
		gEnv->pCryPak->RecordFileOpen(ICryPak::RFOM_Disabled);
		// Save recorded list.
		SaveOpenedFilesList();
	}

	CTimeValue t = gEnv->pTimer->GetAsyncTime();
	m_fLastLevelLoadTime = (t-m_levelLoadStartTime).GetSeconds();
  
	if (!gEnv->IsEditor())
	{
		CryLog( "-----------------------------------------------------" );
		CryLog( "*LOADING: Level %s loading time: %.2f seconds",m_lastLevelName.c_str(),m_fLastLevelLoadTime );
		CryLog( "-----------------------------------------------------" );
	}

	LogLoadingTime();

	m_nLoadedLevelsCount++;

	// Hide console after loading.
	gEnv->pConsole->ShowConsole(false);

//  gEnv->pCryPak->GetFileReadSequencer()->EndSection();

	if (!gEnv->bServer)
		return;

	if (gEnv->pCharacterManager)
	{
		SAnimMemoryTracker amt; 
		gEnv->pCharacterManager->SetAnimMemoryTracker(amt);
	}
	/*
	if( IStatsTracker* tr = CCryAction::GetCryAction()->GetIGameStatistics()->GetSessionTracker() )
	{
		string mapName = "no_map_assigned";
		if( pLevelInfo )
			if (ILevelInfo * pInfo = pLevelInfo->GetLevelInfo())
			{
				mapName = pInfo->GetName();
				PathUtil::RemoveExtension(mapName);
				mapName = PathUtil::GetFileName(mapName);
				mapName.MakeLower();
			}
		tr->StateValue(eSP_Map, mapName.c_str());
	}
	*/

	// LoadLevel is not called in the editor, hence OnLoadingComplete is not invoked on the ILevelSystemListeners
	if (gEnv->IsEditor())
	{
		for (std::vector<ILevelSystemListener *>::const_iterator it = m_listeners.begin(); it != m_listeners.end(); ++it)
		{
			(*it)->OnLoadingComplete(pLevelInfo);
		}

    SEntityEvent loadingCompleteEvent(ENTITY_EVENT_LEVEL_LOADED);
    gEnv->pEntitySystem->SendEventToAll( loadingCompleteEvent );
	}

	END_LOADING(pLevelInfo->GetLevelInfo()->GetName());
}

//------------------------------------------------------------------------
void CLevelSystem::OnLoadingProgress(ILevelInfo *pLevel, int progressAmount)
{
	for (std::vector<ILevelSystemListener *>::const_iterator it = m_listeners.begin(); it != m_listeners.end(); ++it)
	{
		(*it)->OnLoadingProgress(pLevel, progressAmount);
	}
}

//------------------------------------------------------------------------
void CLevelSystem::OnUnloadComplete(ILevel* pLevel)
{
	for (std::vector<ILevelSystemListener *>::const_iterator it = m_listeners.begin(); it != m_listeners.end(); ++it)
	{
		(*it)->OnUnloadComplete(pLevel);
	}
}

//------------------------------------------------------------------------
void CLevelSystem::OnLoadingProgress(int steps) 
{
	float fProgress = (float)gEnv->p3DEngine->GetLoadedObjectCount();

  m_fFilteredProgress = min(m_fFilteredProgress, fProgress);

  float fFrameTime = gEnv->pTimer->GetAsyncCurTime() - m_fLastTime;

  float t = CLAMP(fFrameTime*.25f, 0.0001f, 1.0f);

  m_fFilteredProgress = fProgress*t + m_fFilteredProgress*(1.f-t);

  m_fLastTime = gEnv->pTimer->GetAsyncCurTime();

	OnLoadingProgress( m_pLoadingLevelInfo, (int)m_fFilteredProgress );
}

//------------------------------------------------------------------------
string& CLevelSystem::UnifyName(string& name)
{
	//name.MakeLower();
	name.replace('\\', '/');

	return name;
}

//////////////////////////////////////////////////////////////////////////
void CLevelSystem::LogLoadingTime()
{
	if (gEnv->IsEditor())
		return;

	if (!GetISystem()->IsDevMode())
		return;

#if defined(WIN32) && !defined(XENON)		
	CDebugAllowFileAccess ignoreInvalidFileAccess;

	string filename = gEnv->pSystem->GetRootFolder();
	filename += "Game_LevelLoadTime.log";

	FILE *file = fxopen(filename,"at");
	if (!file)
		return;

	char vers[128];
	GetISystem()->GetFileVersion().ToString(vers);

	const char *sChain = "";
	if (m_nLoadedLevelsCount > 0)
		sChain = " (Chained)";

	string text;
	text.Format( "\n[%s] Level %s loaded in %d seconds%s",vers,m_lastLevelName.c_str(),(int)m_fLastLevelLoadTime,sChain );
	fwrite( text.c_str(),text.length(),1,file );
	fclose(file);

#endif // Only log on windows.
}

void CLevelSystem::GetMemoryUsage(ICrySizer * pSizer) const
{
	pSizer->AddObject(this, sizeof(*this));
	pSizer->AddObject(m_levelInfos);	
	pSizer->AddObject(m_levelsFolder);
	pSizer->AddObject(m_listeners);		
}

void CLevelInfo::GetMemoryUsage(ICrySizer * pSizer) const
{
	pSizer->AddObject(m_levelName);
	pSizer->AddObject(m_levelPath);
	pSizer->AddObject(m_levelPaks);
	//pSizer->AddObject(m_musicLibs);	
	pSizer->AddObject(m_gamerules);	
	pSizer->AddObject(m_gameTypes);	
}

#ifndef NO_LIVECREATE

static bool SubsituteFile(const string& srcPath, const string& destPath)
{
	FILE* src = gEnv->pCryPak->FOpen(srcPath.c_str(), "rb", ICryPak::FOPEN_ONDISK);
	if (!src)
	{
		CryLogAlways("SubsituteFile: Failed to open source file '%s'", srcPath.c_str());
		return false;
	}

	FILE* dst = gEnv->pCryPak->FOpen(destPath.c_str(), "wb", ICryPak::FOPEN_ONDISK);
	if (!dst)
	{
		CryLogAlways("SubsituteFile: Failed to open destination file '%s'", destPath.c_str());
		gEnv->pCryPak->FClose(src);
		return false;
	}

	const uint32 CHUNK_SIZE = 64*1024;
	char *buf = new char[CHUNK_SIZE];
	size_t readBytes = 0;
	size_t writtenBytes = 0;
	while (!gEnv->pCryPak->FEof(src))
	{
		readBytes = gEnv->pCryPak->FReadRaw(buf, sizeof(char), CHUNK_SIZE, src);
		writtenBytes = gEnv->pCryPak->FWrite(buf, sizeof(char), readBytes, dst);

		if(readBytes != writtenBytes)
		{
			gEnv->pCryPak->FClose(src);
			gEnv->pCryPak->FClose(dst);

			delete[] buf;
			return false;
		}
	}

	delete[] buf;
	gEnv->pCryPak->FClose(src);
	gEnv->pCryPak->FClose(dst);
	return true;
}

#endif

//////////////////////////////////////////////////////////////////////////
bool CLevelInfo::OpenLevelPak()
{
	LOADING_TIME_PROFILE_SECTION;
	//////////////////////////////////////////////////////////////////////////
	//LiveCreate level reloading functionality
#ifndef NO_LIVECREATE
	// Overwrite level.pak
	{
		const string newLevelPak = m_levelPath + string("/_level.pak");
		const string curLevelPak = m_levelPath + string("/level.pak");

		if (SubsituteFile(newLevelPak, curLevelPak))
		{
			if (!gEnv->pCryPak->RemoveFile(newLevelPak.c_str()))	
			{
				gEnv->pLog->LogWarning("Cleanup failed: _level.pak not removed");
			}
		}
	}

	// Overwrite terraintextures.pak
	{
		const string newLevelPak = m_levelPath + string("/_terraintexture.pak");
		const string curLevelPak = m_levelPath + string("/terraintexture.pak");

		if (SubsituteFile(newLevelPak, curLevelPak))
		{
			if (!gEnv->pCryPak->RemoveFile(newLevelPak.c_str()))
			{
				gEnv->pLog->LogWarning("Cleanup failed: terraintexture.pak not removed");
			}
		}
	}

#endif
	//////////////////////////////////////////////////////////////////////////

	string levelpak = m_levelPath + string("/level.pak");
	CryFixedStringT<ICryPak::g_nMaxPath> fullLevelPakPath;
	bool bOk = gEnv->pCryPak->OpenPack( levelpak,(unsigned)0, NULL, &fullLevelPakPath );
	m_levelPakFullPath.assign(fullLevelPakPath.c_str());
	if (bOk)
	{
		string levelmmpak = m_levelPath + string("/levelmm.pak");
		if(gEnv->pCryPak->IsFileExist(levelmmpak))
		{
			gEnv->pCryPak->OpenPack( levelmmpak,(unsigned)0, NULL, &fullLevelPakPath );
			m_levelMMPakFullPath.assign(fullLevelPakPath.c_str());
		}
	}

	gEnv->pCryPak->SetPacksAccessibleForLevel( GetName() );

	return bOk;
}

//////////////////////////////////////////////////////////////////////////
void CLevelInfo::CloseLevelPak()
{
	LOADING_TIME_PROFILE_SECTION;
	if(!m_levelPakFullPath.empty())
	{
		gEnv->pCryPak->ClosePack(m_levelPakFullPath.c_str(), ICryPak::FLAGS_PATH_REAL);
		stl::free_container(m_levelPakFullPath);
	}

	if(!m_levelMMPakFullPath.empty())
	{
		gEnv->pCryPak->ClosePack(m_levelMMPakFullPath.c_str(), ICryPak::FLAGS_PATH_REAL);
		stl::free_container(m_levelMMPakFullPath);
	}
}

const bool CLevelInfo::IsOfType( const char* sType ) const
{
	for (unsigned int i = 0; i < m_levelTypeList.size(); ++i)
	{
		if(strcmp(m_levelTypeList[i], sType) == 0)	
			return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
void CLevelSystem::SaveOpenedFilesList()
{
	if (!m_pLoadingLevelInfo)
		return;

	// Write resource list to file.
	string filename = PathUtil::Make( m_pLoadingLevelInfo->GetPath(),"resourcelist.txt" );
	FILE *file = fxopen( filename.c_str(),"wt",true );
	if (file)
	{
		IResourceList *pResList = gEnv->pCryPak->GetResourceList(ICryPak::RFOM_Level);
		for (const char *fname = pResList->GetFirst(); fname; fname = pResList->GetNext())
		{
			fprintf( file,"%s\n",fname );
		}
		fclose(file);
	}
}

//////////////////////////////////////////////////////////////////////////
void CLevelSystem::UnLoadLevel()
{
	if (gEnv->IsEditor())
		return;
	if (!m_pLoadingLevelInfo)
		return;

#ifdef PS3
	bool flipIssued = false;		//enable flipping for PS3 TRC
	if(gEnv->pRenderer)
	{
		gPS3Env->bForce10HzFlip = 0;
		gEnv->pRenderer->ForceSwapBuffers();
		flipIssued = true;
	}
#endif
	CryLog( "UnLoadLevel Start" );
	INDENT_LOG_DURING_SCOPE();

	CTimeValue tBegin = gEnv->pTimer->GetAsyncTime();

	// One last update to execute pending requests.
	// Do this before the EntitySystem resets!
	if (gEnv->pFlowSystem)
	{
		gEnv->pFlowSystem->Update();
	}

	I3DEngine* p3DEngine = gEnv->p3DEngine;
	if ( p3DEngine )
	{
		IDeferredPhysicsEventManager* pPhysEventManager = p3DEngine->GetDeferredPhysicsEventManager();
		if ( pPhysEventManager )
		{
			// clear deferred physics queues before renderer, since we could have spu jobs running
			// which access a rendermesh
			pPhysEventManager->ClearDeferredEvents();
		}
	}

	//AM: Flush render thread (Flush is not exposed - using EndFrame())
	//We are about to delete resources that could be in use
	if (gEnv->pRenderer)
	{
		gEnv->pRenderer->EndFrame();

		// force a black screen as last render command
		gEnv->pRenderer->BeginFrame();
		gEnv->pRenderer->SetState(GS_BLSRC_SRCALPHA|GS_BLDST_ONEMINUSSRCALPHA|GS_NODEPTHTEST);
		gEnv->pRenderer->Draw2dImage(0, 0, 800, 600, -1, 0.0f,0.0f,1.0f,1.0f,	0.f,
			0.0f, 0.0f, 0.0f, 1.0 ,0.f); 
		gEnv->pRenderer->EndFrame();
		
		//flush any outstanding texture requests
		gEnv->pRenderer->FlushPendingTextureTasks();
	}

	// Disable filecaching during level unloading
	// will be reenabled when we get back to the IIS (frontend directly)
	// or after level loading is finished (via system event system)
	gEnv->pSystem->GetPlatformOS()->AllowOpticalDriveUsage(false);

#ifdef XENON
	// suspend the device and when renderer restored resume it to meet TCR: 022
	gEnv->pRenderer->SuspendDevice();
#endif

	if (gEnv->pScriptSystem)
	{
		gEnv->pScriptSystem->ResetTimers();
	}

	CCryAction* pCryAction = CCryAction::GetCryAction();
	if (pCryAction)
	{
		IItemSystem* pItemSystem = pCryAction->GetIItemSystem();
		pItemSystem->ClearGeometryCache();
		pItemSystem->ClearSoundCache();
		pItemSystem->Reset();

		if(ICooperativeAnimationManager* pCoopAnimManager = pCryAction->GetICooperativeAnimationManager())
		{
			pCoopAnimManager->Reset();
		}

		pCryAction->ClearBreakHistory();

		// Custom actions (Active + Library) keep a smart ptr to flow graph and need to be cleared before get below to the gEnv->pFlowSystem->Reset(true);
		ICustomActionManager* pCustomActionManager = pCryAction->GetICustomActionManager();
		if (pCustomActionManager)
		{
			pCustomActionManager->ClearActiveActions();
			pCustomActionManager->ClearLibraryActions();
		}

		IGameVolumes* pGameVolumes = pCryAction->GetIGameVolumesManager();
		if ( pGameVolumes )
		{
			pGameVolumes->Reset();
		}
	}
	
	if (gEnv->pEntitySystem)
	{
		gEnv->pEntitySystem->Unload(); // This needs to be called for editor and game, otherwise entities won't be released
	}

	if (pCryAction)
	{
		// Custom events are used by prefab events and should have been removed once entities which have the prefab flownodes have been destroyed
		// Just in case, clear all the events anyway
		ICustomEventManager* pCustomEventManager = pCryAction->GetICustomEventManager();
		CRY_ASSERT(pCustomEventManager != NULL);
		pCustomEventManager->Clear();

		pCryAction->OnActionEvent(SActionEvent(eAE_unloadLevel));
		pCryAction->ClearPhysicsQueues();
		pCryAction->GetTimeOfDayScheduler()->Reset();
	}

	// reset a bunch of subsystems
	if (gEnv->pDialogSystem)
	{
		gEnv->pDialogSystem->Reset(true);
	}

	if (gEnv->pGame && gEnv->pGame->GetIGameFramework())
	{
		gEnv->pGame->GetIGameFramework()->GetIMaterialEffects()->Reset(true);
	}

	if (gEnv->pAISystem)
	{
		gEnv->pAISystem->FlushSystem(true);
		gEnv->pAISystem->Reset(IAISystem::RESET_EXIT_GAME);
		gEnv->pAISystem->Reset(IAISystem::RESET_UNLOAD_LEVEL);
	}	

	if (gEnv->pMovieSystem)
	{
		gEnv->pMovieSystem->Reset(false,false);
		gEnv->pMovieSystem->RemoveAllSequences();
	}

	// Unload level specific audio binary data.
	SAudioManagerRequestData<eAMRT_UNLOAD_AFCM_DATA_BY_SCOPE> oAMData(eADS_LEVEL_SPECIFIC);
	SAudioRequest oAudioRequestData;
	oAudioRequestData.nFlags = eARF_PRIORITY_HIGH | eARF_EXECUTE_BLOCKING;
	oAudioRequestData.pData = &oAMData;
	gEnv->pAudioSystem->PushRequest(oAudioRequestData);

	// Now unload level specific audio config data.
	SAudioManagerRequestData<eAMRT_CLEAR_CONTROLS_DATA> oAMData2(eADS_LEVEL_SPECIFIC);
	oAudioRequestData.pData = &oAMData2;
	gEnv->pAudioSystem->PushRequest(oAudioRequestData);

	SAudioManagerRequestData<eAMRT_CLEAR_PRELOADS_DATA> oAMData3(eADS_LEVEL_SPECIFIC);
	oAudioRequestData.pData = &oAMData3;
	gEnv->pAudioSystem->PushRequest(oAudioRequestData);

	// Delete engine resources	
	if (p3DEngine)
	{
		p3DEngine->UnloadLevel();
	}

	if (pCryAction)
	{
		IGameObjectSystem* pGameObjectSystem = pCryAction->GetIGameObjectSystem();
		pGameObjectSystem->Reset();
	}

	IGameTokenSystem* pGameTokenSystem = CCryAction::GetCryAction()->GetIGameTokenSystem();
	pGameTokenSystem->RemoveLibrary("Level");
	pGameTokenSystem->Unload();

	if (gEnv->pFlowSystem)
	{
		gEnv->pFlowSystem->Reset(true);
	}

	if (gEnv->pEntitySystem)
	{
		gEnv->pEntitySystem->PurgeHeaps(); // This needs to be called for editor and game, otherwise entities won't be released
	}

	// Reset the camera to (0,0,0) which is the invalid/uninitialised state
	CCamera defaultCam;
	m_pSystem->SetViewCamera(defaultCam);

	if (pCryAction)
	{
		pCryAction->OnActionEvent(SActionEvent(eAE_postUnloadLevel));
	}

	OnUnloadComplete(m_pCurrentLevel);

	// -- kenzo: this will close all pack files for this level 
	// (even the ones which were not added through here, if this is not desired,
	// then change code to close only level.pak)
	if( m_pLoadingLevelInfo )
	{
		((CLevelInfo*)m_pLoadingLevelInfo)->CloseLevelPak();
		m_pLoadingLevelInfo = NULL;
	}	

	stl::free_container(m_lastLevelName);

	GetISystem()->GetIResourceManager()->UnloadLevel();

	SAFE_RELEASE(m_pCurrentLevel);

  // Force to clean render resources left after deleting all objects and materials.
	IRenderer* pRenderer = gEnv->pRenderer;
  if(pRenderer)
	{
		pRenderer->FlushRTCommands(true, true, true);

    CryComment("Deleting Render meshes, render resources and flush texture streaming");
    // This may also release some of the materials.
    int flags = FRR_DELETED_MESHES | FRR_FLUSH_TEXTURESTREAMING | FRR_OBJECTS | FRR_RENDERELEMENTS | FRR_RP_BUFFERS | FRR_POST_EFFECTS;

    // Always keep the system resources around in the editor.
    //#ifdef SHUTDOWN_RENDERER_BETWEEN_LEVELS
    if (!gEnv->IsEditor())
      flags |= FRR_SYSTEM_RESOURCES;
    //#endif

    pRenderer->FreeResources(flags);
    CryComment("done");
  }

	m_bLevelLoaded = false;

	CTimeValue tUnloadTime = gEnv->pTimer->GetAsyncTime() - tBegin;
	CryLog( "UnLoadLevel End: %.1f sec", tUnloadTime.GetSeconds() );

#ifdef PS3
	if(flipIssued && gEnv->pRenderer)
	{
		gEnv->pRenderer->ForceSwapBuffers();	//disable flipping for PS3 TRC
		gPS3Env->bForce10HzFlip = 2;//flag for upcoming level load to reenable
	}
#endif

	// Must be sent last.
	// Cleanup all containers 
	GetISystem()->GetISystemEventDispatcher()->OnSystemEvent(ESYSTEM_EVENT_LEVEL_POST_UNLOAD, 0, 0);
}

//////////////////////////////////////////////////////////////////////////
ILevelRotation* CLevelSystem::FindLevelRotationForExtInfoId(const ILevelRotation::TExtInfoId findId)
{
	CLevelRotation*  pRot = NULL;

	TExtendedLevelRotations::iterator  begin = m_extLevelRotations.begin();
	TExtendedLevelRotations::iterator  end = m_extLevelRotations.end();
	for (TExtendedLevelRotations::iterator i = begin; i != end; ++i)
	{
		CLevelRotation*  pIterRot = &(*i);
		if (pIterRot->GetExtendedInfoId() == findId)
		{
			pRot = pIterRot;
			break;
		}
	}

	return pRot;
}

//////////////////////////////////////////////////////////////////////////
bool CLevelSystem::AddExtendedLevelRotationFromXmlRootNode(const XmlNodeRef rootNode, const char* altRootTag, const ILevelRotation::TExtInfoId extInfoId)
{
	bool  ok = false;

	CRY_ASSERT(extInfoId);

	if (!FindLevelRotationForExtInfoId(extInfoId))
	{
		m_extLevelRotations.push_back(CLevelRotation());
		CLevelRotation*  pRot = &m_extLevelRotations.back();

		if (pRot->LoadFromXmlRootNode(rootNode, altRootTag))
		{
			pRot->SetExtendedInfoId(extInfoId);
			ok = true;
		}
		else
		{
			LOCAL_WARNING(0, string().Format("Couldn't add extended level rotation with id '%u' because couldn't read the xml info root node for some reason!", extInfoId));
		}
	}
	else
	{
		LOCAL_WARNING(0, string().Format("Couldn't add extended level rotation with id '%u' because there's already one with that id!", extInfoId));
	}

	return ok;
}

//////////////////////////////////////////////////////////////////////////
void CLevelSystem::ClearExtendedLevelRotations()
{
	m_extLevelRotations.clear();
}

//////////////////////////////////////////////////////////////////////////
ILevelRotation * CLevelSystem::CreateNewRotation( const ILevelRotation::TExtInfoId id )
{
	CLevelRotation *pRotation = static_cast<CLevelRotation*>(FindLevelRotationForExtInfoId(id));

	if (!pRotation)
	{
		m_extLevelRotations.push_back(CLevelRotation());
		pRotation = &m_extLevelRotations.back();
		pRotation->SetExtendedInfoId(id);
	}
	else
	{
		pRotation->Reset();
		pRotation->SetExtendedInfoId(id);
	}

	return pRotation;
}

DynArray<string>* CLevelSystem::GetLevelTypeList()
{
	return &m_levelTypeList;
}



#undef LOCAL_WARNING

#include UNIQUE_VIRTUAL_WRAPPER(ILevelSystem)
#include UNIQUE_VIRTUAL_WRAPPER(ILevelInfo)
#include UNIQUE_VIRTUAL_WRAPPER(ILevelRotation)
