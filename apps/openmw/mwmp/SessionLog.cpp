/*
    majere addition (session log) -- see SessionLog.hpp
*/
#include "SessionLog.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

#include <boost/filesystem.hpp>

#include <components/files/configurationmanager.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/settings/settings.hpp>
#include <components/esm/loadmgef.hpp>
#include <components/esm/loadnpc.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/activespells.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/timestamp.hpp"

namespace
{
    bool settingEnabled()
    {
        try { return Settings::Manager::getBool("enabled", "SessionLog"); } catch (...) { return true; }
    }

    std::string fmt1(float v)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", v);
        return buf;
    }
}

namespace mwmp
{
    SessionLog& SessionLog::get()
    {
        static SessionLog instance;
        return instance;
    }

    SessionLog::SessionLog()
        : mEnabled(true), mHealth(-1.f), mMagicka(-1.f), mFatigue(-1.f), mDead(false), mHitAttemptActor(-1), mFlushTimer(0.f)
    {
    }

    std::string SessionLog::clock()
    {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const std::time_t t = system_clock::to_time_t(now);
        const int ms = static_cast<int>(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
        return buf;
    }

    std::string SessionLog::stripColours(const std::string& s)
    {
        // MyGUI inline colours are "#RRGGBB"; "#{...}" tags are left alone
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '#' && i + 6 < s.size() && s[i + 1] != '{')
            {
                bool hex = true;
                for (size_t k = 1; k <= 6; ++k)
                    hex = hex && isxdigit(static_cast<unsigned char>(s[i + k]));
                if (hex) { i += 6; continue; }
            }
            out += s[i];
        }
        return out;
    }

    void SessionLog::line(const char* tag, const std::string& text)
    {
        if (!mEnabled || !mFile.is_open())
            return;
        mFile << clock() << '\t' << '[' << tag << ']' << '\t' << text << '\n';
    }

    void SessionLog::start(const std::string& server, unsigned short port)
    {
        mEnabled = settingEnabled();
        if (!mEnabled || mFile.is_open())
            return;
        Files::ConfigurationManager cfgMgr;
        boost::filesystem::path dir = cfgMgr.getLogPath() / "sessionlogs";
        boost::system::error_code ec;
        boost::filesystem::create_directories(dir, ec);
        const boost::filesystem::path file = dir / ("session-" + TimedLog::getFilenameTimestamp() + ".log");
        mFile.open(file.string(), std::ios::out | std::ios::trunc);
        if (!mFile.is_open())
        {
            mEnabled = false;
            return;
        }
        line("SESSION", "start  server=" + server + ":" + std::to_string(port) + "  file=" + file.string());
        mFile.flush();
    }

    void SessionLog::end()
    {
        if (mFile.is_open())
        {
            line("SESSION", "end");
            mFile.close();
        }
    }

    void SessionLog::chat(const std::string& msg)
    {
        // one CHAT entry per text line (multi-line server messages arrive as a single string)
        std::istringstream in(stripColours(msg));
        std::string part;
        while (std::getline(in, part))
        {
            while (!part.empty() && part.back() == '\r')
                part.pop_back();
            if (!part.empty())
                line("CHAT", part);
        }
        mFile.flush();   // chat is the part most worth having after a crash
    }

    void SessionLog::message(const std::string& kind, const std::string& label)
    {
        std::string text = stripColours(label);
        for (char& c : text) if (c == '\n') c = ' ';
        line("MSG", kind + ": " + text);
    }

    void SessionLog::quickKey(int index, const std::string& what)
    {
        line("KEY", std::to_string(index) + "  " + what);
    }

    void SessionLog::note(const char* tag, const std::string& text)
    {
        line(tag, text);
    }

    std::string SessionLog::actorName(int actorId) const
    {
        if (actorId < 0)
            return "";
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::Ptr actor = MWBase::Environment::get().getWorld()->searchPtrViaActorId(actorId);
        if (actor.isEmpty())
            return "actor#" + std::to_string(actorId);
        if (actor == player)
            return "self";
        return actor.getClass().getName(actor) + " (" + actor.getCellRef().getRefId() + ")";
    }

    std::string SessionLog::harmfulSnapshot() const
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty())
            return "";
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const MWMechanics::ActiveSpells& active = player.getClass().getCreatureStats(player).getActiveSpells();
        std::string out;
        for (MWMechanics::ActiveSpells::TIterator it = active.begin(); it != active.end(); ++it)
        {
            for (const MWMechanics::ActiveSpells::ActiveEffect& e : it->second.mEffects)
            {
                const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(e.mEffectId);
                if (!effect || !(effect->mData.mFlags & ESM::MagicEffect::Harmful) || e.mTimeLeft <= 0.f)
                    continue;
                if (!out.empty()) out += "; ";
                out += it->second.mDisplayName + ":" + ESM::MagicEffect::effectIdToString(e.mEffectId)
                     + " " + fmt1(e.mMagnitude) + " (" + fmt1(e.mTimeLeft) + "s left)";
            }
        }
        return out.empty() ? "none" : out;
    }

    void SessionLog::update(float dt)
    {
        if (!mEnabled || !mFile.is_open())
            return;
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty())
            return;
        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();

        // character name (once known)
        if (mName.empty())
        {
            const std::string name = player.get<ESM::NPC>()->mBase->mName;   // getName() answers "player"
            if (!name.empty() && name != "player")
            {
                mName = name;
                line("SESSION", "character=" + mName);
            }
        }

        // cell
        if (player.getCell() && player.getCell()->getCell())
        {
            const std::string cell = player.getCell()->getCell()->getDescription();
            if (cell != mCell)
            {
                mCell = cell;
                line("CELL", cell);
            }
        }

        // attacker bookkeeping (the engine records who last hit / tried to hit us)
        const std::string& lastHit = stats.getLastHitObject();
        const std::string& lastAttempt = stats.getLastHitAttemptObject();
        const int attemptActor = stats.getHitAttemptActorId();
        if (lastHit != mLastHit || lastAttempt != mLastHitAttempt || attemptActor != mHitAttemptActor)
        {
            mLastHit = lastHit; mLastHitAttempt = lastAttempt; mHitAttemptActor = attemptActor;
            std::string who = actorName(attemptActor);
            line("HIT", "lastHit=" + lastHit + "  lastAttempt=" + lastAttempt + "  attacker=" + who);
        }

        // vitals: log every drop, and gains of 5+ (regen ticks are ignored)
        const float hp = stats.getHealth().getCurrent(), hpMax = stats.getHealth().getModified();
        const float mp = stats.getMagicka().getCurrent(), mpMax = stats.getMagicka().getModified();
        const float fp = stats.getFatigue().getCurrent(), fpMax = stats.getFatigue().getModified();
        if (mHealth >= 0.f)
        {
            const float d = hp - mHealth;
            if (d <= -1.f || d >= 5.f)
                line("HP", fmt1(hp) + "/" + fmt1(hpMax) + "  (" + (d > 0 ? "+" : "") + fmt1(d) + ")"
                           + (d < 0 && !mLastHit.empty() ? "  lastHit=" + mLastHit : "")
                           + (d < 0 && mHitAttemptActor >= 0 ? "  attacker=" + actorName(mHitAttemptActor) : ""));
            const float dm = mp - mMagicka;
            if (dm <= -5.f || dm >= 20.f)
                line("MP", fmt1(mp) + "/" + fmt1(mpMax) + "  (" + (dm > 0 ? "+" : "") + fmt1(dm) + ")");
            const float df = fp - mFatigue;
            if (df <= -20.f || df >= 50.f)
                line("FP", fmt1(fp) + "/" + fmt1(fpMax) + "  (" + (df > 0 ? "+" : "") + fmt1(df) + ")");
        }
        mHealth = hp; mMagicka = mp; mFatigue = fp;

        // death
        const bool dead = stats.isDead() || hp <= 0.f;
        if (dead && !mDead)
        {
            line("DEATH", "hp=" + fmt1(hp) + "  lastHit=" + mLastHit + "  attacker=" + actorName(mHitAttemptActor)
                        + "  harmful effects: " + harmfulSnapshot());
            mFile.flush();
        }
        mDead = dead;

        // active effect sources appearing / expiring
        std::set<std::string> now;
        const MWMechanics::ActiveSpells& active = stats.getActiveSpells();
        for (MWMechanics::ActiveSpells::TIterator it = active.begin(); it != active.end(); ++it)
        {
            const MWMechanics::ActiveSpells::ActiveSpellParams& p = it->second;
            const std::string key = it->first + "@" + std::to_string(p.mTimeStamp.getDay()) + ":" + fmt1(p.mTimeStamp.getHour() * 3600.f);
            now.insert(key);
            if (mActive.count(key))
                continue;
            std::string text = (p.mDisplayName.empty() ? it->first : p.mDisplayName) + "  from=" +
                               (p.mCasterActorId >= 0 ? actorName(p.mCasterActorId) : "?") + "  effects:";
            for (const MWMechanics::ActiveSpells::ActiveEffect& e : p.mEffects)
            {
                const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(e.mEffectId);
                text += " " + (effect ? ESM::MagicEffect::effectIdToString(e.mEffectId) : std::to_string(e.mEffectId))
                      + "(" + fmt1(e.mMagnitude) + " for " + fmt1(e.mDuration) + "s" + (e.mArg >= 0 ? " arg " + std::to_string(e.mArg) : "") + ")";
            }
            line("EFFECT+", text);
        }
        for (const std::string& key : mActive)
            if (!now.count(key))
                line("EFFECT-", key.substr(0, key.find('@')));
        mActive.swap(now);

        // keep the file current without flushing every frame
        mFlushTimer += dt;
        if (mFlushTimer >= 2.f)
        {
            mFlushTimer = 0.f;
            mFile.flush();
        }
    }
}
