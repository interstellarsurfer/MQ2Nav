//
// PluginMain.cpp
//

#include "MQ2Navigation.h"

PreSetup("MQ2Nav");
PLUGIN_VERSION(2.00);
#define PLUGIN_NAME "MQ2Nav"

#include <memory>

//----------------------------------------------------------------------------

std::unique_ptr<MQ2NavigationPlugin> g_mq2Nav;

//----------------------------------------------------------------------------

PLUGIN_API void InitializePlugin()
{
	DebugSpewAlways("Initializing MQ2Nav");

	WriteChatf("\ag[MQ2Nav]\ax v%1.1f \ay(BETA)\ax by brainiac (\aohttps://github.com/brainiac/MQ2Nav\ax)", MQ2Version);

	g_mq2Nav.reset(new MQ2NavigationPlugin);
}

PLUGIN_API void ShutdownPlugin()
{
	DebugSpewAlways("Shutting down MQ2Nav");
	g_mq2Nav.reset();
}

PLUGIN_API void OnPulse()
{
	if (g_mq2Nav)
		g_mq2Nav->OnPulse();
}

PLUGIN_API void OnBeginZone()
{
	if (g_mq2Nav)
		g_mq2Nav->OnBeginZone();
}

PLUGIN_API void OnEndZone()
{
	//if (g_mq2Nav)
	//	g_mq2Nav->OnEndZone();
}

PLUGIN_API void OnZoned()
{
	// OnZoned occurs later than OnEndZone, when more data is available
	if (g_mq2Nav)
		g_mq2Nav->OnEndZone();
}

PLUGIN_API void SetGameState(DWORD GameState)
{
	if (g_mq2Nav)
		g_mq2Nav->SetGameState(GameState);
}

PLUGIN_API void OnAddGroundItem(PGROUNDITEM pNewGroundItem)
{
	if (g_mq2Nav)
		g_mq2Nav->OnAddGroundItem(pNewGroundItem);
}

PLUGIN_API void OnRemoveGroundItem(PGROUNDITEM pGroundItem)
{
	if (g_mq2Nav)
		g_mq2Nav->OnRemoveGroundItem(pGroundItem);
}
