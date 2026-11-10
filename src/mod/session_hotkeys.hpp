#pragma once
// Death statistics, developer hotkeys (F8 realtime override, probe requests).
#include "mod/fxcensus.hpp"

namespace p1 {

namespace deadstat {
inline long long g_total = 0;      // total physics ticks advanced during the session
inline long long g_dead = 0;       // of which, ticks where the player was dead
inline long long g_streak = 0;     // current run of consecutive dead ticks
inline long long g_maxStreak = 0;  // longest
inline long long g_episodes = 0;   // number of death episodes (= effective attempt count)
inline void tick(bool dead) {
    ++g_total;
    if (dead) {
        ++g_dead;
        if (++g_streak > g_maxStreak) g_maxStreak = g_streak;
    } else {
        if (g_streak > 0) ++g_episodes;
        g_streak = 0;
    }
}
}

// --- Manual pause / step (F2 / F3 / F4) ---
// Hotkeys cannot touch PlayerObject from the key-polling context, so only a request is raised
// here and consumed in the update hook. 2=pause/resume 3=step 1 substep 4=step 10
inline int g_probeRequest = 0;

// Measurement of orb firing (cfg `orbtrace=1`). Because of the press buffer (a press before
// contact fires at the moment of contact), early presses within the window collapse onto the
// same firing point
#include "solver/sweep.hpp"

inline void endSession(const std::string& why);
// Release the manual (F2) pause. Defined further down the chain -- probe:: is not visible from
// here, and the keys have to be handled in this header
inline void clearManualPause();
// ...and whether it is on, and whether a step is still owed. All defined in helpers_game.hpp,
// where probe:: is visible.
inline bool manualPaused();
inline bool manualStepPending();
// The replay panel's key (replay_panel.hpp, much further down the chain, where the solver's state it draws is visible).
namespace replaypanel {
inline void toggle();
inline bool wanted();
// ...and the transport keys it takes over while the screen is dark (see owns() there)
inline bool owns();
inline void keyPause();
inline void keyStep(int ticks);
inline void keySeek(long long ticks);
inline void keySpeed(int dir);
}

// The keys below are Geode keybind settings, declared in mod.json, so they can be rebound and
// they arrive through the game's own input path.
//
// They used to be read with GetAsyncKeyState and hardcoded virtual key codes. That is a platform
// call where Geode has its own facility, it skips cocos' keyboard dispatcher, and it reports the
// physical key regardless of which window has focus -- which is why there was a foreground check
// here at all, comparing the foreground window's process to our own. Going through Geode's input
// was meant to remove the need for it, and the check is back (hotkeysFocused, below): a worker that
// has taken the foreground and been left behind must not act on keys typed into another window.
//
// A press event says only "down" or "up". The timing further down needs to know how long a key
// has been held (the repeat delay on the step keys, the acceleration on the transport keys), so
// the held state is mirrored here and the polling structure is left exactly as it was.
enum class Key {
    Overlay, Pause, Step1, Step10, Render, Hitboxes, Replay, Quit, Itermap,
    Faster, Slower, SeekBack, SeekForward, TakeoverEnd, ReplayPanel, Count
};

inline bool g_keyDown[(size_t)Key::Count] = {};

// In the order of the enum.
inline const char* const kKeySettings[(size_t)Key::Count] = {
    "key-overlay", "key-pause", "key-step1", "key-step10", "key-render", "key-hitboxes",
    "key-replay", "key-quit", "key-itermap", "key-faster", "key-slower",
    "key-seek-back", "key-seek-forward", "key-takeover-end", "key-replaypanel"
};

inline bool hotkeysFocused();

inline bool keyHeld(Key k) {
    if (!g_keyDown[(size_t)k]) {
#ifdef GEODE_IS_WINDOWS
        if (k == Key::ReplayPanel && hotkeysFocused()) {
            auto binds = Mod::get()->getSettingValue<std::vector<Keybind>>(kKeySettings[(size_t)k]);
            if (!binds.empty() && binds.front().key == cocos2d::enumKeyCodes::KEY_RightShift) {
                return (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
            }
        }
#endif
        return false;
    }
#ifdef GEODE_IS_WINDOWS
    if (hotkeysFocused()) {
        auto binds = Mod::get()->getSettingValue<std::vector<Keybind>>(kKeySettings[(size_t)k]);
        if (!binds.empty()) {
            int vk = (int)binds.front().key;
            if (vk > 0 && vk < 256) {
                const bool physical = (GetAsyncKeyState(vk) & 0x8000) != 0;
                if (!physical) {
                    g_keyDown[(size_t)k] = false;
                    return false;
                }
            }
        }
    }
#endif
    return true;
}

// Hotkey states maintained across frames and cleared between sessions or when showing a solution.
inline bool s_rightDown = false, s_leftDown = false;
inline int s_rightRep = 0, s_leftRep = 0;
inline std::chrono::steady_clock::time_point s_rSince, s_rLast, s_lSince, s_lLast;
inline bool s_upDown = false, s_downDown = false;
inline bool s_f1Down = false, s_f2Down = false, s_f3Down = false, s_f4Down = false;
inline std::chrono::steady_clock::time_point s_f3Since, s_f3Last, s_f4Since, s_f4Last;
inline bool s_f5Down = false, s_f6Down = false, s_f7Down = false, s_f9Down = false, s_f10Down = false;
inline bool s_panelDown = false, s_tkDown = false;

inline void resetTransportHotkeys() {
    s_rightDown = s_leftDown = false;
    s_rightRep = s_leftRep = 0;
    s_rSince = s_rLast = s_lSince = s_lLast = {};
    s_upDown = s_downDown = false;
    s_f1Down = s_f2Down = s_f3Down = s_f4Down = false;
    s_f3Since = s_f3Last = s_f4Since = s_f4Last = {};
    s_f5Down = s_f6Down = s_f7Down = s_f9Down = s_f10Down = false;
    s_panelDown = s_tkDown = false;
    for (size_t i = 0; i < (size_t)Key::Count; ++i) {
        g_keyDown[i] = false;
    }
}

// Whether the keyboard belongs to this game right now: the foreground window is one of our process's.
//
// The keybind events are the only way keys reach the hotkeys, and they come from the window's own key
// callback, so a window that is not in the foreground should never produce one. This does not rely on
// that. A worker is a game that is not there to be typed into -- it takes the foreground when it starts
// and the person goes back to their own window -- and a key it acts on from there is a key meant for
// another application: F9 ends the session, F5 blanks the screen, F2 freezes time. So a press is taken
// only while the foreground window is ours (the check the GetAsyncKeyState version had, which this
// path dropped on the grounds that "nothing is delivered while another application has the keyboard";
// that is what is being asserted here instead of assumed), and the held state is released the moment
// the window loses it, so a key that was down when the person switched away cannot go on repeating.
// Android has no such thing as another window.
inline bool hotkeysFocused() {
#ifdef GEODE_IS_WINDOWS
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
#else
    return true;
#endif
}

// Presses the gate refused, reported from the poll at most once a second (the listener runs inside the
// key callback, and a held key repeats: a line per press would be a flood). Main thread only.
inline int g_keysRefused = 0;
inline int g_keysRefusedTotal = 0;

// Run at the start of every poll. Releases everything that is held while the window is not the one
// with the keyboard, and says so once per change of state -- which is also what shows, in the log,
// whether a key ever reaches the hotkeys from a window that is not in the foreground.
inline void syncHotkeyFocus() {
    static bool s_had = true;
    const bool now = hotkeysFocused();
    int held = 0;
    if (!now) {
        for (size_t i = 0; i < (size_t)Key::Count; ++i) {
            if (g_keyDown[i]) ++held;
            g_keyDown[i] = false;
        }
    }
    if (now != s_had) {
        if (now) log::info("hotkeys: the game window has the foreground again");
        else log::info("hotkeys: the game window lost the foreground; {} held key(s) released", held);
        s_had = now;
    }
    static std::chrono::steady_clock::time_point s_reported;
    const auto t = std::chrono::steady_clock::now();
    if (g_keysRefused > 0 && t - s_reported >= std::chrono::seconds(1)) {
        log::info("hotkeys: {} key press(es) refused, another window has the foreground (total {})",
                  g_keysRefused, g_keysRefusedTotal);
        g_keysRefused = 0;
        s_reported = t;
    }
}

// What a key is bound to right now, for the on-screen legend. The legend used to spell the keys
// out, which was true while they were fixed and would quietly become a lie the moment one was
// rebound -- a legend that names the wrong key is worse than none.
inline std::string keyName(Key k) {
    auto binds = Mod::get()->getSettingValue<std::vector<Keybind>>(kKeySettings[(size_t)k]);
    return binds.empty() ? "(unbound)" : binds.front().toString();
}

// Installed on the first poll rather than at load: the polls are the only readers, so there is no
// window in which a listener could be missing, and this needs no ordering against anything else.
inline void ensureHotkeyListeners() {
    static bool s_installed = false;
    if (s_installed) return;
    s_installed = true;
    for (size_t i = 0; i < (size_t)Key::Count; ++i) {
        listenForKeybindSettingPresses(kKeySettings[i],
            [i](Keybind const&, bool down, bool, double) {
                // A release is always taken (a key must not stay down); a press only while the keyboard is ours.
                if (down && !hotkeysFocused()) {
                    ++g_keysRefused;
                    ++g_keysRefusedTotal;
                    return;
                }
                g_keyDown[i] = down;
            });
    }
}

// The keys that also work outside a session (plain manual play). pollHotkeys is only called
// inside the g_started guard, hence a separate function.
//
// [2026-08-23] The cheat keys were removed: F1 warp, F5 noclip, F6 force-ship, F10 infinite
// jump, and the F11 slow-motion cycle. They let a human do what the game does not allow, which
// is not what this project is for. Slow motion survives as cfg `slowmo=<N>`, which is how the
// replay-watching tools use it; it is no longer reachable from the keyboard mid-play.
// What is left only stops time (F2/F3/F4) or changes what is drawn (F1/F8 and the arrows).
inline void pollProbeHotkeys() {
    ensureHotkeyListeners();
    syncHotkeyFocus();
    // F1: show / hide the overlays. Two things it must not do (spec §9): it does not hide them
    // while solving, and it never hides the bot badge -- a replay has to be visibly a replay, so
    // that part is not the user's to turn off. Everything else (coordinates, progress, the key
    // legend) is. (Not F12: that is Steam's screenshot key by default.)
    //
    // SHOWING IS NEVER REFUSED. The flag outlives the session, and the key used to be ignored
    // outright while solving -- so overlays hidden in a replay stayed hidden into the next Solve,
    // where the search runs nearly all the time and the key could not bring them back (user report,
    // 2026-10-13). A solve now starts with them shown (uiConfigureSession), and the key can always
    // turn them on.
    bool f1 = keyHeld(Key::Overlay);
    if (f1 && !s_f1Down) {
        if (solvingNow() && !g_overlayHidden) {
            log::info("hotkey: F1 ignored (solving)");
        } else {
            g_overlayHidden = !g_overlayHidden;
            log::info("hotkey: F1 -> overlay {}", g_overlayHidden ? "hidden" : "shown");
        }
    }
    s_f1Down = f1;
    // F2: pause/resume (for manual experiments; also works outside a session)
    bool f2 = keyHeld(Key::Pause);
    if (f2 && !s_f2Down) {
        if (replaypanel::owns()) replaypanel::keyPause();     // the screen is dark and the panel is playing: the panel's
        else g_probeRequest = 2;
    }
    s_f2Down = f2;
    // F3: advance 1 substep / F4: advance 10 substeps (only meaningful while paused).
    // Held down they repeat: stepping 200 substeps by hand is 200 presses otherwise. The delay
    // before the repeat starts is what keeps a single press single.
    auto stepKey = [](Key k, int req, bool& down,
                      std::chrono::steady_clock::time_point& since,
                      std::chrono::steady_clock::time_point& last) {
        // F3 / F4 step the panel one tick / ten while it owns the keys (the screen dark, the panel playing), else the game
        auto deliver = [req] {
            if (replaypanel::owns()) replaypanel::keyStep(req == 3 ? 1 : 10);
            else g_probeRequest = req;
        };
        constexpr auto kRepeatAfter = std::chrono::milliseconds(500);
        constexpr auto kRepeatEvery = std::chrono::milliseconds(100);
        const bool now = keyHeld(k);
        const auto t = std::chrono::steady_clock::now();
        if (now && !down) {            // the press itself
            deliver();
            since = t;
            last = t;
        } else if (now && t - since >= kRepeatAfter && t - last >= kRepeatEvery) {
            deliver();                 // ...and every 100ms once it has been held for 500ms
            last = t;
        }
        down = now;
    };
    stepKey(Key::Step1, 3, s_f3Down, s_f3Since, s_f3Last);
    stepKey(Key::Step10, 4, s_f4Down, s_f4Since, s_f4Last);
    // F6: GD's own hitbox drawing (see toggleHitboxes). The game only shows it in practice
    // mode, so the flag and the node's visibility are driven here instead
    bool f6 = keyHeld(Key::Hitboxes);
    if (f6 && !s_f6Down) g_probeRequest = 6;
    s_f6Down = f6;
    // F10: the iteration map (itermap.hpp). OFF by default -- it draws over the level, and the
    // point of a replay is the replay. It reads a file and a draw node and touches nothing the
    // physics can see, so unlike F1 it is not refused while solving; there is simply nothing to
    // look at until the run has produced some rounds.
    //
    // The level is taken from the session when there is one and from the game otherwise, so the
    // key also works while watching a level in plain manual play.
    // cfg `f10auto` (a test hook): the press the check cannot send. Counted from the first poll of the showing -- a replay's
    // start, or the solve's clear -- and held for one poll, so it goes through the same edge test as the key.
    static bool s_f10Synth = false, s_f10Armed = false;
    static size_t s_f10Next = 0;
    static std::chrono::steady_clock::time_point s_f10T0;
    {
        const bool showing = g_started && (!solveSession() || g_dpShowSolution);
        const auto now = std::chrono::steady_clock::now();
        if (!showing) s_f10Armed = false;
        else if (!s_f10Armed) { s_f10Armed = true; s_f10Next = 0; s_f10T0 = now; }
        const bool was = s_f10Synth;
        s_f10Synth = false;
        if (s_f10Armed && !was && s_f10Next < itermap::g_f10Auto.size()
            && std::chrono::duration<double>(now - s_f10T0).count() >= itermap::g_f10Auto[s_f10Next]) {
            s_f10Synth = true;
            ++s_f10Next;
        }
    }
    bool f10 = keyHeld(Key::Itermap) || s_f10Synth;
    if (f10 && !s_f10Down) {
        int lv = g_cfg.levelId;
        if (lv <= 0)
            if (auto* pl = PlayLayer::get())
                if (pl->m_level) lv = pl->m_level->m_levelID.value();
        itermap::toggle(lv);
        log::info("hotkey: F10 -> iteration map {}", itermap::mapWanted() ? "on" : "off");
    }
    s_f10Down = f10;
    // The replay panel (replay_panel.hpp): GD's record of the deepest verified attempt, in the free corner. Like the
    // iteration map it only reads what the solver has kept, so it is not refused while solving.
    const bool panelKey = keyHeld(Key::ReplayPanel);
    if (panelKey && !s_panelDown) {
        replaypanel::toggle();     // full -> recent -> off -> full (it says which in result.txt)
        log::info("hotkey: replay panel key");
    }
    s_panelDown = panelKey;
}

// Whether anything is being drawn right now. Declared here because the hotkey needs it and
// renderSuppressed() lives further down the include chain (same condition, one definition).
inline bool renderingOn() {
    if (g_renderOff) return false;
    return !(g_started && !g_sessionOver && g_cfg.skipRender && !g_realtimeOverride);
}

// A section solve keeps the screen off while it runs, and the visibility pass with it. Its search
// moves the players back and forth through a world it does not put back -- psnap restores the
// players, not the objects the level moves -- and in a solve started from the panel the
// visibility pass kept walking that world, since the panel's session does not skip it the way a
// worker's (skiprender=1) does. The screen broke, the replay after the rung drew nothing but the
// player, and GD's own retry then crashed in resetLevelVariables (user report, 2026-09-25). So
// the handoff turns rendering and the visibility pass off (repair.hpp; the pass is the
// updateVisibility hook), which is how every worker's search has always run, and the rung's end
// lifts the hold after the reset that starts the spliced plan's replay (hooks_gamelayer). A
// screen that was on comes back the way the render key brings it back: another reset at the next
// frame boundary, then rendering (g_visResetPending). The render key is ignored meanwhile.
// A worker's screen was never on (skiprender, not g_renderOff, keeps it dark), so there the hold
// changes nothing: g_renderOff goes back as it was and no reset is asked for.
//
// THE SPEED GOES WITH THE SCREEN. The render key's ON is also "stop skipping frames"
// (g_realtimeOverride, renderTogglePress), and its OFF hands the frames back to the fast loop. The
// hold used to turn only the drawing off, so a solve whose screen had been on went into the section
// solve in real time with nothing to see: the replay that takes the run to the section head -- the
// loop's own verified prefix, thousands of ticks -- ran at the spectating notch (1x by default)
// instead of the fast loop, and the window sat dark for as long as the song. The hold now does what
// the key's OFF does, and the lift gives back what the hold found: a screen that was on comes back
// through the render key's own path (the reset, then real time), and a speed that was real time
// with the screen off comes back as it was.
// -1 = no hold in force.
inline int g_secRenderWas = -1;          // g_renderOff when the hold began (0/1)
inline bool g_secScreenWasOn = false;    // ...and whether anything was being drawn then
inline int g_secRealtimeWas = -1;        // ...and g_realtimeOverride (0/1; -1 = not touched)
inline void secRenderHold() {
    if (g_secRenderWas >= 0) return;
    g_secScreenWasOn = renderingOn();
    g_secRenderWas = g_renderOff ? 1 : 0;
    g_renderOff = true;
    g_secRealtimeWas = -1;
    if (g_cfg.fastdt > 0) {
        g_secRealtimeWas = g_realtimeOverride ? 1 : 0;
        g_realtimeOverride = false;
        // A render-resume asked for just before the handoff would put real time back at the next
        // frame boundary (hooks_gamelayer); the handoff's own reset starts the replay, so it goes.
        g_visResetPending = false;
    }
    log::info("secsolve: screen and visibility off until the section solve ends (screen was {}, "
              "speed was {})", g_secScreenWasOn ? "on" : "off",
              g_secRealtimeWas == 1 ? "real time, now the fast loop" : "the fast loop's");
}
inline void secRenderRelease() {
    if (g_secRenderWas < 0) return;
    g_renderOff = g_secRenderWas != 0;
    g_secRenderWas = -1;
    if (g_secScreenWasOn) g_visResetPending = true;       // ...and real time with it, after the reset
    else if (g_secRealtimeWas == 1) g_realtimeOverride = true;
    g_secRealtimeWas = -1;
    log::info("secsolve: hold lifted after the reset (screen {})",
              g_secScreenWasOn ? "back after one more reset" : "stays as it was");
}

// Exactly what happens when the render key is pressed once: rendering on <-> off, and nothing
// else. It used to be one key that cycled rendering AND the speed together, so "fast but
// visible" and "slow but blind" were both unreachable; the speed is on the arrow keys now.
// Every equivalent transition must go through this function — writing a separate path leads
// to the harness and the real key measuring different things (keep a single entry point).
// [2026-08-23] The key moved from F8 to F5 (user direction). cfg `watchat` / `watchcycle` still
// call this function, so they follow the key wherever it goes.
inline void renderTogglePress() {
        const bool hasFast = g_cfg.fastdt > 0;
        if (g_secRenderWas >= 0) {
            // A section solve holds the screen off (secRenderHold); the key waits for its end.
            log::info("hotkey: F5 ignored - a section solve is running, the screen comes back "
                      "when it ends");
            return;
        }
        if (renderingOn()) {
            // Screen off. With a fast loop configured, hand the frames back to it as well --
            // stopping the drawing is the whole reason to do that.
            g_renderOff = true;
            if (hasFast) g_realtimeOverride = false;
        } else {
            g_renderOff = false;
            if (hasFast && !g_realtimeOverride && !g_visResetPending) {
                // Do not resume rendering here. Resume after resetLevel (the update hook).
                // In the reverse order the first updateVisibility after resuming steps on the
                // stale visibility state and crashes
                g_visResetPending = true;
                g_visRefresh = true;        // old method (only effective with visrefresh=1)
                if (g_watchPurge) purgeDanglingActions();
            }
        }
        log::info("hotkey: F5 -> render {}", renderingOn() ? "ON" : "OFF");
        // Record what was alive at the moment of the switch. The CTD shows up a few frames after
        // this, so after the fact "what was on screen right before the crash" can never be known
        writeResult(fxCensus("render-toggle"));
}

inline void pollHotkeys() {
    ensureHotkeyListeners();
    syncHotkeyFocus();
    // F5: rendering on / off. Only in a solve session -- that is the only time there is anything
    // to gain by not drawing (the frames go back to the fast loop). In a plain replay it stays
    // dead: a replay that cannot be seen is not a replay, and turning the screen off by accident
    // mid-replay looks exactly like a crash.
    //
    // The whole SESSION, not only while the search runs: a solve alternates searching with
    // replaying its candidates, and it comes back to 1x for each replay. If the key only worked
    // while searching, the run could be slowed down but never sped up again.
    bool f5 = keyHeld(Key::Render);
    if (f5 && !s_f5Down && solveSession()) renderTogglePress();
    s_f5Down = f5;
    // F9: leave the level. The session is closed first if it is still open, and then GD's own
    // exit path runs, so this lands on the level screen exactly as pressing GD's own quit
    // button would. It is deliberately NOT a "stop the analysis but stay in the level" key any
    // more: staying behind with a finished session is the one state in which the mod is no
    // longer driving while the player is still mid-level, and the level's record would start
    // counting again from wherever the plan had got to.
    // The human takeover's end (mod/takeover.hpp). Only in a session that armed it; elsewhere
    // the key is nobody's.
    const bool tk = keyHeld(Key::TakeoverEnd);
    // In a session armed by the key alone (cfg takeoverkey) the first press is the takeover itself.
    if (tk && !s_tkDown && takeover::armed()) takeover::keyPressed();
    s_tkDown = tk;
    // A takeover session leaves the level alone except through the person's own play: F7 and the
    // seek keys would restart or run the level under them, and the speed keys are on the arrows,
    // where up is also GD's jump. The edge tracking below still runs. (A session armed by the key
    // alone is an ordinary replay until the key is pressed, and keeps them.)
    const bool takeoverOwnsLevel = takeover::ownsLevel();
    bool f9 = keyHeld(Key::Quit);
    if (f9 && !s_f9Down) {
        log::info("hotkey: F9 -> quit to the level screen");
        if (!g_sessionOver) {
            endSession("user_quit_hotkey");   // ends with GD's onQuit (see endSession)
        } else {
            Loader::get()->queueInMainThread([] {
                if (auto* pl = PlayLayer::get()) pl->onQuit();
            });
        }
    }
    s_f9Down = f9;
    // F7: replay the current plan from the start. resetLevel() puts g_tick and g_nextInput back
    // to 0 and the plan streams from the beginning. Replay is launched with attempts=1, so each
    // press adds one more slot.
    //
    // It also works AFTER the run has finished, which is the case that matters: clearing the
    // level ends the session (`level_complete`), and without this the one key you want at the
    // moment the replay finishes was the one key that had stopped working. Reviving is for
    // replays only -- in serve mode the session belongs to the driver, and resurrecting it
    // behind the driver's back would hand it a run it never asked for.
    bool f7 = keyHeld(Key::Replay);
    // In a session armed by the key alone (cfg takeoverkey) a person who has the input can give it
    // up with this key: the mode is left and the replay restarts below, whole, ready for the
    // takeover key. Elsewhere, and at a fixed point, a takeover session ignores it.
    if (f7 && !s_f7Down && takeover::resetByReplayKey())
        log::info("hotkey: F7 -> takeover left, replay from the start");
    if (f7 && !s_f7Down && !takeover::ownsLevel()) {
        if (auto* pl = PlayLayer::get()) {
            if (g_sessionOver && !g_serveMode) {
                g_sessionOver = false;
                g_finishedAttempts = 0;
                writeResult("session_resume: F7 restarted the replay after the session ended");
            }
            ++g_cfg.maxAttempts;
            g_pauseAtXFired = false;   // stop short of the wall again on the next lap
            g_paused = false;          // release it so it does not start out stuck paused
            clearManualPause();        // ...and F2's, so the replay does not start frozen
            itermap::endSeek("replay_key");
            resetTransportHotkeys();
            // GD leaves its end-of-level screen parented across resetLevel, so replaying a level
            // you have cleared stacks another one on every clear (see dismissEndScreen).
            itermap::dismissEndScreen();
            log::info("hotkey: F7 -> replay from the start");
            pl->resetLevel();
        }
    }
    s_f7Down = f7;
    // [2026-08-28, user direction] The arrows were remapped once the seek bar existed. They are
    // now the transport a person actually reaches for while watching a replay:
    //
    //   left / right   seek back / forward. A tap is five seconds; holding accelerates.
    //   up / down      the spectating speed, one notch (0.25 ... 16x).
    //
    // Up and down used to be deliberately unbound on the grounds that up is a jump in GD and a
    // spectator key that also jumps is a trap. That reasoning does not reach here: this whole
    // function only runs while the mod is DRIVING, where the plan owns the input and a keypress
    // cannot jump. (pollProbeHotkeys, the one that also works in plain manual play, binds
    // neither.)
    const bool kUp   = keyHeld(Key::Faster);
    const bool kDown = keyHeld(Key::Slower);
    int step = 0;
    if (kUp && !s_upDown) step = +1;
    if (kDown && !s_downDown) step = -1;
    s_upDown = kUp; s_downDown = kDown;
    if (step != 0 && !takeoverOwnsLevel) {
        if (replaypanel::owns()) {
            replaypanel::keySpeed(step);   // the screen is dark and the panel is playing: its pace, not the game's
        } else {
            watchSpeedStep(step);
            log::info("hotkey: {} -> {:g}x", step > 0 ? "faster" : "slower", g_watchSpeed);
        }
    }
    // Seeking -- or STEPPING, while the game is stopped.
    //
    // A seek needs the game to run: it fast-forwards to the target and stops when it arrives.
    // While F2 has time frozen nothing arrives, so a forward seek issued there hung the level
    // behind its own blackout forever. Stopped, the arrows are frame-step keys instead:
    //
    //   right   exactly F4 -- ten substeps, same request, same timing
    //   left    ten substeps BACKWARDS, which physics cannot do, so it is a seek to
    //           tick-10 that puts the stop back when it lands (g_seekRepause)
    //
    // The step is refused while such a seek is still running, or a held key would keep moving
    // the target the in-flight one is chasing.
    auto transportKey = [](Key k, int dir, bool& down, int& repeats,
                           std::chrono::steady_clock::time_point& since,
                           std::chrono::steady_clock::time_point& last) {
        const bool stopped = g_paused || manualPaused();
        // Stopped: F3/F4's timing exactly, because that is the key this becomes.
        const auto repeatAfter = std::chrono::milliseconds(stopped ? 500 : 400);
        const auto repeatEvery = std::chrono::milliseconds(stopped ? 100 : 140);
        const bool now = keyHeld(k);
        const auto t = std::chrono::steady_clock::now();
        const bool press = now && !down;
        const bool repeat = now && !press && t - since >= repeatAfter
                            && t - last >= repeatEvery;
        down = now;
        if (!press && !repeat) return;
        // NOT WHILE THE LOOP OWNS THE LEVEL. Both halves of this key move it -- a seek restarts
        // it, a step advances it -- and during a solve the level IS the loop's verification replay,
        // held still between rounds by repair.hpp's spawn. Nudging it there corrupts the round the
        // recorder is about to be asked about. The bar is not drawn for the same reason and a press
        // on it is refused by barLive(); the keys never had the equivalent guard, and now that the
        // map is watchable live they are keys a person has a reason to be pressing.
        // The edge tracking above still runs, so a key held across the end of a solve is not
        // mistaken for a fresh press afterwards.
        // ...except the panel's: with the screen dark it is what the arrows move, and moving it touches no round.
        if (replaypanel::owns()) {
            if (press) { repeats = 0; since = t; }
            last = t;
            replaypanel::keySeek(dir * itermap::seekStep(press ? 0 : ++repeats));
            return;
        }
        if (showingSolve() || takeover::ownsLevel()) {
            repeats = 0;
            since = t;
            last = t;
            return;
        }
        if (press) { repeats = 0; since = t; }
        last = t;
        if (stopped) {
            // ONE STEP AT A TIME, and a step is not over until the tick it owes has been walked.
            // A backward step aims short of its target and finishes with probe::g_step; a repeat
            // arriving in that gap sees a tick the run is not going to stay at, computes its own
            // target from it, and overwrites the finishing step -- two half-moves compounding
            // into a jump in the wrong direction.
            if (itermap::seeking() || manualStepPending()) return;
            if (dir > 0) g_probeRequest = 4;   // ...which IS F4
            else         itermap::stepBack(10);
            return;
        }
        // A BACKWARD repeat waits for the restart it already asked for. Rewinding runs the level
        // again from the top, and re-asking before that has even begun throws the work away and
        // starts over -- so a held key made no progress at all.
        if (dir < 0 && itermap::g_seekRestart) return;
        itermap::seekBy(dir * itermap::seekStep(press ? 0 : ++repeats));
    };
    transportKey(Key::SeekForward, +1, s_rightDown, s_rightRep, s_rSince, s_rLast);
    transportKey(Key::SeekBack,  -1, s_leftDown,  s_leftRep,  s_lSince, s_lLast);
}

// Body of the experimental-hotkey handling (called from update).
// Must be called before the pause check — if we return while paused the keys are never
// processed and the pause can never be released
inline void handleProbeRequest(GJBaseGameLayer* layer);

// Rollback-verification state

}  // namespace p1
