#ifndef MWGUI_INGREDIENTS_H
#define MWGUI_INGREDIENTS_H

/*
    majere addition (ingredient finder)

    A mortar-and-pestle icon right of the hotbar opens (in menus) the ingredient picker: a search box over EVERY
    ingredient record in the loaded game data (type part of a name, or of an effect: "resist" lists everything
    with Resist Fire, Resist Magicka, ...), results tiled as their icons (hover for the name and all four
    effects, click to track / untrack). The tracked ones sit in a top section, starred, above a rule; recent
    picks that are not tracked any more sit just above the search box for quick re-adding. At most THREE are
    tracked at a time (a fourth pick bumps the oldest into Recent). Tracked and recent lists are remembered in
    settings. Colour code, on names and tile rims: an ingredient takes the colour of its effect category --
    fire red, frost blue, shock purple, dispel / spell absorption white, fatigue green, health crimson -- and
    when it has effects in more than one category the one that more ingredients share (the easiest to brew)
    decides; with none of those it keeps the normal font colour. Then two icons that only appear when the
    current cell has something for the TRACKED ingredients:

      barter icon   this town has shopkeepers who restock one of them (vanilla restocking-seller table, only known for
                    the eleven in ingredientdata.cpp), or an NPC actually standing here sells one (live)
      alchemy icon  the game data places raw ones (plants / loose) in this or a neighbouring cell

    Clicking the barter icon (menus only) toggles the shops block above the icons: one line per ingredient
    with its shops "Shop (keeper)" and any live sellers here. Clicking the alchemy icon turns it into a small
    3x3 map (north up, player in the middle with a compass arrow) of how many tracked raw ingredients each
    cell around holds (one figure per cell, summed over the tracked ingredients); a tiny coin marks a cell
    with a shop door or seller; the tracked ingredients' icons sit in a row right of the map. Clicking
    the map folds it back to the icon. Both toggles are remembered in settings.

    Display only: nothing is sent, nothing is touched. Settings: [Ingredients] enabled, show shops, show raw,
    tracked, recents, info width.
*/

#include <string>
#include <vector>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_Widget.h>

#include "ingredientdata.hpp"

namespace ESM { struct Ingredient; }

namespace MWWorld { class CellStore; }

namespace MWGui
{
    class Hotbar;

    class Ingredients
    {
    public:
        explicit Ingredients(Hotbar* hotbar);
        ~Ingredients();

        void onFrame(float dt);
        void setVisible(bool visible);
        bool isEnabled() const { return mEnabled; }

    private:
        void onShopsClicked(MyGUI::Widget* sender);
        void onRawClicked(MyGUI::Widget* sender);
        void onTrackClicked(MyGUI::Widget* sender);
        void onGridClicked(MyGUI::Widget* sender);
        void onTileClicked(MyGUI::Widget* sender);
        void onSearchChanged(MyGUI::EditBox* sender);
        void loadTracked();
        void saveTracked() const;
        void rebuildTable();
        void buildCatalogue();
        void rebuildPicker();
        void place();
        void scan();
        void updateText();

        bool mEnabled;
        int mInfoWidth;
        bool mShowShops, mShowRaw;      // panel toggles (remembered)
        bool mHudVisible;
        Hotbar* mHotbar;        // layout anchor: the icons start right of its page label
        MyGUI::ImageBox* mTrackIcon;    // mortar and pestle: opens the picker
        MyGUI::ImageBox* mShopsIcon;    // Menu-layer roots (clickable in menus)
        MyGUI::ImageBox* mRawIcon;
        MyGUI::Colour mNormalColour;    // the game's normal font colour: "no category"

        // ---- the picker ----
        struct CatalogueEntry
        {
            std::string id;             // record id, lower case
            std::string name;
            std::string icon;           // corrected icon path
            std::string tooltip;        // name + the four effects, with colour tags
            std::string searchText;     // lower-case name and effect names
            MyGUI::Colour colour, colour2;  // effect-category colours (equal when there is only one)
        };
        // colour = the leading category, colour2 = a second one (same as colour when there is only one)
        struct Look { std::string icon, tooltip; MyGUI::Colour colour, colour2; };
        Look lookOf(const ESM::Ingredient& rec) const;
        std::vector<CatalogueEntry> mCatalogue;     // every ingredient record, by name; built on first open
        std::vector<std::string> mTrackedIds;       // record ids, in the order they were added
        std::vector<std::string> mRecentIds;        // last picks, newest first
        MyGUI::Widget* mPicker;                     // Menu layer root, open only while a menu is up
        MyGUI::EditBox* mSearchEdit;
        std::vector<MyGUI::Widget*> mPickerWidgets; // everything rebuilt on each change (not the search box)
        bool isTracked(const std::string& id) const;
        const CatalogueEntry* catalogueEntry(const std::string& id) const;
        MyGUI::Widget* addTile(const CatalogueEntry& e, int x, int y, bool starred);
        /// a tile: colour rim split diagonally when two categories apply (plain = just the icon, no rim)
        MyGUI::ImageBox* makeTile(MyGUI::Widget* parent, int x, int y, int size, const Look& look, bool plain = false);

        // the tracked set as the scanner sees it: one row per tracked ingredient (a seller-table entry when there is
        // one, so its shops and colour come along; otherwise just the record)
        std::vector<IngredientDef> mTable;
        std::vector<bool> mTracked;     // per table row (all true; kept so the scan code reads naturally)
        std::vector<Look> mTableLook;   // per table row: icon, tooltip, colour

        MyGUI::EditBox* mInfo;          // HUD-layer shops block above the icons
        // the alchemy icon "opens" into a small 3x3 map of the cells around the player: each cell shows how
        // many tracked raw ingredients the game data places there (north up), a tiny coin marks a cell with a
        // shop selling one (a door into a known shop, or a seller standing there), the middle one is the
        // player's cell and carries a little compass arrow for the facing direction
        MyGUI::Widget* mGrid;           // Menu-layer root (click collapses it back to the icon)
        MyGUI::ImageBox* mGridBg[9];
        MyGUI::TextBox* mGridText[9];
        MyGUI::ImageBox* mGridCoin[9];
        MyGUI::ImageBox* mCompass;
        MyGUI::ImageBox* mGridIcon[3];  // the tracked ingredients, a row right of the map
        int mGridCount[9];
        bool mGridLoaded[9], mGridShop[9];

        // scan results, per table row
        struct Found
        {
            std::vector<std::string> shops;         // "Shop (keeper)" in this town, or "keeper (this shop)"
            std::vector<std::string> sellersHere;   // live NPCs here carrying it
            int raw;                                // static placements in this cell
            int nearby;                             // static placements in the loaded cells around (exteriors)
            Found() : raw(0), nearby(0) {}
        };
        std::vector<Found> mFound;
        bool mAnyShops, mAnyRaw;
        float mScanTimer;
        const MWWorld::CellStore* mLastCell;    // rescan at once when the player changes cell
        std::string mCellName;
        std::string mShopsText, mLastShopsText;
    };
}

#endif
