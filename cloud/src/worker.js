// StickS3 中控 — Worker 進入點
// 路由：/health 健康檢查、/ws 升級成 WebSocket 交給 Durable Object。
export { VoiceSession } from "./voice_session.js";

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === "/health") {
      return new Response("ok", { headers: { "content-type": "text/plain" } });
    }

    if (url.pathname === "/ws") {
      // B1：所有裝置先都連到同一個具名 DO（之後再按使用者分）
      const id = env.VOICE.idFromName("miles");
      const stub = env.VOICE.get(id);
      return stub.fetch(request);
    }

    return new Response("StickS3 hub is running. Connect to /ws", { status: 200 });
  },
};
