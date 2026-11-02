// vip_sd tests — no engine: the plugin TU is compiled in (its map gate, client checks and the
// "player_hurt" handler live in an anonymous namespace) and every __s2sdk_* / __vip_* binding
// slot is a recording fake.
//
// Covered:
//   * PITFALLS B1/B2 — the map gate: not one s2sdk client/entity/schema call before the first
//     map, in the window between OnMapEnd and the next OnMapStart, after OnPluginEnd, or without
//     the map listeners; the first OnPluginUpdate latches readiness only when no OnMapEnd was seen
//     since OnPluginStart (a plugin loaded into the map-change window); OnMapStart re-opens it;
//   * PITFALLS C2 — HasClient/HasController: the slot range, connected / in-game / SourceTV /
//     controller layers, each checked before the next export is called, and a missing binding
//     refuses instead of crashing;
//   * PITFALLS C8 — GetPlayerTeam: the controller's 1-byte m_iTeamNum at the map-gated schema
//     offset, a failed lookup is not cached, a successful one is;
//   * the guarded PrintToCenter: only a live, non-SourceTV client gets the centre message;
//   * the player_hurt filter of the original: the attacker/victim slots from the event, the
//     controller + team check, the VIP "show_damage" feature, and the exact "-<dmg_health>" text;
//   * a clean unload (PITFALLS A7/C12): the event hook and the map listeners are removed, a stale
//     event pointer after OnPluginEnd prints nothing, and a reload registers everything once;
//   * a plugin started without the vip or s2sdk event bindings stays harmless (never Failed).
//
// No pisexcompat and no s2guard: the only bindings are the generated s2sdk/vip globals, linked
// from their object libraries.
//
// Run by ./test.sh (build/tests/vip_sd_tests).

#include "../src/plugin.cpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const std::string& what) {
	++g_checks;
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what.c_str());
	}
}

void CheckEqInt(long long actual, long long expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %lld, got %lld)\n", what.c_str(), expected, actual);
	}
}

void CheckEqString(std::string_view actual, std::string_view expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected \"%.*s\", got \"%.*s\")\n", what.c_str(),
					static_cast<int>(expected.size()), expected.data(),
					static_cast<int>(actual.size()), actual.data());
	}
}

std::string ToStd(const plg::string& value) {
	return std::string(std::string_view(value));
}

// --- fake s2sdk client exports ---------------------------------------------------------------

struct FakeClient {
	bool connected = false;
	bool inGame = false;
	bool sourceTV = false;
	bool hasController = false;
	int32_t team = 0;
};

FakeClient g_clients[kMaxSlots];

int g_connectedCalls = 0;
int g_inGameCalls = 0;
int g_sourceTVCalls = 0;
int g_controllerCalls = 0;
int g_badSlotCalls = 0;

int ClientCalls() {
	return g_connectedCalls + g_inGameCalls + g_sourceTVCalls + g_controllerCalls;
}

void ResetClientCounters() {
	g_connectedCalls = 0;
	g_inGameCalls = 0;
	g_sourceTVCalls = 0;
	g_controllerCalls = 0;
	g_badSlotCalls = 0;
}

// Every controller pointer is unique per slot so GetEntData2 can find the player.
void* ControllerPointer(int32_t slot) {
	return reinterpret_cast<void*>(static_cast<uintptr_t>(0x1000 + static_cast<uintptr_t>(slot) * 0x10));
}

bool InRange(int32_t slot) {
	if (slot >= 0 && slot < kMaxSlots) {
		return true;
	}
	++g_badSlotCalls;
	return false;
}

bool FakeIsClientConnected(int32_t slot) {
	++g_connectedCalls;
	return InRange(slot) && g_clients[slot].connected;
}

bool FakeIsClientInGame(int32_t slot) {
	++g_inGameCalls;
	return InRange(slot) && g_clients[slot].inGame;
}

bool FakeIsClientSourceTV(int32_t slot) {
	++g_sourceTVCalls;
	return InRange(slot) && g_clients[slot].sourceTV;
}

void* FakePlayerSlotToEntPointer(int32_t slot) {
	++g_controllerCalls;
	if (!InRange(slot) || !g_clients[slot].hasController) {
		return nullptr;
	}
	return ControllerPointer(slot);
}

// --- fake schema exports (the controller's m_iTeamNum) ---------------------------------------

int32_t g_schemaOffset = 0x40;
int g_schemaOffsetCalls = 0;
std::string g_lastSchemaClass;
std::string g_lastSchemaField;

int32_t FakeGetSchemaOffset(const plg::string& className, const plg::string& fieldName) {
	++g_schemaOffsetCalls;
	g_lastSchemaClass = ToStd(className);
	g_lastSchemaField = ToStd(fieldName);
	return g_schemaOffset;
}

int g_entDataCalls = 0;
int32_t g_lastEntDataOffset = -1;
int32_t g_lastEntDataSize = 0;

int64_t FakeGetEntData2(void* entity, int32_t offset, int32_t size) {
	++g_entDataCalls;
	g_lastEntDataOffset = offset;
	g_lastEntDataSize = size;
	for (int32_t slot = 0; slot < kMaxSlots; ++slot) {
		if (g_clients[slot].hasController && ControllerPointer(slot) == entity) {
			return g_clients[slot].team;
		}
	}
	return 0;
}

// --- fake s2sdk output -----------------------------------------------------------------------

struct FakeCenterPrint {
	int32_t slot = 0;
	std::string text;
};

std::vector<FakeCenterPrint> g_centerPrints;

void ResetPrints() {
	g_centerPrints.clear();
}

void FakePrintCenterText(int32_t slot, const plg::string& message) {
	g_centerPrints.push_back({slot, ToStd(message)});
}

// --- fake s2sdk map listeners ----------------------------------------------------------------

s2sdk::OnMapStartCallback g_onMapStart = nullptr;
s2sdk::OnMapEndCallback g_onMapEnd = nullptr;
int g_mapStartRegisters = 0;
int g_mapStartUnregisters = 0;
int g_mapEndRegisters = 0;
int g_mapEndUnregisters = 0;

void FakeMapStartRegister(s2sdk::OnMapStartCallback callback) {
	++g_mapStartRegisters;
	g_onMapStart = callback;
}

void FakeMapStartUnregister(s2sdk::OnMapStartCallback callback) {
	++g_mapStartUnregisters;
	if (g_onMapStart == callback) {
		g_onMapStart = nullptr;
	}
}

void FakeMapEndRegister(s2sdk::OnMapEndCallback callback) {
	++g_mapEndRegisters;
	g_onMapEnd = callback;
}

void FakeMapEndUnregister(s2sdk::OnMapEndCallback callback) {
	++g_mapEndUnregisters;
	if (g_onMapEnd == callback) {
		g_onMapEnd = nullptr;
	}
}

void MapStart() {
	if (g_onMapStart != nullptr) {
		g_onMapStart();
	}
}

void MapEnd() {
	if (g_onMapEnd != nullptr) {
		g_onMapEnd();
	}
}

// --- fake game events (player_hurt) ----------------------------------------------------------

struct FakeEvent {
	int32_t attacker = -1;
	int32_t userId = -1;
	int32_t dmgHealth = 0;
	int32_t dmgArmor = 0;
};

int g_attackerKeyCalls = 0;
int g_userIdKeyCalls = 0;
int g_dmgHealthCalls = 0;
int g_dmgArmorCalls = 0;
int g_unknownEventKeyCalls = 0;

void ResetEventCounters() {
	g_attackerKeyCalls = 0;
	g_userIdKeyCalls = 0;
	g_dmgHealthCalls = 0;
	g_dmgArmorCalls = 0;
	g_unknownEventKeyCalls = 0;
}

int32_t FakeGetEventPlayerSlot(void* event, const plg::string& key) {
	const std::string szKey = ToStd(key);
	if (szKey == "attacker") {
		++g_attackerKeyCalls;
	} else if (szKey == "userid") {
		++g_userIdKeyCalls;
	} else {
		++g_unknownEventKeyCalls;
	}
	const FakeEvent* pEvent = static_cast<const FakeEvent*>(event);
	if (pEvent == nullptr) {
		return -1;
	}
	return szKey == "attacker" ? pEvent->attacker : pEvent->userId;
}

int32_t FakeGetEventInt(void* event, const plg::string& key) {
	const std::string szKey = ToStd(key);
	if (szKey == "dmg_health") {
		++g_dmgHealthCalls;
	} else if (szKey == "dmg_armor") {
		++g_dmgArmorCalls;
	} else {
		++g_unknownEventKeyCalls;
	}
	const FakeEvent* pEvent = static_cast<const FakeEvent*>(event);
	if (pEvent == nullptr) {
		return 0;
	}
	return szKey == "dmg_health" ? pEvent->dmgHealth : pEvent->dmgArmor;
}

// --- fake s2sdk event hook -------------------------------------------------------------------

s2sdk::EventCallback g_hurtCallback = nullptr;
std::string g_hookName;
s2sdk::HookMode g_hookMode = s2sdk::HookMode::Pre;
std::string g_unhookName;
s2sdk::HookMode g_unhookMode = s2sdk::HookMode::Pre;
int g_hookRegisters = 0;
int g_hookUnregisters = 0;
s2sdk::EventHookError g_hookResult = s2sdk::EventHookError::Okay;

s2sdk::EventHookError FakeHookEvent(const plg::string& name, s2sdk::EventCallback callback,
									s2sdk::HookMode mode) {
	++g_hookRegisters;
	g_hookName = ToStd(name);
	g_hookMode = mode;
	if (g_hookResult == s2sdk::EventHookError::Okay) {
		g_hurtCallback = callback;
	}
	return g_hookResult;
}

s2sdk::EventHookError FakeUnhookEvent(const plg::string& name, s2sdk::EventCallback callback,
									  s2sdk::HookMode mode) {
	++g_hookUnregisters;
	g_unhookName = ToStd(name);
	g_unhookMode = mode;
	if (g_hurtCallback == callback) {
		g_hurtCallback = nullptr;
	}
	return s2sdk::EventHookError::Okay;
}

// Delivers a "player_hurt" the way the engine would, through the callback the plugin registered.
s2sdk::ResultType FireHurt(FakeEvent& event) {
	if (g_hurtCallback == nullptr) {
		return s2sdk::ResultType::Continue;
	}
	return g_hurtCallback(plg::string("player_hurt"), &event, false);
}

// --- fake vip core exports -------------------------------------------------------------------

bool g_vipLoaded = true;
bool g_isVip[kMaxSlots] = {};
bool g_showDamage[kMaxSlots] = {};

int g_isVipCalls = 0;
int g_featureBoolCalls = 0;
std::string g_lastFeatureName;
int g_registerFeatureCalls = 0;
std::string g_registeredFeature;
vip::ValueType g_registeredValueType = vip::ValueType::VIP_NULL;
vip::FeatureType g_registeredFeatureType = vip::FeatureType::HIDE;
vip::ItemSelectableDelegate g_registeredSelect = nullptr;
vip::ItemTogglableDelegate g_registeredToggle = nullptr;
vip::ItemDisplayDelegate g_registeredDisplay = nullptr;

void ResetVipCounters() {
	g_isVipCalls = 0;
	g_featureBoolCalls = 0;
	g_lastFeatureName.clear();
}

bool FakeVipIsLoaded() {
	return g_vipLoaded;
}

bool FakeVipIsClientVIP(int32_t slot) {
	++g_isVipCalls;
	return slot >= 0 && slot < kMaxSlots && g_isVip[slot];
}

bool FakeVipGetClientFeatureBool(int32_t slot, const plg::string& feature) {
	++g_featureBoolCalls;
	g_lastFeatureName = ToStd(feature);
	if (slot < 0 || slot >= kMaxSlots) {
		return false;
	}
	return g_showDamage[slot];
}

void FakeVipRegisterFeature(const plg::string& feature, vip::ValueType valueType,
							vip::FeatureType featureType, vip::ItemSelectableDelegate select,
							vip::ItemTogglableDelegate toggle, vip::ItemDisplayDelegate display) {
	++g_registerFeatureCalls;
	g_registeredFeature = ToStd(feature);
	g_registeredValueType = valueType;
	g_registeredFeatureType = featureType;
	g_registeredSelect = select;
	g_registeredToggle = toggle;
	g_registeredDisplay = display;
}

// --- the fake world --------------------------------------------------------------------------

void InstallFakes() {
	__s2sdk_OnMapStart_Register = &FakeMapStartRegister;
	__s2sdk_OnMapStart_Unregister = &FakeMapStartUnregister;
	__s2sdk_OnMapEnd_Register = &FakeMapEndRegister;
	__s2sdk_OnMapEnd_Unregister = &FakeMapEndUnregister;

	__s2sdk_IsClientConnected = &FakeIsClientConnected;
	__s2sdk_IsClientInGame = &FakeIsClientInGame;
	__s2sdk_IsClientSourceTV = &FakeIsClientSourceTV;
	__s2sdk_PlayerSlotToEntPointer = &FakePlayerSlotToEntPointer;
	__s2sdk_GetSchemaOffset = &FakeGetSchemaOffset;
	__s2sdk_GetEntData2 = &FakeGetEntData2;
	__s2sdk_PrintCenterText = &FakePrintCenterText;

	__s2sdk_HookEvent = &FakeHookEvent;
	__s2sdk_UnhookEvent = &FakeUnhookEvent;
	__s2sdk_GetEventPlayerSlot = &FakeGetEventPlayerSlot;
	__s2sdk_GetEventInt = &FakeGetEventInt;

	__vip_VIP_IsVIPLoaded = &FakeVipIsLoaded;
	__vip_VIP_IsClientVIP = &FakeVipIsClientVIP;
	__vip_VIP_GetClientFeatureBool = &FakeVipGetClientFeatureBool;
	__vip_VIP_RegisterFeature = &FakeVipRegisterFeature;
}

// A connected, in-game, non-SourceTV human with a controller on the given team.
void MakePlayer(int32_t slot, int32_t team, bool isVip = false, bool showDamage = false) {
	g_clients[slot] = FakeClient{};
	g_clients[slot].connected = true;
	g_clients[slot].inGame = true;
	g_clients[slot].hasController = true;
	g_clients[slot].team = team;
	g_isVip[slot] = isVip;
	g_showDamage[slot] = showDamage;
}

// Forgets every fake registration and all recorded state. The plugin must have been stopped first
// (PrepareTest/FinishTest do that), otherwise it still believes its listeners are registered.
void ResetWorld() {
	for (FakeClient& client : g_clients) {
		client = FakeClient{};
	}
	for (bool& value : g_isVip) {
		value = false;
	}
	for (bool& value : g_showDamage) {
		value = false;
	}

	ResetClientCounters();
	ResetPrints();
	ResetEventCounters();
	ResetVipCounters();

	g_schemaOffset = 0x40;
	g_schemaOffsetCalls = 0;
	g_lastSchemaClass.clear();
	g_lastSchemaField.clear();
	g_entDataCalls = 0;
	g_lastEntDataOffset = -1;
	g_lastEntDataSize = 0;

	g_onMapStart = nullptr;
	g_onMapEnd = nullptr;
	g_mapStartRegisters = 0;
	g_mapStartUnregisters = 0;
	g_mapEndRegisters = 0;
	g_mapEndUnregisters = 0;

	g_hurtCallback = nullptr;
	g_hookName.clear();
	g_unhookName.clear();
	g_hookMode = s2sdk::HookMode::Pre;
	g_unhookMode = s2sdk::HookMode::Pre;
	g_hookRegisters = 0;
	g_hookUnregisters = 0;
	g_hookResult = s2sdk::EventHookError::Okay;

	g_vipLoaded = true;
	g_registerFeatureCalls = 0;
	g_registeredFeature.clear();
	g_registeredValueType = vip::ValueType::VIP_NULL;
	g_registeredFeatureType = vip::FeatureType::HIDE;
	g_registeredSelect = nullptr;
	g_registeredToggle = nullptr;
	g_registeredDisplay = nullptr;
}

// Stops whatever a previous test left behind (OnPluginEnd is a no-op when nothing is registered)
// and starts the next test from an empty fake world.
void PrepareTest() {
	g_plugin.OnPluginEnd();
	ResetWorld();
}

void FinishTest() {
	g_plugin.OnPluginEnd();
	ResetWorld();
}

// A player_hurt that the plugin should display: attacker slot 1 (VIP, feature on, team 2) hits
// victim slot 2 (team 3) for 25 health damage.
FakeEvent DisplayedHurt() {
	MakePlayer(1, 2, true, true);
	MakePlayer(2, 3);
	FakeEvent event;
	event.attacker = 1;
	event.userId = 2;
	event.dmgHealth = 25;
	event.dmgArmor = 0;
	return event;
}

// --- A. the map gate (PITFALLS B1/B2) ----------------------------------------------------------

void TestMapGate() {
	PrepareTest();
	Check(g_plugin.OnPluginStart().GetCode() == plg::PluginCode::Ok,
		  "OnPluginStart returns Ok (never Failed)");
	Check(g_mapStartRegisters == 1 && g_mapEndRegisters == 1,
		  "OnPluginStart registers both map listeners");
	Check(g_mapListenersRegistered, "the map listeners are marked as registered");
	Check(!g_mapReady, "the gate is shut on plugin start");

	MakePlayer(3, 2);

	// --- before the first map: not one client call may reach s2sdk ----------------------------
	ResetClientCounters();
	Check(!HasClient(3), "HasClient is false before the gate opens");
	Check(!HasController(3), "HasController is false before the gate opens");
	Check(GetPlayerTeam(3) == 0, "GetPlayerTeam is 0 before the gate opens");
	PrintToCenter(3, "x");
	Check(ClientCalls() == 0, "not one s2sdk client call is made before the gate opens");
	Check(g_schemaOffsetCalls == 0, "the schema offset is not resolved before the gate opens");
	Check(g_centerPrints.empty(), "no client-directed output before the gate opens");

	// A hurt before the first map is dropped before a single event field is read.
	FakeEvent early = DisplayedHurt();
	ResetEventCounters();
	Check(FireHurt(early) == s2sdk::ResultType::Continue,
		  "the Pre hook answers Continue before the first map");
	Check(g_attackerKeyCalls == 0 && g_userIdKeyCalls == 0,
		  "an event before the first map reads no event field");
	Check(g_centerPrints.empty(), "an event before the first map prints nothing");

	// A plugin loaded into a running map: its first update latches readiness.
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	Check(g_mapReady, "the first OnPluginUpdate latches the gate");
	ResetClientCounters();
	Check(HasClient(3), "a live client is reachable after the latch");
	Check(HasController(3), "a live client has a controller after the latch");
	Check(ClientCalls() > 0, "the client calls reach s2sdk after the latch");

	// --- after OnMapEnd: not one client call may reach s2sdk ---------------------------------
	MapEnd();
	Check(!g_mapReady, "OnMapEnd drops readiness");
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	Check(!g_mapReady, "an update after OnMapEnd does not re-open the gate");
	ResetClientCounters();
	Check(!HasClient(3), "HasClient is false after OnMapEnd");
	Check(!HasController(3), "HasController is false after OnMapEnd");
	Check(GetPlayerTeam(3) == 0, "GetPlayerTeam is 0 after OnMapEnd");
	PrintToCenter(3, "x");
	Check(ClientCalls() == 0, "not one s2sdk client call is made after OnMapEnd");
	Check(g_centerPrints.empty(), "no client-directed output after OnMapEnd");

	FakeEvent late = DisplayedHurt();
	ResetEventCounters();
	FireHurt(late);
	Check(g_attackerKeyCalls == 0 && g_dmgHealthCalls == 0,
		  "an event between maps reads no event field and prints nothing");
	Check(g_centerPrints.empty(), "no center message in the map-change window");

	// --- the next OnMapStart opens the gate again without waiting for an update --------------
	MapStart();
	Check(g_mapReady, "OnMapStart latches the gate");
	Check(HasController(3), "the client is reachable again");

	FinishTest();
}

// A plugin loaded during the map change (OnMapEnd before its first update) must not latch on the
// update: the first update only counts when no OnMapEnd was seen since OnPluginStart.
void TestMapChangeWindowStart() {
	PrepareTest();
	g_plugin.OnPluginStart();
	MapEnd();
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	Check(!g_mapReady, "the first update after an OnMapEnd does not latch the gate");

	MakePlayer(2, 2);
	ResetClientCounters();
	Check(!HasController(2), "no client is reachable in the map-change window");
	PrintToCenter(2, "x");
	Check(ClientCalls() == 0, "not one s2sdk client call is made in the map-change window");
	Check(g_centerPrints.empty(), "no centre message in the map-change window");

	MapStart();
	Check(g_mapReady, "the next OnMapStart opens the gate");
	Check(HasController(2), "the client is reachable after the map start");

	FinishTest();
}

// Without the map listeners nothing could ever close the gate again, so it must never open.
void TestNoMapListeners() {
	PrepareTest();
	const s2sdk::_OnMapStart_Register savedStart = __s2sdk_OnMapStart_Register;
	const s2sdk::_OnMapEnd_Register savedEnd = __s2sdk_OnMapEnd_Register;
	__s2sdk_OnMapStart_Register = nullptr;
	__s2sdk_OnMapEnd_Register = nullptr;

	MakePlayer(4, 2);
	Check(g_plugin.OnPluginStart().GetCode() == plg::PluginCode::Ok,
		  "OnPluginStart is Ok without the map listeners");
	Check(!g_mapListenersRegistered, "without the map listeners the gate is never armed");
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	Check(!g_mapReady, "an update cannot open a gate that has no map-end listener");
	ResetClientCounters();
	Check(!HasController(4), "no client is reachable without the map listeners");
	PrintToCenter(4, "x");
	Check(ClientCalls() == 0, "not one s2sdk client call is made without the map listeners");
	Check(g_centerPrints.empty(), "no output without the map listeners");

	g_plugin.OnPluginEnd();
	Check(!g_mapListenersRegistered, "OnPluginEnd leaves the unarmed gate alone");

	__s2sdk_OnMapStart_Register = savedStart;
	__s2sdk_OnMapEnd_Register = savedEnd;
	FinishTest();
}

// --- B. the client checks (PITFALLS C2) and the team read (PITFALLS C8) ----------------------

void TestClientGuards() {
	PrepareTest();
	g_plugin.OnPluginStart();
	MapStart();
	Check(g_mapReady, "the map gate is open");

	// --- the slot range is checked before any export ------------------------------------------
	ResetClientCounters();
	Check(!HasClient(-1) && !HasClient(kMaxSlots) && !HasClient(1000),
		  "out-of-range slots have no client");
	Check(!HasController(-1) && !HasController(kMaxSlots), "out-of-range slots have no controller");
	Check(GetPlayerTeam(-1) == 0 && GetPlayerTeam(kMaxSlots) == 0,
		  "out-of-range slots have no team");
	PrintToCenter(kMaxSlots, "x");
	Check(ClientCalls() == 0, "no s2sdk call is made for an out-of-range slot");
	Check(g_badSlotCalls == 0, "no export ever receives an out-of-range slot");

	// --- every layer in order ------------------------------------------------------------------
	MakePlayer(7, 2);
	ResetClientCounters();
	Check(HasClient(7), "a connected, in-game human has a client");
	Check(g_connectedCalls == 1 && g_inGameCalls == 1 && g_sourceTVCalls == 1,
		  "HasClient asks the three client flags once each");

	g_clients[7].connected = false;
	ResetClientCounters();
	Check(!HasClient(7), "a disconnected slot has no client");
	Check(g_connectedCalls == 1 && g_inGameCalls == 0 && g_sourceTVCalls == 0,
		  "in-game/SourceTV are not reached for a disconnected slot");

	g_clients[7].connected = true;
	g_clients[7].inGame = false;
	ResetClientCounters();
	Check(!HasClient(7), "a connected slot that is not in game has no client");
	Check(g_connectedCalls == 1 && g_inGameCalls == 1 && g_sourceTVCalls == 0,
		  "SourceTV is not reached for a slot that is not in game");

	g_clients[7].inGame = true;
	g_clients[7].sourceTV = true;
	ResetClientCounters();
	Check(!HasClient(7), "the SourceTV proxy is not a client");
	Check(g_sourceTVCalls == 1 && g_controllerCalls == 0,
		  "the controller is not queried for the SourceTV proxy");

	g_clients[7].sourceTV = false;
	g_clients[7].hasController = false;
	ResetClientCounters();
	Check(!HasController(7), "a client without a controller entity has no controller");
	Check(g_controllerCalls == 1, "the controller entity is queried once");
	ResetClientCounters();
	Check(GetPlayerTeam(7) == 0, "a client without a controller has no team");
	Check(g_entDataCalls == 0, "the team field is not read without a controller");

	MakePlayer(7, 2);
	ResetClientCounters();
	Check(HasController(7), "a client with a controller entity has a controller");
	Check(g_controllerCalls == 1, "HasController queries the controller entity once");

	// --- the 1-byte m_iTeamNum read -----------------------------------------------------------
	// The first lookup fails: a failed offset must not be cached (PITFALLS C8).
	g_schemaOffset = -1;
	g_schemaOffsetCalls = 0;
	g_entDataCalls = 0;
	Check(GetPlayerTeam(7) == 0, "a failed schema lookup answers team 0");
	CheckEqInt(g_schemaOffsetCalls, 1, "the schema offset is asked for once");
	Check(g_lastSchemaClass == "CBaseEntity", "the schema class is CBaseEntity");
	Check(g_lastSchemaField == "m_iTeamNum", "the schema field is m_iTeamNum");
	CheckEqInt(g_entDataCalls, 0, "no field is read when the offset lookup failed");

	// The retry succeeds and reads one byte at the resolved offset.
	g_schemaOffset = 0x40;
	g_entDataCalls = 0;
	CheckEqInt(GetPlayerTeam(7), 2, "the team is read from the controller");
	CheckEqInt(g_schemaOffsetCalls, 2, "the failed lookup is retried, not poisoned");
	CheckEqInt(g_entDataCalls, 1, "the team field is read once");
	CheckEqInt(g_lastEntDataOffset, 0x40, "the read uses the resolved schema offset");
	CheckEqInt(g_lastEntDataSize, 1, "m_iTeamNum is read as exactly one byte");

	// A successful offset is cached: the next reads do not touch the schema system again.
	g_entDataCalls = 0;
	CheckEqInt(GetPlayerTeam(7), 2, "the team is still read");
	CheckEqInt(g_schemaOffsetCalls, 2, "a successful offset is cached");
	CheckEqInt(g_entDataCalls, 1, "the field read still happens");

	// The read is masked to one byte even if the export answers a wider value.
	g_clients[7].team = 0x102;
	CheckEqInt(GetPlayerTeam(7), 2, "the team is the low byte of the field read");

	// --- a missing binding refuses instead of crashing ----------------------------------------
	MakePlayer(8, 3);
	const s2sdk::_IsClientConnected savedConnected = __s2sdk_IsClientConnected;
	__s2sdk_IsClientConnected = nullptr;
	ResetClientCounters();
	Check(!HasClient(8), "HasClient refuses when the connected binding is missing");
	Check(ClientCalls() == 0, "no export is called when a binding is missing");
	__s2sdk_IsClientConnected = savedConnected;

	const s2sdk::_IsClientInGame savedInGame = __s2sdk_IsClientInGame;
	__s2sdk_IsClientInGame = nullptr;
	ResetClientCounters();
	Check(!HasClient(8), "HasClient refuses when the in-game binding is missing");
	Check(ClientCalls() == 0, "no export is called when the in-game binding is missing");
	__s2sdk_IsClientInGame = savedInGame;

	const s2sdk::_IsClientSourceTV savedSourceTV = __s2sdk_IsClientSourceTV;
	__s2sdk_IsClientSourceTV = nullptr;
	ResetClientCounters();
	Check(!HasClient(8), "HasClient refuses when the SourceTV binding is missing");
	Check(ClientCalls() == 0, "no export is called when the SourceTV binding is missing");
	__s2sdk_IsClientSourceTV = savedSourceTV;

	const s2sdk::_PlayerSlotToEntPointer savedController = __s2sdk_PlayerSlotToEntPointer;
	__s2sdk_PlayerSlotToEntPointer = nullptr;
	Check(!HasController(8), "HasController refuses when the controller binding is missing");
	__s2sdk_PlayerSlotToEntPointer = savedController;

	const s2sdk::_GetEntData2 savedEntData = __s2sdk_GetEntData2;
	__s2sdk_GetEntData2 = nullptr;
	Check(GetPlayerTeam(8) == 0, "GetPlayerTeam refuses when the field binding is missing");
	__s2sdk_GetEntData2 = savedEntData;

	// Between maps even a valid slot answers 0 without an export call (PITFALLS B2).
	MapEnd();
	ResetClientCounters();
	Check(GetPlayerTeam(8) == 0, "no team is read between maps");
	Check(ClientCalls() == 0, "no client call is made between maps");

	FinishTest();
}

// --- C. the guarded PrintToCenter -------------------------------------------------------------

void TestPrintToCenter() {
	PrepareTest();
	g_plugin.OnPluginStart();
	MapStart();
	Check(g_mapReady, "the map gate is open");

	MakePlayer(4, 2);
	ResetPrints();
	ResetClientCounters();
	PrintToCenter(4, "-12");
	CheckEqInt(static_cast<long long>(g_centerPrints.size()), 1,
			   "a live client gets the centre message");
	if (g_centerPrints.size() == 1) {
		CheckEqInt(g_centerPrints[0].slot, 4, "the message goes to the requested slot");
		CheckEqString(g_centerPrints[0].text, "-12", "the message text is passed unchanged");
	}
	Check(ClientCalls() > 0, "the print is guarded by the client checks");

	// Not connected / not in game / SourceTV: HasClient refuses and nothing is printed.
	g_clients[4].connected = false;
	ResetPrints();
	PrintToCenter(4, "x");
	Check(g_centerPrints.empty(), "a disconnected slot gets no centre message");

	g_clients[4].connected = true;
	g_clients[4].inGame = false;
	PrintToCenter(4, "x");
	Check(g_centerPrints.empty(), "a slot that is not in game gets no centre message");

	g_clients[4].inGame = true;
	g_clients[4].sourceTV = true;
	PrintToCenter(4, "x");
	Check(g_centerPrints.empty(), "the SourceTV proxy gets no centre message");

	// A controller is not required by a plain centre print (the guard is HasClient, not
	// HasController): the original printed to the attacker slot the event had.
	MakePlayer(4, 2);
	g_clients[4].hasController = false;
	ResetPrints();
	PrintToCenter(4, "no-controller");
	CheckEqInt(static_cast<long long>(g_centerPrints.size()), 1,
			   "a client without a controller still gets the centre message");

	// An out-of-range slot makes no call at all.
	MakePlayer(4, 2);
	ResetClientCounters();
	ResetPrints();
	PrintToCenter(-1, "x");
	PrintToCenter(kMaxSlots, "x");
	Check(ClientCalls() == 0, "out-of-range slots never reach a client binding");
	Check(g_centerPrints.empty(), "out-of-range slots get no centre message");

	// A missing print binding is refused instead of called.
	const s2sdk::_PrintCenterText savedPrint = __s2sdk_PrintCenterText;
	__s2sdk_PrintCenterText = nullptr;
	ResetPrints();
	PrintToCenter(4, "x");
	Check(g_centerPrints.empty(), "a missing PrintCenterText binding prints nothing");
	__s2sdk_PrintCenterText = savedPrint;

	// After OnMapEnd nothing is printed even for a slot that was valid before.
	MapEnd();
	ResetPrints();
	PrintToCenter(4, "x");
	Check(g_centerPrints.empty(), "no centre message after OnMapEnd");

	FinishTest();
}

// --- D. the player_hurt filter ----------------------------------------------------------------

void TestPlayerHurt() {
	PrepareTest();
	Check(g_plugin.OnPluginStart().GetCode() == plg::PluginCode::Ok,
		  "OnPluginStart returns Ok (never Failed)");
	CheckEqInt(g_hookRegisters, 1, "OnPluginStart hooks player_hurt once");
	CheckEqString(g_hookName, "player_hurt", "the hooked event is player_hurt");
	Check(g_hookMode == s2sdk::HookMode::Pre, "player_hurt is hooked in Pre mode");
	Check(g_hurtCallback != nullptr, "the player_hurt callback is registered");
	CheckEqInt(g_registerFeatureCalls, 1, "OnPluginStart registers the feature once");
	CheckEqString(g_registeredFeature, "show_damage", "the feature is named show_damage");
	Check(g_registeredValueType == vip::ValueType::VIP_BOOL,
		  "the feature is registered as a bool");
	Check(g_registeredFeatureType == vip::FeatureType::TOGGLABLE,
		  "the feature is registered as togglable");
	Check(g_registeredSelect == nullptr && g_registeredToggle == nullptr &&
			  g_registeredDisplay == nullptr,
		  "the feature is registered without callbacks, like the original");

	MapStart();
	Check(g_mapReady, "the map gate is open");

	// --- the original's happy path ------------------------------------------------------------
	MakePlayer(1, 2, true, true);
	MakePlayer(2, 3);
	FakeEvent event = DisplayedHurt();
	ResetPrints();
	ResetEventCounters();
	ResetVipCounters();
	Check(FireHurt(event) == s2sdk::ResultType::Continue,
		  "the Pre hook answers Continue on the happy path");
	CheckEqInt(static_cast<long long>(g_centerPrints.size()), 1,
			   "a VIP attacker with show_damage gets one centre message");
	if (g_centerPrints.size() == 1) {
		CheckEqInt(g_centerPrints[0].slot, 1, "the message goes to the attacker's slot");
		CheckEqString(g_centerPrints[0].text, "-25", "the message is \"-<dmg_health>\"");
	}
	CheckEqInt(g_attackerKeyCalls, 1, "the attacker slot is read from the event");
	CheckEqInt(g_userIdKeyCalls, 1, "the victim slot is read from the event");
	CheckEqInt(g_dmgHealthCalls, 1, "dmg_health is read from the event");
	CheckEqInt(g_dmgArmorCalls, 1, "dmg_armor is read from the event");
	CheckEqInt(g_unknownEventKeyCalls, 0, "no other event field is read");
	CheckEqInt(g_isVipCalls, 1, "the attacker's VIP status is asked once");
	CheckEqInt(g_featureBoolCalls, 1, "the show_damage feature is asked once");
	CheckEqString(g_lastFeatureName, "show_damage", "the feature asked for is show_damage");

	// --- the team comparison ------------------------------------------------------------------
	// The victim on the attacker's team: the original returned before the VIP check.
	MakePlayer(2, 2);
	ResetPrints();
	ResetVipCounters();
	FireHurt(event);
	Check(g_centerPrints.empty(), "a victim on the attacker's team prints nothing");
	CheckEqInt(g_isVipCalls, 0, "the VIP check is not reached for a same-team victim");
	CheckEqInt(g_featureBoolCalls, 0, "the feature is not read for a same-team victim");

	// Both unassigned (team 0): the original's comparison also skipped them.
	MakePlayer(1, 0, true, true);
	MakePlayer(2, 0);
	ResetPrints();
	FireHurt(event);
	Check(g_centerPrints.empty(), "two unassigned players print nothing");

	// Team 0 vs team 2 differ: the message is printed (the original only compared equality).
	MakePlayer(1, 0, true, true);
	MakePlayer(2, 2);
	ResetPrints();
	FireHurt(event);
	CheckEqInt(static_cast<long long>(g_centerPrints.size()), 1,
			   "an unassigned attacker vs a team player prints");

	// --- the VIP filter -----------------------------------------------------------------------
	MakePlayer(1, 2, false, true);
	MakePlayer(2, 3);
	ResetPrints();
	ResetVipCounters();
	FireHurt(event);
	Check(g_centerPrints.empty(), "a non-VIP attacker prints nothing");
	CheckEqInt(g_isVipCalls, 1, "the VIP status is asked once");
	CheckEqInt(g_featureBoolCalls, 0, "the feature is not read for a non-VIP");

	MakePlayer(1, 2, true, false);
	ResetPrints();
	ResetVipCounters();
	FireHurt(event);
	Check(g_centerPrints.empty(), "a VIP with show_damage off prints nothing");
	CheckEqInt(g_featureBoolCalls, 1, "the feature is read for a VIP");

	// --- the controller checks ----------------------------------------------------------------
	// The attacker without a controller: the original's FromSlot returned null.
	MakePlayer(1, 2, true, true);
	g_clients[1].hasController = false;
	MakePlayer(2, 3);
	ResetPrints();
	ResetClientCounters();
	g_entDataCalls = 0;
	FireHurt(event);
	Check(g_centerPrints.empty(), "an attacker without a controller prints nothing");
	Check(g_entDataCalls == 0, "no team is read when the attacker has no controller");
	Check(g_badSlotCalls == 0, "no export gets an invalid slot");

	// The victim without a controller.
	MakePlayer(1, 2, true, true);
	MakePlayer(2, 3);
	g_clients[2].hasController = false;
	ResetPrints();
	ResetClientCounters();
	g_entDataCalls = 0;
	FireHurt(event);
	Check(g_centerPrints.empty(), "a victim without a controller prints nothing");
	Check(g_entDataCalls == 0, "no team is read when the victim has no controller");

	// --- the event slots ----------------------------------------------------------------------
	// A world hit: "attacker" is -1.
	MakePlayer(1, 2, true, true);
	MakePlayer(2, 3);
	FakeEvent world = event;
	world.attacker = -1;
	ResetPrints();
	ResetClientCounters();
	FireHurt(world);
	Check(g_centerPrints.empty(), "a world hit prints nothing");
	Check(ClientCalls() == 0, "a world hit makes no client call");

	// An invalid victim slot is refused before any client call.
	FakeEvent noVictim = event;
	noVictim.userId = kMaxSlots;
	ResetPrints();
	ResetClientCounters();
	FireHurt(noVictim);
	Check(g_centerPrints.empty(), "an out-of-range victim prints nothing");
	Check(ClientCalls() == 0, "an out-of-range victim makes no client call");

	// Self-damage: both slots are the same player, so the team comparison skips it.
	MakePlayer(1, 2, true, true);
	FakeEvent self = event;
	self.userId = 1;
	ResetPrints();
	FireHurt(self);
	Check(g_centerPrints.empty(), "self-damage prints nothing");

	// --- the damage fields --------------------------------------------------------------------
	// Nothing to show: no health and no armour damage (the original printed a meaningless "-0").
	MakePlayer(1, 2, true, true);
	MakePlayer(2, 3);
	FakeEvent nothing = event;
	nothing.dmgHealth = 0;
	nothing.dmgArmor = 0;
	ResetPrints();
	ResetEventCounters();
	FireHurt(nothing);
	Check(g_centerPrints.empty(), "an event with no damage prints nothing");
	CheckEqInt(g_dmgHealthCalls, 1, "dmg_health is still read");
	CheckEqInt(g_dmgArmorCalls, 1, "dmg_armor is still read");

	// An armour-only hit still prints "-0", exactly like the original's fixed format.
	FakeEvent armor = event;
	armor.dmgHealth = 0;
	armor.dmgArmor = 7;
	ResetPrints();
	FireHurt(armor);
	CheckEqInt(static_cast<long long>(g_centerPrints.size()), 1,
			   "an armour-only hit prints the original's \"-0\"");
	if (g_centerPrints.size() == 1) {
		CheckEqString(g_centerPrints[0].text, "-0", "the armour-only text is \"-0\"");
	}

	// A negative health damage is not a real hurt and is not displayed.
	FakeEvent negative = event;
	negative.dmgHealth = -5;
	negative.dmgArmor = 0;
	ResetPrints();
	FireHurt(negative);
	Check(g_centerPrints.empty(), "a negative health damage prints nothing");

	// --- the gate and the null event ----------------------------------------------------------
	MapEnd();
	MakePlayer(1, 2, true, true);
	MakePlayer(2, 3);
	ResetPrints();
	ResetEventCounters();
	Check(FireHurt(event) == s2sdk::ResultType::Continue,
		  "the Pre hook answers Continue between maps");
	Check(g_centerPrints.empty(), "an event between maps prints nothing");
	CheckEqInt(g_attackerKeyCalls, 0, "an event between maps reads no field");

	MapStart();
	ResetPrints();
	Check(g_hurtCallback(plg::string("player_hurt"), nullptr, false) ==
			  s2sdk::ResultType::Continue,
		  "the Pre hook answers Continue for a null event");
	Check(g_centerPrints.empty(), "a null event prints nothing");

	// A missing event binding is refused instead of called.
	const s2sdk::_GetEventInt savedGetEventInt = __s2sdk_GetEventInt;
	__s2sdk_GetEventInt = nullptr;
	ResetPrints();
	FireHurt(event);
	Check(g_centerPrints.empty(), "a missing GetEventInt binding prints nothing");
	__s2sdk_GetEventInt = savedGetEventInt;

	FinishTest();
}

// --- E. a clean unload and a reload (PITFALLS A7/C12) -----------------------------------------

void TestUnload() {
	PrepareTest();
	g_plugin.OnPluginStart();
	MapStart();
	FakeEvent event = DisplayedHurt();
	ResetPrints();
	FireHurt(event);
	CheckEqInt(static_cast<long long>(g_centerPrints.size()), 1,
			   "the damage display works before the unload");

	const s2sdk::EventCallback staleCallback = g_hurtCallback;
	Check(staleCallback != nullptr, "the event callback is registered before the unload");

	g_plugin.OnPluginEnd();
	Check(!g_mapReady, "OnPluginEnd drops readiness");
	CheckEqInt(g_mapStartUnregisters, 1, "OnPluginEnd unregisters the OnMapStart listener");
	CheckEqInt(g_mapEndUnregisters, 1, "OnPluginEnd unregisters the OnMapEnd listener");
	Check(g_onMapStart == nullptr && g_onMapEnd == nullptr,
		  "the map callbacks are gone from s2sdk");
	CheckEqInt(g_hookUnregisters, 1, "OnPluginEnd unhooks player_hurt once");
	CheckEqString(g_unhookName, "player_hurt", "the unhooked event is player_hurt");
	Check(g_unhookMode == s2sdk::HookMode::Pre, "the unhook uses Pre mode");
	Check(g_hurtCallback == nullptr, "the event callback is gone from s2sdk");

	// A pointer the engine may still hold for a moment must not print after the unload.
	ResetPrints();
	Check(staleCallback(plg::string("player_hurt"), &event, false) ==
			  s2sdk::ResultType::Continue,
		  "a stale callback after OnPluginEnd answers Continue");
	Check(g_centerPrints.empty(), "a stale callback after OnPluginEnd prints nothing");

	// The second OnPluginEnd is a no-op (nothing is registered any more).
	g_plugin.OnPluginEnd();
	CheckEqInt(g_hookUnregisters, 1, "the second OnPluginEnd does not unhook again");
	CheckEqInt(g_mapStartUnregisters, 1, "the second OnPluginEnd does not unregister again");

	// plg reload: a fresh start after the end registers everything exactly once more.
	g_plugin.OnPluginStart();
	CheckEqInt(g_mapStartRegisters, 2, "a reload registers the OnMapStart listener again");
	CheckEqInt(g_mapEndRegisters, 2, "a reload registers the OnMapEnd listener again");
	CheckEqInt(g_hookRegisters, 2, "a reload hooks player_hurt again");
	CheckEqInt(g_registerFeatureCalls, 2, "a reload registers the feature again");
	Check(!g_mapReady, "a reload starts with the gate shut");
	MapStart();
	Check(g_mapReady, "the reloaded plugin opens the gate on OnMapStart");
	ResetPrints();
	FireHurt(event);
	CheckEqInt(static_cast<long long>(g_centerPrints.size()), 1,
			   "the damage display works after the reload");

	FinishTest();
}

// --- F. a hook that s2sdk refuses, and missing bindings ---------------------------------------

void TestHookFailure() {
	PrepareTest();
	g_hookResult = s2sdk::EventHookError::InvalidEvent;
	Check(g_plugin.OnPluginStart().GetCode() == plg::PluginCode::Ok,
		  "OnPluginStart is Ok when the hook is refused");
	Check(g_hurtCallback == nullptr, "a refused hook leaves no callback behind");
	CheckEqInt(g_hookRegisters, 1, "the hook is attempted once");
	g_plugin.OnPluginEnd();
	CheckEqInt(g_hookUnregisters, 0, "OnPluginEnd does not unhook a hook that never existed");
	g_hookResult = s2sdk::EventHookError::Okay;
	FinishTest();
}

void TestNoBindings() {
	// Without the s2sdk event bindings the plugin must still start (A1) and print nothing.
	PrepareTest();
	const s2sdk::_HookEvent savedHook = __s2sdk_HookEvent;
	__s2sdk_HookEvent = nullptr;
	Check(g_plugin.OnPluginStart().GetCode() == plg::PluginCode::Ok,
		  "OnPluginStart is Ok without the event bindings");
	Check(g_hurtCallback == nullptr, "no event callback without the event bindings");
	MapStart();
	MakePlayer(1, 2, true, true);
	MakePlayer(2, 3);
	ResetPrints();
	Check(g_hurtCallback == nullptr,
		  "there is no callback to deliver a hurt to without the event bindings");
	Check(g_centerPrints.empty(), "no output without the event bindings");
	g_plugin.OnPluginEnd();
	__s2sdk_HookEvent = savedHook;

	// Without the vip bindings the feature is not registered (no null call) and nothing prints.
	PrepareTest();
	const vip::_VIP_IsVIPLoaded savedLoaded = __vip_VIP_IsVIPLoaded;
	const vip::_VIP_RegisterFeature savedRegister = __vip_VIP_RegisterFeature;
	__vip_VIP_IsVIPLoaded = nullptr;
	__vip_VIP_RegisterFeature = nullptr;
	Check(g_plugin.OnPluginStart().GetCode() == plg::PluginCode::Ok,
		  "OnPluginStart is Ok without the vip bindings");
	CheckEqInt(g_registerFeatureCalls, 0, "no feature is registered without the vip bindings");
	Check(g_hurtCallback != nullptr, "the event hook is still registered");
	MapStart();
	FakeEvent event = DisplayedHurt();
	ResetPrints();
	FireHurt(event);
	Check(g_centerPrints.empty(), "a hurt without the vip bindings prints nothing");
	g_plugin.OnPluginEnd();
	__vip_VIP_IsVIPLoaded = savedLoaded;
	__vip_VIP_RegisterFeature = savedRegister;

	FinishTest();
}

} // namespace

int main() {
	std::printf("vip_sd tests (fake s2sdk/vip bindings, no engine)\n");

	InstallFakes();

	TestMapGate();
	TestMapChangeWindowStart();
	TestNoMapListeners();
	TestClientGuards();
	TestPrintToCenter();
	TestPlayerHurt();
	TestUnload();
	TestHookFailure();
	TestNoBindings();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
