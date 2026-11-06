#include "DisplayRecovery.h"
#include "MenuSession.h"
#include "platform/BackdropEffect.h"
#include <QGuiApplication>
#include <QScreen>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tabletpro {
DisplayRecovery::DisplayRecovery(QQuickWindow* window, MenuSession* session, BackdropEffect* backdrop, QObject* parent)
    : QObject(parent),m_window(window),m_session(session),m_backdrop(backdrop)
{
    m_timer.setSingleShot(true);m_timer.setInterval(180);
    connect(&m_timer,&QTimer::timeout,this,&DisplayRecovery::recover);
    // Release old device resources when the overlay is hidden during recovery.
    window->setPersistentGraphics(false);window->setPersistentSceneGraph(false);
    connect(window,&QWindow::screenChanged,this,[this]{requestRecovery();});
    connect(window,&QQuickWindow::sceneGraphError,this,[this]{requestRecovery();});
    for(auto* screen:QGuiApplication::screens())watchScreen(screen);
    connect(qGuiApp,&QGuiApplication::screenAdded,this,[this](QScreen* screen){watchScreen(screen);requestRecovery();});
    connect(qGuiApp,&QGuiApplication::screenRemoved,this,[this]{requestRecovery();});
    connect(qGuiApp,&QGuiApplication::primaryScreenChanged,this,[this]{requestRecovery();});
    qGuiApp->installNativeEventFilter(this);
}
DisplayRecovery::~DisplayRecovery(){qGuiApp->removeNativeEventFilter(this);}
void DisplayRecovery::watchScreen(QScreen* screen){
    connect(screen,&QScreen::geometryChanged,this,[this]{requestRecovery();});
    connect(screen,&QScreen::availableGeometryChanged,this,[this]{requestRecovery();});
    connect(screen,&QScreen::logicalDotsPerInchChanged,this,[this]{requestRecovery();});
    connect(screen,&QScreen::physicalDotsPerInchChanged,this,[this]{requestRecovery();});
}
void DisplayRecovery::requestRecovery(){
    if(m_recovering || !m_window)return;
    // Hide a potentially stale native Glass surface immediately. The actual
    // overlay rebuild waits for a burst of display-change messages to settle.
    m_backdrop->setEnabled(false);
    m_session->releaseModifiers();
    m_timer.start();
}
void DisplayRecovery::recover(){
    if(!m_window || !m_window->isVisible())return; // Never resurrect a dismissed menu.
    QScreen* screen=m_window->screen();
    if(!screen || !QGuiApplication::screens().contains(screen))screen=QGuiApplication::primaryScreen();
    if(!screen)return;
    m_recovering=true;
    const QPointF globalCenter=m_window->position()+m_session->radialCenter();
    m_session->cancel();m_backdrop->setEnabled(false);
    m_window->hide();m_window->releaseResources();
    m_window->setScreen(screen);
    const auto available=screen->availableGeometry();
    m_window->setGeometry(available);
    m_session->setViewport(available.width(),available.height());
    if(!m_session->docked())m_session->moveRadialCenter(globalCenter.x()-available.x(),globalCenter.y()-available.y());
    // Force Qt to rebuild the HWND region even when the logical shape did not
    // change. Windows may now use a different number of physical pixels.
    m_window->setMask({});
    emit refreshMask(); // Install the body mask before showing any pixels.
    m_window->show(); // Overlay flags guarantee non-activation.
    m_recovering=false;
    emit refreshMask(); // Re-enable Glass only after the small mask is restored.
    m_window->requestUpdate();
}
bool DisplayRecovery::nativeEventFilter(const QByteArray& type,void* message,qintptr*){
#ifdef Q_OS_WIN
    if((type=="windows_generic_MSG"||type=="windows_dispatcher_MSG")&&message){
        const auto* msg=static_cast<MSG*>(message);
        if(msg->message==WM_DISPLAYCHANGE || msg->message==WM_DWMCOMPOSITIONCHANGED ||
           (msg->message==WM_DPICHANGED && m_window && msg->hwnd==reinterpret_cast<HWND>(m_window->winId())))requestRecovery();
    }
#else
    Q_UNUSED(type) Q_UNUSED(message)
#endif
    return false;
}
}
