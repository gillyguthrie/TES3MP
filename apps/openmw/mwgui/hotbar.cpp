/*
    majere addition (hotbar) -- see hotbar.hpp
*/
#include "hotbar.hpp"

#include <algorithm>

#include <MyGUI_Gui.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ITexture.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_Widget.h>

#include <components/esm/loadmgef.hpp>
#include <components/esm/loadspel.hpp>
#include <components/misc/stringops.hpp>
#include <components/settings/settings.hpp>
#include <components/debug/debuglog.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/ptr.hpp"

#include <components/esm/loadench.hpp>

#include "../mwgui/mode.hpp"

#include "draganddrop.hpp"
#include "itemmodel.hpp"
#include "itemwidget.hpp"
#include "quickkeysmenu.hpp"

namespace
{
    // [Hotbar] has no entry in defaults.bin, and Settings::Manager throws for unknown keys.
    int settingInt(const char* key, int def)
    {
        try { return Settings::Manager::getInt(key, "Hotbar"); } catch (...) { return def; }
    }
    bool settingBool(const char* key, bool def)
    {
        try { return Settings::Manager::getBool(key, "Hotbar"); } catch (...) { return def; }
    }

    const float sFlashSeconds = 0.35f;
    const float sFlashAlpha = 0.75f;
    const float sCountInterval = 0.25f;
    const int sMessageBoxDefaultPadding = 48;   // MessageBox::mBottomPadding in messagebox.cpp
    const int sLabelH = 16;                     // key-number line above the icons (game's own font)
    const int sPad = 0;                         // row has no frame; the flash halo (sMargin) is the only inset

    std::string itemIconPath(const MWWorld::Ptr& item)
    {
        std::string icon = item.getClass().getInventoryIcon(item);
        if (icon.empty())
            icon = "default icon.tga";
        return MWBase::Environment::get().getWindowManager()->correctIconPath(icon);
    }
}

namespace MWGui
{
    Hotbar::Hotbar(QuickKeysMenu* menu, DragAndDrop* dragAndDrop)
        : WindowBase("openmw_hotbar.layout"), mStarSize(0)
        , mMenu(menu)
        , mDragAndDrop(dragAndDrop)
        , mPendingDrop(-1)
        , mPendingDialog(-1)
        , mCarrying(false)
        , mDeferredClear(-1)
        , mDeferredClearTimer(0.f)
        , mBindFrom(-1)
        , mCarryType(0)
        , mPendingBindTo(-1)
        , mBindOverlay(nullptr)
        , mBindGhost(nullptr)
        , mRow(nullptr)
        , mPageText(nullptr), mPage(0)
        , mEnabled(true)
        , mSlotSize(38)
        , mSpacing(6)
        , mBottomOffset(12)
        , mXOffset(0)
        , mShowLabels(true)
        , mLastRevision(0)
        , mCountTimer(0.f)
    {
        mEnabled      = settingBool("enabled", true);
        mSlotSize     = std::max(24, settingInt("slot size", 38));
        mSpacing      = std::max(0, settingInt("spacing", 6));
        mBottomOffset = std::max(0, settingInt("bottom offset", 12));
        mXOffset      = settingInt("x offset", 0);
        mShowLabels   = settingBool("show key labels", true);

        // The root is sized to the strip itself (not the whole screen) and is pickable, like the HUD's
        // own root: MyGUI only picks children of pickable widgets, and a full-screen pickable root would
        // swallow every click. Clicks in the gaps between slots simply do nothing.
        mMainWidget->setNeedMouseFocus(true);

        getWidget(mRow, "Row");
        getWidget(mPageText, "PageText");
        mRow->setNeedMouseFocus(true);
        mPageText->detachFromWidget("Menu");   // own root: the strip's pickable root ends at the slots
        mPageText->setNeedMouseFocus(false);
        mPageText->setVisible(false);

        mSlots.resize(sSlotCount);
        const int labelH = mShowLabels ? sLabelH + 2 : 0;

        for (int i = 0; i < sSlotCount; ++i)
        {
            Slot& slot = mSlots[i];
            const int x = sPad + i * (mSlotSize + mSpacing);
            const int y = sPad + labelH;

            // Created before the icon so it renders beneath it: a gold box a little larger than the slot,
            // faded in/out as the press highlight (shows through the icon's transparent parts and the margin).
            slot.flash = mRow->createWidget<MyGUI::ImageBox>("ImageBox",
                MyGUI::IntCoord(x, y, mSlotSize + 2 * sMargin, mSlotSize + 2 * sMargin), MyGUI::Align::Default);
            // Solid gold fill the size of the slot plus its halo margin (the game's equip frame texture is only
            // an outline, which read as a thin ring around the icon rather than the whole box lighting up).
            slot.flash->setImageTexture("white");
            slot.flash->setColour(MyGUI::Colour(0.95f, 0.78f, 0.35f));
            slot.flash->setAlpha(0.f);
            slot.flash->setNeedMouseFocus(false);

            slot.icon = mRow->createWidget<ItemWidget>("MW_ItemIconHotbar",   // full-bleed variant, see openmw_resources.xml
                MyGUI::IntCoord(x + sMargin, y + sMargin, mSlotSize, mSlotSize), MyGUI::Align::Default);
            // clickable so items can be dropped on it while a menu is open (the HUD gets no mouse input in play)
            slot.icon->setNeedMouseFocus(true);
            slot.icon->setUserString("HotbarSlot", std::to_string(i));   // user data is kept for the tooltip's Ptr
            slot.icon->setUserString("ToolTipType", "");
            slot.icon->eventMouseButtonPressed += MyGUI::newDelegate(this, &Hotbar::onSlotPressed);

            // Key number centred above its slot, in the game's header (gold) font.
            slot.label = mRow->createWidget<MyGUI::TextBox>("SandBrightText",
                MyGUI::IntCoord(x + sMargin, sPad, mSlotSize, sLabelH), MyGUI::Align::Default);
            slot.label->setCaption(std::to_string(i + 1));
            slot.label->setTextAlign(MyGUI::Align::Center);
            slot.label->setTextShadow(true);
            slot.label->setNeedMouseFocus(false);
            slot.label->setVisible(mShowLabels);
        }

        layoutRow();
        rebuildAll();
    }

    int Hotbar::getMessageBoxLift() const
    {
        if (!mEnabled)
            return 0;
        // message box bottom must clear the row's top (bottom offset + row height + a gap)
        const int rowH = sPad + (mShowLabels ? sLabelH + 2 : 0) + mSlotSize + 2 * sMargin + sPad;
        const int wanted = mBottomOffset + rowH + 8;
        return std::max(0, wanted - sMessageBoxDefaultPadding);
    }

    void Hotbar::layoutRow()
    {
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        const int rowW = sPad + sSlotCount * mSlotSize + (sSlotCount - 1) * mSpacing + 2 * sMargin + sPad;
        const int rowH = sPad + (mShowLabels ? sLabelH + 2 : 0) + mSlotSize + 2 * sMargin + sPad;
        const int rowX = (view.width - rowW) / 2 + mXOffset;
        const int rowY = view.height - mBottomOffset - rowH;
        mMainWidget->setCoord(rowX, rowY, rowW, rowH);
        mRow->setCoord(0, 0, rowW, rowH);
        mPageText->setCoord(rowX + rowW + 10, rowY + rowH / 2 - 10, 140, 20);   // detached root: screen coords
    }

    int Hotbar::getPageLabelWidth() const
    {
        return (mPageText && mPageText->getVisible()) ? mPageText->getTextSize().width + 10 : 0;
    }

    MyGUI::IntCoord Hotbar::getStarSlot() const
    {
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        int stripLeft = view.width / 2 - 200, centreY = view.height - 30;
        if (mRow)
        {
            const MyGUI::IntCoord strip = getStripCoord();
            stripLeft = strip.left;
            centreY = strip.top + strip.height - 22;   // slot row = 38 px slots + 3 px halo each side
        }
        return MyGUI::IntCoord(stripLeft - 14 - mStarSize, centreY - mStarSize / 2, mStarSize, mStarSize);
    }

    MyGUI::IntCoord Hotbar::getIconAnchor() const
    {
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        int gapLeft = view.width / 2 + 200, centreY = view.height - 30;
        if (mRow)
        {
            const MyGUI::IntCoord strip = getStripCoord();
            gapLeft = strip.right() + getPageLabelWidth();
            centreY = strip.top + strip.height - 22;
        }
        return MyGUI::IntCoord(gapLeft + 14, centreY - 12, 0, 24);
    }

    void Hotbar::onResChange(int /*width*/, int /*height*/)
    {
        layoutRow();
    }

    void Hotbar::onFrame(float dt)
    {
        processPending();
        if (mPageText)
            mPageText->setVisible(mEnabled && isVisible() && mPage > 0);   // detached root: follows the strip by hand
        if (!mEnabled || !isVisible())
            return;

        const unsigned int revision = mMenu->getRevision();
        if (revision != mLastRevision)
        {
            mLastRevision = revision;
            rebuildAll();
        }

        mCountTimer += dt;
        if (mCountTimer >= sCountInterval)
        {
            mCountTimer = 0.f;
            refreshCounts();
        }

        for (int i = 0; i < sSlotCount; ++i)
        {
            Slot& slot = mSlots[i];
            if (slot.flashTimer <= 0.f)
                continue;
            slot.flashTimer -= dt;
            if (slot.flashTimer <= 0.f)
            {
                slot.flashTimer = 0.f;
                slot.flash->setAlpha(0.f);
            }
            else
                slot.flash->setAlpha(sFlashAlpha * (slot.flashTimer / sFlashSeconds));   // fade out
        }
    }

    void Hotbar::rebuildAll()
    {
        for (int i = 0; i < sSlotCount; ++i)
            rebuildSlot(i);
    }

    void Hotbar::clearSlot(int i)
    {
        Slot& slot = mSlots[i];
        slot.refId.clear();
        slot.iconPath.clear();
        slot.icon->setItem(MWWorld::Ptr());
        slot.icon->setCount(1); // empty caption
        slot.icon->setUserString("ToolTipType", "");
    }

    void Hotbar::rebuildSlot(int i)
    {
        if (i < 0 || i >= sSlotCount || i >= mMenu->getSlotCount())
            return;

        Slot& slot = mSlots[i];
        const QuickKeysMenu::QuickKeyType type = mMenu->getSlotType(i);
        const std::string& id = mMenu->getSlotId(i);

        switch (type)
        {
            case QuickKeysMenu::Type_Item:
            case QuickKeysMenu::Type_MagicItem:
            {
                int count = 0;
                MWWorld::Ptr item = findInventoryItem(id, count);
                if (item.isEmpty())
                {
                    clearSlot(i);
                    return;
                }
                slot.refId = id;
                slot.iconPath = itemIconPath(item);
                applyItemIcon(slot.icon, item, type == QuickKeysMenu::Type_MagicItem);
                slot.icon->setCount(count);
                // the game's own item tooltip while a menu is open (same as the quick-key menu's buttons)
                slot.icon->setUserString("ToolTipType", "ItemPtr");
                slot.icon->setUserData(item);
                return;
            }
            case QuickKeysMenu::Type_Magic:
                slot.refId.clear();
                if (!applySpellIcon(slot.icon, id))
                    clearSlot(i);
                else
                {
                    slot.iconPath = mLastSpellIconPath;
                    slot.icon->setUserString("ToolTipType", "Spell");
                    slot.icon->setUserString("Spell", id);
                }
                return;
            default:
                clearSlot(i);
                return;
        }
    }

    void Hotbar::refreshCounts()
    {
        for (int i = 0; i < sSlotCount; ++i)
        {
            Slot& slot = mSlots[i];
            if (slot.refId.empty())
                continue;
            int count = 0;
            MWWorld::Ptr item = findInventoryItem(slot.refId, count);
            if (item.isEmpty())
                rebuildSlot(i);     // item gone: show the empty frame
            else
            {
                slot.icon->setCount(count);
                slot.icon->setUserData(item);   // keep the tooltip's Ptr current as stacks come and go
            }
        }
    }

    MWWorld::Ptr Hotbar::findInventoryItem(const std::string& refId, int& countOut) const
    {
        countOut = 0;
        MWWorld::Ptr first;
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty())
            return first;

        MWWorld::InventoryStore& inventory = player.getClass().getInventoryStore(player);
        for (MWWorld::ContainerStoreIterator it = inventory.begin(); it != inventory.end(); ++it)
        {
            if (!Misc::StringUtils::ciEqual(it->getCellRef().getRefId(), refId))
                continue;
            if (first.isEmpty())
                first = *it;
            countOut += it->getRefData().getCount();
        }
        return first;
    }

    // Icon only, like the HUD's own weapon/spell boxes: the slot skin already draws the square,
    // so the coloured "item state" frame the quick-key menu adds would be a box inside a box.
    void Hotbar::applyItemIcon(ItemWidget* widget, const MWWorld::Ptr& item, bool /*magicItem*/)
    {
        widget->setItem(MWWorld::Ptr());   // clears any previous frame/shadow
        widget->setIcon(item);
    }


    // Mirrors QuickKeysMenu::onAssignMagic: icon of the spell's first effect, "b_" (big) variant.
    bool Hotbar::applySpellIcon(ItemWidget* widget, const std::string& spellId)
    {
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const ESM::Spell* spell = store.get<ESM::Spell>().search(spellId);
        if (!spell || spell->mEffects.mList.empty())
            return false;
        const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(spell->mEffects.mList.front().mEffectID);
        if (!effect)
            return false;

        std::string path = effect->mIcon;
        size_t slashPos = path.rfind('\\');
        path.insert(slashPos == std::string::npos ? 0 : slashPos + 1, "b_");
        path = MWBase::Environment::get().getWindowManager()->correctIconPath(path);
        mLastSpellIconPath = path;

        widget->setItem(MWWorld::Ptr());   // no inner frame (see applyItemIcon)
        widget->setIcon(path);
        widget->setCount(1);
        return true;
    }

    void Hotbar::onSlotPressed(MyGUI::Widget* sender, int /*left*/, int /*top*/, MyGUI::MouseButton button)
    {
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
        const std::string& slotStr = sender->getUserString("HotbarSlot");
        const int index = slotStr.empty() ? -1 : std::atoi(slotStr.c_str());   // 0-based
        if (!mEnabled || !wm->isGuiMode() || index < 0 || index >= sSlotCount)
            return;

        // Only queue here: changing GUI modes or ending a drag inside MyGUI's mouse dispatch leaves the
        // input system confused. processPending() does the work on the next frame.
        if (button != MyGUI::MouseButton::Left)
            return;   // right-click closes menus at the input-manager level and never reaches widgets
        if (mCarrying)
            return;                    // the overlay handles clicks while a binding is carried
        if (mDragAndDrop && mDragAndDrop->mIsOnDragAndDrop)
            mPendingDrop = index;      // drop the dragged item onto this slot
        else if (mMenu->getSlotType(index) != QuickKeysMenu::Type_Unassigned)
            startBindDrag(index);      // lift this slot's binding onto the cursor
        else
            mPendingDialog = index;    // empty slot: open the game's assign dialog for it
    }

    void Hotbar::startBindDrag(int index)
    {
        endBindDrag();
        mBindFrom = index;
        carryBinding(mMenu->getSlotType(index), mMenu->getSlotId(index), mSlots[index].iconPath);
        flashSlot(index + 1);
    }

    void Hotbar::carryBinding(int type, const std::string& id, const std::string& icon)
    {
        mCarrying = true;
        mCarryType = type;
        mCarryId = id;
        mCarryIcon = icon;

        if (!mBindOverlay)
        {
            // transparent full-screen catcher above the windows: the next click anywhere lands here
            const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
            mBindOverlay = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>("",
                MyGUI::IntCoord(0, 0, view.width, view.height), MyGUI::Align::Default, "Popup");
            mBindOverlay->setNeedMouseFocus(true);
            mBindOverlay->eventMouseButtonPressed += MyGUI::newDelegate(this, &Hotbar::onOverlayPressed);
        }
        if (!mBindGhost)
        {
            // ghost icon following the cursor, in the game's own drag layer
            mBindGhost = MyGUI::Gui::getInstance().createWidget<ItemWidget>("MW_ItemIconHotbar",
                MyGUI::IntCoord(0, 0, mSlotSize, mSlotSize), MyGUI::Align::Default, "DragAndDrop");
            mBindGhost->setNeedMouseFocus(false);
        }
        mBindGhost->setItem(MWWorld::Ptr());
        if (!icon.empty())
            mBindGhost->setIcon(icon);
        mBindGhost->setPosition(MyGUI::InputManager::getInstance().getMousePosition() - MyGUI::IntPoint(mSlotSize / 2, mSlotSize / 2));
    }

    void Hotbar::endBindDrag()
    {
        mCarrying = false;
        mBindFrom = -1;
        mCarryId.clear();
        mCarryIcon.clear();
        if (mBindOverlay) { MyGUI::Gui::getInstance().destroyWidget(mBindOverlay); mBindOverlay = nullptr; }
        if (mBindGhost)   { MyGUI::Gui::getInstance().destroyWidget(mBindGhost);   mBindGhost = nullptr; }
    }

    void Hotbar::onOverlayPressed(MyGUI::Widget* /*sender*/, int left, int top, MyGUI::MouseButton button)
    {
        if (button != MyGUI::MouseButton::Left)
        {
            mPendingBindTo = -3;   // anything else cancels (resolved next frame)
            return;
        }
        // Where did the click land? A slot moves/swaps the binding; anywhere else clears it
        // (right-click, handled above, is the cancel).
        const MyGUI::IntPoint pt(left, top);
        int target = -2;   // clear
        for (int i = 0; i < sSlotCount; ++i)
            if (mSlots[i].icon->getAbsoluteCoord().inside(pt))
            {
                target = i;
                break;
            }
        mPendingBindTo = target;   // acted on next frame, outside MyGUI's dispatch
    }

    void Hotbar::assignBinding(int index, int type, const std::string& id)
    {
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
        if (type == QuickKeysMenu::Type_Magic)
            wm->setQuickKey(index + 1, type, MWWorld::Ptr(), id);
        else if (type == QuickKeysMenu::Type_Item || type == QuickKeysMenu::Type_MagicItem)
        {
            int count = 0;
            MWWorld::Ptr item = findInventoryItem(id, count);
            if (item.isEmpty())
                wm->setQuickKey(index + 1, QuickKeysMenu::Type_Unassigned, MWWorld::Ptr());
            else
                wm->setQuickKey(index + 1, type, item);
        }
        else
            wm->setQuickKey(index + 1, QuickKeysMenu::Type_Unassigned, MWWorld::Ptr());
    }

    void Hotbar::flushDeferredClear()
    {
        if (mDeferredClear < 0)
            return;
        const int slot = mDeferredClear;
        mDeferredClear = -1;
        mDeferredClearTimer = 0.f;
        assignBinding(slot, QuickKeysMenu::Type_Unassigned, "");
        Log(Debug::Info) << "Hotbar: cleared slot " << (slot + 1) << " (deferred)";
    }

    void Hotbar::processPending()
    {
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();

        // held-back second packet of a move (see mDeferredClear)
        if (mDeferredClear >= 0)
        {
            mDeferredClearTimer -= MWBase::Environment::get().getFrameDuration();
            if (mDeferredClearTimer <= 0.f)
                flushDeferredClear();
        }

        // binding drag bookkeeping (all outside MyGUI's mouse dispatch)
        if (mCarrying && !wm->isGuiMode())
        {
            endBindDrag();            // menus closed under us: abandon the drag (source slot untouched)
            mPendingBindTo = -1;
        }
        if (mCarrying && mPendingBindTo != -1)
        {
            const int from = mBindFrom;
            const int to = mPendingBindTo;
            const int type = mCarryType;
            const std::string id = mCarryId;
            mPendingBindTo = -1;

            if (to == -3 || to == from)
            {
                endBindDrag();        // cancel / put back: nothing changes (a displaced binding is gone)
            }
            else if (to == -2)
            {
                // off-slot drop: the carried binding is discarded; its source slot (if any) is cleared
                endBindDrag();
                if (from >= 0)
                {
                    flushDeferredClear();
                    assignBinding(from, QuickKeysMenu::Type_Unassigned, "");
                    Log(Debug::Info) << "Hotbar: cleared slot " << (from + 1);
                }
            }
            else
            {
                // drop on a slot: place the carried binding there; clear its source; if the target was
                // occupied, the displaced binding is now on the cursor -- all via the F1 menu's own path
                const int dstType = mMenu->getSlotType(to);
                const std::string dstId = mMenu->getSlotId(to);
                const std::string dstIcon = mSlots[to].iconPath;
                flushDeferredClear();            // never two packets in one frame
                assignBinding(to, type, id);
                if (from >= 0)
                {
                    mDeferredClear = from;       // second packet follows ~0.25 s later
                    mDeferredClearTimer = 0.25f;
                }
                flashSlot(to + 1);
                if (dstType != QuickKeysMenu::Type_Unassigned)
                {
                    mBindFrom = -1;
                    carryBinding(dstType, dstId, dstIcon);
                    Log(Debug::Info) << "Hotbar: placed on slot " << (to + 1) << ", now carrying its previous binding";
                }
                else
                {
                    endBindDrag();
                    Log(Debug::Info) << "Hotbar: placed on slot " << (to + 1);
                }
            }
        }
        else if (mBindGhost)
            mBindGhost->setPosition(MyGUI::InputManager::getInstance().getMousePosition() - MyGUI::IntPoint(mSlotSize / 2, mSlotSize / 2));

        if (mPendingDrop < 0 && mPendingDialog < 0)
            return;

        if (mPendingDrop >= 0)
        {
            const int index = mPendingDrop;
            mPendingDrop = -1;
            if (mDragAndDrop && mDragAndDrop->mIsOnDragAndDrop && wm->isGuiMode())
            {
                // Bind the dragged item. Cast-on-use / cast-once enchantments bind as "magic" (what the
                // F1 menu's Magic button would do), everything else as an item. The binding goes through
                // the same setQuickKey path the server packets use, so the normal quick-key packet is sent.
                MWWorld::Ptr item = mDragAndDrop->mItem.mBase;
                if (!item.isEmpty())
                {
                    int type = QuickKeysMenu::Type_Item;
                    const std::string& enchantId = item.getClass().getEnchantment(item);
                    if (!enchantId.empty())
                    {
                        const ESM::Enchantment* ench = MWBase::Environment::get().getWorld()->getStore().get<ESM::Enchantment>().search(enchantId);
                        if (ench && (ench->mData.mType == ESM::Enchantment::WhenUsed || ench->mData.mType == ESM::Enchantment::CastOnce))
                            type = QuickKeysMenu::Type_MagicItem;
                    }
                    mDragAndDrop->finish();          // end the drag first; the item never left the inventory
                    wm->changePointer("arrow");
                    flushDeferredClear();
                    wm->setQuickKey(index + 1, type, item);
                    flashSlot(index + 1);
                    Log(Debug::Info) << "Hotbar: bound " << item.getCellRef().getRefId() << " to slot " << (index + 1)
                                     << (type == QuickKeysMenu::Type_MagicItem ? " (magic)" : " (item)");
                }
            }
        }

        if (mPendingDialog >= 0)
        {
            const int index = mPendingDialog;
            mPendingDialog = -1;
            if (wm->isGuiMode() && !(mDragAndDrop && mDragAndDrop->mIsOnDragAndDrop))
            {
                // the game's own per-slot dialog (Item / Magic / Unassign) -- also how spells get bound
                if (wm->getMode() != GM_QuickKeysMenu)
                    wm->pushGuiMode(GM_QuickKeysMenu);
                mMenu->openAssignDialogForSlot(index);
            }
        }
    }

    void Hotbar::flashSlot(int index)
    {
        if (!mEnabled || index < 1 || index > sSlotCount)
            return;
        Slot& slot = mSlots[index - 1];
        slot.flashTimer = sFlashSeconds;
        slot.flash->setAlpha(sFlashAlpha);
    }

    void Hotbar::setPage(int page)
    {
        if (page <= 0)
        {
            mPage = 0;
            mPageText->setVisible(false);
            return;
        }
        mPage = page;
        mPageText->setCaption("Page " + std::to_string(page));
        mPageText->setVisible(true);
    }
}
