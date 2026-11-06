#include "platform/StylusInput.h"
#include <QCoreApplication>
#include <array>
#include <cstdlib>
#include <iostream>

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
    std::cout<<"Stylus input: "<<checks<<" packet, edge, cancellation and lifecycle checks passed.\n";
}
