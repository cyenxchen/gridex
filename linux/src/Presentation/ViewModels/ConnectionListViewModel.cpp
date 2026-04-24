#include "Presentation/ViewModels/ConnectionListViewModel.h"

#include <QString>

#include "Core/Errors/GridexError.h"
#include "Domain/Repositories/IConnectionRepository.h"

namespace gridex {

ConnectionListViewModel::ConnectionListViewModel(IConnectionRepository* repo, QObject* parent)
    : QObject(parent), repo_(repo) {}

std::optional<ConnectionConfig> ConnectionListViewModel::find(const std::string& id) const {
    for (const auto& c : connections_) {
        if (c.id == id) return c;
    }
    return std::nullopt;
}

void ConnectionListViewModel::reload() {
    if (!repo_) return;
    try {
        connections_ = repo_->fetchAll();
        emit connectionsChanged();
    } catch (const GridexError& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
    }
}

void ConnectionListViewModel::upsert(const ConnectionConfig& config) {
    if (!repo_) return;
    try {
        repo_->save(config);
        reload();
    } catch (const GridexError& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
    }
}

void ConnectionListViewModel::remove(const std::string& id) {
    if (!repo_) return;
    try {
        repo_->remove(id);
        reload();
    } catch (const GridexError& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
    }
}

}
