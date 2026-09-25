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
    decides; with none of those it keeps the normal font colour. Then two icons that only appear
    when the current cell has something for the TRACKED ingredients:

      barter icon   this town has shopkeepers who restock one of them (vanilla restocking-seller table, only known for
                    the eleven in ingredientdata.cpp), or an NPC actually standing here sells one (live)
      alchemy icon  the game data places raw ones (plants / loose) in this or a neighbouring cell

    Clicking the barter icon (menus only) toggles the shops block above the icons: one line per ingredient
    with its shops "Shop (keeper)" and any live sellers here. Clicking the alchemy icon turns it into a small
    3x3 map (north up, player in the middle with a compass arrow) of how many tracked raw ingredients each
    cell around holds (one figure per cell, summed over the tracked ingredients); a tiny coin marks a cell
    with a shop door or seller; the tracked ingredients' icons sit in a row right of the map. Clicking
    the map folds it back to the icon. Both toggles are remembered in settings. The facing arrow in the
    middle cell also sits where the player stands within that cell. The tracked icons beside the map carry
    the count of that ingredient in the inventory. A small "Map" button under the 3x3 map opens a copy of
    the whole world map with the count of tracked plants written on every cell that has any (every exterior
    cell's object list is read once for that; a few seconds, spread over frames); any click closes it.

    Display only: nothing is sent, nothing is touched. Settings: [Ingredients] enabled, show shops, show raw,
    tracked, recents, info width.
*/

#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_Widget.h>

#include "ingredientdata.hpp"

namespace ESM { struct Ingredient; }

namespace MWWorld { class CellStore; }
namespace MyGUI { class ITexture; }

namespace MWGui
{
    class Hotbar;
    class MapWindow;

    class Ingredients
    {
    public:
        explicit Ingredients(Hotbar* hotbar);
        ~Ingredients();
        void setMapWindow(MapWindow* map) { mMapWindow = map; }

        void onFrame(float dt);
        void setVisible(bool visible);
        bool isEnabled() const { return mEnabled; }

    private:
        void onShopsClicked(MyGUI::Widget* sender);
        void onRawClicked(MyGUI::Widget* sender);
        void onTrackClicked(MyGUI::Widget* sender);
        void onGridClicked(MyGUI::Widget* sender);
        void onIconRowClicked(MyGUI::Widget* sender);
        void onMapClicked(MyGUI::Widget* sender);
        void onWorldCloseClicked(MyGUI::Widget* sender);
        void openWorldMap();
        void closeWorldMap();
        void worldScanStep();
        void updateWorldArrow();
        void layoutWorldMap();          // size and place the big map (fit, or the user's own scale and spot)
        // the picker and the big map can be dragged by their frame; the big map's corner grip rescales it
        void onFramePressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onFrameDragged(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onGripPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onGripDragged(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        MyGUI::IntPoint mDragOffset;
        MyGUI::Widget* mWorldGrip;
        bool mPickerMoved, mWorldMoved;         // the user put it somewhere: keep it there ([Ingredients] picker x/y, world map x/y)
        MyGUI::IntPoint mPickerPos, mWorldPos;
        float mWorldUserScale;                  // 0 = fit the screen; else the user's ([Ingredients] world map scale)
        // leaving the menu hides them; opening the inventory again brings back whatever was open
        bool mGuiLast, mPickerReopen, mWorldReopen;
        std::string worldCellName(int x, int y) const;
        int keeperStock(const std::string& npc, const std::vector<std::string>& ids);   // what the keeper's record restocks
        std::map<std::string, int> mStockCache;
        void rebuildWorldCells();
        int inventoryCount(const IngredientDef& def) const;
        int inventoryCount(const std::vector<std::string>& ids) const;   // the same for any record list
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
        Hotbar* mHotbar;                // layout anchor: the icons start right of its page label
        MyGUI::ImageBox* mTrackIcon;    // mortar and pestle: opens the picker
        MyGUI::ImageBox* mShopsIcon;    // Menu-layer roots (clickable in menus)
        MyGUI::ImageBox* mRawIcon;
        MyGUI::Colour mNormalColour;    // the game's normal font colour: "no category"

        // ---- the picker ----
        struct CatalogueEntry
        {
            std::string id;             // record id, lower case (the first record of this name)
            std::vector<std::string> ids;   // every record with this name (mods and expansions repeat names)
            std::string name;
            std::string icon;           // corrected icon path
            std::string tooltip;        // name + the four effects, with colour tags
            std::string searchText;     // lower-case name and effect names
            MyGUI::Colour colour, colour2;  // effect-category colours (equal when there is only one)
        };
        // colour = the leading category, colour2 = a second one (same as colour when there is only one)
        struct Look { std::string icon, tooltip; MyGUI::Colour colour, colour2; };
        Look lookOf(const ESM::Ingredient& rec) const;
        std::vector<CatalogueEntry> mCatalogue;     // one entry per ingredient NAME, by name; built at start
        std::vector<std::string> mTrackedIds;       // record ids, in the order they were added
        std::vector<std::string> mRecentIds;        // last picks, newest first
        MyGUI::Widget* mPicker;                     // Menu layer root, open only while a menu is up
        MyGUI::EditBox* mSearchEdit;
        MyGUI::Button* mPickerClose;    // top right of the picker; persistent like the search box
        MyGUI::Button* mPickerMap;      // beside it: the world map
        std::vector<MyGUI::Widget*> mPickerWidgets; // everything rebuilt on each change (not the search box)
        bool isTracked(const std::string& id) const;
    public:
        /// For the game's local map (mapwindow.cpp): the tracked ingredients sold in an interior cell, one
        /// "Saltrice (20): keeper" line each; empty when the cell is no tracked shop (or nothing is tracked).
        static std::vector<std::string> shopLines(const std::string& interiorCell);
        /// Changes whenever the tracked set does, so the local map knows to redo its door coins.
        static const std::string& trackedSignature();
        /// The tracked plants in the cells round the player, for the local map's dots: world position and the
        /// ingredient's colour. Live by default (only what still stands on this client, which is the server's
        /// word: harvests by anyone arrive as object changes when the cell loads, a cell reset reloads it);
        /// One switch, the map window's Plants button ([Ingredients] plants mode), governs the dots AND the cell
        /// grid's inner nine counts: dynamic = what still stands, static = every placement, off = no dots
        /// (static counts).
        struct PlantDot { float x, y; MyGUI::Colour colour; };
        static const std::vector<PlantDot>& plantDots();
        static unsigned int plantDotsVersion();     // goes up whenever the dots change
        enum PlantsMode { Plants_Off = 0, Plants_Dynamic = 1, Plants_Static = 2 };
        static int plantsMode();
        static void cyclePlantsMode();          // dynamic -> static -> off -> dynamic
        static std::string plantsModeLabel();   // the button's caption
    private:
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
        // 25 slots, a 5x5 block with north on the top row and the player's cell at slot 12; the 3x3 view shows
        // the inner nine. A click on the grid goes 3x3 -> 5x5 -> folded icon ([Ingredients] grid big).
        MyGUI::ImageBox* mGridBg[25];
        MyGUI::TextBox* mGridText[25];
        MyGUI::ImageBox* mGridCoin[25];
        bool mGridBig;
        void layoutGrid();
        int gridPixels() const;                 // side of the grid as shown now
        bool gridSlotShown(int slot) const;
        // the outer ring's cells are not around the player: their static counts are read once and kept
        struct RingCell { std::vector<int> raw; bool shopDoor; std::vector<std::string> shops; int total; };
        std::map<std::pair<int, int>, RingCell> mRingCache;   // cell -> static placements per row, shop door, shop lines
        // what each grid cell's tooltip says (the big map's tooltip, cell for cell)
        struct GridInfo
        {
            bool known, live, exterior;
            int x, y;
            std::vector<int> placed, standing;      // per table row
            std::vector<std::string> shops;         // "Saltrice (20): shop (keeper)"
            GridInfo() : known(false), live(false), exterior(true), x(0), y(0) {}
        };
        GridInfo mGridInfo[25];
        std::string mGridTipTitle[25], mGridTipBody[25], mGridAudit;
        void updateGridTips();
        std::vector<std::string> shopLinesForDoors(const std::vector<std::string>& dests);
        bool mRingPending;                      // some ring cells are still to be read: the next scan comes soon
        MyGUI::ImageBox* mCompass;
        MyGUI::Widget* mIconRow;        // Menu-layer root for the tracked icons, right of the map on the mortar's line
        MyGUI::ImageBox* mGridIcon[3];  // the tracked ingredients, a row right of the map
        MyGUI::TextBox* mGridQty[3];    // how many of each are in the inventory
        // the world map: a copy of the game's global map with per-cell counts of the tracked plants
        MapWindow* mMapWindow;
        MyGUI::Button* mMapButton;
        MyGUI::Widget* mWorldMap;       // Windows-layer root; stays open until its Close button (or leaving the menu)
        MyGUI::ImageBox* mWorldImage;
        MyGUI::TextBox* mWorldStatus;
        MyGUI::Button* mWorldClose;
        MyGUI::Button* mWorldPick;      // opens the picker over the map (the mortar icon is under the map while it is open)
        std::unique_ptr<MyGUI::ITexture> mWorldTexture;
        std::vector<MyGUI::Widget*> mWorldCells;
        MyGUI::ImageBox* mWorldArrow;   // the player's facing arrow on the big map (one of mWorldCells)
        bool mWorldScanning;
        std::vector<std::pair<int, int>> mWorldQueue;
        size_t mWorldNext;
        // the insides: every interior cell is read too (loose items and container contents, for the
        // ingredients that do not grow, moon sugar first of all) and credited to the exterior cell its
        // way out opens onto, through as many connecting interiors as it takes
        struct InteriorFind
        {
            std::string name;                               // the interior's name as the game shows it
            std::vector<int> raw;                           // per table row: loose stacks + container contents
            std::vector<std::string> exitsInterior;         // doors into other interiors (lower-case names)
            std::vector<std::pair<int, int>> exitsExterior; // doors out, as grid cells
        };
        std::vector<std::string> mWorldInteriorQueue;       // interior cell names
        size_t mWorldInteriorNext;
        std::map<std::string, InteriorFind> mWorldInteriors;
        std::map<std::pair<int, int>, std::vector<std::pair<std::string, std::vector<int>>>> mWorldInside;   // exterior cell -> (interior name, per-row counts) for each interior opening onto it
        void creditInteriors();
        int insideTotal(int x, int y) const;
        float mWorldScale;              // the map image is shrunk when it would not fit above the icon row
        std::map<std::pair<int, int>, int> mWorldCounts;
        std::map<std::pair<int, int>, std::vector<std::string>> mWorldShops;   // cell -> "Ingredient: shop (keeper)" lines
        std::map<std::pair<int, int>, std::vector<int>> mWorldBreakdown;       // cell -> count per table row
        std::string mWorldSignature;    // the tracked set the counts were made for
        static Ingredients* sInstance;
        std::vector<PlantDot> mDots;
        unsigned int mDotsVersion;
        int mPlantsMode;
        bool mGridLive[25];             // this slot's count is "still standing" (dynamic mode, loaded cells only)
        int mHereStanding, mHerePlaced; // the player's own cell, for the grid's tooltip
        std::string mGridTip;
        std::string mDotsAudit;         // last "standing N of M" line sent to the session log
        int mGridCount[25];
        bool mGridLoaded[25], mGridShop[25];

        // scan results, per table row
        struct Found
        {
            std::vector<std::pair<int, std::string>> shops;   // keeper's restock qty, "Shop (keeper)" or "keeper (this shop)"
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
