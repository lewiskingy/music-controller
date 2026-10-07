// Music Assistant WebSocket adapter. No credentials are embedded in the release.
export class MusicAssistantClient {
  constructor(path = "./ma/ws") {
    this.path = path;
    this.socket = null;
    this.pending = new Map();
    this.sequence = 0;
    this.onEvent = () => {};
    this.onDisconnect = () => {};
    this.onReady = () => {};
  }
  async connect() {
    const url = new URL(this.path, window.location.href);
    url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
    await new Promise((resolve, reject) => {
      const ws = new WebSocket(url);
      this.socket = ws;
      const timeout = setTimeout(() => { ws.close(); reject(new Error("Connection timed out")); }, 10000);
      ws.onopen = () => { clearTimeout(timeout); resolve(); };
      ws.onerror = () => { clearTimeout(timeout); reject(new Error("Music Assistant connection failed")); };
      ws.onclose = () => {
        clearTimeout(timeout);
        for (const entry of this.pending.values()) entry.reject(new Error("Disconnected"));
        this.pending.clear();
        this.onDisconnect();
      };
      ws.onmessage = (event) => {
        let msg;
        try { msg = JSON.parse(event.data); } catch { return; }
        if (msg.message_id !== undefined && this.pending.has(String(msg.message_id))) {
          const entry = this.pending.get(String(msg.message_id));
          this.pending.delete(String(msg.message_id));
          clearTimeout(entry.timeout);
          if (msg.error || msg.error_code) {
            const error = typeof msg.error === "string" ? msg.error
              : (msg.error?.message || msg.error?.code || msg.error_code || "API error");
            entry.reject(new Error(String(error).slice(0, 160)));
          }
          else entry.resolve(msg.result);
        } else if (msg.event === "gateway/ready") this.onReady();
        else if (msg.event) this.onEvent(msg);
      };
    });
  }
  command(command, args = {}) {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
      return Promise.reject(new Error("Not connected"));
    }
    const message_id = String(++this.sequence);
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => { this.pending.delete(message_id); reject(new Error("API request timed out")); }, 12000);
      this.pending.set(message_id, { resolve, reject, timeout });
      this.socket.send(JSON.stringify({ message_id, command, args }));
    });
  }
  async players() { return this.command("players/all"); }
  async queues() { return this.command("player_queues/all"); }
  queueItems(queue_id, offset = 0, limit = 50) { return this.command("player_queues/items", {queue_id, offset, limit}); }
  transport(action, player_id) {
    if (!["play", "pause", "stop", "next", "previous"].includes(action)) throw new Error("Unsupported transport");
    return this.command("players/cmd/" + action, {player_id});
  }
  volume(player_id, volume_level) { return this.command("players/cmd/volume_set", {player_id, volume_level}); }
  playIndex(queue_id, index) { return this.command("player_queues/play_index", {queue_id, index}); }
  browse(type, offset = 0, limit = 50) {
    if (!["albums", "artists", "playlists"].includes(type)) throw new Error("Unsupported catalogue");
    return this.command("music/" + type, {offset, limit});
  }
  albumTracks(item_id, provider_instance_id_or_domain) {
    return this.command("music/album_tracks", {item_id, provider_instance_id_or_domain});
  }
  playMedia(queue_id, media, option = "replace") {
    return this.command("player_queues/play_media", {queue_id, media, option});
  }
  close() { this.socket?.close(); }
}
