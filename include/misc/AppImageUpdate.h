#pragma once
#include <string>
namespace AppImageUpdate {
// Staging and the original must be regular files owned by this user, on the
// same filesystem. Returns a uniquely named backup; never touches user data.
std::string replace(const std::string& original, const std::string& staged,
                    const std::string& expectedHash);
}
