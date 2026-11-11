#pragma once
#include <QLocalServer>
#include <QByteArray>
namespace h2d {
class Controller;
class AgentServer final : public QLocalServer {
  public:
    explicit AgentServer(Controller &controller, const QByteArray &token, QObject *parent = nullptr);
    bool start();
  private:
    QByteArray token_;
};
} // namespace h2d
