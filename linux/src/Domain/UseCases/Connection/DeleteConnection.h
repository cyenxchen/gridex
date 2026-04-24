#pragma once

#include <memory>
#include <string>

namespace gridex {

class IConnectionRepository;
class SecretStore;

// Deletes connection record from repository + scrubs credentials from SecretStore.
class DeleteConnectionUseCase {
public:
    DeleteConnectionUseCase(IConnectionRepository* repo, SecretStore* secrets)
        : repo_(repo), secrets_(secrets) {}

    void execute(const std::string& id);

private:
    IConnectionRepository* repo_;
    SecretStore* secrets_;
};

}
