// CardPainter: 把「卡面模型 + 一筆資料」畫出來。
// 編輯器預覽 (scene 單位 10px/mm) 與列印/匯出 (dpi/25.4 px/mm) 共用同一個繪製函式。
#pragma once
#include <QImage>
#include <QRectF>
#include "CardLayout.h"

class QPainter;

namespace card {

	// 在 painter 上、rectPx 這個矩形內畫出一個元素。pxPerMm 用來換算字型大小。
	void paintElement(QPainter& p, const Element& e, const QRectF& rectPx, double pxPerMm, const Record& rec);

	// 畫卡面底圖 (鋪滿 rectPx)。不看 print 旗標，由呼叫端決定要不要畫：
	//   設計畫面：showInDesigner 時畫；renderCard (預覽輸出 / 列印)：print 為 true 時才畫。
	void paintBackground(QPainter& p, const CardBackground& bg, const QRectF& rectPx, double pxPerMm, const Record& rec);

	// 把整張卡渲染成影像 (dpi 例如 300 / 600)。階段 3 列印時會用它。
	QImage renderCard(const CardLayout& layout, const Record& rec, int dpi);

	// 貼合內容 (Element::fitContent)：依「這一筆資料」把元素的框調成剛好包住內容。
	//   文字：寬 = 文字的實際寬度 (不換行)，高 = 行數 x 行高；
	//         依對齊方式固定錨點 (靠左固定左邊、置中固定中心、靠右固定右邊)，底邊固定不動。
	//   圖片：寬度不變，高度 = 寬度 x 圖片長寬比；底邊固定不動 (往上長高 / 縮短)。
	//         找不到圖片時不改變。
	// 回傳框是否有改變。預覽 (編輯器) 與列印 (每一筆資料) 都用同一個函式，所以兩邊一致。
	bool fitElement(Element& e, const Record& rec);

	// 對版面中所有 fitContent 的元素套用 fitElement
	void fitAll(CardLayout& layout, const Record& rec);

	// 圖片有快取；檔案內容改變後可呼叫此函式清除
	void clearImageCache();

} // namespace card