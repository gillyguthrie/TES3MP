#ifndef OPENMW_MWMP_SESSIONLOG_HPP
#define OPENMW_MWMP_SESSIONLOG_HPP

/*
    majere addition (session log)

    One plain-text file per session in <log dir>/sessionlogs/session-<timestamp>.log
    (the same folder as tes3mp-client-*.log), tab-separated:  HH:MM:SS.mmm <TAB> [TAG] <TAB> text

    Tags:
      SESSION  start/end, server, character name
      CELL     cell changes
      CHAT     every chat line exactly as received (colour codes stripped)
      MSG      server message boxes / dialogs (label text)
      KEY      quick key activated (slot + what it held)
      HP/MP/FP health / magicka / fatigue changes worth noting (current/max, delta) with the last attacker if known
      HIT      the engine's "last hit by" bookkeeping changed (attacker name / id)
      EFFECT+  a new active effect source (potion / spell / enchantment / server effect) with its effects,
               magnitudes, durations and the caster's name
      EFFECT-  that source expired
      DEATH    health reached zero, with a snapshot of the active harmful effects

    Read-only: it never sends anything. [SessionLog] enabled = false in settings.cfg turns it off.
*/

#include <fstream>
#include <set>
#include <string>

namespace mwmp
{
    class SessionLog
    {
    public:
        static SessionLog& get();

        void start(const std::string& server, unsigned short port);   // opens the file (idempotent)
        void end();

        void chat(const std::string& msg);
        void message(const std::string& kind, const std::string& label);
        void quickKey(int index, const std::string& what);
        void note(const char* tag, const std::string& text);   // any other feature's tagged line
        void update(float dt);   // per-frame sampling of stats / cell / active effects

        bool isEnabled() const { return mEnabled; }

    private:
        SessionLog();
        void line(const char* tag, const std::string& text);
        static std::string clock();
        static std::string stripColours(const std::string& s);
        std::string actorName(int actorId) const;
        std::string harmfulSnapshot() const;

        bool mEnabled;
        std::ofstream mFile;
        std::string mName;
        std::string mCell;
        float mHealth, mMagicka, mFatigue;
        bool mDead;
        std::string mLastHit, mLastHitAttempt;
        int mHitAttemptActor;
        std::set<std::string> mActive;   // keys of active spell entries already logged
        float mFlushTimer;
    };
}

#endif
