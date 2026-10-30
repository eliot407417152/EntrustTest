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

	// 把整張卡渲染成影像 (dpi 例如 300 / 600)。階段 3 列印時會用它。
	QImage renderCard(const CardLayout& layout, const Record& rec, int dpi);

	// 圖片有快取；檔案內容改變後可呼叫此函式清除
	void clearImageCache();

} // namespace card