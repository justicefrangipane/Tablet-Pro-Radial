#include "RadialSurface.h"
#include "core/gesture.h"

#include <QMouseEvent>
#include <QTouchEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGOpacityNode>
#include <QSGTextureMaterial>
#include <QSGVertexColorMaterial>
#include <QSGTexture>
#include <QSGRendererInterface>
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double outerRadius = 96, innerRadius = 44, iconRadius = 70;
constexpr int spriteSize = 256, dockSize = 44, voxelSize = 6, blendBuckets = 12;
constexpr int childSpriteSize = 96, childAtlasScale = 2, childVoxelSize = 4;
constexpr double childRadius = radial::GestureEngine::ChildRadius;
constexpr double childInner = radial::GestureEngine::ChildInnerRadius;
constexpr double childOuter = radial::GestureEngine::ChildOuterRadius;
constexpr double childHalfAngle = radial::GestureEngine::ChildHalfAngleDegrees*pi/180;
double clamp(double v) { return std::clamp(v, 0.0, 1.0); }
double lerp(double a, double b, double t) { return a + (b-a)*t; }
double easeSmooth(double t) { t=clamp(t); return t*t*(3-2*t); }
double smoother(double t) { t=clamp(t); return t*t*t*(t*(t*6-15)+10); }
double hash(int n) { double s=std::sin(n*127.1+311.7)*43758.5453; return s-std::floor(s); }
QColor mix(QColor a, QColor b, double t) { return QColor::fromRgbF(lerp(a.redF(),b.redF(),t),lerp(a.greenF(),b.greenF(),t),lerp(a.blueF(),b.blueF(),t),lerp(a.alphaF(),b.alphaF(),t)); }
QColor withAlpha(QColor c, double a) { c.setAlphaF(clamp(a)); return c; }
QPointF polar(double r, double a) { return {r*std::cos(a),r*std::sin(a)}; }

struct Button {
    QString id, icon, modifierState, caption;
    QColor color;
    QPointF center, radialCenter, dock, from;
    double angle=0;
    int slot=0;
    bool selected=false, group=false;
    QRect sourceAtlas, targetAtlas;
};
Button readButton(const QVariant &value, int index) {
    const auto v=value.toMap(); Button b;
    b.id=v.value("id").toString(); b.icon=v.value("icon","circle").toString();
    static const std::array<const char*,6> colors={"#749ac6","#9b8bbb","#72b3ac","#c59e7f","#899ac5","#b28ea9"};
    b.modifierState=v.value("modifierState").toString();b.caption=v.value("caption").toString();
    b.slot=std::clamp(v.value("slot",index).toInt(),0,5);
    b.color=QColor(v.value("color",colors[b.slot]).toString());
    if(!b.color.isValid()) b.color=QColor(colors[b.slot]);
    b.angle=v.value("angle",-pi/2+b.slot*pi/3).toDouble();
    b.center={v.value("cx").toDouble(),v.value("cy").toDouble()};
    b.from={v.value("fromX",b.center.x()).toDouble(),v.value("fromY",b.center.y()).toDouble()};
    b.radialCenter=b.center-polar(iconRadius,b.angle);
    b.dock={v.value("dockX",35).toDouble(),v.value("dockY",100+b.slot*52).toDouble()};
    b.selected=v.value("selected",false).toBool(); b.group=v.value("group",false).toBool(); return b;
}
Button readChild(const QVariant &value,int index) {
    auto b=readButton(value,index);
    b.angle=value.toMap().value("axis",std::atan2(b.center.y()-b.from.y(),b.center.x()-b.from.x())).toDouble();
    return b;
}
QPainterPath sector(double angle,double half,double inner,double outer) {
    QPainterPath path;
    const double start=angle-half,end=angle+half,corner=2.0;
    path.moveTo(polar(inner+corner,start));path.lineTo(polar(outer-corner,start));
    path.quadTo(polar(outer,start),polar(outer,start+corner/outer));
    for(int i=1;i<=36;i++)path.lineTo(polar(outer,lerp(start+corner/outer,end-corner/outer,i/36.0)));
    path.quadTo(polar(outer,end),polar(outer-corner,end));path.lineTo(polar(inner+corner,end));
    path.quadTo(polar(inner,end),polar(inner,end-corner/inner));
    for(int i=1;i<=36;i++)path.lineTo(polar(inner,lerp(end-corner/inner,start+corner/inner,i/36.0)));
    path.quadTo(polar(inner,start),polar(inner+corner,start));path.closeSubpath();return path;
}
double childPop(double reveal,int slot) {
    // A small damped pop, with a 3.0% maximum overshoot. Scaling the whole
    // annular sector about its anchor preserves every angular gap mid-flight.
    const double p=clamp((reveal-slot*.027)/.865);
    const double q=p-1;
    return std::min(radial::GestureEngine::ChildVisualOvershoot,1+1.9*q*q*q+.9*q*q);
}
QPainterPath wedge(double angle, double span) {
    QPainterPath path;
    const auto a0=angle-span/2+.035, a1=angle+span/2-.035;
    path.moveTo(polar(outerRadius,a0));
    // Small straight segments keep the texture and hit-test angles identical.
    for(int j=1;j<=32;j++) path.lineTo(polar(outerRadius,lerp(a0,a1,j/32.0)));
    path.lineTo(polar(innerRadius,a1));
    for(int j=1;j<=32;j++) path.lineTo(polar(innerRadius,lerp(a1,a0,j/32.0)));
    path.closeSubpath(); return path;
}

// Original native geometric icon drawings. No platform font or image asset is
// required, so the same sprite atlas works on Windows, macOS and Linux.
void paintIcon(QPainter &p, const QString &name, QPointF center, QColor ink=QColor("#edf3ff")) {
    p.save(); p.translate(center); p.scale(.92,.92);
    p.setPen(QPen(ink,1.65,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
    QPainterPath path;
    if(name=="Alt" || name=="Ctrl" || name=="Shift") {
        QFont font("Segoe UI");font.setPixelSize(12);font.setWeight(QFont::DemiBold);p.setFont(font);
        p.drawText(QRectF(-21,-11,42,22),Qt::AlignCenter,name);
    } else if(name=="dock") {
        p.drawLine(-8,-8,-8,8);p.drawLine(-3,0,9,0);p.drawLine(-3,0,1,-4);p.drawLine(-3,0,1,4);
    } else if(name=="edit") {
        path.moveTo(-8,8);path.lineTo(-5,1);path.lineTo(4,-8);path.lineTo(8,-4);path.lineTo(-1,5);path.closeSubpath();p.drawPath(path);
    } else if(name=="sound") {
        path.moveTo(-9,-3);path.lineTo(-5,-3);path.lineTo(0,-8);path.lineTo(0,8);path.lineTo(-5,3);path.lineTo(-9,3);path.closeSubpath();p.drawPath(path);p.drawArc(QRectF(-4,-7,14,14),-60*16,120*16);
    } else if(name=="glass") {
        path.moveTo(0,-8);path.lineTo(9,-3);path.lineTo(0,2);path.lineTo(-9,-3);path.closeSubpath();p.drawPath(path);p.drawLine(-8,2,0,7);p.drawLine(0,7,8,2);
    } else if(name=="hide") {
        path.moveTo(-9,0);path.quadTo(0,-11,9,0);path.quadTo(0,11,-9,0);p.drawPath(path);p.drawEllipse(QPointF(0,0),3,3);p.drawLine(-8,-8,8,8);
    } else if(name=="quit") {
        p.drawLine(0,-9,0,0);p.drawArc(QRectF(-8,-8,16,16),135*16,270*16);
    } else if(name.contains("brush") || name=="pen" || name=="pencil") {
        path.moveTo(-2,3); path.lineTo(7,-8); path.quadTo(10,-10,9,-6); path.lineTo(2,5); path.closeSubpath(); p.drawPath(path);
        path=QPainterPath(); path.moveTo(-2,3); path.cubicTo(-8,2,-4,8,-10,8); path.cubicTo(-4,11,2,9,2,5); p.drawPath(path);
    } else if(name=="eraser") {
        path.moveTo(-9,2);path.lineTo(0,-8);path.quadTo(1,-9,2,-8);path.lineTo(9,-2);path.lineTo(0,8);path.lineTo(-3,8);path.closeSubpath();p.drawPath(path);p.drawLine(-5,-2,4,5);p.drawLine(0,8,10,8);
    } else if(name=="text-select") {
        p.setPen(QPen(ink,1.2,Qt::DashLine,Qt::RoundCap));p.drawRoundedRect(QRectF(-10,-10,20,20),1,1);
        p.setPen(QPen(ink,1.65,Qt::SolidLine,Qt::RoundCap));p.drawLine(-5,-5,5,-5);p.drawLine(0,-5,0,6);p.drawLine(-3,6,3,6);
    } else if(name.contains("select") || name.contains("lasso")) {
        path.moveTo(-6,6);path.cubicTo(-14,1,-6,-10,3,-8);path.cubicTo(14,-6,10,5,2,6);path.cubicTo(-4,7,-5,2,-2,2);path.cubicTo(3,2,1,11,-3,10);p.drawPath(path);
    } else if(name=="palette" || name=="color") {
        path.moveTo(1,9);path.cubicTo(-11,10,-13,-5,-3,-9);path.cubicTo(4,-12,13,-5,9,1);path.cubicTo(7,4,2,0,1,4);path.cubicTo(0,6,5,8,1,9);p.drawPath(path);
        p.setBrush(ink); for(QPointF dot: {QPointF(-6,-2),QPointF(-2,-6),QPointF(3,-5)})p.drawEllipse(dot,.8,.8);
    } else if(name=="type" || name=="text") {
        p.drawLine(-8,-8,8,-8);p.drawLine(-8,-8,-8,-5);p.drawLine(8,-8,8,-5);p.drawLine(0,-8,0,9);p.drawLine(-4,9,4,9);
    } else if(name.contains("setting") || name=="sliders") {
        p.drawLine(-8,-7,8,-7);p.drawLine(-8,0,8,0);p.drawLine(-8,7,8,7);p.setBrush(QColor("#2e394a"));p.drawEllipse(QPointF(-3,-7),2,2);p.drawEllipse(QPointF(4,0),2,2);p.drawEllipse(QPointF(-1,7),2,2);
    } else if(name=="bold") {
        path.moveTo(-6,9);path.lineTo(-6,-9);path.lineTo(1,-9);path.cubicTo(9,-9,9,0,1,0);path.lineTo(-6,0);path.moveTo(1,0);path.cubicTo(10,0,10,9,1,9);path.lineTo(-6,9);p.drawPath(path);
    } else if(name=="italic") {
        p.drawLine(-1,-9,8,-9);p.drawLine(3,-9,-3,9);p.drawLine(-8,9,1,9);
    } else if(name=="underline") {
        path.moveTo(-6,-9);path.lineTo(-6,1);path.cubicTo(-6,9,6,9,6,1);path.lineTo(6,-9);p.drawPath(path);p.drawLine(-8,11,8,11);
    } else if(name.contains("align")) {
        const bool centered=name.contains("center"), right=name.contains("right");
        for(int i=0;i<4;i++){int length=i%2?11:18; int x=centered?-length/2:(right?9-length:-9);p.drawLine(x,-8+i*5,x+length,-8+i*5);}
    } else if(name.contains("undo") || name=="rotate-ccw") {
        path.moveTo(-8,-3);path.lineTo(-8,-9);path.moveTo(-8,-3);path.lineTo(-2,-3);path.moveTo(-8,-3);path.cubicTo(0,-13,14,-5,7,5);path.cubicTo(3,10,-3,9,-6,6);p.drawPath(path);
    } else if(name.contains("redo") || name=="rotate-cw") {
        path.moveTo(8,-3);path.lineTo(8,-9);path.moveTo(8,-3);path.lineTo(2,-3);path.moveTo(8,-3);path.cubicTo(0,-13,-14,-5,-7,5);path.cubicTo(-3,10,3,9,6,6);p.drawPath(path);
    } else if(name=="ellipsis" || name=="more-horizontal") {
        p.setBrush(ink);for(int x:{-7,0,7})p.drawEllipse(QPointF(x,0),1.3,1.3);
    } else if(name=="arrow-left-to-line" || name=="arrow-right-to-line") {
        if(name=="arrow-right-to-line")p.scale(-1,1);
        p.drawLine(-9,-9,-9,9);p.drawLine(-5,0,9,0);p.drawLine(-5,0,0,-5);p.drawLine(-5,0,0,5);
    } else if(name=="save") {
        path.moveTo(-8,-9);path.lineTo(5,-9);path.lineTo(9,-5);path.lineTo(9,9);path.lineTo(-8,9);path.closeSubpath();p.drawPath(path);p.drawRect(QRectF(-4,-9,7,6));p.drawRect(QRectF(-4,2,9,7));
    } else if(name=="x" || name=="close") {
        p.drawLine(-7,-7,7,7);p.drawLine(-7,7,7,-7);
    } else if(name.contains("plus")) {
        p.drawLine(-8,0,8,0);p.drawLine(0,-8,0,8);
    } else if(name.contains("minus")) { p.drawLine(-8,0,8,0);
    } else if(name.contains("copy") || name.contains("duplicate")) {p.drawRoundedRect(QRectF(-3,-3,12,13),2,2);path.moveTo(4,-6);path.lineTo(4,-9);path.lineTo(-9,-9);path.lineTo(-9,4);path.lineTo(-6,4);p.drawPath(path);
    } else if(name.contains("paste") || name.contains("clipboard")) {p.drawRoundedRect(QRectF(-7,-7,14,17),2,2);p.drawRoundedRect(QRectF(-3,-10,6,5),1,1);
    } else if(name.contains("cut") || name=="scissors") {p.drawEllipse(QPointF(-6,6),3,3);p.drawEllipse(QPointF(6,6),3,3);p.drawLine(-4,4,7,-9);p.drawLine(4,4,-7,-9);
    } else if(name.contains("zoom") || name=="search") {p.drawEllipse(QPointF(-2,-2),6,6);p.drawLine(3,3,9,9);if(name.contains("in")){p.drawLine(-5,-2,1,-2);p.drawLine(-2,-5,-2,1);}if(name.contains("out"))p.drawLine(-5,-2,1,-2);
    } else if(name.contains("layer") || name=="group") {for(int i=0;i<3;i++){path=QPainterPath();path.moveTo(-9,-4+i*5);path.lineTo(0,-9+i*5);path.lineTo(9,-4+i*5);if(i==0)path.lineTo(0,1);else{path.moveTo(-9,-4+i*5);path.lineTo(0,1+i*5);path.lineTo(9,-4+i*5);}p.drawPath(path);}
    } else if(name=="move" || name=="hand") {p.drawLine(-9,0,9,0);p.drawLine(0,-9,0,9);for(int i=0;i<4;i++){p.save();p.rotate(i*90);p.drawLine(-3,-6,0,-9);p.drawLine(3,-6,0,-9);p.restore();}
    } else if(name.contains("check")) {path.moveTo(-8,0);path.lineTo(-2,6);path.lineTo(9,-7);p.drawPath(path);
    } else if(name.contains("trash")) {p.drawLine(-8,-6,8,-6);p.drawLine(-3,-9,3,-9);path.moveTo(-6,-6);path.lineTo(-5,9);path.lineTo(5,9);path.lineTo(6,-6);p.drawPath(path);p.drawLine(-2,-2,-2,5);p.drawLine(2,-2,2,5);
    } else {p.drawRoundedRect(QRectF(-7,-7,14,14),3,3);p.drawLine(-3,0,3,0);p.drawLine(0,-3,0,3);}
    p.restore();
}
void paintChild(QPainter &p,const Button &b,QRect rect,bool glassEnabled) {
    p.save();p.translate(rect.topLeft());p.scale(childAtlasScale,childAtlasScale);
    p.translate(childSpriteSize/2.0,childSpriteSize/2.0);
    p.translate(-polar(childRadius,b.angle));
    const auto shape=sector(b.angle,childHalfAngle,childInner,childOuter);
    const auto icon=polar(childRadius,b.angle);
    // Translucent color leaves the native blurred backdrop visible. The beveled
    // rim is lit separately, so the six swatches read as individual glass pieces.
    QLinearGradient glass(polar(childInner,b.angle-.28),polar(childOuter,b.angle+.28));
    glass.setColorAt(0,withAlpha(mix(QColor("#23414d"),b.color,.55),glassEnabled?.44:.91));
    glass.setColorAt(.46,withAlpha(mix(QColor("#779aac"),b.color,.74),glassEnabled?.34:.86));
    glass.setColorAt(1,withAlpha(mix(QColor("#152b3c"),b.color,.62),glassEnabled?.54:.94));
    p.setBrush(glass);p.setPen(QPen(withAlpha(QColor("#e4f7ff"),.48),.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawPath(shape);
    p.setBrush(Qt::NoBrush);
    QPainterPath rim;rim.moveTo(polar(childOuter-1,b.angle-childHalfAngle+.012));
    for(int i=1;i<=28;i++)rim.lineTo(polar(childOuter-1,b.angle-childHalfAngle+.012+(2*childHalfAngle-.024)*i/28));
    p.setPen(QPen(withAlpha(QColor("#effaff"),.40),.8,Qt::SolidLine,Qt::RoundCap));p.drawPath(rim);
    QPainterPath inset;inset.moveTo(polar(childInner+1,b.angle-childHalfAngle+.02));
    for(int i=1;i<=20;i++)inset.lineTo(polar(childInner+1,b.angle-childHalfAngle+.02+(2*childHalfAngle-.04)*i/20));
    p.setPen(QPen(withAlpha(QColor("#10232e"),.36),.9,Qt::SolidLine,Qt::RoundCap));p.drawPath(inset);
    paintIcon(p,b.icon,icon+QPointF(0,.6),withAlpha(QColor("#102535"),.6));
    paintIcon(p,b.icon,icon,QColor("#f5fbff"));
    if(b.group){
        const auto tip=polar(childOuter-7,b.angle),side=polar(2,b.angle+pi/2),back=polar(-2,b.angle);
        p.setPen(QPen(withAlpha(QColor("#f1faff"),.72),1.0,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        QPainterPath cue;cue.moveTo(tip+back+side);cue.lineTo(tip);cue.lineTo(tip+back-side);p.drawPath(cue);
    }
    p.restore();
}
struct ChildParticle {
    int child=0,id=0;
    QPointF offset;
    QRectF uv;
    double noise=0,alpha=1;
};
struct Particle {
    int tool=0, id=0;
    QPointF source, destination;
    QRectF sourceUV, targetUV;
    double tw=0,th=0,u=0,v=0,noise=0,alpha=1;
};
struct Pose { QPointF position; double rotation,width,height,lift,progress,local; };
Pose quietPose(const Particle &p, const Button &b, double t) {
    const double order=clamp(std::abs(p.u-.5)+std::abs(p.v-.5));
    const double delay=.065+b.slot*.013+order*.09;
    const double local=clamp((t-delay)/.65), q=smoother((local-.08)/.82), sn=std::sin(pi*q);
    QPointF pos=p.source+(p.destination-p.source)*q+QPointF(sn*5,sn*(p.noise-.5)*9);
    const double separation=std::sin(pi*clamp(local/.32))*(1-easeSmooth((local-.2)/.17));
    pos+=(p.source-b.radialCenter)*(separation*3.7/outerRadius);
    const double seat=clamp((local-.9)/.1);
    if(seat>0 && seat<1)pos.rx()-=std::sin(seat*pi*2)*std::exp(-seat*5)*1.6;
    const double lift=std::sin(pi*local), shrink=easeSmooth((q-.45)/.5);
    return {pos,(p.noise-.5)*.34*lift,lerp(voxelSize,p.tw,shrink),lerp(voxelSize,p.th,shrink),lift*1.2,q,local};
}

using ColorVertex=QSGGeometry::ColoredPoint2D;
using TexVertex=QSGGeometry::TexturedPoint2D;
using Colors=std::vector<ColorVertex>;
using Textures=std::vector<TexVertex>;
ColorVertex cv(QPointF p,QColor c) {ColorVertex v; const double a=c.alphaF();v.set(float(p.x()),float(p.y()),uchar(c.red()*a),uchar(c.green()*a),uchar(c.blue()*a),c.alpha());return v;}
void triangle(Colors &v,QPointF a,QPointF b,QPointF c,QColor color) {v.push_back(cv(a,color));v.push_back(cv(b,color));v.push_back(cv(c,color));}
void quad(Colors &v,QPointF a,QPointF b,QPointF c,QPointF d,QColor color) {triangle(v,a,b,c,color);triangle(v,a,c,d,color);}
void line(Colors &v,QPointF a,QPointF b,double width,QColor color) {
    const auto delta=b-a;const double len=std::hypot(delta.x(),delta.y());if(len<.001)return;
    const QPointF n(-delta.y()*width/(2*len),delta.x()*width/(2*len));quad(v,a+n,b+n,b-n,a-n,color);
}
void halo(Colors &v,QPointF center,double radius,QColor tint,double alpha) {
    if(alpha<.001)return;
    constexpr int segments=24;
    for(int ring=0;ring<3;ring++){
        const double r0=radius*ring/3,r1=radius*(ring+1)/3;
        const QColor c0=withAlpha(tint,alpha*std::pow(1-ring/3.0,3)),c1=withAlpha(tint,alpha*std::pow(1-(ring+1)/3.0,3));
        for(int i=0;i<segments;i++){
            const QPointF a=center+polar(r0,i*2*pi/segments),b=center+polar(r1,i*2*pi/segments),c=center+polar(r1,(i+1)*2*pi/segments),d=center+polar(r0,(i+1)*2*pi/segments);
            for(auto pair:{std::pair<QPointF,QColor>(a,c0),{b,c1},{c,c1},{a,c0},{c,c1},{d,c0}})v.push_back(cv(pair.first,pair.second));
        }
    }
}
void textureQuad(Textures &v,QPointF center,double width,double height,double rotation,QRectF uv,QSize atlasSize) {
    const double co=std::cos(rotation),si=std::sin(rotation);
    std::array<QPointF,4> p={QPointF(-width/2,-height/2),{width/2,-height/2},{width/2,height/2},{-width/2,height/2}};
    std::array<QPointF,4> u={uv.topLeft(),uv.topRight(),uv.bottomRight(),uv.bottomLeft()};
    for(int i:{0,1,2,0,2,3}){TexVertex a;a.set(float(center.x()+p[i].x()*co-p[i].y()*si),float(center.y()+p[i].x()*si+p[i].y()*co),float(u[i].x()/atlasSize.width()),float(u[i].y()/atlasSize.height()));v.push_back(a);}
}
QSGGeometryNode *makeColorNode() {
    auto *n=new QSGGeometryNode;auto *g=new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(),0);g->setDrawingMode(QSGGeometry::DrawTriangles);g->setVertexDataPattern(QSGGeometry::DynamicPattern);
    n->setGeometry(g);n->setFlag(QSGNode::OwnsGeometry);n->setMaterial(new QSGVertexColorMaterial);n->setFlag(QSGNode::OwnsMaterial);return n;
}
QSGGeometryNode *makeTextureNode(QSGTexture *texture) {
    auto *n=new QSGGeometryNode;auto *g=new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(),0);g->setDrawingMode(QSGGeometry::DrawTriangles);g->setVertexDataPattern(QSGGeometry::DynamicPattern);
    n->setGeometry(g);n->setFlag(QSGNode::OwnsGeometry);auto *m=new QSGTextureMaterial;m->setTexture(texture);m->setFiltering(QSGTexture::Linear);m->setFlag(QSGMaterial::Blending,true);n->setMaterial(m);n->setFlag(QSGNode::OwnsMaterial);return n;
}
template<class T>void upload(QSGGeometryNode *n,const std::vector<T> &v) {
    auto *g=n->geometry();if(g->vertexCount()!=int(v.size()))g->allocate(int(v.size()));
    if(!v.empty())std::copy(v.begin(),v.end(),static_cast<T*>(g->vertexData()));
    g->markVertexDataDirty();n->markDirty(QSGNode::DirtyGeometry);
}
struct RenderNode : QSGNode {
    QSGTexture *texture=nullptr;
    QSGGeometryNode *lights=nullptr,*sides=nullptr,*solids=nullptr,*outlines=nullptr;
    std::array<QSGGeometryNode*,blendBuckets> sources{},targets{};
    explicit RenderNode(QSGTexture *t):texture(t) {
        lights=makeColorNode();appendChildNode(lights);sides=makeColorNode();appendChildNode(sides);
        for(int i=0;i<blendBuckets;i++){
            const double a=double(i)/(blendBuckets-1);
            auto *sourceOpacity=new QSGOpacityNode;sourceOpacity->setOpacity(1-a);appendChildNode(sourceOpacity);sources[i]=makeTextureNode(texture);sourceOpacity->appendChildNode(sources[i]);
            auto *targetOpacity=new QSGOpacityNode;targetOpacity->setOpacity(a);appendChildNode(targetOpacity);targets[i]=makeTextureNode(texture);targetOpacity->appendChildNode(targets[i]);
        }
        solids=makeTextureNode(texture);appendChildNode(solids);outlines=makeColorNode();appendChildNode(outlines);
    }
    ~RenderNode() override {while(firstChild()){auto *n=firstChild();removeChildNode(n);delete n;}delete texture;}
};
} // namespace

namespace {
void paintModifier(QPainter& p,const Button& b,QPointF center){
    if(b.modifierState.isEmpty())return;
    p.save();p.translate(center+QPointF(0,b.caption.isEmpty()?14:-21));p.setPen(QPen(QColor("#ffe5bd"),1));p.setBrush(QColor("#ffe5bd"));
    if(b.modifierState=="latched"){
        p.setBrush(Qt::NoBrush);p.drawRoundedRect(QRectF(-3,-2,6,5),1,1);p.drawArc(QRectF(-2,-6,4,6),0,180*16);
    }else if(b.modifierState=="dragged"){p.drawRoundedRect(QRectF(-5,-1,10,2),1,1);}
    else p.drawEllipse(QPointF(0,0),1.8,1.8);
    p.restore();
}
}
struct RadialSurface::State {
    std::vector<Button> buttons,children,retiring;
    std::vector<Particle> particles;
    std::vector<ChildParticle> childParticles;
    std::vector<double> ends;
    QImage atlas;
    bool railStyle = false;
    void rebuild(const QVariantList &roots,const QVariantList &childList,const QVariantList &retiringList,bool glassEnabled,bool continuousRail,bool bronzeTheme) {
        railStyle=continuousRail;
        buttons.clear();children.clear();retiring.clear();particles.clear();childParticles.clear();ends.clear();
        for(int i=0;i<roots.size();i++)buttons.push_back(readButton(roots[i],i));
        for(int i=0;i<childList.size();i++)children.push_back(readChild(childList[i],i));
        for(int i=0;i<retiringList.size();i++)retiring.push_back(readChild(retiringList[i],i));
        const int atlasSlots=std::max(1,int(buttons.size()*2+children.size()+retiring.size())),cols=4;
        atlas=QImage(cols*spriteSize,((atlasSlots+cols-1)/cols)*spriteSize,QImage::Format_RGBA8888_Premultiplied);atlas.fill(Qt::transparent);
        QPainter ap(&atlas);ap.setRenderHint(QPainter::Antialiasing);
        const auto slot=[&](int index,int side){return QRect((index%cols)*spriteSize,(index/cols)*spriteSize,side,side);};
        const auto drawDock=[&](Button &b,QRect rect){
            ap.save();ap.translate(rect.topLeft());QLinearGradient gradient(0,0,44,44);gradient.setColorAt(0,withAlpha(mix(QColor("#303b4f"),b.color,.4),glassEnabled?.48:1));gradient.setColorAt(1,withAlpha(mix(QColor("#303b4f"),b.color,.16),glassEnabled?.60:1));
            if(!continuousRail){ap.setBrush(gradient);ap.setPen(QPen(withAlpha(b.color,.62),.8));ap.drawRoundedRect(QRectF(.5,.5,43,43),11,11);}
            if(!b.modifierState.isEmpty()){ap.setBrush(QColor("#88613d"));ap.setPen(QPen(QColor("#ffd08a"),1));ap.drawRoundedRect(QRectF(2,2,40,40),9,9);}
            paintIcon(ap,b.icon,{22,22},b.modifierState.isEmpty()?QColor("#edf3ff"):QColor("#ffe5bd"));
            paintModifier(ap,b,{22,22});ap.restore();
        };
        for(int i=0;i<int(buttons.size());i++){
            auto &b=buttons[i];b.sourceAtlas=slot(i*2,256);b.targetAtlas=slot(i*2+1,44);
            ap.save();ap.translate(b.sourceAtlas.topLeft()+QPoint(128,128));const auto path=wedge(b.angle,pi/3);
            QLinearGradient gradient(-96,-96,96,96);
            const bool held=!b.modifierState.isEmpty();
            gradient.setColorAt(0,bronzeTheme?withAlpha(QColor(held?"#a47747":"#75543b"),glassEnabled?.84:1):withAlpha(mix(QColor("#303b4f"),b.color,.43),glassEnabled?.42:1));
            gradient.setColorAt(1,bronzeTheme?withAlpha(QColor(held?"#69452c":"#382920"),glassEnabled?.90:1):withAlpha(mix(QColor("#303b4f"),b.color,.15),glassEnabled?.56:1));
            ap.setBrush(gradient);ap.setPen(QPen(bronzeTheme?QColor(held?"#ffd08a":"#ad8055"):withAlpha(b.color,.62),.8));ap.drawPath(path);
            const auto center=polar(iconRadius,b.angle);
            paintIcon(ap,b.icon,center+QPointF(0,b.caption.isEmpty()?0:-6),bronzeTheme?QColor("#ffe5bd"):QColor("#edf3ff"));
            if(!b.caption.isEmpty()){QFont font("Segoe UI");font.setPixelSize(9);font.setWeight(QFont::Medium);ap.setFont(font);ap.setPen(QColor("#ffe5bd"));ap.drawText(QRectF(center.x()-23,center.y()+7,46,13),Qt::AlignCenter,b.caption);}
            paintModifier(ap,b,center);ap.restore();drawDock(b,b.targetAtlas);
        }
        for(int i=0;i<int(children.size());i++){auto &b=children[i];b.targetAtlas=slot(int(buttons.size())*2+i,childSpriteSize*childAtlasScale);paintChild(ap,b,b.targetAtlas,glassEnabled);}
        for(int i=0;i<int(retiring.size());i++){auto &b=retiring[i];b.targetAtlas=slot(int(buttons.size())*2+int(children.size())+i,childSpriteSize*childAtlasScale);paintChild(ap,b,b.targetAtlas,glassEnabled);}
        ap.end();
        // Preserve the outgoing icon and glass as real texture fragments. These
        // are presentation-only snapshots, never live action targets.
        constexpr int cell=childVoxelSize*childAtlasScale;
        for(int i=0;i<int(retiring.size());i++){
            const auto &b=retiring[i];int cursor=0;
            for(int y=0;y<childSpriteSize*childAtlasScale;y+=cell)for(int x=0;x<childSpriteSize*childAtlasScale;x+=cell){
                double alpha=0;
                for(int yy=0;yy<cell;yy++)for(int xx=0;xx<cell;xx++)alpha+=qAlpha(atlas.pixel(b.targetAtlas.x()+x+xx,b.targetAtlas.y()+y+yy));
                if(alpha<4)continue;
                ChildParticle p;p.child=i;p.id=b.slot*1000+cursor++;
                p.offset=QPointF((x+cell/2.0)/childAtlasScale-childSpriteSize/2.0,(y+cell/2.0)/childAtlasScale-childSpriteSize/2.0);
                p.uv=QRectF(b.targetAtlas.x()+x,b.targetAtlas.y()+y,cell,cell);p.noise=hash(p.id);p.alpha=alpha/(cell*cell*255.0);childParticles.push_back(p);
            }
        }
        // Every nonempty 6 px source tile has a destination tile. The original
        // sprite UVs travel with it: icon strokes dissolve with their buttons.
        for(int i=0;i<int(buttons.size());i++){
            const auto &b=buttons[i];struct Cell{int x,y;double alpha;};std::vector<Cell> cells;
            for(int y=14;y<242;y+=voxelSize)for(int x=14;x<242;x+=voxelSize){
                double alpha=0;for(int yy=0;yy<voxelSize;yy++)for(int xx=0;xx<voxelSize;xx++)alpha+=qAlpha(atlas.pixel(b.sourceAtlas.x()+x+xx,b.sourceAtlas.y()+y+yy));
                if(alpha>4)cells.push_back({x,y,alpha/(voxelSize*voxelSize*255.0)});
            }
            const int rows=std::max(1,int(std::round(std::sqrt(cells.size()))));int cursor=0;double end=0;
            for(int row=0;row<rows;row++){
                const int count=int(cells.size())/rows+(row<int(cells.size())%rows?1:0);if(!count)continue;
                const double tw=44.0/count,th=44.0/rows;
                for(int col=0;col<count;col++){
                    const auto c=cells[cursor];Particle p;p.tool=i;p.id=b.slot*1000+cursor;p.source=b.radialCenter+QPointF(c.x+3-128,c.y+3-128);p.destination=b.dock+QPointF(col*tw+tw/2-22,row*th+th/2-22);
                    p.sourceUV=QRectF(b.sourceAtlas.x()+c.x,b.sourceAtlas.y()+c.y,6,6);p.targetUV=QRectF(b.targetAtlas.x()+col*tw,b.targetAtlas.y()+row*th,tw,th);p.tw=tw;p.th=th;p.u=double(col)/std::max(1,count-1);p.v=double(row)/std::max(1,rows-1);p.noise=hash(p.id);p.alpha=c.alpha;
                    particles.push_back(p);cursor++;end=std::max(end,.065+b.slot*.013+clamp(std::abs(p.u-.5)+std::abs(p.v-.5))*.09+.65);
                }
            }
            ends.push_back(end);
        }
    }
};

RadialSurface::RadialSurface(QQuickItem *parent):QQuickPaintedItem(parent),m_state(std::make_unique<State>()) {
    setFillColor(Qt::transparent);setAntialiasing(true);setFlag(ItemHasContents,true);setAcceptedMouseButtons(Qt::LeftButton|Qt::RightButton);setAcceptTouchEvents(true);setKeepMouseGrab(true);setKeepTouchGrab(true);
    m_childAnimation.setDuration(330);m_childAnimation.setStartValue(0.0);m_childAnimation.setEndValue(1.0);
    m_retireAnimation.setDuration(280);m_retireAnimation.setStartValue(0.0);m_retireAnimation.setEndValue(1.0);
    connect(&m_childAnimation,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){m_childReveal=value.toReal();update();});
    connect(&m_childAnimation,&QVariantAnimation::finished,this,[this]{emit visualBoundsChanged();});
    connect(&m_retireAnimation,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){m_retireProgress=value.toReal();update();});
    connect(&m_retireAnimation,&QVariantAnimation::finished,this,[this]{m_retiringChildren.clear();m_dirty=true;emit visualBoundsChanged();update();});
}
RadialSurface::~RadialSurface()=default;
void RadialSurface::setButtons(const QVariantList &v){if(m_buttons==v)return;m_buttons=v;m_dirty=true;emit buttonsChanged();update();}
void RadialSurface::setChildButtons(const QVariantList &v){
    if(m_children==v)return;
    const auto sameBranch=[](const QVariantList &a,const QVariantList &b){
        if(a.size()!=b.size())return false;
        for(int i=0;i<a.size();i++)if(a[i].toMap().value("id")!=b[i].toMap().value("id"))return false;
        return true;
    };
    const bool branchChanged=!sameBranch(m_children,v);
    bool atlasChanged=branchChanged;
    if(!atlasChanged)for(int i=0;i<v.size();i++){
        const auto before=m_children[i].toMap(),after=v[i].toMap();
        for(const auto *key:{"icon","color","axis","group","modifierState"})if(before.value(key)!=after.value(key)){atlasChanged=true;break;}
    }
    if(branchChanged){
        m_childAnimation.stop();
        const bool reopening=!v.isEmpty()&&sameBranch(v,m_retiringChildren);
        qreal start=0;
        if(reopening){
            // No duplicate ghost or obsolete branch survives a rapid reopen.
            start=clamp(m_retireReveal*(1-easeSmooth(m_retireProgress)));
            m_retireAnimation.stop();m_retiringChildren.clear();m_retireProgress=1;
        } else if(!m_children.isEmpty()&&!m_reducedMotion){
            m_retireAnimation.stop();m_retiringChildren=m_children;m_retireReveal=m_childReveal;
            m_retireProgress=0;m_retireAnimation.start();
        }
        m_children=v;
        if(m_reducedMotion){m_retireAnimation.stop();m_retiringChildren.clear();m_retireProgress=1;m_childReveal=1;}
        else if(!v.isEmpty()){
            m_childReveal=start;m_childAnimation.setStartValue(start);m_childAnimation.setDuration(std::max(100,int(330*(1-start))));m_childAnimation.start();
        } else m_childReveal=1;
    } else m_children=v;
    m_dirty=m_dirty||atlasChanged;
    emit childButtonsChanged();emit visualBoundsChanged();update();
}
void RadialSurface::setSelectedId(const QString &v){if(m_selectedId==v)return;m_selectedId=v;emit selectedIdChanged();update();}
void RadialSurface::setGlassEnabled(bool v){if(m_glassEnabled==v)return;m_glassEnabled=v;m_dirty=true;emit glassEnabledChanged();update();}
void RadialSurface::setBronzeTheme(bool v){if(m_bronzeTheme==v)return;m_bronzeTheme=v;m_dirty=true;emit bronzeThemeChanged();update();}
void RadialSurface::setContinuousRail(bool v){if(m_continuousRail==v)return;m_continuousRail=v;m_dirty=true;emit continuousRailChanged();update();}
void RadialSurface::setDockProgress(qreal v){v=clamp(v);if(qFuzzyCompare(m_dockProgress,v))return;m_dockProgress=v;emit dockProgressChanged();update();}
void RadialSurface::setReducedMotion(bool v){
    if(m_reducedMotion==v)return;m_reducedMotion=v;
    if(v){m_childAnimation.stop();m_retireAnimation.stop();m_childReveal=1;m_retireProgress=1;m_retiringChildren.clear();m_dirty=true;}
    emit reducedMotionChanged();emit visualBoundsChanged();update();
}
QRectF RadialSurface::visualBounds() const {
    bool found=false;double left=0,right=0,top=0,bottom=0;
    const auto include=[&](QPointF p){if(!found){left=right=p.x();top=bottom=p.y();found=true;}else{left=std::min(left,p.x());right=std::max(right,p.x());top=std::min(top,p.y());bottom=std::max(bottom,p.y());}};
    const auto branch=[&](const QVariantList &list,bool moving,bool retiring){
        for(int i=0;i<list.size();i++){
            const auto b=readChild(list[i],i);const double scale=moving?radial::GestureEngine::ChildVisualOvershoot:1;
            const double padding=(retiring||moving)?20:3;
            std::vector<QPointF> points;
            for(double radius:{childInner,childOuter})for(int n=0;n<=24;n++)points.push_back(b.from+polar(radius*scale,b.angle-childHalfAngle+2*childHalfAngle*n/24));
            if(moving)points.push_back(b.from);
            for(auto p:points){include(p-QPointF(padding,padding));include(p+QPointF(padding,padding));}
        }
    };
    branch(m_children,m_childAnimation.state()==QAbstractAnimation::Running,false);
    branch(m_retiringChildren,true,true);
    return found?QRectF(QPointF(left,top),QPointF(right,bottom)):QRectF{};
}

QString RadialSurface::hitRoot(qreal x,qreal y) const {
    if(m_dockProgress>.001&&m_dockProgress<.999)return {};
    const QPointF p(x,y);const double span=pi/3;
    for(int i=0;i<m_buttons.size();i++){
        const auto b=readButton(m_buttons[i],i);
        if(m_dockProgress>=.999){if(QRectF(b.dock-QPointF(22,22),QSizeF(44,44)).contains(p))return b.id;}
        else {const QPointF d=p-b.radialCenter;const double r=std::hypot(d.x(),d.y()),angle=std::remainder(std::atan2(d.y(),d.x())-b.angle,2*pi);if(r>=innerRadius&&r<=outerRadius&&std::abs(angle)<span/2-.035)return b.id;}
    }
    return {};
}

namespace {
struct SceneBuffers {
    Colors lights,sides,outlines;
    Textures solids;
    std::array<Textures,blendBuckets> sources,targets;
};
SceneBuffers buildScene(const auto &s,double t,const QString &selectedId,double childReveal,double retireProgress,double retireReveal) {
    Colors lights,sides,outlines;Textures solids;std::array<Textures,blendBuckets> sources,targets;
    const QColor beam("#b3dcff");const double intensity=easeSmooth(t/.07)*(1-easeSmooth((t-.85)/.15));
    if(t>0&&t<1){
        for(int i=0;i<int(s.buttons.size());i++){
            const auto &b=s.buttons[i];const double pulse=std::exp(-std::pow((t-s.ends[i])/.065,2));
            halo(lights,b.dock,34+12*(1-intensity),mix(beam,b.color,.4),intensity*.13+pulse*.19);
            const double reveal=clamp(t/.14);const QPointF from=b.center+(b.dock-b.center)*(1-reveal);
            line(lights,from,b.dock,.75,withAlpha(beam,intensity*.085));
            const double head=clamp((t-.08)/.68);const QPointF light=b.center+(b.dock-b.center)*head+QPointF(std::sin(pi*head)*5,0);halo(lights,light,10,beam,intensity*.23);
        }
        if(!s.buttons.empty()){
            double top=s.buttons[0].dock.y(),bottom=top;const double x=s.buttons[0].dock.x()-28;
            for(const auto &b:s.buttons){top=std::min(top,b.dock.y());bottom=std::max(bottom,b.dock.y());}
            line(lights,{x,top-28},{x,bottom+28},1.1,withAlpha(beam,intensity*.62));
        }
    }
    if(t<=0){for(const auto &b:s.buttons)textureQuad(solids,b.radialCenter,256,256,0,b.sourceAtlas,s.atlas.size());}
    else if(t>=1){for(const auto &b:s.buttons)textureQuad(solids,b.dock,44,44,0,b.targetAtlas,s.atlas.size());}
    else {
        for(const auto &p:s.particles){
            if(t>=s.ends[p.tool])continue;
            const auto &b=s.buttons[p.tool];const auto pose=quietPose(p,b,t);
            const int bucket=std::clamp(int(std::round(easeSmooth((pose.progress-.55)/.4)*(blendBuckets-1))),0,blendBuckets-1);
            textureQuad(sources[bucket],pose.position,pose.width,pose.height,pose.rotation,p.sourceUV,s.atlas.size());
            textureQuad(targets[bucket],pose.position,pose.width,pose.height,pose.rotation,p.targetUV,s.atlas.size());
            if(pose.lift>.15&&p.alpha>.28){
                const double hw=pose.width/2,hh=pose.height/2,z=pose.lift;
                const auto transform=[&](QPointF q){const double co=std::cos(pose.rotation),si=std::sin(pose.rotation);return pose.position+QPointF(q.x()*co-q.y()*si,q.x()*si+q.y()*co);};
                quad(sides,transform({-hw,-hh}),transform({-hw+z,-hh-z}),transform({hw+z,-hh-z}),transform({hw,-hh}),withAlpha(mix(b.color,beam,.35),p.alpha*.76));
                quad(sides,transform({hw,-hh}),transform({hw+z,-hh-z}),transform({hw+z,hh-z}),transform({hw,hh}),withAlpha(mix(b.color,QColor("#111722"),.35),p.alpha*.8));
            }
        }
        for(int i=0;i<int(s.buttons.size());i++)if(t>=s.ends[i])textureQuad(solids,s.buttons[i].dock,44,44,0,s.buttons[i].targetAtlas,s.atlas.size());
    }
    // Quiet hub orientation mark and selection halos are screen-space cues.
    if(!s.buttons.empty()&&t<.38){const auto c=s.buttons.front().radialCenter;const double a=1-easeSmooth((t-.06)/.32);for(int i=0;i<40;i++)line(outlines,c+polar(12,i*2*pi/40),c+polar(12,(i+1)*2*pi/40),.8,withAlpha(QColor("#71839e"),a*.48));halo(lights,c,20,beam,a*.04);}
    if(t<=0||t>=1)for(const auto &b:s.buttons)if(b.id==selectedId){
        if(t>=1&&s.railStyle){
            quad(lights,b.dock+QPointF(-20,-21),b.dock+QPointF(20,-21),b.dock+QPointF(20,21),b.dock+QPointF(-20,21),withAlpha(beam,.12));
            line(outlines,b.dock+QPointF(-19,-11),b.dock+QPointF(-19,11),1.8,withAlpha(beam,.9));
        }
        else if(t>=1){halo(lights,b.dock,36,b.color,.28);for(int i=0;i<36;i++)line(outlines,b.dock+polar(26,i*2*pi/36),b.dock+polar(26,(i+1)*2*pi/36),1.2,withAlpha(beam,.7));}
        else {const auto span=pi/3;for(int j=0;j<30;j++){double a0=b.angle-span/2+.04+j*(span-.08)/30;double a1=b.angle-span/2+.04+(j+1)*(span-.08)/30;line(outlines,b.radialCenter+polar(94,a0),b.radialCenter+polar(94,a1),2,withAlpha(beam,.85));}halo(lights,b.center,30,b.color,.22);}
    }
    for(const auto &p:s.childParticles){
        const auto &b=s.retiring[p.child];
        const double sourceScale=childPop(retireReveal,b.slot);
        const double local=clamp((retireProgress-p.noise*.10-b.slot*.008)/.86);
        const double flight=smoother(local),lift=std::sin(pi*local);
        const QPointF initial=b.from+(b.center-b.from+p.offset)*sourceScale;
        const QPointF radial=polar(1,b.angle),tangent=polar(1,b.angle+pi/2);
        const QPointF pos=initial+(b.from-initial)*(.40*flight)+radial*(10*lift)+tangent*((p.noise-.5)*22*flight);
        const double alpha=1-smoother((local-.08)/.92);
        const double size=childVoxelSize*sourceScale*(1-.72*flight);
        const int bucket=std::clamp(int(std::round((1-alpha)*(blendBuckets-1))),0,blendBuckets-1);
        textureQuad(sources[bucket],pos,size,size,(p.noise-.5)*.7*lift,p.uv,s.atlas.size());
        if(lift>.1&&p.alpha>.5){
            const double z=.8*lift,half=size/2;
            quad(sides,pos+QPointF(-half,-half),pos+QPointF(-half+z,-half-z),pos+QPointF(half+z,-half-z),pos+QPointF(half,-half),withAlpha(mix(b.color,beam,.5),alpha*.35));
        }
    }
    for(const auto &b:s.retiring){
        const double flash=std::sin(pi*clamp(retireProgress));
        halo(lights,b.from+polar(childInner*.4,b.angle),22,mix(beam,b.color,.4),flash*.08);
    }
    for(const auto &b:s.children){
        // Both the position and the complete wedge scale about the shared fan
        // anchor. Their angular intervals never intersect, even at peak pop.
        // Hit testing always uses the final, fixed core geometry.
        const double travel=childPop(childReveal,b.slot);
        if(travel<=.0001)continue;
        const QPointF visual=b.from+(b.center-b.from)*travel;
        const double reveal=clamp((childReveal-b.slot*.027)/.865);
        const double size=childSpriteSize*travel;
        if(reveal<1){
            const QPointF head=b.from+polar(childOuter*travel,b.angle);
            halo(lights,head,19,mix(beam,b.color,.35),std::sin(pi*reveal)*.13);
        }
        if(b.selected||b.id==selectedId)halo(lights,visual,35,b.color,.26);
        if(reveal<.23){
            const int bucket=std::clamp(int(std::round((1-easeSmooth(reveal/.23))*(blendBuckets-1))),0,blendBuckets-1);
            textureQuad(sources[bucket],visual,size,size,0,b.targetAtlas,s.atlas.size());
        } else textureQuad(solids,visual,size,size,0,b.targetAtlas,s.atlas.size());
        if(b.selected||b.id==selectedId){
            for(int i=0;i<30;i++){
                const double a0=b.angle-childHalfAngle+.014+(2*childHalfAngle-.028)*i/30;
                const double a1=b.angle-childHalfAngle+.014+(2*childHalfAngle-.028)*(i+1)/30;
                line(outlines,b.from+polar((childOuter-1)*travel,a0),b.from+polar((childOuter-1)*travel,a1),1.65,withAlpha(beam,.94));
            }
        }
    }
    return {std::move(lights),std::move(sides),std::move(outlines),std::move(solids),std::move(sources),std::move(targets)};
}
QColor straightColor(const ColorVertex &v) {
    if(!v.a)return Qt::transparent;
    return QColor(std::min(255,int(v.r)*255/int(v.a)),std::min(255,int(v.g)*255/int(v.a)),std::min(255,int(v.b)*255/int(v.a)),v.a);
}
void paintColors(QPainter &p,const Colors &vertices) {
    p.setPen(Qt::NoPen);
    for(std::size_t i=0;i+2<vertices.size();i+=3){
        const auto &a=vertices[i],&b=vertices[i+1],&c=vertices[i+2];
        const QPolygonF triangle={QPointF(a.x,a.y),QPointF(b.x,b.y),QPointF(c.x,c.y)};
        const double determinant=(b.x-a.x)*(c.y-a.y)-(c.x-a.x)*(b.y-a.y);
        const double gx=std::abs(determinant)>.00001?((b.a-a.a)*(c.y-a.y)-(c.a-a.a)*(b.y-a.y))/(255*determinant):0;
        const double gy=std::abs(determinant)>.00001?((b.x-a.x)*(c.a-a.a)-(c.x-a.x)*(b.a-a.a))/(255*determinant):0;
        const double length=gx*gx+gy*gy;
        if(length>.00000001){
            QColor tint=straightColor(a.a>=b.a&&a.a>=c.a?a:(b.a>=c.a?b:c));
            const QPointF gradient(gx,gy),start=QPointF(a.x,a.y)-gradient*(a.a/(255*length));
            QLinearGradient g(start,start+gradient/length);g.setColorAt(0,withAlpha(tint,0));g.setColorAt(1,withAlpha(tint,1));p.setBrush(g);
        }else p.setBrush(straightColor(a));
        p.drawPolygon(triangle);
    }
}
void paintTextures(QPainter &p,const Textures &vertices,const QImage &atlas,double alpha=1) {
    if(alpha<=0)return;
    p.save();p.setOpacity(alpha);
    for(std::size_t i=0;i+5<vertices.size();i+=6){
        const auto &a=vertices[i],&b=vertices[i+1],&d=vertices[i+5];
        const double tw=(b.tx-a.tx)*atlas.width(),th=(d.ty-a.ty)*atlas.height();
        if(tw<=0||th<=0)continue;
        p.save();p.setTransform(QTransform((b.x-a.x)/tw,(b.y-a.y)/tw,(d.x-a.x)/th,(d.y-a.y)/th,a.x,a.y),true);
        p.drawImage(QRectF(0,0,tw,th),atlas,QRectF(a.tx*atlas.width(),a.ty*atlas.height(),tw,th));p.restore();
    }
    p.restore();
}
} // namespace

bool RadialSurface::prepareGeometry() {
    const bool rebuild=m_dirty;
    if(rebuild){m_state->rebuild(m_buttons,m_children,m_retiringChildren,m_glassEnabled,m_continuousRail,m_bronzeTheme);m_dirty=false;}
    else for(int i=0;i<int(m_state->children.size());i++){
        const auto atlasRect=m_state->children[i].targetAtlas;
        m_state->children[i]=readChild(m_children[i],i);m_state->children[i].targetAtlas=atlasRect;
    }
    return rebuild;
}
QSGNode *RadialSurface::updatePaintNode(QSGNode *oldNode,UpdatePaintNodeData *data) {
    if(!window())return oldNode;
    // Software devices use exactly the same atlas, poses and draw batches. The
    // regular RHI path remains batched GPU geometry; no per-voxel QML items.
    if(window()->rendererInterface()->graphicsApi()==QSGRendererInterface::Software)
        return QQuickPaintedItem::updatePaintNode(oldNode,data);
    auto *node=static_cast<RenderNode*>(oldNode);
    const bool rebuilt=prepareGeometry();
    if(rebuilt||!node){delete node;node=new RenderNode(window()->createTextureFromImage(m_state->atlas));}
    auto frame=buildScene(*m_state,m_reducedMotion?(m_dockProgress<.5?0:1):m_dockProgress,m_selectedId,m_childReveal,m_retireProgress,m_retireReveal);
    upload(node->lights,frame.lights);upload(node->sides,frame.sides);upload(node->solids,frame.solids);upload(node->outlines,frame.outlines);
    for(int i=0;i<blendBuckets;i++){upload(node->sources[i],frame.sources[i]);upload(node->targets[i],frame.targets[i]);}
    return node;
}
void RadialSurface::paint(QPainter *painter) {
    prepareGeometry();
    const auto frame=buildScene(*m_state,m_reducedMotion?(m_dockProgress<.5?0:1):m_dockProgress,m_selectedId,m_childReveal,m_retireProgress,m_retireReveal);
    painter->setRenderHint(QPainter::Antialiasing);painter->setRenderHint(QPainter::SmoothPixmapTransform);
    paintColors(*painter,frame.lights);paintColors(*painter,frame.sides);
    for(int i=0;i<blendBuckets;i++){
        const double a=double(i)/(blendBuckets-1);
        paintTextures(*painter,frame.sources[i],m_state->atlas,1-a);paintTextures(*painter,frame.targets[i],m_state->atlas,a);
    }
    paintTextures(*painter,frame.solids,m_state->atlas);paintColors(*painter,frame.outlines);
}

void RadialSurface::mouseDoubleClickEvent(QMouseEvent *e){mousePressEvent(e);}
void RadialSurface::mousePressEvent(QMouseEvent *e){
    if(!m_pointerActive && e->button()==Qt::RightButton){const auto id=hitRoot(e->position().x(),e->position().y());if(!id.isEmpty()){e->accept();emit editRequested(id);return;}}
    if(m_pointerActive||e->button()!=Qt::LeftButton){e->ignore();return;}
    m_pointerActive=true;m_pointerId=-1;e->accept();emit pressed(m_pointerId,e->position().x(),e->position().y());
}
void RadialSurface::mouseMoveEvent(QMouseEvent *e){if(!m_pointerActive||m_pointerId!=-1){e->ignore();return;}e->accept();emit moved(m_pointerId,e->position().x(),e->position().y());}
void RadialSurface::mouseReleaseEvent(QMouseEvent *e){if(!m_pointerActive||m_pointerId!=-1||e->button()!=Qt::LeftButton){e->ignore();return;}m_pointerActive=false;e->accept();emit released(-1,e->position().x(),e->position().y());}
void RadialSurface::cancelPointer(){if(!m_pointerActive)return;m_pointerActive=false;m_pointerId=-1;emit canceled();}
void RadialSurface::mouseUngrabEvent(){cancelPointer();}
void RadialSurface::touchUngrabEvent(){cancelPointer();}
void RadialSurface::touchEvent(QTouchEvent *e){
    if(e->type()==QEvent::TouchCancel){cancelPointer();e->accept();return;}
    if(!m_pointerActive){
        for(const auto &p:e->points())if(p.state()==QEventPoint::State::Pressed){m_pointerActive=true;m_pointerId=p.id();e->accept();emit pressed(m_pointerId,p.position().x(),p.position().y());return;}
        e->ignore();return;
    }
    if(m_pointerId==-1){e->ignore();return;}
    // Capture one contact. Additional touches never change the active gesture.
    for(const auto &p:e->points())if(p.id()==m_pointerId){
        e->accept();if(p.state()==QEventPoint::State::Released){const int id=m_pointerId;m_pointerActive=false;m_pointerId=-1;emit released(id,p.position().x(),p.position().y());}
        else if(p.state()==QEventPoint::State::Updated)emit moved(m_pointerId,p.position().x(),p.position().y());
        return;
    }
    e->accept();
}
