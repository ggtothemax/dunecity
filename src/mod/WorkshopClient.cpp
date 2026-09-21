#include <mod/WorkshopClient.h>
#include <Network/BoundedHttpClient.h>
#include <Network/RelayWebSocket.h>
#include <globals.h>
#include <misc/WebRuntime.h>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace Workshop {
namespace fs = std::filesystem;
namespace {
using Fields=std::map<std::string,std::string>;
Fields fields(const std::string& body,std::vector<std::string>* items=nullptr) {
    Fields out;std::istringstream in(body);std::string line;
    while(std::getline(in,line)) {
        const auto at=line.find('=');if(at==std::string::npos) throw std::runtime_error("The community service sent an invalid response.");
        auto key=line.substr(0,at),value=line.substr(at+1);
        if(key=="item"&&items)items->push_back(value);
        else if(!out.emplace(key,value).second)throw std::runtime_error("The community service repeated a response field.");
    }
    return out;
}
unsigned integer(const std::string& s) {
    if(s.empty()||s.size()>9||s.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("The community service sent an invalid version.");
    return static_cast<unsigned>(std::stoul(s));
}
bool digest(const std::string& s) { return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos; }
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;size_t pos=0;
    for(;;){auto n=s.find(',',pos);out.push_back(s.substr(pos,n==std::string::npos?n:n-pos));if(n==std::string::npos)break;pos=n+1;}return out;
}
}
class Client::Impl {
public:
    enum class Phase { None, Begin, Chunk, Commit, List, Manifest, Blob };
    std::unique_ptr<BoundedHttpClient> http=createBoundedHttpClient();
    Status status=Status::Idle;
    Phase phase=Phase::None;
    std::string message,upload,wanted;
    Revision current,result;
    std::vector<Revision> items;
    std::deque<Revision> publishQueue;
    std::deque<std::string> downloadQueue;
    fs::path stage;
    unsigned nextPage=0;
    size_t fileIndex=0;
    uint64_t offset=0,lastChunk=0;
    bool promoted=true;
    unsigned retries=0;
    Uint32 retryAt=0;
    BoundedHttpClient::Request lastRequest;
    ~Impl(){cancel();}
    void cancel(){http->cancel();if(!stage.empty()){std::error_code e;fs::remove_all(stage,e);stage.clear();}status=Status::Idle;}
    void reset(){cancel();retries=0;retryAt=0;items.clear();publishQueue.clear();downloadQueue.clear();result={};current={};message.clear();nextPage=0;}
    void send(Phase p,const std::string& route,const std::string& body) {
        phase=p;BoundedHttpClient::Request request;
        auto base=settings.network.activeDirectEndpoint();
        std::string mapped,error;
        if(base.rfind("https://",0)==0)mapped="wss://"+base.substr(8);
        else if(base.rfind("http://",0)==0)mapped="ws://"+base.substr(7);
        if(!mapped.empty()&&mapped.back()=='/')mapped.pop_back();
        if(!isAcceptableRelayUrl(mapped,settings.network.relayUseDevelopmentEndpoint,error))
            throw std::runtime_error("The community server must use a secure HTTPS address.");
        if(!base.empty()&&base.back()=='/')base.pop_back();
        request.url=base+"/v1/content/"+route;
        request.body=body;request.maxResponseBytes=BoundedHttpClient::kMaxResponseBytes;request.timeoutSeconds=60;
        lastRequest=request;http->begin(request);status=Status::Busy;
    }
    void beginPublish() {
        current=publishQueue.front();publishQueue.pop_front();fileIndex=0;offset=0;
        message="Sharing "+current.name+" v"+std::to_string(current.version)+"...";
        send(Phase::Begin,"begin","hash="+current.hash+"&manifest="+hex(current.manifest)+"&owner="+store().owner()
             +"&source="+(promoted?"manual":"host")+"&promoted="+(promoted?"1":"0"));
    }
    void published(unsigned version) {
        if(!version)throw std::runtime_error("The community service did not assign a version.");
        store().setSharedVersion(current.hash,version);current.version=version;result=current;
        if(!publishQueue.empty())beginPublish();
        else {status=Status::Succeeded;message="Shared "+result.name+" v"+std::to_string(result.version)+".";WebRuntime::syncPersistentFiles();}
    }
    void sendChunk() {
        if(fileIndex==current.files.size()){send(Phase::Commit,"commit","upload="+upload);return;}
        const auto& f=current.files[fileIndex];
        const auto n=static_cast<size_t>(std::min<uint64_t>(65536,f.size-offset));
        std::ifstream in(fs::path(current.directory)/f.path,std::ios::binary);in.seekg(offset);
        std::string data(n,'\0');in.read(data.data(),n);
        if(!in || static_cast<size_t>(in.gcount())!=n)throw std::runtime_error("The saved content could not be read.");
        lastChunk=n;
        message="Sharing "+current.name+": file "+std::to_string(fileIndex+1)+"/"+std::to_string(current.files.size())
               +" ("+std::to_string(offset/1024)+"/"+std::to_string(f.size/1024)+" KB)";
        send(Phase::Chunk,"chunk","upload="+upload+"&file="+f.hash+"&offset="+std::to_string(offset)+"&data="+hex(data));
    }
    void beginDownload() {
        auto hash=downloadQueue.front();downloadQueue.pop_front();current={};current.hash=hash;
        try {
            current=store().get(hash);
            downloaded();return;
        } catch(const std::exception&) { /* A missing or corrupt cache is downloaded again. */ }
        message="Finding the selected version...";
        send(Phase::Manifest,"manifest","hash="+hash);
    }
    void receiveBlob() {
        if(fileIndex==current.files.size()) {
            auto imported=store().importRevision(current.manifest,current.hash,current.version,stage);
            current=std::move(imported);fs::remove_all(stage);stage.clear();downloaded();return;
        }
        const auto& f=current.files[fileIndex];
        if(f.size==0){fs::create_directories((stage/f.path).parent_path());std::ofstream(stage/f.path,std::ios::binary);++fileIndex;receiveBlob();return;}
        const auto count=std::min<uint64_t>(65536,f.size-offset);
        message="Downloading "+current.name+": file "+std::to_string(fileIndex+1)+"/"+std::to_string(current.files.size());
        lastChunk=count;
        send(Phase::Blob,"blob","hash="+current.hash+"&file="+f.hash+"&offset="+std::to_string(offset)+"&count="+std::to_string(count));
    }
    void downloaded() {
        if(current.hash==wanted)result=current;
        if(!current.modHash.empty()) {
            try {store().get(current.modHash);}catch(...) {downloadQueue.push_back(current.modHash);}
        }
        if(!downloadQueue.empty())beginDownload();
        else{status=Status::Succeeded;message="Downloaded and verified "+result.name+" v"+std::to_string(result.version)+".";WebRuntime::syncPersistentFiles();}
    }
    void update() {
        if(status!=Status::Busy)return;
        if(retryAt) {
            if(static_cast<Sint32>(SDL_GetTicks()-retryAt)<0)return;
            retryAt=0;http->begin(lastRequest);
        }
        http->update();BoundedHttpClient::Result response;if(!http->poll(response))return;
        if(response.httpStatus==429 && retries++<3) {
            retryAt=SDL_GetTicks()+61000;
            message="The community server is busy. Resuming this transfer in a minute...";
            return;
        }
        retries=0;
        try {
            if(!response.transportError.empty())throw std::runtime_error("Could not reach the community server. Saved versions are still available locally.");
            std::vector<std::string> rows;auto f=fields(response.body,&rows);
            if(response.httpStatus!=200||f["status"]!="ok") {
                std::string error="The community server could not complete this request.";
                if(f.count("message")){try{error=unhex(f["message"]);}catch(...) {}}
                throw std::runtime_error(error);
            }
            switch(phase) {
            case Phase::Begin:
                if(f.count("version")){if(f["hash"]!=current.hash)throw std::runtime_error("Server revision mismatch.");published(integer(f["version"]));}
                else{upload=f["upload"];if(!digest(upload))throw std::runtime_error("Invalid upload receipt.");sendChunk();}break;
            case Phase::Chunk:
                { const auto accepted = integer(f["next"]);
                  if(accepted < offset+lastChunk || accepted > current.files[fileIndex].size)
                      throw std::runtime_error("Upload was not acknowledged completely.");
                  offset=accepted;if(offset==current.files[fileIndex].size){++fileIndex;offset=0;}sendChunk();break; }
            case Phase::Commit:
                if(f["hash"]!=current.hash)throw std::runtime_error("Server revision mismatch.");published(integer(f["version"]));break;
            case Phase::Manifest: {
                auto r=parseManifest(unhex(f["manifest"]));
                if(r.hash!=current.hash)throw std::runtime_error("Downloaded manifest does not match the requested version.");
                r.version=integer(f["version"]);if(!r.version)throw std::runtime_error("Invalid downloaded version.");
                current=std::move(r);stage=store().root()/(".download-"+newID());fs::create_directories(stage);
                fileIndex=0;offset=0;receiveBlob();break;
            }
            case Phase::Blob: {
                auto data=unhex(f["data"]);if(data.size()!=lastChunk)throw std::runtime_error("Incomplete content download.");
                const auto path=stage/current.files[fileIndex].path;fs::create_directories(path.parent_path());
                std::ofstream out(path,std::ios::binary|std::ios::app);out.write(data.data(),data.size());out.close();
                if(!out)throw std::runtime_error("Could not save downloaded content. Check free disk space.");
                offset+=data.size();if(offset==current.files[fileIndex].size){++fileIndex;offset=0;}receiveBlob();break;
            }
            case Phase::List:
                for(const auto& row:rows){auto p=split(row);if(p.size()!=7)throw std::runtime_error("Invalid community listing.");
                    Revision r;r.kind=p[0];r.id=p[1];r.version=integer(p[2]);r.hash=p[3];r.name=unhex(p[4]);r.base=unhex(p[5]);r.modHash=p[6];
                    if((r.kind!="map"&&r.kind!="mod")||!digest(r.hash)||r.name.size()>128)throw std::runtime_error("Invalid community item.");items.push_back(r);}
                nextPage=integer(f["next"]);status=Status::Succeeded;message=items.empty()?"No shared content yet.":"Select a version to download.";break;
            default:break;
            }
        } catch(const std::exception& e){http->cancel();status=Status::Failed;message=e.what();}
    }
};
Client::Client():impl_(std::make_unique<Impl>()){}
Client::~Client()=default;
void Client::publish(const Revision& revision,bool promoted){impl_->reset();impl_->promoted=promoted;
    try{auto r=store().get(revision.hash);if(!r.modHash.empty())impl_->publishQueue.push_back(store().get(r.modHash));impl_->publishQueue.push_back(r);impl_->beginPublish();}
    catch(const std::exception& e){impl_->status=Status::Failed;impl_->message=e.what();}}
void Client::download(const std::string& hash){impl_->reset();impl_->wanted=hash;
    if(!digest(hash)){impl_->status=Status::Failed;impl_->message="Invalid content checksum.";return;}
    try{impl_->downloadQueue.push_back(hash);impl_->beginDownload();}
    catch(const std::exception& e){impl_->status=Status::Failed;impl_->message=e.what();}}
void Client::browse(const std::string& kind,unsigned cursor){impl_->reset();
    if(!kind.empty()&&kind!="map"&&kind!="mod")return;
    impl_->message="Loading community content...";
    try{impl_->send(Impl::Phase::List,"list","kind="+kind+"&cursor="+std::to_string(cursor));}
    catch(const std::exception& e){impl_->status=Status::Failed;impl_->message=e.what();}}
void Client::update(){impl_->update();}
void Client::cancel(){impl_->cancel();}
Client::Status Client::status()const{return impl_->status;}
const std::string& Client::message()const{return impl_->message;}
const Revision& Client::result()const{return impl_->result;}
const std::vector<Revision>& Client::items()const{return impl_->items;}
unsigned Client::nextPage()const{return impl_->nextPage;}
void queuePublish(const Revision& revision) {
    if(!digest(revision.hash)) throw std::runtime_error("Invalid queued revision.");
    const auto dir=store().root()/"outbox";fs::create_directories(dir);
    std::ofstream out(dir/revision.hash,std::ios::binary);out << revision.hash;out.close();
    if(!out)throw std::runtime_error("Could not queue content sharing.");
    WebRuntime::syncPersistentFiles();
}
void updatePublications() {
    static std::unique_ptr<Client> client;
    static std::string hash;
    static Uint32 retryAt=0;
    const auto now=SDL_GetTicks();
    if(retryAt && static_cast<Sint32>(now-retryAt)<0)return;
    try {
        if(client && client->status()==Client::Status::Busy)client->update();
        if(client && client->status()==Client::Status::Failed){
            SDL_Log("Workshop queued sharing: %s",client->message().c_str());
            client.reset();hash.clear();retryAt=now+60000;return;
        }
        if(client && client->status()==Client::Status::Succeeded){
            fs::remove(store().root()/"outbox"/hash);client.reset();hash.clear();WebRuntime::syncPersistentFiles();
        }
        if(client)return;
        auto dir=store().root()/"outbox";if(!fs::is_directory(dir)){retryAt=now+5000;return;}
        for(const auto& entry:fs::directory_iterator(dir)) {
            auto candidate=entry.path().filename().string();if(!digest(candidate))continue;
            auto revision=store().get(candidate);hash=candidate;client=std::make_unique<Client>();client->publish(revision,false);return;
        }
        retryAt=now+5000;
    }catch(const std::exception& e){SDL_Log("Workshop queued sharing: %s",e.what());client.reset();hash.clear();retryAt=now+60000;}
}

}
