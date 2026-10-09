// vip_sd — Plugify port of the Pisex VIP module "VIP_ShowDamage"
// (ref/src/cs2-vip-modules/VIP_ShowDamage/source/vip_sd.cpp, 133 lines).
//
// Behaviour kept identical: a pre hook on the "player_hurt" game event prints the damage the
// attacker just dealt ("-<dmg_health>") to the attacker's screen centre, but only when the
// attacker is a VIP with the "show_damage" feature on and the victim is on another team.
//
// Port notes:
//   * The core (IVIPApi) is the Plugify "vip" plugin (a hard dependency); the module calls it
//     through the consumer wrapper of plugins/vip/include/vip_api.hpp, so the g_pVIPCore calls
//     below are the original's, one to one.
//   * The Metamod event fields "attacker"/"userid" become player slots through
//     pisex::IGameEvent::GetPlayerSlot (s2sdk GetEventPlayerSlot, the engine's own translation of
//     the user id) - the CCSPlayerController::FromSlot() of the original.
//   * The team comparison reads the controllers' m_iTeamNum through
//     pisex::g_pPlayers->GetPlayerTeam (s2sdk GetClientTeam -> CBasePlayerController::GetTeam).
//   * The original registered the event hook in VIP_OnVIPLoaded, i.e. once the core's database is
//     up; the port does the same in OnCoreReady, and additionally polls VIP_IsVIPLoaded once for
//     the late-load case (the API fires no callback to a module that registers after the core
//     finished loading).
//   * VIP_PrintToCenter is the core's variadic wrapper: it formats "%i" into a 256 byte buffer
//     (the original's buffer size) and forwards the finished string.

#include <pisex/pisex.hpp>

#include <plg/plugin.hpp>

#include <vip_api.hpp>

#include <chrono>
#include <cstdint>
#include <exception>

namespace {

// The GetLogTag() of the original ("[VIP-SD]"); the printers add the brackets.
constexpr char kLogTag[] = "VIP-SD";

// The object the ported module uses (the Pisex g_pVIPCore).
vip::IVIPApi* g_pVIPCore = &vip::IVIPApi::Get();

class VipShowDamagePlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

	// VIP_OnVIPLoaded of the original: the core's database is up, the hook may run.
	void OnCoreReady();

private:
	void OnPlayerHurt(const char* szName, pisex::IGameEvent* pEvent, bool bDontBroadcast);

	bool m_coreReady = false;
	bool m_hookRegistered = false;
};

VipShowDamagePlugin g_plugin;

void VipShowDamagePlugin::OnPlayerHurt(const char* szName, pisex::IGameEvent* pEvent, bool bDontBroadcast) {
	(void)szName;
	(void)bDontBroadcast;
	if (!pEvent) {
		return;
	}

	// int iSlot = pEvent->GetPlayerSlot("attacker").Get();
	// int victimIndex = pEvent->GetPlayerSlot("userid").Get();
	const int iAttacker = pEvent->GetPlayerSlot("attacker");
	const int iVictim = pEvent->GetPlayerSlot("userid");

	// CCSPlayerController::FromSlot(iSlot) / FromSlot(victimIndex) being null.
	if (iAttacker < 0 || iVictim < 0) {
		return;
	}
	if (pisex::GetControllerEntity(iAttacker) == 0 || pisex::GetControllerEntity(iVictim) == 0) {
		return;
	}

	const int iAttackerTeam = pisex::GetPlayerTeam(iAttacker);
	const int iVictimTeam = pisex::GetPlayerTeam(iVictim);
	if (iAttackerTeam == iVictimTeam) {
		return;
	}

	if (g_pVIPCore->VIP_IsClientVIP(iAttacker) && g_pVIPCore->VIP_GetClientFeatureBool(iAttacker, "show_damage")) {
		g_pVIPCore->VIP_PrintToCenter(iAttacker, "-%i", pEvent->GetInt("dmg_health"));
	}
}

// --- lifecycle ---------------------------------------------------------------------------------

plg::PluginResult VipShowDamagePlugin::OnPluginStart() {
	try {
		// "vip" makes pisexcompat resolve the module's Metamod paths ("addons/configs/vip/...")
		// through the shared <configs>/vip directory of the core.
		pisex::Init("vip");

		g_pVIPCore->VIP_OnVIPLoaded([] { g_plugin.OnCoreReady(); });
		if (g_pVIPCore->VIP_IsVIPLoaded()) {
			OnCoreReady();
		}

		// VIP_RegisterFeature("show_damage", VIP_BOOL, TOGGLABLE) of AllPluginsLoaded.
		g_pVIPCore->VIP_RegisterFeature("show_damage", vip::VIP_BOOL, vip::TOGGLABLE);

		return {};
	} catch (const std::exception& ex) {
		pisex::ErrorLog("[%s] OnPluginStart failed: %s", kLogTag, ex.what());
	} catch (...) {
		pisex::ErrorLog("[%s] OnPluginStart failed with an unknown exception", kLogTag);
	}
	return {}; // never Failed: that stops every plugify plugin
}

plg::PluginResult VipShowDamagePlugin::OnPluginUpdate(std::chrono::milliseconds dt) {
	(void)dt;
	try {
		pisex::Poll();
	} catch (const std::exception& ex) {
		pisex::ErrorLog("[%s] OnPluginUpdate failed: %s", kLogTag, ex.what());
	} catch (...) {
		pisex::ErrorLog("[%s] OnPluginUpdate failed with an unknown exception", kLogTag);
	}
	return {};
}

plg::PluginResult VipShowDamagePlugin::OnPluginEnd() {
	try {
		// Unregister the trampoline that points into this library before it is unmapped.
		g_pVIPCore->VIP_OnVIPLoadedUnregister();
		pisex::Shutdown();
	} catch (...) {
		// nothing left to do at unload
	}
	return {};
}

void VipShowDamagePlugin::OnCoreReady() {
	if (m_coreReady) {
		return;
	}
	m_coreReady = true;
	if (!m_hookRegistered) {
		m_hookRegistered = true;
		// g_pUtils->HookEvent(g_PLID, "player_hurt", OnPlayerHurt) of VIP_OnVIPLoaded.
		pisex::g_pUtils->HookEvent("player_hurt", [](const char* szName, pisex::IGameEvent* pEvent,
													 bool bDontBroadcast) {
			g_plugin.OnPlayerHurt(szName, pEvent, bDontBroadcast);
		});
	}
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
