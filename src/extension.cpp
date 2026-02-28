/**
 * =============================================================================
 * CS2VoiceFix
 * Copyright (C) 2024 Poggu
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include "extension.h"
#include <iserver.h>
#include <sourcehook_impl.h>

#include "utils/module.h"
#include <schemasystem/schemasystem.h>
#include <entity2/entitysystem.h>
#include "interfaces.h"
#include "networkstringtabledefs.h"
#include "netmessages.pb.h"
#include "networksystem/inetworkmessages.h"
#include "schema/serversideclient.h"

#include "funchook.h"

#ifdef _WIN32
#define ROOTBIN "/bin/win64/"
#define GAMEBIN "/csgo/bin/win64/"
#else
#define ROOTBIN "/bin/linuxsteamrt64/"
#define GAMEBIN "/csgo/bin/linuxsteamrt64/"
#endif

#undef max

CS2VoiceFix g_CS2VoiceFix;

SourceHook::Impl::CSourceHookImpl source_hook_impl;
SourceHook::ISourceHook* source_hook = &source_hook_impl;

int source_hook_pluginid = 0;
uint64_t g_randomSeed = 0;

class GameSessionConfiguration_t
{
};

SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char*, uint64, const char*, const char*, bool);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t &, ISource2WorldSession *, const char *);

typedef bool (FASTCALL *SendNetMessage_t)(CServerSideClient *, CNetMessage*, NetChannelBufType_t);
bool FASTCALL Hook_SendNetMessage(CServerSideClient *pClient, CNetMessage *pData, NetChannelBufType_t bufType);
SendNetMessage_t g_pfnSendNetMessage = nullptr;
funchook_t *g_pSendNetMessageHook = nullptr;

#ifdef PLATFORM_WINDOWS
constexpr int g_iSendNetMessageOffset = 15;
#else
constexpr int g_iSendNetMessageOffset = 16;
#endif

CGameEntitySystem* GameEntitySystem()
{
#ifdef WIN32
	static int offset = 88;
#else
	static int offset = 80;
#endif
	return *reinterpret_cast<CGameEntitySystem**>((uintptr_t)(g_pGameResourceServiceServer)+offset);
}

// Should only be called within the active game loop (i e map should be loaded and active)
// otherwise that'll be nullptr!
CGlobalVars* GetGameGlobals()
{
	INetworkGameServer* server = g_pNetworkServerService->GetIGameServer();

	if (!server)
		return nullptr;

	return g_pNetworkServerService->GetIGameServer()->GetGlobals();
}

void SetupHook(CS2VoiceFix* plugin)
{
	CModule engineModule(ROOTBIN, "engine2");

	void **pServerSideClientVTable = (void **)engineModule.FindVirtualTable("CServerSideClient");
	g_pfnSendNetMessage = (SendNetMessage_t)pServerSideClientVTable[g_iSendNetMessageOffset];

	g_pSendNetMessageHook = funchook_create();
	funchook_prepare(g_pSendNetMessageHook, (void**)&g_pfnSendNetMessage, (void*)Hook_SendNetMessage);
	funchook_install(g_pSendNetMessageHook, 0);
}

PLUGIN_EXPOSE(CS2VoiceFix, g_CS2VoiceFix);
bool CS2VoiceFix::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, Interfaces::engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, Interfaces::icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, Interfaces::server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, Interfaces::gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, Interfaces::networkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION);
	g_SMAPI->AddListener( this, this );

	SH_ADD_HOOK(IServerGameClients, OnClientConnected, Interfaces::gameclients, SH_MEMBER(this, &CS2VoiceFix::Hook_OnClientConnected), false);
	SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &CS2VoiceFix::Hook_StartupServer), true);

	SetupHook(this);

	g_pCVar = Interfaces::icvar;
	ConVar_Register( FCVAR_RELEASE | FCVAR_GAMEDLL );

	srand(time(NULL));
	// just random numbers to create bogus "steam ids" in voice data
	g_randomSeed = 800 + rand() % ((0x00FFFFFFFFFFFFFF) - 800);

	return true;
}

uint64_t g_playerIds[64];

bool FASTCALL Hook_SendNetMessage(CServerSideClient *pClient, CNetMessage *pData, NetChannelBufType_t bufType){

	NetMessageInfo_t* info = pData->GetNetMessage()->GetNetMessageInfo();
	if (info)
	{
		if (info->m_MessageId == SVC_Messages::svc_VoiceData)
		{
			auto msg = pData->ToPB<CSVCMsg_VoiceData>();

			auto playerSlot = msg->client();
			//ConMsg("%i -> %i (%llu)\n", playerSlot, client->GetPlayerSlot().Get(), g_playerIds[client->GetPlayerSlot().Get()] + playerSlot);

			msg->set_xuid(g_playerIds[pClient->GetPlayerSlot().Get()] + playerSlot);
		}
	}

	return g_pfnSendNetMessage(pClient, pData, bufType);
}

void CS2VoiceFix::Hook_OnClientConnected(CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, const char* pszAddress, bool bFakePlayer)
{
	g_playerIds[slot.Get()] = g_randomSeed;
	g_randomSeed += 66;
}

void CS2VoiceFix::Hook_StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*)
{
	Interfaces::g_pEntitySystem = GameEntitySystem();
}

bool CS2VoiceFix::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(IServerGameClients, OnClientConnected, Interfaces::gameclients, SH_MEMBER(this, &CS2VoiceFix::Hook_OnClientConnected), false);
	SH_REMOVE_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &CS2VoiceFix::Hook_StartupServer), true);

	if (g_pSendNetMessageHook)
	{
		funchook_uninstall(g_pSendNetMessageHook, 0);
		funchook_destroy(g_pSendNetMessageHook);
	}

	return true;
}

const char *CS2VoiceFix::GetLicense()
{
	return "GPLv3";
}

const char *CS2VoiceFix::GetVersion()
{
	return "1.0.2";
}

const char *CS2VoiceFix::GetDate()
{
	return __DATE__;
}

const char *CS2VoiceFix::GetLogTag()
{
	return "CS2VoiceFix";
}

const char *CS2VoiceFix::GetAuthor()
{
	return "Poggu, maintained by Slynx";
}

const char *CS2VoiceFix::GetDescription()
{
	return "Fixes voice chat breaking on addon unload";
}

const char *CS2VoiceFix::GetName()
{
	return "CS2VoiceFix";
}

const char *CS2VoiceFix::GetURL()
{
	return "https://slynxdev.cz";
}
