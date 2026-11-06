#include "StylusInput.h"
#include <QCoreApplication>
#include <array>
#include <cstring>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#endif

namespace tabletpro {

std::optional<StylusButtons> StylusButtons::decode(std::span<const std::byte> packet) {
    if (packet.size() != 64) return std::nullopt;
    const auto field = [&](std::size_t offset) {
        std::uint32_t value = 0;
        for (unsigned i=0; i<4; ++i)
            value |= std::uint32_t(std::to_integer<unsigned char>(packet[offset+i])) << (8*i);
        return value;
    };
    // The existing consumer compares signal fields with exactly zero or one.
    for (std::size_t offset=0; offset<=16; offset+=4)
        if (field(offset)>1) return std::nullopt;
    return StylusButtons{field(8)==1, field(4)==1, field(16)==1};
}

int StylusButtonEdges::update(const StylusButtons& buttons, int selected) {
    if (selected!=1 && selected!=2) { reset(); return 0; }
    if (!buttons.inRange) { reset(); return 0; }
    if (!haveBaseline_) { previous_=buttons; haveBaseline_=true; return 0; }
    const bool down=selected==1 ? buttons.barrel : buttons.invert;
    const bool previous=selected==1 ? previous_.barrel : previous_.invert;
    previous_=buttons;
    return down && !previous ? selected : 0;
}

StylusInput::StylusInput(QObject* parent) : QObject(parent) {
    startupTimer_.setSingleShot(true);
    startupTimer_.setInterval(8000);
    connect(&startupTimer_, &QTimer::timeout, this, [this] {
        failProtocol(QStringLiteral("Pen input could not start. Toggle it off and on to retry."));
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, &StylusInput::readOutput);
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        // DLL diagnostics are not an input-event channel.
        process_.readAllStandardError();
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (stopping_) return;
        startupTimer_.stop(); edges_.reset(); latestButtons_.reset(); ready_=false;
        if (error==QProcess::FailedToStart)
            setStatus(QStringLiteral("Could not launch the pen input helper."));
    });
    connect(&process_, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int, QProcess::ExitStatus) {
            startupTimer_.stop(); edges_.reset(); latestButtons_.reset(); ready_=false;
            if (!stopping_ && enabled_ && !status_.startsWith(QStringLiteral("Pen input error:")))
                setStatus(QStringLiteral("Pen input stopped. Toggle it off and on to retry."));
        });
}

StylusInput::~StylusInput() {
    disconnect(&process_, nullptr, this, nullptr);
    stop();
}

void StylusInput::setStatus(const QString& value) {
    if (status_==value) return;
    status_=value; emit statusChanged();
}

void StylusInput::setEnabled(bool value) {
    if (enabled_==value) return;
    enabled_=value; emit enabledChanged(); emit settingsChanged();
    edges_.reset();
    if (!enabled_) stopHelper();
    else if (started_) start();
    else setStatus(QStringLiteral("Pen side button is enabled; input has not started."));
}

void StylusInput::setSelectedSideButton(int button) {
    if ((button!=1 && button!=2) || selectedSideButton_==button) return;
    selectedSideButton_=button; edges_.reset();
    // The helper sends state changes, so the cached sample remains its current
    // state. Use it only as a baseline; selecting a held button never invokes.
    if (ready_ && latestButtons_) edges_.update(*latestButtons_,selectedSideButton_);
    emit selectedSideButtonChanged(); emit settingsChanged();
}

void StylusInput::start() {
    started_=true;
    if (!enabled_ || process_.state()!=QProcess::NotRunning) return;
    edges_.reset(); latestButtons_.reset(); pending_.clear(); sequence_=0; ready_=false; stopping_=false;
#ifdef Q_OS_WIN
    process_.setProgram(QCoreApplication::applicationFilePath());
    process_.setArguments({QStringLiteral("--pen-helper")});
    process_.setWorkingDirectory(QCoreApplication::applicationDirPath());
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
        // The supplied HID DLL calls SW_SHOW then SW_HIDE for its worker sink.
        // Override that first ShowWindow through process startup information;
        // preserve Qt's STARTF_USESTDHANDLES so the IPC pipes remain intact.
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
    });
    setStatus(QStringLiteral("Starting pen input…"));
    startupTimer_.start(); process_.start();
#else
    setStatus(QStringLiteral("Pen input simulation: native side buttons require Windows."));
#endif
}

void StylusInput::stopHelper() {
    stopping_=true; startupTimer_.stop(); edges_.reset(); latestButtons_.reset(); ready_=false; pending_.clear();
    if (process_.state()!=QProcess::NotRunning) {
        // No safe unregister ABI exists: end the entire isolated process.
        process_.kill(); process_.waitForFinished(1500);
    }
    setStatus(QStringLiteral("Pen side button is off."));
}

void StylusInput::stop() { started_=false; stopHelper(); }

void StylusInput::failProtocol(const QString& reason) {
    stopHelper(); setStatus(QStringLiteral("Pen input error: ")+reason);
}

void StylusInput::readOutput() {
    const auto output=process_.readAllStandardOutput();
    if (stopping_) return; // waitForFinished can deliver readyRead reentrantly.
    pending_+=output;
    if (pending_.size()>65536) { failProtocol(QStringLiteral("Invalid helper output.")); return; }
    while (pending_.contains('\n')) {
        const auto newline=pending_.indexOf('\n');
        const auto line=pending_.left(newline); pending_.remove(0,newline+1);
        if (line.size()>256) { failProtocol(QStringLiteral("Invalid helper output.")); return; }
        const auto fields=line.split(' ');
        if (line=="TPP1 READY" && !ready_) {
            ready_=true; startupTimer_.stop(); edges_.reset(); latestButtons_.reset();
            setStatus(QStringLiteral("Pen input ready. Hover, then press the selected side button."));
        } else if (fields.size()==5 && fields[0]=="TPP1" && fields[1]=="DATA" && ready_) {
            bool validSequence=false, validMask=false, validRange=false;
            const quint64 sequence=fields[2].toULongLong(&validSequence);
            const int mask=fields[3].toInt(&validMask), inRange=fields[4].toInt(&validRange);
            if (!validSequence || sequence<=sequence_ || !validMask || mask<0 || mask>3 ||
                !validRange || (inRange!=0 && inRange!=1)) {
                failProtocol(QStringLiteral("Invalid helper packet.")); return;
            }
            sequence_=sequence;
            const StylusButtons buttons{bool(mask&1),bool(mask&2),bool(inRange)};
            latestButtons_=buttons;
            if (enabled_ && edges_.update(buttons,selectedSideButton_)) emit sideButtonPressed();
        } else if (fields.size()>=3 && fields[0]=="TPP1" && fields[1]=="ERROR") {
            QString detail=QStringLiteral("The HID helper could not initialize.");
            if (fields[2]=="load") detail=QStringLiteral("The pen HID DLL or its Microsoft C++ runtime is missing from lib.");
            else if (fields[2]=="abi") detail=QStringLiteral("The pen HID DLL has an unsupported interface.");
            else if (fields[2]=="registration") detail=QStringLiteral("The HID DLL could not register pen input.");
            else if (fields[2]=="packet") detail=QStringLiteral("The HID DLL returned an invalid packet; input has stopped.");
            failProtocol(detail); return;
        } else { failProtocol(QStringLiteral("Invalid helper protocol.")); return; }
    }
}

#ifdef Q_OS_WIN
namespace {
struct HelperState {
    UINT message=0;
    HANDLE heap=nullptr;
    HANDLE output=INVALID_HANDLE_VALUE;
    std::uint64_t sequence=0;
    bool healthy=true;
    std::optional<StylusButtons> previous;
};

bool writeLine(HANDLE output, const char* data) {
    if (output==INVALID_HANDLE_VALUE || output==nullptr) return false;
    const std::size_t length=std::strlen(data);
    DWORD written=0;
    return WriteFile(output,data,static_cast<DWORD>(length),&written,nullptr) && written==length;
}

LRESULT CALLBACK penWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state=reinterpret_cast<HelperState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        const auto* create=reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    if (state && message==state->message) {
        void* allocation=reinterpret_cast<void*>(wParam);
        // The observed contract is GetProcessHeap/HeapFree ownership. Check it
        // and the allocation size before any dereference or release.
        if (!allocation || !HeapValidate(state->heap,0,allocation)) {
            state->healthy=false; writeLine(state->output,"TPP1 ERROR packet\n"); return 0;
        }
        const SIZE_T size=HeapSize(state->heap,0,allocation);
        std::array<std::byte,64> copy{};
        if (size>=copy.size() && size!=SIZE_T(-1)) std::memcpy(copy.data(),allocation,copy.size());
        const bool released=HeapFree(state->heap,0,allocation)!=FALSE;
        if (size<copy.size() || size==SIZE_T(-1) || !released) {
            state->healthy=false; writeLine(state->output,"TPP1 ERROR packet\n"); return 0;
        }
        const auto buttons=StylusButtons::decode(copy);
        if (!buttons) { state->healthy=false; writeLine(state->output,"TPP1 ERROR packet\n"); return 0; }
        // Every DLL allocation was released above. Only first sample or state
        // changes cross IPC, so motion/pressure sampling cannot flood the GUI.
        if (state->previous && state->previous->barrel==buttons->barrel &&
            state->previous->invert==buttons->invert && state->previous->inRange==buttons->inRange)
            return 0;
        state->previous=buttons;
        char line[96];
        const unsigned mask=(buttons->barrel?1u:0u)|(buttons->invert?2u:0u);
        std::snprintf(line,sizeof line,"TPP1 DATA %llu %u %u\n",
            static_cast<unsigned long long>(++state->sequence),mask,buttons->inRange?1u:0u);
        state->healthy=writeLine(state->output,line);
        return 0;
    }
    return DefWindowProcW(hwnd,message,wParam,lParam);
}

std::wstring executablePath(HANDLE process=nullptr) {
    std::wstring path(32768,L'\0');
    DWORD length=static_cast<DWORD>(path.size());
    if (process) {
        if (!QueryFullProcessImageNameW(process,0,path.data(),&length)) return {};
    } else {
        length=GetModuleFileNameW(nullptr,path.data(),length);
        if (!length || length>=path.size()) return {};
    }
    path.resize(length); return path;
}

HANDLE openEditorParent(const std::wstring& ownPath) {
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if (snapshot==INVALID_HANDLE_VALUE) return nullptr;
    PROCESSENTRY32W entry{}; entry.dwSize=sizeof entry;
    DWORD parentId=0;
    if (Process32FirstW(snapshot,&entry)) {
        do { if (entry.th32ProcessID==GetCurrentProcessId()) { parentId=entry.th32ParentProcessID; break; } }
        while (Process32NextW(snapshot,&entry));
    }
    CloseHandle(snapshot);
    if (!parentId) return nullptr;
    HANDLE parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parentId);
    if (!parent) return nullptr;
    const auto parentPath=executablePath(parent);
    if (parentPath.empty() || _wcsicmp(parentPath.c_str(),ownPath.c_str())!=0 ||
        WaitForSingleObject(parent,0)!=WAIT_TIMEOUT) { CloseHandle(parent); return nullptr; }
    return parent;
}
}
#endif

int StylusInput::runHelper() {
#ifdef Q_OS_WIN
    const HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
    const auto path=executablePath();
    if (path.empty()) { writeLine(output,"TPP1 ERROR path\n"); return 2; }
    const HANDLE parent=openEditorParent(path);
    if (!parent) { writeLine(output,"TPP1 ERROR parent\n"); return 2; }
    const auto slash=path.find_last_of(L"\\/");
    if (slash==std::wstring::npos) { CloseHandle(parent); return 2; }
    const auto dllPath=path.substr(0,slash)+
        (sizeof(void*)==8?L"\\lib\\TabletProHIDx64.dll":L"\\lib\\TabletProHIDx32.dll");
    // Only shipped lib/app directories and System32 resolve dependencies.
    // The app directory accommodates windeployqt's local Microsoft runtime;
    // neither the current working directory nor PATH participates.
    const HMODULE dll=LoadLibraryExW(dllPath.c_str(),nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) { writeLine(output,"TPP1 ERROR load\n"); CloseHandle(parent); return 3; }
    using GetMessageNumber=std::uintptr_t (WINAPI*)();
    using RegisterReceiver=std::uintptr_t (WINAPI*)(HWND,int,int,int);
    const auto getMessage=reinterpret_cast<GetMessageNumber>(GetProcAddress(dll,
        sizeof(void*)==8?"GetWindowMessageNumberForSlimDataEvents":"_GetWindowMessageNumberForSlimDataEvents@0"));
    const auto registerReceiver=reinterpret_cast<RegisterReceiver>(GetProcAddress(dll,
        sizeof(void*)==8?"RegisterReceiveSurfacePenSlimDataEvents":"_RegisterReceiveSurfacePenSlimDataEvents@16"));
    if (!getMessage || !registerReceiver) {
        writeLine(output,"TPP1 ERROR abi\n"); CloseHandle(parent); return 4;
    }
    const auto number=getMessage();
    if (number<0xC000 || number>0xFFFF) {
        writeLine(output,"TPP1 ERROR abi\n"); CloseHandle(parent); return 4;
    }
    HelperState state{static_cast<UINT>(number),GetProcessHeap(),output,0,true,std::nullopt};
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    const wchar_t* windowClass=L"TabletPro.IsolatedPenInput.v1";
    WNDCLASSW klass{}; klass.lpfnWndProc=penWindowProc; klass.hInstance=instance; klass.lpszClassName=windowClass;
    if (!RegisterClassW(&klass)) { writeLine(output,"TPP1 ERROR window\n"); CloseHandle(parent); return 5; }
    // Hidden top-level sink matches A_ScriptHwnd; never shown or focused.
    const HWND sink=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,windowClass,L"",WS_POPUP,
        0,0,0,0,nullptr,nullptr,instance,&state);
    if (!sink) { writeLine(output,"TPP1 ERROR window\n"); CloseHandle(parent); return 5; }
    // Static inspection confirms zero success, HRESULT-like failure. This is
    // not a subscription handle despite the original AHK variable name.
    const auto result=registerReceiver(sink,1,0,0);
    if (static_cast<std::uint32_t>(result)!=0) {
        writeLine(output,"TPP1 ERROR registration\n"); ExitProcess(6);
    }
    if (!writeLine(output,"TPP1 READY\n")) ExitProcess(7);
    // Parent process is a wait source, so parent crashes cannot orphan a listener.
    while (state.healthy) {
        const DWORD wait=MsgWaitForMultipleObjects(1,&parent,FALSE,INFINITE,QS_ALLINPUT);
        if (wait==WAIT_OBJECT_0 || wait==WAIT_FAILED) break;
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if (message.message==WM_QUIT) { state.healthy=false; break; }
            TranslateMessage(&message); DispatchMessageW(&message);
            if (!state.healthy || WaitForSingleObject(parent,0)==WAIT_OBJECT_0) break;
        }
        if (WaitForSingleObject(parent,0)==WAIT_OBJECT_0) break;
    }
    // No unregister export: never FreeLibrary or tear down a sink while the DLL
    // worker can post. Operating-system process teardown owns all resources.
    ExitProcess(state.healthy?0:7);
#else
    return 2;
#endif
}

} // namespace tabletpro
