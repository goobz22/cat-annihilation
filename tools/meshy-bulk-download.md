# Meshy bulk download (DevTools console script) + Radix UI automation

> Moved verbatim from `App Development/.claude/rules/cat-annihilation.md` on 2026-07-02
> (token-diet: the ~8.6KB script was 55% of the rules file and only needed during rare
> bulk asset downloads). Run from the Chrome DevTools console per the instructions below.

**Context:** the Meshy.ai workspace sidebar has one `...` menu per generation tile with a Download option that opens a settings dialog. Clicking through 50+ tiles by hand is tedious; the script below automates it from the Chrome DevTools console. **Meshy is built on Radix UI**, so the same pattern works on any Radix-based app (Linear, Vercel dashboard, cal.com, shadcn/ui sites) with testid-targeted tweaks.

**Stable DOM anchors Meshy exposes:**
- `[data-testid="assets-list"]` — the sidebar grid container.
- `[data-testid="assets-card"]` — each generation tile.
- `button` containing `svg.tabler-icon-dots` — the "..." menu trigger inside a tile.
- `[data-testid="assets-context-menu"][data-state="open"]` — the dropdown menu while open.
- `[data-testid="assets-context-menu-download-btn"]` — the "Download" menu item (a `<div role="menuitem">`, NOT a `<button>`).
- `[role="dialog"][data-state="open"]` — the Download Settings dialog.
- Green final button — `<button>` inside the dialog with text "Download" (filter out `disabled`).

**Critical Radix gotchas (the hard-won part — these are why naive automation silently fails):**
1. **Radix popover triggers listen for `pointerdown`, not `click`.** A bare `.click()` does nothing — Radix ignores synthetic clicks with no preceding pointerdown. Fix: dispatch `pointerdown` → `pointerup` → `click` in sequence with `pointerType:'mouse'`, `isPrimary:true`, `button:0`.
2. **Radix menu items can be `<div role="menuitem">`.** Searching for `<button>` misses them — target the stable `data-testid` instead.
3. **Radix portals popovers/dialogs to the document root**, not inside the tile — search the whole document for menu/dialog elements.
4. **Wait for `data-state="open"` before interacting.** Radix animates in; clicking mid-animation fails silently. Poll for the open state with a timeout — never hardcode sleeps as the gate.
5. **`navigator.clipboard.writeText` is blocked on insecure origins / some iframes** — provide a textarea fallback (relevant to the companion recorder, below).
6. **Chrome download prompt is a blocker.** Before running: `chrome://settings/downloads` → turn OFF "Ask where to save each file before downloading" so files land without per-file user input.
7. **Menu can open off-viewport near the right edge.** Radix auto-flips to the left but occasionally still clips. If menu actions don't register, zoom the browser to ~80% (`Ctrl+-`) before running.

**The working script** — paste into Chrome DevTools console on the Meshy workspace tab. Abort mid-run with `window.stopMeshyDownload = true`. Resume a partial run by setting `CONFIG.startIndex`.

```js
(async () => {
  const CONFIG = {
    afterHover:         300,
    afterDotsFire:      900,
    afterMenuSelect:   1100,
    afterFinalDownload:2800,
    betweenTiles:      1800,
    startIndex:           0, // resume here after a partial run
  };

  const sleep = ms => new Promise(r => setTimeout(r, ms));
  window.stopMeshyDownload = false; // set true in console to abort mid-run

  // Radix triggers need pointerdown, not just click.
  const firePointerClick = (el) => {
    const r = el.getBoundingClientRect();
    const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    const common = { bubbles: true, cancelable: true, clientX: cx, clientY: cy, pointerType: 'mouse', isPrimary: true };
    el.dispatchEvent(new PointerEvent('pointerdown', { ...common, button: 0, buttons: 1 }));
    el.dispatchEvent(new PointerEvent('pointerup',   { ...common, button: 0, buttons: 0 }));
    el.dispatchEvent(new MouseEvent('click',         { ...common, button: 0 }));
  };

  const fireHover = (el) => {
    const r = el.getBoundingClientRect();
    const common = { bubbles: true, cancelable: true, clientX: r.left + r.width/2, clientY: r.top + r.height/2, pointerType: 'mouse', isPrimary: true };
    ['pointerover','pointerenter','pointermove'].forEach(t => el.dispatchEvent(new PointerEvent(t, common)));
  };

  const waitFor = async (condFn, { step = 100, timeout = 4000 } = {}) => {
    const start = performance.now();
    while (performance.now() - start < timeout) {
      const res = condFn();
      if (res) return res;
      await sleep(step);
    }
    return null;
  };

  const grid = document.querySelector('[data-testid="assets-list"]');
  if (!grid) { alert('No [data-testid="assets-list"] grid found.'); return; }
  const tiles = Array.from(grid.querySelectorAll('[data-testid="assets-card"]'));
  if (tiles.length === 0) { alert('No tiles found.'); return; }

  alert(`Downloading ${tiles.length} models from tile #${CONFIG.startIndex}.\nAbort: window.stopMeshyDownload = true`);

  let success = 0, failed = 0;
  const failures = [];

  for (let i = CONFIG.startIndex; i < tiles.length; i++) {
    if (window.stopMeshyDownload) { console.warn('aborted at', i); break; }

    const tile = tiles[i];
    const pct = Math.round(((i + 1) / tiles.length) * 100);
    document.title = `[${pct}%] Meshy ${i + 1}/${tiles.length}`;

    try {
      tile.scrollIntoView({ block: 'center' });
      await sleep(250);
      fireHover(tile);
      await sleep(CONFIG.afterHover);

      const dotsBtn = tile.querySelector('button svg.tabler-icon-dots')?.closest('button');
      if (!dotsBtn) throw new Error('dots button not found');
      firePointerClick(dotsBtn);

      const menu = await waitFor(
        () => document.querySelector('[data-testid="assets-context-menu"][data-state="open"]'),
        { timeout: 3000 }
      );
      if (!menu) throw new Error('context menu never opened');

      const menuItem = menu.querySelector('[data-testid="assets-context-menu-download-btn"]');
      if (!menuItem) throw new Error('menu-item "Download" not in menu');
      firePointerClick(menuItem);
      await sleep(CONFIG.afterMenuSelect);

      const dialog = await waitFor(
        () => document.querySelector('[role="dialog"][data-state="open"], [data-state="open"][aria-modal="true"]'),
        { timeout: 3000 }
      );
      if (!dialog) throw new Error('Download Settings dialog never opened');

      const finalBtn = Array.from(dialog.querySelectorAll('button'))
        .filter(b => !b.disabled && b.offsetParent && b.textContent.trim().toLowerCase() === 'download')
        .pop();
      if (!finalBtn) throw new Error('final green Download button not found in dialog');
      firePointerClick(finalBtn);
      await sleep(CONFIG.afterFinalDownload);

      success++;
    } catch (err) {
      failed++;
      failures.push({ index: i, error: err.message });
      console.warn(`[${i}] ${err.message}`);
      document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
      await sleep(500);
      document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
      await sleep(500);
    }

    await sleep(CONFIG.betweenTiles);
  }

  document.title = `Meshy DONE — ${success}/${tiles.length}`;
  alert(`Done.\nSuccess: ${success}\nFailed: ${failed}\n${failures.length ? 'Failed indices: ' + failures.map(f => f.index).join(', ') : ''}`);
  return { success, failed, failures };
})();
```

**Companion click-recorder watchdog** — when a different site's selectors don't match, reuse this recording-based debug flow instead of guessing selectors:
1. Paste a watchdog that logs every click's element chain + CSS path.
2. User manually performs ONE complete download.
3. Export the JSON of captured clicks.
4. Build a targeted script from the real selectors.

Key parts of the watchdog: `document.addEventListener('click', handler, true)` with the **capture phase**; a `cssPath()` helper walking parents with `:nth-of-type(n)`; a floating green badge to stop recording; export copies JSON to clipboard with a **textarea fallback** (re: gotcha #5).

**When to reach for this:** user asks to bulk download/delete/share/anything in the Meshy workspace; user hits a similar block automating a Radix-based site; or a user's synthetic `.click()` isn't triggering a popover/dropdown/dialog → suggest the pointerdown fix.

**Downstream integration / rig pipeline:** the script drops files into Chrome's default download folder as `<meshy-task-id>.glb`. User then moves them into `cat-annihilation/assets/models/meshy_raw/` (dogs → `meshy_raw_dogs/`) and runs:

```
scripts/rig_batch.ps1 -InputDir assets/models/meshy_raw -OutputDir assets/models -Species cat
# or -Species dog
```

This replaces Meshy's weak auto-rig with Blender heat-diffusion weights via `scripts/rig_quadruped.py`.
