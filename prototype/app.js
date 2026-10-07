import { MusicAssistantClient } from "./music-assistant.js";

const $ = (id) => document.getElementById(id);
const client = new MusicAssistantClient("./ma/ws");
let players = [], queues = [], selected = "", authenticated = false, refreshTimer = null;
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
  $("volume").value = player?.volume_level ?? 0;
  $("volumeValue").textContent = (player?.volume_level ?? 0) + "%";
  $("toggle").textContent = player?.state === "playing" ? "⏸" : "▶";

  // This milestone is read-only; do not offer controls until transport semantics
  // are verified (particularly Sonos pause behaviour in Atlas #587).
  for (const id of ["prev", "toggle", "next", "volume"]) $(id).disabled = true;
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
  $("login").hidden = false;
  setStatus("Disconnected. Reload to reconnect.");
};
$("player").addEventListener("change", (event) => {
  selected = event.target.value;
  localStorage.setItem("music-controller-player", selected);
  updateView();
});
$("login").addEventListener("submit", async (event) => {
  event.preventDefault();
  const button = $("login").querySelector("button");
  button.disabled = true;
  setStatus("Signing in…");
  try {
    await client.login($("username").value, $("password").value);
    $("password").value = "";
    authenticated = true;
    await refresh();
    $("login").hidden = true;
    setStatus("Live Music Assistant connection · Read-only Now Playing");
  } catch (error) {
    setStatus("Unable to connect: " + error.message);
  } finally { button.disabled = false; }
});
document.querySelectorAll("nav button").forEach((button) => {
  button.addEventListener("click", () => setStatus(button.dataset.view + " will follow in a later milestone."));
});
(async () => {
  try {
    await client.connect();
    setStatus("Connected to Music Assistant. Sign in to view players.");
  } catch (error) { setStatus("Connection failed: " + error.message); }
})();
