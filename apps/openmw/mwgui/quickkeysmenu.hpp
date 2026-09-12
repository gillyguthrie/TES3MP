#ifndef MWGUI_QUICKKEYS_H
#define MWGUI_QUICKKEYS_H

#include "windowbase.hpp"

#include "spellmodel.hpp"

namespace MWGui
{

    class QuickKeysMenuAssign;
    class ItemSelectionDialog;
    class MagicSelectionDialog;
    class ItemWidget;
    class SpellView;

    class QuickKeysMenu : public WindowBase
    {
    public:
        QuickKeysMenu();
        ~QuickKeysMenu();

        void onResChange(int, int) override { center(); }

        void onItemButtonClicked(MyGUI::Widget* sender);
        void onMagicButtonClicked(MyGUI::Widget* sender);
        void onUnassignButtonClicked(MyGUI::Widget* sender);
        void onCancelButtonClicked(MyGUI::Widget* sender);

        void onAssignItem (MWWorld::Ptr item);
        void onAssignItemCancel ();
        void onAssignMagicItem (MWWorld::Ptr item);
        void onAssignMagic (const std::string& spellId);
        void onAssignMagicCancel ();
        void onOpen() override;

        void activateQuickKey(int index);
        void updateActivatedQuickKey();

        /*
            Start of tes3mp addition

            Allow the setting of the selected index from elsewhere in the code
        */
        void setSelectedIndex(int index);
        /*
            End of tes3mp addition
        */

        /// @note This enum is serialized, so don't move the items around!
        enum QuickKeyType
        {
            Type_Item,
            Type_Magic,
            Type_MagicItem,
            Type_Unassigned,
            Type_HandToHand
        };

        void write (ESM::ESMWriter& writer);
        void readRecord (ESM::ESMReader& reader, uint32_t type);
        void clear() override;

        /*
            Start of tes3mp addition

            Allow unassigning an index directly from elsewhere in the code
        */
        void unassignIndex(int index);
        /*
            End of tes3mp addition
        */

        /*
            Start of majere addition (hotbar)

            Read-only view of the slot table plus a revision counter that ticks on every
            assign/unassign, so the hotbar can redraw only when something changed.
        */
        /// Open the game's own item/magic/unassign dialog for slot index (0-based); no-op for the hand-to-hand slot
        void openAssignDialogForSlot(int index);
        unsigned int getRevision() const { return mRevision; }
        int getSlotCount() const { return static_cast<int>(mKey.size()); }
        QuickKeyType getSlotType(int i) const { return mKey[i].type; }
        const std::string& getSlotId(int i) const { return mKey[i].id; }
        const std::string& getSlotName(int i) const { return mKey[i].name; }
        /*
            End of majere addition
        */

    private:
        unsigned int mRevision = 0;   // majere addition (hotbar)


        struct keyData {
            int index;
            ItemWidget* button;
            QuickKeysMenu::QuickKeyType type;
            std::string id;
            std::string name;
            keyData(): index(-1), button(nullptr), type(Type_Unassigned), id(""), name("") {}
        };

        std::vector<keyData> mKey;
        keyData* mSelected;
        keyData* mActivated;

        MyGUI::EditBox* mInstructionLabel;
        MyGUI::Button* mOkButton;

        QuickKeysMenuAssign* mAssignDialog;
        ItemSelectionDialog* mItemSelectionDialog;
        MagicSelectionDialog* mMagicSelectionDialog;

        void onQuickKeyButtonClicked(MyGUI::Widget* sender);
        void onOkButtonClicked(MyGUI::Widget* sender);

        void unassign(keyData* key);
    };

    class QuickKeysMenuAssign : public WindowModal
    {
    public:
        QuickKeysMenuAssign(QuickKeysMenu* parent);

    private:
        MyGUI::TextBox* mLabel;
        MyGUI::Button* mItemButton;
        MyGUI::Button* mMagicButton;
        MyGUI::Button* mUnassignButton;
        MyGUI::Button* mCancelButton;

        QuickKeysMenu* mParent;
    };

    class MagicSelectionDialog : public WindowModal
    {
    public:
        MagicSelectionDialog(QuickKeysMenu* parent);

        void onOpen() override;
        bool exit() override;

    private:
        MyGUI::Button* mCancelButton;
        SpellView* mMagicList;

        QuickKeysMenu* mParent;

        void onCancelButtonClicked (MyGUI::Widget* sender);
        void onModelIndexSelected(SpellModel::ModelIndex index);
    };
}


#endif
