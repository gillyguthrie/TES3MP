#ifndef MWGUI_HOTBAR_H
#define MWGUI_HOTBAR_H

/*
    majere addition (hotbar)

    Persistent HUD strip mirroring quick-key slots 1-9 (slot 10 / hand-to-hand is omitted).
    Display only: it reads QuickKeysMenu's slot table and never writes to it, so the
    multiplayer quick-key packet path is untouched. A page switch on the server simply
    rewrites the slots, which bumps the menu's revision counter and triggers a redraw.

    Message boxes normally sit 48 px above the bottom edge; when the hotbar is enabled the
    WindowManager lifts them above the strip (see getMessageBoxLift).

    settings.cfg, section [Hotbar] (all optional, defaults in hotbar.cpp):
        enabled = true
        slot size = 38          (32 px icon art + border, crisp at 1:1; larger scales icons up)
        spacing = 6
        bottom offset = 12      (distance from the bottom edge to the strip)
        x offset = 0
        show key labels = true
*/

#include <string>
#include <vector>

#include <MyGUI_MouseButton.h>

#include "windowbase.hpp"

namespace MyGUI
{
    class ImageBox;
    class TextBox;
    class Widget;
}

namespace MWWorld
{
    class Ptr;
}

namespace MWGui
{
    class QuickKeysMenu;
    class ItemWidget;
    class DragAndDrop;

    class Hotbar : public WindowBase
    {
    public:
        Hotbar(QuickKeysMenu* menu, DragAndDrop* dragAndDrop);

        void onFrame(float dt) override;
        void onResChange(int width, int height) override;

        bool isEnabled() const { return mEnabled; }

        /// Extra bottom padding message boxes need so they stack above the strip (0 if disabled).
        int getMessageBoxLift() const;

        /// Brief highlight of a slot's background; index is 1-based like the quick keys themselves.
        void flashSlot(int index);

        /// Server-reported quick-key page number (a server script printing "Quick Key Page: N"); 0 hides it.
        void setPage(int page);

        /// Screen rectangle of the slot strip (for widgets placed next to it).
        MyGUI::IntCoord getStripCoord() const { return mRow ? mRow->getAbsoluteCoord() : MyGUI::IntCoord(); }
        /// Width taken by the "Page N" label right of the strip (0 when hidden).
        int getPageLabelWidth() const;
        /// Layout anchors for the other additions (majere): Azura's Star sits just left of the strip (EffectDials
        /// tells us its size), the ingredient icons start just right of the page label; both on the slot row.
        void setStarSize(int size) { mStarSize = size; }
        MyGUI::IntCoord getStarSlot() const;
        MyGUI::IntCoord getIconAnchor() const;

    private:
        int mStarSize;      // set by EffectDials
        static const int sSlotCount = 9;
        static const int sMargin = 3;       // flash halo around each slot

        struct Slot
        {
            MyGUI::ImageBox* flash;   // tinted "white" texture behind the icon, alpha 0 when idle
            ItemWidget* icon;
            MyGUI::TextBox* label;
            std::string refId;        // item id currently shown (empty for spells / unassigned)
            std::string iconPath;     // texture shown in the slot (for the binding-drag ghost)
            float flashTimer;
            Slot() : flash(nullptr), icon(nullptr), label(nullptr), flashTimer(0.f) {}
        };

        QuickKeysMenu* mMenu;
        DragAndDrop* mDragAndDrop;
        int mPendingDrop;      // slot index (0-based) to bind the dragged item to on the next frame, or -1
        int mPendingDialog;    // slot index to open the assign dialog for on the next frame, or -1

        // Binding drag: clicking a bound slot lifts its binding onto the cursor. The next left-click on a
        // slot puts it there; if that slot was occupied, the displaced binding is now on the cursor (keep
        // going). A left-click anywhere else drops the carried binding (clearing its source slot), and a
        // right-click cancels. A displaced binding that is cancelled or dropped off-slot is simply gone.
        bool mCarrying;                // a binding is on the cursor
        int mBindFrom;                 // slot the carried binding came from (still holds it), or -1 (displaced)
        int mCarryType;                // QuickKeysMenu::QuickKeyType of the carried binding
        std::string mCarryId;
        std::string mCarryIcon;
        int mPendingBindTo;            // queued drop: slot index, -2 = off-slot, -3 = cancel, -1 = nothing

        // A move is two quick-key packets (assign target, unassign source). The unassign is held back a
        // quarter of a second so the two arrive at human pace, never in the same frame (server rule).
        int mDeferredClear;            // slot to unassign once the timer runs out, or -1
        float mDeferredClearTimer;
        void flushDeferredClear();     // send a pending unassign now (before any new packet-producing action)
        MyGUI::Widget* mBindOverlay;   // full-screen catcher (Popup layer) that receives the drop click
        ItemWidget* mBindGhost;        // icon following the cursor (DragAndDrop layer)
        void startBindDrag(int index);
        void carryBinding(int type, const std::string& id, const std::string& icon);   // (re)arm the ghost
        void endBindDrag();
        void onOverlayPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton button);
        void assignBinding(int index, int type, const std::string& id);   // via WindowManager::setQuickKey
        std::string mLastSpellIconPath;   // set by applySpellIcon, read by rebuildSlot for the ghost
        MyGUI::Widget* mRow;
        MyGUI::TextBox* mPageText;  // its own (non-pickable) root in the Menu layer, right of the strip
        int mPage;
        std::vector<Slot> mSlots;

        bool mEnabled;
        int mSlotSize;
        int mSpacing;
        int mBottomOffset;
        int mXOffset;
        bool mShowLabels;

        unsigned int mLastRevision;
        float mCountTimer;

        /// Menus only: left-click while dragging an item assigns it to the slot (same path as the F1 menu,
        /// so the usual quick-key packet is sent); a plain left-click opens the game's assign dialog for the
        /// slot (right-click can't be used: the input manager treats it as "close menu" before widgets see it).
        void onSlotPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton button);
        void processPending();   // runs the queued drop / dialog request outside MyGUI's input dispatch

        void layoutRow();
        void rebuildAll();
        void rebuildSlot(int i);
        void clearSlot(int i);
        void refreshCounts();

        MWWorld::Ptr findInventoryItem(const std::string& refId, int& countOut) const;
        void applyItemIcon(ItemWidget* widget, const MWWorld::Ptr& item, bool magicItem);
        bool applySpellIcon(ItemWidget* widget, const std::string& spellId);
    };
}

#endif
