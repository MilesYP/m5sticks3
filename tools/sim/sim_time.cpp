// 回歸比對用：把 time() 固定在 2026-11-20 18:10 台北，資訊頁的秒數才不會每次不同
#include <time.h>
extern "C" time_t time(time_t* t) { const time_t v = 1795167000; if (t) *t = v; return v; }
