#pragma once
#include <QLocalServer>
namespace h2d {
class Controller;
class AgentServer final : public QLocalServer {
  public:
    explicit AgentServer(Controller &controller, QObject *parent = nullptr);
    bool start();
};
} // namespace h2d
