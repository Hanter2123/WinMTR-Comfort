#ifndef COMFORT_ENGINE_H
#define COMFORT_ENGINE_H
#include "../src/Session.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <functional>

inline std::string Utf8(const std::wstring& value) {
    if(value.empty())return "";int n=WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),NULL,0,NULL,NULL);
    std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),&out[0],n,NULL,NULL);return out;
}
inline std::wstring Wide(const std::string& value) {
    if(value.empty())return L"";int n=MultiByteToWideChar(CP_UTF8,0,value.data(),(int)value.size(),NULL,0);
    std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,0,value.data(),(int)value.size(),&out[0],n);return out;
}
inline std::string Timestamp(bool local=false) {
    SYSTEMTIME t;if(local)GetLocalTime(&t);else GetSystemTime(&t);char b[40];
    sprintf(b,"%04u-%02u-%02u%s%02u:%02u:%02u.%03u%s",t.wYear,t.wMonth,t.wDay,local?" ":"T",t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,local?"":"Z");return b;
}
inline std::string Zone() {
    TIME_ZONE_INFORMATION tz;DWORD mode=GetTimeZoneInformation(&tz);int minutes=-(tz.Bias+(mode==TIME_ZONE_ID_DAYLIGHT?tz.DaylightBias:mode==TIME_ZONE_ID_STANDARD?tz.StandardBias:0));
    char b[32];sprintf(b,"UTC%c%02d:%02d",minutes>=0?'+':'-',abs(minutes)/60,abs(minutes)%60);return b;
}
inline double SinceUtc(const std::string& utc) {
    SYSTEMTIME t={};unsigned y,m,d,h,mi,sec,ms;
    if(sscanf(utc.c_str(),"%u-%u-%uT%u:%u:%u.%uZ",&y,&m,&d,&h,&mi,&sec,&ms)!=7)return 0;
    t.wYear=y;t.wMonth=m;t.wDay=d;t.wHour=h;t.wMinute=mi;t.wSecond=sec;t.wMilliseconds=ms;
    FILETIME ft,now;if(!SystemTimeToFileTime(&t,&ft))return 0;GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a,b;a.LowPart=ft.dwLowDateTime;a.HighPart=ft.dwHighDateTime;b.LowPart=now.dwLowDateTime;b.HighPart=now.dwHighDateTime;
    return b.QuadPart>=a.QuadPart?(b.QuadPart-a.QuadPart)/10000000.0:0;
}
inline bool AtomicSession(const comfort::Session& s,const std::filesystem::path& path) {
    try {
        std::filesystem::create_directories(path.parent_path());auto tmp=path;tmp+=L".tmp";
        comfort::Write(s,tmp);if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;return true;
    }catch(...){return false;}
}
class Engine {
    struct State {std::string target,error;int lastTtl=30; mtr::Hop hop[30];std::deque<mtr::Sample> history[30];};
    std::vector<State> states;
    std::mutex mutex;
    comfort::Settings settings;
    std::string id,started,ended,localStarted,localEnded,timezone,note;
    std::chrono::steady_clock::time_point tick,endTick;
    double base=0;
    bool running=false;
    std::thread thread;
    HANDLE stopEvent;
    std::atomic<bool> stop{false};
    std::atomic<int> live{0};
    std::filesystem::path folder;
    HWND notify;
    void Record(size_t index,int ttl,int rtt,DWORD code,const std::string& address,bool destination) {
        std::lock_guard<std::mutex> lock(mutex);auto& s=states[index];auto& h=s.hop[ttl-1];h.ttl=ttl;++h.sent;
        if(rtt>=0){if(!h.received||rtt<h.best)h.best=rtt;h.worst=std::max(h.worst,rtt);h.last=rtt;h.totalRtt+=rtt;++h.received;h.name=address;}
        else if(comfort::Timeout(code))++h.timeouts;else ++h.errors;
        if(destination)s.lastTtl=std::min(s.lastTtl,ttl);
        mtr::Sample p={Timestamp(),base+std::chrono::duration<double>(std::chrono::steady_clock::now()-tick).count(),rtt,code};mtr::Append(s.history[ttl-1],p);
    }
    void Probe(size_t index,int ttl,sockaddr_storage destination) {
        bool ipv6=destination.ss_family==AF_INET6;HANDLE icmp=ipv6?Icmp6CreateFile():IcmpCreateFile();
        if(icmp==INVALID_HANDLE_VALUE){std::lock_guard<std::mutex> lock(mutex);states[index].error="Cannot open Windows ICMP service";return;}
        struct Close{HANDLE h;~Close(){IcmpCloseHandle(h);}} close{icmp};
        char request[64];memset(request,32,sizeof(request));std::vector<unsigned char> response(2048);IP_OPTION_INFORMATION options={};options.Ttl=(UCHAR)ttl;
        if(WaitForSingleObject(stopEvent,(ttl-1)*30)==WAIT_OBJECT_0)return;
        while(!stop.load()) {
            {std::lock_guard<std::mutex> lock(mutex);if(ttl>states[index].lastTtl)break;}
            auto before=std::chrono::steady_clock::now();DWORD replies=0,code=0;int rtt=-1;std::string source;bool reached=false;
            if(ipv6){
                sockaddr_in6 from={};from.sin6_family=AF_INET6;
                replies=Icmp6SendEcho2(icmp,NULL,NULL,NULL,&from,(sockaddr_in6*)&destination,request,64,&options,response.data(),(DWORD)response.size(),settings.timeout);
                code=replies?((PICMPV6_ECHO_REPLY)response.data())->Status:GetLastError();
                if(replies&&(code==IP_SUCCESS||code==IP_TTL_EXPIRED_TRANSIT)) {
                    auto reply=(PICMPV6_ECHO_REPLY)response.data();rtt=(int)reply->RoundTripTime;sockaddr_in6 a={};a.sin6_family=AF_INET6;memcpy(&a.sin6_addr,reply->Address.sin6_addr,16);a.sin6_scope_id=reply->Address.sin6_scope_id;
                    char name[NI_MAXHOST]={};getnameinfo((sockaddr*)&a,sizeof(a),name,sizeof(name),NULL,0,NI_NUMERICHOST);source=name;reached=code==IP_SUCCESS&&!memcmp(&a.sin6_addr,&((sockaddr_in6*)&destination)->sin6_addr,16);
                }
            }else{
                replies=IcmpSendEcho(icmp,((sockaddr_in*)&destination)->sin_addr.s_addr,request,64,&options,response.data(),(DWORD)response.size(),settings.timeout);
                code=replies?((PICMP_ECHO_REPLY)response.data())->Status:GetLastError();
                if(replies&&(code==IP_SUCCESS||code==IP_TTL_EXPIRED_TRANSIT)){
                    auto reply=(PICMP_ECHO_REPLY)response.data();rtt=(int)reply->RoundTripTime;sockaddr_in a={};a.sin_family=AF_INET;a.sin_addr.s_addr=reply->Address;char name[NI_MAXHOST]={};getnameinfo((sockaddr*)&a,sizeof(a),name,sizeof(name),NULL,0,NI_NUMERICHOST);source=name;reached=code==IP_SUCCESS&&a.sin_addr.s_addr==((sockaddr_in*)&destination)->sin_addr.s_addr;
                }
            }
            Record(index,ttl,rtt,code,source,reached);
            int elapsed=(int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-before).count();
            if(WaitForSingleObject(stopEvent,(DWORD)std::max(0,(int)(settings.interval*1000)-elapsed))==WAIT_OBJECT_0)break;
        }
    }
    void Run() {
        std::vector<std::thread> workers;
        try {
            for(size_t i=0;i<states.size()&&!stop.load();++i){
                ADDRINFOW hints={};hints.ai_family=settings.family==1?AF_INET:settings.family==2?AF_INET6:AF_UNSPEC;hints.ai_socktype=SOCK_DGRAM;PADDRINFOW found=NULL;
                int result=GetAddrInfoW(Wide(states[i].target).c_str(),NULL,&hints,&found);
                if(result||!found){std::lock_guard<std::mutex> lock(mutex);states[i].error="DNS / address resolution failed: "+std::to_string(result);continue;}
                auto choice=found;if(!settings.family)for(auto a=found;a;a=a->ai_next)if(a->ai_family==AF_INET){choice=a;break;}
                sockaddr_storage destination={};memcpy(&destination,choice->ai_addr,choice->ai_addrlen);FreeAddrInfoW(found);
                for(int ttl=1;ttl<=30&&!stop.load();++ttl){
                    ++live;try{workers.emplace_back([this,i,ttl,destination]{try{Probe(i,ttl,destination);}catch(...){std::lock_guard<std::mutex> lock(mutex);states[i].error="Probe worker failed";}--live;});}catch(...){--live;throw;}
                }
            }
            while(!stop.load()&&live.load()>0){if(WaitForSingleObject(stopEvent,10000)==WAIT_OBJECT_0)break;Save();}
        }catch(...){stop.store(true);SetEvent(stopEvent);std::lock_guard<std::mutex> lock(mutex);note="Worker startup failed; session stopped";}
        for(auto& worker:workers)worker.join();
        {std::lock_guard<std::mutex> lock(mutex);running=false;endTick=std::chrono::steady_clock::now();ended=Timestamp();localEnded=Timestamp(true);}
        Save();done.store(true);PostMessageW(notify,WM_APP+1,0,0);
    }
public:
    std::atomic<bool> done{true};std::atomic<bool> saveOk{true};
    Engine(HWND window,const std::filesystem::path& path):folder(path),notify(window){stopEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!stopEvent)throw std::runtime_error("Cannot create stop event");}
    ~Engine(){Stop();Join();CloseHandle(stopEvent);}
    void Join(){if(thread.joinable())thread.join();}
    void Stop(){stop.store(true);SetEvent(stopEvent);}
    bool Stopping()const{return stop.load();}
    comfort::Session Snapshot(){
        std::lock_guard<std::mutex> lock(mutex);comfort::Session s;s.id=id;s.settings=settings;
        for(const auto& st:states){mtr::Report r;r.target=st.target;r.started=started;r.ended=ended;r.localStarted=localStarted;r.localEnded=localEnded;r.timezone=timezone;r.note=note+(st.error.empty()?"":" | "+st.error);r.captured=Timestamp();r.running=running;r.interval=settings.interval;r.timeoutMs=settings.timeout;r.duration=base+std::chrono::duration<double>((running?std::chrono::steady_clock::now():endTick)-tick).count();
            for(int i=0;i<st.lastTtl;++i){if(!st.hop[i].sent)continue;auto h=st.hop[i];h.average=h.received?(int)(h.totalRtt/h.received):0;if(h.name.empty())h.name="No response";h.samples.assign(st.history[i].begin(),st.history[i].end());r.hops.push_back(h);}s.reports.push_back(r);
        }return s;
    }
    bool Save(){auto s=Snapshot();if(s.id.empty())return true;bool ok=AtomicSession(s,folder/Wide(s.id+".wmtr"));saveOk.store(ok);return ok;}
    void Start(const std::vector<std::string>& targets,const comfort::Settings& input,const comfort::Session* resume=nullptr){
        Join();if(!comfort::ValidSettings(input)||targets.empty()||targets.size()>3)throw std::runtime_error("Invalid settings / targets");
        {std::lock_guard<std::mutex> lock(mutex);states.clear();states.resize(targets.size());settings=input;started=Timestamp();localStarted=Timestamp(true);timezone=Zone();ended.clear();localEnded.clear();note.clear();base=0;id=started;for(char& c:id)if(c==':'||c=='.')c='-';
            for(size_t i=0;i<targets.size();++i)states[i].target=targets[i];
            if(resume&&resume->reports.size()==targets.size()){
                id=resume->id;started=resume->reports[0].started;localStarted=resume->reports[0].localStarted;timezone=resume->reports[0].timezone;base=std::max(resume->reports[0].duration,SinceUtc(started));note="Restored session; elapsed time includes downtime, with no probes during the gap";
                for(size_t i=0;i<targets.size();++i)for(const auto& h:resume->reports[i].hops){states[i].hop[h.ttl-1]=h;states[i].hop[h.ttl-1].samples.clear();states[i].history[h.ttl-1].assign(h.samples.begin(),h.samples.end());}
            }
            tick=std::chrono::steady_clock::now();endTick=tick;running=true;
        }
        stop.store(false);done.store(false);ResetEvent(stopEvent);Save();
        try{thread=std::thread(&Engine::Run,this);}catch(...){std::lock_guard<std::mutex> lock(mutex);running=false;done.store(true);throw;}
    }
};
#endif
