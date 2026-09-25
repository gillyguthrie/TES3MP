/*
    majere addition (ingredient finder) -- see ingredients.hpp
*/
#include "ingredients.hpp"

#include "mode.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/SessionLog.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <typeinfo>

#include <MyGUI_Gui.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_LayerManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_RotatingSkin.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_ITexture.h>
#include <MyGUI_LayerManager.h>

#include <osg/Texture2D>

#include <components/debug/debuglog.hpp>
#include <components/esm/attr.hpp>
#include <components/esm/loadcell.hpp>
#include <components/esm/loadregn.hpp>
#include <components/myguiplatform/myguitexture.hpp>
#include <components/esm/loadcont.hpp>
#include <components/esm/loaddoor.hpp>
#include <components/esm/loadingr.hpp>
#include <components/esm/loadlevlist.hpp>
#include <components/esm/loadmgef.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/esm/loadskil.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/stringops.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwrender/globalmap.hpp"
#include "mapwindow.hpp"

#include "hotbar.hpp"

namespace
{
    int settingInt(const char* key, int def)
    {
        try { return Settings::Manager::getInt(key, "Ingredients"); } catch (...) { return def; }
    }
    float settingFloat(const char* key, float def)
    {
        try { return Settings::Manager::getFloat(key, "Ingredients"); } catch (...) { return def; }
    }

    bool settingBool(const char* key, bool def)
    {
        try { return Settings::Manager::getBool(key, "Ingredients"); } catch (...) { return def; }
    }
    std::string settingString(const char* key, const std::string& def)
    {
        try { return Settings::Manager::getString(key, "Ingredients"); } catch (...) { return def; }
    }

    const int sIcon = 32;
    const int sIconGap = 16;
    const int sCell = 22;         // 3x3 map cell (GUI px)
    const int sCellGap = 2;
    const int sCoin = 10;         // shop marker in a map cell
    const int sArrowBox = 16;     // the facing arrow: centred on where the player stands within the middle cell
    const int sWorldPad = 8;
    const int sWorldCellsPerFrame = 40;
    const int sWorldInteriorsPerFrame = 12;
    const int sGridIconGap = 4;   // between the map and its icon column
    const size_t sMaxTracked = 3;
    // the picker
    const int sPad = 8;
    const int sTile = 40;         // icon tile
    const int sTileGap = 4;
    const int sCols = 10;
    const int sHeaderH = 18;
    const int sSearchH = 24;
    const int sMaxResults = 120;  // twelve rows
    const size_t sMaxRecents = 12;
    const float sScanInterval = 1.f;
    const MyGUI::Colour sGold(0.84f, 0.67f, 0.33f);

    std::string hex(const MyGUI::Colour& c)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", static_cast<int>(c.red * 255 + 0.5f),
                 static_cast<int>(c.green * 255 + 0.5f), static_cast<int>(c.blue * 255 + 0.5f));
        return buf;
    }

    // split on commas / semicolons (settings lists) -- or on spaces too when asked (search words)
    std::vector<std::string> splitList(const std::string& s, bool spacesToo = false)
    {
        std::vector<std::string> out;
        std::string cur;
        for (char c : s)
        {
            if (c == ',' || c == ';' || (spacesToo && c == ' ')) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
            else if (c != ' ' || !cur.empty()) cur += c;
        }
        while (!cur.empty() && cur.back() == ' ') cur.pop_back();
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    std::string joinList(const std::vector<std::string>& v)
    {
        std::string s;
        for (const std::string& x : v) s += (s.empty() ? "" : ",") + x;
        return s;
    }

    // effect categories for the colour code
    enum Category { Cat_None, Cat_Fire, Cat_Frost, Cat_Shock, Cat_Dispel, Cat_Fatigue, Cat_Health };
    Category categoryOf(int effectId)
    {
        switch (effectId)
        {
            case ESM::MagicEffect::FireDamage: case ESM::MagicEffect::ResistFire: case ESM::MagicEffect::FireShield: case ESM::MagicEffect::WeaknessToFire:
                return Cat_Fire;
            case ESM::MagicEffect::FrostDamage: case ESM::MagicEffect::ResistFrost: case ESM::MagicEffect::FrostShield: case ESM::MagicEffect::WeaknessToFrost:
                return Cat_Frost;
            case ESM::MagicEffect::ShockDamage: case ESM::MagicEffect::ResistShock: case ESM::MagicEffect::LightningShield: case ESM::MagicEffect::WeaknessToShock:
                return Cat_Shock;
            case ESM::MagicEffect::Dispel: case ESM::MagicEffect::SpellAbsorption:
                return Cat_Dispel;
            case ESM::MagicEffect::RestoreFatigue: case ESM::MagicEffect::FortifyFatigue: case ESM::MagicEffect::DrainFatigue: case ESM::MagicEffect::DamageFatigue: case ESM::MagicEffect::AbsorbFatigue:
                return Cat_Fatigue;
            case ESM::MagicEffect::RestoreHealth: case ESM::MagicEffect::FortifyHealth: case ESM::MagicEffect::DrainHealth: case ESM::MagicEffect::DamageHealth: case ESM::MagicEffect::AbsorbHealth:
                return Cat_Health;
            default:
                return Cat_None;
        }
    }
    const MyGUI::Colour& categoryColour(Category c, const MyGUI::Colour& normal)
    {
        // the elements as the spells look in flight: a fireball's orange, a frost bolt's icy pale blue, a
        // shock bolt's electric violet (the effect icons themselves are all school red / blue, no help)
        static const MyGUI::Colour fire(1.f, 0.58f, 0.15f);
        static const MyGUI::Colour frost(0.62f, 0.86f, 1.f);
        static const MyGUI::Colour shock(0.68f, 0.55f, 1.f);
        static const MyGUI::Colour dispel(1.f, 1.f, 1.f);        // white
        static const MyGUI::Colour fatigue(0.5f, 0.68f, 0.3f);   // moss green, Vvardenfell foliage rather than neon
        static const MyGUI::Colour health(1.f, 0.25f, 0.45f);    // crimson, apart from the fire red
        switch (c)
        {
            case Cat_Fire: return fire;
            case Cat_Frost: return frost;
            case Cat_Shock: return shock;
            case Cat_Dispel: return dispel;
            case Cat_Fatigue: return fatigue;
            case Cat_Health: return health;
            default: return normal;
        }
    }
    // how many ingredient records carry each effect: the commonest effect is the one most brewed
    const std::vector<int>& effectFrequency()
    {
        static std::vector<int> freq;
        if (freq.empty())
        {
            freq.assign(ESM::MagicEffect::Length, 0);
            const MWWorld::Store<ESM::Ingredient>& store = MWBase::Environment::get().getWorld()->getStore().get<ESM::Ingredient>();
            for (const ESM::Ingredient& rec : store)
                for (int i = 0; i < 4; ++i)
                {
                    const int id = rec.mData.mEffectID[i];
                    if (id >= 0 && id < ESM::MagicEffect::Length) ++freq[id];
                }
        }
        return freq;
    }
    // the ingredient's categories, the one whose effect more ingredients share first (at most two are used)
    std::vector<Category> categoriesOf(const ESM::Ingredient& rec)
    {
        const std::vector<int>& freq = effectFrequency();
        std::vector<std::pair<int, Category>> found;   // (frequency, category)
        for (int i = 0; i < 4; ++i)
        {
            const int id = rec.mData.mEffectID[i];
            if (id < 0 || id >= ESM::MagicEffect::Length)
                continue;
            const Category c = categoryOf(id);
            if (c == Cat_None)
                continue;
            bool have = false;
            for (auto& f : found)
                if (f.second == c) { have = true; f.first = std::max(f.first, freq[id]); }
            if (!have)
                found.emplace_back(freq[id], c);
        }
        std::stable_sort(found.begin(), found.end(), [](const std::pair<int, Category>& a, const std::pair<int, Category>& b) { return a.first > b.first; });
        std::vector<Category> out;
        for (const auto& f : found)
            if (out.size() < 2) out.push_back(f.second);
        return out;
    }

    // "Fortify Attribute" + Strength -> "Fortify Strength", as the game names it
    std::string effectName(int id, int attribute, int skill)
    {
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
        const ESM::MagicEffect* effect = MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().search(id);
        if (!effect)
            return "";
        std::string name = wm->getGameSettingString(ESM::MagicEffect::effectIdToString(effect->mIndex), "");
        std::string target;
        if ((effect->mData.mFlags & ESM::MagicEffect::TargetSkill) && skill >= 0 && skill < ESM::Skill::Length)
            target = wm->getGameSettingString(ESM::Skill::sSkillNameIds[skill], "");
        else if ((effect->mData.mFlags & ESM::MagicEffect::TargetAttribute) && attribute >= 0 && attribute < ESM::Attribute::Length)
            target = wm->getGameSettingString(ESM::Attribute::sGmstAttributeIds[attribute], "");
        if (target.empty())
            return name;
        static const char* const generic[] = { " Attribute", " Skill" };
        for (const char* g : generic)
        {
            const size_t p = name.find(g);
            if (p != std::string::npos && p + std::strlen(g) == name.size())
                return name.substr(0, p) + " " + target;
        }
        return name + " " + target;
    }

    // Does this inventory entry yield the ingredient? Directly, or through a levelled item list: plants hold
    // lists like "random_bunglers_bane" (with a chance of nothing), not the ingredient itself.
    bool idYields(const std::string& item, const std::vector<std::string>& ids, int depth = 0)
    {
        for (const std::string& id : ids)
            if (Misc::StringUtils::ciEqual(item, id))
                return true;
        if (depth > 5)
            return false;
        const ESM::ItemLevList* list = MWBase::Environment::get().getWorld()->getStore().get<ESM::ItemLevList>().search(item);
        if (!list)
            return false;
        for (const ESM::LevelledListBase::LevelItem& entry : list->mList)
            if (idYields(entry.mId, ids, depth + 1))
                return true;
        return false;
    }

    bool listHas(const ESM::InventoryList& inv, const std::vector<std::string>& ids)
    {
        for (const ESM::ContItem& it : inv.mList)
            if (idYields(it.mItem, ids))
                return true;
        return false;
    }

    // the tracked plants of a cell for the local map's dots: where they are, which row they feed, and whether
    // they still stand on this client
    struct PlantWalk
    {
        const std::vector<MWGui::IngredientDef>& table;
        const std::vector<bool>& tracked;
        bool localServer;
        struct Hit { float x, y; size_t row; bool standing, placed; };
        std::vector<Hit> hits;
        PlantWalk(const std::vector<MWGui::IngredientDef>& t, const std::vector<bool>& tr, bool local) : table(t), tracked(tr), localServer(local) {}

        bool operator()(const MWWorld::Ptr& ptr)
        {
            const std::string& type = ptr.getClass().getTypeName();
            size_t row = table.size();
            if (type == typeid(ESM::Ingredient).name())
            {
                const std::string& id = ptr.getCellRef().getRefId();
                for (size_t i = 0; i < table.size() && row == table.size(); ++i)
                    if (i < tracked.size() && tracked[i])
                        for (const std::string& want : table[i].ids)
                            if (Misc::StringUtils::ciEqual(id, want)) { row = i; break; }
            }
            else if (type == typeid(ESM::Container).name())
            {
                const ESM::Container* rec = ptr.get<ESM::Container>()->mBase;
                if (!(rec->mFlags & ESM::Container::Organic))
                    return true;
                for (size_t i = 0; i < table.size() && row == table.size(); ++i)
                    if (i < tracked.size() && tracked[i] && listHas(rec->mInventory, table[i].ids))
                        row = i;
            }
            if (row == table.size())
                return true;
            Hit h;
            const ESM::Position& p = ptr.getRefData().getPosition();
            h.x = p.pos[0]; h.y = p.pos[1];
            h.row = row;
            h.placed = ptr.getCellRef().getRefNum().hasContentFile();
            h.standing = !ptr.getRefData().isDeleted() && ptr.getRefData().getCount() > 0 && ptr.getRefData().isEnabled();
            // our own plain local server: an opened plant keeps standing, its emptied contents say it is picked
            if (h.standing && localServer && type == typeid(ESM::Container).name() && ptr.getRefData().getCustomData())
            {
                bool left = false;
                MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);
                for (MWWorld::ContainerStoreIterator it = store.begin(); it != store.end(); ++it)
                    if (it->getClass().getTypeName() == typeid(ESM::Ingredient).name() && it->getRefData().getCount() > 0) { left = true; break; }
                h.standing = left;
            }
            hits.push_back(h);
            return true;
        }
    };

    // one walk over the cell for every ingredient on the list
    struct CellScan
    {
        const std::vector<MWGui::IngredientDef>& table;
        std::vector<int> raw;                                // per ingredient: static placements
        std::vector<std::vector<std::string>> sellers;       // per ingredient: live NPC names here
        const std::vector<std::string>* shopCells;           // lower-case interior names of tracked shops (may be null)
        bool shopDoor;                                       // a door here leads into one of them
        std::vector<std::string> doorShops;                  // which of them (lower-case cell names)
        bool inside;                                         // an interior: stacks and any container count, doors are exits
        std::vector<std::string> exitsInterior;
        std::vector<std::pair<int, int>> exitsExterior;
        explicit CellScan(const std::vector<MWGui::IngredientDef>& t) : table(t), raw(t.size(), 0), sellers(t.size()), shopCells(nullptr), shopDoor(false), inside(false) {}

        bool operator()(const MWWorld::Ptr& ptr)
        {
            const std::string& type = ptr.getClass().getTypeName();
            // STATIC placement counts: only references from a content file (dropped items and server-spawned
            // objects have none); picked / disabled / deleted plants still count -- it says whether this is a
            // good spot, not what is left right now.
            if (type == typeid(ESM::Ingredient).name())
            {
                if (!ptr.getCellRef().getRefNum().hasContentFile())
                    return true;
                const std::string& id = ptr.getCellRef().getRefId();
                const int stack = inside ? std::max(1, ptr.getRefData().getCount()) : 1;
                for (size_t i = 0; i < table.size(); ++i)
                    for (const std::string& want : table[i].ids)
                        if (Misc::StringUtils::ciEqual(id, want)) { raw[i] += stack; break; }
            }
            else if (type == typeid(ESM::Container).name())
            {
                const ESM::Container* rec = ptr.get<ESM::Container>()->mBase;
                if (!ptr.getCellRef().getRefNum().hasContentFile())
                    return true;
                if (inside && !(rec->mFlags & ESM::Container::Organic))
                {
                    // a crate, sack or chest: what its record holds, by the stack
                    for (const ESM::ContItem& item : rec->mInventory.mList)
                        for (size_t i = 0; i < table.size(); ++i)
                            for (const std::string& want : table[i].ids)
                                if (Misc::StringUtils::ciEqual(item.mItem, want)) { raw[i] += std::abs(item.mCount); break; }
                    return true;
                }
                if (!(rec->mFlags & ESM::Container::Organic))
                    return true;
                for (size_t i = 0; i < table.size(); ++i)
                    if (listHas(rec->mInventory, table[i].ids))
                        ++raw[i];
            }
            else if (type == typeid(ESM::Door).name())
            {
                if (!ptr.getCellRef().getTeleport())
                    return true;
                const std::string dest = Misc::StringUtils::lowerCase(ptr.getCellRef().getDestCell());
                if (inside)
                {
                    if (dest.empty())
                    {
                        const ESM::Position& out = ptr.getCellRef().getDoorDest();
                        const float cs = static_cast<float>(Constants::CellSizeInUnits);
                        exitsExterior.emplace_back(static_cast<int>(std::floor(out.pos[0] / cs)), static_cast<int>(std::floor(out.pos[1] / cs)));
                    }
                    else
                        exitsInterior.push_back(dest);
                    return true;
                }
                if (!shopCells)
                    return true;
                for (const std::string& s : *shopCells)
                    if (s == dest) { shopDoor = true; doorShops.push_back(dest); break; }
            }
            else if (type == typeid(ESM::NPC).name())
            {
                // sellers are live: only NPCs actually here and not gone
                if (ptr.getRefData().isDeleted() || ptr.getRefData().getCount() <= 0 || !ptr.getRefData().isEnabled())
                    return true;
                if (!(ptr.getClass().getServices(ptr) & ESM::NPC::Ingredients))
                    return true;
                const ESM::InventoryList& base = ptr.get<ESM::NPC>()->mBase->mInventory;
                MWWorld::ContainerStore* store = nullptr;
                for (size_t i = 0; i < table.size(); ++i)
                {
                    bool has = listHas(base, table[i].ids);
                    if (!has)
                    {
                        if (!store)
                            store = &ptr.getClass().getContainerStore(ptr);
                        for (MWWorld::ContainerStoreIterator it = store->begin(); it != store->end(); ++it)
                        {
                            const std::string& id = it->getCellRef().getRefId();
                            for (const std::string& want : table[i].ids)
                                if (Misc::StringUtils::ciEqual(id, want)) { has = true; break; }
                            if (has) break;
                        }
                    }
                    if (has)
                        sellers[i].push_back(ptr.getClass().getName(ptr));
                }
            }
            return true;
        }
    };

    // the seller-table entry (with its shops and colour) that covers a record id, if any
    const MWGui::IngredientDef* sellerTableEntry(const std::string& id)
    {
        for (const MWGui::IngredientDef& def : MWGui::ingredientTable())
            for (const std::string& x : def.ids)
                if (Misc::StringUtils::ciEqual(x, id))
                    return &def;
        return nullptr;
    }
}

namespace MWGui
{
    Ingredients::Ingredients(Hotbar* hotbar)
        : mEnabled(true), mInfoWidth(460), mShowShops(false), mShowRaw(false), mHudVisible(false), mHotbar(hotbar)
        , mTrackIcon(nullptr), mShopsIcon(nullptr), mRawIcon(nullptr), mNormalColour(MyGUI::Colour::White)
        , mPicker(nullptr), mSearchEdit(nullptr), mPickerClose(nullptr), mPickerMap(nullptr), mInfo(nullptr), mGrid(nullptr), mCompass(nullptr)
        , mMapWindow(nullptr), mMapButton(nullptr), mWorldMap(nullptr), mWorldImage(nullptr), mWorldStatus(nullptr), mWorldClose(nullptr), mWorldPick(nullptr)
        , mWorldArrow(nullptr), mWorldScanning(false), mWorldNext(0), mWorldInteriorNext(0), mWorldScale(1.f)
        , mWorldGrip(nullptr), mPickerMoved(false), mWorldMoved(false), mWorldUserScale(0.f)
        , mGuiLast(false), mPickerReopen(false), mWorldReopen(false)
        , mAnyShops(false), mAnyRaw(false), mScanTimer(sScanInterval), mLastCell(nullptr), mRingPending(false)
    {
        mEnabled   = settingBool("enabled", true);
        mInfoWidth = std::max(200, settingInt("info width", 460));
        mShowShops = settingBool("show shops", false);
        mGridBig = settingBool("grid big", false);
        {
            std::string mode = "dynamic";
            try { mode = Misc::StringUtils::lowerCase(Settings::Manager::getString("plants mode", "Ingredients")); } catch (...) {}
            mPlantsMode = mode == "off" ? Plants_Off : mode == "static" ? Plants_Static : Plants_Dynamic;
        }
        mHereStanding = mHerePlaced = 0;
        for (int i = 0; i < 25; ++i) mGridLive[i] = false;
        mDotsVersion = 1;
        mPickerPos = MyGUI::IntPoint(settingInt("picker x", -1), settingInt("picker y", -1));
        mPickerMoved = mPickerPos.left >= 0 && mPickerPos.top >= 0;
        mWorldPos = MyGUI::IntPoint(settingInt("world map x", -1), settingInt("world map y", -1));
        mWorldMoved = mWorldPos.left >= 0 && mWorldPos.top >= 0;
        mWorldUserScale = settingFloat("world map scale", 0.f);
        if (mWorldUserScale > 0.f)
            mWorldUserScale = std::max(0.3f, std::min(1.6f, mWorldUserScale));
        mShowRaw   = settingBool("show raw", false);

        mNormalColour = MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal}"));
        mIconRow = nullptr;
        for (int i = 0; i < 3; ++i) { mGridIcon[i] = nullptr; mGridQty[i] = nullptr; }
        for (int i = 0; i < 25; ++i) { mGridBg[i] = nullptr; mGridText[i] = nullptr; mGridCoin[i] = nullptr; }
        sInstance = this;
        buildCatalogue();
        loadTracked();

        MyGUI::Gui& gui = MyGUI::Gui::getInstance();

        // mortar and pestle: always shown; opens the picker
        mTrackIcon = gui.createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, 0, sIcon, sIcon), MyGUI::Align::Default, "Menu");
        mTrackIcon->setImageTexture("icons\\m\\tx_mortarpestle_m_01.dds");
        mTrackIcon->setNeedMouseFocus(true);
        mTrackIcon->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onTrackClicked);
        mTrackIcon->setUserString("ToolTipType", "Layout");
        mTrackIcon->setUserString("ToolTipLayout", "TextToolTip");
        mTrackIcon->setUserString("Caption_Text", "Choose which ingredients to track");
        mTrackIcon->setVisible(false);

        // the picker: a box in the Windows layer (with the game's own windows, so it can sit on top of them) with a
        // persistent search box; everything else is rebuilt on change
        const int pickerW = 2 * sPad + sCols * sTile + (sCols - 1) * sTileGap;
        mPicker = gui.createWidget<MyGUI::Widget>("HUD_Box_NoTransp", MyGUI::IntCoord(0, 0, pickerW, 100), MyGUI::Align::Default, "Windows");
        mPicker->setNeedMouseFocus(true);
        mPicker->eventMouseButtonPressed += MyGUI::newDelegate(this, &Ingredients::onFramePressed);
        mPicker->eventMouseDrag += MyGUI::newDelegate(this, &Ingredients::onFrameDragged);
        mSearchEdit = mPicker->createWidget<MyGUI::EditBox>("MW_TextEdit", MyGUI::IntCoord(sPad, 0, pickerW - 2 * sPad, sSearchH), MyGUI::Align::Default);
        mSearchEdit->setNeedKeyFocus(true);
        mSearchEdit->setNeedMouseFocus(true);
        mSearchEdit->eventEditTextChange += MyGUI::newDelegate(this, &Ingredients::onSearchChanged);
        mPickerClose = mPicker->createWidget<MyGUI::Button>("MW_Button", MyGUI::IntCoord(pickerW - sPad - 56, sPad, 56, 20), MyGUI::Align::Default);
        mPickerClose->setCaption("Close");
        mPickerClose->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onTrackClicked);   // toggles it shut
        mPickerMap = mPicker->createWidget<MyGUI::Button>("MW_Button", MyGUI::IntCoord(pickerW - sPad - 56 - 6 - 48, sPad, 48, 20), MyGUI::Align::Default);
        mPickerMap->setCaption("Map");
        mPickerMap->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onMapClicked);
        mPicker->setVisible(false);

        // icons: own Menu-layer roots (clickable in menus)
        mShopsIcon = gui.createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, 0, sIcon, sIcon), MyGUI::Align::Default, "Menu");
        mShopsIcon->setImageTexture("icons\\m\\tx_gold_001.dds");   // a gold coin: "someone here sells it"
        mShopsIcon->setNeedMouseFocus(true);
        mShopsIcon->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onShopsClicked);
        mShopsIcon->setUserString("ToolTipType", "Layout");
        mShopsIcon->setUserString("ToolTipLayout", "TextToolTip");
        mShopsIcon->setUserString("Caption_Text", "Shopkeepers here sell ingredients on the list (click to show / hide)");
        mShopsIcon->setVisible(false);

        mRawIcon = gui.createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, 0, sIcon, sIcon), MyGUI::Align::Default, "Menu");
        mRawIcon->setImageTexture("icons\\k\\magic_alchemy.dds");   // the skill icon, loaded as the stats window does
        mRawIcon->setNeedMouseFocus(true);
        mRawIcon->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onRawClicked);
        mRawIcon->setUserString("ToolTipType", "Layout");
        mRawIcon->setUserString("ToolTipLayout", "TextToolTip");
        mRawIcon->setUserString("Caption_Text", "Tracked ingredients grow near here (click for a 3x3 map of the counts)");
        mRawIcon->setVisible(false);

        // the 3x3 map: dark cells with the count, north up, compass in the middle cell, coin = shop; the
        // tracked ingredients' icons in a row on its right, on the icon line (bottom-aligned with the map)
        const int gridSize = 3 * sCell + 2 * sCellGap;
        mGrid = gui.createWidget<MyGUI::Widget>("", MyGUI::IntCoord(0, 0, gridSize, gridSize), MyGUI::Align::Default, "Menu");
        mGrid->setNeedMouseFocus(true);
        mGrid->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onGridClicked);
        mGrid->setUserString("ToolTipType", "Layout");
        mGrid->setUserString("ToolTipLayout", "TextToolTip");
        mGrid->setUserString("Caption_Text", "Tracked ingredients in the cells around you, north up; a coin marks a shop\nClick: 3x3, then 5x5, then folded");
        for (int i = 0; i < 25; ++i)
        {
            const int cx = (i % 5) * (sCell + sCellGap), cy = (i / 5) * (sCell + sCellGap);
            mGridBg[i] = mGrid->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(cx, cy, sCell, sCell), MyGUI::Align::Default);
            mGridBg[i]->setImageTexture("white");
            mGridBg[i]->setColour(i == 12 ? MyGUI::Colour(0.35f, 0.28f, 0.12f) : MyGUI::Colour(0.f, 0.f, 0.f));
            mGridBg[i]->setAlpha(0.55f);
            // each cell carries the big map's tooltip for that cell; a click still steps the grid
            mGridBg[i]->setNeedMouseFocus(true);
            mGridBg[i]->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onGridClicked);
            mGridBg[i]->setUserString("ToolTipType", "Layout");
            mGridBg[i]->setUserString("ToolTipLayout", "MajereCellToolTip");
            mGridBg[i]->setUserString("Caption_CellTitle", "");
            mGridBg[i]->setUserString("Caption_CellBody", "");
            mGridCount[i] = 0;
            mGridLoaded[i] = false;
            mGridShop[i] = false;
        }
        // the facing arrow: a small gold arrow head at the top of a transparent cell-sized image, rotated about the
        // cell's centre by the RotatingSkin, so it rides the rim of the middle cell and never covers the count
        mCompass = mGrid->createWidget<MyGUI::ImageBox>("RotatingSkin",
            MyGUI::IntCoord(sCell + sCellGap + sCell / 2 - sArrowBox / 2, sCell + sCellGap + sCell / 2 - sArrowBox / 2, sArrowBox, sArrowBox), MyGUI::Align::Default);
        mCompass->setAlpha(0.9f);
        mCompass->setImageTexture("textures\\majere_maparrow.png");
        mCompass->setNeedMouseFocus(false);
        for (int i = 0; i < 25; ++i)
        {
            const int cx = (i % 5) * (sCell + sCellGap), cy = (i / 5) * (sCell + sCellGap);
            mGridText[i] = mGrid->createWidget<MyGUI::TextBox>("SandBrightText", MyGUI::IntCoord(cx, cy, sCell, sCell), MyGUI::Align::Default);
            mGridText[i]->setTextAlign(MyGUI::Align::Center);
            mGridText[i]->setTextShadow(true);
            mGridText[i]->setNeedMouseFocus(false);
            mGridText[i]->setCaption("-");
            mGridCoin[i] = mGrid->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(cx + sCell - sCoin - 1, cy + 1, sCoin, sCoin), MyGUI::Align::Default);
            mGridCoin[i]->setImageTexture("icons\\m\\tx_gold_001.dds");
            mGridCoin[i]->setNeedMouseFocus(false);
            mGridCoin[i]->setVisible(false);
        }
        layoutGrid();
        // the tracked icons: their own root (nothing of the grid's click area may sit over the Map button)
        mIconRow = gui.createWidget<MyGUI::Widget>("", MyGUI::IntCoord(0, 0, 3 * sIcon + 2 * sGridIconGap, sIcon), MyGUI::Align::Default, "Menu");
        // the row itself takes the click too (a click anywhere on it opens the picker): a pick through a
        // root that wants no mouse focus proved unreliable
        mIconRow->setNeedMouseFocus(true);
        mIconRow->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onIconRowClicked);
        mIconRow->setVisible(false);
        for (int i = 0; i < 3; ++i)
        {
            Look none;
            none.colour = mNormalColour;
            mGridIcon[i] = makeTile(mIconRow, i * (sIcon + sGridIconGap), 0, sIcon, none, true);   // plain vanilla icon
            mGridIcon[i]->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onIconRowClicked);   // folds / unfolds the map
            mGridIcon[i]->setVisible(false);
            mGridQty[i] = mGridIcon[i]->createWidget<MyGUI::TextBox>("SandBrightText", MyGUI::IntCoord(0, sIcon - 16, sIcon - 1, 16), MyGUI::Align::Default);
            mGridQty[i]->setTextAlign(MyGUI::Align::Right | MyGUI::Align::Bottom);
            mGridQty[i]->setTextShadow(true);
            mGridQty[i]->setTextColour(MyGUI::Colour(0.95f, 0.85f, 0.45f));
            mGridQty[i]->setNeedMouseFocus(false);
            mGridQty[i]->setCaption("");
        }
        mGrid->setVisible(false);

        // "Map" under the 3x3 map: the whole world with per-cell counts of the tracked plants
        mMapButton = gui.createWidget<MyGUI::Button>("MW_Button", MyGUI::IntCoord(0, 0, gridSize, 24), MyGUI::Align::Default, "Menu");
        mMapButton->setCaption("Map");
        mMapButton->setNeedMouseFocus(true);
        mMapButton->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onMapClicked);
        mMapButton->setUserString("ToolTipType", "Layout");
        mMapButton->setUserString("ToolTipLayout", "TextToolTip");
        mMapButton->setUserString("Caption_Text", "The whole world map with the number of tracked plants in every cell (its Close button shuts it)\nA click on the grid above steps it: 3x3, 5x5, folded");
        mMapButton->setVisible(false);
        layoutGrid();   // the button's width follows the grid's view

        mWorldMap = gui.createWidget<MyGUI::Widget>("HUD_Box_NoTransp", MyGUI::IntCoord(0, 0, 100, 100), MyGUI::Align::Default, "Windows");
        mWorldMap->setNeedMouseFocus(true);
        mWorldMap->eventMouseButtonPressed += MyGUI::newDelegate(this, &Ingredients::onFramePressed);
        mWorldMap->eventMouseDrag += MyGUI::newDelegate(this, &Ingredients::onFrameDragged);
        mWorldImage = mWorldMap->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(sWorldPad, sWorldPad, 10, 10), MyGUI::Align::Default);
        mWorldImage->setNeedMouseFocus(false);
        mWorldStatus = mWorldMap->createWidget<MyGUI::TextBox>("SandBrightText", MyGUI::IntCoord(sWorldPad, 0, 10, 20), MyGUI::Align::Default);
        mWorldStatus->setNeedMouseFocus(true);   // the title bar drags the map
        mWorldStatus->eventMouseButtonPressed += MyGUI::newDelegate(this, &Ingredients::onFramePressed);
        mWorldStatus->eventMouseDrag += MyGUI::newDelegate(this, &Ingredients::onFrameDragged);
        mWorldStatus->setTextColour(sGold);
        mWorldClose = mWorldMap->createWidget<MyGUI::Button>("MW_Button", MyGUI::IntCoord(0, 0, 56, 20), MyGUI::Align::Default);
        mWorldClose->setCaption("Close");
        mWorldClose->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onWorldCloseClicked);
        mWorldPick = mWorldMap->createWidget<MyGUI::Button>("MW_Button", MyGUI::IntCoord(0, 0, 72, 20), MyGUI::Align::Default);
        mWorldPick->setCaption("Track");
        mWorldPick->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onTrackClicked);
        mWorldGrip = mWorldMap->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, 0, 14, 14), MyGUI::Align::Default);
        mWorldGrip->castType<MyGUI::ImageBox>()->setImageTexture("white");
        mWorldGrip->setColour(sGold);
        mWorldGrip->setAlpha(0.75f);
        mWorldGrip->setNeedMouseFocus(true);
        mWorldGrip->setUserString("ToolTipType", "Layout");
        mWorldGrip->setUserString("ToolTipLayout", "TextToolTip");
        mWorldGrip->setUserString("Caption_Text", "Drag to resize the map; drag the frame to move it");
        mWorldGrip->eventMouseButtonPressed += MyGUI::newDelegate(this, &Ingredients::onGripPressed);
        mWorldGrip->eventMouseDrag += MyGUI::newDelegate(this, &Ingredients::onGripDragged);
        mWorldMap->setVisible(false);
        rebuildTable();

        // the shops block (HUD layer: never needs the mouse)
        mInfo = gui.createWidget<MyGUI::EditBox>("SandText", MyGUI::IntCoord(0, 0, mInfoWidth, 20), MyGUI::Align::Default, "HUD");
        mInfo->setEditStatic(true);
        mInfo->setEditMultiLine(true);
        mInfo->setEditWordWrap(true);
        mInfo->setNeedKeyFocus(false);
        mInfo->setNeedMouseFocus(false);
        mInfo->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Bottom);
        mInfo->setVisible(false);
        place();
    }

    Ingredients::~Ingredients()
    {
        sInstance = nullptr;
        MyGUI::Gui& gui = MyGUI::Gui::getInstance();
        if (mInfo) gui.destroyWidget(mInfo);
        if (mGrid) gui.destroyWidget(mGrid);
        if (mIconRow) gui.destroyWidget(mIconRow);
        if (mMapButton) gui.destroyWidget(mMapButton);
        if (mWorldMap) gui.destroyWidget(mWorldMap);
        if (mPicker) gui.destroyWidget(mPicker);   // takes the search box and the tiles with it
        if (mTrackIcon) gui.destroyWidget(mTrackIcon);
        if (mShopsIcon) gui.destroyWidget(mShopsIcon);
        if (mRawIcon) gui.destroyWidget(mRawIcon);
    }

    void Ingredients::place()
    {
        // icons from the hotbar's anchor (right of its page label), vertically centred on it; the panel above
        // the first icon, bottom-anchored so it only ever grows upward
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        MyGUI::IntCoord anchor(view.width / 2 + 300, view.height - 40, 0, 24);
        if (mHotbar)
            anchor = mHotbar->getIconAnchor();
        const int y = anchor.top + anchor.height / 2 - sIcon / 2;
        int x = anchor.right();
        mTrackIcon->setCoord(x, y, sIcon, sIcon);
        x += sIcon + sIconGap;
        mShopsIcon->setCoord(x, y, sIcon, sIcon);
        if (mShopsIcon->getVisible())
            x += sIcon + sIconGap;
        mRawIcon->setCoord(x, y, sIcon, sIcon);
        // the 3x3 map sits where the icon is, bottom-left aligned to it; the Map button just under it
        // the Map button (as wide as the cells) under the grid, kept on screen; the cells move up to leave it
        // room while the tracked icons stay level with the mortar icon
        const int gridSize = gridPixels();
        mMapButton->setPosition(x, std::min(y + sIcon + 2, view.height - mMapButton->getHeight() - 2));
        mGrid->setPosition(x, mMapButton->getTop() - 2 - gridSize);
        // the tracked icons: right of the grid when it is up, right of the alchemy icon when it is folded
        mIconRow->setPosition(x + (mGrid->getVisible() ? gridSize : sIcon) + sIconGap, y);

        // the picker above the mortar icon, kept on screen -- unless the user dragged it somewhere
        int px = mPickerMoved ? mPickerPos.left : mTrackIcon->getLeft();
        int py = mPickerMoved ? mPickerPos.top : y - 4 - mPicker->getHeight();
        px = std::max(0, std::min(px, view.width - mPicker->getWidth()));
        py = std::max(0, std::min(py, view.height - 24));
        mPicker->setPosition(px, py);
        // the shops block sits above the icons (above the open map if it is up); right of the picker when open
        const int panelX = mPicker->getVisible() ? mPicker->getRight() + 12 : mShopsIcon->getLeft();
        int bottom = y - 6;
        if (mGrid->getVisible())
            bottom = std::min(bottom, mGrid->getTop() - 6);   // (the cells start at the root's top)
        if (mInfo->getVisible())
        {
            const int hs = std::max(20, mInfo->getTextSize().height + 4);
            mInfo->setCoord(panelX, bottom - hs, mInfoWidth, hs);
        }
    }

    void Ingredients::setVisible(bool visible)
    {
        mHudVisible = visible && mEnabled;
        if (!mHudVisible)
        {
            mTrackIcon->setVisible(false);
            mShopsIcon->setVisible(false);
            mRawIcon->setVisible(false);
            mInfo->setVisible(false);
            mGrid->setVisible(false);
            mIconRow->setVisible(false);
            mMapButton->setVisible(false);
            mWorldMap->setVisible(false);
            mPicker->setVisible(false);
        }
    }

    // ---------------------------------------------------------------- tracked set

    Ingredients* Ingredients::sInstance = nullptr;

    const std::vector<Ingredients::PlantDot>& Ingredients::plantDots()
    {
        static const std::vector<PlantDot> none;
        return (sInstance && sInstance->mPlantsMode != Plants_Off) ? sInstance->mDots : none;
    }

    unsigned int Ingredients::plantDotsVersion()
    {
        return sInstance ? sInstance->mDotsVersion : 0;
    }

    int Ingredients::plantsMode()
    {
        return sInstance ? sInstance->mPlantsMode : Plants_Off;
    }

    std::string Ingredients::plantsModeLabel()
    {
        const int mode = plantsMode();
        return mode == Plants_Dynamic ? "Plants: dynamic" : mode == Plants_Static ? "Plants: static" : "Plants: off";
    }

    void Ingredients::cyclePlantsMode()
    {
        if (!sInstance)
            return;
        int& mode = sInstance->mPlantsMode;
        mode = mode == Plants_Dynamic ? Plants_Static : mode == Plants_Static ? Plants_Off : Plants_Dynamic;
        Settings::Manager::setString("plants mode", "Ingredients",
            mode == Plants_Dynamic ? "dynamic" : mode == Plants_Static ? "static" : "off");
        ++sInstance->mDotsVersion;
        sInstance->mScanTimer = sScanInterval;   // dots and counts follow at once
    }

    std::vector<std::string> Ingredients::shopLines(const std::string& interiorCell)
    {
        std::vector<std::string> lines;
        if (!sInstance || interiorCell.empty())
            return lines;
        const std::vector<IngredientDef>& table = sInstance->mTable;
        for (size_t i = 0; i < table.size(); ++i)
        {
            if (i < sInstance->mTracked.size() && !sInstance->mTracked[i])
                continue;
            for (const IngredientSeller& s : table[i].sellers)
            {
                if (!Misc::StringUtils::ciEqual(s.cell, interiorCell))
                    continue;
                const int stock = sInstance->keeperStock(s.npc, table[i].ids);
                lines.push_back(table[i].name + (stock > 0 ? " (" + std::to_string(stock) + ")" : "") + ": " + s.npc);
            }
        }
        return lines;
    }

    const std::string& Ingredients::trackedSignature()
    {
        static const std::string none;
        return sInstance ? sInstance->mWorldSignature : none;
    }

    bool Ingredients::isTracked(const std::string& id) const
    {
        return std::find(mTrackedIds.begin(), mTrackedIds.end(), id) != mTrackedIds.end();
    }

    void Ingredients::loadTracked()
    {
        const MWWorld::Store<ESM::Ingredient>& store = MWBase::Environment::get().getWorld()->getStore().get<ESM::Ingredient>();
        mTrackedIds.clear();
        // "all" / "none" / a comma list of seller-table names (the old form) or record ids
        const std::string list = settingString("tracked", "all");
        auto addId = [&](const std::string& raw) {
            const std::string id = Misc::StringUtils::lowerCase(raw);
            if (!isTracked(id) && store.search(id))
                mTrackedIds.push_back(id);
        };
        if (list == "all" || list.empty())
        {
            // the first three seller-table entries (one record each: a seller-table entry covers all its records anyway)
            for (const IngredientDef& def : ingredientTable())
                if (mTrackedIds.size() < sMaxTracked && !def.ids.empty())
                    addId(def.ids.front());
        }
        else if (list != "none")
        {
            for (const std::string& tok : splitList(list))
            {
                bool named = false;
                for (const IngredientDef& def : ingredientTable())
                    if (Misc::StringUtils::ciEqual(def.name, tok))
                    {
                        for (const std::string& id : def.ids) addId(id);
                        named = true;
                    }
                if (!named)
                    addId(tok);
            }
        }
        if (mTrackedIds.size() > sMaxTracked)
            mTrackedIds.resize(sMaxTracked);
        mRecentIds.clear();
        for (const std::string& tok : splitList(settingString("recents", "")))
        {
            const std::string id = Misc::StringUtils::lowerCase(tok);
            if (store.search(id) && std::find(mRecentIds.begin(), mRecentIds.end(), id) == mRecentIds.end())
                mRecentIds.push_back(id);
        }
    }

    void Ingredients::saveTracked() const
    {
        Settings::Manager::setString("tracked", "Ingredients", mTrackedIds.empty() ? "none" : joinList(mTrackedIds));
        Settings::Manager::setString("recents", "Ingredients", joinList(mRecentIds));
    }

    void Ingredients::rebuildTable()
    {
        const MWWorld::Store<ESM::Ingredient>& store = MWBase::Environment::get().getWorld()->getStore().get<ESM::Ingredient>();
        mTable.clear();
        for (const std::string& id : mTrackedIds)
        {
            if (const IngredientDef* def = sellerTableEntry(id))
            {
                bool have = false;
                for (const IngredientDef& t : mTable)
                    if (t.name == def->name) { have = true; break; }
                if (!have)
                    mTable.push_back(*def);
                continue;
            }
            const ESM::Ingredient* rec = store.search(id);
            if (!rec)
                continue;
            IngredientDef def;
            def.name = rec->mName;
            def.group = 2;
            const CatalogueEntry* entry = catalogueEntry(id);
            if (entry && !entry->ids.empty())
                def.ids = entry->ids;   // every record of that name
            else
                def.ids.push_back(id);
            mTable.push_back(def);
        }
        mTracked.assign(mTable.size(), true);
        mFound.assign(mTable.size(), Found());
        {
            std::string sig;
            for (const IngredientDef& def : mTable) for (const std::string& id : def.ids) sig += id + ";";
            if (sig != mWorldSignature) { mRingCache.clear(); mWorldSignature = sig; mWorldCounts.clear(); mWorldShops.clear(); mWorldBreakdown.clear(); mWorldInside.clear(); mWorldInteriors.clear(); mWorldScanning = false; if (mWorldMap && mWorldMap->getVisible()) openWorldMap(); }
        }
        mTableLook.clear();
        for (const IngredientDef& def : mTable)
        {
            const ESM::Ingredient* rec = def.ids.empty() ? nullptr : store.search(def.ids.front());
            Look look;
            look.colour = mNormalColour;
            if (rec)
                look = lookOf(*rec);
            mTableLook.push_back(look);
        }
        // the icon row beside the map
        for (size_t i = 0; i < 3; ++i)
        {
            if (!mGridIcon[i])
                continue;
            const bool have = i < mTableLook.size();
            mGridIcon[i]->setVisible(have);
            if (!have)
                continue;
            const Look& look = mTableLook[i];
            mGridIcon[i]->setUserString("Caption_Text", look.tooltip);
            MyGUI::ImageBox* icon = mGridIcon[i]->getChildAt(0)->castType<MyGUI::ImageBox>(false);
            if (icon)
                icon->setImageTexture(look.icon);
        }
    }

    Ingredients::Look Ingredients::lookOf(const ESM::Ingredient& rec) const
    {
        static const std::string cNormal = hex(MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal}")));
        Look look;
        look.icon = MWBase::Environment::get().getWindowManager()->correctIconPath(rec.mIcon);
        const std::vector<Category> cats = categoriesOf(rec);
        look.colour = cats.empty() ? mNormalColour : categoryColour(cats[0], mNormalColour);
        look.colour2 = cats.size() > 1 ? categoryColour(cats[1], mNormalColour) : look.colour;
        look.tooltip = hex(look.colour) + rec.mName + cNormal;
        for (int i = 0; i < 4; ++i)
        {
            const int id = rec.mData.mEffectID[i];
            if (id < 0)
                continue;
            const std::string fx = effectName(id, rec.mData.mAttributes[i], rec.mData.mSkills[i]);
            if (fx.empty())
                continue;
            // each effect line in its own category's colour
            const Category c = categoryOf(id);
            look.tooltip += "\n  " + (c == Cat_None ? cNormal : hex(categoryColour(c, mNormalColour))) + fx + cNormal;
        }
        return look;
    }

    // a tile: a rim in the category colour (dim and neutral when there is none) with the icon inset; with two
    // categories the rim is split along the diagonal, the leading colour top-left, the second bottom-right
    MyGUI::ImageBox* Ingredients::makeTile(MyGUI::Widget* parent, int x, int y, int size, const Look& look, bool plain)
    {
        MyGUI::ImageBox* back = parent->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(x, y, size, size), MyGUI::Align::Default);
        if (!plain)   // (alpha 0 would cascade to the icon child, so a plain tile simply draws no backing)
        {
            back->setImageTexture("white");
            back->setColour(look.colour);
            back->setAlpha(look.colour == mNormalColour ? 0.35f : 0.8f);
        }
        back->setNeedMouseFocus(true);
        back->setUserString("ToolTipType", "Layout");
        back->setUserString("ToolTipLayout", "TextToolTip");
        back->setUserString("Caption_Text", look.tooltip);
        if (!plain && !(look.colour2 == look.colour))
        {
            MyGUI::ImageBox* half = back->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, 0, size, size), MyGUI::Align::Default);
            half->setImageTexture("textures\\majere_tri.png");   // lower-right triangle
            half->setColour(look.colour2);
            half->setNeedMouseFocus(false);
        }
        const int inset = plain ? 0 : size >= 30 ? 3 : 2;
        MyGUI::ImageBox* icon = back->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(inset, inset, size - 2 * inset, size - 2 * inset), MyGUI::Align::Default);
        icon->setImageTexture(look.icon);
        icon->setNeedMouseFocus(false);
        return back;
    }

    // ---------------------------------------------------------------- the picker

    void Ingredients::buildCatalogue()
    {
        if (!mCatalogue.empty())
            return;
        const MWWorld::Store<ESM::Ingredient>& store = MWBase::Environment::get().getWorld()->getStore().get<ESM::Ingredient>();
        for (const ESM::Ingredient& rec : store)
        {
            if (rec.mName.empty())
                continue;
            const std::string id = Misc::StringUtils::lowerCase(rec.mId);
            // the Daedric shrines' cursed copies share the real ingredient's name; they are never worth tracking
            if (id.find("cursed") != std::string::npos)
                continue;
            // a second record with the same name (expansions and mods repeat names) joins the first entry
            bool merged = false;
            for (CatalogueEntry& other : mCatalogue)
                if (Misc::StringUtils::ciEqual(other.name, rec.mName)) { other.ids.push_back(id); merged = true; break; }
            if (merged)
                continue;
            CatalogueEntry e;
            e.id = id;
            e.ids.push_back(id);
            e.name = rec.mName;
            const Look look = lookOf(rec);
            e.icon = look.icon;
            e.colour = look.colour;
            e.colour2 = look.colour2;
            e.tooltip = look.tooltip;
            e.searchText = Misc::StringUtils::lowerCase(rec.mName);
            for (int i = 0; i < 4; ++i)
            {
                if (rec.mData.mEffectID[i] < 0)
                    continue;
                const std::string fx = effectName(rec.mData.mEffectID[i], rec.mData.mAttributes[i], rec.mData.mSkills[i]);
                if (!fx.empty())
                    e.searchText += "|" + Misc::StringUtils::lowerCase(fx);
            }
            mCatalogue.push_back(e);
        }
        std::sort(mCatalogue.begin(), mCatalogue.end(),
            [](const CatalogueEntry& a, const CatalogueEntry& b) { return Misc::StringUtils::ciLess(a.name, b.name); });
    }

    const Ingredients::CatalogueEntry* Ingredients::catalogueEntry(const std::string& id) const
    {
        for (const CatalogueEntry& e : mCatalogue)
            for (const std::string& x : e.ids)
                if (x == id)
                    return &e;
        return nullptr;
    }

    MyGUI::Widget* Ingredients::addTile(const CatalogueEntry& e, int x, int y, bool starred)
    {
        Look look;
        look.icon = e.icon;
        look.colour = e.colour;
        look.colour2 = e.colour2;
        look.tooltip = e.tooltip;
        MyGUI::ImageBox* back = makeTile(mPicker, x, y, sTile, look, true);   // no colour backing (the tooltip keeps the coloured effects)
        back->setUserString("Id", e.id);
        back->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onTileClicked);
        if (starred)
        {
            MyGUI::TextBox* star = back->createWidget<MyGUI::TextBox>("SandBrightText", MyGUI::IntCoord(1, -3, 14, 16), MyGUI::Align::Default);
            star->setCaption("*");
            star->setTextColour(sGold);
            star->setTextShadow(true);
            star->setNeedMouseFocus(false);
        }
        const int carried = inventoryCount(e.ids);
        if (carried > 0)
        {
            MyGUI::TextBox* qty = back->createWidget<MyGUI::TextBox>("SandBrightText", MyGUI::IntCoord(0, sTile - 16, sTile - 1, 16), MyGUI::Align::Default);
            qty->setCaption(std::to_string(carried));
            qty->setTextAlign(MyGUI::Align::Right | MyGUI::Align::Bottom);
            qty->setTextShadow(true);
            qty->setTextColour(MyGUI::Colour(0.95f, 0.85f, 0.45f));
            qty->setNeedMouseFocus(false);
        }
        mPickerWidgets.push_back(back);
        return back;
    }

    void Ingredients::rebuildPicker()
    {
        MyGUI::Gui& gui = MyGUI::Gui::getInstance();
        for (MyGUI::Widget* w : mPickerWidgets)
            gui.destroyWidget(w);
        mPickerWidgets.clear();
        buildCatalogue();

        const int innerW = mPicker->getWidth() - 2 * sPad;
        int y = sPad;
        auto header = [&](const std::string& text) {
            // the first header shares its row with the Map and Close buttons
            MyGUI::TextBox* t = mPicker->createWidget<MyGUI::TextBox>("SandBrightText", MyGUI::IntCoord(sPad, y, y == sPad ? innerW - 120 : innerW, sHeaderH), MyGUI::Align::Default);
            t->setCaption(text);
            t->setTextColour(sGold);
            t->setNeedMouseFocus(true);   // a header drags the picker
            t->eventMouseButtonPressed += MyGUI::newDelegate(this, &Ingredients::onFramePressed);
            t->eventMouseDrag += MyGUI::newDelegate(this, &Ingredients::onFrameDragged);
            mPickerWidgets.push_back(t);
            y += sHeaderH + 2;
        };
        auto tiles = [&](const std::vector<std::string>& ids, bool starred) {
            int col = 0;
            for (const std::string& id : ids)
            {
                const CatalogueEntry* e = catalogueEntry(id);
                if (!e)
                    continue;
                addTile(*e, sPad + col * (sTile + sTileGap), y, starred);
                if (++col == sCols) { col = 0; y += sTile + sTileGap; }
            }
            if (col > 0) y += sTile + sTileGap;
        };
        auto rule = [&]() {
            y += 4;
            MyGUI::ImageBox* r = mPicker->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(sPad, y, innerW, 1), MyGUI::Align::Default);
            r->setImageTexture("white");
            r->setColour(sGold);
            r->setAlpha(0.7f);
            r->setNeedMouseFocus(false);
            mPickerWidgets.push_back(r);
            y += 8;
        };

        // tracked, starred (click removes)
        header(mTrackedIds.empty() ? "Tracked: none yet (three at most)"
             : mTrackedIds.size() >= sMaxTracked ? "Tracked (three at most: a new pick replaces the oldest)"
             : "Tracked (click to remove; three at most)");
        tiles(mTrackedIds, true);
        rule();

        // recent picks not tracked right now (click adds back)
        std::vector<std::string> recent;
        for (const std::string& id : mRecentIds)
            if (!isTracked(id))
                recent.push_back(id);
        if (!recent.empty())
        {
            header("Recent (click to add)");
            tiles(recent, false);
        }

        // the search box, then what matches: every word typed has to appear in the name or an effect
        header("Search by name or effect, hover for the effects:");
        mSearchEdit->setPosition(sPad, y);
        y += sSearchH + 6;
        // the typed text is matched as ONE phrase against the name and against each effect on its own:
        // "resist fire" finds Resist Fire, not an ingredient that has some Resist and some Fire effect
        std::string phrase = Misc::StringUtils::lowerCase(mSearchEdit->getCaption());
        {
            std::string packed;
            for (char ch : phrase)
            {
                if (ch == '|') continue;                       // the field separator is not searchable
                if (ch == ' ' && (packed.empty() || packed.back() == ' ')) continue;
                packed += ch;
            }
            while (!packed.empty() && packed.back() == ' ') packed.pop_back();
            phrase.swap(packed);
        }
        std::vector<std::string> hits;
        int more = 0;
        for (const CatalogueEntry& e : mCatalogue)
        {
            if (!phrase.empty() && e.searchText.find(phrase) == std::string::npos)
                continue;
            if (static_cast<int>(hits.size()) >= sMaxResults) { ++more; continue; }
            hits.push_back(e.id);
        }
        {
            int col = 0;
            for (const std::string& id : hits)
            {
                addTile(*catalogueEntry(id), sPad + col * (sTile + sTileGap), y, isTracked(id));
                if (++col == sCols) { col = 0; y += sTile + sTileGap; }
            }
            if (col > 0) y += sTile + sTileGap;
        }
        if (hits.empty())
            header("nothing matches");
        else if (more > 0)
            header("+" + std::to_string(more) + " more: type more of the name or effect");
        mPicker->setSize(mPicker->getWidth(), y + sPad);
        place();
    }

    void Ingredients::onTrackClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
        if (!wm->isGuiMode())
            return;
        const bool open = !mPicker->getVisible();
        mPicker->setVisible(open);
        mPickerReopen = open;
        if (open)
        {
            rebuildPicker();
            MyGUI::LayerManager::getInstance().upLayerItem(mPicker);   // above the game's own windows
            wm->setKeyFocusWidget(mSearchEdit);
        }
        place();
    }

    void Ingredients::onSearchChanged(MyGUI::EditBox* /*sender*/)
    {
        rebuildPicker();
    }

    void Ingredients::onTileClicked(MyGUI::Widget* sender)
    {
        const std::string id = sender->getUserString("Id");
        if (id.empty())
            return;
        std::vector<std::string> touched;   // recent picks: the toggled one, and whatever a fourth pick bumped
        if (isTracked(id))
            mTrackedIds.erase(std::find(mTrackedIds.begin(), mTrackedIds.end(), id));
        else
        {
            while (mTrackedIds.size() >= sMaxTracked)   // three at most: the oldest makes way
            {
                touched.push_back(mTrackedIds.front());
                mTrackedIds.erase(mTrackedIds.begin());
            }
            mTrackedIds.push_back(id);
        }
        // either way it is a recent pick: something taken off the list is the likeliest thing to want back
        touched.insert(touched.begin(), id);
        for (auto it = touched.rbegin(); it != touched.rend(); ++it)
        {
            mRecentIds.erase(std::remove(mRecentIds.begin(), mRecentIds.end(), *it), mRecentIds.end());
            mRecentIds.insert(mRecentIds.begin(), *it);
        }
        if (mRecentIds.size() > sMaxRecents)
            mRecentIds.resize(sMaxRecents);
        saveTracked();
        rebuildTable();
        rebuildPicker();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mSearchEdit);
        mScanTimer = sScanInterval;   // rescan with the new set right away
        mLastShopsText.clear();
    }

    // ---------------------------------------------------------------- inventory count, world map

    int Ingredients::inventoryCount(const IngredientDef& def) const
    {
        return inventoryCount(def.ids);
    }

    int Ingredients::inventoryCount(const std::vector<std::string>& ids) const
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty())
            return 0;
        int n = 0;
        MWWorld::InventoryStore& inv = player.getClass().getInventoryStore(player);
        for (MWWorld::ContainerStoreIterator it = inv.begin(); it != inv.end(); ++it)
        {
            const std::string& id = it->getCellRef().getRefId();
            for (const std::string& want : ids)
                if (Misc::StringUtils::ciEqual(id, want)) { n += it->getRefData().getCount(); break; }
        }
        return n;
    }

    void Ingredients::onMapClicked(MyGUI::Widget* /*sender*/)
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;
        Log(Debug::Info) << "[Ingredients] Map button: world map " << (mWorldMap->getVisible() ? "closing" : "opening");
        if (mWorldMap->getVisible())
        {
            closeWorldMap();
            mWorldReopen = false;
        }
        else
        {
            openWorldMap();
            mWorldReopen = true;
        }
    }

    void Ingredients::onWorldCloseClicked(MyGUI::Widget* /*sender*/)
    {
        closeWorldMap();
        mWorldReopen = false;
    }

    void Ingredients::closeWorldMap()
    {
        mWorldMap->setVisible(false);
    }

    void Ingredients::openWorldMap()
    {
        if (!mMapWindow || !mMapWindow->getGlobalMapRender())
            return;
        MWRender::GlobalMap* gm = mMapWindow->getGlobalMapRender();
        const int w = gm->getWidth(), h = gm->getHeight();
        if (w <= 0 || h <= 0 || !gm->getBaseTexture())
            return;
        if (!mWorldTexture)
        {
            mWorldTexture.reset(new osgMyGUI::OSGTexture(gm->getBaseTexture()));
            mWorldImage->setRenderItemTexture(mWorldTexture.get());
            mWorldImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));
        }
        layoutWorldMap();
        mWorldMap->setVisible(true);
        MyGUI::LayerManager::getInstance().upLayerItem(mWorldMap);
        if (mPicker->getVisible())
            MyGUI::LayerManager::getInstance().upLayerItem(mPicker);
        if (mWorldCounts.empty() && !mWorldScanning)
        {
            // every exterior cell in the game data, counted a few per frame
            mWorldQueue.clear();
            const MWWorld::Store<ESM::Cell>& cells = MWBase::Environment::get().getWorld()->getStore().get<ESM::Cell>();
            for (MWWorld::Store<ESM::Cell>::iterator it = cells.extBegin(); it != cells.extEnd(); ++it)
                mWorldQueue.emplace_back(it->getGridX(), it->getGridY());
            mWorldNext = 0;
            mWorldInteriorQueue.clear();
            for (MWWorld::Store<ESM::Cell>::iterator it = cells.intBegin(); it != cells.intEnd(); ++it)
                mWorldInteriorQueue.push_back(it->mName);
            mWorldInteriorNext = 0;
            mWorldInteriors.clear();
            mWorldInside.clear();
            mWorldScanning = !mWorldQueue.empty();
        }
        rebuildWorldCells();
    }

    // how many of an ingredient the keeper's record carries (vanilla restocking stock is a negative count in
    // the NPC's inventory list; the sign is dropped). Looked up by name, cached.
    int Ingredients::keeperStock(const std::string& npc, const std::vector<std::string>& ids)
    {
        const std::string key = npc + "|" + (ids.empty() ? std::string() : ids.front());
        std::map<std::string, int>::const_iterator hit = mStockCache.find(key);
        if (hit != mStockCache.end())
            return hit->second;
        int n = 0;
        const MWWorld::Store<ESM::NPC>& npcs = MWBase::Environment::get().getWorld()->getStore().get<ESM::NPC>();
        for (const ESM::NPC& rec : npcs)
        {
            if (!Misc::StringUtils::ciEqual(rec.mName, npc))
                continue;
            for (const ESM::ContItem& item : rec.mInventory.mList)
                for (const std::string& id : ids)
                    if (Misc::StringUtils::ciEqual(item.mItem, id)) { n += std::abs(item.mCount); break; }
            break;
        }
        mStockCache[key] = n;
        return n;
    }

    // an exterior cell's name for a tooltip: its own name, else its region's, with the grid coordinates
    std::string Ingredients::worldCellName(int x, int y) const
    {
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        std::string name;
        const ESM::Cell* cell = store.get<ESM::Cell>().search(x, y);
        if (cell)
        {
            name = cell->mName;
            if (name.empty())
            {
                const ESM::Region* region = store.get<ESM::Region>().search(cell->mRegion);
                if (region)
                    name = region->mName;
            }
        }
        if (name.empty())
            name = "Wilderness";
        return name + " (" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }

    void Ingredients::layoutWorldMap()
    {
        if (!mMapWindow || !mMapWindow->getGlobalMapRender())
            return;
        MWRender::GlobalMap* gm = mMapWindow->getGlobalMapRender();
        const int w = gm->getWidth(), h = gm->getHeight();
        if (w <= 0 || h <= 0)
            return;
        // fit: against the top of the screen and never over the mortar icon and hotbar below; or the user's scale
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        const int limit = (mTrackIcon ? mTrackIcon->getTop() : view.height) - 6;
        mWorldScale = 1.f;
        if (mWorldUserScale > 0.f)
            mWorldScale = mWorldUserScale;
        else if (h + 2 * sWorldPad + 22 > limit)
            mWorldScale = std::max(0.25f, static_cast<float>(limit - 2 * sWorldPad - 22) / static_cast<float>(h));
        const int iw = static_cast<int>(w * mWorldScale), ih = static_cast<int>(h * mWorldScale);
        const int W = std::max(iw, 420) + 2 * sWorldPad, Hh = ih + 2 * sWorldPad + 22;
        mWorldStatus->setCoord(sWorldPad, sWorldPad, W - 2 * sWorldPad - 140, 20);
        mWorldPick->setCoord(W - sWorldPad - 56 - 6 - 56, sWorldPad, 56, 20);
        mWorldClose->setCoord(W - sWorldPad - 56, sWorldPad, 56, 20);
        mWorldImage->setCoord((W - iw) / 2, sWorldPad + 22, iw, ih);
        mWorldGrip->setCoord(W - 14, Hh - 14, 14, 14);
        int left = mWorldMoved ? mWorldPos.left : (view.width - W) / 2;
        int top = mWorldMoved ? mWorldPos.top : 0;
        left = std::max(0, std::min(left, view.width - 60));
        top = std::max(0, std::min(top, view.height - 40));
        mWorldMap->setCoord(left, top, W, Hh);
    }

    void Ingredients::onFramePressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        if (id != MyGUI::MouseButton::Left)
            return;
        if (sender != mPicker && sender != mWorldMap)
            sender = sender->getParent();   // a title bar: move the frame it sits in
        mDragOffset = sender->getPosition() - MyGUI::IntPoint(left, top);
    }

    void Ingredients::onFrameDragged(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        if (id != MyGUI::MouseButton::Left)
            return;
        if (sender != mPicker && sender != mWorldMap)
            sender = sender->getParent();
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        MyGUI::IntPoint pos = MyGUI::IntPoint(left, top) + mDragOffset;
        pos.left = std::max(0, std::min(pos.left, view.width - 60));
        pos.top = std::max(0, std::min(pos.top, view.height - 40));
        sender->setPosition(pos);
        if (sender == mPicker)
        {
            mPickerMoved = true;
            mPickerPos = pos;
            Settings::Manager::setInt("picker x", "Ingredients", pos.left);
            Settings::Manager::setInt("picker y", "Ingredients", pos.top);
        }
        else if (sender == mWorldMap)
        {
            mWorldMoved = true;
            mWorldPos = pos;
            Settings::Manager::setInt("world map x", "Ingredients", pos.left);
            Settings::Manager::setInt("world map y", "Ingredients", pos.top);
        }
    }

    void Ingredients::onGripPressed(MyGUI::Widget* /*sender*/, int /*left*/, int /*top*/, MyGUI::MouseButton /*id*/)
    {
    }

    void Ingredients::onGripDragged(MyGUI::Widget* /*sender*/, int left, int /*top*/, MyGUI::MouseButton id)
    {
        if (id != MyGUI::MouseButton::Left || !mMapWindow || !mMapWindow->getGlobalMapRender())
            return;
        // the map's right edge follows the mouse: that width over the texture's is the new scale
        const int w = mMapWindow->getGlobalMapRender()->getWidth();
        const int wanted = left - (mWorldMap->getLeft() + sWorldPad);
        if (w <= 0 || wanted < 40)
            return;
        const float scale = std::max(0.3f, std::min(1.6f, static_cast<float>(wanted) / static_cast<float>(w)));
        if (std::abs(scale - mWorldScale) < 0.01f)
            return;
        mWorldUserScale = scale;
        Settings::Manager::setFloat("world map scale", "Ingredients", scale);
        layoutWorldMap();
        rebuildWorldCells();
    }

    // the big map's arrow: at the player's spot in their cell, turned to the yaw like the grid's compass
    void Ingredients::updateWorldArrow()
    {
        if (!mWorldArrow || !mMapWindow || !mMapWindow->getGlobalMapRender())
            return;
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty() || !player.isInCell() || !player.getCell()->isExterior())
        {
            mWorldArrow->setVisible(false);
            return;
        }
        MWRender::GlobalMap* gm = mMapWindow->getGlobalMapRender();
        const ESM::Position& pp = player.getRefData().getPosition();
        const float units = static_cast<float>(Constants::CellSizeInUnits);
        const float fx = (pp.pos[0] - std::floor(pp.pos[0] / units) * units) / units;
        const float fy = (pp.pos[1] - std::floor(pp.pos[1] / units) * units) / units;
        float ix = 0.f, iy = 0.f;
        gm->cellTopLeftCornerToImageSpace(player.getCell()->getCell()->getGridX(), player.getCell()->getCell()->getGridY(), ix, iy);
        const float cs = gm->getCellSize() * mWorldScale;
        const int px = mWorldImage->getLeft() + static_cast<int>(ix * mWorldImage->getWidth() + fx * cs);
        const int py = mWorldImage->getTop() + static_cast<int>(iy * mWorldImage->getHeight() + (1.f - fy) * cs);
        mWorldArrow->setVisible(true);
        mWorldArrow->setPosition(px - sArrowBox / 2, py - sArrowBox / 2);
        MyGUI::ISubWidget* main = mWorldArrow->getSubWidgetMain();
        MyGUI::RotatingSkin* rot = main ? main->castType<MyGUI::RotatingSkin>(false) : nullptr;
        if (rot)
        {
            rot->setCenter(MyGUI::IntPoint(sArrowBox / 2, sArrowBox / 2));
            rot->setAngle(pp.rot[2]);
        }
    }

    // every interior with a find is credited to the exterior cell its way out opens onto: its own door out,
    // or the first one reached through the interiors it connects to (a cave's lower level exits through the
    // upper one). Interiors with no way out at all are left off the map.
    void Ingredients::creditInteriors()
    {
        for (const auto& entry : mWorldInteriors)
        {
            const InteriorFind& f = entry.second;
            int total = 0;
            for (int n : f.raw) total += n;
            if (total <= 0)
                continue;
            std::vector<std::string> frontier(1, entry.first);
            std::set<std::string> seen(frontier.begin(), frontier.end());
            bool credited = false;
            for (int depth = 0; depth < 8 && !frontier.empty() && !credited; ++depth)
            {
                std::vector<std::string> next;
                for (const std::string& name : frontier)
                {
                    std::map<std::string, InteriorFind>::const_iterator it = mWorldInteriors.find(name);
                    if (it == mWorldInteriors.end())
                        continue;
                    if (!it->second.exitsExterior.empty())
                    {
                        mWorldInside[it->second.exitsExterior.front()].emplace_back(f.name, f.raw);
                        credited = true;
                        break;
                    }
                    for (const std::string& n : it->second.exitsInterior)
                        if (seen.insert(n).second) next.push_back(n);
                }
                frontier.swap(next);
            }
        }
        // fold them into the map's totals
        for (const auto& c : mWorldInside)
        {
            int n = 0;
            for (const auto& place : c.second)
                for (int v : place.second) n += v;
            if (n > 0)
                mWorldCounts[c.first] += n;
        }
    }

    // "Saltrice (20): shop (keeper)" for every tracked shop one of these doors leads into
    std::vector<std::string> Ingredients::shopLinesForDoors(const std::vector<std::string>& dests)
    {
        std::vector<std::string> lines;
        for (const std::string& dest : dests)
            for (size_t i = 0; i < mTable.size(); ++i)
            {
                if (i < mTracked.size() && !mTracked[i])
                    continue;
                for (const IngredientSeller& s : mTable[i].sellers)
                {
                    if (Misc::StringUtils::lowerCase(s.cell) != dest)
                        continue;
                    const size_t comma = s.cell.find(", ");
                    const int stock = keeperStock(s.npc, mTable[i].ids);
                    const std::string line = mTable[i].name + (stock > 0 ? " (" + std::to_string(stock) + ")" : "") + ": "
                        + (comma == std::string::npos ? s.cell : s.cell.substr(comma + 2)) + " (" + s.npc + ")";
                    if (std::find(lines.begin(), lines.end(), line) == lines.end())
                        lines.push_back(line);
                }
            }
        return lines;
    }

    // every grid cell wears the big map's tooltip: the cell's name over a rule, then what grows there per
    // ingredient ("7 of 12 standing" where that is known), what the big map found indoors, and the shops
    void Ingredients::updateGridTips()
    {
        std::string audit;
        for (int slot = 0; slot < 25; ++slot)
        {
            const GridInfo& info = mGridInfo[slot];
            std::string title, body;
            if (!info.known)
            {
                title = "Not read yet";
                body = gridSlotShown(slot) ? "This cell's numbers arrive in a moment" : "";
            }
            else
            {
                title = info.exterior ? worldCellName(info.x, info.y) : mCellName;
                for (size_t i = 0; i < mTable.size(); ++i)
                {
                    const int placed = i < info.placed.size() ? info.placed[i] : 0;
                    const int standing = i < info.standing.size() ? info.standing[i] : 0;
                    if (placed <= 0 && !(info.live && standing > 0))
                        continue;
                    body += (body.empty() ? "" : "\n") + mTable[i].name + ": "
                          + (info.live ? std::to_string(standing) + " of " + std::to_string(placed) + " standing" : std::to_string(placed));
                }
                if (info.exterior)
                {
                    std::map<std::pair<int, int>, std::vector<std::pair<std::string, std::vector<int>>>>::const_iterator ins = mWorldInside.find(std::make_pair(info.x, info.y));
                    if (ins != mWorldInside.end())
                        for (const auto& place : ins->second)
                            for (size_t i = 0; i < place.second.size() && i < mTable.size(); ++i)
                                if (place.second[i] > 0)
                                    body += (body.empty() ? "" : "\n") + mTable[i].name + ": " + std::to_string(place.second[i]) + " in " + place.first;
                }
                for (const std::string& line : info.shops)
                    body += (body.empty() ? "" : "\n") + line;
                if (body.empty())
                    body = "nothing tracked here";
            }
            // the audit trail, cell by cell and ingredient by ingredient (the nine loaded cells only)
            if (info.known && !info.standing.empty())
                for (size_t i = 0; i < mTable.size(); ++i)
                {
                    const int placed = i < info.placed.size() ? info.placed[i] : 0;
                    const int standing = i < info.standing.size() ? info.standing[i] : 0;
                    if (placed > 0 || standing > 0)
                        audit += (audit.empty() ? "" : "; ") + title + " " + mTable[i].name + " " + std::to_string(standing) + "/" + std::to_string(placed);
                }
            if (title != mGridTipTitle[slot]) { mGridTipTitle[slot] = title; mGridBg[slot]->setUserString("Caption_CellTitle", title); }
            if (body != mGridTipBody[slot]) { mGridTipBody[slot] = body; mGridBg[slot]->setUserString("Caption_CellBody", body); }
        }
        if (audit != mGridAudit && !audit.empty())
        {
            mGridAudit = audit;
            mwmp::SessionLog::get().note("PLANT", "standing/placed  " + audit);
        }
    }

    int Ingredients::insideTotal(int x, int y) const
    {
        std::map<std::pair<int, int>, std::vector<std::pair<std::string, std::vector<int>>>>::const_iterator it = mWorldInside.find(std::make_pair(x, y));
        if (it == mWorldInside.end())
            return 0;
        int n = 0;
        for (const auto& place : it->second)
            for (size_t i = 0; i < place.second.size() && i < mTracked.size(); ++i)
                if (mTracked[i]) n += place.second[i];
        return n;
    }

    void Ingredients::worldScanStep()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWWorld::ESMStore& store = world->getStore();
        const std::vector<IngredientDef>& table = mTable;
        // one object id -> how many tracked rows it feeds (a loose tracked ingredient, or an organic
        // container whose contents yield one)
        auto countId = [&](const std::string& id) {
            int n = 0;
            for (const IngredientDef& def : table)
                for (const std::string& want : def.ids)
                    if (Misc::StringUtils::ciEqual(id, want)) { ++n; break; }
            if (n > 0)
                return n;
            const ESM::Container* rec = store.get<ESM::Container>().search(id);
            if (rec && (rec->mFlags & ESM::Container::Organic))
                for (const IngredientDef& def : table)
                    if (listHas(rec->mInventory, def.ids))
                        ++n;
            return n;
        };
        // the interiors of every tracked shop: a door into one puts a coin on the cell
        std::vector<std::string> shopCells;
        for (const IngredientDef& def : table)
            for (const IngredientSeller& s : def.sellers)
                shopCells.push_back(Misc::StringUtils::lowerCase(s.cell));
        int done = 0;
        while (mWorldNext < mWorldQueue.size() && done < sWorldCellsPerFrame)
        {
            const std::pair<int, int> xy = mWorldQueue[mWorldNext++];
            ++done;
            // getExterior reads the cell's references in (no rendering, nothing activated) if it was not yet
            MWWorld::CellStore* cs = world->getExterior(xy.first, xy.second);
            if (!cs)
                continue;
            int n = 0;
            std::vector<int> perRow(table.size(), 0);
            if (cs->getState() == MWWorld::CellStore::State_Loaded)
            {
                CellScan v(table);
                v.shopCells = &shopCells;
                cs->forEachType<ESM::Ingredient>(v);
                cs->forEachType<ESM::Container>(v);
                cs->forEachType<ESM::Door>(v);
                for (size_t i = 0; i < table.size(); ++i) n += v.raw[i];
                perRow = v.raw;
                for (const std::string& dest : v.doorShops)
                    for (const IngredientDef& def : table)
                        for (const IngredientSeller& s : def.sellers)
                        {
                            if (Misc::StringUtils::lowerCase(s.cell) != dest)
                                continue;
                            const size_t comma = s.cell.find(", ");
                            const int stock = keeperStock(s.npc, def.ids);
                            const std::string line = def.name + (stock > 0 ? " (" + std::to_string(stock) + ")" : "") + ": "
                                + (comma == std::string::npos ? s.cell : s.cell.substr(comma + 2)) + " (" + s.npc + ")";
                            std::vector<std::string>& lines = mWorldShops[xy];
                            if (std::find(lines.begin(), lines.end(), line) == lines.end())
                                lines.push_back(line);
                        }
            }
            else
            {
                cs->preload();
                if (cs->getState() == MWWorld::CellStore::State_Preloaded)
                    for (const std::string& id : cs->getPreloadedIds())
                        n += countId(id);
            }
            if (n > 0)
            {
                mWorldCounts[xy] = n;
                mWorldBreakdown[xy] = perRow;
            }
        }
        // then the insides, fewer per frame (a cave holds more than a stretch of coast)
        int doneInside = 0;
        while (mWorldNext >= mWorldQueue.size() && mWorldInteriorNext < mWorldInteriorQueue.size() && doneInside < sWorldInteriorsPerFrame)
        {
            const std::string& name = mWorldInteriorQueue[mWorldInteriorNext++];
            ++doneInside;
            MWWorld::CellStore* cs = nullptr;
            try { cs = world->getInterior(name); } catch (...) { cs = nullptr; }
            if (!cs || cs->getState() != MWWorld::CellStore::State_Loaded)
                continue;
            CellScan v(table);
            v.inside = true;
            cs->forEachType<ESM::Ingredient>(v);
            cs->forEachType<ESM::Container>(v);
            cs->forEachType<ESM::Door>(v);
            InteriorFind& f = mWorldInteriors[Misc::StringUtils::lowerCase(name)];
            f.name = name;
            f.raw = v.raw;
            f.exitsInterior = v.exitsInterior;
            f.exitsExterior = v.exitsExterior;
        }
        if (mWorldNext >= mWorldQueue.size() && mWorldInteriorNext >= mWorldInteriorQueue.size() && mWorldScanning)
        {
            creditInteriors();
            mWorldScanning = false;
        }
        if (mWorldMap->getVisible() && (!mWorldScanning || ((mWorldNext + mWorldInteriorNext) % (sWorldCellsPerFrame * 5)) == 0))
            rebuildWorldCells();
    }

    void Ingredients::rebuildWorldCells()
    {
        MyGUI::Gui& gui = MyGUI::Gui::getInstance();
        for (MyGUI::Widget* w : mWorldCells)
            gui.destroyWidget(w);
        mWorldCells.clear();
        mWorldArrow = nullptr;
        if (!mMapWindow || !mMapWindow->getGlobalMapRender())
            return;
        MWRender::GlobalMap* gm = mMapWindow->getGlobalMapRender();
        const int w = mWorldImage->getWidth(), h = mWorldImage->getHeight();
        const int cs = std::max(6, static_cast<int>(gm->getCellSize() * mWorldScale));
        const int ox = mWorldImage->getLeft(), oy = mWorldImage->getTop();
        auto cellBox = [&](int x, int y) {
            float ix = 0.f, iy = 0.f;
            gm->cellTopLeftCornerToImageSpace(x, y, ix, iy);
            return MyGUI::IntCoord(ox + static_cast<int>(ix * w), oy + static_cast<int>(iy * h), cs, cs);
        };
        // a faint dark square, a pixel in from each counted cell's edge, so the cell grid reads under everything
        for (const auto& c : mWorldCounts)
        {
            const MyGUI::IntCoord box = cellBox(c.first.first, c.first.second);
            MyGUI::ImageBox* pane = mWorldMap->createWidget<MyGUI::ImageBox>("ImageBox",
                MyGUI::IntCoord(box.left + 1, box.top + 1, std::max(1, box.width - 2), std::max(1, box.height - 2)), MyGUI::Align::Default);
            pane->setImageTexture("white");
            pane->setColour(MyGUI::Colour(0.f, 0.f, 0.f));
            pane->setAlpha(0.3f);
            pane->setNeedMouseFocus(false);
            mWorldCells.push_back(pane);
        }
        // a coin on every cell with a door into a tracked shop, under the count when there is one; hovering
        // it names the shops and their keepers
        for (const auto& shop : mWorldShops)
        {
            MyGUI::ImageBox* coin = mWorldMap->createWidget<MyGUI::ImageBox>("ImageBox", cellBox(shop.first.first, shop.first.second), MyGUI::Align::Default);
            coin->setImageTexture("icons\\m\\tx_gold_001.dds");
            coin->setAlpha(0.9f);
            coin->setNeedMouseFocus(true);
            std::string body;
            for (const std::string& line : shop.second) body += (body.empty() ? "" : "\n") + line;
            coin->setUserString("ToolTipType", "Layout");
            coin->setUserString("ToolTipLayout", "MajereCellToolTip");
            coin->setUserString("Caption_CellTitle", worldCellName(shop.first.first, shop.first.second));
            coin->setUserString("Caption_CellBody", body);
            mWorldCells.push_back(coin);
        }
        // the player: the map arrow, over the coins and under the counts, placed and turned every frame
        mWorldArrow = mWorldMap->createWidget<MyGUI::ImageBox>("RotatingSkin", MyGUI::IntCoord(0, 0, sArrowBox, sArrowBox), MyGUI::Align::Default);
        mWorldArrow->setImageTexture("textures\\majere_maparrow.png");
        mWorldArrow->setAlpha(0.95f);
        mWorldArrow->setNeedMouseFocus(false);
        mWorldCells.push_back(mWorldArrow);
        updateWorldArrow();
        int total = 0, most = 1;
        for (const auto& c : mWorldCounts)
            most = std::max(most, c.second);
        // the tenth-highest count (white from there up) and the highest below it (the red end of the rest)
        std::vector<int> sorted;
        for (const auto& c : mWorldCounts) sorted.push_back(c.second);
        std::sort(sorted.begin(), sorted.end(), std::greater<int>());
        const int tenth = sorted.size() >= 10 ? sorted[9] : (sorted.empty() ? 1 : sorted.back());
        int rest = 1;
        for (int n : sorted) if (n < tenth) { rest = n; break; }
        for (const auto& c : mWorldCounts)
        {
            const MyGUI::IntCoord box = cellBox(c.first.first, c.first.second);
            // the count in a colour: the ten richest cells white, the rest red / orange / yellow / green by their
            // share of the richest non-white cell
            MyGUI::Colour tone;
            if (c.second >= tenth)
                tone = MyGUI::Colour(1.f, 1.f, 1.f);
            else
            {
                const float share = static_cast<float>(c.second) / static_cast<float>(std::max(1, rest));
                tone = share >= 0.6f ? MyGUI::Colour(1.f, 0.2f, 0.2f)
                     : share >= 0.35f ? MyGUI::Colour(1.f, 0.6f, 0.15f)
                     : share >= 0.15f ? MyGUI::Colour(1.f, 0.95f, 0.3f)
                     : MyGUI::Colour(0.45f, 0.9f, 0.35f);
            }
            MyGUI::TextBox* label = mWorldMap->createWidget<MyGUI::TextBox>("SandBrightText", box, MyGUI::Align::Default);
            label->setCaption(std::to_string(c.second));
            label->setTextColour(tone);
            label->setTextAlign(MyGUI::Align::Center);
            label->setTextShadow(true);
            // hover: the cell's name, what grows there per ingredient, and its shops (the number sits over the coin)
            std::string body;
            std::map<std::pair<int, int>, std::vector<int>>::const_iterator rows = mWorldBreakdown.find(c.first);
            if (rows != mWorldBreakdown.end())
                for (size_t i = 0; i < rows->second.size() && i < mTable.size(); ++i)
                    if (rows->second[i] > 0)
                        body += (body.empty() ? "" : "\n") + mTable[i].name + ": " + std::to_string(rows->second[i]);
            std::map<std::pair<int, int>, std::vector<std::pair<std::string, std::vector<int>>>>::const_iterator ins = mWorldInside.find(c.first);
            if (ins != mWorldInside.end())
                for (const auto& place : ins->second)
                    for (size_t i = 0; i < place.second.size() && i < mTable.size(); ++i)
                        if (place.second[i] > 0)
                            body += (body.empty() ? "" : "\n") + mTable[i].name + ": " + std::to_string(place.second[i]) + " in " + place.first;
            std::map<std::pair<int, int>, std::vector<std::string>>::const_iterator shop = mWorldShops.find(c.first);
            if (shop != mWorldShops.end())
                for (const std::string& line : shop->second)
                    body += (body.empty() ? "" : "\n") + line;
            label->setNeedMouseFocus(true);
            label->setUserString("ToolTipType", "Layout");
            label->setUserString("ToolTipLayout", "MajereCellToolTip");
            label->setUserString("Caption_CellTitle", worldCellName(c.first.first, c.first.second));
            label->setUserString("Caption_CellBody", body);
            mWorldCells.push_back(label);
            total += c.second;
        }
        std::string names;
        for (const IngredientDef& def : mTable) names += (names.empty() ? "" : ", ") + def.name;
        mWorldStatus->setCaption(names + (mWorldScanning ? "   (reading...)" : ""));
        (void)total;
    }

    // ---------------------------------------------------------------- the other icons

    void Ingredients::onShopsClicked(MyGUI::Widget* /*sender*/)
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;
        mShowShops = !mShowShops;
        Settings::Manager::setBool("show shops", "Ingredients", mShowShops);
        mLastShopsText.clear();
    }

    void Ingredients::onIconRowClicked(MyGUI::Widget* /*sender*/)
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;
        onTrackClicked(nullptr);   // a tracked icon opens the picker, like the mortar
    }

    void Ingredients::onRawClicked(MyGUI::Widget* /*sender*/)
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;
        mShowRaw = true;    // icon -> map (3x3 first)
        Settings::Manager::setBool("show raw", "Ingredients", mShowRaw);
        mGridBig = false;
        Settings::Manager::setBool("grid big", "Ingredients", mGridBig);
        layoutGrid();
    }

    void Ingredients::onGridClicked(MyGUI::Widget* /*sender*/)
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;
        if (!mGridBig)
            mGridBig = true;        // 3x3 -> 5x5
        else
        {
            mGridBig = false;       // 5x5 -> folded icon (the next unfold starts at 3x3 again)
            mShowRaw = false;
            Settings::Manager::setBool("show raw", "Ingredients", mShowRaw);
        }
        Settings::Manager::setBool("grid big", "Ingredients", mGridBig);
        layoutGrid();
        mScanTimer = sScanInterval;   // the outer ring's numbers at once
    }

    int Ingredients::gridPixels() const
    {
        const int n = mGridBig ? 5 : 3;
        return n * sCell + (n - 1) * sCellGap;
    }

    bool Ingredients::gridSlotShown(int slot) const
    {
        if (mGridBig)
            return true;
        const int r = slot / 5, c = slot % 5;
        return r >= 1 && r <= 3 && c >= 1 && c <= 3;
    }

    // cells, counts and coins for the view in force: all 25, or the inner nine packed together
    void Ingredients::layoutGrid()
    {
        const int shift = mGridBig ? 0 : 1;
        for (int i = 0; i < 25; ++i)
        {
            const bool shown = gridSlotShown(i);
            const int cx = (i % 5 - shift) * (sCell + sCellGap), cy = (i / 5 - shift) * (sCell + sCellGap);
            mGridBg[i]->setVisible(shown);
            if (mGridText[i]) mGridText[i]->setVisible(shown);
            if (mGridCoin[i] && !shown) mGridCoin[i]->setVisible(false);
            if (!shown)
                continue;
            mGridBg[i]->setCoord(cx, cy, sCell, sCell);
            if (mGridText[i]) mGridText[i]->setCoord(cx, cy, sCell, sCell);
            if (mGridCoin[i]) mGridCoin[i]->setCoord(cx + sCell - sCoin - 1, cy + 1, sCoin, sCoin);
        }
        const int side = gridPixels();
        mGrid->setSize(side, side);
        if (mMapButton)
            mMapButton->setSize(side, mMapButton->getHeight());
    }

    // ---------------------------------------------------------------- the scan

    void Ingredients::scan()
    {
        const std::vector<IngredientDef>& table = mTable;
        for (Found& f : mFound) { f.shops.clear(); f.sellersHere.clear(); f.raw = 0; }
        mAnyShops = mAnyRaw = false;
        mRingPending = false;
        mCellName.clear();
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty() || !player.isInCell())
            return;
        MWWorld::CellStore* cell = player.getCell();
        mCellName = cell->getCell()->mName;
        if (mCellName.empty())
            mCellName = cell->getCell()->getDescription();

        // static: this town's shops (interior "Town, Shop" cells) with a restocking keeper, or this very shop
        for (size_t i = 0; i < table.size(); ++i)
            for (const IngredientSeller& s : table[i].sellers)
            {
                if (!mTracked[i])
                    break;
                if (Misc::StringUtils::ciEqual(s.cell, mCellName))
                    mFound[i].shops.emplace_back(keeperStock(s.npc, table[i].ids), s.npc + " (this shop)");
            }

        // the interiors of every tracked shop, so a door into one marks its cell on the map
        std::vector<std::string> shopCells;
        for (size_t i = 0; i < table.size(); ++i)
            if (mTracked[i])
                for (const IngredientSeller& s : table[i].sellers)
                    shopCells.push_back(Misc::StringUtils::lowerCase(s.cell));

        // live + static walk of the loaded cell
        CellScan v(table);
        v.shopCells = &shopCells;
        cell->forEachType<ESM::Ingredient>(v);
        cell->forEachType<ESM::Container>(v);
        cell->forEachType<ESM::NPC>(v);
        cell->forEachType<ESM::Door>(v);
        const std::set<std::string> doorDests(v.doorShops.begin(), v.doorShops.end());   // this cell's doors only

        // the 3x3 map: tracked raw placements and shops per cell, north (grid y+1) on the top row, west left
        auto trackedTotal = [&](const CellScan& s) { int n = 0; for (size_t i = 0; i < table.size(); ++i) if (mTracked[i]) n += s.raw[i]; return n; };
        auto trackedSeller = [&](const CellScan& s) { for (size_t i = 0; i < table.size(); ++i) if (mTracked[i] && !s.sellers[i].empty()) return true; return false; };
        for (int i = 0; i < 25; ++i) { mGridCount[i] = 0; mGridLoaded[i] = false; mGridShop[i] = false; mGridInfo[i] = GridInfo(); }
        {
            GridInfo& own = mGridInfo[12];
            own.known = true;
            own.exterior = cell->isExterior();
            if (own.exterior) { own.x = cell->getCell()->getGridX(); own.y = cell->getCell()->getGridY(); }
            own.placed = v.raw;
            own.shops = shopLinesForDoors(v.doorShops);
            if (!own.exterior)
            {
                const std::vector<std::string> here = shopLines(mCellName);   // standing in a tracked shop
                own.shops.insert(own.shops.end(), here.begin(), here.end());
            }
        }
        mGridCount[12] = trackedTotal(v);
        if (cell->isExterior())
            mGridCount[12] += insideTotal(cell->getCell()->getGridX(), cell->getCell()->getGridY());
        mGridLoaded[12] = true;
        mGridShop[12] = v.shopDoor || trackedSeller(v);
        if (cell->isExterior())
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();
            const int cx = cell->getCell()->getGridX(), cy = cell->getCell()->getGridY();
            const int reach = mGridBig ? 2 : 1;
            int ringBudget = 2;     // cells read from the game data per scan: sixteen at once would hitch
            mRingPending = false;
            for (int dx = -reach; dx <= reach; ++dx)
                for (int dy = -reach; dy <= reach; ++dy)
                {
                    if (!dx && !dy) continue;
                    const int slot = (2 - dy) * 5 + (dx + 2);   // row 0 = north
                    if (std::abs(dx) == 2 || std::abs(dy) == 2)
                    {
                        // the outer ring: static placements and shop doors only, read once per cell and kept
                        const std::pair<int, int> xy(cx + dx, cy + dy);
                        std::map<std::pair<int, int>, RingCell>::const_iterator kept = mRingCache.find(xy);
                        if (kept == mRingCache.end())
                        {
                            if (!world->getStore().get<ESM::Cell>().search(xy.first, xy.second))
                                continue;   // no such cell (open sea past the map's edge)
                            if (ringBudget <= 0) { mRingPending = true; continue; }   // shows "-" until its turn
                            --ringBudget;
                            MWWorld::CellStore* far = world->getExterior(xy.first, xy.second);
                            if (!far || far->getState() != MWWorld::CellStore::State_Loaded) continue;
                            CellScan f(table);
                            f.shopCells = &shopCells;
                            far->forEachType<ESM::Ingredient>(f);
                            far->forEachType<ESM::Container>(f);
                            far->forEachType<ESM::Door>(f);
                            RingCell ring;
                            ring.raw = f.raw;
                            ring.shopDoor = f.shopDoor;
                            ring.shops = shopLinesForDoors(f.doorShops);
                            ring.total = trackedTotal(f);
                            kept = mRingCache.emplace(xy, ring).first;
                        }
                        mGridCount[slot] = kept->second.total + insideTotal(xy.first, xy.second);
                        mGridLoaded[slot] = true;
                        mGridShop[slot] = kept->second.shopDoor;
                        {
                            GridInfo& info = mGridInfo[slot];
                            info.known = true;
                            info.x = xy.first; info.y = xy.second;
                            info.placed = kept->second.raw;
                            info.shops = kept->second.shops;
                        }
                        continue;
                    }
                    MWWorld::CellStore* other = world->getExterior(cx + dx, cy + dy);
                    if (!other || other->getState() != MWWorld::CellStore::State_Loaded) continue;
                    CellScan n(table);
                    n.shopCells = &shopCells;
                    other->forEachType<ESM::Ingredient>(n);
                    other->forEachType<ESM::Container>(n);
                    other->forEachType<ESM::NPC>(n);
                    other->forEachType<ESM::Door>(n);
                    mGridCount[slot] = trackedTotal(n) + insideTotal(cx + dx, cy + dy);
                    mGridLoaded[slot] = true;
                    mGridShop[slot] = n.shopDoor || trackedSeller(n);
                    {
                        GridInfo& info = mGridInfo[slot];
                        info.known = true;
                        info.x = cx + dx; info.y = cy + dy;
                        info.placed = n.raw;
                        info.shops = shopLinesForDoors(n.doorShops);
                    }
                }
        }
        // shops whose door stands in THIS cell (the block names shops only from the cell that holds them)
        for (size_t i = 0; i < table.size(); ++i)
        {
            if (!mTracked[i])
                continue;
            for (const IngredientSeller& s : table[i].sellers)
            {
                if (!doorDests.count(Misc::StringUtils::lowerCase(s.cell)))
                    continue;
                const size_t comma = s.cell.find(", ");
                const std::string entry = (comma == std::string::npos ? s.cell : s.cell.substr(comma + 2)) + " (" + s.npc + ")";
                bool listed = false;
                for (const std::pair<int, std::string>& e : mFound[i].shops)
                    if (e.second == entry || e.second == s.npc + " (this shop)") { listed = true; break; }
                if (!listed)
                    mFound[i].shops.emplace_back(keeperStock(s.npc, table[i].ids), entry);
            }
        }
        for (size_t i = 0; i < table.size(); ++i)
        {
            if (!mTracked[i])
            {
                mFound[i].raw = 0;
                mFound[i].nearby = 0;
                continue;
            }
            mFound[i].raw = v.raw[i];
            mFound[i].nearby = v.raw[i];
            mFound[i].sellersHere = v.sellers[i];
            std::sort(mFound[i].sellersHere.begin(), mFound[i].sellersHere.end());
            mFound[i].sellersHere.erase(std::unique(mFound[i].sellersHere.begin(), mFound[i].sellersHere.end()), mFound[i].sellersHere.end());
            if (!mFound[i].shops.empty() || !mFound[i].sellersHere.empty()) mAnyShops = true;
        }
        // the tracked plants of the player's cell and its loaded neighbours: the local map's dots, and -- in
        // dynamic mode -- the inner nine counts of the grid ("what still stands" is only known for loaded cells)
        {
            PlantWalk walk(table, mTracked, mwmp::Main::isLocalServer());
            struct Near { MWWorld::CellStore* store; int slot, x, y; size_t from, to; };
            std::vector<Near> around;
            {
                Near own = { cell, 12, 0, 0, 0, 0 };
                if (cell->isExterior()) { own.x = cell->getCell()->getGridX(); own.y = cell->getCell()->getGridY(); }
                around.push_back(own);
            }
            if (cell->isExterior())
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();
                const int cx = cell->getCell()->getGridX(), cy = cell->getCell()->getGridY();
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        if (dx || dy)
                            if (MWWorld::CellStore* other = world->getExterior(cx + dx, cy + dy))
                                if (other->getState() == MWWorld::CellStore::State_Loaded)
                                {
                                    Near n = { other, (2 - dy) * 5 + (dx + 2), cx + dx, cy + dy, 0, 0 };
                                    around.push_back(n);
                                }
            }
            for (Near& n : around)
            {
                n.from = walk.hits.size();
                n.store->forEachType<ESM::Ingredient>(walk);
                n.store->forEachType<ESM::Container>(walk);
                n.to = walk.hits.size();
            }
            for (int i = 0; i < 25; ++i) mGridLive[i] = false;
            mHereStanding = mHerePlaced = 0;
            for (const Near& n : around)
            {
                int standing = 0, placed = 0;
                GridInfo& info = mGridInfo[n.slot];
                info.standing.assign(table.size(), 0);
                info.live = mPlantsMode == Plants_Dynamic;
                for (size_t k = n.from; k < n.to; ++k)
                {
                    if (walk.hits[k].standing) { ++standing; if (walk.hits[k].row < info.standing.size()) ++info.standing[walk.hits[k].row]; }
                    if (walk.hits[k].placed) ++placed;
                }
                if (n.slot == 12) { mHereStanding = standing; mHerePlaced = placed; }
                if (mPlantsMode == Plants_Dynamic)
                {
                    // what is left to pick outdoors, plus what the big map found indoors (never live)
                    mGridCount[n.slot] = standing + (cell->isExterior() ? insideTotal(n.x, n.y) : 0);
                    mGridLoaded[n.slot] = true;
                    mGridLive[n.slot] = true;
                }
            }
            std::vector<PlantDot> dots;
            if (mPlantsMode != Plants_Off)
                for (const PlantWalk::Hit& h : walk.hits)
                {
                    if (mPlantsMode == Plants_Dynamic ? !h.standing : !h.placed)
                        continue;
                    PlantDot d;
                    d.x = h.x; d.y = h.y;
                    d.colour = h.row < mTableLook.size() ? mTableLook[h.row].colour : mNormalColour;
                    if (d.colour == mNormalColour)
                        d.colour = MyGUI::Colour(1.f, 0.9f, 0.55f);   // no effect category: a plain warm gold
                    dots.push_back(d);
                }
            bool same = dots.size() == mDots.size();
            for (size_t k = 0; same && k < dots.size(); ++k)
                same = dots[k].x == mDots[k].x && dots[k].y == mDots[k].y && dots[k].colour == mDots[k].colour;
            if (!same)
            {
                mDots.swap(dots);
                ++mDotsVersion;
            }
            // the audit trail: does "standing" follow harvests by other players and cell resets on a real server?
            const std::string audit = mCellName + ": " + std::to_string(mHereStanding) + " standing, " + std::to_string(mHerePlaced) + " placed by the game data";
            if (audit != mDotsAudit && (mHereStanding > 0 || mHerePlaced > 0))
            {
                mDotsAudit = audit;
                mwmp::SessionLog::get().note("PLANT", audit);
            }
        }

        // inside a shop: the player's own cell IS the shop
        if (!cell->isExterior() && mAnyShops)
            mGridShop[12] = true;
        for (int i = 0; i < 25; ++i)
            if (mGridCount[i] > 0) mAnyRaw = true;
    }

    void Ingredients::updateText()
    {
        const std::vector<IngredientDef>& table = mTable;
        static const std::string cNormal = hex(MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal}")));
        static const std::string cHeader = hex(MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=header}")));

        std::string shops = cHeader + "Shops" + cNormal;
        for (size_t i = 0; i < table.size(); ++i)
        {
            const Found& f = mFound[i];
            if (f.shops.empty() && f.sellersHere.empty())
                continue;
            const std::string name = hex(i < mTableLook.size() ? mTableLook[i].colour : mNormalColour) + table[i].name;
            for (const std::pair<int, std::string>& s : f.shops)
                shops += "\n" + name + (s.first > 0 ? " (" + std::to_string(s.first) + ")" : "") + cNormal + ": " + s.second;
            for (const std::string& s : f.sellersHere)
            {
                bool listed = false;   // already named as this shop's keeper: "(this shop)" says it all
                for (const std::pair<int, std::string>& shop : f.shops)
                    if (shop.second.compare(0, s.size(), s) == 0) { listed = true; break; }
                if (listed)
                    continue;
                const int stock = keeperStock(s, table[i].ids);
                shops += "\n" + name + (stock > 0 ? " (" + std::to_string(stock) + ")" : "") + cNormal + ": " + s + " (here)";
            }
        }
        if (shops.size() == cHeader.size() + 5 + cNormal.size())
            shops += ": none";
        mShopsText = shops;

        if (mShopsText != mLastShopsText) { mLastShopsText = mShopsText; mInfo->setCaption(mShopsText); }
        for (size_t i = 0; i < 3; ++i)
        {
            if (!mGridQty[i]) continue;
            const int n = i < mTable.size() ? inventoryCount(mTable[i]) : 0;
            const std::string cap = n > 0 ? std::to_string(n) : std::string("");
            if (mGridQty[i]->getCaption() != cap)
                mGridQty[i]->setCaption(cap);
        }
        {
            std::string tip = "Tracked ingredients in the cells around you, north up; a coin marks a shop\n";
            if (mPlantsMode == Plants_Dynamic)
                tip += "Bright numbers: plants still standing (the nine cells around you). Dim: placed by the game data.\n"
                       "This cell: " + std::to_string(mHereStanding) + " of " + std::to_string(mHerePlaced) + " standing\n";
            else
                tip += "Numbers: plants placed by the game data (Plants button on the map: dynamic shows what is left)\n"
                       "This cell: " + std::to_string(mHerePlaced) + " placed, " + std::to_string(mHereStanding) + " standing\n";
            tip += "Click: 3x3, then 5x5, then folded";
            if (tip != mGridTip)
            {
                mGridTip = tip;
                mGrid->setUserString("Caption_Text", tip);
            }
        }
        for (int i = 0; i < 25; ++i)
        {
            const std::string cap = mGridLoaded[i] ? std::to_string(mGridCount[i]) : std::string("-");
            if (mGridText[i]->getCaption() != cap)
                mGridText[i]->setCaption(cap);
            mGridText[i]->setTextColour(mGridCount[i] <= 0 ? MyGUI::Colour(0.6f, 0.6f, 0.6f)
                                      : mGridLive[i] ? MyGUI::Colour(0.45f, 0.95f, 0.45f)      // still standing
                                      : MyGUI::Colour(0.42f, 0.66f, 0.42f));                    // placed by the game data
            mGridCoin[i]->setVisible(mGridShop[i] && gridSlotShown(i));
        }
    }

    void Ingredients::onFrame(float dt)
    {
        if (!mEnabled || !mHudVisible)
            return;
        {
            // a cell change refreshes the panels at once instead of waiting out the scan interval
            MWWorld::Ptr player = MWMechanics::getPlayer();
            const MWWorld::CellStore* cur = (!player.isEmpty() && player.isInCell()) ? player.getCell() : nullptr;
            if (cur != mLastCell) { mLastCell = cur; mScanTimer = sScanInterval; }
        }
        mScanTimer += dt;
        if (mScanTimer >= sScanInterval)
        {
            mScanTimer = 0.f;
            scan();
            updateText();
            updateGridTips();
            if (mRingPending)
                mScanTimer = sScanInterval - 0.1f;   // the 5x5 ring fills in two cells every tenth of a second
        }
        {
            MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
            const bool gui = wm->isGuiMode();
            if (!gui && mGuiLast)
            {
                // back to the game: hide, and remember what was up
                if (mPicker->getVisible()) { mPicker->setVisible(false); mPickerReopen = true; }
                if (mWorldMap->getVisible()) { closeWorldMap(); mWorldReopen = true; }
            }
            else if (gui && !mGuiLast && wm->containsMode(MWGui::GM_Inventory))
            {
                if (mWorldReopen) openWorldMap();
                if (mPickerReopen)
                {
                    mPicker->setVisible(true);
                    rebuildPicker();
                    MyGUI::LayerManager::getInstance().upLayerItem(mPicker);
                }
            }
            else if (!gui && mPicker->getVisible())
                mPicker->setVisible(false);
            mGuiLast = gui;
        }
        mTrackIcon->setVisible(true);
        // the coin and the alchemy icon are always there (the map and the shops block must stay reachable)
        mShopsIcon->setVisible(true);
        const bool mapOpen = mShowRaw;
        mRawIcon->setVisible(!mapOpen);
        mGrid->setVisible(mapOpen);
        mIconRow->setVisible(true);   // the tracked icons show folded or not (each tile hides itself when untracked)
        mInfo->setVisible(mShowShops);
        mMapButton->setVisible(mapOpen);
        if (mapOpen)
        {
            // the arrow: where the player stands within the middle cell (so the border is visible coming), turned
            // to the yaw the way the HUD's own compass is (atan2(sin, cos) = yaw)
            MWWorld::Ptr player = MWMechanics::getPlayer();
            MyGUI::ISubWidget* main = mCompass->getSubWidgetMain();
            MyGUI::RotatingSkin* rot = main ? main->castType<MyGUI::RotatingSkin>(false) : nullptr;
            if (rot && !player.isEmpty())
            {
                const ESM::Position& pp = player.getRefData().getPosition();
                float fx = 0.5f, fy = 0.5f;
                if (player.isInCell() && player.getCell()->isExterior())
                {
                    const float cs = static_cast<float>(Constants::CellSizeInUnits);
                    fx = (pp.pos[0] - std::floor(pp.pos[0] / cs) * cs) / cs;
                    fy = (pp.pos[1] - std::floor(pp.pos[1] / cs) * cs) / cs;
                }
                const int centre = (mGridBig ? 2 : 1) * (sCell + sCellGap);
                const int cx = centre + static_cast<int>(std::lround(fx * (sCell - 1)));
                const int cy = centre + static_cast<int>(std::lround((1.f - fy) * (sCell - 1)));
                mCompass->setPosition(cx - sArrowBox / 2, cy - sArrowBox / 2);
                rot->setCenter(MyGUI::IntPoint(sArrowBox / 2, sArrowBox / 2));
                rot->setAngle(pp.rot[2]);
            }
        }
        if (mWorldMap->getVisible() && !MWBase::Environment::get().getWindowManager()->isGuiMode())
            closeWorldMap();
        if (mWorldMap->getVisible())
            updateWorldArrow();
        if (mWorldScanning)
            worldScanStep();
        place();
    }
}
