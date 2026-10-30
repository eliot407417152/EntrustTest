// CardPrinter.cpp
// 移植自 Datacard SDK 範例 print/print.cpp (精簡版)，並改成可由 GUI 呼叫的函式。
// 注意：這個檔案「不要」include 任何 Qt 標頭，避免與 windows.h / ATL 巨集衝突。
#include "stdafx.h"
#include <atlimage.h>
#include <bidispl.h>
#include <prntvpt.h>
#include <winspool.h>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <stdexcept>
#include <vector>
#include "DXP01SDK.H"
#include "util.h"
#include "CardPrinter.h"

using std::runtime_error;
using std::stringstream;

namespace {

    // ---------------------------------------------------------------- helpers ---

    // util.cpp 內的函式會 cout 輸出進度；這裡把 cout 導向 GUI 的 log。
    class LogStreamBuf : public std::streambuf {
    public:
        explicit LogStreamBuf(CardPrinter::LogFn fn) : fn_(std::move(fn)) {}
        ~LogStreamBuf() override { flushLine(); }
    protected:
        int_type overflow(int_type ch) override {
            if (ch == traits_type::eof()) return traits_type::not_eof(ch);
            if (ch == '\n') flushLine();
            else if (ch != '\r') line_.push_back(static_cast<char>(ch));
            return ch;
        }
    private:
        void flushLine() {
            if (line_.empty() || !fn_) { line_.clear(); return; }
            const int n = ::MultiByteToWideChar(CP_ACP, 0, line_.data(), (int)line_.size(), NULL, 0);
            std::wstring w(n, L'\0');
            ::MultiByteToWideChar(CP_ACP, 0, line_.data(), (int)line_.size(), &w[0], n);
            fn_(w);
            line_.clear();
        }
        CardPrinter::LogFn fn_;
        std::string line_;
    };

    class CoutRedirect {
    public:
        explicit CoutRedirect(const CardPrinter::LogFn& fn) : buf_(fn), old_(std::cout.rdbuf(&buf_)) {}
        ~CoutRedirect() { std::cout.flush(); std::cout.rdbuf(old_); }
    private:
        LogStreamBuf buf_;
        std::streambuf* old_;
    };

    struct ComInit {
        HRESULT hr;
        ComInit() : hr(::CoInitialize(NULL)) {}
        ~ComInit() { if (SUCCEEDED(hr)) ::CoUninitialize(); }
    };

    template <class F>
    struct ScopeExit {
        F f;
        explicit ScopeExit(F fn) : f(std::move(fn)) {}
        ~ScopeExit() { f(); }
    };
    template <class F> ScopeExit<F> onExit(F f) { return ScopeExit<F>(std::move(f)); }

    runtime_error winError(const char* api, long code) {
        stringstream s;
        s << api << ": 0x" << std::hex << code << " " << CT2A(util::Win32ErrorString(code));
        return runtime_error(s.str());
    }

    void say(const CardPrinter::LogFn& log, const std::wstring& msg) { if (log) log(msg); }

    // '~' 開頭是印表機 Escape 指令的保留字元，使用者文字不允許出現，避免意外觸發 Escape。
    void rejectEscapeChar(const std::wstring& s, const char* fieldName) {
        if (s.find(L'~') != std::wstring::npos) {
            stringstream e;
            e << fieldName << " 不可包含 '~' (保留給印表機 Escape 指令)";
            throw runtime_error(e.str());
        }
    }

    void validate(const PrintOptions& o) {
        if (o.printerName.empty()) throw runtime_error("請選擇印表機");
        rejectEscapeChar(o.frontText, "正面文字");
        rejectEscapeChar(o.backText, "背面文字");
        rejectEscapeChar(o.track1, "Track1");
        rejectEscapeChar(o.track2, "Track2");
        rejectEscapeChar(o.track3, "Track3");
    }

    CComPtr<IBidiSpl> bindPrinter(const CString& printerName) {
        CComPtr<IBidiSpl> bidiSpl;
        HRESULT hr = bidiSpl.CoCreateInstance(CLSID_BidiSpl);
        if (FAILED(hr)) throw winError("CoCreateInstance(CLSID_BidiSpl)", hr);
        hr = bidiSpl->BindDevice(printerName, BIDI_ACCESS_USER);
        if (FAILED(hr)) throw winError("BindDevice", hr);
        return bidiSpl;
    }

    // ------------------------------------------------------------ print ticket ---

    CString buildPrintTicketXml() {
        // 與範例相同的 PrintTicket 命名空間；這裡只設定「不翻轉」，其餘沿用驅動程式目前設定。
        // 想要頂膜/遮蔽區、停用列印等功能，可把 print/printTicketXml.h 內對應片段加進來。
        return CString(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<psf:PrintTicket version=\"1\" "
            "xmlns:psf=\"http://schemas.microsoft.com/windows/2003/08/printing/printschemaframework\" "
            "xmlns:psk=\"http://schemas.microsoft.com/windows/2003/08/printing/printschemakeywords\" "
            "xmlns:ns0000=\"http://schemas.datacard.com/2009/09/printing/XPS_Card_Printer\"> "
            "<psf:Feature name=\"ns0000:PageFlipFront\"><psf:Option name=\"ns0000:None\" /></psf:Feature>"
            "<psf:Feature name=\"ns0000:PageFlipBack\"><psf:Option name=\"ns0000:None\" /></psf:Feature>"
            "</psf:PrintTicket>");
    }

    // ------------------------------------------------------------------ drawing ---

    void drawPage(HDC dc, int scale, const std::wstring& text, const std::wstring& imagePath) {
        if (!text.empty()) {
            if (!::TextOutW(dc, 75 * scale, 50 * scale, text.c_str(), (int)text.size()))
                throw winError("TextOut", ::GetLastError());
        }
        if (!imagePath.empty()) {
            CImage img;
            HRESULT hr = img.Load(imagePath.c_str());
            if (FAILED(hr)) throw winError("CImage::Load", hr);

            // 縮放到可印區域內 (保持長寬比)
            const int margin = 50 * scale;
            const int top = 150 * scale;
            const int maxW = ::GetDeviceCaps(dc, HORZRES) - 2 * margin;
            const int maxH = ::GetDeviceCaps(dc, VERTRES) - top - margin;
            if (maxW <= 0 || maxH <= 0) throw runtime_error("可列印區域太小");
            const double fw = (double)maxW / img.GetWidth();
            const double fh = (double)maxH / img.GetHeight();
            const double f = fw < fh ? fw : fh;
            if (!img.Draw(dc, margin, top, (int)(img.GetWidth() * f), (int)(img.GetHeight() * f)))
                throw winError("CImage::Draw", ::GetLastError());
        }
    }

    void writeMagstripe(HDC dc, const PrintOptions& o) {
        // 與範例相同：用 TextOut 送出 ~1 ~2 ~3 Escape
        const std::wstring escapes[3] = { L"~1" + o.track1, L"~2" + o.track2, L"~3" + o.track3 };
        for (const auto& e : escapes) {
            if (!::TextOutW(dc, 0, 0, e.c_str(), (int)e.size()))
                throw winError("TextOut(magstripe)", ::GetLastError());
        }
    }

    // ----------------------------------------------------------- GDI print job ---
    // 回傳 Windows job ID。流程與範例 PrintTextAndGraphics() + CreatePrintJob() 相同。
    int submitGdiJob(const CString& printerName, const PrintOptions& o,
        const util::PrinterOptionsValues& po) {
        DWORD maxSchemaVersion = 0;
        HRESULT hr = ::PTQuerySchemaVersionSupport(printerName, &maxSchemaVersion);
        if (FAILED(hr)) throw winError("PTQuerySchemaVersionSupport", hr);

        HPTPROVIDER provider = 0;
        hr = ::PTOpenProvider(printerName, maxSchemaVersion, &provider);
        if (FAILED(hr)) throw winError("PTOpenProvider", hr);
        auto closeProvider = onExit([&] { ::PTCloseProvider(provider); });

        HANDLE printerHandle = 0;
        LPWSTR pName = const_cast<LPWSTR>(printerName.GetString());
        if (!::OpenPrinterW(pName, &printerHandle, NULL)) throw winError("OpenPrinter", ::GetLastError());
        auto closePrinter = onExit([&] { ::ClosePrinter(printerHandle); });

        // 取得 DEVMODE
        LONG size = ::DocumentPropertiesW(0, printerHandle, pName, NULL, NULL, 0);
        if (size < 0) throw winError("DocumentProperties(size)", ::GetLastError());
        std::vector<BYTE> devmodeBytes(size, 0);
        DEVMODE* devmode = reinterpret_cast<DEVMODE*>(devmodeBytes.data());
        if (::DocumentPropertiesW(NULL, printerHandle, pName, devmode, NULL, DM_OUT_BUFFER) < 0)
            throw winError("DocumentProperties", ::GetLastError());

        devmode->dmDuplex = o.duplex ? DMDUP_HORIZONTAL : DMDUP_SIMPLEX;
        devmode->dmOrientation = DMORIENT_LANDSCAPE;
        util::UpdateDevmode(printerHandle, printerName, devmode);
        ULONG devmodeSize = (ULONG)size;

        // DEVMODE -> PrintTicket
        CComPtr<IStream> ticketStream;
        util::CreateIStream(ticketStream);
        hr = ::PTConvertDevModeToPrintTicket(provider, devmodeSize, devmode, kPTJobScope, ticketStream);
        if (FAILED(hr)) throw winError("PTConvertDevModeToPrintTicket", hr);
        hr = ticketStream->Commit(0);
        if (FAILED(hr)) throw winError("ticketStream->Commit", hr);

        // 疊上卡片機專屬設定 (delta ticket)
        const CStringA deltaXml((LPCSTR)CW2A(buildPrintTicketXml()));
        CComPtr<IStream> deltaStream;
        util::CreateIStream(deltaStream);
        ULONG written = 0;
        hr = deltaStream->Write(deltaXml.GetString(), (ULONG)deltaXml.GetLength(), &written);
        if (FAILED(hr)) throw winError("deltaStream->Write", hr);

        CComPtr<IStream> resultStream;
        util::CreateIStream(resultStream);
        util::RewindIStream(ticketStream);
        util::RewindIStream(deltaStream);

        CComBSTR ptError;
        hr = ::PTMergeAndValidatePrintTicket(provider, ticketStream, deltaStream,
            kPTJobScope, resultStream, &ptError);
        if (S_PT_NO_CONFLICT != hr) {
            stringstream s;
            s << "PTMergeAndValidatePrintTicket: " << hr << " " << CW2A(ptError);
            throw runtime_error(s.str());
        }

        // PrintTicket -> DEVMODE (之後交給 CreateDC)
        util::RewindIStream(resultStream);
        devmode = nullptr;
        hr = ::PTConvertPrintTicketToDevMode(provider, resultStream, kUserDefaultDevmode,
            kPTJobScope, &devmodeSize, &devmode, &ptError);
        if (FAILED(hr)) throw winError("PTConvertPrintTicketToDevMode", hr);
        auto releaseDevmode = onExit([&] { if (devmode) ::PTReleaseMemory(devmode); });

        // 解析度：600 dpi 機型放大座標 (與範例相同的作法)
        int scale = 1;
        if (po._monochromePrintResolution == L"600x600") {
            devmode->dmPrintQuality = 600;
            devmode->dmYResolution = 600;
            scale = 2;
        }
        else {
            devmode->dmPaperLength = 856;   // ISO ID-1 85.60 x 53.98 mm (單位 0.1 mm)
            devmode->dmPaperWidth = 541;
        }

        HDC dc = ::CreateDCW(L"WINSPOOL", printerName, NULL, devmode);
        if (!dc) throw winError("CreateDC", ::GetLastError());
        auto deleteDc = onExit([&] { ::DeleteDC(dc); });

        DOCINFOW doc = {};
        doc.cbSize = sizeof(doc);
        doc.lpszDocName = L"QtCardPrint job";
        const int jobId = ::StartDoc(dc, &doc);
        if (jobId <= 0) throw winError("StartDoc", ::GetLastError());
        bool docEnded = false;
        auto abortDoc = onExit([&] { if (!docEnded) ::AbortDoc(dc); });  // 中途出錯就取消 spool

        HFONT font = ::CreateFontW(40 * scale, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH, L"Microsoft JhengHei");
        HGDIOBJ oldFont = font ? ::SelectObject(dc, font) : NULL;
        auto restoreFont = onExit([&] {
            if (oldFont) ::SelectObject(dc, oldFont);
            if (font) ::DeleteObject(font);
            });
        ::SetTextColor(dc, RGB(0, 0, 0));
        ::SetBkMode(dc, TRANSPARENT);

        // 正面
        if (::StartPage(dc) <= 0) throw winError("StartPage(front)", ::GetLastError());
        if (o.magstripe) writeMagstripe(dc, o);
        drawPage(dc, scale, o.frontText, o.frontImagePath);
        if (::EndPage(dc) <= 0) throw winError("EndPage(front)", ::GetLastError());

        // 背面
        if (o.duplex) {
            devmode->dmOrientation = DMORIENT_LANDSCAPE;
            util::UpdateDevmode(printerHandle, printerName, devmode);
            if (!::ResetDC(dc, devmode)) throw winError("ResetDC", ::GetLastError());
            if (::StartPage(dc) <= 0) throw winError("StartPage(back)", ::GetLastError());
            drawPage(dc, scale, o.backText, o.backImagePath);
            if (::EndPage(dc) <= 0) throw winError("EndPage(back)", ::GetLastError());
        }

        if (::EndDoc(dc) < 0) throw winError("EndDoc", ::GetLastError());
        docEnded = true;
        return jobId;
    }

} // namespace

// =============================================================== public API ===

std::vector<std::wstring> CardPrinter::listPrinters() {
    std::vector<std::wstring> names;
    DWORD needed = 0, count = 0;
    const DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    ::EnumPrintersW(flags, NULL, 4, NULL, 0, &needed, &count);
    if (needed == 0) return names;
    std::vector<BYTE> buf(needed);
    if (!::EnumPrintersW(flags, NULL, 4, buf.data(), needed, &needed, &count)) return names;
    const PRINTER_INFO_4W* info = reinterpret_cast<const PRINTER_INFO_4W*>(buf.data());
    for (DWORD i = 0; i < count; ++i) names.emplace_back(info[i].pPrinterName);
    return names;
}

std::wstring CardPrinter::queryStatus(const std::wstring& printerName, const LogFn& log) {
    CoutRedirect redirect(log);
    ComInit com;
    if (FAILED(com.hr)) throw winError("CoInitialize", com.hr);

    const CString name(printerName.c_str());
    CComPtr<IBidiSpl> bidiSpl = bindPrinter(name);
    auto unbind = onExit([&] { if (bidiSpl) bidiSpl->UnbindDevice(); });

    const auto po = util::ParsePrinterOptionsXML(util::GetPrinterOptionsXML(bidiSpl));
    const CString driver = util::ParseDriverVersionXML(util::GetDriverVersionXML(bidiSpl));

    std::wstring s;
    s += L"狀態: " + std::wstring(po._printerStatus.GetString());
    s += L" | 印字頭: " + std::wstring(po._printHead.GetString());
    s += L" | 磁條: " + std::wstring(po._optionMagstripe.GetString());
    s += L" | 解析度: " + std::wstring(po._monochromePrintResolution.GetString());
    s += L" | 驅動版本: " + std::wstring(driver.GetString());
    return s;
}

void CardPrinter::print(const PrintOptions& o, const LogFn& log) {
    validate(o);
    CoutRedirect redirect(log);
    ComInit com;
    if (FAILED(com.hr)) throw winError("CoInitialize", com.hr);

    const CString printerName(o.printerName.c_str());
    CComPtr<IBidiSpl> bidiSpl;
    int printerJobID = 0;

    auto cleanup = [&] { if (bidiSpl) { bidiSpl->UnbindDevice(); bidiSpl = NULL; } };

    try {
        bidiSpl = bindPrinter(printerName);

        const auto po = util::ParsePrinterOptionsXML(util::GetPrinterOptionsXML(bidiSpl));
        if (po._printerStatus != L"Ready" && po._printerStatus != L"Busy")
            throw runtime_error("印表機目前不是 Ready/Busy，無法接受工作");
        if (po._printHead != L"Installed" && po._connectionProtocol != L"Version1")
            throw runtime_error("印表機沒有安裝印字頭");
        if (o.magstripe && po._optionMagstripe.Find(L"ISO") == -1)
            throw runtime_error("印表機沒有 ISO 磁條模組");

        const CString hopperId = util::GetInputHopper(po._optionInputHopper, CString(o.hopperId.c_str()));
        if (o.checkHopper) {
            const CString st = util::ParseHopperStatusXML(util::GetHopperStatusXML(bidiSpl), hopperId);
            if (st.CompareNoCase(L"Empty") == 0) {
                stringstream s;
                s << "進卡匣 '" << CW2A(hopperId) << "' 是空的";
                throw runtime_error(s.str());
            }
            say(log, L"進卡匣 " + std::wstring(hopperId.GetString()) + L" 狀態: " + std::wstring(st.GetString()));
        }

        // 向印表機宣告一個工作 (取得 printerJobID，之後才能輪詢完成狀態)
        printerJobID = util::StartJob(bidiSpl, hopperId, CString(o.cardEjectSide.c_str()));

        const int windowsJobID = submitGdiJob(printerName, o, po);
        say(log, L"Windows 列印工作 " + std::to_wstring(windowsJobID) + L" 已送出");

        // 等資料全部 spool 完，再通知印表機「這張卡的資料送完了」
        util::WaitUntilJobSpooled(printerName, windowsJobID);
        util::EndJob(bidiSpl);

        if (o.pollCompletion) util::PollForJobCompletion(bidiSpl, printerJobID);
        say(log, L"完成");
    }
    catch (const util::BidiException& e) {
        util::CancelJob(bidiSpl, e.printerJobID, e.errorCode);
        cleanup();
        throw runtime_error(e.what());
    }
    catch (const std::exception&) {
        if (printerJobID != 0) util::CancelJob(bidiSpl, printerJobID, 0);
        cleanup();
        throw;
    }
    cleanup();
}