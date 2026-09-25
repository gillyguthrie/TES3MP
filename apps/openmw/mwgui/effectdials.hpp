#ifndef MWGUI_EFFECTDIALS_H
#define MWGUI_EFFECTDIALS_H

/*
    majere addition (effect dials)

    Top-right HUD panels showing the player's timed active effects as enlarged magic-effect
    icons with a clock-sweep overlay (dark wedge growing clockwise as time runs out) and the
    remaining seconds. Display only; reads CreatureStats::getActiveSpells() every frame.

    Two boxes, both anchored to the right edge and growing leftwards:
      Potions   - one column per potion DRUNK (never merged), oldest on the right, capped at
                  [EffectDials] max potions for display purposes only. Column = potion name,
                  the potion's own icon with the sweep of its longest effect, then one line per
                  effect: remaining seconds, mini dial, effect name.
      "Effects" - everything else (spells, enchantments, server effects), merged per effect id
                  like the bottom-right strip, showing the longest remaining time.

    Harmful effects are written in red. Permanent effects never appear here.

    A third element, the Resistances column (right-middle): one row per Resist Magicka/Fire/Frost/
    Shock/Poison/Paralysis showing the net value (resist minus weakness), green if positive, red if
    negative. Read from the actor's aggregated MagicEffects, so racials/birthsigns/spells/gear all count.

    settings.cfg [EffectDials] (defaults in effectdials.cpp / settings-default.cfg):
        enabled, dial size, spacing, max potions, column width, right margin, top margin,
        show potions when empty, show effects box, show resistances
*/

#include <map>
#include <string>
#include <vector>

#include "windowbase.hpp"

namespace MyGUI
{
    class EditBox;
    class ImageBox;
    class TextBox;
    class Widget;
}

namespace ESM
{
    struct MagicEffect;
}

namespace MWGui
{
    class HUD;
    class Hotbar;
    class ItemWidget;

    class EffectDials : public WindowBase
    {
    public:
        EffectDials();

        void onFrame(float dt) override;
        void onResChange(int width, int height) override;

        bool isEnabled() const { return mEnabled; }
        /// Layout anchors (majere): resistances hang above the HUD minimap; the star sits just left of the hotbar.
        void setAnchors(HUD* hud, Hotbar* hotbar);
        int getStarSize() const;

    private:
        struct SubEffect
        {
            std::string icon;
            std::string name;
            float timeLeft;
            float duration;
            bool harmful;
            bool timed;             // false: no countdown, no mini dial (permanent effects)
            SubEffect() : timeLeft(0.f), duration(0.f), harmful(false), timed(true) {}
        };

        struct DialData
        {
            std::string icon;       // potion: bottle art; effect: magic-effect icon
            std::string title;      // potion name (potions only)
            float timeLeft;
            float duration;
            bool harmful;
            std::vector<SubEffect> lines;   // potions only: every effect, primary first
            bool timed;             // false: permanent column (no sweep, no seconds)
            bool potion;            // a potion drunk: these hold the corner as a block of their own
            std::string key;        // which drink this is (record id + when it was drunk), for the run-out warning
            DialData() : timeLeft(0.f), duration(0.f), harmful(false), timed(true), potion(false) {}
        };

        struct LineWidgets
        {
            MyGUI::Widget* root;
            MyGUI::TextBox* secs;
            MyGUI::ImageBox* icon;
            MyGUI::ImageBox* sweep;
            MyGUI::EditBox* text;      // wraps; continuation lines stay indented under the first word
            MyGUI::ImageBox* divider;  // 1 px gold rule beneath the line (tinted "white")
            std::string currentIcon;
            int sweepFrame;
            LineWidgets() : root(nullptr), secs(nullptr), icon(nullptr), sweep(nullptr), text(nullptr), divider(nullptr), sweepFrame(-1) {}
        };

        struct DialWidgets
        {
            MyGUI::Widget* root;
            MyGUI::ImageBox* icon;     // plain icon, no frame
            MyGUI::ImageBox* sweep;
            MyGUI::ImageBox* outline;   // black edge round the elapsed wedge (majere_sweepo_NN), untinted
            MyGUI::TextBox* seconds;   // effects box only
            MyGUI::EditBox* title;     // source name / effect label, word-wrapped to at most two lines
            std::vector<LineWidgets> lines;
            std::string currentIcon;
            int sweepFrame;
            DialWidgets() : root(nullptr), icon(nullptr), sweep(nullptr), outline(nullptr), seconds(nullptr), title(nullptr), sweepFrame(-1) {}
        };

        struct Box
        {
            MyGUI::Widget* frame;
            MyGUI::TextBox* caption;
            std::vector<DialWidgets> dials;
            std::vector<MyGUI::ImageBox*> separators;   // 1 px gold verticals between columns
            MyGUI::ImageBox* groupRule;                 // the heavier rule between the potion block and the rest
            Box() : frame(nullptr), caption(nullptr), groupRule(nullptr) {}
        };

        bool mEnabled;
        int mDialSize;
        int mSpacing;
        int mMaxPotions;
        int mGroupGap;              // [EffectDials] potion block gap: extra space between the potions and the rest
        // a sound when a potion in the block has this many seconds left, once per drink
        // ([EffectDials] potion warning seconds / sound / repeats)
        float mPotionWarnSeconds;
        std::string mPotionWarnSound;
        std::map<std::string, int> mPotionsWarned;   // drink -> how many of the repeats have played
        int mPotionWarnRepeats;                      // [EffectDials] potion warning repeats
        void warnExpiringPotions(const std::vector<DialData>& columns);
        int mColumnWidth;
        int mRightMargin;
        int mTopMargin;
        bool mShowPotionsWhenEmpty;
        bool mShowEffectsBox;

        Box mPotions;
        Box mEffects;

        // Resistances: vertical column, right-middle of the screen. Net value = Resist X total minus
        // Weakness to X total from CreatureStats::getMagicEffects() (all sources, additive, no hardcoding).
        struct ResistRow
        {
            int resistId;
            int weaknessId;     // -1 if the effect has no weakness counterpart (paralysis)
            MyGUI::Widget* row; // hover target carrying the tooltip (menus only)
            MyGUI::ImageBox* icon;
            MyGUI::TextBox* value;
            std::string currentIcon;
            std::string tooltip;
            ResistRow() : resistId(-1), weaknessId(-1), row(nullptr), icon(nullptr), value(nullptr) {}
        };
        bool mShowResists;
        int mResistTop;             // fixed top (GUI px) for the column; -1 = a quarter of the screen height
        MyGUI::Widget* mResistBox;  // expanded: a 3x2 grid (fire frost shock / magicka poison paralysis)
        MyGUI::Button* mResistButton;   // collapsed: a "Resists" button in the same spot
        bool mResistsExpanded;      // remembered ([EffectDials] resists expanded)
        void onResistsClicked(MyGUI::Widget* sender);   // grid click collapses, button click expands
        // majere: in a menu the grid wears a title bar -- four layout buttons (1 = one column, 2 = two wide,
        // 3 = three wide, 4 = one row) and a drag handle; the layout and the dragged spot are remembered
        // ([EffectDials] resists layout / resists x / resists y). The bar is gone while the game runs.
        std::vector<std::string> mHiddenSources;   // [EffectDials] hide sources: lower-case name beginnings
        std::vector<std::string> mNotPotions;      // [EffectDials] not potions: potions kept out of the potion block
        float mLongPotionSeconds;                  // [EffectDials] long potion minutes: so are potions this long (0 = off)
        static void readNameList(const std::string& list, std::vector<std::string>& out);
        int mResistLayout;
        bool mResistMoved;
        bool mResistWasInMenu;      // the grid is raised above the game's windows when a menu opens
        MyGUI::IntPoint mResistPos;
        MyGUI::Widget* mResistTitle;
        MyGUI::Button* mResistLayoutButtons[4];
        MyGUI::IntPoint mResistDragOffset;
        // the spread grip at the bar's left: dragging it right/left and down/up opens or closes the gaps
        // between cells ([EffectDials] resists gap x / resists gap y)
        MyGUI::Widget* mResistGrip;
        int mResistGapX, mResistGapY;
        MyGUI::IntPoint mResistGripStart;
        int mResistGripGapX, mResistGripGapY;
        void onResistGripPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onResistGripDragged(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onResistLayoutClicked(MyGUI::Widget* sender);
        void onResistTitlePressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onResistTitleDragged(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void layoutResistRows(bool withTitle);   // place the six cells (and the bar) for the current layout
        int resistCols() const;
        HUD* mHud;                  // layout anchors: the minimap box (resists sit above it)
        Hotbar* mHotbar;            // layout anchor: where the star goes
        std::vector<ResistRow> mResistRows;
        void updateResists();
        /// Tooltip text per row: every source contributing to that resistance, one per line, weaknesses in red.
        void updateResistTooltips();
        float mResistTooltipTimer;

        // Constant-effect star: Azura's Star under the resistances. Flat while nothing worn carries a
        // constant enchantment, breathing gold very slowly when something does. Its tooltip lists every
        // effect (lumped totals) and each item's own effects.
        bool mShowConstant;
        float mPulse;               // seconds, drives the glow pulse
        MyGUI::Widget* mStarRow;    // hover target
        MyGUI::ImageBox* mStarIcon;
        bool mStarIconSet;
        bool mStarActive;           // worn gear carries at least one constant effect
        /// rebuilds active state + popup lines (call a few times a second); the pulse is per frame
        void updateConstantStar();

        // The star's popup is built by hand (not the game's tooltip system) so it can be a real table:
        // effect name column, then the contributing items word-wrapped in a fixed-width column, so wrapped
        // lines stay aligned under the first one.
        struct StarLine
        {
            std::string name;       // "Resist Fire 40" (sort key without the number)
            std::string sources;    // "Denstagmer's Ring 30, Wraithguard 10"
            bool harmful;
        };
        std::vector<StarLine> mStarLines;
        MyGUI::Widget* mStarTip;    // Popup-layer box, shown while the star is hovered in a menu
        bool mStarTipDirty;
        void onStarFocus(MyGUI::Widget* sender, MyGUI::Widget* old);
        void onStarLostFocus(MyGUI::Widget* sender, MyGUI::Widget* now);
        void rebuildStarTip();
        void positionStarTip();

        /// every active timed source (potions and spells alike) as one column each, oldest first;
        /// potions beyond [EffectDials] max potions are left out (display cap only)
        void collect(std::vector<DialData>& columns) const;
        /// Vampire sun damage: a permanent ability, so never in ActiveSpells. One untimed column while the
        /// engine would actually be applying it (outside, daylight), showing the scaled per-second damage.
        bool collectSunDamage(DialData& column) const;
    public:
        ~EffectDials();
    private:
        /// "+Speed 10", "++Health 200", "+Health 10/s", "-Agility 5", "--Health 3/s"
        std::string effectLabel(const ESM::MagicEffect* effect, int arg, float magnitude) const;
        void fillBox(Box& box, const std::vector<DialData>& data, int top, bool withLines);
        DialWidgets createDial(Box& box, bool potionStyle);
        LineWidgets createLine(DialWidgets& dial);
        void applyDial(DialWidgets& w, const DialData& d, int colW, bool potionStyle, int titleH, const std::vector<int>& lineHeights);
        /// measures each effect line's wrapped height at this column width (creates line widgets as needed)
        std::vector<int> measureLines(DialWidgets& w, const DialData& d, int colW);
        static void setSweep(MyGUI::ImageBox* sweep, int& cachedFrame, float timeLeft, float duration, bool mini, MyGUI::ImageBox* outline = nullptr);
    };
}

#endif
