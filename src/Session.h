#ifndef COMFORT_SESSION_H
#define COMFORT_SESSION_H
#include "WinMTRHistory.h"
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <cmath>

namespace comfort {
struct Settings { double interval=1;unsigned int timeout=5000;int family=0; };
inline bool ValidSettings(const Settings& s) {return std::isfinite(s.interval)&&s.interval>=0.2&&s.interval<=60&&s.timeout>=200&&s.timeout<=30000&&s.family>=0&&s.family<=2;}
inline bool Timeout(unsigned long status) {return status==11010||status==258||status==1460;}
inline mtr::Report Slice(const mtr::Report& input,double first,double last) {
    mtr::Report r=input;if(first<0||last<first)return r;
    r.selectionFrom=first;r.selectionTo=last;
    for(auto& h:r.hops) {
        std::vector<mtr::Sample> selected;unsigned long long total=0;
        h.sent=h.received=h.timeouts=h.errors=h.best=h.worst=h.last=h.average=0;
        for(const auto& p:h.samples)if(p.seconds>=first&&p.seconds<=last) {
            selected.push_back(p);++h.sent;
            if(p.rtt>=0){if(!h.received||p.rtt<h.best)h.best=p.rtt;++h.received;h.worst=std::max(h.worst,p.rtt);h.last=p.rtt;total+=p.rtt;}
            else if(Timeout(p.status))++h.timeouts;else ++h.errors;
        }
        h.totalRtt=total;h.average=h.received?(int)(total/h.received):0;h.samples=std::move(selected);
    }
    return r;
}
struct Session {
    std::string id;
    Settings settings;
    std::vector<mtr::Report> reports;
};
// Versioned length-prefixed format; loader validates lengths before allocating.
class Writer {
    std::ofstream out;
public:
    explicit Writer(const std::filesystem::path& path):out(path,std::ios::binary|std::ios::trunc){if(!out)throw std::runtime_error("Cannot open session file");}
    template<class T>void number(T v){out.write(reinterpret_cast<const char*>(&v),sizeof(v));if(!out)throw std::runtime_error("Session write failed");}
    void str(const std::string& s){number((uint32_t)s.size());out.write(s.data(),s.size());if(!out)throw std::runtime_error("Session write failed");}
    void finish(){out.flush();if(!out)throw std::runtime_error("Session flush failed");out.close();if(out.fail())throw std::runtime_error("Session close failed");}
};
class Reader {
    std::ifstream in;
public:
    explicit Reader(const std::filesystem::path& path):in(path,std::ios::binary){if(!in||std::filesystem::file_size(path)>64*1024*1024)throw std::runtime_error("Invalid session file");}
    template<class T>T number(){T v{};in.read(reinterpret_cast<char*>(&v),sizeof(v));if(!in)throw std::runtime_error("Truncated session");return v;}
    std::string str(){auto n=number<uint32_t>();if(n>16384)throw std::runtime_error("Invalid string length");std::string s(n,0);in.read(s.data(),n);if(!in)throw std::runtime_error("Truncated string");return s;}
    void end(){if(in.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Unexpected trailing data");}
};
inline void Write(const Session& s,const std::filesystem::path& path) {
    Writer w(path);w.str("WinMTR-Comfort-2");w.str(s.id);w.number(s.settings.interval);w.number(s.settings.timeout);w.number(s.settings.family);w.number((uint32_t)s.reports.size());
    for(const auto& r:s.reports) {
        for(const auto& v:{r.target,r.started,r.ended,r.captured,r.localStarted,r.localEnded,r.timezone,r.note})w.str(v);
        w.number(r.duration);w.number(r.interval);w.number(r.packetSize);w.number(r.timeoutMs);w.number((uint8_t)r.running);w.number((uint32_t)r.hops.size());
        for(const auto& h:r.hops) {
            w.number(h.ttl);w.number(h.sent);w.number(h.received);w.number(h.best);w.number(h.average);w.number(h.worst);w.number(h.last);w.number(h.timeouts);w.number(h.errors);w.number(h.totalRtt);w.str(h.name);w.number((uint32_t)h.samples.size());
            for(const auto& p:h.samples){w.str(p.utc);w.number(p.seconds);w.number(p.rtt);w.number((uint32_t)p.status);}
        }
    }
    w.finish();
}
inline Session Read(const std::filesystem::path& path) {
    Reader rd(path);if(rd.str()!="WinMTR-Comfort-2")throw std::runtime_error("Unsupported session version");Session s;s.id=rd.str();s.settings.interval=rd.number<double>();s.settings.timeout=rd.number<unsigned int>();s.settings.family=rd.number<int>();if(!ValidSettings(s.settings))throw std::runtime_error("Invalid settings");
    if(s.id.empty()||s.id.size()>100||s.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid session identifier");
    auto count=rd.number<uint32_t>();if(count==0||count>3)throw std::runtime_error("Invalid target count");
    for(uint32_t i=0;i<count;++i) {
        mtr::Report r;r.target=rd.str();r.started=rd.str();r.ended=rd.str();r.captured=rd.str();r.localStarted=rd.str();r.localEnded=rd.str();r.timezone=rd.str();r.note=rd.str();r.duration=rd.number<double>();r.interval=rd.number<double>();r.packetSize=rd.number<unsigned int>();r.timeoutMs=rd.number<unsigned int>();r.running=rd.number<uint8_t>()!=0;
        if(!std::isfinite(r.duration)||r.duration<0||!std::isfinite(r.interval)||r.interval<0.2||r.interval>60||r.timeoutMs<200||r.timeoutMs>30000||r.packetSize!=64)throw std::runtime_error("Invalid report settings");
        auto hops=rd.number<uint32_t>();if(hops>30)throw std::runtime_error("Too many TTLs");
        for(uint32_t j=0;j<hops;++j) {
            mtr::Hop h;h.ttl=rd.number<int>();h.sent=rd.number<int>();h.received=rd.number<int>();h.best=rd.number<int>();h.average=rd.number<int>();h.worst=rd.number<int>();h.last=rd.number<int>();h.timeouts=rd.number<int>();h.errors=rd.number<int>();h.totalRtt=rd.number<unsigned long long>();h.name=rd.str();
            if(h.ttl<1||h.ttl>30||h.sent<0||h.received<0||h.received>h.sent||h.timeouts<0||h.errors<0||h.timeouts>h.sent-h.received||h.errors!=h.sent-h.received-h.timeouts||h.best<0||h.average<0||h.worst<0||h.last<0)throw std::runtime_error("Invalid statistics");
            auto samples=rd.number<uint32_t>();if(samples>mtr::HistoryLimit)throw std::runtime_error("Too much history");double previous=-1;
            for(uint32_t k=0;k<samples;++k){mtr::Sample p;p.utc=rd.str();p.seconds=rd.number<double>();p.rtt=rd.number<int>();p.status=rd.number<uint32_t>();if(!std::isfinite(p.seconds)||p.seconds<previous||p.seconds<0||p.rtt< -1)throw std::runtime_error("Invalid sample");previous=p.seconds;h.samples.push_back(p);}
            r.hops.push_back(h);
        }
        s.reports.push_back(r);
    }
    rd.end();return s;
}
inline std::string Text(const Session& s) {
    std::string out="WinMTR Comfort 2.0: synchronized targets\r\nSession: "+s.id+"\r\n\r\n";
    for(const auto& r:s.reports)out+=mtr::Text(r)+"\r\n";
    return out;
}
inline std::string Csv(const Session& s) {
    std::string out;for(const auto& r:s.reports){auto csv=mtr::SamplesCsv(r);if(out.empty())out=csv;else{auto p=csv.find('\n');if(p!=std::string::npos)out+=csv.substr(p+1);}}return out;
}
inline std::string Html(const Session& s,bool provider) {
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<"<!doctype html><html lang='ru'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'><title>WinMTR Comfort</title><style>body{font:15px system-ui,sans-serif;max-width:1200px;margin:30px auto;padding:0 20px;color:#172033}pre{white-space:pre-wrap;background:#f6f8fb;padding:16px}table{border-collapse:collapse;width:100%}th,td{text-align:left;border-bottom:1px solid #ddd;padding:8px}svg{width:100%;max-width:850px}section{margin-top:36px;border-top:2px solid #bbb}svg,table{break-inside:avoid}</style></head><body><h1>"<<(provider?"Отчёт о качестве соединения для провайдера":"WinMTR Comfort: несколько целей")<<"</h1><p>Сеанс: "<<mtr::Html(s.id)<<"</p><p>Сравнивайте одинаковые моменты на общей шкале секунд от начала. Тайм-аут ICMP и ошибка локальной отправки различаются; отсутствие ответов промежуточного узла само по себе не доказывает потери транзитного трафика.</p>";
    if(provider){
        out<<"<h2>Сводка по последнему отображаемому TTL каждой цели</h2><table><tr><th>Цель</th><th>TTL / адрес</th><th>Нет ответа</th><th>Тайм-ауты</th><th>Ошибки</th><th>Средняя / макс. мс</th></tr>";
        for(const auto& r:s.reports){out<<"<tr><td>"<<mtr::Html(r.target)<<"</td>";if(r.hops.empty())out<<"<td colspan='5'>Нет завершённых проверок</td>";else{const auto& h=r.hops.back();out<<"<td>"<<h.ttl<<" / "<<mtr::Html(h.name)<<"</td><td>"<<mtr::Number(mtr::Loss(h.sent,h.received))<<"%</td><td>"<<h.timeouts<<"</td><td>"<<h.errors<<"</td><td>"<<(h.received?std::to_string(h.average)+" / "+std::to_string(h.worst):"N/A")<<"</td>";}out<<"</tr>";}
        out<<"</table><p>Последний TTL может не быть конечным сервером. Отчёт описывает ответы ICMP, не устанавливает виновника сбоя.</p>";
    }
    double first=-1,last=0;for(const auto& r:s.reports)for(const auto& h:r.hops)if(!h.samples.empty()){first=first<0?h.samples.front().seconds:std::min(first,h.samples.front().seconds);last=std::max(last,h.samples.back().seconds);}
    for(const auto& r:s.reports){auto html=mtr::HtmlReport(r,first,last);auto begin=html.find("<body>")+6;auto end=html.rfind("</body>");out<<"<section><h2>Цель: "<<mtr::Html(r.target)<<"</h2>"<<html.substr(begin,end-begin)<<"</section>";}
    out<<"</body></html>";return out.str();
}
}
#endif
