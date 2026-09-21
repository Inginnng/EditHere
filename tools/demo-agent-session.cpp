// Deterministic demo capture. Uses a unique local server; never touches the user's app.
#include "agentprotocol.h"
#include "agentserver.h"
#include "controller.h"
#include "ui.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <stdexcept>
using namespace h2d;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void write(const QString &path, const QByteArray &bytes) { QFile f(path); require(f.open(QIODevice::WriteOnly), "write failed"); require(f.write(bytes)==bytes.size(), "short write"); }
int main(int argc,char **argv) {
    QApplication app(argc,argv); app.setApplicationName("EditHere Demo Recorder"); app.setQuitOnLastWindowClosed(false);
    if (argc!=3) return 2;
    const QString root=QString::fromLocal8Bit(argv[1]), out=QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(out+"/native");
    for (const auto &font : {"segoeui.ttf", "msyh.ttc"}) QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/"+font);
    applyTheme(ThemeMode::Light);
    auto settings=defaultSettings(); settings.shortcuts["capture"]={}; settings.captureOnStartup=false; settings.checkUpdatesOnStartup=false;
    Controller controller(nullptr,settings,out+"/isolated-settings.ini");
    auto tray=controller.findChild<QSystemTrayIcon *>("edithereTray");
    auto editor=qobject_cast<Editor *>(tray->contextMenu()->parentWidget()); require(editor,"no editor");
    AgentServer server(controller); const auto name="EditHere-demo-"+uniqueId(); require(server.listen(name),"listen failed");
    const auto input=root+"/artifacts/promo-narrated/charts/original.png", output=out+"/feedback.json";
    require(!QFileInfo::exists(output),"Choose a fresh output folder; feedback must not be overwritten");
    const QJsonObject request{{"protocol",1},{"command","annotate"},{"input",input},{"output",output},{"timeout",120},{"embed",true}};
    write(out+"/request.json",QJsonDocument(request).toJson());
    QLocalSocket client; client.connectToServer(name); require(client.waitForConnected(1000),"connect failed"); client.write(encodeAgentMessage(request));client.flush();
    for(int i=0;i<100&&!controller.handleAgentRequest({{"command","status"}})["agentSession"].toBool();i++)QTest::qWait(10);
    require(controller.handleAgentRequest({{"command","status"}})["agentSession"].toBool(),"session missing");
    editor->resize(1520,900); editor->fit();QTest::qWait(200);
    QJsonArray frames; int number=0;
    auto capture=[&](double duration,QPoint cursor=QPoint(-100,-100),const QString &action="hold") {
        QTest::qWait(35); const auto file=QString("native/%1.png").arg(number++,3,10,QChar('0'));require(editor->grab().save(out+"/"+file),"capture failed");
        frames.append(QJsonObject{{"file",file},{"duration",duration},{"cursor",QJsonArray{cursor.x(),cursor.y()}},{"action",action}});
    };
    auto button=[&](const char *id){auto p=editor->findChild<QPushButton *>(id);require(p,"button missing");return p;};
    auto pointOf=[&](QWidget *w){return w->mapTo(editor,w->rect().center());};
    capture(3.0);
    auto pointButton=button("mode_point");capture(.5,pointOf(pointButton),"move"); QTest::mouseClick(pointButton,Qt::LeftButton);capture(.5,pointOf(pointButton),"click");
    auto canvas=editor->canvas(); const QPoint target(qRound(732*canvas->zoom()),qRound(326*canvas->zoom()));
    capture(.6,canvas->mapTo(editor,target),"move");QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,target);QTest::qWait(120);
    capture(.6,canvas->mapTo(editor,target),"click");
    auto edits=editor->findChildren<QPlainTextEdit *>();QPlainTextEdit *note=nullptr;for(auto e:edits)if(e->isVisible()&&e->objectName().startsWith("noteText_"))note=e;require(note,"note editor missing");
    note->setFocus(); const QString text=QString::fromUtf8("6 月实际营收应为 84 万元，请按原始数据修正。");
    for(int i=0;i<text.size();i++){note->insertPlainText(text.mid(i,1));capture(.10,pointOf(note),"typing");}
    QTest::keyClick(note,Qt::Key_Return,Qt::ControlModifier);capture(2.5,pointOf(note),"hold");
    require(!QFileInfo::exists(output),"feedback published before finish");
    auto finish=button("agentFinish");capture(1.0,pointOf(finish),"move");capture(1.0,pointOf(finish),"hold");QTest::mouseClick(finish,Qt::LeftButton);capture(.5,pointOf(finish),"click");
    QByteArray buffer;QJsonObject reply;QString error; AgentFrameState state=AgentFrameState::Incomplete;
    for(int i=0;i<100&&state==AgentFrameState::Incomplete;i++){QTest::qWait(10);buffer+=client.readAll();state=takeAgentMessage(buffer,reply,error);}
    require(state==AgentFrameState::Complete&&reply["ok"].toBool(),"completion reply missing");require(QFileInfo::exists(output),"feedback missing");
    write(out+"/reply.json",QJsonDocument(reply).toJson());
    capture(2.0);
    const QJsonObject metadata{{"method","Qt QWidget grab and QTest inputs; unmodified Controller and AgentServer; unique socket"},{"aiExecution","Illustrative handoff only, not an AI execution recording"},{"frames",frames},{"width",editor->width()},{"height",editor->height()},{"socket",name},{"reply",reply},{"feedbackAbsentBeforeFinish",true}};
    write(out+"/native-timeline.json",QJsonDocument(metadata).toJson());editor->hide();return 0;
}
