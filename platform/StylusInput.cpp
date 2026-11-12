#include "StylusInput.h"
#include <QCoreApplication>
#include <array>
#include <cstring>
#include <bit>
#include <QVariantMap>

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

std::optional<StylusSample> StylusSample::decode(std::span<const std::byte> packet) {
    if(packet.size()!=64)return std::nullopt;
    StylusSample sample;
    for(std::size_t field=0;field<16;++field){
        quint32 value=0;
        for(unsigned i=0;i<4;++i)value|=quint32(std::to_integer<unsigned char>(packet[field*4+i]))<<(8*i);
        if(field<5 && value>1)return std::nullopt;
        sample.values[field]=std::bit_cast<qint32>(value);
    }
    return sample;
}
std::optional<StylusSample> StylusSample::fromHex(const QByteArray& hex){
    if(hex.size()!=128)return std::nullopt;
    for(auto c:hex)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return std::nullopt;
    const auto bytes=QByteArray::fromHex(hex);
    return decode({reinterpret_cast<const std::byte*>(bytes.constData()),std::size_t(bytes.size())});
}
std::optional<StylusButtons> StylusButtons::decode(std::span<const std::byte> packet) {
    const auto sample=StylusSample::decode(packet);
    return sample ? std::optional<StylusButtons>(sample->buttons()) : std::nullopt;
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

struct StylusInput::Channel {
    QProcess process;
    QTimer startup;
    QByteArray pending;
    StylusButtonEdges edges;
    std::optional<StylusSample> sample;
    QString status="Not started";
    quint64 sequence=0, tipDowns=0, tipUps=0;
    int index=0;
    bool ready=false, stopping=false;
};

StylusInput::StylusInput(QObject* parent) : QObject(parent) {
    buttonHoldTimer_.setSingleShot(true);buttonHoldTimer_.setInterval(SideButtonHoldMs);buttonHoldTimer_.setTimerType(Qt::PreciseTimer);
    connect(&buttonHoldTimer_,&QTimer::timeout,this,[this]{
        if(!enabled_ || buttonOwner_<0)return;
        buttonHeld_=true;emit sideButtonHeld();
    });
    // Only the diagnostic display is throttled. Every contact/button transition
    // reaches consumers immediately, including down and up in the same frame.
    readingsTimer_.setInterval(100);readingsTimer_.setSingleShot(true);
    connect(&readingsTimer_,&QTimer::timeout,this,&StylusInput::readingsChanged);
    for(int i=0;i<2;++i){
        channels_[i]=std::make_unique<Channel>();auto* c=channels_[i].get();c->index=i;
        c->startup.setSingleShot(true);c->startup.setInterval(8000);
        connect(&c->startup,&QTimer::timeout,this,[this,c]{failProtocol(*c,"Listener could not start; reconnect to retry.");});
        connect(&c->process,&QProcess::readyReadStandardOutput,this,[this,c]{readOutput(*c);});
        connect(&c->process,&QProcess::readyReadStandardError,this,[c]{c->process.readAllStandardError();});
        connect(&c->process,&QProcess::errorOccurred,this,[this,c](QProcess::ProcessError error){
            if(!c->stopping && error==QProcess::FailedToStart)failProtocol(*c,"Could not launch listener.");
        });
        connect(&c->process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,c](int,QProcess::ExitStatus){
            if(!c->stopping)failProtocol(*c,"Listener stopped; reconnect to retry.");
        });
    }
}
StylusInput::~StylusInput(){for(auto& c:channels_)disconnect(&c->process,nullptr,this,nullptr);stop();}
void StylusInput::setStatus(const QString& value){if(status_==value)return;status_=value;emit statusChanged();}
void StylusInput::refreshStatus(){
    setStatus(QStringLiteral("Integrated / MPP: %1\nExternal / generic: %2").arg(channels_[0]->status,channels_[1]->status));
    emit readingsChanged();
}
QVariantList StylusInput::readings() const {
    QVariantList result;
    for(const auto& c:channels_){
        QVariantMap row{{"name",c->index==0?"Integrated / MPP":"External / generic"},{"status",c->status},
            {"packets",QVariant::fromValue(c->sequence)},{"tipDowns",QVariant::fromValue(c->tipDowns)},{"tipUps",QVariant::fromValue(c->tipUps)}};
        if(c->sample){const auto& v=c->sample->values;
            row["inRange"]=bool(v[4]);row["tip"]=bool(v[3]);row["barrel"]=bool(v[2]);row["invert"]=bool(v[1]);row["eraser"]=bool(v[0]);
            row["x"]=v[10];row["y"]=v[11];row["pressure"]=v[12];row["tiltX"]=v[8];row["tiltY"]=v[9];
        }
        result<<row;
    }
    return result;
}
void StylusInput::setEnabled(bool value){
    if(enabled_==value)return;
    enabled_=value;emit enabledChanged();emit settingsChanged();
    if(!enabled_)stopHelper();else if(started_)start();else setStatus("Pen input is enabled; input has not started.");
}
void StylusInput::setSelectedSideButton(int button){
    if((button!=1&&button!=2)||selectedSideButton_==button)return;
    cancelButtonGesture();
    selectedSideButton_=button;
    for(auto& c:channels_){c->edges.reset();if(c->sample)c->edges.update(c->sample->buttons(),button);}
    emit selectedSideButtonChanged();emit settingsChanged();
}
void StylusInput::start(){
    started_=true;if(!enabled_)return;
#ifdef Q_OS_WIN
    for(auto& owned:channels_){auto& c=*owned;
        if(c.process.state()!=QProcess::NotRunning)continue;
        c.edges.reset();c.sample.reset();c.pending.clear();c.sequence=c.tipDowns=c.tipUps=0;c.ready=false;c.stopping=false;
        c.process.setProgram(QCoreApplication::applicationFilePath());
        c.process.setArguments({"--pen-helper",c.index==1?"--generic-pen":"--integrated-pen"});
        c.process.setWorkingDirectory(QCoreApplication::applicationDirPath());
        c.process.setProcessChannelMode(QProcess::SeparateChannels);
        c.process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){
            args->flags|=CREATE_NO_WINDOW;args->startupInfo->dwFlags|=STARTF_USESHOWWINDOW;args->startupInfo->wShowWindow=SW_HIDE;
        });
        c.status="Starting…";c.startup.start();c.process.start();
    }
    refreshStatus();
#else
    setStatus("Pen input simulation: native pen data requires Windows.");
#endif
}
void StylusInput::stopChannel(Channel& c){
    if(buttonOwner_==c.index)cancelButtonGesture();
    c.stopping=true;c.startup.stop();c.ready=false;c.edges.reset();c.sample.reset();c.pending.clear();
    if(c.process.state()!=QProcess::NotRunning){c.process.kill();c.process.waitForFinished(1500);}
}
void StylusInput::stopHelper(){
    for(auto& c:channels_){stopChannel(*c);c->status="Off";}
    emit inputLost();emit readingsChanged();setStatus("Pen input is off.");
}
void StylusInput::stop(){started_=false;stopHelper();}
void StylusInput::failProtocol(Channel& c,const QString& reason){
    stopChannel(c);c.status=reason;emit inputLost();refreshStatus();
}
void StylusInput::cancelButtonGesture(){
    buttonHoldTimer_.stop();buttonOwner_=-1;buttonHeld_=false;
}
void StylusInput::updateButtonGesture(Channel& c,const StylusButtons& buttons){
    const bool rising=c.edges.update(buttons,selectedSideButton_)!=0;
    const bool down=selectedSideButton_==1?buttons.barrel:buttons.invert;
    if(buttonOwner_==c.index){
        if(!buttons.inRange){cancelButtonGesture();return;}
        if(!down){
            const bool tap=!buttonHeld_;
            cancelButtonGesture();
            if(tap)emit sideButtonPressed();
            return;
        }
    }
    if(rising && buttonOwner_<0){
        buttonOwner_=c.index;buttonHeld_=false;buttonHoldTimer_.start();emit sideButtonDown();
    }
}
void StylusInput::readOutput(Channel& c){
    const auto output=c.process.readAllStandardOutput();if(c.stopping)return;
    c.pending+=output;
    if(c.pending.size()>65536){failProtocol(c,"Invalid listener output.");return;}
    while(c.pending.contains('\n')){
        const auto newline=c.pending.indexOf('\n');auto line=c.pending.left(newline);c.pending.remove(0,newline+1);
        if(line.endsWith('\r'))line.chop(1);
        if(line.size()>256){failProtocol(c,"Invalid listener output.");return;}
        const auto fields=line.split(' ');
        if(line=="TPP2 READY"&&!c.ready){
            c.ready=true;c.startup.stop();c.status="Listening; waiting for pen data";refreshStatus();
        }else if(fields.size()==4&&fields[0]=="TPP2"&&fields[1]=="DATA"&&c.ready){
            bool ok=false;const auto sequence=fields[2].toULongLong(&ok);const auto sample=StylusSample::fromHex(fields[3]);
            if(!ok||sequence<=c.sequence||!sample){failProtocol(c,"Invalid pen packet.");return;}
            c.sequence=sequence;
            // Baseline the first sample; a pen already down when enabling input
            // must not manufacture a fresh drawing stroke or a side-button press.
            const bool down=sample->tip()&&sample->inRange();
            const bool changed=c.sample && down!=(c.sample->tip()&&c.sample->inRange());
            const bool left=c.sample && c.sample->inRange()&&!sample->inRange();
            c.sample=sample;
            if(changed){if(down)++c.tipDowns;else ++c.tipUps;emit tipChanged(quintptr(c.index+1),down);}
            if(left)emit proximityLeft();
            emit sampleReceived(c.index,*sample);
            c.status=sample->inRange()?(down?"Pen touching":"Pen hovering"):"Pen out of range";
            if(!readingsTimer_.isActive())readingsTimer_.start();
            if(enabled_)updateButtonGesture(c,sample->buttons());
        }else if(fields.size()>=3&&fields[0]=="TPP2"&&fields[1]=="ERROR"){
            failProtocol(c,fields[2]=="registration"?"No compatible pen listener; reconnect after connecting the device.":"Pen DLL error: "+QString::fromLatin1(fields[2]));return;
        }else{failProtocol(c,"Invalid listener protocol.");return;}
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
    std::optional<StylusSample> previous;
    std::array<std::byte,64> pending{};
    bool motionPending=false;
    ULONGLONG lastSent=0;
};

bool writeLine(HANDLE output, const char* data) {
    if (output==INVALID_HANDLE_VALUE || output==nullptr) return false;
    const std::size_t length=std::strlen(data);
    DWORD written=0;
    return WriteFile(output,data,static_cast<DWORD>(length),&written,nullptr) && written==length;
}

void sendSample(HelperState& state){
    char hex[129]{};constexpr char digits[]="0123456789abcdef";
    for(unsigned i=0;i<64;++i){const auto b=std::to_integer<unsigned char>(state.pending[i]);hex[2*i]=digits[b>>4];hex[2*i+1]=digits[b&15];}
    char line[192];std::snprintf(line,sizeof line,"TPP2 DATA %llu %s\n",static_cast<unsigned long long>(++state.sequence),hex);
    state.healthy=writeLine(state.output,line);state.lastSent=GetTickCount64();state.motionPending=false;
}

LRESULT CALLBACK penWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state=reinterpret_cast<HelperState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        const auto* create=reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    if(state && message==WM_TIMER && wParam==1){if(state->motionPending)sendSample(*state);return 0;}
    if (state && message==state->message) {
        void* allocation=reinterpret_cast<void*>(wParam);
        // The observed contract is GetProcessHeap/HeapFree ownership. Check it
        // and the allocation size before any dereference or release.
        if (!allocation || !HeapValidate(state->heap,0,allocation)) {
            state->healthy=false; writeLine(state->output,"TPP2 ERROR packet\n"); return 0;
        }
        const SIZE_T size=HeapSize(state->heap,0,allocation);
        std::array<std::byte,64> copy{};
        if (size>=copy.size() && size!=SIZE_T(-1)) std::memcpy(copy.data(),allocation,copy.size());
        const bool released=HeapFree(state->heap,0,allocation)!=FALSE;
        if (size<copy.size() || size==SIZE_T(-1) || !released) {
            state->healthy=false; writeLine(state->output,"TPP2 ERROR packet\n"); return 0;
        }
        const auto sample=StylusSample::decode(copy);
        if(!sample){state->healthy=false;writeLine(state->output,"TPP2 ERROR packet\n");return 0;}
        bool transition=!state->previous;
        if(state->previous)for(unsigned i=0;i<5;++i)transition|=state->previous->values[i]!=sample->values[i];
        state->previous=sample;state->pending=copy;state->motionPending=true;
        // Coalesce analog motion only. Contact, proximity and button edges are
        // never delayed or dropped; the timer flushes the final motion sample.
        if(transition || GetTickCount64()-state->lastSent>=16)sendSample(*state);
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

int StylusInput::runHelper(bool generic) {
#ifdef Q_OS_WIN
    const HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
    const auto path=executablePath();
    if (path.empty()) { writeLine(output,"TPP2 ERROR path\n"); return 2; }
    const HANDLE parent=openEditorParent(path);
    if (!parent) { writeLine(output,"TPP2 ERROR parent\n"); return 2; }
    const auto slash=path.find_last_of(L"\\/");
    if (slash==std::wstring::npos) { CloseHandle(parent); return 2; }
    const auto dllPath=path.substr(0,slash)+
        (sizeof(void*)==8?L"\\lib\\TabletProHIDx64.dll":L"\\lib\\TabletProHIDx32.dll");
    // Only shipped lib/app directories and System32 resolve dependencies.
    // The app directory accommodates windeployqt's local Microsoft runtime;
    // neither the current working directory nor PATH participates.
    const HMODULE dll=LoadLibraryExW(dllPath.c_str(),nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) { writeLine(output,"TPP2 ERROR load\n"); CloseHandle(parent); return 3; }
    using GetMessageNumber=std::uintptr_t (WINAPI*)();
    using RegisterReceiver=std::uintptr_t (WINAPI*)(HWND,int,int,int);
    const auto getMessage=reinterpret_cast<GetMessageNumber>(GetProcAddress(dll,
        sizeof(void*)==8?"GetWindowMessageNumberForSlimDataEvents":"_GetWindowMessageNumberForSlimDataEvents@0"));
    const auto registerReceiver=reinterpret_cast<RegisterReceiver>(GetProcAddress(dll,
        sizeof(void*)==8?"RegisterReceiveSurfacePenSlimDataEvents":"_RegisterReceiveSurfacePenSlimDataEvents@16"));
    if (!getMessage || !registerReceiver) {
        writeLine(output,"TPP2 ERROR abi\n"); CloseHandle(parent); return 4;
    }
    const auto number=getMessage();
    if (number<0xC000 || number>0xFFFF) {
        writeLine(output,"TPP2 ERROR abi\n"); CloseHandle(parent); return 4;
    }
    HelperState state{static_cast<UINT>(number),GetProcessHeap(),output,0,true,std::nullopt};
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    const wchar_t* windowClass=L"TabletPro.IsolatedPenInput.v1";
    WNDCLASSW klass{}; klass.lpfnWndProc=penWindowProc; klass.hInstance=instance; klass.lpszClassName=windowClass;
    if (!RegisterClassW(&klass)) { writeLine(output,"TPP2 ERROR window\n"); CloseHandle(parent); return 5; }
    // Hidden top-level sink matches A_ScriptHwnd; never shown or focused.
    const HWND sink=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,windowClass,L"",WS_POPUP,
        0,0,0,0,nullptr,nullptr,instance,&state);
    if (!sink) { writeLine(output,"TPP2 ERROR window\n"); CloseHandle(parent); return 5; }
    // Static inspection confirms zero success, HRESULT-like failure. This is
    // not a subscription handle despite the original AHK variable name.
    const auto result=registerReceiver(sink,0,1,generic?1:0);
    if (static_cast<std::uint32_t>(result)!=0) {
        writeLine(output,"TPP2 ERROR registration\n"); ExitProcess(6);
    }
    if(!SetTimer(sink,1,16,nullptr)){writeLine(output,"TPP2 ERROR timer\n");ExitProcess(7);}
    if (!writeLine(output,"TPP2 READY\n")) ExitProcess(7);
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
