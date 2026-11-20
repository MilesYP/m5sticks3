// 簡轉繁（台灣用字）（B4 實機補，11-20）：GPT-Live 的使用者逐字稿有時是簡體（例：「刚刚」），
// 裝置的繁中點陣字型 efontTW_12 沒有簡體字形 → 顯示成方框。Miles 要求「盡量都以繁體中文顯示」：
// 使用者與Friday 的逐字稿都轉。用 OpenCC（詞組＋單字，cn → twp：台灣用字＋慣用詞，例：头发→頭髮、软件→軟體），
// 轉完把正式寫法「臺」換回日常的「台」。單字一對一表會轉錯（皇后→皇後、头发→頭發），所以不用。
// 套件：opencc-js 1.4.2（MIT；字典來自 OpenCC，Apache-2.0）。第一次呼叫建表約 60ms，之後每段約 0.02ms。
import * as OpenCC from "opencc-js/cn2t";

let conv = null;
export function toTw(s) {
  if (!s) return s;
  if (!conv) conv = OpenCC.Converter({ from: "cn", to: "twp" });
  return conv(s).replace(/臺/g, "台");
}
