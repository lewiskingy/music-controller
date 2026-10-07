// Music Assistant WebSocket adapter. No credentials are embedded in the release.
export class MusicAssistantClient {
  constructor(path = "./ma/ws") {
    this.path = path;
    this.socket = null;
    this.pending = new Map();
    this.sequence = 0;
    this.onEvent = () => {};
    this.onDisconnect = () => {};
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
          if (msg.error || msg.error_code) entry.reject(new Error(msg.error?.message || msg.error || "API error"));
          else entry.resolve(msg.result);
        } else if (msg.event) this.onEvent(msg);
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
  async login(username, password) {
    const response = await this.command("auth/login", { username, password, device_name: "Music Controller Prototype" });
    if (!response?.success || !response.access_token) throw new Error(response?.error || "Login failed");
    await this.command("auth", { token: response.access_token });
    return response.access_token;
  }
  async authenticate(token) { return this.command("auth", { token }); }
  async players() { return this.command("players/all"); }
  async queues() { return this.command("player_queues/all"); }
  close() { this.socket?.close(); }
}
