// Shatterline Offline Profile
//
// Keeps a player's progression in a local JSON file, the job FragLab's backend database did online.
// The game runs its offline backend stubs (in-memory, seeded from read-only files); this DLL restores the
// saved state into the game at startup and saves every change:
//   - wallet: amounts restored into the offline wallet when it starts, saved on every change
//   - owned items: bought hero perk levels, re-added whenever the game reloads the inventory (in time for it to
//     re-equip them), saved when bought
//   - mission rewards: materials the server granted at the end of a mission are added to the wallet
//   - carried gear: the weapon, ability and perk tokens the server granted for a successful extraction are kept as
//     the start loadout of the next expedition (added with the owned items); a mission without them clears it
//   - MVP scene: the post-match "most valuable players" step, still in the game but no longer used, is put back,
//     with its heroes (their spawn waits for a flag nothing sets any more)
//   - nameplate: every badge icon and background is owned, and the chosen pair is kept (the offline nameplate
//     backend answers with nothing and stores nothing)
//   - squads: invites, members, leader and kick between Steam friends over a Steam lobby (the backend's party
//     service is gone), served to the Lua squad frontends (custom\lua\squads.lua); a squad joins one team
//   - launch options: the ones the mod needs are added to the command line, so none have to be set in Steam;
//     a new player starts with a starter wallet
//
// Loaded by the game as Bin\version.dll (Shatterline.exe imports VERSION.dll and Windows looks in the exe's
// folder first). The real version.dll functions are forwarded to System32 (see proxy stubs at the end).
//
// Hooks are vtable slot swaps: each slot is checked against the value it has in the known game build before
// it is replaced. Any mismatch (other exe build) disables the DLL and the game runs unchanged.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <map>
#include <set>
#include <vector>

// ---------------------------------------------------------------------------------------------------------
// Game build this was reverse-engineered on (Shatterline.exe, Steam). VAs below assume the preferred base.
static const DWORD kTimeDateStamp = 0x694AAD28;
static const DWORD kSizeOfImage = 0x1591E000;
static const uintptr_t kPreferredBase = 0x140000000;

// WalletComponentStub vtable slot of its Init (sets prestige/hard = 10000 in the wallet map at +0x1b0)
static const uintptr_t kWalletInitSlot = 0x151666CD0, kWalletInitThunk = 0x14036B86C;
// WalletRequests vtables (WalletComponent and the stub): +0x10 AddCurrency, +0x18 GetCurrency, +0x28 UpdateCurrency
static const uintptr_t kAddCurrencySlots[] = { 0x1516661F8, 0x1516670E8 }, kAddCurrencyThunk = 0x14028A817;
static const uintptr_t kGetCurrencySlots[] = { 0x151666200, 0x1516670F0 }, kGetCurrencyThunk = 0x14001B4AF;
static const uintptr_t kUpdateCurrencySlots[] = { 0x151666210, 0x151667100 }, kUpdateCurrencyThunk = 0x14028B69F;
// Wallet map operator[] (AZStd::map<Currency, int>): returns a pointer to the amount, inserting 0 if missing
static const uintptr_t kWalletMapIndex = 0x1404CFFF5;
// The WalletRequests handler sits at component + 0x188; its wallet map at handler + 0x28
static const uintptr_t kHandlerOffset = 0x188, kHandlerMapOffset = 0x28;

// The player's inventory as the frontend sees it (AZ::Interface getter). HeroFrontend::OnHeroPerkUnlocked adds a
// bought perk with +0x50 AddItem(name, count); GachaBoxManager uses +0x68 HasItem(name).
static const uintptr_t kInventoryGetter = 0x1401D4391;
// FragLab::Inventory::Inventory vtable: +0x50 AddItem, +0x58 RemoveItem(name, count; 0 = all), +0x68 HasItem
static const uintptr_t kInventoryVtable = 0x151657BE8;
static const uintptr_t kInventoryAddItemThunk = 0x1403E0293, kInventoryRemoveItemThunk = 0x1403EE523,
                       kInventoryHasItemThunk = 0x14036830B;
static const size_t kInventoryAddItem = 0x50 / 8, kInventoryRemoveItem = 0x58 / 8, kInventoryHasItem = 0x68 / 8;
// Item link pass (owner): called right after the inventory is (re)loaded from the backend; walks the item list and
// re-attaches each item's saved slots (equipped perks, outfit... from selected_items.savedata) by name. A bought perk
// missing at that moment is unequipped ("Item ... should be present in player's inventory"). The inventory is
// owner + 0x18. Reached only through an incremental-link thunk.
static const uintptr_t kApplyItemLinks = 0x1481A1D20, kApplyItemLinksThunk = 0x14046A1FA;
static const unsigned char kApplyItemLinksProlog[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                                                       0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
static const size_t kLinksOwnerInventory = 0x18;
// Pieces of that pass reused to link the badge items: owner + 0x180 = AZStd::vector<shared_ptr<Item>> (16 bytes
// each); Inventory +0x20 Find(inventory, shared_ptr<Item>* out, name); Item +0x40 -> vector<shared_ptr<ItemSlot>>,
// Item +0xE8 SetParent(item, const shared_ptr<Item>*). ItemSlot (vtable 0x151658020): +0x08 Name, +0x10 linked item
// name, +0x58 Link(slot, const shared_ptr<Item>*), +0x60 Unlink, +0x68 Commit (the linked item becomes the saved one).
static const size_t kLinksOwnerItems = 0x180;
static const size_t kInventoryFind = 0x20 / 8;
static const uintptr_t kInventoryFindThunk = 0x14003F107;
static const size_t kItemSlots = 0x40 / 8, kItemSetParent = 0xE8 / 8;
static const uintptr_t kItemSlotVtable = 0x151658020;
static const size_t kSlotName = 0x08 / 8, kSlotLinkedName = 0x10 / 8, kSlotLink = 0x58 / 8, kSlotUnlink = 0x60 / 8,
                    kSlotCommit = 0x68 / 8;
static const uintptr_t kSlotNameThunk = 0x1402E3732, kSlotLinkedNameThunk = 0x14022E652, kSlotLinkThunk = 0x1402F06EE,
                       kSlotUnlinkThunk = 0x14007C746, kSlotCommitThunk = 0x1402EB6BC;
// Slots the client and server paks add to every preset (scripts\make_badge_items.ps1)
static const char* const kBadgeSlots[] = { "badge_icon", "badge_background" };
// An item every inventory owns; until HasItem finds it, the inventory hasn't been loaded yet
static const char* const kInventoryLoadedMarker = "h_strix";

// SessionResultsFrontend::InitSessionRewards(this, const PlayerId*, session, const map<PlayerId, vector<Reward>>*):
// the post-session screen receives the rewards the dedicated server calculated for every player. Online the backend
// credited them; the offline client only applies the xp. Called through an incremental-link thunk (E9 rel32).
static const uintptr_t kInitSessionRewards = 0x143C6C810, kInitSessionRewardsThunk = 0x140379BCE;
static const unsigned char kInitSessionRewardsProlog[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10 };
// Reward entry (0x50 bytes): +0x08 int type (1 = currency), +0x10 AZStd::string name (16-byte inline buffer,
// +0x20 size, +0x28 capacity; heap pointer when capacity > 15), +0x38 int amount
static const size_t kRewardSize = 0x50, kRewardType = 0x08, kRewardName = 0x10, kRewardNameSize = 0x20,
                    kRewardNameCapacity = 0x28, kRewardAmount = 0x38;
static const int kRewardTypeCurrency = 1, kRewardTypeItem = 5;

// Post-session chain (after a match): its builder appends ShowSessionResultsElement + DisconnectFromSessionElement
// through this appender (this + 0x50 = AZStd::vector<shared_ptr<Element>>, 16 bytes each), then the result-screen
// steps. MostValuablePlayersElement (the MVP scene) still ships but nothing appends it any more. It is built here
// the way the appender builds the results element: 0xF0 bytes from the system allocator, its constructor, a
// shared_ptr control block (0x28 bytes; the results element's control block type only calls virtual functions, so
// it serves any element), enable_shared_from_this hookup.
static const uintptr_t kAppendResultsThunk = 0x14016373B, kAppendResults = 0x1442A8290;
static const unsigned char kAppendResultsProlog[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
                                                      0x48, 0x8D, 0x6C, 0x24, 0xD1 };
static const size_t kChainElements = 0x50;
static const uintptr_t kVectorReserve = 0x14015C305;              // (vector*, count)
static const uintptr_t kSystemAllocator = 0x1404479CA;            // () -> object whose +8 is IAllocator*
static const uintptr_t kAllocateControlBlock = 0x140450B5B;       // (allocator name ref*, size, align, flags)
static const uintptr_t kAllocatorNameRef = 0x1510370C8;
static const uintptr_t kResultsControlBlockVtable = 0x151308940;
static const uintptr_t kEnableSharedFromThis = 0x140295B9F;       // (shared_ptr*, element*, element*)
static const uintptr_t kMvpElementCtorThunk = 0x1401E663B, kMvpElementCtor = 0x1442DCA70;
static const unsigned char kMvpElementCtorProlog[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x15 };
static const size_t kMvpElementSize = 0xF0;
// MVP element vtable +0x18 OnActivate: shows the scene when the "has MVP" query is true and the "campaign playlist"
// query is false; the queries are EBus broadcasts taking a member-function pointer to the handler call
static const uintptr_t kMvpOnActivateSlot = 0x151302C58 + 0x18, kMvpOnActivateThunk = 0x1401C36B3;
static const uintptr_t kQueryHasMvp = 0x140127E3E, kHasMvpCall = 0x14015E4C5;
static const uintptr_t kQueryCampaignPlaylist = 0x14045E4D6, kCampaignPlaylistCall = 0x14041F6EB;
// SessionPerformanceElement (the medals screen: instant rewards + session currencies), unreferenced like the MVP one.
// 0xA8 bytes (its deleting destructor frees that much). OnActivate shows it unless the "has session rewards" query is
// false or the campaign-playlist query is true.
static const uintptr_t kPerformanceElementCtorThunk = 0x1400C6224, kPerformanceElementCtor = 0x1442DDAB0;
static const unsigned char kPerformanceElementCtorProlog[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x15 };
static const size_t kPerformanceElementSize = 0xA8;
static const uintptr_t kPerformanceOnActivateSlot = 0x151302E28 + 0x18, kPerformanceOnActivateThunk = 0x1403B8743;
static const uintptr_t kQueryHasSessionRewards = 0x1400F35E4, kHasSessionRewardsCall = 0x140138964;
// DetailedSessionInfoElement (the session details screen: rewards / unlocks / scoreboard tabs, play again + leave),
// unreferenced too. 0xA8 bytes; OnActivate shows it unless the campaign-playlist query is true.
static const uintptr_t kDetailsElementCtorThunk = 0x1402586D7, kDetailsElementCtor = 0x1442DB320;
static const unsigned char kDetailsElementCtorProlog[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x15 };
static const size_t kDetailsElementSize = 0xA8;
static const uintptr_t kDetailsOnActivateSlot = 0x151302870 + 0x18, kDetailsOnActivateThunk = 0x140377522;
// The match highlight (the server's replay of the best player's moment, sent when its highlight_enabled is on):
// WaitForHighlightElement waits for the replay data (0x110 bytes; done at once if it has arrived, else up to
// flow_post_session_wait_for_highlight_duration), ShowHighlightElement plays it (0xA8 bytes; skipped by the game when
// there is no highlight or on a campaign playlist). Both are unreferenced like the MVP one. Show's handler at +0x88
// (vtable slot +8) ends it: broadcasts "hide highlight" (the post-session frontend stops the replay and raises
// on_replay_finished), disconnects, finishes the element. Elements finish through vtable +0x20.
static const uintptr_t kWaitHighlightCtorThunk = 0x14038654A, kWaitHighlightCtor = 0x1442DE100;
static const unsigned char kWaitHighlightCtorProlog[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x15 };
static const size_t kWaitHighlightSize = 0x110;
static const uintptr_t kWaitHighlightOnActivateSlot = 0x151303500 + 0x18, kWaitHighlightOnActivateThunk = 0x140096119;
static const uintptr_t kShowHighlightCtorThunk = 0x14046CA86, kShowHighlightCtor = 0x1442DDB00;
static const unsigned char kShowHighlightCtorProlog[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x15 };
static const size_t kShowHighlightSize = 0xA8;
static const uintptr_t kShowHighlightDtorSlot = 0x151302FC0 + 0x10, kShowHighlightDtorThunk = 0x140195B1E;
static const uintptr_t kShowHighlightOnActivateSlot = 0x151302FC0 + 0x18, kShowHighlightOnActivateThunk = 0x140165E23;
static const uintptr_t kShowHighlightEndSlot = 0x151303008 + 0x08, kShowHighlightEndThunk = 0x14023CF63;
static const size_t kShowHighlightHandler = 0x88, kElementFinish = 0x20;
static const uintptr_t kQueryHasHighlight = 0x140013778, kHasHighlightCall = 0x14005C3F6;

// FragLab::BackendAssets::Currency
static const char* const kCurrencyNames[] = { "invalid", "prestige", "hard", "shard", "spore", "capacitor",
                                              "energy_core", "red_matter", "pve_points", "engram" };
static const int kCurrencyCount = 10;

typedef void (*WalletInitFn)(void* component);
typedef void (*AddCurrencyFn)(void* handler, uint32_t currency, int delta);
typedef int (*GetCurrencyFn)(void* handler, uint32_t currency);
typedef void (*UpdateCurrencyFn)(void* handler, uint32_t currency, int amount);
typedef int* (*WalletMapIndexFn)(void* map, const uint32_t* currency);
typedef void* (*InventoryGetterFn)();
typedef uintptr_t (*InventoryAddItemFn)(void* inventory, const char* name, intptr_t count, intptr_t extra);
typedef void (*InventoryRemoveItemFn)(void* inventory, const char* name, size_t count);
typedef unsigned char (*InventoryHasItemFn)(void* inventory, const char* name);
typedef void (*ApplyItemLinksFn)(char* owner);
typedef void (*InitSessionRewardsFn)(void* self, const uint64_t* playerId, void* session, uintptr_t* rewardsByPlayer);

static uintptr_t g_base;
static WalletInitFn g_origWalletInit;
static AddCurrencyFn g_origAddCurrency;
static GetCurrencyFn g_origGetCurrency;
static UpdateCurrencyFn g_origUpdateCurrency;
static WalletMapIndexFn g_walletMapIndex;
static InventoryGetterFn g_inventoryGetter;
static InventoryAddItemFn g_origInventoryAddItem;
static InventoryRemoveItemFn g_inventoryRemoveItem;
static InventoryHasItemFn g_inventoryHasItem;
static ApplyItemLinksFn g_origApplyItemLinks;
static InitSessionRewardsFn g_origInitSessionRewards;
static void* g_walletHandler;                 // WalletRequests handler of the offline wallet (set at its Init)

static CRITICAL_SECTION g_lock;
static std::wstring g_dir, g_profilePath, g_logPath;
static std::map<std::string, int> g_wallet;   // currency name -> amount
static std::set<std::string> g_ownedItems;    // item names bought in the game
static std::set<std::string> g_carriedItems;  // tokens of the last successful extraction (next expedition's start)
static std::string g_nameplateIcon, g_nameplateBackground;   // chosen badge pair (empty: the game's defaults)
static bool g_profileLoaded;
static bool g_starterProfile;                 // no profile file yet: the starter wallet is saved as the first one
static bool g_inventoryHooked;                // AddItem hooked (inventory seen loaded once)
static bool g_inventoryBroken;                // interface not recognized: leave the inventory alone
static const ULONGLONG kInventoryCheckMs = 500;

static uintptr_t Va(uintptr_t preferredVa) { return preferredVa - kPreferredBase + g_base; }

// ---------------------------------------------------------------------------------------------------------
// Log: one per session, overwritten on every launch (the previous one is kept as offline_profile.prev.log)

static void Log(const char* fmt, ...)
{
    static bool truncated = false;
    FILE* f = _wfopen(g_logPath.c_str(), truncated ? L"ab" : L"wb");
    if (!f) return;
    truncated = true;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputs("\r\n", f);
    fclose(f);
}

// ---------------------------------------------------------------------------------------------------------
// Profile file: { "version": 1, "wallet": { "shard": 1095, ... }, "owned_items": [ "orbit_perk1_core_lvl1", ... ],
//                 "carried_items": [ "smg05_token", "grenade_1_token", ... ],
//                 "nameplate": { "icon": "bcn04", "background": "bgr10" } }

static int CurrencyIndex(const std::string& name)
{
    for (int i = 1; i < kCurrencyCount; ++i)
        if (name == kCurrencyNames[i]) return i;
    return 0;
}

// Items the profile keeps: hero perk levels (<hero>_perkN_core_lvlM)
static bool IsSavedItem(const char* name)
{
    return name && strstr(name, "_perk") && strstr(name, "_core_lvl");
}

// Gear an extraction carries over: extractable consumables (<weapon>_token, grenade_1_token, <perk>_token). Not the
// difficulty tokens (expedition_easy_etoken).
static bool IsCarriedItem(const std::string& name)
{
    static const char suffix[] = "_token";
    const size_t n = sizeof(suffix) - 1;
    return name.size() > n && name.compare(name.size() - n, n, suffix) == 0;
}

static void SkipSpace(const std::string& s, size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
}

static bool ReadString(const std::string& s, size_t& i, std::string& out)
{
    SkipSpace(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    out.clear();
    for (++i; i < s.size() && s[i] != '"'; ++i) {
        if (s[i] != '\\') { out += s[i]; continue; }
        if (++i >= s.size()) return false;
        switch (s[i]) {
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {                              // \uXXXX (BMP only), stored as UTF-8
            if (i + 4 >= s.size()) return false;
            char hex[5] = { s[i + 1], s[i + 2], s[i + 3], s[i + 4], 0 };
            char* end = nullptr;
            unsigned long c = strtoul(hex, &end, 16);
            if (end != hex + 4) return false;
            i += 4;
            if (c < 0x80) out += (char)c;
            else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
            else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
            break;
        }
        default: out += s[i];                   // \" \\ \/
        }
    }
    if (i >= s.size()) return false;
    ++i;
    return true;
}

// A JSON string literal: quotes, backslashes and control characters escaped (other bytes, UTF-8 too, as they are)
static std::string JsonQuote(const std::string& text)
{
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c < 0x20) { char esc[8]; snprintf(esc, sizeof(esc), "\\u%04x", c); out += esc; }
        else out += (char)c;
    }
    return out + "\"";
}

// Reads the "wallet" object: { "shard": 1095, ... }
static bool ParseWallet(const std::string& s, std::map<std::string, int>& wallet)
{
    size_t i = s.find("\"wallet\"");
    if (i == std::string::npos) return false;
    i = s.find('{', i);
    if (i == std::string::npos) return false;
    ++i;
    for (;;) {
        SkipSpace(s, i);
        if (i < s.size() && s[i] == '}') return true;
        std::string name;
        if (!ReadString(s, i, name)) return false;
        SkipSpace(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        SkipSpace(s, i);
        char* end = nullptr;
        long value = strtol(s.c_str() + i, &end, 10);
        if (end == s.c_str() + i) return false;
        i = end - s.c_str();
        wallet[name] = (int)value;
        SkipSpace(s, i);
        if (i < s.size() && s[i] == ',') ++i;
    }
}

// Reads an array of strings ("owned_items", "carried_items"); a profile without it simply has none yet
static bool ParseItems(const std::string& s, const char* key, std::set<std::string>& items)
{
    size_t i = s.find(std::string("\"") + key + "\"");
    if (i == std::string::npos) return true;
    i = s.find('[', i);
    if (i == std::string::npos) return false;
    ++i;
    for (;;) {
        SkipSpace(s, i);
        if (i < s.size() && s[i] == ']') return true;
        std::string name;
        if (!ReadString(s, i, name)) return false;
        items.insert(name);
        SkipSpace(s, i);
        if (i < s.size() && s[i] == ',') ++i;
    }
}

// Reads the "nameplate" object: { "icon": "bcn04", "background": "bgr10" }; a profile without it has the defaults
static bool ParseNameplate(const std::string& s, std::string& icon, std::string& background)
{
    size_t i = s.find("\"nameplate\"");
    if (i == std::string::npos) return true;
    i = s.find('{', i);
    if (i == std::string::npos) return false;
    ++i;
    for (;;) {
        SkipSpace(s, i);
        if (i < s.size() && s[i] == '}') return true;
        std::string key, value;
        if (!ReadString(s, i, key)) return false;
        SkipSpace(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        if (!ReadString(s, i, value)) return false;
        if (key == "icon") icon = value;
        else if (key == "background") background = value;
        SkipSpace(s, i);
        if (i < s.size() && s[i] == ',') ++i;
    }
}

// A new player's wallet: enough materials for the first perk levels, so progression starts at once
static const std::pair<const char*, int> kStarterWallet[] = {
    { "prestige", 10000 }, { "hard", 10000 }, { "shard", 3000 }, { "spore", 1500 }, { "capacitor", 800 },
    { "energy_core", 10 }, { "red_matter", 10 },
};

static void LoadProfile()
{
    // Only a profile that doesn't exist means a new player. Any other failure (the file locked for a moment by an
    // antivirus scan or a sync tool, access denied) must not start a starter wallet that would be saved over it.
    if (GetFileAttributesW(g_profilePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            // Restored into the wallet when it starts and saved there (HookWalletInit), like a loaded profile
            for (auto& kv : kStarterWallet) g_wallet[kv.first] = kv.second;
            g_profileLoaded = true;
            g_starterProfile = true;
            Log("No profile yet (%ls); starting with the starter wallet", g_profilePath.c_str());
            return;
        }
        Log("ERROR: cannot check %ls (error %lu); nothing is saved this session", g_profilePath.c_str(), error);
        return;
    }
    FILE* f = nullptr;
    for (int attempt = 0; attempt < 10 && !f; ++attempt) {   // a scan or sync holds the file only briefly
        if (attempt) Sleep(100);
        f = _wfopen(g_profilePath.c_str(), L"rb");
    }
    if (!f) {
        Log("ERROR: cannot open %ls (%s); the file is left untouched and nothing is saved this session",
            g_profilePath.c_str(), strerror(errno));
        return;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    std::map<std::string, int> wallet;
    std::set<std::string> items, carried;
    std::string icon, background;
    if (!ParseWallet(text, wallet) || !ParseItems(text, "owned_items", items) ||
        !ParseItems(text, "carried_items", carried) || !ParseNameplate(text, icon, background)) {
        Log("ERROR: could not read %ls; the file is left untouched and nothing is saved this session (fix or "
            "delete it; offline_profile.json.bak is the profile from before the last session's first save)",
            g_profilePath.c_str());
        return;
    }
    g_wallet = wallet;
    g_ownedItems = items;
    g_carriedItems = carried;
    g_nameplateIcon = icon;
    g_nameplateBackground = background;
    g_profileLoaded = true;
    Log("Profile loaded: %u currencies, %u owned items, %u carried items, nameplate '%s' / '%s'",
        (unsigned)g_wallet.size(), (unsigned)g_ownedItems.size(), (unsigned)g_carriedItems.size(),
        g_nameplateIcon.c_str(), g_nameplateBackground.c_str());
}

// Written to a temporary file first and then renamed over the profile, so a crash never leaves half a file
static void SaveProfile()
{
    std::string text = "{\r\n    \"version\": 1,\r\n    \"wallet\": {";
    bool first = true;
    for (int c = 1; c < kCurrencyCount; ++c) {
        auto it = g_wallet.find(kCurrencyNames[c]);
        if (it == g_wallet.end()) continue;
        char line[96];
        snprintf(line, sizeof(line), "%s\r\n        \"%s\": %d", first ? "" : ",", it->first.c_str(), it->second);
        text += line;
        first = false;
    }
    text += "\r\n    },\r\n    \"owned_items\": [";
    first = true;
    for (const std::string& item : g_ownedItems) {
        text += first ? "\r\n        " : ",\r\n        ";
        text += JsonQuote(item);
        first = false;
    }
    text += "\r\n    ],\r\n    \"carried_items\": [";
    first = true;
    for (const std::string& item : g_carriedItems) {
        text += first ? "\r\n        " : ",\r\n        ";
        text += JsonQuote(item);
        first = false;
    }
    text += "\r\n    ]";
    if (!g_nameplateIcon.empty() || !g_nameplateBackground.empty()) {
        text += ",\r\n    \"nameplate\": {\r\n        \"icon\": " + JsonQuote(g_nameplateIcon) +
                ",\r\n        \"background\": " + JsonQuote(g_nameplateBackground) + "\r\n    }";
    }
    text += "\r\n}\r\n";

    // The profile as it was loaded is kept as offline_profile.json.bak once per session, before the first save: a
    // way back if a session goes wrong (a game update, a bug), one session deep
    static bool backedUp = false;
    if (!backedUp) {
        backedUp = true;
        std::wstring bak = g_profilePath + L".bak";
        if (CopyFileW(g_profilePath.c_str(), bak.c_str(), FALSE)) Log("Profile backup: %ls", bak.c_str());
        else if (GetLastError() != ERROR_FILE_NOT_FOUND) Log("ERROR: profile backup failed (%lu)", GetLastError());
    }

    std::wstring tmp = g_profilePath + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) { Log("ERROR: cannot write %ls", tmp.c_str()); return; }
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = (fclose(f) == 0) && ok;
    if (!ok || !MoveFileExW(tmp.c_str(), g_profilePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        Log("ERROR: saving the profile failed (%lu)", GetLastError());
}

// ---------------------------------------------------------------------------------------------------------
// Wallet hooks

// The offline wallet starts here. Saved amounts are written into its map right after the stub's own defaults.
static void HookWalletInit(void* component)
{
    g_origWalletInit(component);
    EnterCriticalSection(&g_lock);
    g_walletHandler = (char*)component + kHandlerOffset;
    void* map = (char*)component + kHandlerOffset + kHandlerMapOffset;
    if (g_profileLoaded) {
        for (auto& kv : g_wallet) {
            uint32_t c = CurrencyIndex(kv.first);
            if (c == 0) { Log("Unknown currency '%s' in profile, ignored", kv.first.c_str()); continue; }
            *g_walletMapIndex(map, &c) = kv.second;
        }
    }
    std::string summary;
    for (uint32_t c = 1; c < (uint32_t)kCurrencyCount; ++c) {
        int amount = *g_walletMapIndex(map, &c);
        g_wallet[kCurrencyNames[c]] = amount;
        summary += std::string(" ") + kCurrencyNames[c] + "=" + std::to_string(amount);
    }
    Log("Wallet %s:%s", g_starterProfile ? "starter" : g_profileLoaded ? "restored" : "game defaults, NOT saved",
        summary.c_str());
    // Only a new player's starter wallet is saved here. A profile that could not be read stays as it is: nothing is
    // saved this session (g_profileLoaded stays false), so the player's progress is never replaced by defaults.
    if (g_starterProfile) {
        g_starterProfile = false;
        SaveProfile();
    }
    LeaveCriticalSection(&g_lock);
}

static void Remember(uint32_t currency, int amount, const char* how)
{
    if (currency == 0 || currency >= (uint32_t)kCurrencyCount) return;
    EnterCriticalSection(&g_lock);
    if (g_profileLoaded) {
        int& slot = g_wallet[kCurrencyNames[currency]];
        if (slot != amount) {
            Log("%s %s: %d -> %d", how, kCurrencyNames[currency], slot, amount);
            slot = amount;
            SaveProfile();
        }
    }
    LeaveCriticalSection(&g_lock);
}

// Purchases, box openings and backend syncs set the exact new amount
static void HookUpdateCurrency(void* handler, uint32_t currency, int amount)
{
    g_origUpdateCurrency(handler, currency, amount);
    Remember(currency, amount, "Set");
}

static void HookAddCurrency(void* handler, uint32_t currency, int delta)
{
    g_origAddCurrency(handler, currency, delta);
    if (currency == 0 || currency >= (uint32_t)kCurrencyCount) return;
    int amount = *g_walletMapIndex((char*)handler + kHandlerMapOffset, &currency);
    Remember(currency, amount, "Add");
}

// ---------------------------------------------------------------------------------------------------------
// Inventory

// Every item the game adds to the player's inventory passes here; bought perks are saved
static uintptr_t HookInventoryAddItem(void* inventory, const char* name, intptr_t count, intptr_t extra)
{
    uintptr_t result = g_origInventoryAddItem(inventory, name, count, extra);
    if (IsSavedItem(name)) {
        EnterCriticalSection(&g_lock);
        if (g_profileLoaded && g_ownedItems.insert(name).second) {
            Log("Bought %s", name);
            SaveProfile();
        }
        LeaveCriticalSection(&g_lock);
    }
    return result;
}

static void WriteSlot(uintptr_t slot, void* target);

// Badge items: the chosen badge pair travels to the other players as two items, badge_<icon> and
// badge_<background>. The client sends its whole inventory to the server when it joins; the server keeps only items
// its own pak defines (others are dropped), and its session results carry each player's EQUIPPED items (linked into
// a slot) to every client, so the pair is linked into the badge_icon / badge_background slots of every preset.
static const char* const kBadgeItemPrefix = "badge_";
static const size_t kBadgeItemPrefixLength = 6;
static const int kBadgeIcon = 0, kBadgeBackground = 1;
static bool IsKnownBadge(const std::string& name, int type);
static std::set<std::string> g_badgeItems;    // badge items added to the inventory this session

static std::set<std::string> ChosenBadgeItems()
{
    std::set<std::string> items;
    if (IsKnownBadge(g_nameplateIcon, kBadgeIcon)) items.insert(kBadgeItemPrefix + g_nameplateIcon);
    if (IsKnownBadge(g_nameplateBackground, kBadgeBackground)) items.insert(kBadgeItemPrefix + g_nameplateBackground);
    return items;
}

// Links the chosen badge items into the badge slots of every item that has them (the presets), the way the item
// link pass re-attaches saved slots; a slot whose badge was reset to the default is emptied. Only touches slots
// that differ, so it is cheap to run on every lobby check.
struct SharedPtr { void* object; void* control; };
typedef void (*VoidFn)(void* self);
typedef const char* (*NameFn)(void* self);
typedef void (*SharedPtrArgFn)(void* self, const SharedPtr* arg);
typedef void (*InventoryFindFn)(void* inventory, SharedPtr* out, const char* name);
struct SharedPtrVector { SharedPtr* begin; SharedPtr* end; };
typedef SharedPtrVector* (*ItemSlotsFn)(void* item);
static bool g_badgeSlots;                     // ItemSlot / Inventory code recognized

static void* VirtualFn(void* object, size_t index) { return (*(void***)object)[index]; }

static void Release(SharedPtr& p)
{
    char* control = (char*)p.control;
    p.object = p.control = nullptr;
    if (!control) return;
    if (InterlockedDecrement((volatile LONG*)(control + 8)) == 0) {
        ((VoidFn)VirtualFn(control, 1))(control);
        if (InterlockedDecrement((volatile LONG*)(control + 0xC)) == 0) ((VoidFn)VirtualFn(control, 2))(control);
    }
}

static void LinkBadgeItems(void* inventory)
{
    if (!g_badgeSlots) return;
    const std::string wanted[2] = {
        IsKnownBadge(g_nameplateIcon, kBadgeIcon) ? kBadgeItemPrefix + g_nameplateIcon : std::string(),
        IsKnownBadge(g_nameplateBackground, kBadgeBackground) ? kBadgeItemPrefix + g_nameplateBackground : std::string() };
    SharedPtr found[2] = {};
    for (int k = 0; k < 2; ++k)
        if (!wanted[k].empty()) ((InventoryFindFn)VirtualFn(inventory, kInventoryFind))(inventory, &found[k], wanted[k].c_str());

    const SharedPtrVector* items = (SharedPtrVector*)((char*)inventory - kLinksOwnerInventory + kLinksOwnerItems);
    unsigned linked = 0, emptied = 0;
    for (SharedPtr* item = items->begin; item && item != items->end; ++item) {
        if (!item->object) continue;
        const SharedPtrVector* slots = ((ItemSlotsFn)VirtualFn(item->object, kItemSlots))(item->object);
        for (SharedPtr* s = slots->begin; s && s != slots->end; ++s) {
            void* slot = s->object;
            if (!slot || *(uintptr_t*)slot != Va(kItemSlotVtable)) continue;
            const char* name = ((NameFn)VirtualFn(slot, kSlotName))(slot);
            for (int k = 0; k < 2; ++k) {
                if (strcmp(name, kBadgeSlots[k]) != 0) continue;
                const char* current = ((NameFn)VirtualFn(slot, kSlotLinkedName))(slot);
                if (found[k].object) {
                    if (wanted[k] == current) break;
                    ((SharedPtrArgFn)VirtualFn(slot, kSlotLink))(slot, &found[k]);
                    ((SharedPtrArgFn)VirtualFn(found[k].object, kItemSetParent))(found[k].object, item);
                    ++linked;
                } else {
                    if (!*current) break;
                    ((VoidFn)VirtualFn(slot, kSlotUnlink))(slot);
                    ++emptied;
                }
                ((VoidFn)VirtualFn(slot, kSlotCommit))(slot);
                break;
            }
        }
    }
    for (SharedPtr& p : found) Release(p);
    if (linked || emptied)
        Log("Badge slots: %u linked (icon '%s', background '%s'), %u emptied", linked, wanted[0].c_str(),
            wanted[1].c_str(), emptied);
}

// Re-adds the saved items the player's inventory lacks. Called under g_lock; g_restoring stops a nested restore
// should adding an item re-enter one of the hooks. Items are removed only when the caller allows it (never during
// the item link pass, which walks the item list).
static bool g_restoring;
static void RestoreOwnedItems(void* inventory, const char* when, bool mayRemove)
{
    if (g_restoring) return;
    g_restoring = true;
    unsigned added = 0;
    std::set<std::string> badges = ChosenBadgeItems();
    for (const std::set<std::string>* items : { &g_ownedItems, &g_carriedItems, &badges }) {
        for (const std::string& item : *items) {
            if (g_inventoryHasItem(inventory, item.c_str())) continue;
            g_origInventoryAddItem(inventory, item.c_str(), 1, 0);
            ++added;
        }
    }
    if (mayRemove) LinkBadgeItems(inventory);    // the new pair takes the slots before the old items go
    for (auto it = g_badgeItems.begin(); mayRemove && g_inventoryRemoveItem && it != g_badgeItems.end();) {
        if (badges.count(*it)) { ++it; continue; }
        if (g_inventoryHasItem(inventory, it->c_str())) g_inventoryRemoveItem(inventory, it->c_str(), 0);
        Log("Badge item %s removed (badge changed)", it->c_str());
        it = g_badgeItems.erase(it);
    }
    for (const std::string& item : badges)
        if (g_badgeItems.insert(item).second) Log("Badge item %s in the inventory (shown to other players)", item.c_str());
    g_restoring = false;
    if (added)
        Log("Inventory restored %s: %u of %u owned + carried + badge items added", when, added,
            (unsigned)(g_ownedItems.size() + g_carriedItems.size() + badges.size()));
}

// Runs on wallet reads (the lobby UI reads it constantly). The game reloads the inventory file after login and
// again after every match (SyncProfileChainEntity), which drops bought perks, so whenever the inventory is loaded
// the saved items it lacks are re-added. The first time, AddItem is also hooked to record new purchases.
static void TryRestoreInventory()
{
    void* inventory = g_inventoryGetter();
    if (!inventory) return;
    if (*(uintptr_t*)inventory != Va(kInventoryVtable)) {
        g_inventoryBroken = true;
        Log("ERROR: inventory interface is not where expected; owned items are not restored or saved");
        return;
    }
    if (!g_inventoryHasItem(inventory, kInventoryLoadedMarker)) return;

    if (!g_inventoryHooked) {
        WriteSlot(kInventoryVtable + kInventoryAddItem * 8, (void*)&HookInventoryAddItem);
        g_inventoryHooked = true;
        Log("Inventory loaded, recording purchases");
    }
    RestoreOwnedItems(inventory, "in lobby", true);
}

// The inventory has just been (re)loaded and its saved slots are about to be re-attached: the owned items go back
// in first so equipped perks stay equipped. Not during the pass itself: it walks the item list being added to.
// Afterwards the badge items are linked (the pass only re-attaches what the game itself saved).
static void HookApplyItemLinks(char* owner)
{
    void* inventory = owner + kLinksOwnerInventory;
    bool ours = false;
    if (!g_inventoryBroken) {
        EnterCriticalSection(&g_lock);
        ours = g_profileLoaded && inventory == g_inventoryGetter() && *(uintptr_t*)inventory == Va(kInventoryVtable);
        if (ours) RestoreOwnedItems(inventory, "before equipping", false);
        LeaveCriticalSection(&g_lock);
    }
    g_origApplyItemLinks(owner);
    if (ours) {
        EnterCriticalSection(&g_lock);
        LinkBadgeItems(inventory);
        LeaveCriticalSection(&g_lock);
    }
}

static int HookGetCurrency(void* handler, uint32_t currency)
{
    static ULONGLONG lastCheck;
    ULONGLONG now = GetTickCount64();
    if (!g_inventoryBroken && now - lastCheck >= kInventoryCheckMs) {
        EnterCriticalSection(&g_lock);
        lastCheck = now;
        if (!g_inventoryBroken && g_profileLoaded) TryRestoreInventory();
        LeaveCriticalSection(&g_lock);
    }
    return g_origGetCurrency(handler, currency);
}

// ---------------------------------------------------------------------------------------------------------
// Mission rewards

// The rewards of one player: the same lower_bound walk over the AZStd::map the game does (node +0x08 left,
// +0x10 right, +0x18 key, +0x20/+0x28 vector begin/end; header word 0 = root | color bit)
static bool FindPlayerRewards(const uintptr_t* header, uint64_t player, const char*& begin, const char*& end)
{
    const uintptr_t* node = (const uintptr_t*)(header[0] & ~(uintptr_t)1);
    const uintptr_t* found = header;
    while (node) {
        if ((uint64_t)node[3] < player) node = (const uintptr_t*)node[2];
        else { found = node; node = (const uintptr_t*)node[1]; }
    }
    if (found == header || (uint64_t)found[3] > player) return false;
    begin = (const char*)found[4];
    end = (const char*)found[5];
    return begin && end >= begin && (size_t)(end - begin) % kRewardSize == 0 && (size_t)(end - begin) < 100 * kRewardSize;
}

static std::string RewardName(const char* reward)
{
    size_t size = *(const size_t*)(reward + kRewardNameSize);
    size_t capacity = *(const size_t*)(reward + kRewardNameCapacity);
    if (size > 256 || size > capacity) return std::string();
    const char* text = capacity > 15 ? *(const char* const*)(reward + kRewardName) : reward + kRewardName;
    return text ? std::string(text, size) : std::string();
}

// The mission of the server joined last (from the server list, via Lua's "match" call): only an expedition changes the
// carried gear. Expeditions are the missions with game mode pve_horde_v2; in this build exactly these three.
static const char* const kExpeditionMissions[] = { "isl_march_v2_easy", "isl_march_v2_medium", "isl_march_v2_hard" };
static std::string g_matchMission;            // empty: not known (a join that didn't come through the server list)

static void SetMatchMission(const std::string& mission)
{
    EnterCriticalSection(&g_lock);
    g_matchMission = mission.size() <= 64 && mission.find_first_of("\t\r\n") == std::string::npos ? mission : "";
    LeaveCriticalSection(&g_lock);
    Log("Match on mission '%s'", g_matchMission.c_str());
}

static int MatchKind()                        // 1 expedition, 0 another mode, -1 not known
{
    if (g_matchMission.empty()) return -1;
    for (const char* m : kExpeditionMissions) if (_stricmp(g_matchMission.c_str(), m) == 0) return 1;
    return 0;
}

// Materials the server granted for the mission are added to the wallet, and the gear tokens become the next
// expedition's start loadout (the server grants both only for a successful extraction; a failed one gives xp only,
// so the player starts the next one with nothing carried, as in the original game). Other modes (PvP, co-op
// missions) leave the carried gear alone: PvP starts with the loadout from customization, not the carried gear.
static void CreditSessionRewards(const uint64_t* playerId, uintptr_t* rewardsByPlayer)
{
    const char *begin, *end;
    if (!playerId || !rewardsByPlayer || !FindPlayerRewards(rewardsByPlayer, *playerId, begin, end)) {
        Log("Mission rewards: none for this player");
        return;
    }
    EnterCriticalSection(&g_lock);
    std::set<std::string> carried;
    for (const char* r = begin; r < end; r += kRewardSize) {
        int type = *(const int*)(r + kRewardType);
        int amount = *(const int*)(r + kRewardAmount);
        std::string name = RewardName(r);
        int currency = type == kRewardTypeCurrency ? CurrencyIndex(name) : 0;
        // prestige and hard are the premium currencies; the server never grants them for missions
        bool credit = currency >= 3 && amount > 0 && g_walletHandler && g_profileLoaded;
        bool carry = type == kRewardTypeItem && amount > 0 && IsCarriedItem(name);
        Log("Mission reward: type %d '%s' %d%s", type, name.c_str(), amount,
            credit ? "" : carry ? " (carried to the next expedition)" : " (not a material)");
        if (credit) HookAddCurrency(g_walletHandler, (uint32_t)currency, amount);   // adds and saves
        if (carry) carried.insert(name);
    }
    // An expedition sets the carried gear (none after a failed one). Another mode keeps it. Mode not known: only gear
    // tokens (a successful extraction) replace it, so a PvP match or a failed run can't clear it by mistake.
    int kind = MatchKind();
    bool update = kind == 1 || (kind == -1 && !carried.empty());
    if (!update && carried != g_carriedItems)
        Log("Carried gear kept for the next expedition (%s)", kind == 0 ? "not an expedition" : "mode not known, no gear");
    if (update && g_profileLoaded && carried != g_carriedItems) {
        if (carried.empty()) Log("No gear carried: the next expedition starts without carried items");
        g_carriedItems = carried;
        SaveProfile();
    }
    LeaveCriticalSection(&g_lock);
}

static void SquadsSessionEnded();

static void HookInitSessionRewards(void* self, const uint64_t* playerId, void* session, uintptr_t* rewardsByPlayer)
{
    SquadsSessionEnded();
    CreditSessionRewards(playerId, rewardsByPlayer);
    g_origInitSessionRewards(self, playerId, session, rewardsByPlayer);
}

// A page within rel32 reach of the game's code holding "jmp [rip+0]; <hook address>"
static void* MakeTrampoline(uintptr_t caller, void* hook)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t step = si.dwAllocationGranularity;
    DWORD firstError = 0;
    unsigned tries = 0;
    for (uintptr_t a = (g_base & ~(step - 1)) - step; a > caller - 0x7FF00000 && a > step; a -= step) {
        ++tries;
        unsigned char* p = (unsigned char*)VirtualAlloc((void*)a, si.dwPageSize, MEM_RESERVE | MEM_COMMIT,
                                                        PAGE_READWRITE);
        if (!p) { if (!firstError) firstError = GetLastError(); continue; }
        p[0] = 0xFF; p[1] = 0x25; memset(p + 2, 0, 4);
        memcpy(p + 6, &hook, sizeof(hook));
        DWORD old;
        if (!VirtualProtect(p, si.dwPageSize, PAGE_EXECUTE_READ, &old)) {
            Log("Trampoline at %p cannot be made executable (error %lu)", p, GetLastError());
            VirtualFree(p, 0, MEM_RELEASE);
            return nullptr;
        }
        return p;
    }
    Log("Trampoline: %u addresses tried below %p, first error %lu", tries, (void*)g_base, firstError);
    return nullptr;
}

// Redirects an incremental-link thunk "jmp function" to the hook. Returns the function (to call from the hook), or
// null when the code isn't the expected one; `what` names the feature lost then.
static void* RedirectThunk(uintptr_t thunkVa, uintptr_t functionVa, const unsigned char* prolog, size_t prologSize,
                           void* hook, const char* what)
{
    unsigned char* thunk = (unsigned char*)Va(thunkVa);
    uintptr_t function = Va(functionVa);
    if (thunk[0] != 0xE9 || (uintptr_t)(thunk + 5 + *(int32_t*)(thunk + 1)) != function ||
        memcmp((void*)function, prolog, prologSize) != 0) {
        Log("Game code is not where expected; %s", what);
        return nullptr;
    }
    unsigned char* trampoline = (unsigned char*)MakeTrampoline((uintptr_t)thunk, hook);
    if (!trampoline) {
        Log("No memory near the game code; %s", what);
        return nullptr;
    }
    int32_t rel = (int32_t)((intptr_t)trampoline - (intptr_t)(thunk + 5));
    DWORD old;
    VirtualProtect(thunk + 1, 4, PAGE_EXECUTE_READWRITE, &old);
    memcpy(thunk + 1, &rel, 4);
    VirtualProtect(thunk + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), thunk, 5);
    return (void*)function;
}

// Optional, the rest of the profile works without it
static void InstallSessionRewardsHook()
{
    g_origInitSessionRewards = (InitSessionRewardsFn)RedirectThunk(
        kInitSessionRewardsThunk, kInitSessionRewards, kInitSessionRewardsProlog, sizeof(kInitSessionRewardsProlog),
        (void*)&HookInitSessionRewards, "materials from missions are not added");
}

// ---------------------------------------------------------------------------------------------------------
// Post-session MVP scene

typedef void (*AppendResultsFn)(char* chain);
typedef void (*VectorReserveFn)(void* vector, size_t count);
typedef char* (*SystemAllocatorFn)();
typedef void* (*AllocateFn)(void* allocator, size_t size, size_t align, int flags, const char* name, const char* file,
                            int line, int suppress);
typedef uintptr_t* (*AllocateControlBlockFn)(void* nameRef, size_t size, size_t align, int flags);
typedef void (*EnableSharedFromThisFn)(void* sharedPtr, void* element, void* element2);
typedef void (*ElementCtorFn)(void* element);
typedef void (*ElementCallFn)(void* element);
struct MemberCall { uintptr_t function; int adjust; };
typedef void (*BoolQueryFn)(bool* result, const MemberCall* call);

static bool SlotHolds(uintptr_t slot, uintptr_t expected);
static AppendResultsFn g_origAppendResults;
static ElementCallFn g_origMvpOnActivate;
static ElementCallFn g_origPerformanceOnActivate;
static ElementCallFn g_origDetailsOnActivate;
static bool g_performanceStep;   // the medals step can be built (its code is where expected)
static bool g_detailsStep;       // the session details step can be built
static bool g_highlightSteps;    // the highlight wait + show steps can be built
static ElementCallFn g_origWaitHighlightOnActivate;
static ElementCallFn g_origShowHighlightOnActivate;
static ElementCallFn g_origShowHighlightEnd;
typedef void* (*ElementDeleteFn)(void* element, unsigned flags);
static ElementDeleteFn g_origShowHighlightDtor;
static char* g_showingHighlight;   // the ShowHighlightElement whose replay is playing (the skip key ends it)
static DWORD g_highlightWaitStart;

// A new post-session step element (constructor thunk, size) owned by a shared_ptr {element, control block}; false if
// out of memory
static bool MakeElement(uintptr_t ctorThunk, size_t size, uintptr_t sharedPtr[2])
{
    char* system = ((SystemAllocatorFn)Va(kSystemAllocator))();
    void* allocator = *(void**)(system + 8);
    AllocateFn allocate = (AllocateFn)(*(uintptr_t**)allocator)[1];
    void* element = allocate(allocator, size, 8, 0, nullptr, nullptr, 0, 0);
    if (!element) return false;
    ((ElementCtorFn)Va(ctorThunk))(element);
    uintptr_t nameRef = Va(kAllocatorNameRef);
    uintptr_t* block = ((AllocateControlBlockFn)Va(kAllocateControlBlock))(&nameRef, 0x28, 8, 0);
    if (!block) {
        (*(void (**)(void*, int))(*(uintptr_t*)element + 0x10))(element, 1);   // virtual deleting destructor
        return false;
    }
    block[0] = Va(kResultsControlBlockVtable);
    ((int*)block)[2] = 1;   // uses
    ((int*)block)[3] = 1;   // weak
    block[2] = (uintptr_t)element;
    block[4] = nameRef;
    sharedPtr[0] = (uintptr_t)element;
    sharedPtr[1] = (uintptr_t)block;
    ((EnableSharedFromThisFn)Va(kEnableSharedFromThis))(sharedPtr, element, element);
    return true;
}

// After the results and disconnect steps are appended, the highlight steps, the MVP step, the medals step and the
// session details step go between them (the order of the regular post-session screen): the replay and the MVP scene
// play on the session's map, so they must come before leaving the server
static void HookAppendResults(char* chain)
{
    uintptr_t** vec = (uintptr_t**)(chain + kChainElements);   // begin, end, capacity end
    size_t before = vec[1] - vec[0];
    g_origAppendResults(chain);
    if (vec[1] - vec[0] != (ptrdiff_t)before + 4) {
        Log("MVP scene not added: the post-session steps are not the expected ones");
        return;
    }
    uintptr_t steps[5][2];
    size_t adding = 0;
    bool highlight = false;
    if (g_highlightSteps) {
        // a wait step without its show step only waits out its time, so it can stay if the second one fails
        if (MakeElement(kWaitHighlightCtorThunk, kWaitHighlightSize, steps[adding])) {
            ++adding;
            if (MakeElement(kShowHighlightCtorThunk, kShowHighlightSize, steps[adding])) {
                ++adding;
                highlight = true;
            } else Log("Match highlight not added: out of memory");
        } else Log("Match highlight not added: out of memory");
    }
    if (MakeElement(kMvpElementCtorThunk, kMvpElementSize, steps[adding])) ++adding;
    else Log("MVP scene not added: out of memory");
    if (g_performanceStep) {
        if (MakeElement(kPerformanceElementCtorThunk, kPerformanceElementSize, steps[adding])) ++adding;
        else Log("Medals screen not added: out of memory");
    }
    if (g_detailsStep) {
        if (MakeElement(kDetailsElementCtorThunk, kDetailsElementSize, steps[adding])) ++adding;
        else Log("Session details not added: out of memory");
    }
    if (!adding) return;
    size_t count = (vec[1] - vec[0]) / 2;
    ((VectorReserveFn)Va(kVectorReserve))(vec, count + adding);
    // shared_ptrs are plain {pointer, control block} pairs: moving the last one (disconnect) up transfers its ownership
    uintptr_t* last = vec[1] - 2;
    last[adding * 2] = last[0];
    last[adding * 2 + 1] = last[1];
    for (size_t i = 0; i < adding; ++i) {
        last[i * 2] = steps[i][0];
        last[i * 2 + 1] = steps[i][1];
    }
    vec[1] += adding * 2;
    Log("%u post-session steps added (%sMVP scene%s%s), %u steps", (unsigned)adding,
        highlight ? "match highlight + " : "", g_performanceStep ? " + medals screen" : "",
        g_detailsStep ? " + session details" : "", (unsigned)(count + adding));
}

static bool CampaignPlaylist()
{
    MemberCall campaign = { Va(kCampaignPlaylistCall), 0 };
    bool isCampaign = false;
    ((BoolQueryFn)Va(kQueryCampaignPlaylist))(&isCampaign, &campaign);
    return isCampaign;
}

static bool HighlightReceived()
{
    MemberCall hasHighlight = { Va(kHasHighlightCall), 0 };
    bool has = false;
    ((BoolQueryFn)Va(kQueryHasHighlight))(&has, &hasHighlight);
    return has;
}

// PvE servers never send a highlight, so the wait step is skipped there instead of waiting out its time
static void HookWaitHighlightOnActivate(void* element)
{
    if (CampaignPlaylist()) {
        (*(ElementCallFn*)(*(uintptr_t*)element + kElementFinish))(element);
        return;
    }
    bool has = HighlightReceived();
    Log("Match highlight: %s", has ? "received" : "waiting for the server's replay");
    g_highlightWaitStart = GetTickCount();
    g_origWaitHighlightOnActivate(element);
}

static void HookShowHighlightOnActivate(void* element)
{
    if (CampaignPlaylist()) {
        g_origShowHighlightOnActivate(element);   // the game skips it
        return;
    }
    bool has = HighlightReceived();
    Log("Match highlight: %s after %.1f s", has ? "playing" : "none (the server sends none: highlight_enabled 0?)",
        (GetTickCount() - g_highlightWaitStart) / 1000.0);
    g_showingHighlight = has ? (char*)element : nullptr;
    g_origShowHighlightOnActivate(element);
}

static void HookShowHighlightEnd(void* handler)
{
    if (g_showingHighlight == (char*)handler - kShowHighlightHandler) {
        g_showingHighlight = nullptr;
        Log("Match highlight: finished");
    }
    g_origShowHighlightEnd(handler);
}

static void* HookShowHighlightDtor(void* element, unsigned flags)
{
    if (g_showingHighlight == element) g_showingHighlight = nullptr;
    return g_origShowHighlightDtor(element, flags);
}

// The skip key (Lua, through the squad bridge): ends the playing replay the way its own end does
static bool SkipHighlight()
{
    if (!g_showingHighlight) return false;
    Log("Match highlight: skipped");
    HookShowHighlightEnd(g_showingHighlight + kShowHighlightHandler);
    return true;
}

static void HookDetailsOnActivate(void* element)
{
    MemberCall campaign = { Va(kCampaignPlaylistCall), 0 };
    bool isCampaign = false;
    ((BoolQueryFn)Va(kQueryCampaignPlaylist))(&isCampaign, &campaign);
    Log("Session details: %s", isCampaign ? "skipped by the game (campaign playlist)" : "showing");
    g_origDetailsOnActivate(element);
}

static void HookPerformanceOnActivate(void* element)
{
    MemberCall hasRewards = { Va(kHasSessionRewardsCall), 0 }, campaign = { Va(kCampaignPlaylistCall), 0 };
    bool has = false, isCampaign = false;
    ((BoolQueryFn)Va(kQueryHasSessionRewards))(&has, &hasRewards);
    ((BoolQueryFn)Va(kQueryCampaignPlaylist))(&isCampaign, &campaign);
    Log("Medals screen: %s (session rewards %s, campaign playlist %s)",
        has && !isCampaign ? "showing" : "skipped by the game", has ? "received" : "missing", isCampaign ? "yes" : "no");
    g_origPerformanceOnActivate(element);
}

static void HookMvpOnActivate(void* element)
{
    MemberCall hasMvp = { Va(kHasMvpCall), 0 }, campaign = { Va(kCampaignPlaylistCall), 0 };
    bool has = false, isCampaign = false;
    ((BoolQueryFn)Va(kQueryHasMvp))(&has, &hasMvp);
    ((BoolQueryFn)Va(kQueryCampaignPlaylist))(&isCampaign, &campaign);
    Log("MVP scene: %s (MVP results %s, campaign playlist %s)", has && !isCampaign ? "showing" : "skipped by the game",
        has ? "received" : "missing", isCampaign ? "yes" : "no");
    g_origMvpOnActivate(element);
}

// PostSessionSceneFrontend hero spawner (this = frontend): spawns the MVP players (+0x130/+0x138) hidden, into
// +0x150/+0x158 (shared_ptr<AZ::Entity>). It runs when the results arrive, but only spawns if +0x17a is set; else it
// sets +0x17b ("spawn pending"). Nothing in this build sets the one or reads the other, so it never spawns anyone.
static const uintptr_t kMvpSpawnHeroes = 0x1436F4080;
static const unsigned char kMvpSpawnHeroesProlog[] = { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56, 0x57 };
static const size_t kMvpHeroes = 0x150, kMvpHeroesVisible = 0x178, kMvpCanSpawn = 0x17a, kMvpSpawnPending = 0x17b;
// It also spawns the local player (id at +0xa0) in the centre unless +0x179 says not to; nothing sets that either, so
// a local player among the MVPs (0x60-byte entries, player id first) would stand on the stage twice
static const size_t kMvpLocalPlayer = 0xa0, kMvpEntries = 0x130, kMvpEntrySize = 0x60, kMvpSkipLocalPlayer = 0x179;
// PostSessionNotifications::OnMVPScene (this = notification base, frontend + 0x60): shows the spawned heroes
static const uintptr_t kOnMvpSceneSlot = 0x15126CB08, kOnMvpSceneThunk = 0x1404584A0;
static const size_t kNotificationsBase = 0x60;
static ElementCallFn g_origOnMvpScene;
// PostSessionNotifications::OnSessionDetails (next slot): shows the heroes again for the medals / details shot.
// That camera wants the local player in slot 0; the PvP stages hide the MVP heroes there (PvE ones keep them).
static const uintptr_t kOnSessionDetailsSlot = 0x15126CB10, kOnSessionDetailsThunk = 0x140104286;
static ElementCallFn g_origOnSessionDetails;
static char* g_localSkippedOn;   // frontend whose MVP scene left out the local player's slot-0 hero

// Spawns the pending heroes just before the scene shows them; the stage is loaded by then
static void HookOnMvpScene(void* notifications)
{
    char* frontend = (char*)notifications - kNotificationsBase;
    // Only this scene's spawn may leave the local hero to the details shot: a mark left by an earlier session (its
    // details shot never came) must not spawn a hero in this one
    g_localSkippedOn = nullptr;
    if (frontend[kMvpSpawnPending] && !frontend[kMvpCanSpawn]) {
        uint64_t local = *(const uint64_t*)(frontend + kMvpLocalPlayer);
        const char* entry = *(const char* const*)(frontend + kMvpEntries);
        const char* entriesEnd = *(const char* const*)(frontend + kMvpEntries + 8);
        bool localIsMvp = false;
        for (; entry && entry < entriesEnd; entry += kMvpEntrySize)
            if (*(const uint64_t*)entry == local) localIsMvp = true;
        char skipLocal = frontend[kMvpSkipLocalPlayer];
        if (localIsMvp) frontend[kMvpSkipLocalPlayer] = 1;
        frontend[kMvpCanSpawn] = 1;
        ((ElementCallFn)Va(kMvpSpawnHeroes))(frontend);
        frontend[kMvpCanSpawn] = 0;   // the next session waits for its own scene again
        frontend[kMvpSpawnPending] = 0;
        frontend[kMvpSkipLocalPlayer] = skipLocal;
        g_localSkippedOn = localIsMvp && !skipLocal ? frontend : nullptr;
    }
    g_origOnMvpScene(notifications);
    const uintptr_t* begin = *(const uintptr_t* const*)(frontend + kMvpHeroes);
    const uintptr_t* end = *(const uintptr_t* const*)(frontend + kMvpHeroes + 8);
    Log("MVP scene: %lld heroes on stage (visible %u)", begin ? (long long)(end - begin) / 2 : 0LL,
        (unsigned)(unsigned char)frontend[kMvpHeroesVisible]);
}

// The medals / details shot: add the local player's slot-0 hero that the MVP scene left out. The spawner runs with
// the MVP list emptied for the call, so it spawns only that hero (visible, as the scene is shown by now).
static void HookOnSessionDetails(void* notifications)
{
    char* frontend = (char*)notifications - kNotificationsBase;
    if (g_localSkippedOn == frontend) {
        g_localSkippedOn = nullptr;
        char** entriesEnd = (char**)(frontend + kMvpEntries + 8);
        char* savedEnd = *entriesEnd;
        *entriesEnd = *(char**)(frontend + kMvpEntries);
        char skipLocal = frontend[kMvpSkipLocalPlayer];
        frontend[kMvpSkipLocalPlayer] = 0;
        frontend[kMvpCanSpawn] = 1;
        ((ElementCallFn)Va(kMvpSpawnHeroes))(frontend);
        frontend[kMvpCanSpawn] = 0;
        frontend[kMvpSpawnPending] = 0;
        frontend[kMvpSkipLocalPlayer] = skipLocal;
        *entriesEnd = savedEnd;
        const uintptr_t* begin = *(const uintptr_t* const*)(frontend + kMvpHeroes);
        const uintptr_t* end = *(const uintptr_t* const*)(frontend + kMvpHeroes + 8);
        Log("Session details: local player's hero added for the medals shot (%lld heroes on stage)",
            begin ? (long long)(end - begin) / 2 : 0LL);
    }
    g_origOnSessionDetails(notifications);
}

// Optional: without it the match just ends without the MVP scene, as in the stock Steam build
static void InstallMvpSceneHook()
{
    if (SlotHolds(kOnMvpSceneSlot, kOnMvpSceneThunk) &&
        memcmp((void*)Va(kMvpSpawnHeroes), kMvpSpawnHeroesProlog, sizeof(kMvpSpawnHeroesProlog)) == 0) {
        g_origOnMvpScene = (ElementCallFn)Va(kOnMvpSceneThunk);
        WriteSlot(kOnMvpSceneSlot, (void*)&HookOnMvpScene);
        if (SlotHolds(kOnSessionDetailsSlot, kOnSessionDetailsThunk)) {
            g_origOnSessionDetails = (ElementCallFn)Va(kOnSessionDetailsThunk);
            WriteSlot(kOnSessionDetailsSlot, (void*)&HookOnSessionDetails);
        } else Log("Game code is not where expected; the medals screen may show no hero after an MVP scene");
    } else Log("Game code is not where expected; the MVP scene shows no heroes");

    if (memcmp((void*)Va(kMvpElementCtor), kMvpElementCtorProlog, sizeof(kMvpElementCtorProlog)) != 0 ||
        *(unsigned char*)Va(kMvpElementCtorThunk) != 0xE9 || !SlotHolds(kMvpOnActivateSlot, kMvpOnActivateThunk)) {
        Log("Game code is not where expected; no MVP scene");
        return;
    }
    g_origMvpOnActivate = (ElementCallFn)Va(kMvpOnActivateThunk);
    WriteSlot(kMvpOnActivateSlot, (void*)&HookMvpOnActivate);
    if (memcmp((void*)Va(kPerformanceElementCtor), kPerformanceElementCtorProlog,
               sizeof(kPerformanceElementCtorProlog)) == 0 &&
        *(unsigned char*)Va(kPerformanceElementCtorThunk) == 0xE9 &&
        *(unsigned char*)Va(kQueryHasSessionRewards) == 0xE9 &&
        SlotHolds(kPerformanceOnActivateSlot, kPerformanceOnActivateThunk)) {
        g_origPerformanceOnActivate = (ElementCallFn)Va(kPerformanceOnActivateThunk);
        WriteSlot(kPerformanceOnActivateSlot, (void*)&HookPerformanceOnActivate);
        g_performanceStep = true;
    } else Log("Game code is not where expected; no medals screen");
    if (memcmp((void*)Va(kDetailsElementCtor), kDetailsElementCtorProlog, sizeof(kDetailsElementCtorProlog)) == 0 &&
        *(unsigned char*)Va(kDetailsElementCtorThunk) == 0xE9 &&
        SlotHolds(kDetailsOnActivateSlot, kDetailsOnActivateThunk)) {
        g_origDetailsOnActivate = (ElementCallFn)Va(kDetailsOnActivateThunk);
        WriteSlot(kDetailsOnActivateSlot, (void*)&HookDetailsOnActivate);
        g_detailsStep = true;
    } else Log("Game code is not where expected; no session details screen");
    if (memcmp((void*)Va(kWaitHighlightCtor), kWaitHighlightCtorProlog, sizeof(kWaitHighlightCtorProlog)) == 0 &&
        memcmp((void*)Va(kShowHighlightCtor), kShowHighlightCtorProlog, sizeof(kShowHighlightCtorProlog)) == 0 &&
        *(unsigned char*)Va(kWaitHighlightCtorThunk) == 0xE9 && *(unsigned char*)Va(kShowHighlightCtorThunk) == 0xE9 &&
        *(unsigned char*)Va(kQueryHasHighlight) == 0xE9 &&
        SlotHolds(kWaitHighlightOnActivateSlot, kWaitHighlightOnActivateThunk) &&
        SlotHolds(kShowHighlightOnActivateSlot, kShowHighlightOnActivateThunk) &&
        SlotHolds(kShowHighlightEndSlot, kShowHighlightEndThunk) &&
        SlotHolds(kShowHighlightDtorSlot, kShowHighlightDtorThunk)) {
        g_origWaitHighlightOnActivate = (ElementCallFn)Va(kWaitHighlightOnActivateThunk);
        g_origShowHighlightOnActivate = (ElementCallFn)Va(kShowHighlightOnActivateThunk);
        g_origShowHighlightEnd = (ElementCallFn)Va(kShowHighlightEndThunk);
        g_origShowHighlightDtor = (ElementDeleteFn)Va(kShowHighlightDtorThunk);
        WriteSlot(kWaitHighlightOnActivateSlot, (void*)&HookWaitHighlightOnActivate);
        WriteSlot(kShowHighlightOnActivateSlot, (void*)&HookShowHighlightOnActivate);
        WriteSlot(kShowHighlightEndSlot, (void*)&HookShowHighlightEnd);
        WriteSlot(kShowHighlightDtorSlot, (void*)&HookShowHighlightDtor);
        g_highlightSteps = true;
    } else Log("Game code is not where expected; no match highlight");
    g_origAppendResults = (AppendResultsFn)RedirectThunk(
        kAppendResultsThunk, kAppendResults, kAppendResultsProlog, sizeof(kAppendResultsProlog),
        (void*)&HookAppendResults, "no MVP scene");
}

// ---------------------------------------------------------------------------------------------------------
// Nameplate: badge icon + background shown with the player's name (lobby, social panel, profile, "Edit badge")

// NameplateComponentStub's INameplate interface (component + 0x188). Its requests get empty answers offline, so the
// player owns no badges, has no nameplate and a new choice is never stored.
static const uintptr_t kNameplateVtable = 0x15166D3B8;
static const uintptr_t kGetNameplateSlot = kNameplateVtable + 0x18, kGetNameplateThunk = 0x1401E2A36;
static const uintptr_t kSetNameplateSlot = kNameplateVtable + 0x28, kSetNameplateThunk = 0x140032BE6;
static const uintptr_t kGetBadgesSlot = kNameplateVtable + 0x48, kGetBadgesThunk = 0x1403E1A58;
// Interface data: "nameplate updated" event, current icon and background (AZStd::string), owned badges (vector of
// {AZStd::string name, int type})
static const size_t kNameplateUpdated = 0x08, kNameplateIcon = 0x118, kNameplateBackground = 0x140,
                    kNameplateBadges = 0x1b8;
static const size_t kBadgeSize = 0x30, kBadgeType = 0x28, kGameStringSize = 0x28;
static const uintptr_t kStringAssign = 0x1404457B0;    // (string*, const string* source, offset, count)
static const uintptr_t kBadgePushBack = 0x14002453C;   // (vector*, const badge*)
static const uintptr_t kSignalEvent = 0x1402999E8;     // (AZ::Event<>*)
// assets/configs/nameplates.config "nameplate_defaults"
static const char* const kDefaultIcon = "bcn_tr02";
static const char* const kDefaultBackground = "bgr10";

struct BadgeAsset { const char* name; int type; };
static const BadgeAsset kBadges[] = {
#include "badges.inc"
};
static const size_t kBadgeCount = sizeof(kBadges) / sizeof(kBadges[0]);

// Private badges (custom\private_items.txt): every build knows them, so a player wearing one is shown with it, but
// the public build doesn't own them, so its players can't choose them. The owner's build owns every badge.
static const char* const kPrivateBadges[] = {
#ifdef PUBLIC_RELEASE
#include "out/public/private_badges.inc"
#endif
    nullptr
};

static bool IsPrivateBadge(const char* name)
{
    for (const char* const* p = kPrivateBadges; *p; ++p)
        if (strcmp(*p, name) == 0) return true;
    return false;
}

static size_t OwnedBadgeCount()
{
    size_t n = 0;
    for (const BadgeAsset& asset : kBadges)
        if (!IsPrivateBadge(asset.name)) ++n;
    return n;
}

// The allow-list for badge names, ours and those other players send: only badges the game ships
static bool IsKnownBadge(const std::string& name, int type)
{
    for (const BadgeAsset& asset : kBadges)
        if (asset.type == type && name == asset.name) return true;
    return false;
}

typedef void* (*GetNameplateFn)(char* nameplate);
typedef void* (*SetNameplateFn)(char* nameplate, void* request, const char* value);
typedef uintptr_t* (*GetBadgesFn)(char* nameplate);
typedef void (*StringAssignFn)(char* string, const void* source, size_t offset, size_t count);
typedef void (*BadgePushBackFn)(uintptr_t* vector, const void* badge);
typedef void (*SignalEventFn)(char* event);
static GetNameplateFn g_origGetNameplate;
static SetNameplateFn g_origSetNameplate;
static GetBadgesFn g_origGetBadges;

// AZStd::string as the game lays it out: 16-byte inline buffer or heap pointer (capacity > 15), size, capacity,
// allocator. A source string only, never destroyed: the long form points at our own text.
static void MakeGameString(char* s, const char* text)
{
    size_t size = strlen(text);
    memset(s, 0, kGameStringSize);
    if (size < 16) {
        memcpy(s, text, size);
        *(size_t*)(s + 0x18) = 15;
    } else {
        *(const char**)s = text;
        *(size_t*)(s + 0x18) = size;
    }
    *(size_t*)(s + 0x10) = size;
    *(uintptr_t*)(s + 0x20) = Va(kAllocatorNameRef);
}

static std::string ReadGameString(const char* s)
{
    size_t size = *(const size_t*)(s + 0x10), capacity = *(const size_t*)(s + 0x18);
    return std::string(capacity > 15 ? *(const char* const*)s : s, size);
}

static void AssignGameString(char* s, const std::string& text)
{
    if (ReadGameString(s) == text) return;
    char source[kGameStringSize];
    MakeGameString(source, text.c_str());
    ((StringAssignFn)Va(kStringAssign))(s, source, 0, (size_t)-1);
}

// The saved choice (or the game's defaults) replaces whatever the backend has
static void ApplyNameplate(char* nameplate)
{
    EnterCriticalSection(&g_lock);
    std::string icon = g_nameplateIcon.empty() ? kDefaultIcon : g_nameplateIcon;
    std::string background = g_nameplateBackground.empty() ? kDefaultBackground : g_nameplateBackground;
    LeaveCriticalSection(&g_lock);
    AssignGameString(nameplate + kNameplateIcon, icon);
    AssignGameString(nameplate + kNameplateBackground, background);
}

static void* HookGetNameplate(char* nameplate)
{
    ApplyNameplate(nameplate);
    return g_origGetNameplate(nameplate);
}

// The backend request goes out as before (offline it never stores anything); the choice is saved here and the
// game's "nameplate updated" event tells the screens, as a successful online answer would
static void* HookSetNameplate(char* nameplate, void* request, const char* value)
{
    void* result = g_origSetNameplate(nameplate, request, value);
    std::string icon = ReadGameString(value), background = ReadGameString(value + kGameStringSize);
    EnterCriticalSection(&g_lock);
    if (icon != g_nameplateIcon || background != g_nameplateBackground) {
        g_nameplateIcon = icon;
        g_nameplateBackground = background;
        Log("Nameplate set: icon '%s', background '%s'", icon.c_str(), background.c_str());
        if (g_profileLoaded) SaveProfile();
        // the new badge items and slots now: the lobby check runs only on wallet reads, and a match joined before
        // the next one would send the server the old pair
        if (g_profileLoaded && g_inventoryHooked && !g_inventoryBroken) TryRestoreInventory();
    }
    LeaveCriticalSection(&g_lock);
    ApplyNameplate(nameplate);
    ((SignalEventFn)Va(kSignalEvent))(nameplate + kNameplateUpdated);
    return result;
}

// Owned badges: every badge asset except the private ones in the public build. The backend's own (empty) answer can
// clear the list again, so whatever is missing is added each time it is read.
static uintptr_t* HookGetBadges(char* nameplate)
{
    static const size_t ownedCount = OwnedBadgeCount();
    uintptr_t* badges = (uintptr_t*)(nameplate + kNameplateBadges);   // begin, end, capacity end
    if ((badges[1] - badges[0]) / kBadgeSize < ownedCount) {
        std::set<std::pair<std::string, int>> owned;
        for (uintptr_t b = badges[0]; b < badges[1]; b += kBadgeSize)
            owned.insert({ ReadGameString((const char*)b), *(const int*)(b + kBadgeType) });
        unsigned added = 0;
        for (const BadgeAsset& asset : kBadges) {
            if (IsPrivateBadge(asset.name) || owned.count({ asset.name, asset.type })) continue;
            char badge[kBadgeSize] = {};
            MakeGameString(badge, asset.name);
            *(int*)(badge + kBadgeType) = asset.type;
            ((BadgePushBackFn)Va(kBadgePushBack))(badges, badge);
            ++added;
        }
        Log("Badges: %u of %u added (all badge icons and backgrounds are owned, %u private ones only shown)", added,
            (unsigned)ownedCount, (unsigned)(kBadgeCount - ownedCount));
    }
    return g_origGetBadges(nameplate);
}

// Login: LoadAssetsChainElement asks the backend for the assets of every type in its list (element + 0x1c0, vector
// of AZ::Uuid asset types; offline the backend serves them from assets.pak). Badge icons and backgrounds are not in
// the list (online the nameplate service loaded them), so every badge lookup fails ("Asset bcn_tr02 is missed
// locally") and the badge screen stays empty.
static const uintptr_t kLoadAssetsElementCtor = 0x1442D6020, kLoadAssetsElementCtorThunk = 0x140181448;
static const unsigned char kLoadAssetsElementCtorProlog[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                                              0x24, 0x18, 0x55, 0x57 };
static const size_t kLoadAssetsTypes = 0x1c0;   // begin, end, capacity end, allocator name ref
// () -> const AZ::Uuid*: the types NameplateFrontend looks badges up with
static const uintptr_t kBadgeIconAssetType = 0x14018553E, kBadgeBackgroundAssetType = 0x1402FD646;
static const uintptr_t kDeallocate = 0x1401A39B2;   // (allocator name ref*, pointer, size, align)
struct GameUuid { uint64_t low, high; };
typedef const GameUuid* (*AssetTypeFn)();
typedef void (*DeallocateFn)(void* nameRef, void* pointer, size_t size, size_t align);
static ElementCtorFn g_origLoadAssetsElementCtor;

static void HookLoadAssetsElementCtor(void* element)
{
    g_origLoadAssetsElementCtor(element);
    GameUuid** types = (GameUuid**)((char*)element + kLoadAssetsTypes);
    void* nameRef = (char*)element + kLoadAssetsTypes + 0x18;
    if (*(uintptr_t*)nameRef != Va(kAllocatorNameRef)) {
        Log("Badge assets not loaded: the login asset list is not the expected one");
        return;
    }
    size_t count = types[1] - types[0];
    GameUuid add[2];
    size_t adding = 0;
    for (uintptr_t getter : { kBadgeIconAssetType, kBadgeBackgroundAssetType }) {
        GameUuid type = *((AssetTypeFn)Va(getter))();
        bool listed = false;
        for (size_t i = 0; i < count; ++i)
            if (types[0][i].low == type.low && types[0][i].high == type.high) listed = true;
        if (!listed) add[adding++] = type;
    }
    if (!adding) return;
    GameUuid* list = (GameUuid*)((AllocateControlBlockFn)Va(kAllocateControlBlock))(
        nameRef, (count + adding) * sizeof(GameUuid), 16, 0);
    if (!list) { Log("Badge assets not loaded: out of memory"); return; }
    if (count) memcpy(list, types[0], count * sizeof(GameUuid));
    memcpy(list + count, add, adding * sizeof(GameUuid));
    if (types[0])
        ((DeallocateFn)Va(kDeallocate))(nameRef, types[0], (char*)types[2] - (char*)types[0], 16);
    types[0] = list;
    types[1] = types[2] = list + count + adding;
    Log("Login loads %u asset types (badge icons and backgrounds added)", (unsigned)(count + adding));
}

// MVP scene: each hero gets its badge from PostSessionSceneFrontend::GetNameplate(this, player, string* icon image,
// string* background image). It no longer asks presence (online only) and always gives the nameplate_defaults
// images. Its only caller is the one-hero spawner (this, slot, const player id*, player info, ...), so the spawner
// tells it whether the hero is the local player, whose saved choice is then shown.
static const uintptr_t kMvpSpawnHero = 0x1436F35A0, kMvpSpawnHeroThunk = 0x14012F8B4;
static const unsigned char kMvpSpawnHeroProlog[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x89, 0x4C, 0x24, 0x20,
                                                     0x4C, 0x89, 0x44, 0x24, 0x18 };
static const uintptr_t kMvpGetNameplate = 0x14364E610, kMvpGetNameplateThunk = 0x14048B12A;
static const unsigned char kMvpGetNameplateProlog[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
                                                        0x48, 0x89, 0x7C, 0x24, 0x18 };
// (const char* badge name) -> badge asset data or null; the image path (AZStd::string) is at +0x30
static const uintptr_t kFindBadgeIcon = 0x14039CC82, kFindBadgeBackground = 0x1404ABD49;
static const size_t kBadgeImage = 0x30;
// The spawner has stack arguments too; they are passed on as they are
typedef uintptr_t (*MvpSpawnHeroFn)(char* frontend, uintptr_t slot, const uint64_t* player, uintptr_t info,
                                    uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8, uintptr_t a9,
                                    uintptr_t a10, uintptr_t a11, uintptr_t a12);
typedef void (*MvpGetNameplateFn)(char* frontend, void* player, char* icon, char* background);
typedef const char* (*FindBadgeFn)(const char* name);
static MvpSpawnHeroFn g_origMvpSpawnHero;
static MvpGetNameplateFn g_origMvpGetNameplate;
static bool g_mvpSpawningLocalPlayer;
static std::string g_mvpIcon, g_mvpBackground;   // badge pair from the items of the hero being spawned

// The player info is the player's items as the server's session results list them: a list at +0x08 of nodes
// {next, prev, item, control block}; item + 0x30 points 0x10 into its definition, whose vfunc +0x38 gives the name
static const size_t kMvpInfoItems = 0x08, kItemDefinition = 0x30, kDefinitionBase = 0x10, kDefinitionName = 0x38 / 8;
static const unsigned kMvpItemsMax = 4000;
static const size_t kItemNameMax = 128;
typedef const char* (*ItemNameFn)(void* definition);

// Reads the hero's item names and picks its badge items; names are data from another player, so they are bounded
// and only badges the game ships are taken
static void ReadMvpItems(const uint64_t* player, uintptr_t info, bool local)
{
    std::string names;
    unsigned count = 0;
    g_mvpIcon.clear();
    g_mvpBackground.clear();
    const uintptr_t head = info + kMvpInfoItems;
    for (uintptr_t node = *(const uintptr_t*)head; node && node != head && count < kMvpItemsMax;
         node = *(const uintptr_t*)node, ++count) {
        uintptr_t item = ((const uintptr_t*)node)[2];
        uintptr_t definition = item ? *(const uintptr_t*)(item + kItemDefinition) : 0;
        if (!definition) continue;
        void* base = (void*)(definition - kDefinitionBase);
        const char* text = ((ItemNameFn)(*(uintptr_t**)base)[kDefinitionName])(base);
        std::string name = text ? std::string(text, strnlen(text, kItemNameMax)) : "?";
        if (!names.empty()) names += ' ';
        names += name;
        if (name.compare(0, kBadgeItemPrefixLength, kBadgeItemPrefix) != 0) continue;
        std::string badge = name.substr(kBadgeItemPrefixLength);
        if (IsKnownBadge(badge, kBadgeIcon)) g_mvpIcon = badge;
        else if (IsKnownBadge(badge, kBadgeBackground)) g_mvpBackground = badge;
    }
    Log("MVP hero %llu%s: %u items from the server: %s", player ? (unsigned long long)*player : 0ULL,
        local ? " (local player)" : "", count, names.c_str());
    Log("MVP hero %llu badge from the server: icon '%s', background '%s'",
        player ? (unsigned long long)*player : 0ULL, g_mvpIcon.c_str(), g_mvpBackground.c_str());
}

static uintptr_t HookMvpSpawnHero(char* frontend, uintptr_t slot, const uint64_t* player, uintptr_t info,
                                  uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8, uintptr_t a9,
                                  uintptr_t a10, uintptr_t a11, uintptr_t a12)
{
    g_mvpSpawningLocalPlayer = player && *player == *(const uint64_t*)(frontend + kMvpLocalPlayer);
    if (info) ReadMvpItems(player, info, g_mvpSpawningLocalPlayer);
    uintptr_t result = g_origMvpSpawnHero(frontend, slot, player, info, a5, a6, a7, a8, a9, a10, a11, a12);
    g_mvpSpawningLocalPlayer = false;
    g_mvpIcon.clear();
    g_mvpBackground.clear();
    return result;
}

static void SetMvpBadgeImage(char* image, uintptr_t find, const std::string& name)
{
    if (name.empty()) return;
    const char* badge = ((FindBadgeFn)Va(find))(name.c_str());
    if (badge) ((StringAssignFn)Va(kStringAssign))(image, badge + kBadgeImage, 0, (size_t)-1);
    else Log("MVP scene: badge '%s' not found, default shown", name.c_str());
}

static void HookMvpGetNameplate(char* frontend, void* player, char* icon, char* background)
{
    g_origMvpGetNameplate(frontend, player, icon, background);
    // The hero's badge items from the server; the local player's own choice when the server sent none
    std::string iconName = g_mvpIcon, backgroundName = g_mvpBackground;
    if (g_mvpSpawningLocalPlayer) {
        EnterCriticalSection(&g_lock);
        if (iconName.empty()) iconName = g_nameplateIcon;
        if (backgroundName.empty()) backgroundName = g_nameplateBackground;
        LeaveCriticalSection(&g_lock);
    }
    SetMvpBadgeImage(icon, kFindBadgeIcon, iconName);
    SetMvpBadgeImage(background, kFindBadgeBackground, backgroundName);
}

// Death camera ("killed by" screen): DeathCameraFrontend keeps the killer's info (hero, weapon, nameplate) when the
// local player dies; its nameplate always comes from the nameplate defaults. The killer's badge items reach this
// client mid-match in the killer's inventory (linked into the badge slots of the hero preset), the same items the
// MVP scene reads from the session results.
// PlayerEquipmentComponent::OnCurrentPresetChanged(component, const string* preset) runs on every client for every
// player when its hero preset changes; afterwards the preset name is at component + 0x1E0 and the replicated
// PlayerInventory at component + 0x248. component + 0x08 = AZ::Entity*, its id at entity + 0x08.
static const uintptr_t kPresetChanged = 0x14149D790, kPresetChangedThunk = 0x14033F9C9;
static const unsigned char kPresetChangedProlog[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                                      0x55, 0x57, 0x41, 0x54 };
static const size_t kEquipmentEntity = 0x08, kEntityId = 0x08, kEquipmentPreset = 0x1E0, kEquipmentInventory = 0x248;
// PlayerInventory find (inventory, shared_ptr<PlayerInventoryItem>* out, const char* name);
// PlayerInventoryItem item-by-slot (item, shared_ptr<PlayerInventoryItem>* out, const char* slot)
static const uintptr_t kPlayerInventoryFind = 0x1401E3116, kItemBySlot = 0x1400A0754;
// DeathCameraFrontendRequests (frontend + 0xB0) vtable +0x20 GetEnemyInfo(this, info* out): copies this + 0x1C0;
// the killer's entity id is this + 0x1B0. Info: +0x50 nameplate {nickname, +0x78 icon, +0xA0 background} (badge ids)
static const uintptr_t kGetEnemyInfoSlot = 0x151213BA8 + 0x20, kGetEnemyInfoThunk = 0x14022BB14;
static const size_t kDeathCameraKiller = 0x1B0, kInfoNameplateIcon = 0x78, kInfoNameplateBackground = 0xA0;
static const size_t kEntityBadgesMax = 256;
typedef void (*PresetChangedFn)(char* component, void* preset);
typedef void (*ItemLookupFn)(void* owner, SharedPtr* out, const char* name);
typedef void* (*GetEnemyInfoFn)(char* self, char* info);
static PresetChangedFn g_origPresetChanged;
static GetEnemyInfoFn g_origGetEnemyInfo;
static std::map<uint64_t, std::pair<std::string, std::string>> g_entityBadges;   // entity id -> icon, background

// The name of a PlayerInventoryItem (see ReadMvpItems), bounded
static std::string ItemName(void* item)
{
    uintptr_t definition = item ? *(const uintptr_t*)((char*)item + kItemDefinition) : 0;
    if (!definition) return std::string();
    void* base = (void*)(definition - kDefinitionBase);
    const char* text = ((ItemNameFn)(*(uintptr_t**)base)[kDefinitionName])(base);
    return text ? std::string(text, strnlen(text, kItemNameMax)) : std::string();
}

// The badge in a preset slot, if its item is a badge the game ships
static std::string SlotBadge(void* preset, int type)
{
    SharedPtr item = {};
    ((ItemLookupFn)Va(kItemBySlot))(preset, &item, kBadgeSlots[type]);
    std::string name = ItemName(item.object);
    Release(item);
    if (name.compare(0, kBadgeItemPrefixLength, kBadgeItemPrefix) != 0) return std::string();
    name = name.substr(kBadgeItemPrefixLength);
    return IsKnownBadge(name, type) ? name : std::string();
}

static void HookPresetChanged(char* component, void* preset)
{
    g_origPresetChanged(component, preset);
    const char* entity = *(const char* const*)(component + kEquipmentEntity);
    if (!entity) return;
    uint64_t id = *(const uint64_t*)(entity + kEntityId);
    std::string presetName = ReadGameString(component + kEquipmentPreset);
    SharedPtr item = {};
    ((ItemLookupFn)Va(kPlayerInventoryFind))(component + kEquipmentInventory, &item, presetName.c_str());
    std::pair<std::string, std::string> badges;
    if (item.object) badges = { SlotBadge(item.object, kBadgeIcon), SlotBadge(item.object, kBadgeBackground) };
    Release(item);
    EnterCriticalSection(&g_lock);
    auto it = g_entityBadges.find(id);
    bool changed = it == g_entityBadges.end() ? !badges.first.empty() || !badges.second.empty() : it->second != badges;
    if (it == g_entityBadges.end() && g_entityBadges.size() >= kEntityBadgesMax) g_entityBadges.clear();
    g_entityBadges[id] = badges;
    LeaveCriticalSection(&g_lock);
    if (changed)
        Log("Player %llu (%s) badge from the server: icon '%s', background '%s'", (unsigned long long)id,
            presetName.c_str(), badges.first.c_str(), badges.second.c_str());
}

static void* HookGetEnemyInfo(char* self, char* info)
{
    void* result = g_origGetEnemyInfo(self, info);
    uint64_t killer = *(const uint64_t*)(self + kDeathCameraKiller);
    std::pair<std::string, std::string> badges;
    EnterCriticalSection(&g_lock);
    auto it = g_entityBadges.find(killer);
    if (it != g_entityBadges.end()) badges = it->second;
    LeaveCriticalSection(&g_lock);
    if (!badges.first.empty()) AssignGameString(info + kInfoNameplateIcon, badges.first);
    if (!badges.second.empty()) AssignGameString(info + kInfoNameplateBackground, badges.second);
    return result;
}

static void InstallDeathCameraHooks()
{
    if (!SlotHolds(kGetEnemyInfoSlot, kGetEnemyInfoThunk)) {
        Log("Death camera code is not where expected; the killer's badge shows the default");
        return;
    }
    g_origPresetChanged = (PresetChangedFn)RedirectThunk(
        kPresetChangedThunk, kPresetChanged, kPresetChangedProlog, sizeof(kPresetChangedProlog),
        (void*)&HookPresetChanged, "the killer's badge shows the default");
    if (!g_origPresetChanged) return;
    g_origGetEnemyInfo = (GetEnemyInfoFn)Va(kGetEnemyInfoThunk);
    WriteSlot(kGetEnemyInfoSlot, (void*)&HookGetEnemyInfo);
}

// ---------------------------------------------------------------------------------------------------------
// Squads over Steam lobbies
//
// The squad is a friends-only Steam lobby owned by the leader. Lobby data: "leader" = the leader's account id,
// "inv_<account id>" = an invite (Unix time), "kick_<account id>" = that member must leave. A friend's client finds
// the invite by looking at the lobby its Steam friends are in (GetFriendGamePlayed). Everything runs on the game's
// main thread from the Lua bridge calls (the squad frontends poll "state" every second); Steam call results are
// polled, the game itself pumps SteamAPI_RunCallbacks. Ids given to Lua are 32-bit Steam account ids as decimal
// strings (SteamID64 = kSteamIdBase + account id).
// The squad lobby is public so that players who aren't Steam friends find their invite too: "to_<account id>" = "1"
// goes with each invite and the invited game searches the lobby list for it. Nobody else finds it: the leader kicks
// a member that joined without an invite. Each member's "st" member data is "lobby" or "match": a member in a match
// is not ready and doesn't follow the leader.
// Recent players: on each server join the game joins (or creates) a public "match lobby" whose "match" data is the
// server's session id. Everyone in it while you're in that match is a recent player (only players with this mod;
// bots never are). The list is replaced at your next match; you leave the match lobby when you're back in the lobby.
static const uint64_t kSteamIdBase = 76561197960265728ULL;
static const int kSquadMaxMembers = 5, kMatchMaxMembers = 16;
static const int kLobbyTypePublic = 2, kFriendFlagImmediate = 4, kFriendRelationshipFriend = 3;
static const int kLobbyCompareEqual = 0, kLobbyDistanceWorldwide = 3;
static const int kLobbyCreatedCallback = 513, kLobbyEnterCallback = 504, kLobbyMatchListCallback = 510,
                 kChatRoomEnterSuccess = 1;
static const time_t kSquadInviteLifetime = 120;
static const time_t kMatchStartWindow = 240;  // a join counts as a match this long, or until you spawn in it
static const time_t kMatchEndWindow = 90;     // after the session rewards you're out of the match this soon
static const ULONGLONG kSquadFriendScanMs = 3000, kInviteSearchMs = 6000, kMatchSearchMs = 20000;
struct LobbyCreated { int result; uint64_t lobby; };
struct LobbyEnter { uint64_t lobby; uint32_t permissions; bool locked; uint32_t response; };
struct LobbyMatchList { uint32_t count; };
struct FriendGameInfo { uint64_t game; uint32_t ip; uint16_t port, queryPort; uint64_t lobby; };
struct SteamApi {
    void* mm; void* friends; void* user; void* utils;
    uint64_t (*CreateLobby)(void*, int type, int maxMembers);
    uint64_t (*JoinLobby)(void*, uint64_t lobby);
    void (*LeaveLobby)(void*, uint64_t lobby);
    bool (*SetLobbyData)(void*, uint64_t lobby, const char* key, const char* value);
    const char* (*GetLobbyData)(void*, uint64_t lobby, const char* key);
    bool (*DeleteLobbyData)(void*, uint64_t lobby, const char* key);
    int (*GetLobbyDataCount)(void*, uint64_t lobby);
    bool (*GetLobbyDataByIndex)(void*, uint64_t lobby, int index, char* key, int keySize, char* value, int valueSize);
    int (*GetNumLobbyMembers)(void*, uint64_t lobby);
    uint64_t (*GetLobbyMemberByIndex)(void*, uint64_t lobby, int index);
    uint64_t (*GetLobbyOwner)(void*, uint64_t lobby);
    bool (*SetLobbyOwner)(void*, uint64_t lobby, uint64_t owner);
    bool (*SetLobbyJoinable)(void*, uint64_t lobby, bool joinable);
    bool (*RequestLobbyData)(void*, uint64_t lobby);
    void (*SetLobbyMemberData)(void*, uint64_t lobby, const char* key, const char* value);
    const char* (*GetLobbyMemberData)(void*, uint64_t lobby, uint64_t user, const char* key);
    void (*AddRequestLobbyListStringFilter)(void*, const char* key, const char* value, int comparison);
    void (*AddRequestLobbyListFilterSlotsAvailable)(void*, int slots);
    void (*AddRequestLobbyListDistanceFilter)(void*, int distance);
    uint64_t (*RequestLobbyList)(void*);
    uint64_t (*GetLobbyByIndex)(void*, int index);
    int (*GetFriendCount)(void*, int flags);
    int (*GetFriendRelationship)(void*, uint64_t id);
    void (*SetPlayedWith)(void*, uint64_t id);
    void (*ActivateGameOverlayToUser)(void*, const char* dialog, uint64_t id);
    uint64_t (*GetFriendByIndex)(void*, int index, int flags);
    bool (*GetFriendGamePlayed)(void*, uint64_t id, FriendGameInfo* info);
    const char* (*GetFriendPersonaName)(void*, uint64_t id);
    bool (*SetRichPresence)(void*, const char* key, const char* value);
    const char* (*GetFriendRichPresence)(void*, uint64_t id, const char* key);
    uint64_t (*GetSteamID)(void*);
    uint32_t (*GetAppID)(void*);
    bool (*IsAPICallCompleted)(void*, uint64_t call, bool* failed);
    bool (*GetAPICallResult)(void*, uint64_t call, void* result, int size, int callback, bool* failed);
};
struct SquadInvite { uint64_t lobby; uint32_t leader; std::string nick; };
static SteamApi g_steam;
static int g_steamState;                      // 0 not tried, 1 ready, -1 unavailable
static uint32_t g_steamMe, g_steamAppId;
static uint64_t g_squadLobby, g_squadCreateCall, g_squadJoinCall;
static std::set<uint32_t> g_squadQueued;      // invited while the lobby is being created
static std::map<uint32_t, time_t> g_squadSent;
static std::map<uint32_t, SquadInvite> g_squadInvites;   // by leader
static std::set<uint64_t> g_squadDeclined;    // lobbies whose invite was declined
// A Steam friend in Shatterline now: name and badge ids (from their rich presence "badge" = "<icon>,<background>",
// only badges the game ships)
struct SquadFriend {
    std::string nick, icon, background;
    bool operator==(const SquadFriend& o) const { return nick == o.nick && icon == o.icon && background == o.background; }
};
static std::map<uint32_t, SquadFriend> g_squadPlaying;
static std::string g_squadBadgeSent;          // our rich presence "badge" value
static ULONGLONG g_squadBadgeSentAt;
static const ULONGLONG kSquadBadgeRefreshMs = 30000;   // set again now and then, in case the game clears it
static std::set<uint32_t> g_squadKicked;      // kicked by us (leader) and not gone yet; their kick_ key is ours to remove
static ULONGLONG g_squadFriendScan;
static std::string g_squadTestTeam;           // team of the Lua test squad (test friends), for solo testing
static std::string g_squadLoggedTeam;
static std::set<uint32_t> g_squadKnown;       // leader: members that may stay (invited, or there when we got the lead)
// A kick only works if the kicked player's game obeys it, and Steam passes the lobby's ownership (and so its "join"
// data) to another member when the leader leaves. So a member trusts the leader that invited it plus the members the
// trusted leader kept for kTrustAfterMs without a kick, and leaves the squad if the lead goes to anyone else.
static std::set<uint32_t> g_squadTrusted;
static std::map<uint32_t, ULONGLONG> g_squadSeen;   // member -> first seen unkicked under a trusted leader
static const ULONGLONG kTrustAfterMs = 5000;
static std::string g_squadStatusSent;         // our "st" member data and the squad it was set in
static uint64_t g_squadStatusLobby;
// Lobby list searches, one at a time: invites to us (players who aren't Steam friends) and the match lobby
enum LobbySearch { kSearchInvites, kSearchMatch };
static uint64_t g_listCall;
static LobbySearch g_listKind;
static std::string g_listSession;
static ULONGLONG g_inviteSearchAt, g_matchSearchAt;
static std::map<uint32_t, SquadInvite> g_searchInvites;   // by leader
// The current match: server session, its match lobby, whether you're in it, and the recent players (by account)
static std::string g_matchSession, g_matchCallSession;
static uint64_t g_matchLobby, g_matchCreateCall, g_matchJoinCall;
static bool g_inMatch, g_matchSpawned, g_matchSearchDue, g_matchPollLogged;
static time_t g_matchJoinedAt, g_matchEndedAt;
static std::map<uint32_t, std::string> g_recent;

static bool SteamReady()
{
    if (g_steamState) return g_steamState > 0;
    g_steamState = -1;
    HMODULE m = GetModuleHandleW(L"steam_api64.dll");
    if (!m) { Log("Squads: steam_api64.dll is not loaded; no squads"); return false; }
    bool ok = true;
    auto get = [&](const char* name) { FARPROC p = GetProcAddress(m, name); if (!p) { Log("Squads: %s missing", name); ok = false; } return p; };
    typedef void* (*AccessorFn)();
    AccessorFn mm = (AccessorFn)get("SteamAPI_SteamMatchmaking_v009"), fr = (AccessorFn)get("SteamAPI_SteamFriends_v018"),
               us = (AccessorFn)get("SteamAPI_SteamUser_v023"), ut = (AccessorFn)get("SteamAPI_SteamUtils_v010");
#define STEAM_FN(field, name) *(FARPROC*)&g_steam.field = get(name)
    STEAM_FN(CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby");
    STEAM_FN(JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby");
    STEAM_FN(LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby");
    STEAM_FN(SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData");
    STEAM_FN(GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData");
    STEAM_FN(DeleteLobbyData, "SteamAPI_ISteamMatchmaking_DeleteLobbyData");
    STEAM_FN(GetLobbyDataCount, "SteamAPI_ISteamMatchmaking_GetLobbyDataCount");
    STEAM_FN(GetLobbyDataByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyDataByIndex");
    STEAM_FN(GetNumLobbyMembers, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    STEAM_FN(GetLobbyMemberByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    STEAM_FN(GetLobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    STEAM_FN(SetLobbyOwner, "SteamAPI_ISteamMatchmaking_SetLobbyOwner");
    STEAM_FN(SetLobbyJoinable, "SteamAPI_ISteamMatchmaking_SetLobbyJoinable");
    STEAM_FN(RequestLobbyData, "SteamAPI_ISteamMatchmaking_RequestLobbyData");
    STEAM_FN(SetLobbyMemberData, "SteamAPI_ISteamMatchmaking_SetLobbyMemberData");
    STEAM_FN(GetLobbyMemberData, "SteamAPI_ISteamMatchmaking_GetLobbyMemberData");
    STEAM_FN(AddRequestLobbyListStringFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter");
    STEAM_FN(AddRequestLobbyListFilterSlotsAvailable, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListFilterSlotsAvailable");
    STEAM_FN(AddRequestLobbyListDistanceFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter");
    STEAM_FN(RequestLobbyList, "SteamAPI_ISteamMatchmaking_RequestLobbyList");
    STEAM_FN(GetLobbyByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyByIndex");
    STEAM_FN(GetFriendCount, "SteamAPI_ISteamFriends_GetFriendCount");
    STEAM_FN(GetFriendRelationship, "SteamAPI_ISteamFriends_GetFriendRelationship");
    STEAM_FN(SetPlayedWith, "SteamAPI_ISteamFriends_SetPlayedWith");
    STEAM_FN(ActivateGameOverlayToUser, "SteamAPI_ISteamFriends_ActivateGameOverlayToUser");
    STEAM_FN(GetFriendByIndex, "SteamAPI_ISteamFriends_GetFriendByIndex");
    STEAM_FN(GetFriendGamePlayed, "SteamAPI_ISteamFriends_GetFriendGamePlayed");
    STEAM_FN(GetFriendPersonaName, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    STEAM_FN(SetRichPresence, "SteamAPI_ISteamFriends_SetRichPresence");
    STEAM_FN(GetFriendRichPresence, "SteamAPI_ISteamFriends_GetFriendRichPresence");
    STEAM_FN(GetSteamID, "SteamAPI_ISteamUser_GetSteamID");
    STEAM_FN(GetAppID, "SteamAPI_ISteamUtils_GetAppID");
    STEAM_FN(IsAPICallCompleted, "SteamAPI_ISteamUtils_IsAPICallCompleted");
    STEAM_FN(GetAPICallResult, "SteamAPI_ISteamUtils_GetAPICallResult");
#undef STEAM_FN
    if (!ok) return false;
    g_steam.mm = mm(); g_steam.friends = fr(); g_steam.user = us(); g_steam.utils = ut();
    if (!g_steam.mm || !g_steam.friends || !g_steam.user || !g_steam.utils) { Log("Squads: Steam is not running; no squads"); return false; }
    g_steamMe = (uint32_t)(g_steam.GetSteamID(g_steam.user) - kSteamIdBase);
    g_steamAppId = g_steam.GetAppID(g_steam.utils);
    g_steamState = 1;
    Log("Squads: Steam ready (account %u, app %u)", g_steamMe, g_steamAppId);
    return true;
}

static std::string AccountKey(const char* prefix, uint32_t account) { return prefix + std::to_string(account); }
static uint32_t ToAccount(uint64_t steamId) { return (uint32_t)(steamId - kSteamIdBase); }

// A name made safe for the line protocol
static std::string CleanNick(const char* n)
{
    // Steam names are up to 32 characters, up to 128 bytes in UTF-8; a longer value is cut at a character boundary
    size_t length = n ? strnlen(n, 129) : 0;
    if (length > 128) {
        length = 128;
        while (length > 0 && ((unsigned char)n[length] & 0xC0) == 0x80) --length;   // not inside a character
    }
    std::string s = n ? std::string(n, length) : std::string();
    for (char& c : s) if (c == '\t' || c == '\n' || c == '\r' || c == '\1') c = ' ';
    return s;
}

// Display name of a Steam user (Steam knows it for friends and for players in a lobby with us), else the account id
static std::string SteamNick(uint32_t account)
{
    std::string s = CleanNick(g_steam.GetFriendPersonaName(g_steam.friends, kSteamIdBase + account));
    return s.empty() ? std::to_string(account) : s;
}

// A member's "st" in the squad lobby ("lobby", "match", or empty from an older mod version)
static std::string MemberStatus(uint32_t account)
{
    // Set by that member's game: only the two known values pass (it goes into the tab/newline text for Lua)
    const char* st = g_steam.GetLobbyMemberData(g_steam.mm, g_squadLobby, kSteamIdBase + account, "st");
    std::string s = st ? std::string(st, strnlen(st, 16)) : std::string();
    return s == "lobby" || s == "match" ? s : std::string();
}

// In a match: from the server join until you're back in the lobby; a join you never spawned in (it failed) ends
// after kMatchStartWindow, a match whose rewards came kMatchEndWindow after them
static bool InMatch()
{
    if (!g_inMatch) return false;
    time_t now = time(nullptr);
    if (g_matchEndedAt && now - g_matchEndedAt > kMatchEndWindow) return false;
    return g_matchSpawned || now - g_matchJoinedAt < kMatchStartWindow;
}

// An account id from what the UI gives for a player: a SteamID64, or an account id
static uint32_t ParseAccount(const std::string& text)
{
    if (text.empty() || text.size() > 20 || text.find_first_not_of("0123456789") != std::string::npos) return 0;
    unsigned long long v = strtoull(text.c_str(), nullptr, 10);
    if (v > kSteamIdBase && v - kSteamIdBase <= 0xFFFFFFFFULL) return (uint32_t)(v - kSteamIdBase);
    return v <= 0xFFFFFFFFULL ? (uint32_t)v : 0;
}

static bool SquadIsLeader()
{
    return g_squadLobby && ToAccount(g_steam.GetLobbyOwner(g_steam.mm, g_squadLobby)) == g_steamMe;
}

static void SquadLeave(const char* why)
{
    if (!g_squadLobby) return;
    g_steam.LeaveLobby(g_steam.mm, g_squadLobby);
    Log("Squads: left the squad (%s)", why);
    g_squadLobby = 0;
    g_squadSent.clear();
    g_squadKicked.clear();
    g_squadKnown.clear();
    g_squadTrusted.clear();
    g_squadSeen.clear();
}

static void SquadSendInvite(uint32_t account)
{
    if (g_squadKicked.erase(account))              // invited again after a kick: they may join
        g_steam.DeleteLobbyData(g_steam.mm, g_squadLobby, AccountKey("kick_", account).c_str());
    g_steam.SetLobbyData(g_steam.mm, g_squadLobby, AccountKey("inv_", account).c_str(),
                         std::to_string((long long)time(nullptr)).c_str());
    g_steam.SetLobbyData(g_steam.mm, g_squadLobby, AccountKey("to_", account).c_str(), "1");
    g_squadSent[account] = time(nullptr);
    Log("Squads: invited %s (%u)", SteamNick(account).c_str(), account);
}

static void SquadDeleteInvite(uint32_t account)
{
    g_steam.DeleteLobbyData(g_steam.mm, g_squadLobby, AccountKey("inv_", account).c_str());
    g_steam.DeleteLobbyData(g_steam.mm, g_squadLobby, AccountKey("to_", account).c_str());
}

// Everyone in the match lobby now joins the recent players (and Steam's own "recent games" list)
static void MatchCollect()
{
    if (!g_matchLobby) return;
    int n = g_steam.GetNumLobbyMembers(g_steam.mm, g_matchLobby);
    for (int i = 0; i < n && i < kMatchMaxMembers * 2; ++i) {
        uint64_t id = g_steam.GetLobbyMemberByIndex(g_steam.mm, g_matchLobby, i);
        uint32_t a = ToAccount(id);
        if (!a || a == g_steamMe) continue;
        std::string nick = SteamNick(a);
        auto it = g_recent.find(a);
        if (it == g_recent.end()) {
            g_steam.SetPlayedWith(g_steam.friends, id);
            g_recent[a] = nick;
        } else if (nick != std::to_string(a)) it->second = nick;
    }
}

static void MatchLeaveLobby()
{
    if (!g_matchLobby) return;
    MatchCollect();
    g_steam.LeaveLobby(g_steam.mm, g_matchLobby);
    g_matchLobby = 0;
}

// You joined a server: a new match, a new recent players list
static void MatchStart(const std::string& session)
{
    MatchLeaveLobby();
    g_recent.clear();
    g_matchSession = session;
    g_inMatch = true;
    g_matchSpawned = false;
    g_matchSearchDue = true;
    g_matchPollLogged = false;
    g_matchJoinedAt = time(nullptr);
    g_matchEndedAt = 0;
    Log("Squads: in a match on server %s", session.c_str());
}

// Back in the lobby: the list of this match's players stays until the next match
static void MatchEnd()
{
    if (!g_inMatch && g_matchSession.empty()) return;
    MatchLeaveLobby();
    g_matchSession.clear();
    g_inMatch = false;
    std::string names;
    for (auto& r : g_recent) names += (names.empty() ? "" : ", ") + r.second;
    Log("Squads: back in the lobby; recent players: %s", names.empty() ? "none (nobody else with this mod)" : names.c_str());
}

static void StartLobbySearch(LobbySearch kind)
{
    if (kind == kSearchInvites) {
        g_steam.AddRequestLobbyListStringFilter(g_steam.mm, AccountKey("to_", g_steamMe).c_str(), "1", kLobbyCompareEqual);
    } else {
        g_steam.AddRequestLobbyListStringFilter(g_steam.mm, "match", g_matchSession.c_str(), kLobbyCompareEqual);
        g_steam.AddRequestLobbyListFilterSlotsAvailable(g_steam.mm, 1);
    }
    g_steam.AddRequestLobbyListDistanceFilter(g_steam.mm, kLobbyDistanceWorldwide);
    g_listCall = g_steam.RequestLobbyList(g_steam.mm);
    g_listKind = kind;
    g_listSession = g_matchSession;
}

// A lobby list came: the invites found, or the match lobby to be in (the oldest = lowest id, so two lobbies made at
// the same time end up as one)
static void LobbySearchDone(uint32_t count)
{
    if (g_listKind == kSearchInvites) {
        std::map<uint32_t, SquadInvite> found;
        const std::string myKey = AccountKey("inv_", g_steamMe);
        for (uint32_t i = 0; i < count && i < 50; ++i) {
            uint64_t lobby = g_steam.GetLobbyByIndex(g_steam.mm, (int)i);
            if (!lobby || lobby == g_squadLobby || g_squadDeclined.count(lobby)) continue;
            const char* sent = g_steam.GetLobbyData(g_steam.mm, lobby, myKey.c_str());
            const char* leader = g_steam.GetLobbyData(g_steam.mm, lobby, "leader");
            uint32_t leaderAccount = leader ? ParseAccount(leader) : 0;
            if (!sent || !*sent || !leaderAccount) continue;
            std::string nick = SteamNick(leaderAccount);
            std::string named = CleanNick(g_steam.GetLobbyData(g_steam.mm, lobby, "lnick"));
            if (nick == std::to_string(leaderAccount) && !named.empty()) nick = named;
            found[leaderAccount] = { lobby, leaderAccount, nick };
        }
        g_searchInvites.swap(found);
        return;
    }
    if (g_listSession != g_matchSession || g_matchSession.empty() || g_matchCreateCall || g_matchJoinCall) return;
    uint64_t best = 0;
    for (uint32_t i = 0; i < count && i < 50; ++i) {
        uint64_t lobby = g_steam.GetLobbyByIndex(g_steam.mm, (int)i);
        if (lobby && lobby != g_matchLobby && (!best || lobby < best)) best = lobby;
    }
    g_matchCallSession = g_matchSession;
    if (best && (!g_matchLobby || best < g_matchLobby)) {
        MatchLeaveLobby();
        g_matchJoinCall = g_steam.JoinLobby(g_steam.mm, best);
    } else if (!best && !g_matchLobby) {
        g_matchCreateCall = g_steam.CreateLobby(g_steam.mm, kLobbyTypePublic, kMatchMaxMembers);
    }
}

// Call results of the lobby list and the match lobby, searches when due, the recent players
static void MatchPoll()
{
    bool failed = false;
    if (g_listCall && g_steam.IsAPICallCompleted(g_steam.utils, g_listCall, &failed)) {
        LobbyMatchList r = {};
        bool got = g_steam.GetAPICallResult(g_steam.utils, g_listCall, &r, sizeof(r), kLobbyMatchListCallback, &failed);
        g_listCall = 0;
        LobbySearchDone(got && !failed ? r.count : 0);
    }
    if (g_matchCreateCall && g_steam.IsAPICallCompleted(g_steam.utils, g_matchCreateCall, &failed)) {
        LobbyCreated r = {};
        bool got = g_steam.GetAPICallResult(g_steam.utils, g_matchCreateCall, &r, sizeof(r), kLobbyCreatedCallback, &failed);
        g_matchCreateCall = 0;
        if (got && !failed && r.result == 1 && r.lobby) {
            if (g_matchCallSession != g_matchSession || g_matchLobby) {
                g_steam.LeaveLobby(g_steam.mm, r.lobby);        // made for a match that's over
            } else {
                g_matchLobby = r.lobby;
                g_steam.SetLobbyData(g_steam.mm, r.lobby, "match", g_matchSession.c_str());
                Log("Squads: match lobby created (the first player with this mod on this server)");
            }
        } else Log("Squads: creating the match lobby failed (result %d)", r.result);
    }
    if (g_matchJoinCall && g_steam.IsAPICallCompleted(g_steam.utils, g_matchJoinCall, &failed)) {
        LobbyEnter r = {};
        bool got = g_steam.GetAPICallResult(g_steam.utils, g_matchJoinCall, &r, sizeof(r), kLobbyEnterCallback, &failed);
        g_matchJoinCall = 0;
        if (got && !failed && r.response == kChatRoomEnterSuccess && r.lobby && g_matchCallSession == g_matchSession &&
            !g_matchLobby) {
            g_matchLobby = r.lobby;
            Log("Squads: joined the match lobby (%d players with this mod)", g_steam.GetNumLobbyMembers(g_steam.mm, r.lobby));
        } else {
            if (got && r.lobby) g_steam.LeaveLobby(g_steam.mm, r.lobby);
            if (g_matchCallSession == g_matchSession) {
                Log("Squads: joining the match lobby failed (response %u); searching again", r.response);
                g_matchSearchDue = true;
            }
        }
    }
    MatchCollect();
    if (InMatch() && g_matchSpawned && !g_matchPollLogged) {   // tells from the log whether the game polls in a match
        g_matchPollLogged = true;
        Log("Squads: match lobby %s", g_matchLobby ? "joined, players are collected" : "not joined yet");
    }
    if (g_listCall) return;
    ULONGLONG now = GetTickCount64();
    bool matchSearch = !g_matchSession.empty() && InMatch() && !g_matchCreateCall && !g_matchJoinCall;
    if (matchSearch && (g_matchSearchDue || now - g_matchSearchAt >= kMatchSearchMs)) {
        g_matchSearchDue = false;
        g_matchSearchAt = now;
        StartLobbySearch(kSearchMatch);
    } else if (!InMatch() && now - g_inviteSearchAt >= kInviteSearchMs) {
        g_inviteSearchAt = now;
        StartLobbySearch(kSearchInvites);
    }
}

// Call results, membership checks, invite expiry and the friends' invite scan
static void SquadPoll()
{
    bool failed = false;
    if (g_squadCreateCall && g_steam.IsAPICallCompleted(g_steam.utils, g_squadCreateCall, &failed)) {
        LobbyCreated r = {};
        bool got = g_steam.GetAPICallResult(g_steam.utils, g_squadCreateCall, &r, sizeof(r), kLobbyCreatedCallback, &failed);
        g_squadCreateCall = 0;
        if (got && !failed && r.result == 1 && r.lobby && (g_squadLobby || g_squadJoinCall)) {
            // An invite was accepted while this squad was being made: that squad wins, this one is left at once
            g_steam.LeaveLobby(g_steam.mm, r.lobby);
            Log("Squads: the new squad was dropped (an invite was accepted meanwhile)");
        } else if (got && !failed && r.result == 1 && r.lobby) {
            g_squadLobby = r.lobby;
            g_steam.SetLobbyData(g_steam.mm, g_squadLobby, "leader", std::to_string(g_steamMe).c_str());
            g_steam.SetLobbyData(g_steam.mm, g_squadLobby, "lnick", SteamNick(g_steamMe).c_str());
            Log("Squads: squad created (lobby %llu)", (unsigned long long)g_squadLobby);
            for (uint32_t a : g_squadQueued) SquadSendInvite(a);
        } else {
            Log("Squads: creating the squad failed (result %d)", r.result);
        }
        g_squadQueued.clear();
    }
    if (g_squadJoinCall && g_steam.IsAPICallCompleted(g_steam.utils, g_squadJoinCall, &failed)) {
        LobbyEnter r = {};
        bool got = g_steam.GetAPICallResult(g_steam.utils, g_squadJoinCall, &r, sizeof(r), kLobbyEnterCallback, &failed);
        g_squadJoinCall = 0;
        if (got && !failed && r.response == kChatRoomEnterSuccess && r.lobby) {
            if (g_squadLobby && g_squadLobby != r.lobby) SquadLeave("joined another squad");   // never in two
            g_squadLobby = r.lobby;
            uint32_t owner = ToAccount(g_steam.GetLobbyOwner(g_steam.mm, r.lobby));
            g_squadTrusted = { owner, g_steamMe };   // the leader whose invite we accepted
            Log("Squads: joined the squad of %s", SteamNick(owner).c_str());
        } else {
            Log("Squads: joining the squad failed (response %u)", r.response);
            if (got && r.lobby) g_steam.LeaveLobby(g_steam.mm, r.lobby);
        }
    }
    if (g_squadLobby) {
        int n = g_steam.GetNumLobbyMembers(g_steam.mm, g_squadLobby);
        const char* kicked = g_steam.GetLobbyData(g_steam.mm, g_squadLobby, AccountKey("kick_", g_steamMe).c_str());
        if (n <= 0) SquadLeave("the squad is gone");
        else if (kicked && *kicked) SquadLeave("kicked by the leader");
    }
    if (g_squadLobby) {                         // ready or not: in a match you don't follow the leader
        std::string st = InMatch() ? "match" : "lobby";
        if (st != g_squadStatusSent || g_squadLobby != g_squadStatusLobby) {
            g_steam.SetLobbyMemberData(g_steam.mm, g_squadLobby, "st", st.c_str());
            g_squadStatusSent = st;
            g_squadStatusLobby = g_squadLobby;
        }
    }
    if (g_squadLobby && !SquadIsLeader()) {
        // Not (or no longer) the leader: our invites and kicks went to the new leader with the lobby data
        g_squadKnown.clear();
        g_squadSent.clear();
        g_squadKicked.clear();
        uint32_t owner = ToAccount(g_steam.GetLobbyOwner(g_steam.mm, g_squadLobby));
        if (!g_squadTrusted.count(owner)) {
            Log("Squads: the lead went to %s (%u), who wasn't invited", SteamNick(owner).c_str(), owner);
            SquadLeave("the lead went to a player who wasn't invited");
        } else {
            ULONGLONG now = GetTickCount64();
            int n = g_steam.GetNumLobbyMembers(g_steam.mm, g_squadLobby);
            std::set<uint32_t> members;
            for (int i = 0; i < n; ++i) {
                uint32_t a = ToAccount(g_steam.GetLobbyMemberByIndex(g_steam.mm, g_squadLobby, i));
                members.insert(a);
                const char* kick = g_steam.GetLobbyData(g_steam.mm, g_squadLobby, AccountKey("kick_", a).c_str());
                if (kick && *kick) { g_squadTrusted.erase(a); g_squadSeen.erase(a); continue; }
                if (g_squadTrusted.count(a)) continue;
                auto seen = g_squadSeen.emplace(a, now).first;
                if (now - seen->second >= kTrustAfterMs) g_squadTrusted.insert(a);
            }
            for (auto it = g_squadSeen.begin(); it != g_squadSeen.end();)
                if (members.count(it->first)) ++it; else it = g_squadSeen.erase(it);
        }
    }
    if (g_squadLobby && SquadIsLeader()) {
        std::set<uint32_t> members;
        int n = g_steam.GetNumLobbyMembers(g_steam.mm, g_squadLobby);
        for (int i = 0; i < n; ++i) members.insert(ToAccount(g_steam.GetLobbyMemberByIndex(g_steam.mm, g_squadLobby, i)));
        g_steam.SetLobbyData(g_steam.mm, g_squadLobby, "leader", std::to_string(g_steamMe).c_str());
        // The squad lobby is public: a member that joined without an invite is kicked
        // The lead just came to us (or the squad is new): the members we trusted stay, anyone else is kicked
        if (g_squadKnown.empty()) {
            g_squadKnown.insert(g_steamMe);
            for (uint32_t a : members) if (g_squadTrusted.count(a)) g_squadKnown.insert(a);
            // The leader before us may have invites out: they become ours (joins accepted, expiry, cleanup)
            int keys = g_steam.GetLobbyDataCount(g_steam.mm, g_squadLobby);
            for (int k = 0; k < keys && k < 200; ++k) {
                char key[64] = {}, value[64] = {};
                if (!g_steam.GetLobbyDataByIndex(g_steam.mm, g_squadLobby, k, key, sizeof(key), value, sizeof(value)))
                    continue;
                if (strncmp(key, "inv_", 4) != 0) continue;
                uint32_t a = ParseAccount(key + 4);
                long long sentAt = strtoll(value, nullptr, 10);
                if (!a || a == g_steamMe || g_squadSent.count(a)) continue;
                g_squadSent[a] = sentAt > 0 ? (time_t)sentAt : time(nullptr);
                Log("Squads: the invite to %s (%u) is ours now", SteamNick(a).c_str(), a);
            }
        }
        for (uint32_t a : members) {
            if (g_squadKnown.count(a) || g_squadKicked.count(a)) continue;
            if (g_squadSent.count(a)) { g_squadKnown.insert(a); continue; }
            g_steam.SetLobbyData(g_steam.mm, g_squadLobby, AccountKey("kick_", a).c_str(), "1");
            g_squadKicked.insert(a);
            Log("Squads: %s (%u) joined without an invite: kicked", SteamNick(a).c_str(), a);
        }
        for (auto it = g_squadKnown.begin(); it != g_squadKnown.end();)
            if (members.count(*it)) ++it; else it = g_squadKnown.erase(it);
        g_squadTrusted = g_squadKnown;           // if we hand the lead on or leave, these may lead
        g_squadTrusted.insert(g_steamMe);
        g_squadSeen.clear();
        for (auto it = g_squadSent.begin(); it != g_squadSent.end();) {
            bool joined = members.count(it->first) != 0, expired = time(nullptr) - it->second > kSquadInviteLifetime;
            if (joined || expired) {
                SquadDeleteInvite(it->first);
                it = g_squadSent.erase(it);
            } else ++it;
        }
        for (auto it = g_squadKicked.begin(); it != g_squadKicked.end();) {   // a kicked member left: clear the mark
            if (!members.count(*it)) {
                g_steam.DeleteLobbyData(g_steam.mm, g_squadLobby, AccountKey("kick_", *it).c_str());
                Log("Squads: %s left after the kick", SteamNick(*it).c_str());
                it = g_squadKicked.erase(it);
            } else ++it;
        }
        // Alone with no invite out (the last member left or was kicked, or the invites expired): no squad any more
        if (members.size() <= 1 && g_squadSent.empty() && g_squadKicked.empty())
            SquadLeave("nobody else is in it");
    }
    MatchPoll();
    ULONGLONG now = GetTickCount64();
    if (now - g_squadFriendScan < kSquadFriendScanMs) return;
    g_squadFriendScan = now;
    std::string badge = (IsKnownBadge(g_nameplateIcon, kBadgeIcon) ? g_nameplateIcon : std::string()) + "," +
                        (IsKnownBadge(g_nameplateBackground, kBadgeBackground) ? g_nameplateBackground : std::string());
    if (badge != g_squadBadgeSent || now - g_squadBadgeSentAt >= kSquadBadgeRefreshMs) {
        g_steam.SetRichPresence(g_steam.friends, "badge", badge.c_str());
        if (badge != g_squadBadgeSent) Log("Squads: your badge for your Steam friends: %s", badge.c_str());
        g_squadBadgeSent = badge;
        g_squadBadgeSentAt = now;
    }
    std::map<uint32_t, SquadInvite> found;
    std::map<uint32_t, SquadFriend> playing;
    const std::string myKey = AccountKey("inv_", g_steamMe);
    int count = g_steam.GetFriendCount(g_steam.friends, kFriendFlagImmediate);
    for (int i = 0; i < count && i < 500; ++i) {
        uint64_t id = g_steam.GetFriendByIndex(g_steam.friends, i, kFriendFlagImmediate);
        FriendGameInfo game = {};
        if (!g_steam.GetFriendGamePlayed(g_steam.friends, id, &game) || (uint32_t)(game.game & 0xFFFFFF) != g_steamAppId)
            continue;
        SquadFriend& pf = playing[ToAccount(id)];
        pf.nick = SteamNick(ToAccount(id));
        const char* rp = g_steam.GetFriendRichPresence(g_steam.friends, id, "badge");
        std::string theirs = rp ? std::string(rp, strnlen(rp, 200)) : std::string();
        size_t comma = theirs.find(',');
        if (comma != std::string::npos) {
            std::string icon = theirs.substr(0, comma), background = theirs.substr(comma + 1);
            if (IsKnownBadge(icon, kBadgeIcon)) pf.icon = icon;
            if (IsKnownBadge(background, kBadgeBackground)) pf.background = background;
        }
        if (!game.lobby || game.lobby == g_squadLobby || g_squadDeclined.count(game.lobby))
            continue;
        g_steam.RequestLobbyData(g_steam.mm, game.lobby);
        const char* sent = g_steam.GetLobbyData(g_steam.mm, game.lobby, myKey.c_str());
        const char* leader = g_steam.GetLobbyData(g_steam.mm, game.lobby, "leader");
        uint32_t leaderAccount = leader ? ParseAccount(leader) : 0;
        if (!sent || !*sent || !leaderAccount) continue;
        found[leaderAccount] = { game.lobby, leaderAccount, SteamNick(leaderAccount) };
    }
    for (auto& s : g_searchInvites)             // invites from players who aren't Steam friends (lobby search)
        if (!found.count(s.first) && s.second.lobby != g_squadLobby && !g_squadDeclined.count(s.second.lobby))
            found.insert(s);
    for (auto& f : found)
        if (!g_squadInvites.count(f.first)) Log("Squads: %s invited you to their squad", f.second.nick.c_str());
    g_squadInvites.swap(found);
    static bool scanLogged;                     // the first scan is logged even when nobody plays
    if (playing != g_squadPlaying || !scanLogged) {
        scanLogged = true;
        std::string names;
        for (auto& p : playing)
            names += (names.empty() ? "" : ", ") + p.second.nick + " (badge '" + p.second.icon + "' / '" +
                     p.second.background + "')";
        Log("Squads: Steam friends in Shatterline: %s (%d friends checked)", names.empty() ? "none" : names.c_str(), count);
        g_squadPlaying.swap(playing);
    }
}

// The match's rewards came (its end): the players still there are collected; out of the match soon after
static void SquadsSessionEnded()
{
    if (g_steamState <= 0 || !g_inMatch) return;
    if (!g_matchEndedAt) g_matchEndedAt = time(nullptr);
    MatchCollect();
}

static bool ValidSession(const std::string& session);

// The leader's "join" lobby data as "follow" writes it: "<Unix time>:<session id>"
static bool ValidJoin(const char* join)
{
    size_t length = strnlen(join, 300);
    if (length >= 300) return false;
    std::string s(join, length);
    size_t colon = s.find(':');
    return colon != std::string::npos && colon > 0 && colon <= 20 &&
           s.find_first_not_of("0123456789") == colon && ValidSession(s.substr(colon + 1));
}

static std::string SquadTeam()
{
    if (g_squadLobby && g_steam.GetNumLobbyMembers(g_steam.mm, g_squadLobby) >= 2)
        return ((g_squadLobby ^ (g_squadLobby >> 32)) & 1) ? "A" : "B";
    return g_squadTestTeam;
}

// The state Lua reads: "me", "squad <leader>", "m <account> <nick> <st>" per member (st: "match" = in a match),
// "sent <account>", "inv <leader> <nick>", "f <account> <nick>" per Steam friend in Shatterline with "badge <account>
// <icon> <background>" when they publish one, "join <time>:<session>" (members: the server the leader joined),
// "busy" while a create or join is pending, "inmatch" while you're in a match, "r <account> <nick> <1 = Steam
// friend>" per recent player; tab separated, one per line
static std::string SquadState()
{
    std::string s = "me\t" + std::to_string(g_steamMe) + "\n";
    if (g_squadCreateCall || g_squadJoinCall) s += "busy\n";
    if (InMatch()) s += "inmatch\n";
    if (g_squadLobby) {
        s += "squad\t" + std::to_string(ToAccount(g_steam.GetLobbyOwner(g_steam.mm, g_squadLobby))) + "\n";
        int n = g_steam.GetNumLobbyMembers(g_steam.mm, g_squadLobby);
        for (int i = 0; i < n; ++i) {
            uint32_t a = ToAccount(g_steam.GetLobbyMemberByIndex(g_steam.mm, g_squadLobby, i));
            if (g_squadKicked.count(a)) continue;      // shown as gone at once; their game leaves on its next poll
            s += "m\t" + std::to_string(a) + "\t" + SteamNick(a) + "\t" + MemberStatus(a) + "\n";
        }
        const char* join = g_steam.GetLobbyData(g_steam.mm, g_squadLobby, "join");
        if (join && *join && !SquadIsLeader() && ValidJoin(join)) s += std::string("join\t") + join + "\n";
    }
    for (uint32_t a : g_squadQueued) s += "sent\t" + std::to_string(a) + "\n";
    for (auto& p : g_squadSent) s += "sent\t" + std::to_string(p.first) + "\n";
    for (auto& p : g_squadInvites) s += "inv\t" + std::to_string(p.first) + "\t" + p.second.nick + "\n";
    for (auto& p : g_squadPlaying) {
        s += "f\t" + std::to_string(p.first) + "\t" + p.second.nick + "\n";
        if (!p.second.icon.empty() || !p.second.background.empty())
            s += "badge\t" + std::to_string(p.first) + "\t" + p.second.icon + "\t" + p.second.background + "\n";
    }
    for (auto& r : g_recent) {
        bool fr = g_steam.GetFriendRelationship(g_steam.friends, kSteamIdBase + r.first) == kFriendRelationshipFriend;
        s += "r\t" + std::to_string(r.first) + "\t" + r.second + "\t" + (fr ? "1" : "0") + "\n";
    }
    return s;
}

// A session id from Lua, safe to keep in lobby data and the line protocol
static bool ValidSession(const std::string& session)
{
    return !session.empty() && session.size() < 256 && session.find_first_of("\t\n\r") == std::string::npos;
}

// One bridge call: fields separated by \1, the first is the command
static std::string SquadCommand(const std::string& text)
{
    std::vector<std::string> f;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i)
        if (i == text.size() || text[i] == '\1') { f.push_back(text.substr(start, i - start)); start = i + 1; }
    const std::string& cmd = f[0];
    auto arg = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
    if (cmd == "testteam") {
        g_squadTestTeam = arg(1) == "A" || arg(1) == "B" ? arg(1) : std::string();
        return "ok\n";
    }
    if (cmd == "skiphighlight") return SkipHighlight() ? "ok\n" : "none\n";   // the post-session skip key
    if (cmd == "match") SetMatchMission(arg(2));   // also without Steam: carried gear depends on it, not squads
    if (!SteamReady()) return "nosteam\n";
    SquadPoll();
    if (cmd == "state") return SquadState();
    if (cmd == "invite") {
        uint32_t a = ParseAccount(arg(2));          // the friend's external (Steam) id, else its player id
        if (!a) a = ParseAccount(arg(1));
        if (!a || a == g_steamMe) { Log("Squads: can't invite '%s' / '%s' (no Steam id)", arg(1).c_str(), arg(2).c_str()); return "error\n"; }
        // Only the leader invites: not a member, and not while joining someone's squad (that join makes us a member;
        // a queued invite would only be sent by a squad we create)
        if ((g_squadLobby && !SquadIsLeader()) || g_squadJoinCall) {
            Log("Squads: can't invite %s: only the squad's leader invites%s", SteamNick(a).c_str(),
                g_squadJoinCall ? " (joining a squad right now)" : "");
            return "error\n";
        }
        if (g_squadLobby) SquadSendInvite(a);
        else {
            g_squadQueued.insert(a);
            if (!g_squadCreateCall && !g_squadJoinCall)
                g_squadCreateCall = g_steam.CreateLobby(g_steam.mm, kLobbyTypePublic, kSquadMaxMembers);
        }
        return SquadState();
    }
    if (cmd == "accept") {
        auto it = g_squadInvites.find(ParseAccount(arg(1)));
        if (it == g_squadInvites.end() || g_squadJoinCall) return SquadState();
        SquadLeave("joining another squad");
        g_squadQueued.clear();                  // invites for a squad still being made: that squad is dropped
        g_squadJoinCall = g_steam.JoinLobby(g_steam.mm, it->second.lobby);
        g_squadInvites.erase(it);
        return SquadState();
    }
    if (cmd == "decline") {
        auto it = g_squadInvites.find(ParseAccount(arg(1)));
        if (it != g_squadInvites.end()) {
            g_squadDeclined.insert(it->second.lobby);
            Log("Squads: declined the invite of %s", it->second.nick.c_str());
            g_squadInvites.erase(it);
        }
        return SquadState();
    }
    if (cmd == "kick" && SquadIsLeader()) {
        uint32_t a = ParseAccount(arg(1));
        if (a && a != g_steamMe) {
            g_steam.SetLobbyData(g_steam.mm, g_squadLobby, AccountKey("kick_", a).c_str(), "1");
            g_squadKicked.insert(a);
            Log("Squads: kicked %s", SteamNick(a).c_str());
        }
        return SquadState();
    }
    if (cmd == "leader" && SquadIsLeader()) {
        uint32_t a = ParseAccount(arg(1));
        if (a && a != g_steamMe) {
            g_steam.SetLobbyData(g_steam.mm, g_squadLobby, "leader", std::to_string(a).c_str());
            g_steam.SetLobbyOwner(g_steam.mm, g_squadLobby, kSteamIdBase + a);
            Log("Squads: %s is the leader now", SteamNick(a).c_str());
        }
        return SquadState();
    }
    if (cmd == "follow" && SquadIsLeader()) {
        // The leader joined a server: "join" = "<Unix time>:<session id>", members' games join the same session
        std::string session = arg(1);
        if (ValidSession(session)) {
            g_steam.SetLobbyData(g_steam.mm, g_squadLobby, "join",
                                 (std::to_string((long long)time(nullptr)) + ":" + session).c_str());
            // The ready check: members in the lobby follow, a member in a match stays there
            std::string ready, busy;
            int n = g_steam.GetNumLobbyMembers(g_steam.mm, g_squadLobby);
            for (int i = 0; i < n; ++i) {
                uint32_t a = ToAccount(g_steam.GetLobbyMemberByIndex(g_steam.mm, g_squadLobby, i));
                if (a == g_steamMe || g_squadKicked.count(a)) continue;
                std::string& list = MemberStatus(a) == "match" ? busy : ready;
                list += (list.empty() ? "" : ", ") + SteamNick(a);
            }
            Log("Squads: the squad follows you to server %s (follow: %s; in a match, staying: %s)", session.c_str(),
                ready.empty() ? "nobody" : ready.c_str(), busy.empty() ? "nobody" : busy.c_str());
        }
        return SquadState();
    }
    if (cmd == "leave") { SquadLeave("left"); return SquadState(); }
    if (cmd == "match") {                       // you joined a server (any way: list, Play, following the leader)
        if (ValidSession(arg(1))) MatchStart(arg(1));
        return SquadState();
    }
    if (cmd == "lobby") { MatchEnd(); return SquadState(); }
    if (cmd == "friendadd" || cmd == "profile") {   // the Steam overlay: add as a friend / their Steam profile
        uint32_t a = ParseAccount(arg(1));
        if (!a || a == g_steamMe) return "error\n";
        g_steam.ActivateGameOverlayToUser(g_steam.friends, cmd == "friendadd" ? "friendadd" : "steamid", kSteamIdBase + a);
        Log("Squads: Steam overlay %s for %s (%u)", cmd == "friendadd" ? "friend request" : "profile", SteamNick(a).c_str(), a);
        return SquadState();
    }
    return "error\n";
}

// The bridge: Lua calls NameplateFrontend.GetBadgeImage("\1squad\1<command>\1<args...>", 0); a badge id never
// starts with \1, every other call goes to the game. The answer starts with "squad\n".
static const uintptr_t kGetBadgeImage = 0x143622F30, kGetBadgeImageThunk = 0x1401FCAFD;
static const unsigned char kGetBadgeImageProlog[] = { 0x44, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10,
                                                      0x55, 0x53, 0x56, 0x57 };
static const char kSquadBridgePrefix[] = "\1squad\1";
typedef char* (*GetBadgeImageFn)(void* self, char* out, const char* id, unsigned type);
static GetBadgeImageFn g_origGetBadgeImage;

static char* HookGetBadgeImage(void* self, char* out, const char* id, unsigned type)
{
    if (!id || strncmp(id, kSquadBridgePrefix, sizeof(kSquadBridgePrefix) - 1) != 0)
        return g_origGetBadgeImage(self, out, id, type);
    std::string reply = "squad\n" + SquadCommand(std::string(id + sizeof(kSquadBridgePrefix) - 1, strnlen(id, 4096) - (sizeof(kSquadBridgePrefix) - 1)));
    MakeGameString(out, "");                    // an empty string the game owns, then the game copies the reply in
    AssignGameString(out, reply);
    return out;
}

// SpawnPlayerRPC carries the team name (PlayerSpawnParams + 0xE0) the client gets from the GameSession::
// GameDataRequests broadcast (string* out, member call) with the handler's +0x18 call; online it was the team the
// matchmaker reserved, offline it is empty and the server puts the player on the smaller team. The server honors a
// name it knows ("Set backend provided team"); TDM teams are A and B. A squad gets one of them from its lobby id.
static const uintptr_t kGameDataBroadcast = 0x143395760, kGameDataBroadcastThunk = 0x1403B1A92;
static const unsigned char kGameDataBroadcastProlog[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
                                                          0x48, 0x89, 0x7C, 0x24, 0x20 };
static const uintptr_t kTeamNameCall = 0x1401FA465;
typedef void (*GameDataBroadcastFn)(char* out, const MemberCall* call);
static GameDataBroadcastFn g_origGameDataBroadcast;

static void HookGameDataBroadcast(char* out, const MemberCall* call)
{
    g_origGameDataBroadcast(out, call);
    if (!call || call->function != Va(kTeamNameCall)) return;
    g_matchSpawned = true;                      // spawning: the server join worked, you're in the match
    if (!ReadGameString(out).empty()) return;
    std::string team = SquadTeam();
    if (team != g_squadLoggedTeam) {
        Log("Squads: joining with team '%s'", team.empty() ? "(server's choice)" : team.c_str());
        g_squadLoggedTeam = team;
    }
    if (!team.empty()) AssignGameString(out, team);
}

static void InstallSquadHooks()
{
    g_origGetBadgeImage = (GetBadgeImageFn)RedirectThunk(
        kGetBadgeImageThunk, kGetBadgeImage, kGetBadgeImageProlog, sizeof(kGetBadgeImageProlog),
        (void*)&HookGetBadgeImage, "no squads");
    if (!g_origGetBadgeImage) return;
    g_origGameDataBroadcast = (GameDataBroadcastFn)RedirectThunk(
        kGameDataBroadcastThunk, kGameDataBroadcast, kGameDataBroadcastProlog, sizeof(kGameDataBroadcastProlog),
        (void*)&HookGameDataBroadcast, "squads don't share a team");
}

// Optional: without it the badge screen stays empty, as in the stock offline game
static void InstallNameplateHooks()
{
    InstallDeathCameraHooks();
    g_origMvpGetNameplate = (MvpGetNameplateFn)RedirectThunk(
        kMvpGetNameplateThunk, kMvpGetNameplate, kMvpGetNameplateProlog, sizeof(kMvpGetNameplateProlog),
        (void*)&HookMvpGetNameplate, "the MVP scene shows the default badge");
    if (g_origMvpGetNameplate)
        g_origMvpSpawnHero = (MvpSpawnHeroFn)RedirectThunk(
            kMvpSpawnHeroThunk, kMvpSpawnHero, kMvpSpawnHeroProlog, sizeof(kMvpSpawnHeroProlog),
            (void*)&HookMvpSpawnHero, "the MVP scene shows the default badge");
    g_origLoadAssetsElementCtor = (ElementCtorFn)RedirectThunk(
        kLoadAssetsElementCtorThunk, kLoadAssetsElementCtor, kLoadAssetsElementCtorProlog,
        sizeof(kLoadAssetsElementCtorProlog), (void*)&HookLoadAssetsElementCtor, "badge pictures are missing");
    if (!SlotHolds(kGetNameplateSlot, kGetNameplateThunk) || !SlotHolds(kSetNameplateSlot, kSetNameplateThunk) ||
        !SlotHolds(kGetBadgesSlot, kGetBadgesThunk)) {
        Log("Game code is not where expected; no badges");
        return;
    }
    g_origGetNameplate = (GetNameplateFn)Va(kGetNameplateThunk);
    g_origSetNameplate = (SetNameplateFn)Va(kSetNameplateThunk);
    g_origGetBadges = (GetBadgesFn)Va(kGetBadgesThunk);
    WriteSlot(kGetNameplateSlot, (void*)&HookGetNameplate);
    WriteSlot(kSetNameplateSlot, (void*)&HookSetNameplate);
    WriteSlot(kGetBadgesSlot, (void*)&HookGetBadges);
}

// New outfit parts: OutfitPartsManagerComponent::SpawnPart turns the game item's slice path into an asset id with
// AssetCatalogRequests::GetAssetIdByPath(path, DynamicSliceAsset, autoRegisterIfNotFound = false). Slices that only
// exist in our pak are not in the shipped asset catalog, so the part is never spawned ("Failed to get assetId by slice
// path"). Passing true lets the catalog register such a slice under its path, as the engine does for unlisted files;
// shipped slices are found as before.
static const uintptr_t kSpawnPartAutoRegister = 0x1426BE56A;
static const unsigned char kSpawnPartAutoRegisterOld[] = { 0x44, 0x88, 0x7C, 0x24, 0x40 };   // mov [rsp+40h], r15b (0)
static const unsigned char kSpawnPartAutoRegisterNew[] = { 0xC6, 0x44, 0x24, 0x40, 0x01 };   // mov byte [rsp+40h], 1

// Optional: without it outfit parts that only exist in our pak are not shown on the model
static void InstallOutfitPartPatch()
{
    unsigned char* p = (unsigned char*)Va(kSpawnPartAutoRegister);
    if (memcmp(p, kSpawnPartAutoRegisterOld, sizeof(kSpawnPartAutoRegisterOld)) != 0) {
        Log("Game code is not where expected; new outfit parts are not shown");
        return;
    }
    DWORD old;
    VirtualProtect(p, sizeof(kSpawnPartAutoRegisterNew), PAGE_EXECUTE_READWRITE, &old);
    memcpy(p, kSpawnPartAutoRegisterNew, sizeof(kSpawnPartAutoRegisterNew));
    VirtualProtect(p, sizeof(kSpawnPartAutoRegisterNew), old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, sizeof(kSpawnPartAutoRegisterNew));
}

// New outfits: their slices (third-person, first-person, lobby) are looked up by other code than SpawnPart, also with
// autoRegisterIfNotFound = false. Every such lookup goes through one EBus broadcaster (result AssetId*, handler
// member pointer, const char** path, const Uuid* type, const bool* autoRegister), reached through an incremental-link
// thunk. A lookup that fails for one of OUR slices (under slices/custom/) is repeated with autoRegister; any other
// path behaves as shipped.
static const uintptr_t kGetAssetIdByPathThunk = 0x1400EDA9F, kGetAssetIdByPath = 0x1409F3E40;
static const unsigned char kGetAssetIdByPathProlog[] = { 0x40, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57,
                                                         0x48, 0x8D, 0x6C, 0x24, 0xC8 };
static const char kCustomSlicePrefix[] = "slices/custom/";
typedef void (*GetAssetIdByPathFn)(void* result, void* handler, const char* const* path, const void* type,
                                   const bool* autoRegister);
static GetAssetIdByPathFn g_origGetAssetIdByPath;

// New models: a slice names its mesh by asset id, and the shipped catalog only lists shipped files. Every .cgf in our
// paks (game01\shintsugi*.pak, kModPaks) that the catalog doesn't know is registered under its path, which gives it
// the id Uuid::CreateName(path):0 (SHA-1 name UUID) - the id our slices are written with (make_headgear_variant.ps1).
// Done once, at the first slice lookup (the catalog is loaded by then).
// The mod's paks in game01: the client pak shintsugi.pak (and any later companion pak, also named shintsugi...)
static const wchar_t kModPaks[] = L"shintsugi*.pak";
static const unsigned char kMeshAssetType[16] = { 0xC2, 0x86, 0x9E, 0x3B, 0xDD, 0xA0, 0x4E, 0x01,
                                                  0x8F, 0xE3, 0x67, 0x70, 0xD7, 0x88, 0x86, 0x6B };

// Names of the .cgf files in one zip (central directory only)
static void ListPakModels(const std::wstring& pak, std::vector<std::string>& models)
{
    FILE* f = _wfopen(pak.c_str(), L"rb");
    if (!f) return;
    std::vector<unsigned char> tail;
    if (_fseeki64(f, 0, SEEK_END) == 0) {
        long long size = _ftelli64(f), n = size < 66000 ? size : 66000;
        tail.resize((size_t)n);
        if (_fseeki64(f, size - n, SEEK_SET) != 0 || fread(tail.data(), 1, tail.size(), f) != tail.size()) tail.clear();
    }
    for (size_t i = tail.size() >= 22 ? tail.size() - 22 : 0; tail.size() >= 22; i--) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4B && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            uint32_t cdSize, cdOffset;
            memcpy(&cdSize, &tail[i + 12], 4); memcpy(&cdOffset, &tail[i + 16], 4);
            std::vector<unsigned char> cd(cdSize);
            if (cdOffset != 0xFFFFFFFF && _fseeki64(f, cdOffset, SEEK_SET) == 0 && fread(cd.data(), 1, cd.size(), f) == cd.size()) {
                for (size_t p = 0; p + 46 <= cd.size() && memcmp(&cd[p], "PK\x01\x02", 4) == 0;) {
                    uint16_t nameLen, extraLen, commentLen;
                    memcpy(&nameLen, &cd[p + 28], 2); memcpy(&extraLen, &cd[p + 30], 2); memcpy(&commentLen, &cd[p + 32], 2);
                    if (p + 46 + nameLen > cd.size()) break;
                    std::string name((const char*)&cd[p + 46], nameLen);
                    if (name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".cgf") == 0) models.push_back(name);
                    p += 46 + nameLen + extraLen + commentLen;
                }
            }
            break;
        }
        if (i == 0) break;
    }
    fclose(f);
}

static void RegisterCustomModels(void* handler)
{
    static volatile LONG done;
    if (InterlockedExchange(&done, 1)) return;
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring dir(exe, n);
    dir = dir.substr(0, dir.find_last_of(L'\\'));                      // ...\Bin
    dir = dir.substr(0, dir.find_last_of(L'\\')) + L"\\game01\\";     // ...\game01
    std::vector<std::string> models;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + kModPaks).c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do ListPakModels(dir + fd.cFileName, models); while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (models.empty()) Log("No models of ours to register: no .cgf in game01\\%ls", kModPaks);
    static const unsigned char nullId[16] = {};
    for (const std::string& m : models) {
        unsigned char id[32] = {};
        const char* p = m.c_str();
        const bool no = false, yes = true;
        g_origGetAssetIdByPath(id, handler, &p, kMeshAssetType, &no);
        if (memcmp(id, nullId, 16) != 0) continue;                      // a shipped model we override
        g_origGetAssetIdByPath(id, handler, &p, kMeshAssetType, &yes);
        uint32_t sub; memcpy(&sub, id + 16, 4);
        Log("Registered our model %s as {%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X}:%x", p,
            id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9], id[10], id[11], id[12], id[13], id[14],
            id[15], sub);
    }
}

static void HookGetAssetIdByPath(void* result, void* handler, const char* const* path, const void* type,
                                 const bool* autoRegister)
{
    if (path && *path && strstr(*path, "slice")) RegisterCustomModels(handler);
    g_origGetAssetIdByPath(result, handler, path, type, autoRegister);
    if (*autoRegister || !path || !*path || strncmp(*path, kCustomSlicePrefix, sizeof(kCustomSlicePrefix) - 1) != 0)
        return;
    static const unsigned char nullId[16] = {};
    if (memcmp(result, nullId, sizeof(nullId)) != 0) return;   // already known (registered earlier)
    const bool yes = true;
    g_origGetAssetIdByPath(result, handler, path, type, &yes);
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 20) Log("Registered our slice %s", *path);
}

// Optional: without it outfits that only exist in our pak show the default outfit
static void InstallCustomSliceLookup()
{
    g_origGetAssetIdByPath = (GetAssetIdByPathFn)RedirectThunk(
        kGetAssetIdByPathThunk, kGetAssetIdByPath, kGetAssetIdByPathProlog, sizeof(kGetAssetIdByPathProlog),
        (void*)&HookGetAssetIdByPath, "new outfits are not shown");
}

// ---------------------------------------------------------------------------------------------------------
// Installation

// Online backend (probe): the ClientBackend module's GetRequiredSystemComponents always takes its offline list
// (vtable +0x50, the *ComponentStub set); +0x48 builds the online list (AuthComponent, NameplateComponent,
// PresenceComponent, ...). Only when the game is started with -auth_server <url> (the real AuthComponent's login
// address) is the online list used, so a normal start is unchanged.
static const uintptr_t kClientBackendModuleVtable = 0x15166F108;
static const uintptr_t kOfflineComponentsSlot = kClientBackendModuleVtable + 0x50, kOfflineComponents = 0x14019154B;
static const uintptr_t kOnlineComponentsSlot = kClientBackendModuleVtable + 0x48, kOnlineComponents = 0x140124A8B;

static void InstallOnlineBackendSwitch()
{
    if (!wcsstr(GetCommandLineW(), L"-auth_server")) return;
    if (!SlotHolds(kOfflineComponentsSlot, kOfflineComponents) || !SlotHolds(kOnlineComponentsSlot, kOnlineComponents)) {
        Log("Backend: module code is not where expected; staying offline");
        return;
    }
    WriteSlot(kOfflineComponentsSlot, (void*)Va(kOnlineComponents));
    Log("Backend: ONLINE components (started with -auth_server)");
}

static bool SlotHolds(uintptr_t slot, uintptr_t expected)
{
    return *(uintptr_t*)Va(slot) == Va(expected);
}

static void WriteSlot(uintptr_t slot, void* target)
{
    uintptr_t* p = (uintptr_t*)Va(slot);
    DWORD old;
    VirtualProtect(p, sizeof(*p), PAGE_READWRITE, &old);
    *p = (uintptr_t)target;
    VirtualProtect(p, sizeof(*p), old, &old);
}

// Optional: without it the wallet still works, owned items are just not restored or saved
static void InstallInventoryHooks()
{
    if (!SlotHolds(kInventoryVtable + kInventoryAddItem * 8, kInventoryAddItemThunk) ||
        !SlotHolds(kInventoryVtable + kInventoryHasItem * 8, kInventoryHasItemThunk)) {
        g_inventoryBroken = true;
        Log("ERROR: inventory code is not where expected; owned items are not restored or saved");
        return;
    }
    g_origInventoryAddItem = (InventoryAddItemFn)Va(kInventoryAddItemThunk);
    g_inventoryHasItem = (InventoryHasItemFn)Va(kInventoryHasItemThunk);
    // Without it a badge changed in the lobby leaves the old badge items until the next inventory reload
    if (SlotHolds(kInventoryVtable + kInventoryRemoveItem * 8, kInventoryRemoveItemThunk))
        g_inventoryRemoveItem = (InventoryRemoveItemFn)Va(kInventoryRemoveItemThunk);
    else Log("Inventory RemoveItem is not where expected; an old badge stays until the next inventory reload");
    // Without it the badge items stay unlinked: other players see the default badge
    g_badgeSlots = SlotHolds(kInventoryVtable + kInventoryFind * 8, kInventoryFindThunk) &&
                   SlotHolds(kItemSlotVtable + kSlotName * 8, kSlotNameThunk) &&
                   SlotHolds(kItemSlotVtable + kSlotLinkedName * 8, kSlotLinkedNameThunk) &&
                   SlotHolds(kItemSlotVtable + kSlotLink * 8, kSlotLinkThunk) &&
                   SlotHolds(kItemSlotVtable + kSlotUnlink * 8, kSlotUnlinkThunk) &&
                   SlotHolds(kItemSlotVtable + kSlotCommit * 8, kSlotCommitThunk);
    if (!g_badgeSlots) Log("Item slot code is not where expected; badges are not shown to other players");
    // Without this one owned items still come back (in the lobby), but equipped perks must be re-equipped
    g_origApplyItemLinks = (ApplyItemLinksFn)RedirectThunk(
        kApplyItemLinksThunk, kApplyItemLinks, kApplyItemLinksProlog, sizeof(kApplyItemLinksProlog),
        (void*)&HookApplyItemLinks, "equipped bought perks are unequipped after each inventory reload");
}

static void Install()
{
    g_base = (uintptr_t)GetModuleHandleW(nullptr);
    auto dos = (IMAGE_DOS_HEADER*)g_base;
    auto nt = (IMAGE_NT_HEADERS64*)(g_base + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != kTimeDateStamp || nt->OptionalHeader.SizeOfImage != kSizeOfImage) {
        Log("Different game build (timestamp %08lX, size %08lX); offline profile disabled",
            nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        return;
    }
    bool ok = SlotHolds(kWalletInitSlot, kWalletInitThunk);
    for (uintptr_t s : kAddCurrencySlots) ok = ok && SlotHolds(s, kAddCurrencyThunk);
    for (uintptr_t s : kGetCurrencySlots) ok = ok && SlotHolds(s, kGetCurrencyThunk);
    for (uintptr_t s : kUpdateCurrencySlots) ok = ok && SlotHolds(s, kUpdateCurrencyThunk);
    if (!ok) {
        Log("Game code is not where expected; offline profile disabled");
        return;
    }
    g_origWalletInit = (WalletInitFn)Va(kWalletInitThunk);
    g_origAddCurrency = (AddCurrencyFn)Va(kAddCurrencyThunk);
    g_origGetCurrency = (GetCurrencyFn)Va(kGetCurrencyThunk);
    g_origUpdateCurrency = (UpdateCurrencyFn)Va(kUpdateCurrencyThunk);
    g_walletMapIndex = (WalletMapIndexFn)Va(kWalletMapIndex);
    g_inventoryGetter = (InventoryGetterFn)Va(kInventoryGetter);

    // With the online backend (-auth_server, for tests) the backend owns wallet, items and badge: the offline profile
    // is neither loaded nor saved (g_profileLoaded stays false) and the saved badge isn't forced onto the nameplate
    bool online = wcsstr(GetCommandLineW(), L"-auth_server") != nullptr;
    if (online) Log("Online backend (-auth_server): the offline profile is not used this session");
    else LoadProfile();
    WriteSlot(kWalletInitSlot, (void*)&HookWalletInit);
    for (uintptr_t s : kAddCurrencySlots) WriteSlot(s, (void*)&HookAddCurrency);
    for (uintptr_t s : kGetCurrencySlots) WriteSlot(s, (void*)&HookGetCurrency);
    for (uintptr_t s : kUpdateCurrencySlots) WriteSlot(s, (void*)&HookUpdateCurrency);
    InstallInventoryHooks();
    InstallSessionRewardsHook();
    InstallMvpSceneHook();
    if (!online) InstallNameplateHooks();
    InstallSquadHooks();
    InstallOutfitPartPatch();
    InstallCustomSliceLookup();
    InstallOnlineBackendSwitch();
    Log("Hooks installed (game base %p)", (void*)g_base);
}

static void InitPaths()
{
    // %LOCALAPPDATA%\Shatterline, next to the game's own .savedata files. Read from the environment because
    // shell APIs are unsafe inside DllMain (loader lock).
    wchar_t local[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    g_dir = (n > 0 && n < MAX_PATH) ? std::wstring(local) + L"\\Shatterline" : std::wstring(L".");
    CreateDirectoryW(g_dir.c_str(), nullptr);
    g_profilePath = g_dir + L"\\offline_profile.json";
    g_logPath = g_dir + L"\\offline_profile.log";
    // The previous session's log is kept as offline_profile.prev.log (a restart after a hang must not erase it)
    MoveFileExW(g_logPath.c_str(), (g_dir + L"\\offline_profile.prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
}

// ---------------------------------------------------------------------------------------------------------
// version.dll proxy: every export jumps to the same function in System32\version.dll

#define VERSION_EXPORTS(X) \
    X(GetFileVersionInfoA) X(GetFileVersionInfoByHandle) X(GetFileVersionInfoExA) X(GetFileVersionInfoExW) \
    X(GetFileVersionInfoSizeA) X(GetFileVersionInfoSizeExA) X(GetFileVersionInfoSizeExW) X(GetFileVersionInfoSizeW) \
    X(GetFileVersionInfoW) X(VerFindFileA) X(VerFindFileW) X(VerInstallFileA) X(VerInstallFileW) \
    X(VerLanguageNameA) X(VerLanguageNameW) X(VerQueryValueA) X(VerQueryValueW)

extern "C" {
#define DECLARE_REAL(name) FARPROC real_##name;
VERSION_EXPORTS(DECLARE_REAL)
}
#define DEFINE_STUB(name) \
    asm(".globl proxy_" #name "\nproxy_" #name ":\n    jmp *real_" #name "(%rip)\n");
VERSION_EXPORTS(DEFINE_STUB)

static bool LoadRealVersionDll()
{
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 16) return false;
    wcscat(path, L"\\version.dll");
    HMODULE real = LoadLibraryW(path);
    if (!real) return false;
#define RESOLVE(name) real_##name = GetProcAddress(real, #name);
    VERSION_EXPORTS(RESOLVE)
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// Launch options: the classic lobby, the server browser and the offline inventory are chosen by command-line
// options. They are added here, so nobody has to type them into Steam. An option already on the command line
// (a player's own launch options) is left as it is.
//
// The game reads its arguments through the C runtime (_configure_narrow_argv, which splits _acmdln) after all its
// DLLs are initialized. ucrtbase is initialized before this DLL (it is one of our imports), so its _acmdln and
// _wcmdln already point at Windows' command line; they are pointed at the extended one instead.

static const struct { const char* name; const char* value; } kLaunchOptions[] = {
    { "-platform_type", "Steam" },
    { "+flow_server_browser_enabled", "1" },
    { "+persistent_info_enabled", "1" },
    { "+sys_DeactivateConsole", "0" },
    { "+backend_offline_inventory_file_name", "inventory_jar_test.json" },
    { "+backend_offline_claimable_rewards_file_name", "rewards_progress_test.json" },
    { "+flow_layout_config", "libs/ui/flowchart_classic.json" },
    { "+flow_lobby_mission", "lobby" },
};

// True when the command line has the option as a whole word (any case; cvar names are case-insensitive)
static bool HasOption(const std::string& line, const char* name)
{
    size_t len = strlen(name);
    for (size_t i = 0; i + len <= line.size(); ++i) {
        if (i > 0 && line[i - 1] != ' ' && line[i - 1] != '\t' && line[i - 1] != '"') continue;
        if (_strnicmp(line.c_str() + i, name, len) != 0) continue;
        char next = i + len < line.size() ? line[i + len] : ' ';
        if (next == ' ' || next == '\t' || next == '"' || next == '=') return true;
    }
    return false;
}

static void AddLaunchOptions()
{
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* name = wcsrchr(exe, L'\\');
    if (n == 0 || n >= MAX_PATH || !name || _wcsicmp(name + 1, L"Shatterline.exe") != 0) return;
    HMODULE crt = GetModuleHandleW(L"ucrtbase.dll");
    auto acmdln = crt ? (char** (*)())GetProcAddress(crt, "__p__acmdln") : nullptr;
    auto wcmdln = crt ? (wchar_t** (*)())GetProcAddress(crt, "__p__wcmdln") : nullptr;
    if (!acmdln || !wcmdln || !*acmdln() || !*wcmdln()) {
        Log("Launch options: the C runtime's command line was not found; set them in Steam (see README)");
        return;
    }
    std::string line = *acmdln(), added;
    for (auto& o : kLaunchOptions) {
        if (HasOption(line, o.name)) continue;
        added += std::string(" ") + o.name + " " + o.value;
    }
    if (added.empty()) {
        Log("Launch options: all set already");
        return;
    }
    // Kept for the whole process: the runtime and the game hold on to these pointers
    std::string a = line + added;
    std::wstring w = std::wstring(*wcmdln()) + std::wstring(added.begin(), added.end());
    char* newA = (char*)HeapAlloc(GetProcessHeap(), 0, a.size() + 1);
    wchar_t* newW = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (w.size() + 1) * sizeof(wchar_t));
    if (!newA || !newW) return;
    memcpy(newA, a.c_str(), a.size() + 1);
    memcpy(newW, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
    *acmdln() = newA;
    *wcmdln() = newW;
    Log("Launch options added:%s", added.c_str());
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&g_lock);
        if (!LoadRealVersionDll()) return FALSE;
        InitPaths();
        Log("Shatterline Offline Profile starting (built " __DATE__ " " __TIME__ ")");
        AddLaunchOptions();
        Install();
    }
    return TRUE;
}
