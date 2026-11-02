#include "TopcoatSpec.h"

#include <cmath>
#include <cstdio>

namespace topcoat {

    std::wstring formatMm(double v) {
        if (std::fabs(v) < 0.005) v = 0;                     // 避免 "-0"
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f", v);         // C 地區的 '.'，且不使用 iostream 的 locale
        std::string s(buf);
        if (s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.pop_back();
        }
        return std::wstring(s.begin(), s.end());
    }

    std::wstring escape(const wchar_t* code, const std::vector<TopcoatRect>& rects) {
        if (rects.empty()) return std::wstring();
        std::wstring s = L"~";
        s += code;
        s += L"%";
        for (size_t i = 0; i < rects.size(); ++i) {
            if (i) s += L";";
            const TopcoatRect& r = rects[i];
            s += formatMm(r.topMm) + L" " + formatMm(r.leftMm) + L" " + formatMm(r.widthMm) + L" " + formatMm(r.heightMm);
        }
        s += L"?";
        return s;
    }

    std::vector<std::wstring> escapes(const TopcoatSpec& spec) {
        std::vector<std::wstring> out;
        for (const auto& e : { escape(L"TA", spec.addAreas), escape(L"TR", spec.removeAreas), escape(L"PB", spec.blockAreas) })
            if (!e.empty()) out.push_back(e);
        return out;
    }

    std::wstring frontTicketXml(const TopcoatSpec& spec) {
        switch (spec.base) {
        case TopcoatSpec::Base::DriverDefault:
            return std::wstring();
        case TopcoatSpec::Base::All:
            return L"<psf:Feature name=\"ns0000:PageFrontPrintAndTopcoat\"><psf:Option name=\"ns0000:All\" /></psf:Feature>";
        case TopcoatSpec::Base::Except:
            break;
        }
        std::wstring x = L"<psf:Feature name=\"ns0000:PageFrontPrintAndTopcoat\"><psf:Option name=\"ns0000:Except\" /></psf:Feature>";
        const wchar_t* opt = nullptr;
        switch (spec.exception) {
        case TopcoatSpec::Exception::Chip7816: opt = L"ns0000:ISO_7816_ContactedSmartCard"; break;
        case TopcoatSpec::Exception::MagJis:   opt = L"ns0000:JIS_SingleTrackMagneticStripe"; break;
        case TopcoatSpec::Exception::MagIso2:  opt = L"ns0000:ISO2_TrackMagneticStripe"; break;
        case TopcoatSpec::Exception::MagIso3:  opt = L"ns0000:ISO3_TrackMagneticStripe"; break;
        case TopcoatSpec::Exception::None:     break;
        }
        if (opt) x += std::wstring(L"<psf:Feature name=\"ns0000:PageFrontTopcoatException\"><psf:Option name=\"") + opt + L"\" /></psf:Feature>";
        return x;
    }

    // 直式設計座標 (x 向右、y 向下，寬 W、高 H)：
    //   順時針轉 90°：點 (x, y) -> (H - y, x)    ；矩形 left' = H - (top + h)、top' = left、w' = h、h' = w
    //   逆時針轉 90°：點 (x, y) -> (y, W - x)    ；矩形 left' = top、top' = W - (left + w)、w' = h、h' = w
    TopcoatRect rotatePortraitToLandscape(const TopcoatRect& r, double W, double H, bool clockwise) {
        TopcoatRect o;
        o.widthMm = r.heightMm;
        o.heightMm = r.widthMm;
        if (clockwise) {
            o.leftMm = H - (r.topMm + r.heightMm);
            o.topMm = r.leftMm;
        }
        else {
            o.leftMm = r.topMm;
            o.topMm = W - (r.leftMm + r.widthMm);
        }
        return o;
    }

    TopcoatSpec rotatePortraitToLandscape(const TopcoatSpec& s, double W, double H, bool clockwise) {
        TopcoatSpec o = s;
        for (auto* v : { &o.addAreas, &o.removeAreas, &o.blockAreas })
            for (auto& r : *v) r = rotatePortraitToLandscape(r, W, H, clockwise);
        return o;
    }

    std::wstring describeBase(const TopcoatSpec& spec) {
        std::wstring s;
        switch (spec.base) {
        case TopcoatSpec::Base::DriverDefault: s = L"沿用驅動程式設定"; break;
        case TopcoatSpec::Base::All: s = L"全卡鍍膜 (PrintTicket)"; break;
        case TopcoatSpec::Base::Except: {
            s = L"全卡鍍膜，避開";
            switch (spec.exception) {
            case TopcoatSpec::Exception::Chip7816: s += L"晶片 ISO 7816"; break;
            case TopcoatSpec::Exception::MagJis:   s += L"磁條 JIS"; break;
            case TopcoatSpec::Exception::MagIso2:  s += L"磁條 ISO 2 軌"; break;
            case TopcoatSpec::Exception::MagIso3:  s += L"磁條 ISO 3 軌"; break;
            default: s += L"(未指定)"; break;
            }
            s += L" (PrintTicket)";
            break;
        }
        }
        return s;
    }

    std::wstring describe(const TopcoatSpec& spec) {
        std::wstring s = describeBase(spec);
        for (const auto& e : escapes(spec)) s += L"；" + e;
        return s;
    }

} // namespace topcoat