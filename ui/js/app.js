(() => {
  const state = {
    view: "connections",
    profiles: [],
    workspace: null,
    selected: { market: null, trader: null, zone: null, types: null, item: 0, typeItem: 0 },
    selectedItems: new Set(),
    selectedTypeItems: new Set(),
    lastClickedItem: 0,
    lastClickedType: 0,
    typesQuick: "all",
    variantMove: null,
    form: emptyProfile(),
    search: "",
    searchCaret: null,
    sortFiles: true,
    sortEntries: true,
    drawerIssues: [],
    types: {
      folder: "",
      files: [],
      types: [],
      error: "",
      filter: "",
      selected: new Set(),
      attachCounts: new Map(),
      hideUsed: true,
      lastClicked: 0,
      mode: "items"
    },
    pending: new Map(),
    reqId: 1,
    browser: {
      path: "",
      parent: "",
      entries: [],
      selected: null,
      picks: { market: "", traders: "", zones: "" },
      profile: null,
      password: "",
      jsonCount: 0,
      suggestions: {}
    }
  };

  function emptyProfile() {
    return {
      id: "",
      name: "",
      protocol: "sftp",
      host: "",
      port: 22,
      username: "",
      passive: true,
      marketPath: "/ExpansionMod/Market",
      tradersPath: "/ExpansionMod/Traders",
      zonesPath: "/mpmissions/dayzOffline.chernarusplus/expansion/traderzones",
      password: ""
    };
  }

  function nativeHost() {
    return window.chrome && window.chrome.webview;
  }

  function api(cmd, payload = {}) {
    return new Promise((resolve, reject) => {
      if (!nativeHost()) {
        reject(new Error("WebView2 host is not available"));
        return;
      }
      const id = state.reqId++;
      state.pending.set(id, { resolve, reject });
      window.chrome.webview.postMessage({ id, cmd, payload });
    });
  }

  if (nativeHost()) {
    window.chrome.webview.addEventListener("message", (ev) => {
      const msg = ev.data;
      if (msg && msg.event === "progress") {
        showProgress(msg.data.message || "Working…", msg.data.percent);
        return;
      }
      if (!msg || typeof msg.id !== "number") return;
      const waiter = state.pending.get(msg.id);
      if (!waiter) return;
      state.pending.delete(msg.id);
      if (msg.ok) waiter.resolve(msg.data || {});
      else waiter.reject(new Error(msg.error || "Request failed"));
    });
  }

  function $(id) { return document.getElementById(id); }

  function toast(text, bad = false) {
    const el = document.createElement("div");
    el.className = "toast" + (bad ? " bad" : "");
    el.textContent = text;
    $("toasts").appendChild(el);
    setTimeout(() => el.remove(), 4200);
  }

  function setStatus(text, live) {
    $("statusText").textContent = text;
    $("statusPulse").classList.toggle("live", !!live);
  }

  function setTicker(text) {
    $("tickerText").textContent = text;
  }

  function showProgress(label, percent) {
    $("progress").classList.remove("hidden");
    $("progressLabel").textContent = label || "Working…";
    $("progressFill").style.width = Math.max(8, Math.min(100, percent || 12)) + "%";
  }

  function hideProgress() {
    $("progress").classList.add("hidden");
  }

  function confirmModal(title, body) {
    return new Promise((resolve) => {
      $("modalTitle").textContent = title;
      $("modalBody").textContent = body;
      $("modal").classList.remove("hidden");
      const ok = () => { cleanup(); resolve(true); };
      const cancel = () => { cleanup(); resolve(false); };
      function cleanup() {
        $("modal").classList.add("hidden");
        $("modalOk").removeEventListener("click", ok);
        $("modalCancel").removeEventListener("click", cancel);
      }
      $("modalOk").addEventListener("click", ok);
      $("modalCancel").addEventListener("click", cancel);
    });
  }

  function dirtyCount() {
    const ws = state.workspace;
    if (!ws) return 0;
    return (ws.dirty || []).length + (ws.pendingDeletes || []).length;
  }

  function uploadButtonLabel(kind) {
    const n = dirtyCount();
    if (kind === "rail") return n ? ("UPLOAD ALL (" + n + ")") : "UPLOAD ALL";
    return n ? ("Upload all " + n + " change" + (n === 1 ? "" : "s")) : "Upload all changes";
  }

  function syncUploadLabels() {
    const btn = $("uploadBtn");
    if (!btn) return;
    btn.textContent = uploadButtonLabel();
    btn.title = "Upload every saved local change to the server";
  }

  function errorCount(ws = state.workspace) {
    return (ws && ws.issues || []).filter((i) => i.severity === "error").length;
  }

  function applyWorkspace(ws) {
    state.workspace = ws;
    if (ws && ws.types) applyTypesCatalog(ws.types);
    $("hudMeta").textContent = ws && ws.profileName ? ws.profileName.toUpperCase() : "NO LINK";
    $("dirtyBadge").classList.toggle("hidden", dirtyCount() === 0);
    $("dirtyBadge").textContent = dirtyCount() + " dirty";
    syncUploadLabels();
    document.querySelectorAll(".rail-btn[data-view]").forEach((btn) => {
      if (btn.dataset.view !== "connections") btn.disabled = !ws;
    });
    $("validateBtn").disabled = !ws;
    $("uploadBtn").disabled = !ws;
    if (ws && ws.quarantine && ws.quarantine.length) {
      const parsed = ((ws.markets || []).length + (ws.traders || []).length + (ws.zones || []).length);
      const first = ws.quarantine[0] || {};
      const detail = (first.filename ? first.filename + ": " : "") + (first.error || "failed");
      toast(detail + (ws.quarantine.length > 1 ? "  (+" + (ws.quarantine.length - 1) + " more)" : "") +
        (parsed ? "" : "  Browse again if this is the wrong folder."), true);
    }
    render();
  }

  function findFileByName(files, filename) {
    const key = String(filename || "").toLowerCase();
    return (files || []).find((f) => String(f.filename).toLowerCase() === key) || null;
  }

  function findClassIndex(rows, field, preferLast) {
    const key = String(field || "").trim().toLowerCase();
    if (!key) return -1;
    let found = -1;
    (rows || []).forEach((row, i) => {
      if (String(row.className || "").trim().toLowerCase() === key) {
        if (found < 0 || preferLast) found = i;
      }
    });
    return found;
  }

  function issueView(kind) {
    if (kind === "Traders" || kind === "Trader") return "traders";
    if (kind === "TraderZones" || kind === "Zones") return "zones";
    if (kind === "Types" || kind === "Type") return "types";
    return "market";
  }

  function flashEl(el) {
    if (!el) return;
    el.classList.remove("flash");
    void el.offsetWidth;
    el.classList.add("flash");
    if (typeof el.focus === "function" && el.tabIndex !== -1) {
      try { el.focus({ preventScroll: true }); } catch { el.focus(); }
    }
  }

  function jumpToIssue(issue, issueIdx) {
    if (!issue || !state.workspace) return;
    flushOpenEditors();
    const view = issueView(issue.kind);
    state.view = view;
    const field = String(issue.field || "");
    const idx = Number.isInteger(issue.itemIndex) ? issue.itemIndex : Number(issue.itemIndex);
    const hasIdx = Number.isInteger(idx) && idx >= 0;
    const msg = String(issue.message || "").toLowerCase();

    if (view === "market") {
      const file = findFileByName(state.workspace.markets, issue.filename);
      if (file) state.selected.market = file.filename;
      state.search = "";
      let item = hasIdx ? idx : -1;
      const market = file || selectedMarket();
      if (item < 0 && field && field !== "ClassName") {
        item = findClassIndex(market && market.items, field, msg.includes("duplicat"));
      }
      if (item < 0 && field === "ClassName" && market) {
        item = (market.items || []).findIndex((it) => !String(it.className || "").trim());
      }
      if (item >= 0) {
        state.selected.item = item;
        state.selectedItems = new Set([item]);
        state.lastClickedItem = item;
      }
    } else if (view === "traders") {
      const file = findFileByName(state.workspace.traders, issue.filename);
      if (file) state.selected.trader = file.filename;
    } else if (view === "types") {
      const file = findFileByName(state.workspace.typesFiles, issue.filename);
      if (file) state.selected.types = file.filename;
      state.search = "";
      let item = hasIdx ? idx : -1;
      const doc = file || selectedTypesFile();
      if (item < 0 && field && field !== "Name") {
        item = findTypeIndex(doc && doc.types, field, msg.includes("duplicat"));
      }
      if (item < 0 && field === "Name" && doc) {
        item = (doc.types || []).findIndex((it) => !String(it.name || "").trim());
      }
      if (item >= 0) {
        state.selected.typeItem = item;
        state.selectedTypeItems = new Set([item]);
        state.lastClickedType = item;
      }
    } else {
      const file = findFileByName(state.workspace.zones, issue.filename);
      if (file) state.selected.zone = file.filename;
    }

    render();
    $("drawer").classList.add("open");
    document.querySelectorAll(".issue").forEach((el) => {
      el.classList.toggle("active", Number(el.dataset.issue) === issueIdx);
    });

    requestAnimationFrame(() => {
      requestAnimationFrame(() => {
        if (view === "market") {
          const row = document.querySelector(`tr[data-item="${state.selected.item}"]`);
          if (row) {
            row.scrollIntoView({ block: "center", behavior: "smooth" });
            flashEl(row);
          }
          const focusId = ({ DisplayName: "catDisplay", Color: "catColor", InitStockPercent: "catStock",
            IsExchange: "catExchange" })[field];
          flashEl($(focusId) || (hasIdx || field === "ClassName" || findClassIndex((selectedMarket() || {}).items, field, false) >= 0 ? $("itClass") : null));
          return;
        }
        if (view === "traders") {
          if (field === "Categories" || msg.includes("category")) {
            const row = document.querySelector(`[data-cat-stem="${hasIdx ? idx : 0}"]`);
            const wrap = row && row.closest(".chip-row");
            if (wrap) wrap.scrollIntoView({ block: "center", behavior: "smooth" });
            flashEl(wrap || row);
            if (row) row.focus();
            return;
          }
          if (hasIdx || field === "Items" || msg.includes("classname") || msg.includes("item mode")) {
            const row = document.querySelector(`[data-tr-item="${hasIdx ? idx : findClassIndex((selectedTrader() || {}).items, field, msg.includes("duplicat"))}"]`);
            const wrap = row && row.closest(".chip-row");
            if (wrap) wrap.scrollIntoView({ block: "center", behavior: "smooth" });
            flashEl(wrap || row);
            if (row) row.focus();
            return;
          }
          const focusId = ({ DisplayName: "trDisplay", MinRequiredReputation: "trMinRep",
            MaxRequiredReputation: "trMaxRep" })[field];
          flashEl($(focusId) || $("trDisplay"));
          return;
        }
        if (view === "types") {
          const row = document.querySelector(`tr[data-type-item="${state.selected.typeItem}"]`);
          if (row) {
            row.scrollIntoView({ block: "center", behavior: "smooth" });
            flashEl(row);
          }
          flashEl($("tyName"));
          return;
        }
        if (hasIdx || field === "Stock" || msg.includes("stock") || msg.includes("classname")) {
          const stockIdx = hasIdx ? idx : findClassIndex((selectedZone() || {}).stock, field, msg.includes("duplicat"));
          const row = document.querySelector(`[data-st-name="${stockIdx}"]`);
          const wrap = row && row.closest(".chip-row");
          if (wrap) wrap.scrollIntoView({ block: "center", behavior: "smooth" });
          flashEl(wrap || row);
          if (row) row.focus();
          return;
        }
        const focusId = ({ m_DisplayName: "znName", DisplayName: "znName", Radius: "znR" })[field];
        flashEl($(focusId) || $("znName"));
      });
    });
  }

  function missingClassFromIssue(issue) {
    const msg = String((issue && issue.message) || "");
    if (!/not a ClassName or Variant in any Market file|not present in any Market file/i.test(msg)) return "";
    return String(issue.field || "").trim();
  }

  function classNameForInsert(raw) {
    const key = String(raw || "").trim().toLowerCase();
    const entry = (state.types.types || []).find((t) => String(t.name || "").toLowerCase() === key);
    return entry ? entry.name : String(raw || "").trim();
  }

  function closeAddMissing() {
    const modal = $("addMissing");
    if (modal) modal.classList.add("hidden");
    state.addMissing = null;
  }

  function openAddMissing(issueIdx) {
    const issue = state.drawerIssues[issueIdx];
    const name = missingClassFromIssue(issue);
    if (!name) return;
    state.addMissing = { name, issueIdx };
    $("addMissingMeta").textContent = name + " is not in any market file. Pick where to add it as an item.";
    const files = sortedFiles((state.workspace && state.workspace.markets) || []);
    const current = state.selected.market || (files[0] && files[0].filename) || "";
    $("addMissingFile").innerHTML = files.map((f) =>
      `<option value="${escapeHtml(f.filename)}" ${f.filename === current ? "selected" : ""}>${escapeHtml(f.filename)}</option>`
    ).join("") || "<option value=''>No market files yet</option>";
    $("addMissingNew").value = "";
    $("addMissing").classList.remove("hidden");
    $("addMissingFile").focus();
  }

  async function confirmAddMissing() {
    const rawName = state.addMissing && state.addMissing.name;
    if (!rawName) return;
    const className = classNameForInsert(rawName);
    const neu = $("addMissingNew").value.trim();
    let destName = $("addMissingFile").value;
    try {
      if (neu) {
        showProgress("Creating " + neu + "…", 20);
        const ws = await api("createFile", { kind: "Market", filename: neu });
        applyWorkspace(ws);
        destName = (ws.created && ws.created.filename) || JsonName(neu);
      }
      if (!destName) {
        hideProgress();
        toast("Pick a market file or name a new one.", true);
        return;
      }
      const file = findFileByName((state.workspace && state.workspace.markets) || [], destName);
      if (!file) {
        hideProgress();
        toast("Market file not found.", true);
        return;
      }
      const exists = (file.items || []).some((it) => String(it.className || "").toLowerCase() === className.toLowerCase());
      if (!exists) {
        file.items.push(defaultMarketItem(className, file.items[file.items.length - 1]));
      }
      showProgress("Saving " + file.filename + "…", 50);
      const saved = await api("saveFile", { kind: "Market", file: JSON.parse(JSON.stringify(file)) });
      const lint = await api("validateAll");
      if (saved) saved.issues = lint.issues || [];
      hideProgress();
      closeAddMissing();
      applyWorkspace(saved);
      openDrawer(saved.issues || []);
      toast(exists
        ? (className + " was already in " + file.filename)
        : ("Added " + className + " to " + file.filename + ". Press Save + lint if you change prices."));
    } catch (err) {
      hideProgress();
      toast(err.message, true);
    }
  }

  function openDrawer(issues) {
    const body = $("drawerBody");
    const list = issues || (state.workspace && state.workspace.issues) || [];
    state.drawerIssues = list;
    if (!list.length) {
      body.innerHTML = "<p class='lede'>No issues. Market, Traders, and Zones are clean.</p>";
    } else {
      body.innerHTML = list.map((issue, i) => {
        const missing = missingClassFromIssue(issue);
        return `
        <div class="issue-row">
          <button type="button" class="issue ${escapeHtml(issue.severity)}" data-issue="${i}">
            <strong>${escapeHtml(issue.severity).toUpperCase()}</strong>
            ${escapeHtml(issue.kind)} / ${escapeHtml(issue.filename)}
            ${issue.field ? " · " + escapeHtml(issue.field) : ""}<br>
            ${escapeHtml(issue.message)}
          </button>
          ${missing ? `<button type="button" class="btn ghost issue-fix" data-add-missing="${i}">Add to market…</button>` : ""}
        </div>`;
      }).join("");
      body.querySelectorAll("[data-issue]").forEach((el) => {
        el.addEventListener("click", () => {
          const i = Number(el.dataset.issue);
          jumpToIssue(state.drawerIssues[i], i);
        });
      });
      body.querySelectorAll("[data-add-missing]").forEach((el) => {
        el.addEventListener("click", (ev) => {
          ev.stopPropagation();
          openAddMissing(Number(el.dataset.addMissing));
        });
      });
    }
    $("drawer").classList.add("open");
  }

  function escapeHtml(value) {
    return String(value ?? "").replace(/[&<>"']/g, (c) => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
    }[c]));
  }

  function normalizeExpansionColor(raw) {
    let hex = String(raw || "").replace(/[#\s]/g, "").toUpperCase().replace(/[^0-9A-F]/g, "");
    if (hex.length < 6) hex = (hex + "FBFCFE").slice(0, 6);
    else hex = hex.slice(0, 6);
    return hex + "FF";
  }

  function iconSvg(name) {
    return (window.expansionIconSvg || (() => ""))(name || "");
  }

  function renderColorField(id, value) {
    const hex8 = normalizeExpansionColor(value);
    return `
      <div class="color-field">
        <input type="color" id="${id}Pick" value="#${hex8.slice(0, 6)}" title="Pick color">
        <input id="${id}" value="${hex8}" maxlength="8" spellcheck="false" autocomplete="off">
      </div>`;
  }

  function renderIconField(id, value) {
    const name = String(value || "");
    return `
      <div class="icon-field">
        <button type="button" class="icon-pick-btn" id="${id}Btn" data-icon-input="${id}">
          <span class="icon-thumb">${iconSvg(name)}</span>
          <span class="icon-pick-name">${escapeHtml(name || "Select icon")}</span>
          <span class="icon-pick-caret">▾</span>
        </button>
        <input type="hidden" id="${id}" value="${escapeHtml(name)}">
      </div>`;
  }

  function bindColorField(id) {
    const text = $(id);
    const pick = $(id + "Pick");
    if (!text || !pick) return;
    const applyText = (raw, pad) => {
      let hex = String(raw || "").replace(/[#\s]/g, "").toUpperCase().replace(/[^0-9A-F]/g, "").slice(0, 8);
      if (hex.length >= 6) hex = hex.slice(0, 6) + "FF";
      else if (pad) hex = normalizeExpansionColor(hex);
      text.value = hex;
      if (hex.length >= 6) pick.value = "#" + hex.slice(0, 6);
    };
    text.addEventListener("input", () => applyText(text.value, false));
    text.addEventListener("blur", () => applyText(text.value, true));
    pick.addEventListener("input", () => {
      text.value = pick.value.replace("#", "").toUpperCase() + "FF";
    });
  }

  function ensureIconGrid() {
    const grid = $("iconGrid");
    if (!grid || grid.dataset.ready) return grid;
    const names = [...(window.EXPANSION_ICONS || [])].sort((a, b) => a.localeCompare(b));
    grid.innerHTML = names.map((n) => `
      <button type="button" class="icon-opt" data-icon="${escapeHtml(n)}">
        <span class="icon-thumb">${iconSvg(n)}</span>
        <span>${escapeHtml(n)}</span>
      </button>`).join("");
    grid.dataset.ready = "1";
    return grid;
  }

  function closeIconMenu() {
    const menu = $("iconMenu");
    if (menu) menu.classList.add("hidden");
    document.querySelectorAll(".icon-pick-btn.open").forEach((btn) => btn.classList.remove("open"));
  }

  function openIconMenu(btn) {
    const menu = $("iconMenu");
    const search = $("iconSearch");
    const grid = ensureIconGrid();
    if (!menu || !grid || !btn) return;
    const inputId = btn.dataset.iconInput;
    const current = ($(inputId) || {}).value || "";
    menu.dataset.target = inputId || "";
    grid.querySelectorAll(".icon-opt").forEach((opt) => {
      opt.classList.toggle("selected", opt.dataset.icon === current);
      opt.classList.remove("is-hidden");
    });
    if (search) search.value = "";
    const rect = btn.getBoundingClientRect();
    const width = Math.min(440, window.innerWidth - 16);
    let left = rect.left;
    if (left + width > window.innerWidth - 8) left = Math.max(8, window.innerWidth - 8 - width);
    const maxH = 380;
    let top = rect.bottom + 4;
    if (top + 240 > window.innerHeight - 8) {
      top = Math.max(8, rect.top - 4 - Math.min(maxH, rect.top - 8));
    }
    menu.style.left = left + "px";
    menu.style.top = top + "px";
    menu.style.width = width + "px";
    menu.classList.remove("hidden");
    btn.classList.add("open");
    const selected = grid.querySelector(".icon-opt.selected");
    if (selected) selected.scrollIntoView({ block: "nearest" });
    if (search) search.focus();
  }

  function applyIconChoice(name) {
    const menu = $("iconMenu");
    const inputId = menu && menu.dataset.target;
    const hidden = inputId && $(inputId);
    const btn = inputId && $(inputId + "Btn");
    if (hidden) hidden.value = name;
    if (btn) {
      const thumb = btn.querySelector(".icon-thumb");
      const label = btn.querySelector(".icon-pick-name");
      if (thumb) thumb.innerHTML = iconSvg(name);
      if (label) label.textContent = name || "Select icon";
    }
    closeIconMenu();
  }

  function bindIconField(id) {
    const btn = $(id + "Btn");
    if (!btn) return;
    btn.addEventListener("click", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      if (btn.classList.contains("open")) closeIconMenu();
      else {
        closeIconMenu();
        openIconMenu(btn);
      }
    });
  }

  function selectedMarket() {
    const files = (state.workspace && state.workspace.markets) || [];
    return files.find((f) => f.filename === state.selected.market) || files[0] || null;
  }
  function selectedTypesFile() {
    const files = (state.workspace && state.workspace.typesFiles) || [];
    return files.find((f) => f.filename === state.selected.types) || files[0] || null;
  }
  function findTypeIndex(rows, field, preferLast) {
    const key = String(field || "").trim().toLowerCase();
    if (!key) return -1;
    let found = -1;
    (rows || []).forEach((row, i) => {
      if (String(row.name || "").trim().toLowerCase() === key) {
        if (found < 0 || preferLast) found = i;
      }
    });
    return found;
  }
  function defaultType(name) {
    return {
      name: name || "NewItem",
      nominal: 0,
      lifetime: 14400,
      restock: 0,
      min: 0,
      quantMin: -1,
      quantMax: -1,
      cost: 100,
      flags: { countInCargo: 0, countInHoarder: 0, countInMap: 1, countInPlayer: 0, crafted: 0, deloot: 0 },
      category: "",
      usages: [],
      values: [],
      tags: []
    };
  }
  function selectedTrader() {
    const files = (state.workspace && state.workspace.traders) || [];
    return files.find((f) => f.filename === state.selected.trader) || files[0] || null;
  }
  function selectedZone() {
    const files = (state.workspace && state.workspace.zones) || [];
    return files.find((f) => f.filename === state.selected.zone) || files[0] || null;
  }

  let lastEditorKey = "";

  function editorFileKey() {
    if (state.view === "market") return "market:" + (state.selected.market || "");
    if (state.view === "traders") return "traders:" + (state.selected.trader || "");
    if (state.view === "zones") return "zones:" + (state.selected.zone || "");
    if (state.view === "types") return "types:" + (state.selected.types || "");
    return state.view;
  }

  function capturePaneScroll() {
    const tops = (sel) => [...document.querySelectorAll(sel)].map((el) => el.scrollTop);
    return {
      view: state.view,
      lists: tops(".editor .list"),
      wraps: tops(".editor .table-wrap"),
      inspectors: tops(".editor .inspector"),
      stage: ($("stage") && $("stage").scrollTop) || 0
    };
  }

  function applyPaneScroll(sel, tops) {
    document.querySelectorAll(sel).forEach((el, i) => {
      if (tops[i] != null) el.scrollTop = tops[i];
    });
  }

  function render() {
    closeIconMenu();
    hideCtx();
    const saved = capturePaneScroll();
    const keepList = lastEditorKey.split(":")[0] === state.view;
    const keepContent = lastEditorKey === editorFileKey();
    const stage = $("stage");
    document.querySelectorAll(".rail-btn[data-view]").forEach((btn) => {
      btn.classList.toggle("active", btn.dataset.view === state.view);
    });
    if (state.view === "connections") stage.innerHTML = renderConnections();
    else if (state.view === "market") stage.innerHTML = renderMarket();
    else if (state.view === "traders") stage.innerHTML = renderTraders();
    else if (state.view === "zones") stage.innerHTML = renderZones();
    else if (state.view === "types") stage.innerHTML = renderTypesEditor();
    else if (state.view === "backups") stage.innerHTML = renderBackups();
    bindView();
    syncUploadLabels();
    if (keepList) applyPaneScroll(".editor .list", saved.lists);
    if (keepContent) {
      applyPaneScroll(".editor .table-wrap", saved.wraps);
      applyPaneScroll(".editor .inspector", saved.inspectors);
      stage.scrollTop = saved.stage;
    }
    lastEditorKey = editorFileKey();
  }

  function renderConnections() {
    const p = state.form;
    const cards = state.profiles.map((profile) => `
      <article class="card">
        <div class="proto">${escapeHtml(profile.protocol).toUpperCase()}</div>
        <h3>${escapeHtml(profile.name)}</h3>
        <p>${escapeHtml(profile.username)}@${escapeHtml(profile.host)}:${profile.port}<br>
        ${escapeHtml(profile.marketPath)}</p>
        <div class="actions">
          <button class="btn" data-connect="${profile.id}">Connect</button>
          <button class="btn ghost" data-browse="${profile.id}">Browse</button>
          <button class="btn ghost" data-edit="${profile.id}">Edit</button>
          <button class="btn ghost" data-test="${profile.id}">Test</button>
          <button class="btn danger" data-delete="${profile.id}">Delete</button>
        </div>
      </article>
    `).join("");
    return `
      <h1>UPLINK</h1>
      <p class="lede">Save credentials into Windows Credential Manager. Point each profile at Market, Traders, and TraderZones.</p>
      <form class="form" id="profileForm">
        <div class="field"><label>PROFILE NAME</label><input name="name" value="${escapeHtml(p.name)}" required></div>
        <div class="field"><label>PROTOCOL</label>
          <select name="protocol">
            <option value="sftp" ${p.protocol === "sftp" ? "selected" : ""}>SFTP</option>
            <option value="ftp" ${p.protocol === "ftp" ? "selected" : ""}>FTP</option>
            <option value="ftps" ${p.protocol === "ftps" ? "selected" : ""}>FTPS</option>
          </select>
        </div>
        <div class="field"><label>HOST</label><input name="host" value="${escapeHtml(p.host)}" required></div>
        <div class="field"><label>PORT</label><input name="port" type="number" value="${p.port}"></div>
        <div class="field"><label>USERNAME</label><input name="username" value="${escapeHtml(p.username)}"></div>
        <div class="field"><label>PASSWORD</label><input name="password" type="password" value="${escapeHtml(p.password)}" placeholder="${p.id ? "Saved in Credential Manager" : ""}"></div>
        <div class="field span"><label>MARKET PATH</label>
          <div class="actions">
            <input name="marketPath" value="${escapeHtml(p.marketPath)}" style="flex:1">
          </div>
        </div>
        <div class="field span"><label>TRADERS PATH</label><input name="tradersPath" value="${escapeHtml(p.tradersPath)}"></div>
        <div class="field span"><label>TRADERZONES PATH</label><input name="zonesPath" value="${escapeHtml(p.zonesPath)}"></div>
        <div class="field"><label>PASSIVE FTP</label>
          <select name="passive">
            <option value="true" ${p.passive ? "selected" : ""}>Yes</option>
            <option value="false" ${!p.passive ? "selected" : ""}>No</option>
          </select>
        </div>
        <div class="field"><label>&nbsp;</label>
          <div class="actions">
            <button class="btn" type="submit">Save profile</button>
            <button class="btn ghost" type="button" id="browseFolders">Browse folders</button>
            <button class="btn ghost" type="button" id="newProfile">New</button>
          </div>
        </div>
      </form>
      <div class="grid">${cards || "<p class='lede'>No profiles yet.</p>"}</div>
    `;
  }

  function alpha(a, b) {
    return String(a || "").toLowerCase().localeCompare(String(b || "").toLowerCase());
  }

  function sortedFiles(files) {
    if (!state.sortFiles) return files;
    return [...files].sort((a, b) => alpha(
      a.displayName || a.traderName || a.filename,
      b.displayName || b.traderName || b.filename
    ));
  }

  function sortedEntries(rows, nameFn) {
    if (!state.sortEntries) return rows;
    return [...rows].sort((a, b) => alpha(nameFn(a), nameFn(b)));
  }

  function filterEntries(rows, nameFn) {
    const q = state.search.trim().toLowerCase();
    if (!q) return rows;
    return rows.filter((row) => String(nameFn(row) || "").toLowerCase().includes(q));
  }

  function fileList(files, selected, kind) {
    return sortedFiles(files).map((file) => `
      <button class="${file.filename === selected ? "active" : ""}" data-open="${escapeHtml(file.filename)}" data-kind="${kind}">
        ${escapeHtml(file.displayName || file.traderName || file.filename)}
        <small>${escapeHtml(file.filename)}</small>
      </button>
    `).join("");
  }

  function sortFilesBtn() {
    return `<button type="button" class="icon-btn ${state.sortFiles ? "active" : ""}" id="sortFiles" title="Sort files A-Z">A-Z</button>`;
  }

  function sortEntriesBtn() {
    return `<button type="button" class="btn ghost ${state.sortEntries ? "active-sort" : ""}" id="sortEntries" title="Sort entries A-Z">${state.sortEntries ? "A-Z on" : "A-Z off"}</button>`;
  }

  function renderMarket() {
    const files = (state.workspace && state.workspace.markets) || [];
    const file = selectedMarket();
    if (file && !state.selected.market) state.selected.market = file.filename;
    const items = file ? sortedEntries(filterEntries(file.items, (it) => it.className), (it) => it.className) : [];
    const item = file && file.items[state.selected.item] ? file.items[state.selected.item] : null;
    const picked = state.selectedItems.size;
    const allVisibleOn = items.length > 0 && items.every((it) => state.selectedItems.has(file.items.indexOf(it)));
    return `
      <div class="editor">
        <section class="pane">
          <div class="pane-head"><strong>CATEGORIES</strong>
            <div class="head-actions">${sortFilesBtn()}<button class="icon-btn" id="addFile">NEW</button></div></div>
          <div class="list">${fileList(files, state.selected.market, "Market")}</div>
        </section>
        <section class="pane">
          <div class="toolbar">
            <button class="btn ghost" id="loadTypes">Pull types</button>
            <button class="btn ghost" id="addItem">Add item</button>
            <button class="btn ghost" id="dedupeItems">Remove dupes</button>
            <button class="btn" id="saveFile">Save + lint</button>
            <button class="btn danger" id="deleteFile">Delete</button>
          </div>
          ${file ? `
          <div class="inspector" style="border-bottom:1px solid var(--line)">
            <div class="field"><label>DISPLAY NAME</label><input id="catDisplay" value="${escapeHtml(file.displayName)}"></div>
            <div class="field"><label>ICON</label>${renderIconField("catIcon", file.icon)}</div>
            <div class="field"><label>COLOR</label>${renderColorField("catColor", file.color)}</div>
            <div class="field"><label>INIT STOCK %</label><input id="catStock" type="number" value="${file.initStockPercent}"></div>
            <div class="field"><label>EXCHANGE</label>
              <select id="catExchange"><option value="0" ${file.isExchange ? "" : "selected"}>No</option><option value="1" ${file.isExchange ? "selected" : ""}>Yes</option></select>
            </div>
          </div>` : ""}
          <div class="entries-bar">
            <input class="search" id="search" placeholder="Filter classnames" value="${escapeHtml(state.search)}">
            ${sortEntriesBtn()}
            <button class="btn ghost" type="button" id="selectVisible">Select visible</button>
            <button class="btn ghost" type="button" id="clearSelection">Clear</button>
            <span class="count">${picked} selected · ${items.length} shown</span>
          </div>
          <div class="table-wrap">
            <table>
              <thead><tr>
                <th class="col-check"><input type="checkbox" id="selectAllVisible" ${allVisibleOn ? "checked" : ""}></th>
                <th>CLASSNAME</th><th>MIN $</th><th>MAX $</th><th>MIN STK</th><th>MAX STK</th>
              </tr></thead>
              <tbody>
                ${items.map((it) => {
                  const idx = file.items.indexOf(it);
                  const on = state.selectedItems.has(idx);
                  return `<tr data-item="${idx}" class="${on ? "selected picked" : ""}">
                    <td class="col-check"><input type="checkbox" ${on ? "checked" : ""} tabindex="-1"></td>
                    <td>${escapeHtml(it.className)}</td>
                    <td>${it.minPriceThreshold}</td><td>${it.maxPriceThreshold}</td>
                    <td>${it.minStockThreshold}</td><td>${it.maxStockThreshold}</td>
                  </tr>`;
                }).join("")}
              </tbody>
            </table>
          </div>
        </section>
        <section class="pane inspector" id="inspector">
          ${picked > 1 ? renderBulkInspector(picked) : item ? renderItemInspector(item) : "<p class='lede'>Select an item.</p>"}
        </section>
      </div>
    `;
  }

  function renderItemInspector(item) {
    return `
      <div class="field"><label>CLASSNAME</label><input id="itClass" value="${escapeHtml(item.className)}"></div>
      <div class="field"><label>MIN PRICE</label><input id="itMinP" type="number" value="${item.minPriceThreshold}"></div>
      <div class="field"><label>MAX PRICE</label><input id="itMaxP" type="number" value="${item.maxPriceThreshold}"></div>
      <div class="field"><label>SELL %</label><input id="itSell" type="number" step="0.1" value="${item.sellPricePercent}"></div>
      <div class="field"><label>MIN STOCK</label><input id="itMinS" type="number" value="${item.minStockThreshold}"></div>
      <div class="field"><label>MAX STOCK</label><input id="itMaxS" type="number" value="${item.maxStockThreshold}"></div>
      <div class="field"><label>QTY %</label><input id="itQty" type="number" value="${item.quantityPercent}"></div>
      <div class="field"><label>ATTACHMENTS</label>
        <div class="field-row">
          <input id="itAtt" value="${escapeHtml((item.spawnAttachments || []).join(", "))}">
          <button type="button" class="btn ghost" id="editAtt">Edit</button>
        </div>
      </div>
      <div class="field"><label>VARIANTS</label>
        <div class="field-row">
          <input id="itVar" value="${escapeHtml((item.variants || []).join(", "))}">
          <button type="button" class="btn ghost" id="editVar">Edit</button>
        </div>
      </div>
      <button class="btn danger" id="removeItem">Remove item</button>
    `;
  }

  function renderBulkInspector(count) {
    return `
      <h3>BULK EDIT</h3>
      <p class="lede">${count} items selected. Fill only the fields you want to change, then apply.</p>
      <div class="field"><label>MIN PRICE</label><input id="bulkMinP" type="number" placeholder="leave unchanged"></div>
      <div class="field"><label>MAX PRICE</label><input id="bulkMaxP" type="number" placeholder="leave unchanged"></div>
      <div class="field"><label>SELL %</label><input id="bulkSell" type="number" step="0.1" placeholder="leave unchanged"></div>
      <div class="field"><label>MIN STOCK</label><input id="bulkMinS" type="number" placeholder="leave unchanged"></div>
      <div class="field"><label>MAX STOCK</label><input id="bulkMaxS" type="number" placeholder="leave unchanged"></div>
      <div class="field"><label>QTY %</label><input id="bulkQty" type="number" placeholder="leave unchanged"></div>
      <div class="actions">
        <button class="btn" id="bulkApply">Apply to selected</button>
        <button class="btn ghost" id="infiniteStock">Set Infinite Stock</button>
        <button class="btn danger" id="removeItem">Remove selected</button>
      </div>
    `;
  }

  function typesFileList(files, selected) {
    return sortedFiles(files).map((file) => `
      <button class="${file.filename === selected ? "active" : ""}" data-open="${escapeHtml(file.filename)}" data-kind="Types">
        ${escapeHtml(file.filename)}
        <small>${(file.types || []).length} types</small>
      </button>
    `).join("");
  }

  function visibleTypeRows(file) {
    const rows = file ? (file.types || []) : [];
    const filtered = rows.filter((it) => {
      const hay = [it.name, it.category, (it.usages || []).join(" "), (it.values || []).join(" ")].join(" ");
      if (!matchTypesQuery(hay, state.search)) return false;
      if (state.typesQuick === "disabled") return Number(it.nominal) === 0;
      if (state.typesQuick === "loot") return Number(it.nominal) > 0;
      if (state.typesQuick === "nocat") return Number(it.nominal) > 0 && !String(it.category || "").trim();
      return true;
    });
    return sortedEntries(filtered, (it) => it.name);
  }

  function renderTypesEditor() {
    const files = (state.workspace && state.workspace.typesFiles) || [];
    const file = selectedTypesFile();
    if (file && !state.selected.types) state.selected.types = file.filename;
    const items = visibleTypeRows(file);
    const type = file && file.types[state.selected.typeItem] ? file.types[state.selected.typeItem] : null;
    const picked = state.selectedTypeItems.size;
    const allVisibleOn = items.length > 0 && items.every((it) => state.selectedTypeItems.has(file.types.indexOf(it)));
    const catalog = (state.workspace && state.workspace.types) || {};
    return `
      <div class="editor">
        <section class="pane">
          <div class="pane-head"><strong>TYPES FILES</strong>
            <div class="head-actions">${sortFilesBtn()}<button class="icon-btn" id="addFile">NEW</button></div></div>
          <div class="list">${typesFileList(files, state.selected.types) || "<p class='lede' style='padding:12px'>Pull types from the mission to edit XML here.</p>"}</div>
        </section>
        <section class="pane">
          <div class="toolbar">
            <button class="btn ghost" id="loadTypes">Re-pull types</button>
            <button class="btn ghost" id="addType">Add type</button>
            <button class="btn ghost" id="dupType">Duplicate</button>
            <button class="btn ghost" id="disableTypes">Nominal 0</button>
            <button class="btn" id="saveFile">Save + lint</button>
            <button class="btn danger" id="deleteFile">Delete file</button>
          </div>
          <p class="types-meta">${escapeHtml(catalog.folder || "mission folder unknown")} · ${files.length} file(s) · ${(catalog.typeCount || 0)} unique classnames</p>
          <div class="entries-bar">
            <input class="search" id="search" placeholder="Filter: name && Military || !Tier1" value="${escapeHtml(state.search)}">
            ${sortEntriesBtn()}
            <button class="btn ghost" type="button" id="selectVisibleTypes">Select visible</button>
            <button class="btn ghost" type="button" id="clearTypesSel">Clear</button>
            <span class="count">${picked} selected · ${items.length} shown</span>
          </div>
          <div class="quick-filters">
            <button type="button" class="pill ${state.typesQuick === "all" ? "active" : ""}" data-types-quick="all">All</button>
            <button type="button" class="pill ${state.typesQuick === "loot" ? "active" : ""}" data-types-quick="loot">Loot (nom&gt;0)</button>
            <button type="button" class="pill ${state.typesQuick === "disabled" ? "active" : ""}" data-types-quick="disabled">Nominal 0</button>
            <button type="button" class="pill ${state.typesQuick === "nocat" ? "active" : ""}" data-types-quick="nocat">No category</button>
          </div>
          <div class="table-wrap">
            <table>
              <thead><tr>
                <th class="col-check"><input type="checkbox" id="selectAllTypes" ${allVisibleOn ? "checked" : ""}></th>
                <th>CLASSNAME</th><th>CATEGORY</th><th>NOM</th><th>MIN</th><th>LIFE</th><th>RESTOCK</th>
              </tr></thead>
              <tbody>
                ${items.map((it) => {
                  const idx = file.types.indexOf(it);
                  const on = state.selectedTypeItems.has(idx);
                  return `<tr data-type-item="${idx}" class="${on ? "selected picked" : ""} ${Number(it.nominal) === 0 ? "disabled-type" : ""}">
                    <td class="col-check"><input type="checkbox" ${on ? "checked" : ""} tabindex="-1"></td>
                    <td>${escapeHtml(it.name)}</td>
                    <td>${escapeHtml(it.category || "")}</td>
                    <td>${it.nominal}</td><td>${it.min}</td>
                    <td>${it.lifetime}</td><td>${it.restock}</td>
                  </tr>`;
                }).join("")}
              </tbody>
            </table>
          </div>
        </section>
        <section class="pane inspector" id="inspector">
          ${picked > 1 ? renderTypesBulk(picked) : type ? renderTypeInspector(type) : "<p class='lede'>Select a type. Save + lint, then Upload all changes.</p>"}
        </section>
      </div>
    `;
  }

  function flagChecks(flags, prefix) {
    const f = flags || {};
    const row = (id, key, label) =>
      `<label class="types-flag"><input type="checkbox" id="${prefix}${id}" ${Number(f[key]) ? "checked" : ""}>${label}</label>`;
    return `<div class="types-flags">
      ${row("Cargo", "countInCargo", "Count in cargo")}
      ${row("Hoarder", "countInHoarder", "Count in hoarder")}
      ${row("Map", "countInMap", "Count in map")}
      ${row("Player", "countInPlayer", "Count in player")}
      ${row("Crafted", "crafted", "Crafted")}
      ${row("Deloot", "deloot", "Dynamic loot")}
    </div>`;
  }

  function renderTypeInspector(item) {
    const f = item.flags || {};
    return `
      <div class="field"><label>CLASSNAME</label><input id="tyName" value="${escapeHtml(item.name)}"></div>
      <div class="field"><label>NOMINAL</label><input id="tyNom" type="number" value="${item.nominal}"></div>
      <div class="field"><label>MIN</label><input id="tyMin" type="number" value="${item.min}"></div>
      <div class="field"><label>LIFETIME (sec)</label><input id="tyLife" type="number" value="${item.lifetime}"></div>
      <div class="field"><label>RESTOCK (sec)</label><input id="tyRestock" type="number" value="${item.restock}"></div>
      <div class="field"><label>QUANT MIN</label><input id="tyQmin" type="number" value="${item.quantMin}"></div>
      <div class="field"><label>QUANT MAX</label><input id="tyQmax" type="number" value="${item.quantMax}"></div>
      <div class="field"><label>COST</label><input id="tyCost" type="number" value="${item.cost}"></div>
      <div class="field"><label>CATEGORY</label><input id="tyCat" value="${escapeHtml(item.category || "")}"></div>
      <div class="field"><label>USAGE (comma)</label><input id="tyUse" value="${escapeHtml((item.usages || []).join(", "))}"></div>
      <div class="field"><label>VALUE / TIER (comma)</label><input id="tyVal" value="${escapeHtml((item.values || []).join(", "))}"></div>
      <div class="field"><label>TAGS (comma)</label><input id="tyTag" value="${escapeHtml((item.tags || []).join(", "))}"></div>
      <div class="field"><label>FLAGS</label>${flagChecks(f, "ty")}</div>
      <div class="actions">
        <button class="btn ghost" id="copyTypeName">Copy name</button>
        <button class="btn danger" id="removeType">Remove type</button>
      </div>
    `;
  }

  function renderTypesBulk(count) {
    return `
      <h3>BULK EDIT</h3>
      <p class="lede">${count} types selected. Fill only the fields you want to change.</p>
      <div class="field"><label>NOMINAL</label><input id="bulkTyNom" type="number" placeholder="leave unchanged"></div>
      <div class="field"><label>MIN</label><input id="bulkTyMin" type="number" placeholder="leave unchanged"></div>
      <div class="field"><label>LIFETIME</label><input id="bulkTyLife" type="number" placeholder="leave unchanged"></div>
      <div class="field"><label>RESTOCK</label><input id="bulkTyRestock" type="number" placeholder="leave unchanged"></div>
      <div class="field"><label>QUANT MIN</label><input id="bulkTyQmin" type="number" placeholder="leave unchanged"></div>
      <div class="field"><label>QUANT MAX</label><input id="bulkTyQmax" type="number" placeholder="leave unchanged"></div>
      <div class="field"><label>COST</label><input id="bulkTyCost" type="number" placeholder="leave unchanged"></div>
      <div class="field"><label>CATEGORY</label><input id="bulkTyCat" placeholder="leave unchanged"></div>
      <div class="field"><label>USAGE (replace)</label><input id="bulkTyUse" placeholder="leave unchanged"></div>
      <div class="field"><label>VALUE / TIER (replace)</label><input id="bulkTyVal" placeholder="leave unchanged"></div>
      <div class="field"><label>TAGS (replace)</label><input id="bulkTyTag" placeholder="leave unchanged"></div>
      <div class="actions">
        <button class="btn" id="bulkTypesApply">Apply to selected</button>
        <button class="btn ghost" id="disableTypesBulk">Set nominal 0</button>
        <button class="btn ghost" id="copyTypeName">Copy names</button>
        <button class="btn danger" id="removeType">Remove selected</button>
      </div>
    `;
  }

  function collectTypes() {
    const file = selectedTypesFile();
    if (!file) return null;
    const item = file.types[state.selected.typeItem];
    if (!item || !$("tyName")) return file;
    item.name = $("tyName").value;
    item.nominal = Number($("tyNom").value);
    item.min = Number($("tyMin").value);
    item.lifetime = Number($("tyLife").value);
    item.restock = Number($("tyRestock").value);
    item.quantMin = Number($("tyQmin").value);
    item.quantMax = Number($("tyQmax").value);
    item.cost = Number($("tyCost").value);
    item.category = $("tyCat").value;
    item.usages = $("tyUse").value.split(",").map((s) => s.trim()).filter(Boolean);
    item.values = $("tyVal").value.split(",").map((s) => s.trim()).filter(Boolean);
    item.tags = $("tyTag").value.split(",").map((s) => s.trim()).filter(Boolean);
    item.flags = {
      countInCargo: $("tyCargo") && $("tyCargo").checked ? 1 : 0,
      countInHoarder: $("tyHoarder") && $("tyHoarder").checked ? 1 : 0,
      countInMap: $("tyMap") && $("tyMap").checked ? 1 : 0,
      countInPlayer: $("tyPlayer") && $("tyPlayer").checked ? 1 : 0,
      crafted: $("tyCrafted") && $("tyCrafted").checked ? 1 : 0,
      deloot: $("tyDeloot") && $("tyDeloot").checked ? 1 : 0
    };
    return file;
  }

  function syncTypesEntryRow() {
    collectTypes();
    const file = selectedTypesFile();
    const idx = state.selected.typeItem;
    const item = file && file.types[idx];
    const row = document.querySelector(`tr[data-type-item="${idx}"]`);
    if (!item || !row) return;
    const cells = row.children;
    if (cells[1]) cells[1].textContent = item.name;
    if (cells[2]) cells[2].textContent = item.category || "";
    if (cells[3]) cells[3].textContent = String(item.nominal);
    if (cells[4]) cells[4].textContent = String(item.min);
    if (cells[5]) cells[5].textContent = String(item.lifetime);
    if (cells[6]) cells[6].textContent = String(item.restock);
    row.classList.toggle("disabled-type", Number(item.nominal) === 0);
  }

  function selectedTypeIndices() {
    const picked = [...state.selectedTypeItems];
    if (!picked.length && state.selected.typeItem >= 0) picked.push(state.selected.typeItem);
    return [...new Set(picked)].sort((a, b) => b - a);
  }

  function visibleTypeIndices() {
    const file = selectedTypesFile();
    if (!file) return [];
    return visibleTypeRows(file).map((it) => file.types.indexOf(it));
  }

  function addNewType() {
    const file = selectedTypesFile();
    if (!file) return;
    const name = prompt("New classname", "NewItem");
    if (!name) return;
    file.types.push(defaultType(name.trim()));
    state.selected.typeItem = file.types.length - 1;
    state.selectedTypeItems = new Set([state.selected.typeItem]);
    render();
    toast("Added " + name + ". Press Save then lint.");
  }

  function uniqueTypeName(file, base) {
    let name = base || "NewItem_Copy";
    const used = new Set((file.types || []).map((it) => String(it.name || "").toLowerCase()));
    if (!used.has(name.toLowerCase())) return name;
    let n = 2;
    while (used.has((name + n).toLowerCase())) n += 1;
    return name + n;
  }

  function duplicateSelectedTypes() {
    const file = selectedTypesFile();
    if (!file) return;
    const ids = selectedTypeIndices().sort((a, b) => a - b);
    if (!ids.length) return;
    const created = [];
    ids.forEach((idx) => {
      const src = file.types[idx];
      if (!src) return;
      const copy = JSON.parse(JSON.stringify(src));
      copy.name = uniqueTypeName(file, String(src.name || "NewItem") + "_Copy");
      file.types.push(copy);
      created.push(file.types.length - 1);
    });
    state.selected.typeItem = created[created.length - 1] || 0;
    state.selectedTypeItems = new Set(created);
    render();
    toast("Duplicated " + created.length + " type(s). Press Save then lint.");
  }

  function disableSelectedTypes() {
    const file = selectedTypesFile();
    if (!file) return;
    selectedTypeIndices().forEach((idx) => {
      if (file.types[idx]) file.types[idx].nominal = 0;
    });
    render();
    toast("Set nominal to 0. Press Save then lint.");
  }

  async function copySelectedTypeNames() {
    const file = selectedTypesFile();
    if (!file) return;
    const names = selectedTypeIndices().sort((a, b) => a - b)
      .map((idx) => file.types[idx] && file.types[idx].name).filter(Boolean);
    const text = names.join("\n");
    try {
      if (navigator.clipboard && navigator.clipboard.writeText) await navigator.clipboard.writeText(text);
      else {
        const box = document.createElement("textarea");
        box.value = text;
        document.body.appendChild(box);
        box.select();
        document.execCommand("copy");
        box.remove();
      }
      toast("Copied " + names.length + " classname(s).");
    } catch {
      toast(text, false);
    }
  }

  function deleteSelectedTypes() {
    const file = selectedTypesFile();
    if (!file) return;
    const ids = selectedTypeIndices();
    if (!ids.length) return;
    ids.forEach((idx) => file.types.splice(idx, 1));
    state.selected.typeItem = Math.max(0, (file.types || []).length - 1);
    state.selectedTypeItems = new Set();
    render();
    toast("Removed " + ids.length + " type(s). Press Save then lint.");
  }

  function applyTypesBulk() {
    const file = selectedTypesFile();
    if (!file) return;
    const num = (id) => {
      const el = $(id);
      if (!el || el.value === "") return null;
      return Number(el.value);
    };
    const text = (id) => {
      const el = $(id);
      if (!el || !String(el.value || "").trim()) return null;
      return el.value;
    };
    const list = (id) => {
      const raw = text(id);
      return raw == null ? null : raw.split(",").map((s) => s.trim()).filter(Boolean);
    };
    const patch = {
      nominal: num("bulkTyNom"),
      min: num("bulkTyMin"),
      lifetime: num("bulkTyLife"),
      restock: num("bulkTyRestock"),
      quantMin: num("bulkTyQmin"),
      quantMax: num("bulkTyQmax"),
      cost: num("bulkTyCost"),
      category: text("bulkTyCat"),
      usages: list("bulkTyUse"),
      values: list("bulkTyVal"),
      tags: list("bulkTyTag")
    };
    selectedTypeIndices().forEach((idx) => {
      const item = file.types[idx];
      if (!item) return;
      if (patch.nominal != null) item.nominal = patch.nominal;
      if (patch.min != null) item.min = patch.min;
      if (patch.lifetime != null) item.lifetime = patch.lifetime;
      if (patch.restock != null) item.restock = patch.restock;
      if (patch.quantMin != null) item.quantMin = patch.quantMin;
      if (patch.quantMax != null) item.quantMax = patch.quantMax;
      if (patch.cost != null) item.cost = patch.cost;
      if (patch.category != null) item.category = patch.category;
      if (patch.usages) item.usages = patch.usages;
      if (patch.values) item.values = patch.values;
      if (patch.tags) item.tags = patch.tags;
    });
    render();
    toast("Applied bulk edit. Press Save then lint.");
  }

  function renderTraders() {
    const files = (state.workspace && state.workspace.traders) || [];
    const file = selectedTrader();
    if (file && !state.selected.trader) state.selected.trader = file.filename;
    const marketNames = sortedFiles((state.workspace && state.workspace.markets) || []).map((m) => m.filename.replace(/\.json$/i, ""));
    const overrides = file ? sortedEntries(filterEntries(file.items || [], (it) => it.className), (it) => it.className) : [];
    return `
      <div class="editor">
        <section class="pane">
          <div class="pane-head"><strong>TRADERS</strong>
            <div class="head-actions">${sortFilesBtn()}<button class="icon-btn" id="addFile">NEW</button></div></div>
          <div class="list">${fileList(files, state.selected.trader, "Traders")}</div>
        </section>
        <section class="pane inspector">
          ${file ? `
            <div class="field"><label>TRADER NAME (optional)</label><input id="trName" value="${escapeHtml(file.traderName || "")}" placeholder="v13 uses the filename"></div>
            <div class="field"><label>DISPLAY NAME</label><input id="trDisplay" value="${escapeHtml(file.displayName)}"></div>
            <div class="field"><label>ICON</label>${renderIconField("trIcon", file.traderIcon)}</div>
            <div class="field"><label>CURRENCIES (comma)</label><input id="trCur" value="${escapeHtml((file.currencies || []).join(", "))}"></div>
            <div class="field"><label>MIN REPUTATION</label><input id="trMinRep" type="number" value="${file.minRequiredReputation ?? 0}"></div>
            <div class="field"><label>MAX REPUTATION</label><input id="trMaxRep" type="number" value="${file.maxRequiredReputation ?? 2147483647}"></div>
            <div class="field"><label>REQUIRED FACTION</label><input id="trFaction" value="${escapeHtml(file.requiredFaction || "")}"></div>
            <div class="field"><label>REQUIRED QUEST ID</label><input id="trQuest" type="number" value="${file.requiredCompletedQuestId ?? -1}"></div>
            <div class="field"><label>DISPLAY CURRENCY VALUE</label><input id="trCurVal" type="number" value="${file.displayCurrencyValue ?? 1}"></div>
            <div class="field"><label>DISPLAY CURRENCY NAME</label><input id="trCurName" value="${escapeHtml(file.displayCurrencyName || "")}"></div>
            <div class="field"><label>USE CATEGORY ORDER</label>
              <select id="trCatOrder">
                <option value="0" ${file.useCategoryOrder ? "" : "selected"}>No</option>
                <option value="1" ${file.useCategoryOrder ? "selected" : ""}>Yes</option>
              </select>
            </div>
            <div class="actions">
              <button class="btn" id="saveFile">Save + lint</button>
              <button class="btn danger" id="deleteFile">Delete</button>
              <button class="btn ghost" id="addCat">Add category</button>
              <button class="btn ghost" id="addTraderItem">Add item override</button>
            </div>
            <h3>CATEGORIES</h3>
            ${(file.categories || []).map((c, i) => `
              <div class="chip-row">
                <select data-cat-stem="${i}">${marketNames.map((n) => `<option ${n === c.fileStem ? "selected" : ""}>${escapeHtml(n)}</option>`).join("")}</select>
                <select data-cat-mode="${i}">
                  ${[0,1,2,3].map((m) => `<option value="${m}" ${m === c.mode ? "selected" : ""}>${m}</option>`).join("")}
                </select>
                <button class="icon-btn" data-del-cat="${i}">X</button>
              </div>`).join("")}
            <h3>ITEM OVERRIDES</h3>
            <div class="entries-bar">
              <input class="search" id="search" placeholder="Filter item overrides" value="${escapeHtml(state.search)}">
              ${sortEntriesBtn()}
            </div>
            ${overrides.map((it) => {
              const i = file.items.indexOf(it);
              return `
              <div class="chip-row">
                <input data-tr-item="${i}" value="${escapeHtml(it.className)}">
                <select data-tr-mode="${i}">${[0,1,2,3].map((m) => `<option value="${m}" ${m === it.mode ? "selected" : ""}>${m}</option>`).join("")}</select>
                <button class="icon-btn" data-del-tr-item="${i}">X</button>
              </div>`;
            }).join("")}
          ` : "<p class='lede'>Select a trader.</p>"}
        </section>
        <section class="pane inspector">
          <p class="lede">0 buy · 1 buy/sell · 2 sell · 3 hidden attachments. Manual items override categories.</p>
        </section>
      </div>
    `;
  }

  function renderZones() {
    const files = (state.workspace && state.workspace.zones) || [];
    const file = selectedZone();
    if (file && !state.selected.zone) state.selected.zone = file.filename;
    const stock = file ? sortedEntries(filterEntries(file.stock || [], (row) => row.className), (row) => row.className) : [];
    return `
      <div class="editor">
        <section class="pane">
          <div class="pane-head"><strong>ZONES</strong>
            <div class="head-actions">${sortFilesBtn()}<button class="icon-btn" id="addFile">NEW</button></div></div>
          <div class="list">${fileList(files, state.selected.zone, "TraderZones")}</div>
        </section>
        <section class="pane inspector">
          ${file ? `
            <div class="field"><label>DISPLAY NAME</label><input id="znName" value="${escapeHtml(file.displayName)}"></div>
            <div class="field"><label>POSITION X</label><input id="znX" type="number" step="0.01" value="${file.position.x}"></div>
            <div class="field"><label>POSITION Y</label><input id="znY" type="number" step="0.01" value="${file.position.y}"></div>
            <div class="field"><label>POSITION Z</label><input id="znZ" type="number" step="0.01" value="${file.position.z}"></div>
            <div class="field"><label>RADIUS</label><input id="znR" type="number" step="0.1" value="${file.radius}"></div>
            <div class="field"><label>BUY %</label><input id="znBuy" type="number" step="0.1" value="${file.buyPricePercent}"></div>
            <div class="field"><label>SELL %</label><input id="znSell" type="number" step="0.1" value="${file.sellPricePercent}"></div>
            <div class="actions">
              <button class="btn" id="saveFile">Save + lint</button>
              <button class="btn danger" id="deleteFile">Delete</button>
              <button class="btn ghost" id="addStock">Add stock</button>
            </div>
            <h3>STOCK</h3>
            <div class="entries-bar">
              <input class="search" id="search" placeholder="Filter stock classnames" value="${escapeHtml(state.search)}">
              ${sortEntriesBtn()}
            </div>
            ${stock.map((row) => {
              const i = file.stock.indexOf(row);
              return `
              <div class="chip-row">
                <input data-st-name="${i}" value="${escapeHtml(row.className)}">
                <input data-st-val="${i}" type="number" value="${row.stock}">
                <button class="icon-btn" data-del-st="${i}">X</button>
              </div>`;
            }).join("")}
          ` : "<p class='lede'>Select a zone.</p>"}
        </section>
        <section class="pane inspector">
          <p class="lede">Zones are spheres. Altitude must be correct or traders inside will not act as traders.</p>
        </section>
      </div>
    `;
  }

  function renderBackups() {
    const list = state.backups || [];
    return `
      <h1>BACKUPS</h1>
      <p class="lede">Each confirmed upload zips the previous local set into %APPDATA%\\EDITY\\backups.</p>
      <div class="grid">${list.map((name) => `<article class="card"><h3>${escapeHtml(name)}</h3></article>`).join("") || "<p class='lede'>No backups yet.</p>"}</div>
    `;
  }

  function readProfileForm(form) {
    const data = new FormData(form);
    const profile = {
      id: state.form.id,
      name: data.get("name"),
      protocol: data.get("protocol"),
      host: data.get("host"),
      port: Number(data.get("port") || (data.get("protocol") === "sftp" ? 22 : 21)),
      username: data.get("username"),
      passive: data.get("passive") === "true",
      marketPath: data.get("marketPath"),
      tradersPath: data.get("tradersPath"),
      zonesPath: data.get("zonesPath")
    };
    return { profile, password: String(data.get("password") || "") };
  }

  function collectMarket() {
    const file = selectedMarket();
    if (!file) return null;
    if (!$("catDisplay")) return file;
    file.displayName = $("catDisplay").value;
    file.icon = $("catIcon").value;
    file.color = normalizeExpansionColor($("catColor").value);
    file.initStockPercent = Number($("catStock").value);
    file.isExchange = Number($("catExchange").value);
    const item = file.items[state.selected.item];
    if (item && $("itClass")) {
      item.className = $("itClass").value;
      item.minPriceThreshold = Number($("itMinP").value);
      item.maxPriceThreshold = Number($("itMaxP").value);
      item.sellPricePercent = Number($("itSell").value);
      item.minStockThreshold = Number($("itMinS").value);
      item.maxStockThreshold = Number($("itMaxS").value);
      item.quantityPercent = Number($("itQty").value);
      item.spawnAttachments = $("itAtt").value.split(",").map((s) => s.trim()).filter(Boolean);
      item.variants = $("itVar").value.split(",").map((s) => s.trim()).filter(Boolean);
    }
    return file;
  }

  function syncMarketEntryRow() {
    collectMarket();
    const file = selectedMarket();
    const idx = state.selected.item;
    const item = file && file.items[idx];
    const row = document.querySelector(`tr[data-item="${idx}"]`);
    if (!item || !row) return;
    const cells = row.children;
    if (cells[1]) cells[1].textContent = item.className;
    if (cells[2]) cells[2].textContent = String(item.minPriceThreshold);
    if (cells[3]) cells[3].textContent = String(item.maxPriceThreshold);
    if (cells[4]) cells[4].textContent = String(item.minStockThreshold);
    if (cells[5]) cells[5].textContent = String(item.maxStockThreshold);
  }

  function flushOpenEditors() {
    if (!state.workspace) return;
    if (state.view === "market" && $("catDisplay")) collectMarket();
    if (state.view === "traders" && $("trDisplay")) collectTrader();
    if (state.view === "zones" && $("znName")) collectZone();
    if (state.view === "types" && $("tyName")) collectTypes();
  }

  function hideCtx() {
    $("ctx").classList.add("hidden");
    if ($("ctxTypes")) $("ctxTypes").classList.add("hidden");
  }

  function selectedItemIndices() {
    const picked = [...state.selectedItems];
    if (!picked.length && state.selected.item >= 0) picked.push(state.selected.item);
    return [...new Set(picked)].sort((a, b) => b - a);
  }

  function visibleMarketIndices() {
    const file = selectedMarket();
    if (!file) return [];
    return sortedEntries(filterEntries(file.items, (it) => it.className), (it) => it.className)
      .map((it) => file.items.indexOf(it));
  }

  function selectVisibleMarketItems() {
    const ids = visibleMarketIndices();
    state.selectedItems = new Set(ids);
    if (ids.length) {
      state.selected.item = ids[0];
      state.lastClickedItem = ids[0];
    }
    render();
  }

  function clearMarketSelection() {
    state.selectedItems = new Set();
    render();
  }

  function toggleVisibleMarketItems() {
    const ids = visibleMarketIndices();
    const allOn = ids.length > 0 && ids.every((i) => state.selectedItems.has(i));
    if (allOn) {
      ids.forEach((i) => state.selectedItems.delete(i));
    } else {
      ids.forEach((i) => state.selectedItems.add(i));
      if (ids.length) {
        state.selected.item = ids[0];
        state.lastClickedItem = ids[0];
      }
    }
    render();
  }

  function applyBulkEdit() {
    const file = selectedMarket();
    if (!file) return;
    const indices = [...state.selectedItems];
    if (indices.length < 2) {
      toast("Select at least two items to bulk edit.", true);
      return;
    }
    const fields = [
      ["bulkMinP", "minPriceThreshold"],
      ["bulkMaxP", "maxPriceThreshold"],
      ["bulkSell", "sellPricePercent"],
      ["bulkMinS", "minStockThreshold"],
      ["bulkMaxS", "maxStockThreshold"],
      ["bulkQty", "quantityPercent"],
    ];
    const updates = fields.map(([id, key]) => {
      const el = $(id);
      if (!el || String(el.value).trim() === "") return null;
      return [key, Number(el.value)];
    }).filter(Boolean);
    if (!updates.length) {
      toast("Fill at least one field to apply.", true);
      return;
    }
    for (const idx of indices) {
      const item = file.items[idx];
      if (!item) continue;
      for (const [key, value] of updates) item[key] = value;
    }
    render();
    toast("Bulk updated " + indices.length + " item(s). Press Save then lint.");
  }

  function deleteSelectedMarketItems() {
    const file = selectedMarket();
    if (!file) return;
    flushOpenEditors();
    const indices = selectedItemIndices();
    if (!indices.length) return;
    for (const idx of indices) {
      if (idx >= 0 && idx < file.items.length) file.items.splice(idx, 1);
    }
    state.selectedItems = new Set();
    state.selected.item = Math.min(state.selected.item, Math.max(0, file.items.length - 1));
    hideCtx();
    render();
    toast("Removed " + indices.length + " item(s). Press Save then lint.");
  }

  function setInfiniteStock() {
    const file = selectedMarket();
    if (!file) return;
    flushOpenEditors();
    const indices = selectedItemIndices();
    if (!indices.length) {
      toast("Select one or more items first.", true);
      return;
    }
    for (const idx of indices) {
      const item = file.items[idx];
      if (!item) continue;
      item.minStockThreshold = 1;
      item.maxStockThreshold = 1;
    }
    render();
    toast("Set infinite stock (min/max 1) on " + indices.length + " item(s). Press Save then lint.");
  }

  function variantSourceIndices() {
    const file = selectedMarket();
    if (!file) return [];
    let sources = [...state.selectedItems];
    if (!sources.length && state.selected.item >= 0) sources = [state.selected.item];
    return [...new Set(sources)].filter((i) => file.items[i]);
  }

  function closeVariantPick() {
    const pick = $("variantPick");
    if (pick) pick.classList.add("hidden");
    state.variantMove = null;
  }

  function openVariantParentPicker() {
    hideCtx();
    flushOpenEditors();
    const file = selectedMarket();
    const sources = variantSourceIndices();
    if (!file || !sources.length) {
      toast("Right-click an entry first.", true);
      return;
    }
    if (file.items.length - sources.length < 1) {
      toast("Need another entry in this file to use as the parent.", true);
      return;
    }
    state.variantMove = { sources };
    const names = sources.map((i) => file.items[i].className).filter(Boolean);
    $("variantPickMeta").textContent = names.length === 1
      ? ("Move " + names[0] + " into another item's Variants, then remove it as its own entry.")
      : ("Move " + names.length + " selected items into another item's Variants, then remove them as their own entries.");
    $("variantPickFilter").value = "";
    renderVariantPickList();
    $("variantPick").classList.remove("hidden");
    $("variantPickFilter").focus();
  }

  function renderVariantPickList() {
    const file = selectedMarket();
    const list = $("variantPickList");
    if (!file || !list || !state.variantMove) return;
    const blocked = new Set(state.variantMove.sources);
    const needle = (($("variantPickFilter") || {}).value || "").trim().toLowerCase();
    const rows = file.items
      .map((item, idx) => ({ item, idx }))
      .filter((row) => !blocked.has(row.idx))
      .filter((row) => !needle || String(row.item.className || "").toLowerCase().includes(needle))
      .sort((a, b) => String(a.item.className || "").localeCompare(String(b.item.className || ""), undefined, { sensitivity: "base" }));
    list.innerHTML = rows.map((row) => `
      <button type="button" class="types-row" data-variant-parent="${row.idx}">
        <span></span>
        <span>${escapeHtml(row.item.className)}</span>
        <small>${row.item.minPriceThreshold}–${row.item.maxPriceThreshold}</small>
        <small>${(row.item.variants || []).length} variants</small>
      </button>`).join("") || "<p class='lede' style='padding:12px'>No other entries match.</p>";
    list.querySelectorAll("[data-variant-parent]").forEach((btn) => {
      btn.addEventListener("click", () => applyMakeVariantOf(Number(btn.dataset.variantParent)));
    });
  }

  function applyMakeVariantOf(parentIdx) {
    const file = selectedMarket();
    const sources = (state.variantMove && state.variantMove.sources) || [];
    if (!file || parentIdx < 0 || !file.items[parentIdx] || sources.includes(parentIdx)) {
      toast("Pick a different entry as the parent.", true);
      return;
    }
    const parent = file.items[parentIdx];
    parent.variants = parent.variants || [];
    const existing = new Set(parent.variants.map((v) => String(v || "").trim().toLowerCase()));
    const parentKey = String(parent.className || "").trim().toLowerCase();
    if (parentKey) existing.add(parentKey);
    const moved = [];
    const addName = (raw) => {
      const name = String(raw || "").trim();
      if (!name || existing.has(name.toLowerCase())) return;
      parent.variants.push(name);
      existing.add(name.toLowerCase());
    };
    for (const idx of sources) {
      const item = file.items[idx];
      if (!item) continue;
      addName(item.className);
      moved.push(String(item.className || "").trim());
      for (const variant of (item.variants || [])) addName(variant);
    }
    const sourceSet = new Set(sources);
    file.items = file.items.filter((_, i) => !sourceSet.has(i));
    const next = file.items.indexOf(parent);
    state.selected.item = next >= 0 ? next : 0;
    state.selectedItems = new Set(next >= 0 ? [next] : []);
    state.lastClickedItem = state.selected.item;
    closeVariantPick();
    render();
    const label = moved.filter(Boolean).join(", ") || "selection";
    toast("Moved " + label + " into variants of " + parent.className + ". Press Save then lint.");
  }

  function removeDuplicateClassnames() {
    const file = selectedMarket();
    if (!file) return;
    flushOpenEditors();
    const seen = new Set();
    const kept = [];
    let removed = 0;
    for (const item of file.items) {
      const key = String(item.className || "").trim().toLowerCase();
      if (!key || seen.has(key)) {
        removed += 1;
        continue;
      }
      seen.add(key);
      kept.push(item);
    }
    file.items = kept;
    state.selectedItems = new Set();
    state.selected.item = 0;
    hideCtx();
    render();
    toast(removed ? ("Removed " + removed + " duplicate classname(s). Press Save then lint.") : "No duplicate classnames in this file.");
  }

  function collectTrader() {
    const file = selectedTrader();
    if (!file) return null;
    file.traderName = $("trName").value;
    file.displayName = $("trDisplay").value;
    file.traderIcon = $("trIcon").value;
    file.currencies = $("trCur").value.split(",").map((s) => s.trim()).filter(Boolean);
    file.minRequiredReputation = Number($("trMinRep").value);
    file.maxRequiredReputation = Number($("trMaxRep").value);
    file.requiredFaction = $("trFaction").value;
    file.requiredCompletedQuestId = Number($("trQuest").value);
    file.displayCurrencyValue = Number($("trCurVal").value);
    file.displayCurrencyName = $("trCurName").value;
    file.useCategoryOrder = Number($("trCatOrder").value);
    file.categories.forEach((cat, i) => {
      const stem = document.querySelector(`[data-cat-stem="${i}"]`);
      const mode = document.querySelector(`[data-cat-mode="${i}"]`);
      if (stem) cat.fileStem = stem.value;
      if (mode) cat.mode = Number(mode.value);
    });
    file.items.forEach((item, i) => {
      const name = document.querySelector(`[data-tr-item="${i}"]`);
      const mode = document.querySelector(`[data-tr-mode="${i}"]`);
      if (name) item.className = name.value;
      if (mode) item.mode = Number(mode.value);
    });
    return file;
  }

  function collectZone() {
    const file = selectedZone();
    if (!file) return null;
    file.displayName = $("znName").value;
    file.position = { x: Number($("znX").value), y: Number($("znY").value), z: Number($("znZ").value) };
    file.radius = Number($("znR").value);
    file.buyPricePercent = Number($("znBuy").value);
    file.sellPricePercent = Number($("znSell").value);
    file.stock.forEach((row, i) => {
      const name = document.querySelector(`[data-st-name="${i}"]`);
      const val = document.querySelector(`[data-st-val="${i}"]`);
      if (name) row.className = name.value;
      if (val) row.stock = Number(val.value);
    });
    return file;
  }

  async function afterSave(ws) {
    applyWorkspace(ws);
    const errors = errorCount(ws);
    if (errors) {
      openDrawer(ws.issues);
      toast("Saved locally. " + errors + " lint error(s).", true);
      return;
    }
    const pending = dirtyCount();
    toast("Saved locally. Lint passed." + (pending ? " " + pending + " file(s) ready to upload." : ""));
    if (pending) setTicker(pending + " local change(s) waiting. Use Upload when you are ready.");
  }

  async function requestUpload() {
    if (!state.workspace) {
      toast("Connect first.", true);
      return;
    }
    try {
      flushOpenEditors();
      const file = state.view === "market" ? selectedMarket()
        : state.view === "traders" ? selectedTrader()
        : state.view === "zones" ? selectedZone()
        : state.view === "types" ? selectedTypesFile() : null;
      if (file && (state.view === "market" || state.view === "traders" || state.view === "zones" || state.view === "types")) {
        showProgress("Saving current file…", 20);
        const kind = state.view === "traders" ? "Traders"
          : state.view === "zones" ? "TraderZones"
          : state.view === "types" ? "Types" : "Market";
        const snapshot = JSON.parse(JSON.stringify(file));
        const ws = await api("saveFile", { kind, file: snapshot });
        const lint = await api("validateAll");
        if (ws) ws.issues = lint.issues || [];
        hideProgress();
        applyWorkspace(ws);
        if (errorCount(ws)) {
          openDrawer(ws.issues);
          toast("Lint errors — upload blocked until you fix them.", true);
          return;
        }
      }
      const n = dirtyCount();
      if (!n) {
        toast("No local changes to upload.");
        return;
      }
      const ok = await confirmModal(
        "Upload " + n + " changed file(s)?",
        "This will zip the current local set, overwrite those files on the FTP/SFTP, then pull a fresh copy."
      );
      if (!ok) return;
      await runUpload();
    } catch (err) {
      hideProgress();
      toast(err.message, true);
    }
  }

  async function runUpload() {
    try {
      showProgress("Uploading…", 20);
      const result = await api("confirmUpload", {});
      hideProgress();
      if (!result.uploaded) {
        if (result.issues) openDrawer(result.issues);
        toast(result.error || "Upload blocked", true);
        return;
      }
      applyWorkspace(result.workspace);
      toast("Remote updated. Backup " + (result.backup || "").split(/[/\\]/).pop());
      setStatus("SYNCED", true);
    } catch (err) {
      hideProgress();
      toast(err.message, true);
    }
  }

  async function saveCurrent() {
    try {
      let kind = "Market";
      let file = null;
      flushOpenEditors();
      if (state.view === "market") { kind = "Market"; file = selectedMarket(); }
      if (state.view === "traders") { kind = "Traders"; file = collectTrader(); }
      if (state.view === "zones") { kind = "TraderZones"; file = collectZone(); }
      if (state.view === "types") { kind = "Types"; file = collectTypes(); }
      if (!file) return;
      const snapshot = JSON.parse(JSON.stringify(file));
      showProgress("Saving locally…", 20);
      const ws = await api("saveFile", { kind, file: snapshot });
      showProgress("Linting saved file…", 80);
      const lint = await api("validateAll");
      if (ws) ws.issues = lint.issues || [];
      hideProgress();
      await afterSave(ws);
    } catch (err) {
      hideProgress();
      toast(err.message, true);
    }
  }

  function browserCredsFromForm() {
    const form = $("profileForm");
    if (!form) {
      return { profile: { ...state.form }, password: state.form.password || "" };
    }
    const parsed = readProfileForm(form);
    return { profile: parsed.profile, password: parsed.password || state.form.password || "" };
  }

  function updatePickLabels() {
    $("pickMarket").textContent = state.browser.picks.market || "not set";
    $("pickTraders").textContent = state.browser.picks.traders || "not set";
    $("pickZones").textContent = state.browser.picks.zones || "not set";
  }

  function renderBrowserList() {
    const list = $("browserList");
    const path = state.browser.path || "/ (login directory)";
    $("browserPath").textContent = path;
    const rows = state.browser.entries.map((entry) => {
      const kind = entry.isDir ? "FOLDER" : entry.isJson ? "JSON" : "FILE";
      const active = state.browser.selected && state.browser.selected.path === entry.path ? " active" : "";
      return `<button type="button" class="browser-row${active}" data-remote="${escapeHtml(entry.path)}" data-dir="${entry.isDir ? "1" : "0"}">
        <span>${entry.isDir ? "▸ " : ""}${escapeHtml(entry.name)}</span>
        <span class="meta">${kind}</span>
      </button>`;
    }).join("");
    list.innerHTML = rows || "<p class='lede' style='padding:12px'>This folder is empty.</p>";
    $("browserHint").textContent = state.browser.jsonCount
      ? state.browser.jsonCount + " JSON file(s) in this folder. If these are trader files, assign the folder below."
      : "Open folders until you see the JSON files, then mark Market / Traders / TraderZones.";
    list.querySelectorAll("[data-remote]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const entry = state.browser.entries.find((e) => e.path === btn.dataset.remote);
        state.browser.selected = entry || null;
        if (entry && entry.isDir) {
          await loadBrowser(entry.path);
        } else {
          renderBrowserList();
        }
      });
    });
    updatePickLabels();
  }

  async function loadBrowser(path) {
    showProgress("Listing " + (path || "/"), 20);
    const data = await api("browseRemote", {
      profile: state.browser.profile,
      password: state.browser.password,
      path: path || ""
    });
    hideProgress();
    state.browser.path = data.path || "";
    state.browser.parent = data.parent || "";
    state.browser.entries = data.entries || [];
    state.browser.jsonCount = data.jsonCount || 0;
    state.browser.suggestions = data.suggestions || {};
    state.browser.selected = { path: state.browser.path, isDir: true, name: state.browser.path };
    if (data.suggestions) {
      if (data.suggestions.market && !state.browser.picks.market) state.browser.picks.market = data.suggestions.market;
      if (data.suggestions.traders && !state.browser.picks.traders) state.browser.picks.traders = data.suggestions.traders;
      if (data.suggestions.zones && !state.browser.picks.zones) state.browser.picks.zones = data.suggestions.zones;
    }
    renderBrowserList();
  }

  async function openBrowser(profile, password) {
    state.browser.profile = profile;
    state.browser.password = password || "";
    state.browser.picks = {
      market: profile.marketPath || "",
      traders: profile.tradersPath || "",
      zones: profile.zonesPath || ""
    };
    $("browser").classList.remove("hidden");
    updatePickLabels();
    try {
      await loadBrowser("");
    } catch (err) {
      hideProgress();
      toast(err.message, true);
    }
  }

  function currentBrowserFolder() {
    return (state.browser.selected && state.browser.selected.isDir && state.browser.selected.path) || state.browser.path || "";
  }

  async function applyBrowserPaths() {
    const picks = state.browser.picks;
    if (!picks.market && !picks.traders && !picks.zones) {
      toast("Pick at least one folder first", true);
      return;
    }
    state.form = { ...state.form, ...state.browser.profile, password: state.browser.password };
    state.form.marketPath = picks.market || state.form.marketPath;
    state.form.tradersPath = picks.traders || state.form.tradersPath;
    state.form.zonesPath = picks.zones || state.form.zonesPath;
    try {
      const data = await api("saveProfile", { profile: state.form, password: state.browser.password });
      state.profiles = data.profiles || [];
      if (data.profile) state.form = { ...state.form, ...data.profile, password: state.browser.password };
    } catch (err) {
      toast(err.message, true);
      return;
    }
    $("browser").classList.add("hidden");
    toast("Folder paths saved. Connect when ready.");
    render();
  }

  function bindView() {
    if (state.view === "connections") {
      const form = $("profileForm");
      form.addEventListener("submit", async (ev) => {
        ev.preventDefault();
        try {
          const { profile, password } = readProfileForm(form);
          const data = await api("saveProfile", { profile, password });
          state.profiles = data.profiles || [];
          state.form = emptyProfile();
          toast("Profile saved");
          render();
        } catch (err) { toast(err.message, true); }
      });
      $("newProfile").addEventListener("click", () => { state.form = emptyProfile(); render(); });
      $("browseFolders").addEventListener("click", async () => {
        const creds = browserCredsFromForm();
        state.form = { ...state.form, ...creds.profile, password: creds.password };
        await openBrowser(creds.profile, creds.password);
      });
      document.querySelectorAll("[data-browse]").forEach((btn) => btn.addEventListener("click", async () => {
        const profile = state.profiles.find((p) => p.id === btn.dataset.browse);
        if (profile) await openBrowser(profile, "");
      }));
      document.querySelectorAll("[data-edit]").forEach((btn) => btn.addEventListener("click", () => {
        const profile = state.profiles.find((p) => p.id === btn.dataset.edit);
        if (profile) { state.form = { ...profile, password: "" }; render(); }
      }));
      document.querySelectorAll("[data-delete]").forEach((btn) => btn.addEventListener("click", async () => {
        if (!await confirmModal("Delete profile?", "Saved credentials for this link will be removed.")) return;
        const data = await api("deleteProfile", { id: btn.dataset.delete });
        state.profiles = data.profiles || [];
        render();
      }));
      document.querySelectorAll("[data-test]").forEach((btn) => btn.addEventListener("click", async () => {
        try {
          showProgress("Testing link…", 15);
          await api("testConnection", { id: btn.dataset.test });
          hideProgress();
          toast("Connection OK");
        } catch (err) { hideProgress(); toast(err.message, true); }
      }));
      document.querySelectorAll("[data-connect]").forEach((btn) => btn.addEventListener("click", async () => {
        try {
          showProgress("Pulling remote files…", 10);
          setStatus("LINKING", true);
          const ws = await api("connect", { id: btn.dataset.connect });
          hideProgress();
          applyWorkspace(ws);
          const parsed = ((ws.markets || []).length + (ws.traders || []).length + (ws.zones || []).length);
          if (!parsed) {
            setStatus("ONLINE", true);
            setTicker("Connected, but no valid Expansion files were found. Select the correct folders.");
            const profile = state.profiles.find((p) => p.id === btn.dataset.connect);
            render();
            if (profile) await openBrowser(profile, "");
            return;
          }
          state.view = "market";
          setStatus("ONLINE", true);
          setTicker("Local workspace synced. Save + lint files as you go, then Upload when ready.");
          render();
        } catch (err) {
          hideProgress();
          setStatus("STANDBY", false);
          toast(err.message, true);
        }
      }));
    }

    document.querySelectorAll("[data-open]").forEach((btn) => btn.addEventListener("click", () => {
      flushOpenEditors();
      if (btn.dataset.kind === "Market") state.selected.market = btn.dataset.open;
      if (btn.dataset.kind === "Traders") state.selected.trader = btn.dataset.open;
      if (btn.dataset.kind === "TraderZones") state.selected.zone = btn.dataset.open;
      if (btn.dataset.kind === "Types") state.selected.types = btn.dataset.open;
      state.selected.item = 0;
      state.selectedItems = new Set();
      state.selected.typeItem = 0;
      state.selectedTypeItems = new Set();
      render();
    }));
    const search = $("search");
    if (search) {
      search.addEventListener("input", (ev) => {
        state.search = ev.target.value;
        state.searchCaret = ev.target.selectionStart;
        render();
      });
      if (state.searchCaret != null) {
        search.focus();
        const pos = Math.min(state.searchCaret, search.value.length);
        search.setSelectionRange(pos, pos);
        state.searchCaret = null;
      }
    }
    const selectVisible = $("selectVisible");
    if (selectVisible) selectVisible.addEventListener("click", selectVisibleMarketItems);
    const clearSelection = $("clearSelection");
    if (clearSelection) clearSelection.addEventListener("click", clearMarketSelection);
    const selectAll = $("selectAllVisible");
    if (selectAll) selectAll.addEventListener("click", (ev) => {
      ev.stopPropagation();
      toggleVisibleMarketItems();
    });
    const bulkApply = $("bulkApply");
    if (bulkApply) bulkApply.addEventListener("click", applyBulkEdit);
    document.querySelectorAll("[data-item]").forEach((row) => row.addEventListener("click", (ev) => {
      flushOpenEditors();
      const idx = Number(row.dataset.item);
      const onCheck = ev.target.closest("input[type='checkbox']");
      const additive = ev.ctrlKey || ev.metaKey || Boolean(onCheck);
      if (ev.shiftKey) {
        const visible = [...document.querySelectorAll("[data-item]")].map((r) => Number(r.dataset.item));
        const from = visible.indexOf(state.lastClickedItem);
        const to = visible.indexOf(idx);
        const lo = Math.min(from, to);
        const hi = Math.max(from, to);
        const range = (from >= 0 && to >= 0) ? visible.slice(lo, hi + 1) : [idx];
        if (additive) range.forEach((i) => state.selectedItems.add(i));
        else state.selectedItems = new Set(range);
      } else if (additive) {
        if (state.selectedItems.has(idx)) state.selectedItems.delete(idx);
        else state.selectedItems.add(idx);
        state.lastClickedItem = idx;
      } else {
        state.selectedItems = new Set([idx]);
        state.lastClickedItem = idx;
      }
      state.selected.item = idx;
      render();
    }));
    document.querySelectorAll("[data-item]").forEach((row) => row.addEventListener("contextmenu", (ev) => {
      ev.preventDefault();
      flushOpenEditors();
      const idx = Number(row.dataset.item);
      if (!state.selectedItems.has(idx)) {
        state.selectedItems = new Set([idx]);
        state.selected.item = idx;
        state.lastClickedItem = idx;
        document.querySelectorAll("[data-item]").forEach((r) => {
          r.classList.toggle("selected", Number(r.dataset.item) === idx);
          r.classList.toggle("picked", Number(r.dataset.item) === idx);
        });
      }
      const menu = $("ctx");
      menu.classList.remove("hidden");
      menu.style.left = Math.min(ev.clientX, window.innerWidth - 240) + "px";
      menu.style.top = Math.min(ev.clientY, window.innerHeight - 90) + "px";
    }));
    const sortFiles = $("sortFiles");
    if (sortFiles) sortFiles.addEventListener("click", () => {
      state.sortFiles = !state.sortFiles;
      render();
    });
    const sortEntries = $("sortEntries");
    if (sortEntries) sortEntries.addEventListener("click", () => {
      flushOpenEditors();
      state.sortEntries = !state.sortEntries;
      render();
    });
    const infinite = $("infiniteStock");
    if (infinite) infinite.addEventListener("click", setInfiniteStock);
    const dedupe = $("dedupeItems");
    if (dedupe) dedupe.addEventListener("click", removeDuplicateClassnames);
    const save = $("saveFile");
    if (save) save.addEventListener("click", saveCurrent);
    const addFile = $("addFile");
    if (addFile) addFile.addEventListener("click", async () => {
      const typesMode = state.view === "types";
      const name = prompt(
        typesMode ? "New types path relative to the mission (e.g. CustomTypes/MyMod_types.xml)" : "New filename (without path)",
        typesMode ? "CustomTypes/New_types.xml" : "New_Category"
      );
      if (!name) return;
      const kind = state.view === "traders" ? "Traders"
        : state.view === "zones" ? "TraderZones"
        : typesMode ? "Types" : "Market";
      try {
        const ws = await api("createFile", { kind, filename: name });
        applyWorkspace(ws);
        if (kind === "Market") state.selected.market = JsonName(name);
        if (kind === "Traders") state.selected.trader = JsonName(name);
        if (kind === "TraderZones") state.selected.zone = JsonName(name);
        if (kind === "Types") {
          state.selected.types = (ws.created && ws.created.filename) || TypesName(name);
          state.selected.typeItem = 0;
          state.selectedTypeItems = new Set();
        }
        render();
      } catch (err) { toast(err.message, true); }
    });
    const delFile = $("deleteFile");
    if (delFile) delFile.addEventListener("click", async () => {
      const file = state.view === "traders" ? selectedTrader()
        : state.view === "zones" ? selectedZone()
        : state.view === "types" ? selectedTypesFile()
        : selectedMarket();
      if (!file) return;
      const vanilla = state.view === "types" && String(file.filename || "").toLowerCase() === "db/types.xml";
      if (!await confirmModal(
        "Delete " + file.filename + "?",
        vanilla
          ? "This is the vanilla mission types file. It will be removed locally and deleted on the remote after you confirm upload."
          : "This will be removed locally and deleted on the remote after you confirm upload."
      )) return;
      const kind = state.view === "traders" ? "Traders"
        : state.view === "zones" ? "TraderZones"
        : state.view === "types" ? "Types" : "Market";
      const ws = await api("deleteFile", { kind, filename: file.filename });
      applyWorkspace(ws);
    });
    const loadTypes = $("loadTypes");
    if (loadTypes) loadTypes.addEventListener("click", importTypesFolder);
    const addItem = $("addItem");
    if (addItem) addItem.addEventListener("click", () => openTypesWizard("items"));
    const editAtt = $("editAtt");
    if (editAtt) editAtt.addEventListener("click", () => openListEditor("attachments"));
    const editVar = $("editVar");
    if (editVar) editVar.addEventListener("click", () => openListEditor("variants"));
    const removeItem = $("removeItem");
    if (removeItem) removeItem.addEventListener("click", deleteSelectedMarketItems);
    const addCat = $("addCat");
    if (addCat) addCat.addEventListener("click", () => {
      const file = selectedTrader();
      const first = ((state.workspace.markets || [])[0] || {}).filename || "Category.json";
      file.categories.push({ fileStem: first.replace(/\.json$/i, ""), mode: 1 });
      render();
    });
    const addTraderItem = $("addTraderItem");
    if (addTraderItem) addTraderItem.addEventListener("click", () => {
      selectedTrader().items.push({ className: "classname", mode: 1 });
      render();
    });
    document.querySelectorAll("[data-del-cat]").forEach((btn) => btn.addEventListener("click", () => {
      selectedTrader().categories.splice(Number(btn.dataset.delCat), 1);
      render();
    }));
    document.querySelectorAll("[data-del-tr-item]").forEach((btn) => btn.addEventListener("click", () => {
      selectedTrader().items.splice(Number(btn.dataset.delTrItem), 1);
      render();
    }));
    const addStock = $("addStock");
    if (addStock) addStock.addEventListener("click", () => {
      selectedZone().stock.push({ className: "classname", stock: 0 });
      render();
    });
    document.querySelectorAll("[data-del-st]").forEach((btn) => btn.addEventListener("click", () => {
      selectedZone().stock.splice(Number(btn.dataset.delSt), 1);
      render();
    }));
    bindColorField("catColor");
    bindIconField("catIcon");
    bindIconField("trIcon");
    ["itClass", "itMinP", "itMaxP", "itMinS", "itMaxS"].forEach((id) => {
      const el = $(id);
      if (el) el.addEventListener("input", syncMarketEntryRow);
    });
    const addType = $("addType");
    if (addType) addType.addEventListener("click", addNewType);
    const dupType = $("dupType");
    if (dupType) dupType.addEventListener("click", duplicateSelectedTypes);
    document.querySelectorAll("[data-types-quick]").forEach((btn) => btn.addEventListener("click", () => {
      state.typesQuick = btn.dataset.typesQuick;
      render();
    }));
    const selectVisibleTypes = $("selectVisibleTypes");
    if (selectVisibleTypes) selectVisibleTypes.addEventListener("click", () => {
      state.selectedTypeItems = new Set(visibleTypeIndices());
      if (state.selectedTypeItems.size) state.selected.typeItem = [...state.selectedTypeItems][0];
      render();
    });
    const clearTypesSel = $("clearTypesSel");
    if (clearTypesSel) clearTypesSel.addEventListener("click", () => {
      state.selectedTypeItems = new Set();
      render();
    });
    const selectAllTypes = $("selectAllTypes");
    if (selectAllTypes) selectAllTypes.addEventListener("click", (ev) => {
      ev.stopPropagation();
      const ids = visibleTypeIndices();
      const allOn = ids.length > 0 && ids.every((i) => state.selectedTypeItems.has(i));
      state.selectedTypeItems = allOn ? new Set() : new Set(ids);
      render();
    });
    const bulkTypesApply = $("bulkTypesApply");
    if (bulkTypesApply) bulkTypesApply.addEventListener("click", applyTypesBulk);
    const disableTypes = $("disableTypes");
    if (disableTypes) disableTypes.addEventListener("click", disableSelectedTypes);
    const disableTypesBulk = $("disableTypesBulk");
    if (disableTypesBulk) disableTypesBulk.addEventListener("click", disableSelectedTypes);
    const copyTypeName = $("copyTypeName");
    if (copyTypeName) copyTypeName.addEventListener("click", copySelectedTypeNames);
    const removeType = $("removeType");
    if (removeType) removeType.addEventListener("click", deleteSelectedTypes);
    document.querySelectorAll("[data-type-item]").forEach((row) => row.addEventListener("click", (ev) => {
      flushOpenEditors();
      const idx = Number(row.dataset.typeItem);
      const onCheck = ev.target.closest("input[type='checkbox']");
      const additive = ev.ctrlKey || ev.metaKey || Boolean(onCheck);
      if (ev.shiftKey) {
        const visible = [...document.querySelectorAll("[data-type-item]")].map((r) => Number(r.dataset.typeItem));
        const from = visible.indexOf(state.lastClickedType);
        const to = visible.indexOf(idx);
        const lo = Math.min(from, to);
        const hi = Math.max(from, to);
        const range = (from >= 0 && to >= 0) ? visible.slice(lo, hi + 1) : [idx];
        if (additive) range.forEach((i) => state.selectedTypeItems.add(i));
        else state.selectedTypeItems = new Set(range);
      } else if (additive) {
        if (state.selectedTypeItems.has(idx)) state.selectedTypeItems.delete(idx);
        else state.selectedTypeItems.add(idx);
        state.lastClickedType = idx;
      } else {
        state.selectedTypeItems = new Set([idx]);
        state.lastClickedType = idx;
      }
      state.selected.typeItem = idx;
      render();
    }));
    document.querySelectorAll("[data-type-item]").forEach((row) => row.addEventListener("contextmenu", (ev) => {
      ev.preventDefault();
      flushOpenEditors();
      const idx = Number(row.dataset.typeItem);
      if (!state.selectedTypeItems.has(idx)) {
        state.selectedTypeItems = new Set([idx]);
        state.selected.typeItem = idx;
        state.lastClickedType = idx;
        document.querySelectorAll("[data-type-item]").forEach((r) => {
          r.classList.toggle("selected", Number(r.dataset.typeItem) === idx);
          r.classList.toggle("picked", Number(r.dataset.typeItem) === idx);
        });
      }
      const menu = $("ctxTypes");
      menu.classList.remove("hidden");
      menu.style.left = Math.min(ev.clientX, window.innerWidth - 240) + "px";
      menu.style.top = Math.min(ev.clientY, window.innerHeight - 90) + "px";
    }));
    ["tyName", "tyNom", "tyMin", "tyLife", "tyRestock"].forEach((id) => {
      const el = $(id);
      if (el) el.addEventListener("input", syncTypesEntryRow);
    });
  }

  function applyTypesCatalog(data) {
    if (!data || data.cancelled) return;
    state.types.folder = data.folder || "";
    state.types.files = data.files || [];
    state.types.types = data.types || [];
    state.types.error = data.error || "";
    if (data.error && !state.types.types.length) {
      setTicker(data.error);
    } else if (state.types.types.length) {
      setTicker(state.types.types.length + " types from mission (" + (data.fileCount || state.types.files.length) + " XML).");
    }
  }

  function usedClassNames() {
    const used = new Set();
    for (const file of (state.workspace && state.workspace.markets) || []) {
      for (const item of file.items || []) {
        const key = String(item.className || "").trim().toLowerCase();
        if (key) used.add(key);
        for (const variant of item.variants || []) {
          const name = String(variant || "").trim().toLowerCase();
          if (name) used.add(name);
        }
      }
    }
    return used;
  }

  function matchTypesQuery(hay, query) {
    const q = String(query || "").trim();
    if (!q) return true;
    const text = String(hay || "").toLowerCase();
    return q.split(/\s*\|\|\s*/).some((group) => group.split(/\s*&&\s*/).every((raw) => {
      let term = raw.trim();
      if (!term) return true;
      let neg = false;
      if (term.startsWith("!")) {
        neg = true;
        term = term.slice(1).trim();
      }
      if (!term) return true;
      const hit = text.includes(term.toLowerCase());
      return neg ? !hit : hit;
    }));
  }

  function wizardMode() {
    return state.types.mode || "items";
  }

  function setWizardChrome() {
    const listMode = wizardMode() !== "items";
    $("typesTitle").textContent = wizardMode() === "attachments"
      ? "EDIT ATTACHMENTS"
      : wizardMode() === "variants" ? "EDIT VARIANTS" : "ADD FROM TYPES";
    $("typesDestBox").classList.toggle("hidden", listMode);
    $("typesAddBlank").classList.toggle("hidden", listMode);
    $("typesHideUsedWrap").classList.toggle("hidden", listMode);
    $("typesAdd").textContent = listMode ? "Apply selected" : "Add selected";
    const help = $("typesAttachHint");
    if (help) help.classList.toggle("hidden", wizardMode() !== "attachments");
  }

  function attachCount(key) {
    const row = state.types.attachCounts && state.types.attachCounts.get(key);
    return row ? row.count : 0;
  }

  function attachTotal() {
    let n = 0;
    if (!state.types.attachCounts) return 0;
    for (const row of state.types.attachCounts.values()) n += row.count;
    return n;
  }

  function addAttach(name, delta) {
    const label = String(name || "").trim();
    const key = label.toLowerCase();
    if (!key || !delta) return;
    if (!state.types.attachCounts) state.types.attachCounts = new Map();
    const cur = state.types.attachCounts.get(key) || { name: label, count: 0 };
    cur.count = Math.max(0, cur.count + delta);
    if (cur.count === 0) state.types.attachCounts.delete(key);
    else state.types.attachCounts.set(key, cur);
  }

  function filteredTypes() {
    const used = usedClassNames();
    const hide = wizardMode() === "items" && ($("typesHideUsed") ? $("typesHideUsed").checked : state.types.hideUsed);
    return (state.types.types || []).filter((entry) => {
      const key = String(entry.name || "").toLowerCase();
      if (hide && used.has(key)) return false;
      return matchTypesQuery([entry.name, entry.category, entry.file].filter(Boolean).join(" "), state.types.filter);
    });
  }

  function renderTypesList() {
    const list = $("typesList");
    if (!list) return;
    const rows = filteredTypes();
    const cap = 600;
    const shown = rows.slice(0, cap);
    const used = usedClassNames();
    const attachMode = wizardMode() === "attachments";
    list.innerHTML = shown.map((entry, i) => {
      const key = String(entry.name).toLowerCase();
      const qty = attachMode ? attachCount(key) : 0;
      const picked = attachMode ? qty > 0 : state.types.selected.has(key);
      const mark = attachMode ? (qty ? "×" + qty : "") : (picked ? "●" : "○");
      const missing = attachMode && !used.has(key);
      return `<button type="button" class="types-row ${attachMode ? "att" : ""} ${picked ? "picked" : ""} ${used.has(key) ? "used" : ""} ${missing ? "missing" : ""}" data-type="${i}">
        <span class="att-qty">${mark}</span>
        <span>${escapeHtml(entry.name)}</span>
        <small>${missing ? "not in market" : escapeHtml(entry.category || "")}</small>
        <small>${escapeHtml(entry.file || "")}</small>
        ${attachMode ? `<span class="att-minus ${qty ? "" : "is-hidden"}" data-att-minus title="Remove one">−</span>` : ""}
      </button>`;
    }).join("") || "<p class='lede' style='padding:12px'>No types match. Pull types from the mission or change the filter.</p>";
    if (rows.length > cap) {
      list.insertAdjacentHTML("beforeend", `<p class="lede" style="padding:10px">Showing ${cap} of ${rows.length}. Narrow the filter to see the rest.</p>`);
    }
    $("typesCounts").textContent = rows.length + " shown · " +
      (attachMode
        ? (attachTotal() + " attachment(s)")
        : (state.types.selected.size + " selected")) +
      (state.types.files.length ? " · " + state.types.files.length + " XML" : "");
    $("typesMeta").textContent = state.types.folder
      ? (state.types.types.length + " classnames from " + state.types.folder + " (db/types.xml + cfgeconomycore.xml)")
      : (state.types.error || "Pull types from the mission db/types.xml and cfgeconomycore.xml on the server.");
    list.querySelectorAll("[data-type]").forEach((btn) => {
      btn.addEventListener("click", (ev) => {
        const i = Number(btn.dataset.type);
        const entry = shown[i];
        if (!entry) return;
        const key = String(entry.name).toLowerCase();
        if (attachMode && ev.target.closest("[data-att-minus]")) {
          addAttach(entry.name, -1);
        } else if (attachMode) {
          if (ev.shiftKey) {
            const from = Math.min(state.types.lastClicked, i);
            const to = Math.max(state.types.lastClicked, i);
            for (let n = from; n <= to; n += 1) addAttach(shown[n].name, 1);
          } else {
            addAttach(entry.name, 1);
          }
        } else if (ev.shiftKey) {
          const from = Math.min(state.types.lastClicked, i);
          const to = Math.max(state.types.lastClicked, i);
          for (let n = from; n <= to; n += 1) state.types.selected.add(String(shown[n].name).toLowerCase());
        } else if (state.types.selected.has(key)) state.types.selected.delete(key);
        else state.types.selected.add(key);
        state.types.lastClicked = i;
        renderTypesList();
      });
    });
  }

  function fillTypesDest() {
    const sel = $("typesDestFile");
    if (!sel) return;
    const files = (state.workspace && state.workspace.markets) || [];
    sel.innerHTML = files.map((f) => `<option value="${escapeHtml(f.filename)}" ${f.filename === state.selected.market ? "selected" : ""}>${escapeHtml(f.filename)}</option>`).join("");
  }

  async function importTypesFolder() {
    if (!state.workspace) {
      toast("Connect first. Types are pulled from the mission folder on the server.", true);
      return;
    }
    const dirtyTypes = ((state.workspace.dirty || []).some((key) => String(key).startsWith("Types/")));
    if (dirtyTypes && !await confirmModal("Re-pull types from the server?", "Saved local types changes will be overwritten by the remote XML.")) return;
    try {
      showProgress("Pulling types from mission…", 8);
      const ws = await api("pullTypes", {});
      hideProgress();
      applyWorkspace(ws);
      if (ws.types && ws.types.error && !(ws.types.types || []).length) {
        toast(ws.types.error, true);
        return;
      }
      if (!$("typesWizard").classList.contains("hidden")) {
        $("typesWizard").classList.remove("hidden");
        renderTypesList();
      }
      const n = (ws.types && (ws.types.typeCount || (ws.types.types || []).length)) || 0;
      toast("Pulled " + n + " types from " + ((ws.types && ws.types.fileCount) || 0) + " mission file(s).");
    } catch (err) {
      hideProgress();
      toast(err.message, true);
    }
  }

  async function openTypesWizard(mode) {
    if (!state.workspace) {
      toast("Connect and pull Market files first.", true);
      return;
    }
    flushOpenEditors();
    if (mode) state.types.mode = mode;
    if (wizardMode() === "items") fillTypesDest();
    setWizardChrome();
    $("typesWizard").classList.remove("hidden");
    $("typesFilter").value = state.types.filter;
    $("typesHideUsed").checked = state.types.hideUsed;
    if (!state.types.types.length) {
      try { applyTypesCatalog(await api("getTypesCatalog")); } catch { /* empty catalog */ }
    }
    renderTypesList();
    if (!state.types.types.length) await importTypesFolder();
    $("typesFilter").focus();
  }

  const COLOR_CAMO_SUFFIXES = [
    "multicam_tropic", "multicam_black", "multicamblack", "dark_woodland", "darkwoodland",
    "ranger_green", "tiger_stripe", "tigerstripe", "flecktarn", "multicam", "woodland",
    "marpat", "cadpat", "alpine", "atacs", "erdl", "ucp", "aor1", "aor2", "aor",
    "desert", "urban", "winter", "summer", "autumn", "spring", "tropic", "tropical",
    "camouflage", "camo", "digital", "kryptek", "tiger", "hex",
    "black", "white", "red", "blue", "green", "yellow", "orange", "pink", "purple", "violet",
    "brown", "grey", "gray", "tan", "khaki", "olive", "navy", "gold", "silver", "cyan", "teal",
    "beige", "charcoal", "coyote", "fde", "odg", "od", "snow", "sand", "mc", "blk"
  ].sort((a, b) => b.length - a.length);

  function variantFilterPrefill(className) {
    let name = String(className || "").trim();
    if (!name) return "";
    const original = name;
    let changed = true;
    while (changed) {
      changed = false;
      const lower = name.toLowerCase();
      for (const suffix of COLOR_CAMO_SUFFIXES) {
        const tail = "_" + suffix;
        if (lower.endsWith(tail) && name.length > tail.length) {
          name = name.slice(0, -tail.length);
          changed = true;
          break;
        }
      }
    }
    return name || original;
  }

  async function openListEditor(mode) {
    flushOpenEditors();
    const file = selectedMarket();
    const item = file && file.items[state.selected.item];
    if (!item) {
      toast("Select an item first.", true);
      return;
    }
    state.types.filter = mode === "variants" ? variantFilterPrefill(item.className) : "";
    state.types.selected = new Set();
    state.types.attachCounts = new Map();
    const current = mode === "attachments" ? (item.spawnAttachments || []) : (item.variants || []);
    for (const name of current) {
      if (!String(name || "").trim()) continue;
      if (mode === "attachments") addAttach(name, 1);
      else state.types.selected.add(String(name).trim().toLowerCase());
    }
    await openTypesWizard(mode);
    if (mode === "variants" && $("typesFilter")) {
      $("typesFilter").select();
    }
  }

  function closeTypesWizard() {
    $("typesWizard").classList.add("hidden");
    state.types.mode = "items";
    state.types.selected = new Set();
    state.types.attachCounts = new Map();
  }

  function applyListSelection() {
    flushOpenEditors();
    const file = selectedMarket();
    const item = file && file.items[state.selected.item];
    if (!item) {
      toast("Select an item first.", true);
      return;
    }
    const byKey = new Map(state.types.types.map((t) => [String(t.name).toLowerCase(), t]));
    const mode = wizardMode();
    const current = mode === "attachments" ? (item.spawnAttachments || []) : (item.variants || []);
    const names = [];
    if (mode === "attachments") {
      const leftover = new Map();
      for (const [key, row] of (state.types.attachCounts || new Map())) {
        leftover.set(key, { name: row.name, count: row.count });
      }
      for (const name of current) {
        const key = String(name || "").trim().toLowerCase();
        const row = leftover.get(key);
        if (!row || row.count <= 0) continue;
        names.push(name);
        row.count -= 1;
        if (row.count <= 0) leftover.delete(key);
      }
      for (const [key, row] of leftover) {
        const entry = byKey.get(key);
        const label = entry ? entry.name : row.name;
        for (let i = 0; i < row.count; i += 1) names.push(label);
      }
      item.spawnAttachments = names;
      closeTypesWizard();
      render();
      toast("Updated attachments. Press Save then lint.");
      return;
    } else {
      const seen = new Set();
      for (const name of current) {
        const key = String(name || "").trim().toLowerCase();
        if (!key || !state.types.selected.has(key) || seen.has(key)) continue;
        names.push(name);
        seen.add(key);
      }
      for (const key of state.types.selected) {
        if (seen.has(key)) continue;
        const entry = byKey.get(key);
        names.push(entry ? entry.name : key);
        seen.add(key);
      }
      item.variants = names;
    }
    closeTypesWizard();
    render();
    toast("Updated " + mode + ". Press Save then lint.");
  }

  function defaultMarketItem(className, template) {
    return {
      className,
      minPriceThreshold: template ? template.minPriceThreshold : 100,
      maxPriceThreshold: template ? template.maxPriceThreshold : 200,
      sellPricePercent: template ? template.sellPricePercent : -1,
      minStockThreshold: template ? template.minStockThreshold : 1,
      maxStockThreshold: template ? template.maxStockThreshold : 100,
      quantityPercent: template ? template.quantityPercent : -1,
      spawnAttachments: [],
      variants: []
    };
  }

  function addBlankMarketItem() {
    const file = selectedMarket();
    if (!file) return;
    file.items.push(defaultMarketItem("NewItem", file.items[file.items.length - 1]));
    state.selected.item = file.items.length - 1;
    closeTypesWizard();
    render();
  }

  async function addSelectedTypes() {
    if (wizardMode() !== "items") {
      applyListSelection();
      return;
    }
    const names = [...state.types.selected];
    if (!names.length) {
      toast("Select one or more classnames first.", true);
      return;
    }
    const destMode = (document.querySelector("input[name='typesDest']:checked") || {}).value || "current";
    let destName = state.selected.market || (selectedMarket() && selectedMarket().filename);
    if (destMode === "existing") destName = $("typesDestFile").value;
    if (destMode === "new") {
      const raw = $("typesNewName").value.trim();
      if (!raw) {
        toast("Name the new market file.", true);
        return;
      }
      try {
        const ws = await api("createFile", { kind: "Market", filename: raw });
        applyWorkspace(ws);
        destName = (ws.created && ws.created.filename) || JsonName(raw);
        state.selected.market = destName;
      } catch (err) {
        toast(err.message, true);
        return;
      }
    }
    const file = ((state.workspace && state.workspace.markets) || [])
      .find((f) => String(f.filename).toLowerCase() === String(destName || "").toLowerCase());
    if (!file) {
      toast("Pick a destination market file.", true);
      return;
    }
    const used = usedClassNames();
    const template = file.items[file.items.length - 1];
    const byKey = new Map(state.types.types.map((t) => [String(t.name).toLowerCase(), t]));
    let added = 0;
    let skipped = 0;
    for (const key of names) {
      const entry = byKey.get(key);
      const className = entry ? entry.name : key;
      const lower = String(className).toLowerCase();
      if (used.has(lower)) {
        skipped += 1;
        continue;
      }
      file.items.push(defaultMarketItem(className, template));
      used.add(lower);
      added += 1;
    }
    state.selected.market = file.filename;
    state.selected.item = Math.max(0, file.items.length - 1);
    closeTypesWizard();
    render();
    toast(added
      ? ("Added " + added + " item(s) to " + file.filename + ". Press Save then lint." + (skipped ? " Skipped " + skipped + " already in market or variants." : ""))
      : ("Nothing added." + (skipped ? " All selected were already in market or variants." : "")), !added);
  }

  function JsonName(name) {
    let cleaned = String(name).replace(/[^A-Za-z0-9_-]+/g, "_");
    if (!cleaned.toLowerCase().endsWith(".json")) cleaned += ".json";
    return cleaned;
  }

  function TypesName(name) {
    let cleaned = String(name || "").replace(/\\/g, "/").replace(/[^A-Za-z0-9_./-]+/g, "_");
    cleaned = cleaned.replace(/^\/+/, "").replace(/\.\./g, "");
    if (!cleaned.toLowerCase().endsWith(".xml")) cleaned += ".xml";
    return cleaned;
  }

  document.querySelectorAll(".rail-btn[data-view]").forEach((btn) => {
    btn.addEventListener("click", async () => {
      if (btn.disabled) return;
      flushOpenEditors();
      state.view = btn.dataset.view;
      if (state.view === "backups") {
        try { state.backups = (await api("listBackups")).backups || []; } catch { state.backups = []; }
      }
      render();
    });
  });
  $("uploadBtn").addEventListener("click", requestUpload);
  $("validateBtn").addEventListener("click", async () => {
    try {
      const data = await api("validateAll");
      if (state.workspace) state.workspace.issues = data.issues || [];
      openDrawer(data.issues || []);
    } catch (err) { toast(err.message, true); }
  });
  $("drawerClose").addEventListener("click", () => $("drawer").classList.remove("open"));
  $("ctx").addEventListener("click", (ev) => {
    const action = ev.target && ev.target.dataset ? ev.target.dataset.ctx : "";
    if (action === "variant") openVariantParentPicker();
    if (action === "delete") deleteSelectedMarketItems();
    if (action === "infinite") setInfiniteStock();
    if (action === "dupes") removeDuplicateClassnames();
  });
  $("ctxTypes").addEventListener("click", (ev) => {
    const action = ev.target && ev.target.dataset ? ev.target.dataset.ctxType : "";
    hideCtx();
    if (action === "dup") duplicateSelectedTypes();
    if (action === "disable") disableSelectedTypes();
    if (action === "copy") copySelectedTypeNames();
    if (action === "delete") deleteSelectedTypes();
  });
  $("addMissingCancel").addEventListener("click", closeAddMissing);
  $("addMissingOk").addEventListener("click", confirmAddMissing);
  $("variantPickCancel").addEventListener("click", closeVariantPick);
  $("variantPickFilter").addEventListener("input", renderVariantPickList);
  document.addEventListener("click", (ev) => {
    if (!$("ctx").contains(ev.target) && !($("ctxTypes") && $("ctxTypes").contains(ev.target))) hideCtx();
  });
  $("typesPickFolder").addEventListener("click", importTypesFolder);
  $("typesCancel").addEventListener("click", closeTypesWizard);
  $("typesAdd").addEventListener("click", () => addSelectedTypes());
  $("typesAddBlank").addEventListener("click", addBlankMarketItem);
  $("typesFilter").addEventListener("input", (ev) => {
    state.types.filter = ev.target.value;
    renderTypesList();
  });
  $("typesHideUsed").addEventListener("change", (ev) => {
    state.types.hideUsed = ev.target.checked;
    renderTypesList();
  });
  $("typesSelectVisible").addEventListener("click", () => {
    if (wizardMode() === "attachments") {
      for (const entry of filteredTypes()) addAttach(entry.name, 1);
    } else {
      for (const entry of filteredTypes()) state.types.selected.add(String(entry.name).toLowerCase());
    }
    renderTypesList();
  });
  $("typesClearSel").addEventListener("click", () => {
    state.types.selected = new Set();
    state.types.attachCounts = new Map();
    renderTypesList();
  });
  $("typesDestFile").addEventListener("focus", () => {
    const existing = document.querySelector("input[name='typesDest'][value='existing']");
    if (existing) existing.checked = true;
  });
  $("typesNewName").addEventListener("focus", () => {
    const neu = document.querySelector("input[name='typesDest'][value='new']");
    if (neu) neu.checked = true;
  });

  $("iconSearch").addEventListener("input", (ev) => {
    const needle = ev.target.value.trim().toLowerCase();
    document.querySelectorAll("#iconGrid .icon-opt").forEach((opt) => {
      const name = (opt.dataset.icon || "").toLowerCase();
      opt.classList.toggle("is-hidden", Boolean(needle) && !name.includes(needle));
    });
  });
  $("iconGrid").addEventListener("click", (ev) => {
    const opt = ev.target.closest("[data-icon]");
    if (opt) applyIconChoice(opt.dataset.icon);
  });
  document.addEventListener("mousedown", (ev) => {
    const menu = $("iconMenu");
    if (!menu || menu.classList.contains("hidden")) return;
    if (menu.contains(ev.target) || ev.target.closest(".icon-pick-btn")) return;
    closeIconMenu();
  });
  window.addEventListener("resize", closeIconMenu);

  document.addEventListener("keydown", (ev) => {
    if (ev.key === "Escape" && !$("iconMenu").classList.contains("hidden")) {
      closeIconMenu();
      return;
    }
    if (ev.key === "Escape" && !$("addMissing").classList.contains("hidden")) {
      closeAddMissing();
      return;
    }
    if (ev.key === "Escape" && !$("variantPick").classList.contains("hidden")) {
      closeVariantPick();
      return;
    }
    if (ev.key === "Escape" && !$("typesWizard").classList.contains("hidden")) {
      closeTypesWizard();
      return;
    }
    if (!state.workspace) return;
    if (!$("typesWizard").classList.contains("hidden")) return;
    const tag = (ev.target && ev.target.tagName) || "";
    if (tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT") return;
    if (state.view === "types") {
      if ((ev.ctrlKey || ev.metaKey) && (ev.key === "a" || ev.key === "A")) {
        ev.preventDefault();
        state.selectedTypeItems = new Set(visibleTypeIndices());
        render();
        return;
      }
      if (ev.key === "Delete" || ev.key === "Backspace") {
        ev.preventDefault();
        deleteSelectedTypes();
      }
      return;
    }
    if (state.view !== "market") return;
    if ((ev.ctrlKey || ev.metaKey) && (ev.key === "a" || ev.key === "A")) {
      ev.preventDefault();
      selectVisibleMarketItems();
      return;
    }
    if (ev.key === "Delete" || ev.key === "Backspace") {
      ev.preventDefault();
      deleteSelectedMarketItems();
    }
  });
  $("browserCancel").addEventListener("click", () => $("browser").classList.add("hidden"));
  $("browserUp").addEventListener("click", async () => {
    try { await loadBrowser(state.browser.parent || ""); } catch (err) { hideProgress(); toast(err.message, true); }
  });
  $("useMarket").addEventListener("click", () => { state.browser.picks.market = currentBrowserFolder(); updatePickLabels(); });
  $("useTraders").addEventListener("click", () => { state.browser.picks.traders = currentBrowserFolder(); updatePickLabels(); });
  $("useZones").addEventListener("click", () => { state.browser.picks.zones = currentBrowserFolder(); updatePickLabels(); });
  $("browserAuto").addEventListener("click", async () => {
    const s = state.browser.suggestions || {};
    if (s.expansionMod && !s.market && !s.traders) {
      try { await loadBrowser(s.expansionMod); } catch (err) { hideProgress(); toast(err.message, true); }
      return;
    }
    if (s.market) state.browser.picks.market = s.market;
    if (s.traders) state.browser.picks.traders = s.traders;
    if (s.zones) state.browser.picks.zones = s.zones;
    if (!s.market && !s.traders && !s.zones) {
      toast("No Market / Traders / TraderZones folders in this directory. Open ExpansionMod or the mission folder.", true);
    }
    updatePickLabels();
  });
  $("browserApply").addEventListener("click", applyBrowserPaths);

  async function boot() {
    try {
      const data = await api("listProfiles");
      state.profiles = data.profiles || [];
    } catch (err) {
      toast(err.message, true);
    }
    try {
      applyTypesCatalog(await api("getTypesCatalog"));
    } catch { /* first run has no catalog */ }
    render();
  }

  boot();
})();
