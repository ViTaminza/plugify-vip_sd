// vip_sd — Plugify port of the Pisex VIP module "VIP_ShowDamage" / "[VIP] Show Damage"
// (the original's vip_sd.cpp, 133 lines).
//
// Behaviour kept identical: a pre hook on the "player_hurt" game event prints the health damage
// the attacker just dealt ("-<dmg_health>") to the attacker's screen centre, but only when the
// attacker is a VIP with the "show_damage" feature on and the victim is on another team.
//
// Pure s2sdk (docs/PLUGIN_STANDARD.md §7): there is no pisexcompat and no s2guard. Every safety
// rule the plugin needs lives in this file:
//   * PITFALLS B1/B2 - the map gate g_mapReady. The OnMapStart/OnMapEnd listeners are registered
//     in OnPluginStart, readiness is latched by OnMapStart and by the first OnPluginUpdate of a
//     plugin loaded into a running map (when no OnMapEnd was seen since OnPluginStart), and
//     dropped by OnMapEnd. No client, entity or schema call is made while the gate is closed:
//     s2sdk dereferences a torn-down client list in the OnMapEnd..OnMapStart window and a null
//     serializer map before the first map.
//   * PITFALLS C12/C13 - "player_hurt" is hooked with s2sdk::HookEvent(HookMode::Pre) in
//     OnPluginStart and unhooked in OnPluginEnd; the handler answers Continue on every path and
//     reads the fields with GetEventPlayerSlot/GetEventInt when the event arrives (nothing about
//     an entity is cached across a changelevel).
//   * PITFALLS C2 - HasClient() layers the gate, the slot range, IsClientConnected, IsClientInGame
//     and !IsClientSourceTV; HasController() adds the non-null PlayerSlotToEntPointer - the
//     original's CCSPlayerController::FromSlot(iSlot) chain. The team is read from the controller
//     (CBaseEntity::m_iTeamNum, 1 byte) exactly like the original's pAttacker->m_iTeamNum().
//   * PITFALLS C8 - the m_iTeamNum schema offset is resolved only with a live map, and only a
//     successful lookup is cached (the caller keeps asking until the class is available).
//   * PITFALLS A1/A7 - OnPluginStart never returns Failed (one Failed stops every Plugify plugin)
//     and OnPluginEnd removes every hook/listener it registered, so `plg reload` is safe.
//
// Port notes:
//   * The core (IVIPApi) is the Plugify "vip" plugin (a hard dependency); the module calls it
//     through the consumer wrapper of plugins/vip/include/vip_api.hpp.
//   * The original hooked "player_hurt" from VIP_OnVIPLoaded, i.e. only once the core's database
//     was up. The port hooks it in OnPluginStart like the other vip modules: the handler asks the
//     core per event, and the core answers "not a VIP" / false while its records are not loaded,
//     so nothing is printed early, while a hook registered late would miss every hurt in between.
//   * VIP_PrintToCenter of the original is the core's variadic wrapper around the Pisex utils
//     PrintToCenter; the port formats the same "-%i" string and calls the s2sdk export directly
//     (s2sdk calls it PrintCenterText), behind PrintToCenter()/HasClient().
//   * The original read the raw m_iTeamNum of both controllers. s2sdk's GetClientTeam goes through
//     the pawn and answers 0 (with a console warning) for every client without a current pawn, so
//     the port keeps reading the controller field, like pisexcompat's GetPlayerTeam did.
//   * The original printed "-0" for a "player_hurt" that carried no damage at all. The port reads
//     both dmg_health and dmg_armor and drops such an event before it reaches the screen (nothing
//     to show); a hit that only hit armour still prints "-0", exactly like the original.

#include <plg/plugin.hpp>
#include <plugin_export.h>
#include <s2sdk/s2sdk.hpp>

#include <vip_api.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace {

// The original code's unqualified spellings (VIP_BOOL, TOGGLABLE, ...).
using namespace vip;

// The GetLogTag() of the original ("[VIP-SD]"); the log lines add the brackets.
constexpr char kLogTag[] = "VIP-SD";

// The feature of the original: a bool the player toggles in the VIP menu.
constexpr char kFeature[] = "show_damage";

// CS2 has exactly 64 player slots; the original iterated them through CCSPlayerController.
constexpr int32_t kMaxSlots = 64;

// The object the ported module uses (the Pisex g_pVIPCore).
IVIPApi* g_pVIPCore = &IVIPApi::Get();

void LogInfo(std::string_view message) {
	if (plg::Log) {
		plg::Log(message, plg::Severity::Info, plg::source_location::current());
	}
}

void LogError(std::string_view message) {
	if (plg::Log) {
		plg::Log(message, plg::Severity::Error, plg::source_location::current());
	}
}

// The original's ErrorLog("[%s] ...", GetLogTag()).
void LogTaggedError(const std::string& message) {
	LogError(std::string("[") + kLogTag + "] " + message);
}

// --- PITFALLS B1/B2: the map gate --------------------------------------------------------------

bool g_mapReady = false;              // a map is up: s2sdk client/entity/schema calls are safe
bool g_mapStartSeen = false;          // OnMapStart arrived since start / the last OnMapEnd
bool g_mapEndSeen = false;            // OnMapEnd arrived since start
bool g_mapListenersRegistered = false; // OnMapStart/OnMapEnd are registered with s2sdk

// --- PITFALLS C12/C13: the game-event hook -----------------------------------------------------

bool g_playerHurtHooked = false; // "player_hurt" is hooked (and must be unhooked on unload)

// s2sdk listeners are plain function pointers called on the game thread: they must not throw and
// OnMapEnd must not touch a client (the client list is torn down then).
void OnMapStartListener() {
	// The map's first live frame: clients, entities and the serializer are in place.
	g_mapReady = true;
	g_mapStartSeen = true;
	g_mapEndSeen = false;
}

void OnMapEndListener() {
	// PITFALLS B2: from here until the next OnMapStart every s2sdk client query can crash (the
	// client list is being torn down), so this handler only drops state.
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = true;
}

void InitMapGate() {
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = false;
	if (g_mapListenersRegistered) {
		return;
	}
	if (__s2sdk_OnMapStart_Register == nullptr || __s2sdk_OnMapEnd_Register == nullptr) {
		// Without the map-end listener nothing could ever take readiness away again, so the gate
		// stays closed for good (PITFALLS B2: log and stay inactive, never return Failed).
		LogTaggedError("the s2sdk map listeners are unavailable - damage display stays disabled");
		return;
	}
	s2sdk::OnMapStart_Register(&OnMapStartListener);
	s2sdk::OnMapEnd_Register(&OnMapEndListener);
	g_mapListenersRegistered = true;
}

// The mid-map latch of the gate: a plugin loaded into a running map never saw OnMapStart, so its
// first update is the "a map is up" signal (the host pumps updates from the server frame). After
// an OnMapEnd only a new OnMapStart re-opens the gate: the host keeps pumping updates through a
// changelevel, so `!g_mapEndSeen` alone must not be enough once one was seen.
void PollMapGate() {
	if (!g_mapListenersRegistered) {
		return;
	}
	if (!g_mapReady && (g_mapStartSeen || !g_mapEndSeen)) {
		g_mapReady = true;
	}
}

void ShutdownMapGate() {
	if (g_mapListenersRegistered) {
		g_mapListenersRegistered = false;
		if (__s2sdk_OnMapStart_Unregister != nullptr) {
			s2sdk::OnMapStart_Unregister(&OnMapStartListener);
		}
		if (__s2sdk_OnMapEnd_Unregister != nullptr) {
			s2sdk::OnMapEnd_Unregister(&OnMapEndListener);
		}
	}
	// A listener the engine may still hold for a moment must not reach a client.
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = false;
}

// --- PITFALLS C2: the client checks ------------------------------------------------------------

constexpr bool IsSlotInRange(int32_t iSlot) {
	return iSlot >= 0 && iSlot < kMaxSlots;
}

// A live, human-addressable slot: connected, in game and not the SourceTV proxy. Only a slot that
// passes this may be handed to a client print (PITFALLS C2).
bool HasClient(int32_t iSlot) {
	if (!g_mapReady || !IsSlotInRange(iSlot)) {
		return false;
	}
	// The s2sdk wrappers call their binding slots unchecked: a missing binding would be a null
	// call from inside the game event hook.
	if (__s2sdk_IsClientConnected == nullptr || __s2sdk_IsClientInGame == nullptr ||
		__s2sdk_IsClientSourceTV == nullptr) {
		return false;
	}
	try {
		return s2sdk::IsClientConnected(iSlot) && s2sdk::IsClientInGame(iSlot) &&
			   !s2sdk::IsClientSourceTV(iSlot);
	} catch (...) {
		return false;
	}
}

// The original's CCSPlayerController::FromSlot(iSlot) != nullptr: the client must also resolve to a
// controller entity, which is what the team read and the victim/attacker filter need.
bool HasController(int32_t iSlot) {
	if (!HasClient(iSlot) || __s2sdk_PlayerSlotToEntPointer == nullptr) {
		return false;
	}
	try {
		return s2sdk::PlayerSlotToEntPointer(iSlot) != nullptr;
	} catch (...) {
		return false;
	}
}

// PITFALLS C8: the s2sdk export must only run with a live map, and a failed lookup must not be
// cached (the caller keeps asking until the class is available).
int32_t SchemaOffset(const char* szClassName, const char* szFieldName) {
	if (__s2sdk_GetSchemaOffset == nullptr) {
		return -1;
	}
	try {
		return s2sdk::GetSchemaOffset(szClassName, szFieldName);
	} catch (...) {
		return -1;
	}
}

// The team lives on the controller (CBaseEntity::m_iTeamNum, 1 byte at the schema offset), what
// the original's pController->m_iTeamNum() read: s2sdk's GetClientTeam goes through the pawn and
// answered 0 - with a console warning - for every client without a current pawn. The offset is
// resolved only with a live map (PITFALLS C8) and only a successful lookup is cached.
int32_t GetPlayerTeam(int32_t iSlot) {
	if (!HasController(iSlot)) {
		return 0;
	}
	static int32_t s_iTeamOffset = -1;
	if (s_iTeamOffset < 0) {
		s_iTeamOffset = SchemaOffset("CBaseEntity", "m_iTeamNum");
	}
	if (s_iTeamOffset < 0 || __s2sdk_GetEntData2 == nullptr ||
		__s2sdk_PlayerSlotToEntPointer == nullptr) {
		return 0;
	}
	try {
		void* pController = s2sdk::PlayerSlotToEntPointer(iSlot);
		return pController != nullptr
				   ? static_cast<int32_t>(s2sdk::GetEntData2(pController, s_iTeamOffset, 1) & 0xFF)
				   : 0;
	} catch (...) {
		return 0;
	}
}

// The original's VIP_PrintToCenter (the Pisex utils PrintToCenter -> s2sdk PrintCenterText), behind
// the same client guard as every other client call of this plugin.
void PrintToCenter(int32_t iSlot, std::string_view text) {
	if (!HasClient(iSlot) || __s2sdk_PrintCenterText == nullptr) {
		return;
	}
	try {
		s2sdk::PrintCenterText(iSlot, plg::string(text));
	} catch (...) {
	}
}

class VipShowDamagePlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

	// The "player_hurt" body (an s2sdk event callback is a plain function pointer that cannot be a
	// member function, so it forwards here).
	void OnPlayerHurt(void* event);
};

VipShowDamagePlugin g_plugin;

// The single "player_hurt" hook. A Pre hook must answer Continue on every path: a result >=
// Handled frees the event for every other plugin on the server (the original's listener returned
// false for the same reason).
s2sdk::ResultType OnPlayerHurtEvent(const plg::string& /*name*/, void* event, bool /*dontBroadcast*/) {
	try {
		g_plugin.OnPlayerHurt(event);
	} catch (const std::exception& ex) {
		LogTaggedError(std::string("player_hurt failed: ") + ex.what());
	} catch (...) {
		LogTaggedError("player_hurt failed with an unknown exception");
	}
	return s2sdk::ResultType::Continue;
}

void VipShowDamagePlugin::OnPlayerHurt(void* event) {
	// Events keep arriving during a changelevel (PITFALLS C13) and every client query before the
	// first map crashes (PITFALLS B1/B2): the gate turns both into a no-op first.
	if (event == nullptr || !g_mapReady) {
		return;
	}
	// The s2sdk wrappers call their binding slots unchecked: a missing binding would be a null
	// call from inside the game event hook.
	if (__s2sdk_GetEventPlayerSlot == nullptr || __s2sdk_GetEventInt == nullptr) {
		return;
	}

	// The attacker's and the victim's slots from the event (the original's two lookups).
	const int32_t iAttacker = s2sdk::GetEventPlayerSlot(event, "attacker");
	const int32_t iVictim = s2sdk::GetEventPlayerSlot(event, "userid");
	if (!IsSlotInRange(iAttacker) || !IsSlotInRange(iVictim)) {
		return;
	}
	// CCSPlayerController::FromSlot(iSlot) / FromSlot(victimIndex) being null.
	if (!HasController(iAttacker) || !HasController(iVictim)) {
		return;
	}

	// The original's pAttacker->m_iTeamNum() vs. pVictim->m_iTeamNum().
	const int32_t iAttackerTeam = GetPlayerTeam(iAttacker);
	const int32_t iVictimTeam = GetPlayerTeam(iVictim);
	if (iAttackerTeam == iVictimTeam) {
		return;
	}

	if (!g_pVIPCore->VIP_IsClientVIP(iAttacker) ||
		!g_pVIPCore->VIP_GetClientFeatureBool(iAttacker, kFeature)) {
		return;
	}

	// The original printed dmg_health with a fixed "-%i" format. dmg_armor is read as well: an
	// event that carries neither health nor armour damage has nothing to show (the original
	// printed a meaningless "-0" for it); an armour-only hit still prints "-0", like the original.
	const int32_t iDamage = s2sdk::GetEventInt(event, "dmg_health");
	const int32_t iArmorDamage = s2sdk::GetEventInt(event, "dmg_armor");
	if (iDamage <= 0 && iArmorDamage <= 0) {
		return;
	}

	PrintToCenter(iAttacker, "-" + std::to_string(iDamage));
}

// --- lifecycle ---------------------------------------------------------------------------------

plg::PluginResult VipShowDamagePlugin::OnPluginStart() {
	try {
		// PITFALLS B1: registering the map listeners touches no client, so it is safe here.
		InitMapGate();

		// The original registered the "show_damage" feature (bool, togglable) in AllPluginsLoaded;
		// the core stores the entry whether or not its database is up yet. The vip module fills
		// every binding slot or none of them: __vip_VIP_IsVIPLoaded is the availability probe the
		// consumer wrapper itself uses.
		if (__vip_VIP_IsVIPLoaded != nullptr && __vip_VIP_RegisterFeature != nullptr) {
			g_pVIPCore->VIP_RegisterFeature(kFeature, VIP_BOOL, TOGGLABLE);
		} else {
			LogTaggedError("the vip bindings are unavailable - 'show_damage' is not registered");
		}

		// PITFALLS C12/C13: one Pre hook on "player_hurt"; s2sdk only accepts plain function
		// pointers, and the callback above answers Continue on every path.
		if (!g_playerHurtHooked && __s2sdk_HookEvent != nullptr) {
			if (s2sdk::HookEvent("player_hurt", &OnPlayerHurtEvent, s2sdk::HookMode::Pre) ==
				s2sdk::EventHookError::Okay) {
				g_playerHurtHooked = true;
			} else {
				LogTaggedError("hooking 'player_hurt' failed - damage display is disabled");
			}
		} else if (__s2sdk_HookEvent == nullptr) {
			LogTaggedError("the s2sdk event bindings are unavailable - 'player_hurt' is not hooked");
		}

		LogInfo(std::string("[") + kLogTag + "] ready");
	} catch (const std::exception& ex) {
		LogTaggedError(std::string("OnPluginStart failed: ") + ex.what());
	} catch (...) {
		LogTaggedError("OnPluginStart failed with an unknown exception");
	}
	return {}; // never Failed: that stops every plugify plugin (PITFALLS A1)
}

plg::PluginResult VipShowDamagePlugin::OnPluginUpdate(std::chrono::milliseconds dt) {
	(void)dt;
	try {
		// A plugin loaded into a running map never saw an OnMapStart: the first update latches
		// readiness (no-op while a map is already up, never after an OnMapEnd).
		PollMapGate();
	} catch (const std::exception& ex) {
		LogTaggedError(std::string("OnPluginUpdate failed: ") + ex.what());
	} catch (...) {
		LogTaggedError("OnPluginUpdate failed with an unknown exception");
	}
	return {};
}

plg::PluginResult VipShowDamagePlugin::OnPluginEnd() {
	try {
		// PITFALLS A7/C12: every registration of this library is removed again, so a `plg reload`
		// leaves nothing that points into the unloaded .so.
		if (g_playerHurtHooked) {
			g_playerHurtHooked = false;
			if (__s2sdk_UnhookEvent != nullptr) {
				s2sdk::UnhookEvent("player_hurt", &OnPlayerHurtEvent, s2sdk::HookMode::Pre);
			}
		}
	} catch (...) {
		// nothing left to do at unload
	}
	try {
		ShutdownMapGate();
	} catch (...) {
	}
	return {};
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
