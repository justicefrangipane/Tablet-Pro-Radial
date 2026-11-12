#include "platform/StylusInput.h"
#include <QCoreApplication>
#include <array>
#include <cstdlib>
#include <iostream>
#include <QTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QSet>

namespace {
int checks=0;
void require(bool condition,const char* name) {
    ++checks;
    if (!condition) { std::cerr<<"FAILED: "<<name<<'\n'; std::exit(1); }
}
void field(std::array<std::byte,64>& bytes,std::size_t offset,std::uint32_t value) {
    for (unsigned i=0;i<4;++i) bytes[offset+i]=std::byte((value>>(8*i))&0xff);
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    using namespace tabletpro;
    if(app.arguments().contains("--pen-helper")){
        const bool generic=app.arguments().contains("--generic-pen");
        std::array<std::byte,64> data{};quint64 sequence=0;
        const auto send=[&]{std::cout<<"TPP2 DATA "<<++sequence<<" "<<QByteArray(reinterpret_cast<const char*>(data.data()),64).toHex().constData()<<'\n'<<std::flush;};
        std::cout<<"TPP2 READY\n"<<std::flush;
        const auto gesture=qEnvironmentVariable("TABLETPRO_TEST_BUTTON_GESTURE");
        if(!gesture.isEmpty()){
            field(data,16,1);send();
            if(generic || gesture=="duplicate"){
                const auto offset=gesture=="invert"?4:8;
                QTimer::singleShot(30,&app,[&,offset]{field(data,offset,1);send();});
                if(gesture=="range")QTimer::singleShot(130,&app,[&]{field(data,16,0);send();});
                if(gesture=="failure")QTimer::singleShot(130,&app,[&]{std::cout<<"TPP2 ERROR registration\n"<<std::flush;});
                const int release=(gesture=="tap"||gesture=="duplicate")?100:900;
                QTimer::singleShot(release,&app,[&,offset]{field(data,offset,0);send();});
            }
            QTimer::singleShot(10000,&app,&QCoreApplication::quit);return app.exec();
        }
        field(data,16,1);field(data,32,quint32(qint32(-37)));send();
        if(qEnvironmentVariableIsSet("TABLETPRO_TEST_BAD_PACKET")&&!generic){std::cout<<"TPP2 DATA 2 abc\n"<<std::flush;}
        else{
            field(data,12,1);send();field(data,12,0);send();
            field(data,8,1);send();send();field(data,8,0);send();
            field(data,12,1);send();field(data,16,0);send();
        }
        QTimer::singleShot(10000,&app,&QCoreApplication::quit);return app.exec();
    }
    std::array<std::byte,64> bytes{};
    require(!StylusButtons::decode(std::span<const std::byte>{}),"empty packet rejected");
    require(!StylusButtons::decode(std::span<const std::byte>(bytes.data(),63)),"truncated packet rejected");
    std::array<std::byte,65> tooLong{};
    require(!StylusButtons::decode(tooLong),"wrong packet size rejected");
    auto decoded=StylusButtons::decode(bytes);
    require(decoded && !decoded->barrel && !decoded->invert && !decoded->inRange,"zero state decoded");
    field(bytes,8,1); field(bytes,16,1);
    decoded=StylusButtons::decode(bytes);
    require(decoded && decoded->barrel && !decoded->invert && decoded->inRange,"barrel and range offsets");
    field(bytes,4,1); field(bytes,8,0);
    decoded=StylusButtons::decode(bytes);
    require(decoded && !decoded->barrel && decoded->invert,"invert offset");
    for (std::size_t offset=0;offset<=16;offset+=4) {
        const auto saved=bytes;
        field(bytes,offset,256);
        require(!StylusButtons::decode(bytes),"all four bytes of boolean are validated");
        bytes=saved;
    }
    field(bytes,28,0xffffffff); field(bytes,40,0x12345678);
    require(StylusButtons::decode(bytes).has_value(),"unrelated pressure and coordinates do not alter button decode");
    field(bytes,32,quint32(qint32(-63)));field(bytes,36,62);field(bytes,12,1);
    const auto full=StylusSample::decode(bytes);
    require(full&&full->tip()&&full->values[8]==-63&&full->values[9]==62&&full->values[12]==0,"full packet retains signed tilt and tip");
    const auto hex=QByteArray(reinterpret_cast<const char*>(bytes.data()),64).toHex();
    require(StylusSample::fromHex(hex)->values==full->values,"IPC preserves every field exactly");
    auto invalidHex=hex;invalidHex[5]='z';
    require(!StylusSample::fromHex(invalidHex)&&!StylusSample::fromHex(hex.left(126)),"invalid or short hexadecimal packet rejected");

    StylusButtonEdges edges;
    const StylusButtons hover{false,false,true},barrel{true,false,true},invert{false,true,true},both{true,true,true};
    require(edges.update(barrel,1)==0,"already held at first sample is baseline");
    require(edges.update(barrel,1)==0,"held input never repeats");
    require(edges.update(hover,1)==0,"release rearms without firing");
    require(edges.update(barrel,1)==1,"barrel rising edge fires once");
    require(edges.update(barrel,1)==0,"duplicate sample does not repeat");
    require(edges.update(hover,1)==0,"second release rearms");
    require(edges.update(invert,1)==0,"other button does not invoke");
    require(edges.update(both,1)==1,"barrel edge while invert held invokes once");
    edges.reset();
    require(edges.update(hover,2)==0,"second button starts with baseline");
    require(edges.update(invert,2)==2,"invert rising edge selects button two");
    require(edges.update(invert,2)==0,"invert hold never repeats");
    require(edges.update({true,true,false},2)==0,"out of range cancels even when buttons report held");
    require(edges.update(invert,2)==0,"return in range held establishes new baseline");
    require(edges.update(hover,2)==0,"release after return rearms");
    require(edges.update(invert,2)==2,"next real edge after return invokes");
    edges.reset();
    require(edges.update(invert,2)==0,"explicit cancellation rebaselines");
    require(edges.update(hover,3)==0,"invalid button ignored and cancels");
    require(edges.update(invert,2)==0,"invalid selection leaves no stale baseline");

    StylusInput input;
    int settings=0,enabled=0,selection=0,invoked=0;
    QObject::connect(&input,&StylusInput::settingsChanged,[&]{++settings;});
    QObject::connect(&input,&StylusInput::enabledChanged,[&]{++enabled;});
    QObject::connect(&input,&StylusInput::selectedSideButtonChanged,[&]{++selection;});
    QObject::connect(&input,&StylusInput::sideButtonPressed,[&]{++invoked;});
    require(!input.enabled() && input.selectedSideButton()==1,"input starts disabled with barrel selected");
    input.setSelectedSideButton(3);
    require(input.selectedSideButton()==1 && !settings,"invalid setting rejected");
    input.setSelectedSideButton(2); input.setSelectedSideButton(2);
    require(selection==1 && settings==1,"selection persisted once per change");
    input.setEnabled(true); input.setEnabled(true);
    require(input.enabled() && enabled==1 && settings==2,"enabled persisted once per change");
    require(input.status().contains("not started"),"settings alone do not spawn helper");
#ifndef Q_OS_WIN
    input.start();
    require(input.status().contains("simulation"),"non-Windows start is explicit simulation");
    require(StylusInput::runHelper()==2,"non-Windows helper is unavailable");
#endif
    input.stop();
    require(input.enabled(),"stopping lifetime preserves preference");
    input.setEnabled(false);
    require(!input.enabled() && settings==3 && enabled==2,"disable updates preference");
    input.setEnabled(true);
    require(input.status().contains("not started"),"stop prevents automatic restart via setter");
    require(invoked==0,"configuration emits no synthetic button presses");
#ifdef Q_OS_WIN
    const auto waitFor=[&](auto predicate){QElapsedTimer timer;timer.start();while(!predicate()&&timer.elapsed()<4000){app.processEvents();QThread::msleep(5);}return predicate();};
    input.setSelectedSideButton(1);
    int downs=0,ups=0,hoverExits=0;QSet<quintptr> sources;
    QObject::connect(&input,&StylusInput::proximityLeft,[&]{++hoverExits;});
    QObject::connect(&input,&StylusInput::tipChanged,[&](quintptr source,bool down){sources.insert(source);if(down)++downs;else ++ups;});
    input.start();
    require(waitFor([&]{return ups==4;}),"both helper channels deliver every tip down and up, including leaving range");
    require(downs==4&&sources.size()==2&&invoked==2,"channels retain independent baselines; duplicate button samples never repeat invocation");
    require(hoverExits==2,"each listener reports hover exit independently of ordinary tip lifts");
    const auto rows=input.readings();require(rows.size()==2&&rows[0].toMap()["tiltX"].toInt()==-37,"live monitor reports full packet values");
    input.stop();const auto before=invoked;downs=ups=0;sources.clear();
    qputenv("TABLETPRO_TEST_BAD_PACKET","1");input.start();
    require(waitFor([&]{return ups==2&&input.status().contains("Invalid pen packet");}),"malformed integrated channel fails without disabling generic channel");
    require(invoked==before+1&&sources.size()==1,"healthy channel continues independently after protocol failure");
    input.stop();qunsetenv("TABLETPRO_TEST_BAD_PACKET");
    const auto wait=[](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
    for(const auto* mode:{"tap","hold","invert","duplicate","range","failure","disable","selection","stop"}){
        qputenv("TABLETPRO_TEST_BUTTON_GESTURE",mode);
        StylusInput gesture;gesture.setEnabled(true);if(QString(mode)=="invert")gesture.setSelectedSideButton(2);
        int pressed=0,tapped=0,held=0;
        QElapsedTimer duration;
        QObject::connect(&gesture,&StylusInput::sideButtonDown,[&]{++pressed;duration.start();});
        QObject::connect(&gesture,&StylusInput::sideButtonPressed,[&]{++tapped;});
        QObject::connect(&gesture,&StylusInput::sideButtonHeld,[&]{require(duration.elapsed()>=StylusInput::SideButtonHoldMs,"hold cannot fire before 500 ms");++held;});
        gesture.start();require(waitFor([&]{return pressed>0;}),"real helper button-down starts classification");
        require(tapped==0&&held==0,"press alone does not hide or switch the menu");
        const QString scenario(mode);
        if(scenario=="disable")gesture.setEnabled(false);
        if(scenario=="selection")gesture.setSelectedSideButton(2);
        if(scenario=="stop")gesture.stop();
        if(scenario=="tap" || scenario=="duplicate"){
            require(waitFor([&]{return tapped==1;}),"short press invokes on release");wait(550);
            require(pressed==1&&tapped==1&&held==0,"short press and duplicate channels never generate a long press");
        }else if(scenario=="hold" || scenario=="invert"){
            require(waitFor([&]{return held==1;}),"selected barrel or invert hold invokes mode switching");
            require(tapped==0,"long press does not also invoke a short press");wait(500);
            require(held==1&&tapped==0,"holding and releasing after long press never toggles visibility");
        }else{
            wait(1000);require(held==0&&tapped==0,"range exit, listener failure, disable, selection change and stop cancel pending gestures");
        }
        gesture.stop();
    }
    qunsetenv("TABLETPRO_TEST_BUTTON_GESTURE");
#endif
    std::cout<<"Stylus input: "<<checks<<" packet, edge, cancellation and lifecycle checks passed.\n";
}
