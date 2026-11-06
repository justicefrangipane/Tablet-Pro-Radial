#include "DesktopShell.h"
#include "StudioController.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QDesktopServices>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QQuickWindow>
#include <QTextEdit>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

static void initializeBrandResources() { Q_INIT_RESOURCE(radial_brand); }
static void initializeHistoryResources() { Q_INIT_RESOURCE(radial_history); }

namespace tabletpro {
QIcon DesktopShell::icon(bool update) {
    static const QIcon brand=[] { initializeBrandResources(); return QIcon(":/icons/radial.ico"); }();
    if(!update)return brand;
    QIcon result;
    for(const int size:{16,20,24,32,40,48,64,96,128,256}) {
        QPixmap pix=brand.pixmap(size,size);QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);
        p.scale(size/32.0,size/32.0);p.setPen(QPen(QColor("#38281e"),1.5));
        p.setBrush(QColor("#ffd08a"));p.drawEllipse(QPointF(26,7),4.5,4.5);
        p.end();
        result.addPixmap(pix);
    }
    return result;
}
DesktopShell::DesktopShell(StudioController* studio,QQuickWindow* overlay,QObject* parent,bool testMode)
    :QObject(parent),m_studio(studio),m_overlay(overlay),m_updates(this),m_tray(icon(),this) {
    m_trayAvailable=QSystemTrayIcon::isSystemTrayAvailable();
    auto* title=m_menu.addAction("Tablet Pro Radial "+QCoreApplication::applicationVersion());title->setEnabled(false);
    m_menu.addSeparator();
    auto* show=m_menu.addAction("Show radial menu");show->setObjectName("trayToggle");
    connect(show,&QAction::triggered,this,&DesktopShell::toggleOverlay);
    connect(overlay,&QWindow::visibleChanged,this,[show](bool visible){show->setText(visible?"Hide radial menu":"Show radial menu");});
    auto* edit=m_menu.addAction("Open editor");edit->setObjectName("trayEditor");connect(edit,&QAction::triggered,studio,&StudioController::showEditor);
    m_menu.addSeparator();
    auto* updates=m_menu.addAction("Check for updates...");updates->setObjectName("trayUpdates");connect(updates,&QAction::triggered,this,&DesktopShell::showUpdates);
    auto* history=m_menu.addAction("Update log");history->setObjectName("trayUpdateLog");connect(history,&QAction::triggered,this,&DesktopShell::showUpdateLog);
    m_menu.addSeparator();
    auto* quit=m_menu.addAction("Quit Tablet Pro Radial");quit->setObjectName("trayQuit");connect(quit,&QAction::triggered,this,&DesktopShell::requestQuit);
    m_tray.setContextMenu(&m_menu);m_tray.setToolTip("Tablet Pro Radial "+QCoreApplication::applicationVersion()+"\nRight-click for options or Quit");
    connect(&m_tray,&QSystemTrayIcon::activated,this,[this](auto reason){if(reason==QSystemTrayIcon::Trigger)toggleOverlay();});
    connect(&m_tray,&QSystemTrayIcon::messageClicked,this,&DesktopShell::showUpdates);
    connect(&m_updates,&UpdateChecker::changed,this,[this,updates]{
        updates->setText(m_updates.ready()?"Install downloaded update...":m_updates.available()?"Update available...":"Check for updates...");
        m_tray.setIcon(icon(m_updates.available()));
    });
    connect(&m_updates,&UpdateChecker::updateFound,this,[this]{
        if(m_tray.isVisible())m_tray.showMessage("Tablet Pro Radial update","Version "+m_updates.release().version+" is available. Click to review.",QSystemTrayIcon::Information);
    });
    if(!testMode) {
        m_tray.show();
        connect(&m_checkTimer,&QTimer::timeout,this,[this]{m_updates.check(false);});
        m_checkTimer.start(30*60*1000);
        QTimer::singleShot(8000,this,[this]{m_updates.check(false);});
    }
}
DesktopShell::~DesktopShell(){m_tray.hide();delete m_dialog.data();delete m_history.data();}
void DesktopShell::toggleOverlay() { if(m_overlay->isVisible())m_studio->hideOverlay();else m_studio->showOverlay(); }
void DesktopShell::requestQuit(){m_installPending=false;emit quitRequested();}
void DesktopShell::finishQuit(){
    if(m_installPending&&!m_updates.launchInstaller()){m_installPending=false;showUpdates();return;}
    m_tray.hide();emit exitApproved();
}
void DesktopShell::showUpdateLog() {
    if(m_history){m_history->show();m_history->raise();m_history->activateWindow();return;}
    static const auto releases=[] {
        initializeHistoryResources();QFile file(":/history/update-history.json");
        return file.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(file.readAll()).object()["releases"].toArray():QJsonArray{};
    }();
    auto* dialog=new QDialog;dialog->setAttribute(Qt::WA_DeleteOnClose);m_history=dialog;
    dialog->setObjectName("updateHistoryDialog");dialog->setWindowTitle("Tablet Pro Radial — Update log");dialog->setWindowIcon(icon());
    dialog->resize(610,520);dialog->setMinimumSize(400,380);
    dialog->setStyleSheet("QDialog { background:#29211c; color:#ffe5bd; } QLabel { color:#ffe5bd; }"
        "QComboBox { background:#403126; color:#ffe5bd; border:1px solid #806046; border-radius:6px; padding:8px; }"
        "QComboBox QAbstractItemView { background:#403126; color:#ffe5bd; selection-background-color:#715139; selection-color:#fff0d6; }"
        "QTextBrowser { background:transparent; color:#f2e4d3; border:0; }"
        "QPushButton { background:#403126; color:#ffe5bd; border:1px solid #806046; border-radius:6px; padding:8px 14px; }"
        "QPushButton:hover { background:#5b422f; } QPushButton:focus { border:1px solid #ffe5bd; }");
    auto* layout=new QVBoxLayout(dialog);layout->setContentsMargins(24,22,24,20);layout->setSpacing(14);
    auto* heading=new QLabel("Update log");auto font=heading->font();font.setPointSize(20);font.setWeight(QFont::DemiBold);heading->setFont(font);layout->addWidget(heading);
    auto* installed=new QLabel("Installed version: "+m_updates.currentVersion());installed->setObjectName("historyInstalledVersion");layout->addWidget(installed);
    auto* versions=new QComboBox;versions->setObjectName("historyVersionPicker");versions->setAccessibleName("Version history");
    int current=0;
    for(int i=0;i<releases.size();++i){
        const auto release=releases[i].toObject();const auto version=release["version"].toString();
        const bool active=version==m_updates.currentVersion();if(active)current=i;
        const auto date=QLocale().toString(QDate::fromString(release["date"].toString(),Qt::ISODate),"MMM d, yyyy");
        versions->addItem(version+"  ·  "+date+(active?"  ·  Installed":""));
    }
    layout->addWidget(versions);
    auto* content=new QTextBrowser;content->setObjectName("historyContent");content->setAccessibleName("Changes in the selected version");
    content->setOpenExternalLinks(false);content->setOpenLinks(false);
    content->document()->setDefaultStyleSheet("h2 { font-size:19px; color:#ffe5bd; margin-top:4px; margin-bottom:16px; } h3 { font-size:14px; color:#e4bb84; margin-top:16px; margin-bottom:6px; } li { margin-bottom:9px; }");
    layout->addWidget(content,1);
    const auto render=[content](int index){
        if(index<0||index>=releases.size()){content->setPlainText("Update history is not available in this build.");return;}
        const auto release=releases[index].toObject();
        QString html="<h2>"+release["title"].toString().toHtmlEscaped()+"</h2>";
        for(const auto* key:{"new","improved","fixed"}){
            const auto changes=release[key].toArray();if(changes.isEmpty())continue;
            QString label=QString::fromLatin1(key);label[0]=label[0].toUpper();html+="<h3>"+label+"</h3><ul>";
            for(const auto& change:changes)html+="<li>"+change.toString().toHtmlEscaped()+"</li>";
            html+="</ul>";
        }
        content->setHtml(html);content->moveCursor(QTextCursor::Start);
    };
    connect(versions,qOverload<int>(&QComboBox::currentIndexChanged),dialog,render);
    versions->setCurrentIndex(current);render(releases.isEmpty()?-1:current);
    auto* footer=new QHBoxLayout;auto* offline=new QLabel("Included with the app · available offline");offline->setWordWrap(true);
    offline->setStyleSheet("color:#c4b3a1;");footer->addWidget(offline,1);
    auto* close=new QPushButton("Close");connect(close,&QPushButton::clicked,dialog,&QDialog::close);footer->addWidget(close);layout->addLayout(footer);
    dialog->show();dialog->raise();dialog->activateWindow();
}
void DesktopShell::showUpdates() {
    if(m_dialog){m_dialog->show();m_dialog->raise();m_dialog->activateWindow();return;}
    auto* dialog=new QDialog;dialog->setAttribute(Qt::WA_DeleteOnClose);m_dialog=dialog;
    dialog->setWindowTitle("Tablet Pro Radial — Updates");dialog->setWindowIcon(icon());dialog->resize(500,390);
    auto* layout=new QVBoxLayout(dialog);layout->setContentsMargins(22,20,22,20);layout->setSpacing(12);
    auto* heading=new QLabel("Tablet Pro Radial "+m_updates.currentVersion());auto font=heading->font();font.setPointSize(15);font.setBold(true);heading->setFont(font);layout->addWidget(heading);
    auto* sourceLabel=new QLabel("GitHub release repository");layout->addWidget(sourceLabel);
    auto* row=new QHBoxLayout;auto* source=new QLineEdit(m_updates.repository());source->setPlaceholderText("owner/repository or GitHub URL");source->setAccessibleName("GitHub release repository");
    auto* check=new QPushButton("Check now");row->addWidget(source,1);row->addWidget(check);layout->addLayout(row);
    auto* automatic=new QCheckBox("Check automatically for updates");automatic->setChecked(m_updates.automatic());layout->addWidget(automatic);
    auto* status=new QLabel;status->setWordWrap(true);status->setTextFormat(Qt::PlainText);layout->addWidget(status);
    auto* last=new QLabel;layout->addWidget(last);
    auto* history=new QPushButton("Update log");history->setObjectName("updatesHistoryButton");history->setAccessibleName("Read version-by-version update history");
    connect(history,&QPushButton::clicked,this,&DesktopShell::showUpdateLog);layout->addWidget(history,0,Qt::AlignLeft);
    auto* notes=new QTextEdit;notes->setReadOnly(true);notes->setMinimumHeight(90);layout->addWidget(notes,1);
    auto* progress=new QProgressBar;progress->setRange(0,100);layout->addWidget(progress);
    auto* actions=new QHBoxLayout;auto* page=new QPushButton("Release notes");auto* download=new QPushButton("Download update");auto* cancel=new QPushButton("Cancel download");auto* install=new QPushButton("Install update");
    actions->addWidget(page);actions->addStretch();actions->addWidget(cancel);actions->addWidget(download);actions->addWidget(install);layout->addLayout(actions);
    const auto refresh=[this,source,check,automatic,status,last,notes,progress,page,download,cancel,install]{
        const bool busy=m_updates.busy();source->setEnabled(!busy&&!m_updates.ready());check->setEnabled(!busy&&!m_updates.ready());
        automatic->setChecked(m_updates.automatic());status->setText(m_updates.status());last->setText(m_updates.lastChecked());
        notes->setPlainText(m_updates.release().notes);notes->setVisible(!m_updates.release().notes.isEmpty());
        progress->setValue(m_updates.progress());progress->setVisible(busy&&m_updates.available());
        cancel->setVisible(busy);cancel->setText(m_updates.available()?"Cancel download":"Cancel check");
        page->setVisible(!m_updates.release().page.isEmpty());download->setVisible(m_updates.available()&&!m_updates.release().download.isEmpty()&&!m_updates.ready());download->setEnabled(!busy);
        install->setVisible(m_updates.ready());
    };
    connect(&m_updates,&UpdateChecker::changed,dialog,refresh);
    connect(automatic,&QCheckBox::toggled,&m_updates,&UpdateChecker::setAutomatic);
    connect(check,&QPushButton::clicked,dialog,[this,source]{if(m_updates.setRepository(source->text()))m_updates.check();});
    connect(source,&QLineEdit::returnPressed,check,&QPushButton::click);
    connect(download,&QPushButton::clicked,&m_updates,&UpdateChecker::download);
    connect(cancel,&QPushButton::clicked,&m_updates,&UpdateChecker::cancel);
    connect(page,&QPushButton::clicked,dialog,[this]{QDesktopServices::openUrl(m_updates.release().page);});
    connect(install,&QPushButton::clicked,dialog,[this]{m_installPending=true;emit quitRequested();});
    refresh();dialog->show();dialog->raise();dialog->activateWindow();
    if(!m_updates.repository().isEmpty()&&!m_updates.busy()&&!m_updates.ready())m_updates.check();
}
}
