#include <Network/UpdateManifest.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifndef _WIN32
#include <misc/AppImageUpdate.h>
#include <unistd.h>
#endif

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<typename F> void rejects(F f) {
    bool rejected = false; try { f(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid update was accepted");
}
std::string b64(const unsigned char* bytes, size_t size) {
    std::string s(4*((size+2)/3)+1, '\0');
    s.resize(EVP_EncodeBlock(reinterpret_cast<unsigned char*>(s.data()), bytes, static_cast<int>(size)));
    return s;
}
std::string sign(EVP_PKEY* key, const std::string& data) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new(); unsigned char signature[64]; size_t size = sizeof(signature);
    require(EVP_DigestSignInit(ctx,nullptr,nullptr,nullptr,key)==1, "Sign init failed");
    require(EVP_DigestSign(ctx,signature,&size,reinterpret_cast<const unsigned char*>(data.data()),data.size())==1,"Sign failed");
    EVP_MD_CTX_free(ctx); return data+b64(signature,size)+"\n";
}
}
int main() {
    try {
        using namespace DesktopUpdates;
        unsigned char seed[32], raw[32]; size_t size=sizeof(raw);
        require(RAND_bytes(seed,sizeof(seed))==1,"RNG failed");
        EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519,nullptr,seed,sizeof(seed));
        require(key && EVP_PKEY_get_raw_public_key(key,raw,&size)==1,"Key failed");
        const auto publicKey=b64(raw,size);
        const std::string payload="DuneCityUpdate1\n1.0.731\nlinux-x86_64\nhttps://example.com/game.AppImage\n10\n"+std::string(64,'a')+"\n";
        const auto valid=sign(key,payload);
        require(verifyManifest(valid,publicKey,"linux-x86_64").version=="1.0.731","Valid update rejected");
        for (std::size_t i=0;i<valid.size()-1;++i) {
            auto tampered=valid; tampered[i]^=1;
            rejects([&]{verifyManifest(tampered,publicKey,"linux-x86_64");});
        }
        rejects([&]{verifyManifest(valid,publicKey,"windows-x64");});
        raw[0]^=1; rejects([&]{verifyManifest(valid,b64(raw,size),"linux-x86_64");});
        for (const auto& fromTo : std::vector<std::pair<std::string,std::string>>{
            {"https://", "http://"}, {"\n10\n","\n536870913\n"},
            {"\n10\n","\n-1\n"}, {"1.0.731","1.0.731-evil"},
            {"1.0.731","01.0.731"}, {"example.com","user:password@example.com"}}) {
            auto invalid=payload; auto i=invalid.find(fromTo.first); invalid.replace(i,fromTo.first.size(),fromTo.second);
            rejects([&]{verifyManifest(sign(key,invalid),publicKey,"linux-x86_64");});
        }
        rejects([&]{verifyManifest(valid+"extra\n",publicKey,"linux-x86_64");});
        rejects([&]{verifyManifest(std::string(5000,'x'),publicKey,"linux-x86_64");});
        require(compareVersions("1.0.731","1.0.730")>0,"Upgrade comparison failed");
        require(compareVersions("1.0.730","1.0.731")<0,"Downgrade comparison failed");
        require(compareVersions("1.0.731","1.0.731")==0,"Current comparison failed");
        for (auto v : {"1.0.731.","1.0", "1.0.731.1", "-1.0.1", "1.0.9999999"}) rejects([&]{compareVersions(v,"1.0.731");});
        EVP_PKEY_free(key);
#ifndef _WIN32
        char temp[]="/tmp/dunecity-updater-test-XXXXXX";
        require(mkdtemp(temp)!=nullptr,"Temp directory failed");
        const std::filesystem::path root(temp), old=root/"Game.AppImage", next=root/"staged", save=root/"save.dls";
        std::ofstream(old)<<"previous game"; std::ofstream(next)<<"new game"; std::ofstream(save)<<"player save";
        const auto oldHash=fileSha256(old.string()), nextHash=fileSha256(next.string()), saveHash=fileSha256(save.string());
        rejects([&]{AppImageUpdate::replace(old.string(),next.string(),std::string(64,'0'));});
        require(fileSha256(old.string())==oldHash,"Failed update changed the current game");
        const auto link=root/"link"; std::filesystem::create_symlink(old,link);
        rejects([&]{AppImageUpdate::replace(link.string(),next.string(),nextHash);});
        const auto backup=AppImageUpdate::replace(old.string(),next.string(),nextHash);
        require(fileSha256(old.string())==nextHash,"Update did not replace app");
        require(fileSha256(backup)==oldHash,"Backup lost previous app");
        require(fileSha256(save.string())==saveHash,"Update changed player data");
        require(!std::filesystem::exists(next),"Staging file left after success");
        std::filesystem::remove_all(root);
#endif
        std::cout<<"Update signature, tampering, platform, size, version and replacement checks passed.\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
