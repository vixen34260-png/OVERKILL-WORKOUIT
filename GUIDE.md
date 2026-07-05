# 🏋️ OVRK Workout Reminder — User Guide

A tiny desktop app that reminds you to do a quick workout **after every match** of
**OVERKILL** (the Roblox game). When a match ends, it pops up a random exercise —
push-ups, a plank, whatever you've set — and tracks how much you've done toward a
daily goal. Get fit between rounds. 💪

There's nothing to set up and nothing to calibrate — just run it and play.

---

## ✅ What you need

- **Windows 10 or 11**
- **Roblox**, playing **OVERKILL**
- Roblox running in **Fullscreen** or **Windowed Fullscreen (borderless)** — *not*
  a tiny floating window. (The app watches the screen for the match-end buttons,
  so they need to be where it expects: along the bottom.)

---

## ▶️ Getting started

1. **Download** `ovrk-workout.exe`.
2. **Double-click it.** There's no installer — it just runs. A small **overlay**
   appears in the top-right corner, and an icon appears in your system tray
   (bottom-right, near the clock).
3. **Play OVERKILL.** That's it. When a match ends, the reminder pops up.

> The overlay is **click-through** — your mouse goes straight through it to the
> game, so it can never get in your way or be clicked by accident during a match.

---

## ⚠️ "Windows protected your PC" / antivirus warning

When you first run it, Windows may show a blue **"Windows protected your PC"** box,
or your antivirus might flag it. **This is a false alarm, not a virus.**

It happens because the app isn't code-signed (that costs money) and it *reads the
screen* to detect when your match ends — a normal, necessary thing here, but
something security software is suspicious of by default.

**To run it anyway:**
- On the blue SmartScreen box: click **More info** → **Run anyway**.
- If your antivirus quarantines it: open **Windows Security → Virus & threat
  protection → Protection history**, find the file, and choose **Allow / Restore**.
  You can also add the app's folder as an **exclusion**.

The full source code is public in this repo — anyone can read exactly what it does.
It has **no internet access at all** (it can't send your data anywhere) and only
saves a couple of small settings files on your own PC.

---

## 🎮 How it works

- The app watches for OVERKILL's **match-end screen** — the red **Leave** button
  next to the purple **Rematch** button. When it sees them, it pops your reminder.
- It **only runs while Roblox is open.** If Roblox is closed, the overlay says
  *"Waiting for Roblox…"* and nothing will pop up.
- Do the workout, then hit **DONE** — the reps get added to your daily goal and
  your all-time total.

That's the whole loop: **match ends → reminder → do it → back to the game.**

---

## 🖥️ The overlay

The little bar shows, at a glance:

- **● OVRK WORKOUT** and a status line:
  - *Watching for match end* — playing, waiting for a match to finish
  - *Match ended ✓ — reminded* — it just reminded you
  - *Waiting for Roblox…* — Roblox isn't running
  - *Paused* — watching is turned off
- A **daily goal bar** — e.g. `45 / 100` — that fills as you complete workouts and
  resets every day.

Move it, hide it, or expand it to show your shortcuts (see below).

---

## ⌨️ Shortcuts

These work anywhere, even while you're in the game:

| Shortcut | What it does |
|----------|--------------|
| **Ctrl + Alt + W** | Pop a workout reminder right now (test it) |
| **Ctrl + Alt + S** | Open / close the **settings** menu |
| **Ctrl + Alt + H** | Hide / show the overlay |
| **Ctrl + Alt + M** | Move the overlay (drag it, then press again to lock) |
| **Ctrl + Alt + P** | Pause / resume watching |
| **Ctrl + Alt + X** | Close the app |

You can also **right-click the tray icon** for all of these plus a few extras.

**Moving the overlay:** press **Ctrl + Alt + M** — the border turns amber and it
says *"Drag me."* Drag it wherever you like with the mouse, then press
**Ctrl + Alt + M** again (or **Esc**) to lock it. It remembers the spot.

---

## ⚙️ Settings menu (Ctrl + Alt + S)

Everything here **saves automatically** the moment you change it:

- **Workouts** — a checkbox to turn each exercise on or off. For the ones that are
  on, use the **−** / **+** buttons to set the amount: reps change by **1**, and
  timed moves like the plank change by **5 seconds**.
- **Daily goal** — your rep target for the day (**−** / **+** to change it). The
  overlay bar fills toward this, and it resets each morning.
- **All-time reps** — your lifetime total. It counts up automatically every time
  you finish a workout and never resets. Watch it climb. 📈
- **Add offline reps** — did push-ups at the gym or away from your PC? Hit
  **+5 / +10 / +25** to log them. They count toward today's goal and your
  all-time total.
- **Show shortcuts on overlay** — flip this on and the overlay lists every hotkey,
  so you don't have to memorize them.

Want to add or remove exercises entirely? Right-click the tray icon →
**Edit workout list** to open the list in Notepad (one exercise per line).

---

## 🔔 The reminder popup

When a match ends you'll get a clean pop-up: **TIME TO MOVE!**, the exercise to do
(e.g. *20 Push-ups*), a little progress bar showing your day, and a green button.

- **DONE** (or press **Enter**) — you did it; counts toward your totals.
- **Esc** — skip this one; doesn't count.

It waits a moment before it can be dismissed, so a stray keypress in-game won't
close it before you see it.

---

## 🩹 Troubleshooting

**No reminder when a match ends?**
- Make sure **Roblox is actually running** (overlay shouldn't say "Waiting for
  Roblox…").
- Play in **Fullscreen or Borderless**, not a small window.
- Nudge detection: right-click the tray icon → **More sensitive**.

**It popped up in the middle of a match?**
- Right-click the tray icon → **Less sensitive**.

**Overlay is in the way?**
- Move it with **Ctrl + Alt + M**, or hide it with **Ctrl + Alt + H**.

**Want to start totally fresh?**
- Close the app, then delete the folder `%LOCALAPPDATA%\OvrkWorkout`. It rebuilds
  with default settings next time you launch.

Your settings, workout list, and progress live in that same folder
(`config.bin`, `workouts.txt`) — safe to back up or copy to another PC.

---

## 🛠️ For developers

The app is a single native Win32 C++ file — no frameworks, no dependencies. To
build it yourself you need Visual Studio with the "Desktop development with C++"
workload, then run **`build.bat`**. The source is `src/main.cpp`; see the main
[README](README.md) for build details.

---

*Made for grinding OVERKILL without skipping leg day.*
