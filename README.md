# OVRK Workout Reminder

An always-on-top overlay that **automatically detects the end of an OVERKILL
match** (the Roblox first-to-5 game) and pops up a random workout to do before
your next game.

It's a single native Windows `.exe` — no install, no runtime, no dependencies.
Just double-click it. No calibration or setup required.

---

## Run it

Double-click **`dist\ovrk-workout.exe`**.

A small rounded status HUD appears in the top-right corner, always on top of
everything (including the game). It shows **● OVRK WORKOUT**, a live status line,
and a **confidence bar**.

The overlay is **click-through** — your mouse passes straight through it to the
game, so it can never be moved or clicked by accident during a match. You control
it entirely with the hotkeys below, or by right-clicking the tray icon near the
clock.

### Hotkeys

| Hotkey | Action |
|--------|--------|
| **Ctrl + Alt + W** | Show a workout reminder right now (test / manual trigger) |
| **Ctrl + Alt + H** | Hide / show the overlay |
| **Ctrl + Alt + P** | Pause / resume watching |
| **Ctrl + Alt + M** | Move the overlay (unlock to drag; press again or Esc to lock) |
| **Ctrl + Alt + X** | Close the app entirely |

**Moving the overlay:** press **Ctrl + Alt + M**. The border turns amber and it
says *"Drag me"* — now drag it anywhere with the mouse. Press **Ctrl + Alt + M**
again (or **Esc**) to lock it back to click-through. Its new spot is remembered
for next time.

The tray icon (right-click) has the same actions plus sensitivity and the workout
list. Double-click the tray icon to un-hide the overlay.

---

## How the detection works

There's nothing to set up. The app watches the whole screen and recognises
OVERKILL's match-end screen by its signature: the **red "Leave" button next to
the purple "Rematch" button** at the bottom-centre.

To avoid firing during a match, it's deliberately strict. It only counts as the
end screen when it finds *both* buttons as two solid, similarly-sized, adjacent
coloured blocks, centred along the very bottom of the screen (this ignores your
red character and stray red/purple effects). It also has to see them for about a
**second** before firing (tolerating the odd dropped frame), so a one-frame flash
mid-game can't set it off — only the real results screen lasts long enough.

**If it ever misses a match end:** there's a log at
`%LOCALAPPDATA%\OvrkWorkout\detect_log.txt` that records every moment the screen
looked end-screen-ish (with a confidence value). If a reminder doesn't show when
it should, send me that file — it tells us whether detection didn't catch the
screen or you just left it too fast.

**It only runs while Roblox is open.** The reminder can only appear when the
Roblox game client (`RobloxPlayerBeta.exe`) is actually running — if Roblox is
closed the overlay shows *"Waiting for Roblox…"* and nothing fires. (The manual
**Ctrl+Alt+W** test still works anytime.)

- The **confidence bar** on the overlay shows how sure it is right now (it stays
  near empty during play and jumps toward full on the results screen).
- One reminder per match: it won't fire again until the results screen goes away
  and a short cooldown passes.
- If it ever fires too easily or misses, use **More sensitive / Less sensitive**
  in the tray icon menu.

If OVERKILL ever changes the look of that screen, the sensitivity options are the
first thing to try.

---

## Workouts

Each reminder shows one random exercise from your list. Right-click the tray icon
→ **Edit workout list** to change it in Notepad (one exercise per line). Stored at:

```
%LOCALAPPDATA%\OvrkWorkout\workouts.txt
```

---

## Notes & limits

- **Play in windowed or borderless-fullscreen mode** (the Roblox default). In
  borderless the overlay shows on top and the screen-reading works. True
  exclusive fullscreen can hide overlays and return black captures — if
  detection seems dead, switch out of exclusive fullscreen.
- Detection is screen-based (Roblox exposes no match-end API). It keys off the
  Leave/Rematch buttons, so it works whether you win or lose.
- Settings live in `%LOCALAPPDATA%\OvrkWorkout\`. Delete that folder to reset.

---

## Building from source

Requires Visual Studio with the C++ workload (for `cl.exe`). Then:

```
build.bat
```

That produces `dist\ovrk-workout.exe`. If your Visual Studio is installed
elsewhere, edit the `VCVARS` path at the top of `build.bat`.

Source is a single file: `src\main.cpp` (pure Win32 + GDI). The detector lives in
`detectEndScreen()` — tweak the red/purple colour thresholds there if the game's
button colours change.
"# OVERKILL-WORKOUIT" 
