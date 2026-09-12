/*
    majere addition (ingredient finder) -- see ingredients.hpp
*/
#include "ingredients.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <typeinfo>

#include <MyGUI_Gui.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_LayerManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_RotatingSkin.h>
#include <MyGUI_TextBox.h>

#include <components/esm/attr.hpp>
#include <components/esm/loadcont.hpp>
#include <components/esm/loaddoor.hpp>
#include <components/esm/loadingr.hpp>
#include <components/esm/loadlevlist.hpp>
#include <components/esm/loadmgef.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/esm/loadskil.hpp>
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
#include "../mwworld/ptr.hpp"

#include "hotbar.hpp"

namespace
{
    int settingInt(const char* key, int def)
    {
        try { return Settings::Manager::getInt(key, "Ingredients"); } catch (...) { return def; }
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

    // one walk over the cell for every ingredient on the list
    struct CellScan
    {
        const std::vector<MWGui::IngredientDef>& table;
        std::vector<int> raw;                                // per ingredient: static placements
        std::vector<std::vector<std::string>> sellers;       // per ingredient: live NPC names here
        const std::vector<std::string>* shopCells;           // lower-case interior names of tracked shops (may be null)
        bool shopDoor;                                       // a door here leads into one of them
        explicit CellScan(const std::vector<MWGui::IngredientDef>& t) : table(t), raw(t.size(), 0), sellers(t.size()), shopCells(nullptr), shopDoor(false) {}

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
                for (size_t i = 0; i < table.size(); ++i)
                    for (const std::string& want : table[i].ids)
                        if (Misc::StringUtils::ciEqual(id, want)) { ++raw[i]; break; }
            }
            else if (type == typeid(ESM::Container).name())
            {
                const ESM::Container* rec = ptr.get<ESM::Container>()->mBase;
                if (!(rec->mFlags & ESM::Container::Organic) || !ptr.getCellRef().getRefNum().hasContentFile())
                    return true;
                for (size_t i = 0; i < table.size(); ++i)
                    if (listHas(rec->mInventory, table[i].ids))
                        ++raw[i];
            }
            else if (type == typeid(ESM::Door).name())
            {
                if (!shopCells || !ptr.getCellRef().getTeleport())
                    return true;
                const std::string dest = Misc::StringUtils::lowerCase(ptr.getCellRef().getDestCell());
                for (const std::string& s : *shopCells)
                    if (s == dest) { shopDoor = true; break; }
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
        , mPicker(nullptr), mSearchEdit(nullptr), mInfo(nullptr), mGrid(nullptr), mCompass(nullptr)
        , mAnyShops(false), mAnyRaw(false), mScanTimer(sScanInterval), mLastCell(nullptr)
    {
        mEnabled   = settingBool("enabled", true);
        mInfoWidth = std::max(200, settingInt("info width", 460));
        mShowShops = settingBool("show shops", false);
        mShowRaw   = settingBool("show raw", false);

        mNormalColour = MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal}"));
        for (int i = 0; i < 3; ++i) mGridIcon[i] = nullptr;
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
        mSearchEdit = mPicker->createWidget<MyGUI::EditBox>("MW_TextEdit", MyGUI::IntCoord(sPad, 0, pickerW - 2 * sPad, sSearchH), MyGUI::Align::Default);
        mSearchEdit->setNeedKeyFocus(true);
        mSearchEdit->setNeedMouseFocus(true);
        mSearchEdit->eventEditTextChange += MyGUI::newDelegate(this, &Ingredients::onSearchChanged);
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
        mGrid = gui.createWidget<MyGUI::Widget>("", MyGUI::IntCoord(0, 0, gridSize + sGridIconGap + 3 * sIcon + 2 * sGridIconGap, gridSize), MyGUI::Align::Default, "Menu");
        mGrid->setNeedMouseFocus(true);
        mGrid->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onGridClicked);
        mGrid->setUserString("ToolTipType", "Layout");
        mGrid->setUserString("ToolTipLayout", "TextToolTip");
        mGrid->setUserString("Caption_Text", "Tracked ingredients in the cells around you, north up; a coin marks a shop (click to fold)");
        for (int i = 0; i < 9; ++i)
        {
            const int cx = (i % 3) * (sCell + sCellGap), cy = (i / 3) * (sCell + sCellGap);
            mGridBg[i] = mGrid->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(cx, cy, sCell, sCell), MyGUI::Align::Default);
            mGridBg[i]->setImageTexture("white");
            mGridBg[i]->setColour(i == 4 ? MyGUI::Colour(0.35f, 0.28f, 0.12f) : MyGUI::Colour(0.f, 0.f, 0.f));
            mGridBg[i]->setAlpha(0.55f);
            mGridBg[i]->setNeedMouseFocus(false);
            mGridCount[i] = 0;
            mGridLoaded[i] = false;
            mGridShop[i] = false;
        }
        // the facing arrow: a small gold arrow head at the top of a transparent cell-sized image, rotated about the
        // cell's centre by the RotatingSkin, so it rides the rim of the middle cell and never covers the count
        mCompass = mGrid->createWidget<MyGUI::ImageBox>("RotatingSkin",
            MyGUI::IntCoord(sCell + sCellGap, sCell + sCellGap, sCell, sCell), MyGUI::Align::Default);
        mCompass->setImageTexture("textures\\majere_maparrow.png");
        mCompass->setNeedMouseFocus(false);
        for (int i = 0; i < 9; ++i)
        {
            const int cx = (i % 3) * (sCell + sCellGap), cy = (i / 3) * (sCell + sCellGap);
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
        for (int i = 0; i < 3; ++i)
        {
            Look none;
            none.colour = mNormalColour;
            mGridIcon[i] = makeTile(mGrid, gridSize + sGridIconGap + i * (sIcon + sGridIconGap), gridSize - sIcon, sIcon, none, true);   // plain vanilla icon
            mGridIcon[i]->eventMouseButtonClick += MyGUI::newDelegate(this, &Ingredients::onGridClicked);   // any click folds the map
            mGridIcon[i]->setVisible(false);
        }
        mGrid->setVisible(false);
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
        MyGUI::Gui& gui = MyGUI::Gui::getInstance();
        if (mInfo) gui.destroyWidget(mInfo);
        if (mGrid) gui.destroyWidget(mGrid);
        if (mPicker) gui.destroyWidget(mPicker);   // takes the search box and the tiles with it
        if (mTrackIcon) gui.destroyWidget(mTrackIcon);
        if (mShopsIcon) gui.destroyWidget(mShopsIcon);
        if (mRawIcon) gui.destroyWidget(mRawIcon);
    }

    void Ingredients::place()
    {
        // icons from the hotbar's anchor (right of its page label), vertically centred on the slot row; the
        // panel above the first icon, bottom-anchored so it only ever grows upward
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
        // the 3x3 map sits where the icon is, bottom-left aligned to it
        mGrid->setPosition(x, y + sIcon - mGrid->getHeight());

        // the picker above the mortar icon, kept on screen
        int px = mTrackIcon->getLeft(), py = y - 4 - mPicker->getHeight();
        px = std::max(4, std::min(px, view.width - mPicker->getWidth() - 4));
        py = std::max(4, py);
        mPicker->setPosition(px, py);
        // the shops block sits above the icons (above the open map if it is up); right of the picker when open
        const int panelX = mPicker->getVisible() ? mPicker->getRight() + 12 : mShopsIcon->getLeft();
        int bottom = y - 6;
        if (mGrid->getVisible())
            bottom = std::min(bottom, mGrid->getTop() - 6);
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
            mPicker->setVisible(false);
        }
    }

    // ---------------------------------------------------------------- tracked set

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
            def.ids.push_back(id);
            mTable.push_back(def);
        }
        mTracked.assign(mTable.size(), true);
        mFound.assign(mTable.size(), Found());
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
            CatalogueEntry e;
            e.id = Misc::StringUtils::lowerCase(rec.mId);
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
            if (e.id == id)
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
        MyGUI::ImageBox* back = makeTile(mPicker, x, y, sTile, look);
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
            MyGUI::TextBox* t = mPicker->createWidget<MyGUI::TextBox>("SandBrightText", MyGUI::IntCoord(sPad, y, innerW, sHeaderH), MyGUI::Align::Default);
            t->setCaption(text);
            t->setTextColour(sGold);
            t->setNeedMouseFocus(false);
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
        const std::vector<std::string> words = splitList(Misc::StringUtils::lowerCase(mSearchEdit->getCaption()), true);
        std::vector<std::string> hits;
        int more = 0;
        for (const CatalogueEntry& e : mCatalogue)
        {
            bool ok = true;
            for (const std::string& w : words)
                if (e.searchText.find(w) == std::string::npos) { ok = false; break; }
            if (!ok)
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

    // ---------------------------------------------------------------- the other icons

    void Ingredients::onShopsClicked(MyGUI::Widget* /*sender*/)
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;
        mShowShops = !mShowShops;
        Settings::Manager::setBool("show shops", "Ingredients", mShowShops);
        mLastShopsText.clear();
    }

    void Ingredients::onRawClicked(MyGUI::Widget* /*sender*/)
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;
        mShowRaw = true;    // icon -> map
        Settings::Manager::setBool("show raw", "Ingredients", mShowRaw);
    }

    void Ingredients::onGridClicked(MyGUI::Widget* /*sender*/)
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;
        mShowRaw = false;   // map -> icon
        Settings::Manager::setBool("show raw", "Ingredients", mShowRaw);
    }

    // ---------------------------------------------------------------- the scan

    void Ingredients::scan()
    {
        const std::vector<IngredientDef>& table = mTable;
        for (Found& f : mFound) { f.shops.clear(); f.sellersHere.clear(); f.raw = 0; }
        mAnyShops = mAnyRaw = false;
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
                const bool here = Misc::StringUtils::ciEqual(s.cell, mCellName);
                const bool inTown = cell->isExterior()
                    && s.cell.size() > mCellName.size() + 2
                    && Misc::StringUtils::ciEqual(s.cell.substr(0, mCellName.size()), mCellName)
                    && s.cell.compare(mCellName.size(), 2, ", ") == 0;
                if (here)
                    mFound[i].shops.push_back(s.npc + " (this shop)");
                else if (inTown)
                    mFound[i].shops.push_back(s.cell.substr(mCellName.size() + 2) + " (" + s.npc + ")");
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

        // the 3x3 map: tracked raw placements and shops per cell, north (grid y+1) on the top row, west left
        auto trackedTotal = [&](const CellScan& s) { int n = 0; for (size_t i = 0; i < table.size(); ++i) if (mTracked[i]) n += s.raw[i]; return n; };
        auto trackedSeller = [&](const CellScan& s) { for (size_t i = 0; i < table.size(); ++i) if (mTracked[i] && !s.sellers[i].empty()) return true; return false; };
        for (int i = 0; i < 9; ++i) { mGridCount[i] = 0; mGridLoaded[i] = false; mGridShop[i] = false; }
        mGridCount[4] = trackedTotal(v);
        mGridLoaded[4] = true;
        mGridShop[4] = v.shopDoor || trackedSeller(v);
        if (cell->isExterior())
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();
            const int cx = cell->getCell()->getGridX(), cy = cell->getCell()->getGridY();
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                {
                    if (!dx && !dy) continue;
                    const int slot = (1 - dy) * 3 + (dx + 1);   // row 0 = north
                    MWWorld::CellStore* other = world->getExterior(cx + dx, cy + dy);
                    if (!other || other->getState() != MWWorld::CellStore::State_Loaded) continue;
                    CellScan n(table);
                    n.shopCells = &shopCells;
                    other->forEachType<ESM::Ingredient>(n);
                    other->forEachType<ESM::Container>(n);
                    other->forEachType<ESM::NPC>(n);
                    other->forEachType<ESM::Door>(n);
                    mGridCount[slot] = trackedTotal(n);
                    mGridLoaded[slot] = true;
                    mGridShop[slot] = n.shopDoor || trackedSeller(n);
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
        // inside a shop: the player's own cell IS the shop
        if (!cell->isExterior() && mAnyShops)
            mGridShop[4] = true;
        for (int i = 0; i < 9; ++i)
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
            shops += "\n" + hex(i < mTableLook.size() ? mTableLook[i].colour : mNormalColour) + table[i].name + cNormal + ": ";
            bool first = true;
            for (const std::string& s : f.shops) { shops += (first ? "" : ", ") + s; first = false; }
            for (const std::string& s : f.sellersHere)
            {
                bool listed = false;   // already named as this shop's keeper: "(this shop)" says it all
                for (const std::string& shop : f.shops)
                    if (shop.compare(0, s.size(), s) == 0) { listed = true; break; }
                if (listed)
                    continue;
                shops += (first ? "" : ", ") + s + " (here)";
                first = false;
            }
        }
        mShopsText = shops;

        if (mShopsText != mLastShopsText) { mLastShopsText = mShopsText; mInfo->setCaption(mShopsText); }
        for (int i = 0; i < 9; ++i)
        {
            const std::string cap = mGridLoaded[i] ? std::to_string(mGridCount[i]) : std::string("-");
            if (mGridText[i]->getCaption() != cap)
                mGridText[i]->setCaption(cap);
            mGridText[i]->setTextColour(mGridCount[i] > 0 ? MyGUI::Colour(0.45f, 0.9f, 0.45f) : MyGUI::Colour(0.6f, 0.6f, 0.6f));
            mGridCoin[i]->setVisible(mGridShop[i]);
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
        }
        if (mPicker->getVisible() && !MWBase::Environment::get().getWindowManager()->isGuiMode())
            mPicker->setVisible(false);
        mTrackIcon->setVisible(true);
        mShopsIcon->setVisible(mAnyShops);
        const bool mapOpen = mAnyRaw && mShowRaw;
        mRawIcon->setVisible(mAnyRaw && !mapOpen);
        mGrid->setVisible(mapOpen);
        mInfo->setVisible(mAnyShops && mShowShops);
        if (mapOpen)
        {
            // compass: the player's yaw, driven the way the HUD's own compass is (atan2(sin, cos) = yaw)
            MWWorld::Ptr player = MWMechanics::getPlayer();
            MyGUI::ISubWidget* main = mCompass->getSubWidgetMain();
            MyGUI::RotatingSkin* rot = main ? main->castType<MyGUI::RotatingSkin>(false) : nullptr;
            if (rot && !player.isEmpty())
            {
                rot->setCenter(MyGUI::IntPoint(sCell / 2, sCell / 2));
                rot->setAngle(player.getRefData().getPosition().rot[2]);
            }
        }
        place();
    }
}
