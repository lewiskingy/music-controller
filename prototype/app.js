// Static UX foundation only. Live Music Assistant integration follows the API spike.
const tracks = [
  { title: "Tejano Blue", artist: "Cigarettes After Sex", album: "X's" },
  { title: "Wet Dream", artist: "Wet Leg", album: "Wet Leg" },
  { title: "The Killing Moon", artist: "Echo & the Bunnymen", album: "Ocean Rain" }
];
let index = 0;
let playing = true;
const byId = (id) => document.getElementById(id);
function render() {
  const track = tracks[index];
  byId("track").textContent = track.title;
  byId("artist").textContent = track.artist;
  byId("album").textContent = track.album;
  byId("toggle").textContent = playing ? "⏸" : "▶";
  byId("toggle").setAttribute("aria-label", playing ? "Pause" : "Play");
}
byId("toggle").addEventListener("click", () => {
  playing = !playing;
  render();
  byId("status").textContent = playing ? "Demo playback resumed (no audio)." : "Demo playback paused (no audio).";
});
for (const [id, step] of [["prev", -1], ["next", 1]]) {
  byId(id).addEventListener("click", () => {
    index = (index + step + tracks.length) % tracks.length;
    render();
    byId("status").textContent = "Sample track selected (no audio).";
  });
}
byId("volume").addEventListener("input", (event) => {
  byId("volumeValue").textContent = event.target.value + "%";
  byId("status").textContent = "Demo volume only — no player command sent.";
});
document.querySelectorAll("nav button").forEach((button) => {
  button.addEventListener("click", () => {
    document.querySelectorAll("nav button").forEach((item) => item.removeAttribute("aria-current"));
    button.setAttribute("aria-current", "page");
    byId("status").textContent = button.dataset.view === "Now Playing"
      ? "Demo Now Playing — sample data."
      : button.dataset.view + " is planned; no live data yet.";
  });
});
render();
