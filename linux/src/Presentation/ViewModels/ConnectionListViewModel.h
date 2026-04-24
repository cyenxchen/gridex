#pragma once

#include <QObject>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Core/Models/Database/ConnectionConfig.h"

namespace gridex {

class IConnectionRepository;

// QObject holding the list of saved connections. Pure read/write facade over
// IConnectionRepository — UI observers listen to signals and refresh.
class ConnectionListViewModel : public QObject {
    Q_OBJECT

public:
    explicit ConnectionListViewModel(IConnectionRepository* repo, QObject* parent = nullptr);

    [[nodiscard]] const std::vector<ConnectionConfig>& connections() const noexcept { return connections_; }
    [[nodiscard]] std::optional<ConnectionConfig> find(const std::string& id) const;

    // Fetches from repository and emits connectionsChanged.
    void reload();

    // Upsert: saves to repo then emits connectionsChanged.
    void upsert(const ConnectionConfig& config);

    // Removes and emits connectionsChanged.
    void remove(const std::string& id);

signals:
    void connectionsChanged();
    void errorOccurred(const QString& message);

private:
    IConnectionRepository* repo_;
    std::vector<ConnectionConfig> connections_;
};

}
