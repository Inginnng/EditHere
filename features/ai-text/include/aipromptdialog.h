#pragma once
#include <QString>
#include <optional>
class QWidget;
namespace h2d {
// An empty accepted value selects the built-in prompt; cancellation changes nothing.
std::optional<QString> editAiSystemPrompt(const QString &prompt, QWidget *parent);
}
