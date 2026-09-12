/*
    majere addition (effect dials) -- see effectdials.hpp
*/
#include "effectdials.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ITexture.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_Widget.h>

#include <components/esm/attr.hpp>
#include <components/esm/loadalch.hpp>
#include <components/esm/loadgmst.hpp>
#include <components/esm/loadmgef.hpp>
#include <components/esm/loadmisc.hpp>
#include <components/esm/loadskil.hpp>
#include <components/settings/settings.hpp>
#include <components/debug/debuglog.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/activespells.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/magiceffects.hpp"
#include "../mwmechanics/spells.hpp"

#include "../mwworld/inventorystore.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/timestamp.hpp"

#include "hotbar.hpp"
#include "hud.hpp"
#include "itemwidget.hpp"

namespace
{
    int settingInt(const char* key, int def)
    {
        try { return Settings::Manager::getInt(key, "EffectDials"); } catch (...) { return def; }
    }
    float settingFloat(const char* key, float def)
    {
        try { return Settings::Manager::getFloat(key, "EffectDials"); } catch (...) { return def; }
    }
    bool settingBool(const char* key, bool def)
    {
        try { return Settings::Manager::getBool(key, "EffectDials"); } catch (...) { return def; }
    }

    // textures\majere_sweep_NN.png (files/vfs): 72 single frames of 64 px; frame k = elapsed fraction k/71 darkened,
    // clockwise from 12 o'clock. One file per frame because MyGUI's atlas/tile selection drew nothing on this build.
    const int sSweepFrames = 72;
    // big dial: white sweep (majere_sweepw_NN) tinted by [EffectDials] sweep colour / sweep alpha;
    // mini line dials: black sweep (majere_sweepk_NN). (majere_sweep_NN, the old baked gold set, is unused now.)
    std::string sweepTexture(int frame, bool mini = false)
    {
        char buf[48];
        snprintf(buf, sizeof(buf), mini ? "textures\\majere_sweepk_%02d.png" : "textures\\majere_sweepw_%02d.png", frame);
        return buf;
    }
    MyGUI::Colour sweepColour()
    {
        try { return MyGUI::Colour::parse(Settings::Manager::getString("sweep colour", "EffectDials")); }
        catch (...) { return MyGUI::Colour(0.90f, 0.84f, 0.68f); }
    }

    const int sCaptionH = 16;
    const int sPad = 5;
    const int sRing = 2;
    const int sLineH = 18;      // one effect line
    const int sLineIcon = 16;
    const int sDividerH = 0;    // no rules between lines (by choice); code path kept, widget hidden
    const int sSecondsH = 16;   // seconds line under an effects-box dial / potion title line
    const int sSecondsW = 32;   // width of the countdown column at the start of an effect line ("59s", "11m")
    const int sGap = 2;
    const int sTextIndent = 6;  // gap between the line icon and the label (same for every wrapped row)
    const MyGUI::Colour sTextHarmful(0.95f, 0.35f, 0.3f);
    const MyGUI::Colour sRuleGold(0.78f, 0.62f, 0.3f);
    const float sRuleAlpha = 0.7f;
    const int sIconInset = 0;   // icon and sweep fill the dial square
    const MyGUI::Colour sTextGood(0.45f, 0.9f, 0.45f);
    const int sEffectColW = 96;   // effects-box column: wide enough for "+Magicka 100"
    // base sizes at scale 1; multiplied by [EffectDials] resistances scale in the constructor
    int sResistRowH = 22;      // 18 px icon + a little air between rows
    int sResistIcon = 18;
    int sResistValueW = 40;
    int sStarPad = 34;            // hover target (square)
    int sStarIcon = 28;           // Azura's Star icon inside it (the icon itself pulses; no backdrop)
    MyGUI::Colour sTextNormal = MyGUI::Colour::White;         // captured from the SandText skin on first use
    MyGUI::Colour sTextNormalBright = MyGUI::Colour::White;   // captured from SandBrightText

    std::string secondsText(float t)
    {
        if (t < 0.f) t = 0.f;
        if (t < 60.f)
            return std::to_string(static_cast<int>(std::ceil(t))) + "s";
        return std::to_string(static_cast<int>(t) / 60) + "m";
    }
}

namespace MWGui
{
    EffectDials::EffectDials()
        : WindowBase("openmw_effectdials.layout")
        , mEnabled(true), mDialSize(44), mSpacing(6), mMaxPotions(5), mColumnWidth(150)
        , mRightMargin(12), mTopMargin(12), mShowPotionsWhenEmpty(false), mShowEffectsBox(true)
        , mShowResists(true), mResistTop(-1), mResistBox(nullptr), mResistButton(nullptr), mResistsExpanded(true)
        , mHud(nullptr), mHotbar(nullptr), mResistTooltipTimer(0.f)
        , mShowConstant(true), mPulse(0.f), mStarRow(nullptr), mStarIcon(nullptr), mStarIconSet(false), mStarActive(false), mStarTip(nullptr), mStarTipDirty(true)
    {
        mEnabled              = settingBool("enabled", true);
        mDialSize             = std::max(24, settingInt("dial size", 44));
        mSpacing              = std::max(0, settingInt("spacing", 6));
        mMaxPotions           = std::max(1, settingInt("max potions", 5));
        mColumnWidth          = std::max(mDialSize, settingInt("column width", 150));
        mRightMargin          = settingInt("right margin", 12);
        mTopMargin            = settingInt("top margin", 12);
        mShowPotionsWhenEmpty = settingBool("show potions when empty", false);
        mShowEffectsBox       = settingBool("show effects box", true);
        mShowResists          = settingBool("show resistances", true);
        mResistTop            = settingInt("resistances top", -1);
        mShowConstant         = settingBool("show constant effects", true);
        mResistsExpanded      = settingBool("resists expanded", true);
        const float resistScale = std::min(4.f, std::max(1.f, settingFloat("resistances scale", 1.f)));
        sResistRowH    = static_cast<int>(std::lround(22 * resistScale));
        sResistIcon    = static_cast<int>(std::lround(18 * resistScale));
        sResistValueW  = static_cast<int>(std::lround(40 * resistScale));
        sStarPad       = static_cast<int>(std::lround(34 * resistScale));
        sStarIcon      = static_cast<int>(std::lround(28 * resistScale));

        mMainWidget->setSize(MyGUI::RenderManager::getInstance().getViewSize());
        mMainWidget->setNeedMouseFocus(false);

        getWidget(mPotions.frame, "PotionBox");
        getWidget(mPotions.caption, "PotionCaption");
        getWidget(mEffects.frame, "EffectBox");
        getWidget(mEffects.caption, "EffectCaption");
        mPotions.frame->setNeedMouseFocus(false);
        mPotions.caption->setNeedMouseFocus(false);
        mEffects.frame->setNeedMouseFocus(false);
        mEffects.caption->setNeedMouseFocus(false);
        mPotions.frame->setVisible(false);
        mEffects.frame->setVisible(false);

        // Resistances column
        getWidget(mResistBox, "ResistBox");
        mResistBox->setNeedMouseFocus(false);
        mResistBox->setVisible(false);
        // grid order: fire, frost, shock on top; magicka, poison, paralysis below
        const int resistIds[][2] = {
            { ESM::MagicEffect::ResistFire,      ESM::MagicEffect::WeaknessToFire },
            { ESM::MagicEffect::ResistFrost,     ESM::MagicEffect::WeaknessToFrost },
            { ESM::MagicEffect::ResistShock,     ESM::MagicEffect::WeaknessToShock },
            { ESM::MagicEffect::ResistMagicka,   ESM::MagicEffect::WeaknessToMagicka },
            { ESM::MagicEffect::ResistPoison,    ESM::MagicEffect::WeaknessToPoison },
            { ESM::MagicEffect::ResistParalysis, -1 } };
        // The column lives in the Menu layer as its own root so its rows can be hovered for tooltips
        // (a HUD-layer widget is never picked: the HUD's full-screen root wins). Visibility is kept in
        // step with the panel by updateResists / onFrame.
        mResistBox->detachFromWidget("Menu");
        mResistBox->setNeedMouseFocus(true);
        mResistBox->eventMouseButtonClick += MyGUI::newDelegate(this, &EffectDials::onResistsClicked);
        for (const auto& ids : resistIds)
        {
            ResistRow row;
            row.resistId = ids[0];
            row.weaknessId = ids[1];
            const int cellW = sResistIcon + 4 + sResistValueW;
            const int n = static_cast<int>(mResistRows.size());
            const int x = (n % 3) * cellW, y = (n / 3) * sResistRowH;
            row.row = mResistBox->createWidget<MyGUI::Widget>("", MyGUI::IntCoord(x, y, cellW, sResistRowH), MyGUI::Align::Default);
            row.row->setNeedMouseFocus(true);
            row.row->eventMouseButtonClick += MyGUI::newDelegate(this, &EffectDials::onResistsClicked);
            row.row->setUserString("ToolTipType", "Layout");
            row.row->setUserString("ToolTipLayout", "TextToolTip");
            row.row->setUserString("Caption_Text", "");
            row.icon = row.row->createWidget<MyGUI::ImageBox>("ImageBox",
                MyGUI::IntCoord(0, (sResistRowH - sResistIcon) / 2, sResistIcon, sResistIcon), MyGUI::Align::Default);
            row.icon->setNeedMouseFocus(false);
            row.value = row.row->createWidget<MyGUI::TextBox>("SandBrightText",
                MyGUI::IntCoord(sResistIcon + 4, 0, sResistValueW, sResistRowH), MyGUI::Align::Default);
            row.value->setFontHeight(static_cast<int>(std::lround(row.value->getFontHeight() * resistScale)));
            row.value->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
            row.value->setTextShadow(true);
            row.value->setNeedMouseFocus(false);
            mResistRows.push_back(row);
        }

        // collapsed form: a "Resists" button in the grid's place (own Menu-layer root, clickable in menus)
        mResistButton = MyGUI::Gui::getInstance().createWidget<MyGUI::Button>("MW_Button",
            MyGUI::IntCoord(0, 0, 96, 24), MyGUI::Align::Default, "Menu");
        mResistButton->setCaption("Resists");
        mResistButton->setNeedMouseFocus(true);
        mResistButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EffectDials::onResistsClicked);
        mResistButton->setVisible(false);

        // Constant-effect star: its own pickable root in the Menu layer, placed just left of the hotbar
        // (see Hotbar::getStarSlot); glows only with constant effects worn
        {
            mStarRow = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>("", MyGUI::IntCoord(0, 0, sStarPad, sStarPad), MyGUI::Align::Default, "Menu");
            mStarRow->setNeedMouseFocus(true);
            mStarRow->setVisible(false);
            mStarRow->eventMouseSetFocus += MyGUI::newDelegate(this, &EffectDials::onStarFocus);
            mStarRow->eventMouseLostFocus += MyGUI::newDelegate(this, &EffectDials::onStarLostFocus);
            mStarTip = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>("HUD_Box_NoTransp",
                MyGUI::IntCoord(0, 0, 10, 10), MyGUI::Align::Default, "Popup");
            mStarTip->setNeedMouseFocus(false);
            mStarTip->setVisible(false);
            const int inset = (sStarPad - sStarIcon) / 2;
            mStarIcon = mStarRow->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(inset, inset, sStarIcon, sStarIcon), MyGUI::Align::Default);
            mStarIcon->setNeedMouseFocus(false);
        }

        MyGUI::ITexture* sweepTex = MyGUI::RenderManager::getInstance().getTexture(sweepTexture(sSweepFrames - 1));
        Log(Debug::Info) << "EffectDials: sweep frame '" << sweepTexture(sSweepFrames - 1) << "' "
                         << (sweepTex ? "loaded, " + std::to_string(sweepTex->getWidth()) + "x" + std::to_string(sweepTex->getHeight())
                                        + " (expected 64x64)"
                                      : "NOT FOUND (no clock sweep will be drawn)");
    }

    void EffectDials::onResChange(int width, int height)
    {
        mMainWidget->setSize(width, height);
    }

    void EffectDials::setSweep(MyGUI::ImageBox* sweep, int& cachedFrame, float timeLeft, float duration, bool mini)
    {
        float elapsed = (duration > 0.f) ? 1.f - (timeLeft / duration) : 0.f;
        elapsed = std::min(1.f, std::max(0.f, elapsed));
        const int frame = static_cast<int>(std::lround(elapsed * (sSweepFrames - 1)));
        if (frame == cachedFrame)
            return;
        cachedFrame = frame;
        sweep->setImageTexture(sweepTexture(frame, mini));
    }

    EffectDials::DialWidgets EffectDials::createDial(Box& box, bool potionStyle)
    {
        DialWidgets w;
        const int s = mDialSize;
        const int iconTop = sSecondsH;   // title line (potion name / effect label) first
        w.root = box.frame->createWidget<MyGUI::Widget>("", MyGUI::IntCoord(0, 0, s, s), MyGUI::Align::Default);
        w.root->setNeedMouseFocus(false);

        w.title = w.root->createWidget<MyGUI::EditBox>("SandBrightText",
            MyGUI::IntCoord(0, 0, mColumnWidth, sSecondsH), MyGUI::Align::Default);
        w.title->setEditStatic(true);
        w.title->setEditReadOnly(true);
        w.title->setEditMultiLine(true);
        w.title->setEditWordWrap(true);
        w.title->setTextAlign(MyGUI::Align::HCenter | MyGUI::Align::Top);
        w.title->setTextShadow(true);
        w.title->setNeedMouseFocus(false);

        w.icon = w.root->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, iconTop, s, s), MyGUI::Align::Default);
        w.icon->setNeedMouseFocus(false);

        w.sweep = w.root->createWidget<MyGUI::ImageBox>("ImageBox",
            MyGUI::IntCoord(sIconInset, iconTop + sIconInset, s - 2 * sIconInset, s - 2 * sIconInset), MyGUI::Align::Default);
        w.sweep->setImageTexture(sweepTexture(0));
        w.sweep->setColour(sweepColour());
        w.sweep->setAlpha(std::min(1.f, std::max(0.05f, settingFloat("sweep alpha", 0.45f))));
        w.sweep->setNeedMouseFocus(false);

        if (!potionStyle)
        {
            w.seconds = w.root->createWidget<MyGUI::TextBox>("SandBrightText",
                MyGUI::IntCoord(0, iconTop + s, s, sSecondsH), MyGUI::Align::Default);
            w.seconds->setTextAlign(MyGUI::Align::Center);
            w.seconds->setTextShadow(true);
            w.seconds->setNeedMouseFocus(false);
        }
        return w;
    }

    EffectDials::LineWidgets EffectDials::createLine(DialWidgets& dial)
    {
        LineWidgets l;
        l.root = dial.root->createWidget<MyGUI::Widget>("", MyGUI::IntCoord(0, 0, mColumnWidth, sLineH), MyGUI::Align::Default);
        l.root->setNeedMouseFocus(false);
        const int textH = sLineH - sDividerH;
        l.secs = l.root->createWidget<MyGUI::TextBox>("SandBrightText", MyGUI::IntCoord(0, 0, sSecondsW, textH), MyGUI::Align::Default);
        l.secs->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
        l.secs->setTextShadow(true);
        l.secs->setNeedMouseFocus(false);
        l.icon = l.root->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(sSecondsW + sGap, 0, sLineIcon, sLineIcon), MyGUI::Align::Default);
        l.icon->setNeedMouseFocus(false);
        l.sweep = l.root->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(sSecondsW + sGap, 0, sLineIcon, sLineIcon), MyGUI::Align::Default);
        l.sweep->setImageTexture(sweepTexture(0, true));
        l.sweep->setNeedMouseFocus(false);
        l.text = l.root->createWidget<MyGUI::EditBox>("SandText",
            MyGUI::IntCoord(sSecondsW + sGap + sLineIcon + sTextIndent, 0, mColumnWidth - sSecondsW - sLineIcon - sGap - sTextIndent, textH), MyGUI::Align::Default);
        l.text->setEditStatic(true);
        l.text->setEditReadOnly(true);
        l.text->setEditMultiLine(true);
        l.text->setEditWordWrap(true);
        l.text->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        l.divider = l.root->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, textH + 1, mColumnWidth, 1), MyGUI::Align::Default);
        l.divider->setImageTexture("white");
        l.divider->setColour(sRuleGold);
        l.divider->setAlpha(sRuleAlpha);
        l.divider->setNeedMouseFocus(false);
        l.divider->setVisible(false);
        static bool captured = false;
        if (!captured)
        {
            captured = true;
            sTextNormal = l.text->getTextColour();
            sTextNormalBright = l.secs->getTextColour();
        }
        l.text->setTextShadow(true);
        l.text->setNeedMouseFocus(false);
        return l;
    }

    std::vector<int> EffectDials::measureLines(DialWidgets& w, const DialData& d, int colW)
    {
        std::vector<int> heights;
        const int textH = sLineH - sDividerH;
        const int textX = sSecondsW + sGap + sLineIcon + sTextIndent;
        while (w.lines.size() < d.lines.size())
            w.lines.push_back(createLine(w));
        for (size_t i = 0; i < d.lines.size(); ++i)
        {
            LineWidgets& l = w.lines[i];
            l.text->setSize(colW - textX, 4 * textH);
            l.text->setCaption(d.lines[i].name);
            // rows of text needed (1..3), from the wrapped text height
            const int rows = std::min(3, std::max(1, (l.text->getTextSize().height + textH / 2) / textH));
            heights.push_back(rows * textH + sDividerH);
        }
        return heights;
    }

    void EffectDials::applyDial(DialWidgets& w, const DialData& d, int colW, bool potionStyle, int titleH, const std::vector<int>& lineHeights)
    {
        if (w.currentIcon != d.icon)
        {
            w.currentIcon = d.icon;
            w.icon->setImageTexture(d.icon);
        }
        w.sweep->setVisible(d.timed);
        if (d.timed)
            setSweep(w.sweep, w.sweepFrame, d.timeLeft, d.duration, false);
        if (w.seconds)
        {
            w.seconds->setCaption(d.timed ? secondsText(d.timeLeft) : "");
            w.seconds->setTextColour(d.harmful ? sTextHarmful : sTextNormal);
        }

        // title across the top (potion name / effect label, up to two lines), icon centred beneath it
        const int s = mDialSize;
        const int iconX = (colW - s) / 2;
        const int iconTop = titleH;
        w.title->setCoord(0, 0, colW, titleH);
        w.title->setCaption(d.title);
        w.title->setTextColour((!potionStyle && d.harmful) ? sTextHarmful : sTextNormalBright);
        if (w.seconds)
            w.seconds->setCoord(iconX, iconTop + s, s, sSecondsH);
        w.icon->setCoord(iconX, iconTop, s, s);
        w.sweep->setCoord(iconX + sIconInset, iconTop + sIconInset, s - 2 * sIconInset, s - 2 * sIconInset);

        // effect lines run beneath the icon; each line's height comes from measureLines (1 or 2 text rows)
        const int n = static_cast<int>(d.lines.size());
        while (static_cast<int>(w.lines.size()) < n)
            w.lines.push_back(createLine(w));
        int y = iconTop + mDialSize + 2;
        for (size_t i = 0; i < w.lines.size(); ++i)
        {
            LineWidgets& l = w.lines[i];
            if (static_cast<int>(i) >= n)
            {
                l.root->setVisible(false);
                continue;
            }
            const SubEffect& e = d.lines[i];
            const int lineH = (i < lineHeights.size()) ? lineHeights[i] : sLineH;
            l.root->setCoord(0, y, colW, lineH);
            y += lineH;
            l.root->setVisible(true);
            if (l.currentIcon != e.icon)
            {
                l.currentIcon = e.icon;
                l.icon->setImageTexture(e.icon);
            }
            l.sweep->setVisible(e.timed);
            if (e.timed)
                setSweep(l.sweep, l.sweepFrame, e.timeLeft, e.duration, true);
            l.secs->setCaption(e.timed ? secondsText(e.timeLeft) : "");
            l.text->setCoord(sSecondsW + sGap + sLineIcon + sTextIndent, 0, colW - sSecondsW - sLineIcon - sGap - sTextIndent, lineH - sDividerH);
            l.text->setCaption(e.name);
            l.divider->setVisible(false);
            l.secs->setTextColour(e.harmful ? sTextHarmful : sTextNormalBright);
            l.text->setTextColour(e.harmful ? sTextHarmful : sTextNormal);
        }
    }

    std::string EffectDials::effectLabel(const ESM::MagicEffect* effect, int arg, float magnitude) const
    {
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
        std::string name = wm->getGameSettingString(ESM::MagicEffect::effectIdToString(effect->mIndex), "");

        // Short (<= 8 chars) attribute / skill names, in the game's own index order (see components/esm/attr.hpp, loadskil.hpp)
        static const char* const sAttrShort[ESM::Attribute::Length] = {
            "Strength", "Intel", "Will", "Agility", "Speed", "Endur", "Persona", "Luck" };
        static const char* const sSkillShort[ESM::Skill::Length] = {
            "Block", "Armorer", "MedArmor", "HvyArmor", "Blunt", "LngBlade", "Axe", "Spear", "Athletic",
            "Enchant", "Destruct", "Alter", "Illusion", "Conjure", "Mystic", "Restore", "Alchemy", "Unarmor",
            "Security", "Sneak", "Acrobat", "LtArmor", "ShtBlade", "Marksman", "Mercant", "Speech", "H2H" };

        std::string target;
        if ((effect->mData.mFlags & ESM::MagicEffect::TargetSkill) && arg >= 0 && arg < ESM::Skill::Length)
            target = sSkillShort[arg];
        else if ((effect->mData.mFlags & ESM::MagicEffect::TargetAttribute) && arg >= 0 && arg < ESM::Attribute::Length)
            target = sAttrShort[arg];

        // "Fortify Attribute" -> "Fortify Speed", "Drain Skill" -> "Drain Long Blade"
        if (!target.empty())
        {
            bool replaced = false;
            for (const char* placeholder : { "Attribute", "Skill" })
            {
                size_t pos = name.find(placeholder);
                if (pos != std::string::npos)
                {
                    name = name.substr(0, pos) + target + name.substr(pos + std::string(placeholder).size());
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
                name += " " + target;
        }

        // compact verbs: Fortify/Restore -> "+", Drain -> "-", Damage -> "--";
        // flat pool buffs (Fortify Health/Magicka/Fatigue) -> "++"; per-second effects get "/s"
        struct Abbrev { const char* word; const char* sign; bool perSecond; const char* fixedLabel; };
        static const Abbrev abbrevs[] = {
            {"Resist ", "+", false, nullptr}, {"Weakness to ", "-", false, nullptr},
            {"Fortify ", "+", false, nullptr}, {"Restore ", "+", true, nullptr},
            {"Drain ", "-", false, nullptr},   {"Damage ", "--", true, nullptr}, {"Absorb ", "-", true, nullptr},
            {"Poison", "--", true, "Poison"},  {"Fire Damage", "--", true, "Fire"}, {"Frost Damage", "--", true, "Frost"},
            {"Shock Damage", "--", true, "Shock"}, {"Sun Damage", "--", true, "Sun"} };
        bool perSecond = false;
        for (const Abbrev& a : abbrevs)
        {
            const std::string w(a.word);
            if (name.compare(0, w.size(), w) != 0)
                continue;
            // verb dropped (red text already marks harmful effects); "/s" still marks per-second effects
            name = a.fixedLabel ? std::string(a.fixedLabel) : name.substr(w.size());
            perSecond = a.perSecond;
            break;
        }

        // magnitude, formatted the way the game's tooltips do
        std::string mag;
        const int m = static_cast<int>(std::lround(magnitude));
        // fractional per-second values (sun damage at dawn/dusk, e.g. 0.4/s) would otherwise read as 0
        std::string points = std::to_string(m);
        if (magnitude < 10.f && std::abs(magnitude - m) > 0.05f)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.1f", magnitude);
            points = buf;
        }
        switch (effect->getMagnitudeDisplayType())
        {
            case ESM::MagicEffect::MDT_Points:     mag = points; break;
            case ESM::MagicEffect::MDT_Percentage: mag = std::to_string(m) + "%"; break;
            case ESM::MagicEffect::MDT_Feet:       mag = std::to_string(m) + " ft"; break;
            case ESM::MagicEffect::MDT_Level:      mag = std::to_string(m) + " lvl"; break;
            case ESM::MagicEffect::MDT_TimesInt:   mag = std::to_string(m) + "x"; break;
            default: break;
        }
        if (!mag.empty())
            name += " " + mag + (perSecond ? "/s" : "");
        // short forms for long pool names
        for (size_t pos = name.find("Magicka"); pos != std::string::npos; pos = name.find("Magicka"))
            name.replace(pos, 7, "Mag");
        return name;
    }

    bool EffectDials::collectSunDamage(DialData& column) const
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty() || !player.isInCell() || !player.getCell()->isExterior())
            return false;
        const float magnitude = player.getClass().getCreatureStats(player).getMagicEffects()
                                    .get(MWMechanics::EffectKey(ESM::MagicEffect::SunDamage)).getMagnitude();
        if (magnitude <= 0.f)
            return false;
        // The game's own list always says the full magnitude, but the damage only lands outside in daylight:
        // same scaling as MWMechanics::effectTick (tickableeffects.cpp): full at 13:00, nothing 7 h either side,
        // times fMagicSunBlockedMult when it is cloudy or worse. Shown only while it would actually be ticking.
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const float hour = world->getTimeStamp().getHour();
        const float timeDiff = std::min(7.f, std::max(0.f, std::abs(hour - 13.f)));
        float scale = 1.f - timeDiff / 7.f;
        if (world->getCurrentWeather() > 1)
            scale *= world->getStore().get<ESM::GameSetting>().find("fMagicSunBlockedMult")->mValue.getFloat();
        if (magnitude * scale <= 0.f)
            return false;

        const MWWorld::ESMStore& store = world->getStore();
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
        const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(ESM::MagicEffect::SunDamage);
        if (!effect)
            return false;
        column = DialData();
        column.timed = false;
        column.harmful = true;
        column.title = wm->getGameSettingString(ESM::MagicEffect::effectIdToString(effect->mIndex), "Sun Damage");
        column.icon = wm->correctIconPath(effect->mIcon);
        SubEffect sub;
        sub.timed = false;
        sub.harmful = true;
        sub.icon = column.icon;
        sub.name = effectLabel(effect, -1, magnitude * scale);
        column.lines.push_back(sub);
        return true;
    }

    void EffectDials::collect(std::vector<DialData>& columns) const
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty())
            return;
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const MWMechanics::ActiveSpells& active = player.getClass().getCreatureStats(player).getActiveSpells();
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();

        struct Entry { MWWorld::TimeStamp when; DialData data; };
        std::vector<Entry> potionEntries, effectEntries;

        for (MWMechanics::ActiveSpells::TIterator it = active.begin(); it != active.end(); ++it)
        {
            const std::string& id = it->first;
            const MWMechanics::ActiveSpells::ActiveSpellParams& params = it->second;
            const ESM::Potion* potionRec = store.get<ESM::Potion>().search(id);
            const bool isPotion = potionRec != nullptr;

            // one column per source (potion drunk / spell cast / enchantment used), never merged
            DialData col;
            for (const MWMechanics::ActiveSpells::ActiveEffect& e : params.mEffects)
            {
                if (e.mTimeLeft <= 0.f || e.mDuration <= 0.f)
                    continue;
                const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(e.mEffectId);
                if (!effect)
                    continue;

                SubEffect sub;
                sub.icon = wm->correctIconPath(effect->mIcon);
                sub.name = effectLabel(effect, e.mArg, e.mMagnitude);
                sub.timeLeft = e.mTimeLeft;
                sub.duration = e.mDuration;
                sub.harmful = (effect->mData.mFlags & ESM::MagicEffect::Harmful) != 0;

                if (col.lines.empty())
                {
                    col.icon = isPotion ? wm->correctIconPath(potionRec->mIcon) : sub.icon;   // bottle art / first effect
                    col.title = params.mDisplayName;
                    if (col.title.empty())
                        col.title = isPotion ? potionRec->mName : id;
                    col.harmful = sub.harmful;
                }
                if (col.timeLeft <= 0.f || sub.timeLeft < col.timeLeft)   // big sweep: soonest to expire
                {
                    col.timeLeft = sub.timeLeft;
                    col.duration = sub.duration;
                }
                col.lines.push_back(sub);
            }
            if (col.lines.empty())
                continue;
            (isPotion ? potionEntries : effectEntries).push_back({ params.mTimeStamp, col });
        }

        // one line of columns: potions and spells together, oldest first; potion display cap applies to potions only
        std::stable_sort(potionEntries.begin(), potionEntries.end(), [](const Entry& a, const Entry& b) { return a.when < b.when; });
        if (static_cast<int>(potionEntries.size()) > mMaxPotions)
            potionEntries.resize(mMaxPotions);
        std::vector<Entry> all(potionEntries);
        all.insert(all.end(), effectEntries.begin(), effectEntries.end());
        std::stable_sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) { return a.when < b.when; });
        for (const Entry& e : all)
            columns.push_back(e.data);
    }

    // Lays the columns out right-to-left inside the box (data[0] ends up rightmost), sizes the box,
    // and anchors it to the top-right corner at the given top offset.
    void EffectDials::fillBox(Box& box, const std::vector<DialData>& data, int top, bool withLines)
    {
        const int n = static_cast<int>(data.size());
        while (static_cast<int>(box.dials.size()) < n)
            box.dials.push_back(createDial(box, withLines));

        int maxLines = 0;
        if (withLines)
            for (const DialData& d : data)
                maxLines = std::max(maxLines, static_cast<int>(d.lines.size()));
        const int colW = (withLines && maxLines > 0) ? mColumnWidth : sEffectColW;

        // does any title need a second line at this column width?
        int titleLines = 1;
        for (int i = 0; i < n; ++i)
        {
            DialWidgets& w = box.dials[i];
            w.title->setSize(colW, 4 * sSecondsH);
            w.title->setCaption(data[i].title);
            const int rows = std::min(3, std::max(1, (w.title->getTextSize().height + sSecondsH / 2) / sSecondsH));
            titleLines = std::max(titleLines, rows);
        }
        const int titleH = titleLines * sSecondsH;

        // measure every column's effect lines (wrapped names may take two rows)
        std::vector<std::vector<int>> lineHeights(n);
        int tallestLines = 0;
        if (withLines)
            for (int i = 0; i < n; ++i)
            {
                lineHeights[i] = measureLines(box.dials[i], data[i], colW);
                int sum = 0;
                for (int h : lineHeights[i]) sum += h;
                tallestLines = std::max(tallestLines, sum);
            }

        // column: title + icon + lines (potion style) / title + icon + seconds line
        const int colH = withLines ? (titleH + mDialSize + (tallestLines > 0 ? 2 + tallestLines : 0))
                                   : (titleH + mDialSize + sSecondsH);

        const int contentW = std::max(n * colW + std::max(0, n - 1) * mSpacing, 60);
        const int boxW = contentW + 2 * sPad;
        const int captionH = withLines ? 0 : sCaptionH + 2;   // potions box has no caption
        const int boxH = sPad + captionH + colH + sPad;
        const MyGUI::IntSize view = mMainWidget->getSize();
        box.frame->setCoord(view.width - mRightMargin - boxW, top, boxW, boxH);
        box.caption->setVisible(!withLines);
        box.caption->setCoord(sPad, sPad - 2, contentW, sCaptionH);

        for (size_t i = 0; i < box.dials.size(); ++i)
        {
            DialWidgets& w = box.dials[i];
            if (static_cast<int>(i) >= n)
            {
                w.root->setVisible(false);
                continue;
            }
            const int x = sPad + contentW - (static_cast<int>(i) + 1) * colW - static_cast<int>(i) * mSpacing;
            w.root->setCoord(x, sPad + captionH, colW, colH);
            w.root->setVisible(true);
            applyDial(w, data[i], colW, withLines, titleH, lineHeights[i]);
        }

        // 1 px gold verticals in the gaps between columns (potions box only)
        const int sepCount = withLines ? std::max(0, n - 1) : 0;
        while (static_cast<int>(box.separators.size()) < sepCount)
        {
            MyGUI::ImageBox* sep = box.frame->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, 0, 1, 10), MyGUI::Align::Default);
            sep->setImageTexture("white");
            sep->setColour(sRuleGold);
            sep->setAlpha(sRuleAlpha);
            sep->setNeedMouseFocus(false);
            box.separators.push_back(sep);
        }
        for (size_t i = 0; i < box.separators.size(); ++i)
        {
            const bool show = static_cast<int>(i) < sepCount;
            box.separators[i]->setVisible(show);
            if (!show)
                continue;
            // gap between column i (right) and column i+1 (left)
            const int gapRight = sPad + contentW - (static_cast<int>(i) + 1) * colW - static_cast<int>(i) * mSpacing;
            box.separators[i]->setCoord(gapRight - mSpacing / 2 - 1, sPad + captionH + 2, 1, colH - 4);
        }
    }

    void EffectDials::onFrame(float dt)
    {
        if (!mEnabled || !isVisible())
        {
            if (mResistBox)
                mResistBox->setVisible(false);   // detached root: hide it with the rest of the HUD
            if (mResistButton)
                mResistButton->setVisible(false);
            if (mStarRow)
                mStarRow->setVisible(false);
            if (mStarTip)
                mStarTip->setVisible(false);
            return;
        }
        mResistTooltipTimer += dt;
        mPulse += dt;
        if (mStarTip && mStarTip->getVisible())
        {
            if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
                mStarTip->setVisible(false);     // menu closed under the cursor: no lost-focus event arrives
            else
            {
                if (mStarTipDirty)
                    rebuildStarTip();
                positionStarTip();
            }
        }

        std::vector<DialData> columns;
        collect(columns);
        DialData sun;
        if (collectSunDamage(sun))
            columns.insert(columns.begin(), sun);   // index 0 = rightmost column: it stays put

        // a single box holds every column (the old separate Effects box is no longer used)
        mEffects.frame->setVisible(false);
        const bool show = !columns.empty() || mShowPotionsWhenEmpty;
        mPotions.frame->setVisible(show);
        if (show)
            fillBox(mPotions, columns, mTopMargin, true);

        updateResists();
    }

    namespace
    {
        // "#{fontcolour=x}" expands to "r g b a" floats (skin syntax); inline text needs "#RRGGBB"
        std::string colourCode(const char* tag)
        {
            const MyGUI::Colour c = MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags(tag));
            char buf[16];
            snprintf(buf, sizeof(buf), "#%02X%02X%02X", static_cast<int>(c.red * 255 + 0.5f),
                     static_cast<int>(c.green * 255 + 0.5f), static_cast<int>(c.blue * 255 + 0.5f));
            return buf;
        }
    }

    void EffectDials::setAnchors(HUD* hud, Hotbar* hotbar)
    {
        mHud = hud;
        mHotbar = hotbar;
        if (mHotbar)
            mHotbar->setStarSize(sStarPad);
    }

    int EffectDials::getStarSize() const
    {
        return sStarPad;
    }

    void EffectDials::onResistsClicked(MyGUI::Widget* /*sender*/)
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;
        mResistsExpanded = !mResistsExpanded;
        Settings::Manager::setBool("resists expanded", "EffectDials", mResistsExpanded);
    }

    void EffectDials::updateResists()
    {
        mResistBox->setVisible(mShowResists && mResistsExpanded);
        mResistButton->setVisible(mShowResists && !mResistsExpanded);
        if (!mShowResists)
            return;
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty())
            return;
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const MWMechanics::MagicEffects& effects = player.getClass().getCreatureStats(player).getMagicEffects();
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();

        for (ResistRow& row : mResistRows)
        {
            if (row.currentIcon.empty())
            {
                const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(row.resistId);
                if (effect)
                {
                    row.currentIcon = wm->correctIconPath(effect->mIcon);
                    row.icon->setImageTexture(row.currentIcon);
                }
            }
            // additive: the engine sums every source's magnitude per effect; net = resist - weakness
            float net = effects.get(MWMechanics::EffectKey(row.resistId)).getMagnitude();
            if (row.weaknessId >= 0)
                net -= effects.get(MWMechanics::EffectKey(row.weaknessId)).getMagnitude();
            const int v = static_cast<int>(std::lround(net));
            row.value->setCaption((v > 0 ? "+" : "") + std::to_string(v));
            row.value->setTextColour(v > 0 ? sTextGood : (v < 0 ? sTextHarmful : sTextNormalBright));
        }

        // right edge at a FIXED height (a quarter of the way down unless [EffectDials] resistances top is set),
        // so the column never moves when effect columns appear or vanish above it
        // star pulse: the icon itself breathes from white to gold and back (one cycle every ~6 s) while active,
        // plain white otherwise. Tinting the ImageBox multiplies the texture, so the star art keeps its shape.
        if (mStarActive)
        {
            const float p = 0.5f + 0.5f * std::sin(mPulse * 1.0f);   // 0..1
            mStarIcon->setColour(MyGUI::Colour(1.f, 1.f - 0.25f * p, 1.f - 0.65f * p));
            mStarIcon->setAlpha(0.85f + 0.15f * p);
        }
        else
        {
            mStarIcon->setColour(MyGUI::Colour::White);
            mStarIcon->setAlpha(1.f);
        }

        // star just left of the hotbar strip
        if (mHotbar)
            mStarRow->setCoord(mHotbar->getStarSlot());

        // resistances: a 3x2 grid (or the collapsed "Resists" button) sitting just above the HUD's active-effect
        // icons, right edge on that box's right edge; [EffectDials] resistances top overrides the vertical spot
        const MyGUI::IntSize view = mMainWidget->getSize();
        const int w = 3 * (sResistIcon + 4 + sResistValueW);
        const int h = 2 * sResistRowH;
        int right = view.width - mRightMargin;
        int bottom = view.height - 120;
        if (mHud && mHud->getEffectBox())
        {
            const MyGUI::IntCoord box = mHud->getEffectBox()->getAbsoluteCoord();
            right = box.right();
            bottom = box.top - 14;
        }
        if (mResistTop >= 0)
            bottom = mResistTop + h;
        mResistBox->setCoord(right - w, bottom - h, w, h);
        mResistButton->setCoord(right - mResistButton->getWidth(), bottom - mResistButton->getHeight(), mResistButton->getWidth(), mResistButton->getHeight());

        // a few times a second: star visibility + its tooltip; resist tooltips only matter while a menu is open
        if (mResistTooltipTimer >= 0.25f)
        {
            mResistTooltipTimer = 0.f;
            updateConstantStar();
            if (wm->isGuiMode())
                updateResistTooltips();
        }
    }

    namespace
    {
        // Every constant-effect enchantment on worn gear, grouped per effect (first-seen order):
        // lumped total plus the items contributing to it.
        struct ConstantSources : public MWMechanics::EffectSourceVisitor
        {
            struct Effect
            {
                MWMechanics::EffectKey key;
                float total;
                std::vector<std::pair<std::string, float>> items;   // item name, magnitude
            };
            std::vector<Effect> effects;
            void visit(MWMechanics::EffectKey key, int /*effectIndex*/, const std::string& sourceName,
                       const std::string& sourceId, int /*casterActorId*/, float magnitude,
                       float /*remainingTime*/, float /*totalTime*/) override
            {
                const std::string name = sourceName.empty() ? sourceId : sourceName;
                for (Effect& e : effects)
                    if (e.key.mId == key.mId && e.key.mArg == key.mArg)
                    {
                        e.total += magnitude;
                        e.items.emplace_back(name, magnitude);
                        return;
                    }
                effects.push_back({key, magnitude, {{name, magnitude}}});
            }
        };
    }

    void EffectDials::updateConstantStar()
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        mStarRow->setVisible(mShowConstant && mShowResists);
        if (!mShowConstant || player.isEmpty())
        {
            mStarActive = false;
            return;
        }
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();

        if (!mStarIconSet)   // first call: the icon is looked up once the store exists
        {
            mStarIconSet = true;
            const ESM::Miscellaneous* star = store.get<ESM::Miscellaneous>().search("misc_soulgem_azura");
            mStarIcon->setImageTexture(wm->correctIconPath(star ? star->mIcon : std::string("m\\tx_soulgem_grand.tga")));
        }

        ConstantSources sources;
        player.getClass().getInventoryStore(player).visitEffectSources(sources);   // worn constant enchantments only
        mStarActive = !sources.effects.empty();

        // one row per effect, alphabetical, full game names ("Resist Fire", "Fortify Attribute Luck")
        std::vector<StarLine> lines;
        for (const ConstantSources::Effect& e : sources.effects)
        {
            const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(e.key.mId);
            if (!effect)
                continue;
            StarLine l;
            l.name = wm->getGameSettingString(ESM::MagicEffect::effectIdToString(effect->mIndex), "");
            if (effect->mData.mFlags & ESM::MagicEffect::TargetSkill && e.key.mArg >= 0 && e.key.mArg < ESM::Skill::Length)
                l.name += " " + wm->getGameSettingString(ESM::Skill::sSkillNameIds[e.key.mArg], "");
            else if (effect->mData.mFlags & ESM::MagicEffect::TargetAttribute && e.key.mArg >= 0 && e.key.mArg < ESM::Attribute::Length)
                l.name += " " + wm->getGameSettingString(ESM::Attribute::sGmstAttributeIds[e.key.mArg], "");
            l.harmful = (effect->mData.mFlags & ESM::MagicEffect::Harmful) != 0;
            l.name += " " + std::to_string(static_cast<int>(std::lround(e.total)));
            for (size_t i = 0; i < e.items.size(); ++i)
                l.sources += (i ? ", " : "") + e.items[i].first + " " + std::to_string(static_cast<int>(std::lround(e.items[i].second)));
            lines.push_back(l);
        }
        std::sort(lines.begin(), lines.end(), [](const StarLine& a, const StarLine& b) { return a.name < b.name; });
        bool changed = lines.size() != mStarLines.size();
        for (size_t i = 0; !changed && i < lines.size(); ++i)
            changed = lines[i].name != mStarLines[i].name || lines[i].sources != mStarLines[i].sources;
        if (changed)
        {
            mStarLines.swap(lines);
            mStarTipDirty = true;
        }
    }

    void EffectDials::onStarFocus(MyGUI::Widget* /*sender*/, MyGUI::Widget* /*old*/)
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;
        if (mStarTipDirty)
            rebuildStarTip();
        positionStarTip();
        mStarTip->setVisible(true);
    }

    void EffectDials::onStarLostFocus(MyGUI::Widget* /*sender*/, MyGUI::Widget* /*now*/)
    {
        mStarTip->setVisible(false);
    }

    void EffectDials::rebuildStarTip()
    {
        mStarTipDirty = false;
        while (mStarTip->getChildCount())
            MyGUI::Gui::getInstance().destroyWidget(mStarTip->getChildAt(0));

        const MyGUI::Colour cHeader = MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=header}"));
        const MyGUI::Colour cNormal = MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal}"));
        const MyGUI::Colour cNegative = MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=negative}"));
        const int pad = 8, gap = 16, rowGap = 2, nameMax = 320;
        const int srcW = std::max(200, settingInt("constant sources width", 420));   // wrap width of the items column

        MyGUI::TextBox* header = mStarTip->createWidget<MyGUI::TextBox>("SandText", MyGUI::IntCoord(pad, pad, 400, 20), MyGUI::Align::Default);
        header->setCaption("Constant Effects");
        header->setTextColour(cHeader);
        header->setNeedMouseFocus(false);
        header->setSize(header->getTextSize().width, header->getTextSize().height);
        int y = pad + header->getHeight() + 4;
        int width = pad + header->getWidth() + pad;

        if (mStarLines.empty())
        {
            MyGUI::TextBox* none = mStarTip->createWidget<MyGUI::TextBox>("SandText", MyGUI::IntCoord(pad, y, 400, 20), MyGUI::Align::Default);
            none->setCaption("Nothing worn has a constant effect");
            none->setTextColour(cNormal);
            none->setNeedMouseFocus(false);
            none->setSize(none->getTextSize().width, none->getTextSize().height);
            width = std::max(width, pad + none->getWidth() + pad);
            y += none->getHeight();
            mStarTip->setSize(width, y + pad);
            return;
        }

        // pass 1: name column, measured
        std::vector<MyGUI::TextBox*> names;
        int nameW = 0;
        for (const StarLine& l : mStarLines)
        {
            MyGUI::TextBox* t = mStarTip->createWidget<MyGUI::TextBox>("SandText", MyGUI::IntCoord(pad, 0, nameMax, 20), MyGUI::Align::Default);
            t->setCaption(l.name);
            t->setTextColour(l.harmful ? cNegative : cHeader);
            t->setNeedMouseFocus(false);
            nameW = std::max(nameW, std::min(nameMax, t->getTextSize().width));
            names.push_back(t);
        }
        // pass 2: rows, with the items column word-wrapped at a fixed width so continuation lines line up
        for (size_t i = 0; i < mStarLines.size(); ++i)
        {
            MyGUI::EditBox* src = mStarTip->createWidget<MyGUI::EditBox>("SandText", MyGUI::IntCoord(pad + nameW + gap, y, srcW, 400), MyGUI::Align::Default);
            src->setEditStatic(true);
            src->setEditMultiLine(true);
            src->setEditWordWrap(true);
            src->setNeedKeyFocus(false);
            src->setNeedMouseFocus(false);
            src->setTextColour(cNormal);
            src->setCaption(mStarLines[i].sources);
            const int srcH = std::max(names[i]->getTextSize().height, src->getTextSize().height);
            src->setSize(srcW, srcH);
            names[i]->setCoord(pad, y, nameW, names[i]->getTextSize().height);
            y += srcH + rowGap;
        }
        width = std::max(width, pad + nameW + gap + srcW + pad);
        mStarTip->setSize(width, y - rowGap + pad);
    }

    void EffectDials::positionStarTip()
    {
        // same placement rule as the game's tooltips: below the cursor, slid left in proportion to how far
        // right the cursor is, flipped above when it would run off the bottom, and never off the top
        const MyGUI::IntPoint mouse = MyGUI::InputManager::getInstance().getMousePosition();
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        const MyGUI::IntSize size = mStarTip->getSize();
        MyGUI::IntPoint pos(mouse.left - static_cast<int>(mouse.left / float(view.width) * size.width), mouse.top + 32);
        if (pos.left + size.width > view.width) pos.left = view.width - size.width;
        if (pos.top + size.height > view.height) pos.top = mouse.top - size.height - 8;
        pos.left = std::max(0, pos.left);
        pos.top = std::max(0, pos.top);
        mStarTip->setPosition(pos);
    }

    namespace
    {
        // Collects every source (potion, spell, ability incl. racials/birthsigns, constant enchantment)
        // contributing to one resistance and its weakness counterpart.
        struct ResistSources : public MWMechanics::EffectSourceVisitor
        {
            int resistId, weaknessId;
            std::vector<std::pair<std::string, float>> resist, weakness;
            ResistSources(int r, int w) : resistId(r), weaknessId(w) {}
            void visit(MWMechanics::EffectKey key, int /*effectIndex*/, const std::string& sourceName,
                       const std::string& sourceId, int /*casterActorId*/, float magnitude,
                       float /*remainingTime*/, float /*totalTime*/) override
            {
                const std::string name = sourceName.empty() ? sourceId : sourceName;
                if (key.mId == resistId)
                    resist.emplace_back(name, magnitude);
                else if (weaknessId >= 0 && key.mId == weaknessId)
                    weakness.emplace_back(name, magnitude);
            }
        };
    }

    void EffectDials::updateResistTooltips()
    {
        static const std::string cHeader = colourCode("#{fontcolour=header}");
        static const std::string cNormal = colourCode("#{fontcolour=normal}");
        static const std::string cNegative = colourCode("#{fontcolour=negative}");
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty())
            return;
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
        MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        MWWorld::InventoryStore& inventory = player.getClass().getInventoryStore(player);

        for (ResistRow& row : mResistRows)
        {
            ResistSources sources(row.resistId, row.weaknessId);
            inventory.visitEffectSources(sources);                 // constant-effect enchantments worn
            stats.getSpells().visitEffectSources(sources);         // abilities, incl. racials / birthsigns
            stats.getActiveSpells().visitEffectSources(sources);   // timed spells and potions

            const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(row.resistId);
            std::string header = effect ? wm->getGameSettingString(ESM::MagicEffect::effectIdToString(effect->mIndex), "") : "";
            float net = 0.f;
            for (const auto& s : sources.resist) net += s.second;
            for (const auto& s : sources.weakness) net -= s.second;
            const int n = static_cast<int>(std::lround(net));
            std::string text = cHeader + header + " " + (n > 0 ? "+" : "") + std::to_string(n) + cNormal;
            for (const auto& s : sources.resist)
                text += "\n" + s.first + "  +" + std::to_string(static_cast<int>(std::lround(s.second)));
            for (const auto& s : sources.weakness)
                text += "\n" + cNegative + s.first + "  -" + std::to_string(static_cast<int>(std::lround(s.second))) + cNormal;
            if (sources.resist.empty() && sources.weakness.empty())
                text += "\nNo active sources";
            if (text != row.tooltip)
            {
                row.tooltip = text;
                row.row->setUserString("Caption_Text", text);
            }
        }
    }
}
