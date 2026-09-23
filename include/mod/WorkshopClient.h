#ifndef DUNECITY_WORKSHOP_CLIENT_H
#define DUNECITY_WORKSHOP_CLIENT_H
#include <mod/Workshop.h>
#include <memory>

namespace Workshop {
// Nonblocking chunked transfer. update() is driven by a menu/game loop.
class Client {
public:
    enum class Status { Idle, Busy, Succeeded, Failed };
    Client();
    ~Client();
    void publish(const Revision& revision, bool promoted = true);
    void download(const std::string& hash);
    void browse(const std::string& kind = {}, unsigned cursor = 0);
    void browseMaps(unsigned cursor = 0);
    void update();
    void cancel();
    Status status() const;
    const std::string& message() const;
    const Revision& result() const;
    const std::vector<Revision>& items() const;
    unsigned nextPage() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
void queuePublish(const Revision& revision);
void updatePublications();
// Modal progress uses the normal SDL loop; cancellation never publishes a partial revision.
bool publishWithProgress(const Revision& revision, bool promoted = true);
bool downloadWithProgress(const std::string& hash);
}
#endif
