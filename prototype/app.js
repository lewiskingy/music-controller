import { MusicAssistantClient } from "./music-assistant.js";

const $ = (id) => document.getElementById(id);
const client = new MusicAssistantClient("./ma/ws");
let players = [], queues = [], selected = "", authenticated = false, refreshTimer = null;
let view = "Now Playing", category = "albums", page = 0, currentItems = [], browsingParent = null, busy = false;
const setStatus = (message) => { $("status").textContent = message; };
function selectedPlayer() { return players.find((player) => player.player_id === selected); }
function queueForPlayer(player) {
  return queues.find((queue) => queue.queue_id === (player?.active_group || player?.player_id)) ||
    queues.find((queue) => queue.queue_id === player?.player_id);
}
function updateView() {
  const player = selectedPlayer();
  const queue = queueForPlayer(player);
  const item = queue?.current_item?.media_item || player?.current_media;
  $("track").textContent = item?.name || player?.current_media?.title || (player ? "Nothing playing" : "Select a player");
  $("artist").textContent = item?.artists?.map((artist) => artist.name).join(", ") ||
    player?.current_media?.artist || "";
  $("album").textContent = item?.album?.name || player?.current_media?.album || "";
  const imagePath = item?.image?.path || item?.metadata?.images?.[0]?.path ||
    player?.current_media?.image_url || "";
  const art = $("art");
  art.replaceChildren();
  if (imagePath) {
    const img = document.createElement("img");
    img.alt = "Album artwork";
    const url = new URL("./ma/image", window.location.href);
    url.searchParams.set("path", imagePath);
    url.searchParams.set("size", "320");
    img.src = url.toString();
    img.onerror = () => { art.textContent = "♪"; };
    art.append(img);
  } else art.textContent = "♪";
  $("volume").value = player?.volume_level ?? 0;
  $("volumeValue").textContent = (player?.volume_level ?? 0) + "%";
  $("toggle").textContent = player?.state === "playing" ? "⏸" : "▶";

  const enabled = authenticated && player?.available !== false && !!player;
  for (const id of ["prev", "toggle", "next", "stop", "volume"]) $(id).disabled = !enabled || busy;
  // Known Sonos Pause queue hopping (#587): offer Stop instead of Pause on
  // native Sonos until a verified fix is available.
  const sonos = /sonos/i.test([player?.provider, player?.type, player?.device_info?.manufacturer].join(" "));
  $("toggle").disabled = !enabled || busy || (sonos && player?.state === "playing");
  $("toggle").title = sonos && player?.state === "playing" ? "Use Stop for Sonos until pause issue is resolved" : "";
  $("stop").disabled = !enabled || busy;
}
function renderPlayers() {
  const previous = selected;
  $("player").replaceChildren();
  for (const player of players.filter((p) => p.available !== false)) {
    const option = document.createElement("option");
    option.value = player.player_id;
    option.textContent = player.name;
    $("player").append(option);
  }
  const ids = Array.from($("player").options, (option) => option.value);
  selected = ids.includes(previous) ? previous : (ids.includes(localStorage.getItem("music-controller-player")) ? localStorage.getItem("music-controller-player") : $("player").options[0]?.value || "");
  $("player").value = selected;
  updateView();
}
async function refresh() {
  if (!authenticated) return;
  const [newPlayers, newQueues] = await Promise.all([client.players(), client.queues()]);
  players = Array.isArray(newPlayers) ? newPlayers : [];
  queues = Array.isArray(newQueues) ? newQueues : [];
  renderPlayers();
  if (view === "Queue") await showQueue();
}
function retryLater() {
  if (refreshTimer) clearTimeout(refreshTimer);
  refreshTimer = setTimeout(() => refresh().catch((error) => setStatus("Refresh failed: " + error.message)), 500);
}
client.onEvent = (event) => {
  if (/player|queue/i.test(event.event || "")) retryLater();
};
client.onDisconnect = () => {
  authenticated = false;
  setStatus("Disconnected. Reload to reconnect.");
};
$("player").addEventListener("change", (event) => {
  selected = event.target.value;
  localStorage.setItem("music-controller-player", selected);
  updateView();
});
async function runCommand(task) {
  if (!authenticated || busy) return;
  busy = true;
  updateView();
  try { await task(); await refresh(); setStatus("Music Assistant updated"); }
  catch (error) { setStatus("Command failed: " + error.message); }
  finally { busy = false; updateView(); }
}
$("toggle").addEventListener("click", () => {
  const player = selectedPlayer(); if (!player) return;
  runCommand(() => client.transport(player.state === "playing" ? "pause" : "play", selected));
});
$("stop").addEventListener("click", () => runCommand(() => client.transport("stop", selected)));
for (const [id, action] of [["prev", "previous"], ["next", "next"]]) {
  $(id).addEventListener("click", () => runCommand(() => client.transport(action, selected)));
}
$("volume").addEventListener("change", (event) => {
  runCommand(() => client.volume(selected, Number(event.target.value)));
});
function drawRows(items, onSelect, caption) {
  const target = $("panel-items"); target.replaceChildren();
  if (!items.length) { target.textContent = caption || "No items available"; return; }
  items.forEach((item, index) => {
    const row = document.createElement("button");
    row.type = "button"; row.className = "item-row";
    row.textContent = item.name || item.media_item?.name || item.media_item?.title || item.title || "Untitled";
    row.addEventListener("click", () => onSelect(item, index));
    target.append(row);
  });
}
async function showQueue() {
  const queue = queueForPlayer(selectedPlayer());
  if (view !== "Queue") return;
  $("panel-title").textContent = "Queue · " + (selectedPlayer()?.name || "");
  if (!queue) { drawRows([], () => {}, "No queue for this player"); return; }
  try {
    const result = await client.queueItems(queue.queue_id, 0, 100);
    const items = Array.isArray(result) ? result : (result?.items || []);
    if (view !== "Queue") return;
    drawRows(items, (item, index) => runCommand(() => client.playIndex(queue.queue_id, item.index ?? index)), "Queue empty");
  } catch (error) { setStatus("Queue unavailable: " + error.message); }
}
async function showBrowse(reset = true) {
  if (view !== "Browse") return;
  if (reset) { page = 0; currentItems = []; browsingParent = null; $("panel-items").scrollTop = 0; }
  $("browse-tabs").hidden = false;
  $("panel-title").textContent = browsingParent?.name || category[0].toUpperCase() + category.slice(1);
  try {
    const result = browsingParent
      ? await client.albumTracks(browsingParent.item_id, browsingParent.provider)
      : await client.browse(category, page * 50, 50);
    const items = Array.isArray(result) ? result : (result?.items || []);
    if (view !== "Browse") return;
    if (!browsingParent) currentItems.push(...items); else currentItems = items;
    drawRows(currentItems, async (item) => {
      if (category === "albums" && !browsingParent) {
        browsingParent = item; await showBrowse(false); return;
      }
      const queue = queueForPlayer(selectedPlayer());
      if (!queue) { setStatus("Select an available player first"); return; }
      const media = item.uri || item.media_item?.uri;
      if (!media) { setStatus("This item has no playable URI"); return; }
      await runCommand(() => client.playMedia(queue.queue_id, media));
    }, "No results");
    $("more").hidden = !!browsingParent || items.length < 50;
  } catch (error) { setStatus("Browse failed: " + error.message); }
}
$("more").addEventListener("click", async () => { page++; await showBrowse(false); });
$("panel-back").addEventListener("click", () => {
  if (browsingParent) { browsingParent = null; showBrowse(); }
  else switchView("Now Playing");
});
document.querySelectorAll("[data-category]").forEach(button => button.addEventListener("click", () => {
  category = button.dataset.category; showBrowse();
}));
function switchView(next) {
  view = next;
  const isHome = next === "Now Playing";
  $("now-playing").hidden = !isHome;
  $("panel").hidden = isHome;
  $("home").hidden = isHome;
  $("screen-label").textContent = next;
  $("browse-tabs").hidden = next !== "Browse";
  $("players-controls").hidden = next !== "Players";
  $("search-controls").hidden = next !== "Search";
  $("more").hidden = true;
  $("panel-items").replaceChildren();
  $("panel-items").scrollTop = 0;
  $("panel-title").textContent = next;
  if (next === "Queue") showQueue();
  else if (next === "Browse") showBrowse();
  else if (next === "Players") {
    const options = Array.from($("player").options);
    drawRows(options.map(option => ({name: option.textContent, player_id: option.value})), item => {
      $("player").value = item.player_id;
      $("player").dispatchEvent(new Event("change"));
      switchView("Now Playing");
    }, "No players available");
  } else if (next === "Search") {
    drawRows([], () => {}, "Search integration is next; no results yet.");
  }
}
$("home").addEventListener("click", () => switchView("Now Playing"));
document.querySelectorAll(".home-actions button").forEach(button => {
  button.addEventListener("click", () => switchView(button.dataset.view));
});
let reconnectDelay = 1000;
let reconnectTimer = null;
client.onReady = async () => {
  try {
    authenticated = true;
    await refresh();
    reconnectDelay = 1000;
    setStatus("Connected to Music Assistant");
  } catch (error) {
    setStatus("Unable to load player state: " + error.message);
  }
};
client.onDisconnect = () => {
  authenticated = false;
  setStatus("Music Assistant disconnected. Reconnecting…");
  clearTimeout(reconnectTimer);
  reconnectTimer = setTimeout(connect, reconnectDelay);
  reconnectDelay = Math.min(reconnectDelay * 2, 30000);
};
async function connect() {
  try { await client.connect(); }
  catch (error) {
    setStatus("Connection failed: " + error.message + ". Retrying…");
    clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(connect, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, 30000);
  }
}
connect();
