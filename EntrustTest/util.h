////////////////////////////////////////////////////////////////////////////////
// Copyright (c) Datacard Corporation.  All Rights Reserved.
//
// declarations for XPS Card Printer SDK common utilities
////////////////////////////////////////////////////////////////////////////////
#pragma once
#include <atlstr.h>
#include <msxml6.h>
#include <bidispl.h>
#include <vector>


namespace util
{
    class BidiException : public std::runtime_error {
    public:
        BidiException(std::string errorString, const int printerJobID, const int errorCode)
            : std::runtime_error(errorString)
            , printerJobID(printerJobID)
            , errorCode(errorCode)
        {
        }

        int errorCode{};
        int printerJobID{};
    };

    typedef std::vector<CString>    HOPPER_STATUS_LIST;

    struct FirmwareVersion {
        CString _printerBase;
        short   _majorVersion{};
        short   _minorVersion{};
        short   _deviationVersion{};
    };

    struct PrinterStatusValues {
        CString  _clientID;
        int      _errorCode{};
        int      _errorSeverity{};
        CString  _errorString;
        CString  _printerData;
        int      _printerJobID{};
        int      _windowsJobID{};
    };

    struct PrinterOptionsValues {
        CString  _colorPrintResolution;
        CString  _connectionPortType;
        CString  _connectionProtocol;
        CString  _embosserVersion;
        CString  _laminator;
        CString  _laminatorFirmwareVersion;
        CString  _laminatorImpresser;
        CString  _laminatorScanner;
        CString  _lockState;
        CString  _moduleEmbosser;
        CString  _monochromePrintResolution;
        CString  _optionDuplex;
        CString  _multiHopperVersion;
        CString  _optionInputHopperType;
        CString  _optionInputHopper;
        CString  _optionOutputhopper;
        CString  _optionObscureBlackPanel;
        CString  _optionLocks;
        CString  _optionMagstripe;
        CString  _optionSecondaryMagstripeJIS;
        CString  _optionPrinterBarcodeReader;
        CString  _optionRewrite;
        CString  _optionSmartcard;
        CString  _optionTactileImpresser;
        CString  _printEngineType;
        CString  _printerAddress;
        int      _printerMessageNumber{};
        CString  _printerModel;
        CString  _printerSerialNumber;
        CString  _printerStatus;
        CString  _printerVersion;
        CString  _printHead;
        CString  _topcoatPrintResolution;
        CString  _rearPickOption;
        CString  _kioskOption;
        CString  _optionUVCure;
        CString  _fluorescentPrintResolution;
    };

    struct PrinterOptions3Values {
        CString  _printerColorMode;
    };

    struct PrinterCardCountValues {
        int      _cardsPickedSinceCleaningCard{};
        int      _cleaningCardsRun{};
        int      _currentCompleted{};
        int      _currentLost{};
        int      _currentPicked{};
        int      _currentPickedExceptionSlot{};
        int      _currentPickedInputHopper1{};
        int      _currentPickedInputHopper2{};
        int      _currentPickedInputHopper3{};
        int      _currentPickedInputHopper4{};
        int      _currentPickedInputHopper5{};
        int      _currentPickedInputHopper6{};
        int      _currentRejected{};
        CString  _printerStatus;
        int      _totalCompleted{};
        int      _totalLost{};
        int      _totalPicked{};
        int      _totalPickedExceptionSlot{};
        int      _totalPickedInputHopper1{};
        int      _totalPickedInputHopper2{};
        int      _totalPickedInputHopper3{};
        int      _totalPickedInputHopper4{};
        int      _totalPickedInputHopper5{};
        int      _totalPickedInputHopper6{};
        int      _totalRejected{};
    };

    struct SuppliesValues {
        CString _indentRibbon;
        short   _indentRibbonRemaining{};
        CString _indentRibbonSerialNumber;
        long    _indentRibbonPartNumber{};
        CString _indentRibbonLotCode;

        CString _printerStatus;
        CString _printRibbonType;
        CString _printRibbonSerialNumber;
        long    _printRibbonPartNumber{};
        CString _printRibbonLotCode;
        long    _printRibbonRegionCode{};
        short   _ribbonRemaining{};

        CString _retransferFilmSerialNumber;
        long    _retransferFilmPartNumber{};
        CString _retransferFilmLotCode;
        short   _retransferFilmRemaining{};
        short   _retransferHeaterPercentReady{};

        short   _topperRibbonRemaining{};
        CString _topperRibbonType;
        CString _topperRibbonSerialNumber;
        long    _topperRibbonPartNumber{};
        CString _topperRibbonLotCode;

        long    _laminatorL1SupplyCode{};
        long    _laminatorL1PercentRemaining{};
        long    _laminatorL1PartNumber{};
        CString _laminatorL1LotCode;
        CString _laminatorL1SerialNumber;

        long    _laminatorL2SupplyCode{};
        long    _laminatorL2PercentRemaining{};
        long    _laminatorL2PartNumber{};
        CString _laminatorL2LotCode;
        CString _laminatorL2SerialNumber;

        long    _tactileImpresserFoilType{};
        long    _tactileImpresserFoilRemaining{};
        long    _tactileImpresserPartNumber{};
        CString _tactileImpresserLotCode;
        CString _tactileImpresserSerialNumber;
    };

    struct JobStatusValues {
        CString  _clientID;
        int      _windowsJobID{};
        int      _printerJobID{};
        CString  _jobState;
        int      _jobRestartCount{};
    };

    struct SmartcardResponseValues {
        std::wstring               _protocol;
        std::vector <std::wstring> _states;
        std::wstring               _status;
        std::wstring               _base64string;
        std::vector <byte>         _bytesFromBase64String;
    };

    enum EscapeSide {
        escapeOnCardFront, escapeOnCardBack, escapeOnBothCardSides
    };

    struct Escapes {
        CString     printBlockingEscape;
        CString     topcoatRemovalEscape;
        EscapeSide  escapeSide;
    };

    struct HOPPER_INFORMATION {
        CString _name;
        CString _type;
        CString _status;
    };

    typedef std::vector <HOPPER_INFORMATION> HOPPER_INFO_LIST;

    CString Win32ErrorString(const long errorCode);
    void EchoCurrentTime(CString msg);

    CString FormatPrinterActionXML(
        const int actionID,
        const int printerJobID,
        const int errorCode);

    CString GetPrinterStatusXML(CComPtr <IBidiRequest>& bidiRequest);
    CString GetPrinterStatusXML(CComPtr <IBidiSpl>& bidiSpl);
    PrinterStatusValues ParsePrinterStatusXML(const CString printerStatusXML);

    CString GetPrinterOptionsXML(CComPtr <IBidiSpl>& bidiSpl);
    PrinterOptionsValues ParsePrinterOptionsXML(const CString printerOptionsXML);

    CString GetPrinterOptions3XML(CComPtr <IBidiSpl>& bidiSpl);
    PrinterOptions3Values ParsePrinterOptions3XML(const CString printerOptions3XML);

    CString GetHopperStatusXML(CComPtr <IBidiSpl>& bidiSpl);
    CString ParseHopperStatusXML(
        const CString   hopperStatusXML,
        const CString   hopperId);
    int GetHopperIndex(const CString hopperId);

    CString GetPrinterCardCountsXML(CComPtr <IBidiSpl>& bidiSpl);
    PrinterCardCountValues ParsePrinterCardCountsXML(const CString printerCardCountsXML);

    CString GetDriverVersionXML(CComPtr <IBidiSpl>& bidiSpl);
    CString ParseDriverVersionXML(const CString driverVersionXML);

    CString GetPrinterActionReturnXML(
        CComPtr <IBidiSpl>& bidiSpl,
        CString actionString);

    CString GetJobStatusXML(CComPtr <IBidiSpl>& bidiSpl, const DWORD printerJobID);
    JobStatusValues ParseJobStatusXML(const CString jobStatusXML);

    CString GetSuppliesXML(CComPtr<IBidiSpl>& bidiSpl);
    SuppliesValues ParseSuppliesXML(const CString suppliesXML);

    CString GetInputHopper(CString inputHopper, CString commandLineHopperID);

    SmartcardResponseValues ParseSmartcardResponseXML(const CString smartcardReponseXML);

    void ParseMagstripeStrings(
        const CString  printerStatusXML,
        CString& track1,
        CString& track2,
        CString& track3,
        bool           bJISRequest);

    void WaitUntilJobSpooled(const HANDLE printerHandle, const DWORD windowsJobID);
    void WaitUntilJobSpooled(CString printerName, const DWORD windowsJobID);

    void UpdateDevmode(
        const HANDLE    printerHandle,
        const CString   printerName,
        DEVMODE* devmode);

    void PrintTextAndGraphics(
        CString        printerName,
        const HANDLE   printerHandle,
        DWORD& windowsJobID,
        const bool     duplex,
        int yLocation = 200,
        const bool emboss = false);

    BOOL RawDataToXpsPrinter(LPTSTR szPrinterName, LPBYTE lpData, DWORD dwCount);

    void PrintText(CString printerName, const bool duplex, int yLocation = 200, const bool emboss = false);

    void PrintTextAndGraphics(
        CString        printerName,
        const HANDLE   printerHandle,
        DWORD& windowsJobID,
        const bool     duplex,
        const Escapes  escapes);

    void PrintText(
        CString         printerName,
        const bool      duplex,
        const Escapes   escapes);

    int     IntFromXml(CComPtr <IXMLDOMDocument2>& doc, const PWCHAR xpathExpression);
    CString StringFromXml(CComPtr <IXMLDOMDocument2>& doc, const PWCHAR xpathExpression);

    CString GetShortExeName();
    CString GetExePath();

    void CreateIStream(CComPtr <IStream>& stream);
    void RewindIStream(CComPtr <IStream>& stream);

    int StartJob(
        CComPtr <IBidiSpl>& bidiSpl,
        const CString       hopperID = CString("1"),
        const CString       cardEjectSide = CString("Default"),
        //const CString       outputHopperLocation = CString("reardispense")
        const CString       outputHopperLocation = CString("")
    );

    void PollForJobCompletion(CComPtr <IBidiSpl>& bidiSpl, const int printerJobID);
    void EndJob(CComPtr <IBidiSpl>& bidiSpl);

    void CancelJob(CComPtr <IBidiSpl>& bidiSpl, const PrinterStatusValues& printerStatusValues);
    void CancelJob(CComPtr <IBidiSpl>& bidiSpl, const int printerJobID, const int errorCode);


    HOPPER_INFO_LIST  ParseHopperStatusXml(const CString hopperStatusXml);

    FirmwareVersion ParseFirmwareRev(const CString strFwRev);

    void SendEmbossIndentData(HDC printerDC);
}