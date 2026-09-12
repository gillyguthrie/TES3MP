#ifndef MWGUI_STATBARS_H
#define MWGUI_STATBARS_H

/*
    majere addition (stat bars)

    Health / Magicka / Fatigue bars with their numbers, sitting just left of the hotbar and matching its
    height, so the vitals are read in the same glance as the quick keys. Display only: the same values
    the game's own bottom-left bars show (current / modified maximum), using the game's own bar skins.
    Settings: [StatBars] enabled, width, gap.
*/

#include <string>

#include <MyGUI_ProgressBar.h>

namespace MWMechanics { class MagicEffects; }
#include <MyGUI_TextBox.h>
#include <MyGUI_Widget.h>

namespace MWGui
{
    class Hotbar;

    class StatBars
    {
    public:
        explicit StatBars(Hotbar* hotbar);
        ~StatBars();

        void onFrame(float dt);
        void setVisible(bool visible);
        bool isEnabled() const { return mEnabled; }

    private:
        struct Bar
        {
            MyGUI::ProgressBar* bar;
            MyGUI::TextBox* text;
            MyGUI::TextBox* minus;  // "-" above the left end while a per-second damage effect is on this stat
            MyGUI::TextBox* plus;   // "+" above the right end while a per-second restore effect is on it
            int current, maximum;   // last shown, to skip redundant caption updates
            Bar() : bar(nullptr), text(nullptr), minus(nullptr), plus(nullptr), current(-1), maximum(-1) {}
        };
        void place();
        void update(Bar& b, float current, float maximum);
        /// per-second effects only: Damage/Poison/elemental/Absorb (never Drain) vs Restore (never Fortify)
        void updateSigns(Bar& b, const MWMechanics::MagicEffects& effects, const int* damageIds, int nDamage, int restoreId, bool sunDamage);
        /// tooltip text for a sign: every source of the given effects on the player, one per line
        void updateSignTooltips(Bar& b, const int* damageIds, int nDamage, int restoreId);
        float mTooltipTimer;

        bool mEnabled;
        int mWidth;
        int mGap;               // distance from the hotbar
        int mLeftMargin;        // where the panel may start (right of the HUD's bottom-left boxes)
        int mTextNudge;         // vertical shift of the numbers (px, negative = up)
        Hotbar* mHotbar;        // also knows where Azura's Star sits (just left of the strip): we stop short of it
        int mBarHeight;         // [StatBars] bar height
        MyGUI::Widget* mRoot;   // HUD layer (no interaction needed)
        Bar mHealth, mMagicka, mFatigue;
    };
}

#endif
