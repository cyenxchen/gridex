#include "Domain/UseCases/Connection/DeleteConnection.h"

#include "Core/Errors/GridexError.h"
#include "Data/Keychain/SecretStore.h"
#include "Domain/Repositories/IConnectionRepository.h"

namespace gridex {

void DeleteConnectionUseCase::execute(const std::string& id) {
    if (!repo_) throw InternalError("DeleteConnectionUseCase: null repository");
    repo_->remove(id);
    if (secrets_) {
        // Best-effort: ignore failures so a vanished keyring doesn't block delete.
        try { secrets_->removePassword(id); } catch (...) {}
        try { secrets_->remove("ssh.password." + id); } catch (...) {}
    }
}

}
