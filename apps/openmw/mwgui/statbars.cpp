/*
    majere addition (stat bars) -- see statbars.hpp
*/
#include "statbars.hpp"

#include <algorithm>
#include <cmath>

#include <MyGUI_Gui.h>
#include <MyGUI_RenderManager.h>

#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/magiceffects.hpp"
#include "../mwmechanics/spells.hpp"
#include "../mwmechanics/activespells.hpp"

#include "../mwbase/windowmanager.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"

#include <MyGUI_LanguageManager.h>
#include <cstdio>

#include "../mwworld/cellstore.hpp"

#include <components/esm/loadmgef.hpp>

#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"

#include "hotbar.hpp"

namespace
{
    int settingInt(const char* key, int def)
    {
        try { return Settings::Manager::getInt(key, "StatBars"); } catch (...) { return def; }
    }
    bool settingBool(const char* key, bool def)
    {
        try { return Settings::Manager::getBool(key, "StatBars"); } catch (...) { return def; }
    }

    const int sPad = 3;         // inside the panel
    const int sBarGap = 6;      // between bars
}

namespace MWGui
{
    StatBars::StatBars(Hotbar* hotbar)
        : mEnabled(true), mWidth(390), mGap(14), mLeftMargin(172), mTextNudge(-3), mHotbar(hotbar), mBarHeight(22), mRoot(nullptr)
        , mTooltipTimer(0.f)
    {
        mEnabled = settingBool("enabled", true);
        mWidth   = settingInt("width", 0);                    // 0 = fill from left margin to the hotbar
        mGap     = std::max(0, settingInt("gap", 14));
        mLeftMargin = std::max(0, settingInt("left margin", 172));   // the HUD's spell box ends at 158 (sneak icon only shows while sneaking)
        mTextNudge = settingInt("text nudge", -3);
        mBarHeight = std::max(10, std::min(44, settingInt("bar height", 22)));

        mRoot = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>("", MyGUI::IntCoord(0, 0, mWidth, 60), MyGUI::Align::Default, "HUD");
        mRoot->setNeedMouseFocus(false);

        const char* skins[3] = { "MW_Progress_Red", "MW_Progress_Blue", "MW_Progress_Green" };   // the game's own bar art
        Bar* bars[3] = { &mHealth, &mMagicka, &mFatigue };
        for (int i = 0; i < 3; ++i)
        {
            Bar& b = *bars[i];
            b.bar = mRoot->createWidget<MyGUI::ProgressBar>(skins[i], MyGUI::IntCoord(0, 0, 10, 10), MyGUI::Align::Default);
            b.bar->setNeedMouseFocus(false);
            b.text = b.bar->createWidget<MyGUI::TextBox>("SandBrightText", MyGUI::IntCoord(0, 0, 10, 10), MyGUI::Align::Default);
            b.text->setTextAlign(MyGUI::Align::Center);
            b.text->setTextShadow(true);
            b.text->setNeedMouseFocus(false);
            // the per-second signs are their own Menu-layer roots (above the bar) so they can be hovered for a
            // tooltip while a menu is open; the HUD layer is never picked
            MyGUI::TextBox** signs[2] = { &b.minus, &b.plus };
            for (int k = 0; k < 2; ++k)
            {
                MyGUI::TextBox* t = MyGUI::Gui::getInstance().createWidget<MyGUI::TextBox>("SandBrightText",
                    MyGUI::IntCoord(0, 0, 24, 24), MyGUI::Align::Default, "Menu");
                t->setCaption(k == 0 ? "-" : "+");
                t->setFontHeight(t->getFontHeight() * 2);   // a good bit bigger than the bar text
                t->setTextColour(k == 0 ? MyGUI::Colour(0.95f, 0.35f, 0.3f) : MyGUI::Colour(0.45f, 0.9f, 0.45f));
                t->setTextAlign(MyGUI::Align::Center);
                t->setTextShadow(true);
                t->setNeedMouseFocus(true);
                t->setUserString("ToolTipType", "Layout");
                t->setUserString("ToolTipLayout", "TextToolTip");
                t->setUserString("Caption_Text", "");
                t->setVisible(false);
                *signs[k] = t;
            }
        }
        mRoot->setVisible(false);
        place();
    }

    StatBars::~StatBars()
    {
        Bar* bars[3] = { &mHealth, &mMagicka, &mFatigue };
        for (Bar* b : bars)
        {
            if (b->minus) MyGUI::Gui::getInstance().destroyWidget(b->minus);
            if (b->plus) MyGUI::Gui::getInstance().destroyWidget(b->plus);
        }
        if (mRoot)
            MyGUI::Gui::getInstance().destroyWidget(mRoot);
    }

    void StatBars::place()
    {
        // left of the hotbar strip, same top and height; three equal bars stacked inside
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        MyGUI::IntCoord strip(view.width / 2 - 200, view.height - 70, 400, 60);
        if (mHotbar)
            strip = mHotbar->getStripCoord();
        // fill the space between the HUD's bottom-left boxes and Azura's Star / the hotbar (or a fixed width)
        int rightEdge = strip.left;
        if (mHotbar && mHotbar->getStarSlot().width > 0)
            rightEdge = mHotbar->getStarSlot().left;
        const int width = (mWidth > 0) ? mWidth : std::max(150, rightEdge - mGap - mLeftMargin);
        mRoot->setCoord(rightEdge - mGap - width, strip.top, width, strip.height);

        // three bars side by side, level with the hotbar's slot row (the strip's top part is the key labels)
        const int slotRowH = 44;                       // 38 px slots + 3 px halo each side (hotbar defaults)
        const int barH = mBarHeight;
        const int y = std::max(0, strip.height - slotRowH) + (slotRowH - barH) / 2;
        const int barW = (width - 2 * sPad - 2 * sBarGap) / 3;
        Bar* bars[3] = { &mHealth, &mMagicka, &mFatigue };
        for (int i = 0; i < 3; ++i)
        {
            const int bx = sPad + i * (barW + sBarGap);
            bars[i]->bar->setCoord(bx, y, barW, barH);
            bars[i]->text->setCoord(0, mTextNudge, barW - 4, barH - 4);
            // signs: just above the bar's ends (own roots: screen coordinates)
            const int rx = mRoot->getLeft(), ry = mRoot->getTop();
            bars[i]->minus->setCoord(rx + bx, ry + y - 26, 24, 24);
            bars[i]->plus->setCoord(rx + bx + barW - 24, ry + y - 26, 24, 24);
        }
    }

    void StatBars::setVisible(bool visible)
    {
        if (mRoot)
            mRoot->setVisible(visible && mEnabled);
        if (!(visible && mEnabled))
        {
            Bar* bars[3] = { &mHealth, &mMagicka, &mFatigue };
            for (Bar* b : bars)
            {
                if (b->minus) b->minus->setVisible(false);
                if (b->plus) b->plus->setVisible(false);
            }
        }
    }

    void StatBars::update(Bar& b, float current, float maximum)
    {
        const int cur = std::max(0, static_cast<int>(std::lround(current)));
        const int max = std::max(0, static_cast<int>(std::lround(maximum)));
        if (cur == b.current && max == b.maximum)
            return;
        b.current = cur;
        b.maximum = max;
        b.bar->setProgressRange(std::max(1, max));
        b.bar->setProgressPosition(std::min(cur, std::max(1, max)));
        b.text->setCaption(std::to_string(cur) + " / " + std::to_string(max));
    }

    void StatBars::onFrame(float dt)
    {
        if (!mEnabled || !mRoot->getVisible())
            return;
        place();
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty())
            return;
        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        update(mHealth,  stats.getHealth().getCurrent(),  stats.getHealth().getModified());
        update(mMagicka, stats.getMagicka().getCurrent(), stats.getMagicka().getModified());
        update(mFatigue, stats.getFatigue().getCurrent(), stats.getFatigue().getModified());

        // per-second effects on each stat (aggregate of every source: spells, potions, worn enchantments)
        const MWMechanics::MagicEffects& fx = stats.getMagicEffects();
        static const int healthDamage[]  = { ESM::MagicEffect::DamageHealth, ESM::MagicEffect::Poison, ESM::MagicEffect::FireDamage,
                                             ESM::MagicEffect::FrostDamage, ESM::MagicEffect::ShockDamage, ESM::MagicEffect::AbsorbHealth };
        static const int magickaDamage[] = { ESM::MagicEffect::DamageMagicka, ESM::MagicEffect::AbsorbMagicka };
        static const int fatigueDamage[] = { ESM::MagicEffect::DamageFatigue, ESM::MagicEffect::AbsorbFatigue };
        // sun damage only counts while it would actually be ticking: outside, in daylight
        bool sun = false;
        if (fx.get(MWMechanics::EffectKey(ESM::MagicEffect::SunDamage)).getMagnitude() > 0.f && player.isInCell() && player.getCell()->isExterior())
        {
            const float hour = MWBase::Environment::get().getWorld()->getTimeStamp().getHour();
            sun = std::abs(hour - 13.f) < 7.f;
        }
        updateSigns(mHealth,  fx, healthDamage,  6, ESM::MagicEffect::RestoreHealth,  sun);
        updateSigns(mMagicka, fx, magickaDamage, 2, ESM::MagicEffect::RestoreMagicka, false);
        updateSigns(mFatigue, fx, fatigueDamage, 2, ESM::MagicEffect::RestoreFatigue, false);

        // tooltips only matter while a menu is open; refresh them a few times a second
        mTooltipTimer += dt;
        if (mTooltipTimer >= 0.5f && MWBase::Environment::get().getWindowManager()->isGuiMode())
        {
            mTooltipTimer = 0.f;
            updateSignTooltips(mHealth,  healthDamage,  6, ESM::MagicEffect::RestoreHealth);
            updateSignTooltips(mMagicka, magickaDamage, 2, ESM::MagicEffect::RestoreMagicka);
            updateSignTooltips(mFatigue, fatigueDamage, 2, ESM::MagicEffect::RestoreFatigue);
        }
    }

    namespace
    {
        // every source (potion, spell, ability, worn enchantment) of a set of effects on the player
        struct SignSources : public MWMechanics::EffectSourceVisitor
        {
            const int* damageIds; int nDamage; int restoreId;
            std::vector<std::pair<std::string, float>> damage, restore;   // "Effect: source", magnitude
            SignSources(const int* d, int n, int r) : damageIds(d), nDamage(n), restoreId(r) {}
            void visit(MWMechanics::EffectKey key, int /*effectIndex*/, const std::string& sourceName,
                       const std::string& sourceId, int /*casterActorId*/, float magnitude,
                       float /*remainingTime*/, float /*totalTime*/) override
            {
                if (magnitude <= 0.f)
                    return;
                const std::string src = sourceName.empty() ? sourceId : sourceName;
                const ESM::MagicEffect* effect = MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().search(key.mId);
                const std::string name = effect ? MWBase::Environment::get().getWindowManager()->getGameSettingString(ESM::MagicEffect::effectIdToString(effect->mIndex), "") : "";
                if (key.mId == restoreId)
                    restore.emplace_back(name + ": " + src, magnitude);
                for (int i = 0; i < nDamage; ++i)
                    if (key.mId == damageIds[i])
                        damage.emplace_back(name + ": " + src, magnitude);
            }
        };
        std::string colourHex(const char* tag)
        {
            const MyGUI::Colour c = MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags(tag));
            char buf[16];
            snprintf(buf, sizeof(buf), "#%02X%02X%02X", static_cast<int>(c.red * 255 + 0.5f), static_cast<int>(c.green * 255 + 0.5f), static_cast<int>(c.blue * 255 + 0.5f));
            return buf;
        }
    }

    void StatBars::updateSignTooltips(Bar& b, const int* damageIds, int nDamage, int restoreId)
    {
        static const std::string cHeader = colourHex("#{fontcolour=header}");
        static const std::string cNormal = colourHex("#{fontcolour=normal}");
        static const std::string cNegative = colourHex("#{fontcolour=negative}");
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty())
            return;
        MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        SignSources v(damageIds, nDamage, restoreId);
        player.getClass().getInventoryStore(player).visitEffectSources(v);   // worn constant enchantments
        stats.getSpells().visitEffectSources(v);                             // abilities, diseases, curses
        stats.getActiveSpells().visitEffectSources(v);                       // timed spells and potions
        auto fmt = [](float m) { char buf[16]; snprintf(buf, sizeof(buf), (m < 10.f && std::abs(m - std::lround(m)) > 0.05f) ? "%.1f" : "%.0f", m); return std::string(buf); };
        std::string minus = cHeader + "Losing per second" + cNormal;
        for (const auto& d : v.damage) minus += "\n" + cNegative + d.first + "  " + fmt(d.second) + "/s" + cNormal;
        if (v.damage.empty()) minus += "\nnothing";
        std::string plus = cHeader + "Restoring per second" + cNormal;
        for (const auto& r : v.restore) plus += "\n" + r.first + "  " + fmt(r.second) + "/s";
        if (v.restore.empty()) plus += "\nnothing";
        b.minus->setUserString("Caption_Text", minus);
        b.plus->setUserString("Caption_Text", plus);
    }

    void StatBars::updateSigns(Bar& b, const MWMechanics::MagicEffects& effects, const int* damageIds, int nDamage, int restoreId, bool sunDamage)
    {
        bool losing = sunDamage;
        for (int i = 0; i < nDamage && !losing; ++i)
            losing = effects.get(MWMechanics::EffectKey(damageIds[i])).getMagnitude() > 0.f;
        const bool gaining = effects.get(MWMechanics::EffectKey(restoreId)).getMagnitude() > 0.f;
        if (b.minus->getVisible() != losing) b.minus->setVisible(losing);
        if (b.plus->getVisible() != gaining) b.plus->setVisible(gaining);
    }
}
