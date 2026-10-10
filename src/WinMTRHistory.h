#ifndef WINMTRHISTORY_H
#define WINMTRHISTORY_H
#include <string>
#include <vector>
#include <deque>
#include <sstream>
#include <iomanip>
#include <locale>
#include <algorithm>

namespace mtr {
const size_t HistoryLimit = 3600;
struct Sample {
    std::string utc;
    double seconds;
    int rtt; // -1: no accepted reply, NOT zero milliseconds
    unsigned long status;
};
struct Hop {
    int ttl=0, sent=0, received=0, best=0, average=0, worst=0, last=0;
    int timeouts=0, errors=0;
    unsigned long long totalRtt=0;
    std::string name;
    std::vector<Sample> samples;
};
struct Report {
    std::string target, started, ended, captured;
    double duration=0, interval=1;
    double selectionFrom=-1, selectionTo=-1;
    unsigned int timeoutMs=5000;
    std::string localStarted, localEnded, timezone, note;
    unsigned int packetSize=64;
    bool running=false;
    std::vector<Hop> hops;
};
inline double Loss(int sent, int received) {
    return sent > 0 ? 100.0 * (sent - received) / sent : 0.0;
}
inline std::string Number(double value, int decimals=2) {
    std::ostringstream s; s.imbue(std::locale::classic());
    s << std::fixed << std::setprecision(decimals) << value; return s.str();
}
inline std::string Html(const std::string& value) {
    std::string out;
    for(size_t i=0;i<value.size();++i) {
        switch(value[i]) {
        case '&': out+="&amp;"; break; case '<': out+="&lt;"; break;
        case '>': out+="&gt;"; break; case '"': out+="&quot;"; break;
        case '\'': out+="&#39;"; break; default: out+=value[i];
        }
    }
    return out;
}
inline std::string Csv(const std::string& value) {
    std::string out="\"";
    // Prevent spreadsheet formula execution when opening an exported CSV.
    if(!value.empty() && (value[0]=='=' || value[0]=='+' || value[0]=='-' || value[0]=='@' || value[0]=='\t' || value[0]=='\r')) out+="'";
    for(size_t i=0;i<value.size();++i) { if(value[i]=='"') out+='"'; out+=value[i]; }
    return out+'"';
}
inline void Append(std::deque<Sample>& samples, const Sample& sample) {
    if(samples.size()>=HistoryLimit) samples.pop_front();
    samples.push_back(sample);
}
inline std::string Metadata(const Report& r) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "WinMTR Comfort (based on WinMTR Redux approach; Comfort 2.0)\r\nTarget: " << r.target
        << "\r\nStarted UTC: " << (r.started.empty()?"not started":r.started)
        << "\r\nStopped UTC: " << (r.ended.empty()?"not stopped":r.ended)
        << "\r\nSnapshot UTC: " << r.captured << "\r\nState: " << (r.running?"running / stopping":"stopped")
        << "\r\nNote: " << r.note
        << "\r\nLocal start: " << r.localStarted << "\r\nLocal stop: " << r.localEnded << "\r\nTimezone: " << r.timezone
        << "\r\nSelected seconds from/to: " << (r.selectionFrom<0?"whole retained timeline":Number(r.selectionFrom)+" / "+Number(r.selectionTo))
        << "\r\nTimeout ms: " << r.timeoutMs
        << "\r\nDuration seconds: " << Number(r.duration)
        << "\r\nConfigured interval seconds: " << Number(r.interval)
        << "\r\nPacket size bytes: " << r.packetSize
        << "\r\nHistory: last 3600 completed probes per TTL; statistics cover the full session unless a period is selected."
        << "\r\nTimestamp = probe completion time (including timeout wait); all timestamps are UTC."
        << "\r\nNo response / ICMP error is not proof of transit packet loss.\r\n\r\n";
    return out.str();
}
inline std::string Text(const Report& r) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << Metadata(r);
    out << "TTL\tHost\tLoss %\tSent\tRecv\tTimeouts\tErrors\tBest ms\tAvg ms\tWorst ms\tLast reply ms\r\n";
    for(size_t i=0;i<r.hops.size();++i) {
        const Hop& h=r.hops[i];
        out << h.ttl << '\t' << h.name << '\t' << Number(Loss(h.sent,h.received)) << '\t' << h.sent << '\t' << h.received << '\t' << h.timeouts << '\t' << h.errors;
        if(h.received) out << '\t' << h.best << '\t' << h.average << '\t' << h.worst << '\t' << h.last;
        else out << "\tN/A\tN/A\tN/A\tN/A";
        out << "\r\n";
    }
    return out.str();
}
inline std::string SamplesCsv(const Report& r) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "target,session_started_utc,session_stopped_utc,snapshot_utc,session_duration_seconds,completion_utc,elapsed_seconds,ttl,summary_host,rtt_ms,result,icmp_status\r\n";
    for(size_t i=0;i<r.hops.size();++i) {
        const Hop& h=r.hops[i];
        for(size_t j=0;j<h.samples.size();++j) {
            const Sample& p=h.samples[j];
            out << Csv(r.target) << ',' << Csv(r.started) << ',' << Csv(r.ended) << ',' << Csv(r.captured) << ',' << Number(r.duration) << ','
                << Csv(p.utc) << ',' << Number(p.seconds,3) << ',' << h.ttl << ',' << Csv(h.name) << ',';
            if(p.rtt>=0) out << p.rtt;
            out << ',' << (p.rtt>=0?"reply":(p.status==11010 || p.status==258 || p.status==1460)?"timeout":"icmp_error") << ',' << p.status << "\r\n";
        }
    }
    return out.str();
}
inline std::string Svg(const Hop& h, double sharedStart=-1, double sharedEnd=-1) {
    if(h.samples.empty()) return "<p>No completed probes.</p>";
    const double left=55, top=20, width=620, height=140;
    double start=h.samples.front().seconds, span=(std::max)(1.0,h.samples.back().seconds-start);
    if(sharedStart>=0&&sharedEnd>=sharedStart){start=sharedStart;span=(std::max)(1.0,sharedEnd-sharedStart);}
    int maxRtt=1;
    for(size_t i=0;i<h.samples.size();++i) maxRtt=(std::max)(maxRtt,h.samples[i].rtt);
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "<svg viewBox='0 0 710 205' role='img' aria-label='RTT timeline; red: timeout; orange: ICMP error'>"
        << "<rect width='710' height='205' fill='#f6f8fb'/><path d='M55 20 V160 H675' fill='none' stroke='#64748b'/>"
        << "<text x='4' y='24'>" << maxRtt << " ms</text><text x='18' y='162'>0</text>"
        << "<text x='55' y='182'>" << Number(start,1) << " s</text><text x='590' y='182'>" << Number(start+span,1) << " s</text>";
    bool connected=false;
    double previousX=0,previousY=0;
    for(size_t i=0;i<h.samples.size();++i) {
        const Sample& p=h.samples[i]; double x=left+(p.seconds-start)*width/span;
        if(p.rtt<0) {
            out << "<path d='M" << x << " 20 V160' stroke='" << ((p.status==11010 || p.status==258 || p.status==1460)?"#dc2626":"#d97706") << "' opacity='.6'><title>" << Html(p.utc) << ": no response / error " << p.status << "</title></path>";
            connected=false;
        } else {
            double y=top+height-p.rtt*height/maxRtt;
            if(connected) out << "<path d='M" << previousX << ' ' << previousY << " L" << x << ' ' << y << "' fill='none' stroke='#2563eb'/>";
            out << "<circle cx='" << x << "' cy='" << y << "' r='2' fill='#2563eb'><title>" << Html(p.utc) << ": " << p.rtt << " ms</title></circle>";
            previousX=x;previousY=y;connected=true;
        }
    }
    out << "<text x='55' y='200'>Blue: RTT. Red: timeout. Orange: ICMP error. Time: seconds since session start.</text></svg>";
    return out.str();
}
inline std::string HtmlReport(const Report& r, double sharedStart=-1, double sharedEnd=-1) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'><title>WinMTR report</title>"
        << "<style>body{font:15px system-ui,sans-serif;max-width:1100px;margin:30px auto;padding:0 20px;color:#172033}pre{white-space:pre-wrap;background:#f6f8fb;padding:16px}table{border-collapse:collapse;width:100%}th,td{text-align:left;border-bottom:1px solid #ddd;padding:8px}svg{width:100%;max-width:850px}h2{margin-top:32px}@media print{svg{break-inside:avoid}}</style></head><body><h1>WinMTR session report</h1><pre>"
        << Html(Metadata(r)) << "</pre><table><thead><tr><th>TTL</th><th>Host</th><th>Loss %</th><th>Sent</th><th>Recv</th><th>Timeouts</th><th>Errors</th><th>Best ms</th><th>Avg ms</th><th>Worst ms</th><th>Last reply ms</th></tr></thead><tbody>";
    for(size_t i=0;i<r.hops.size();++i) {
        const Hop& h=r.hops[i];
        out << "<tr><td>" << h.ttl << "</td><td>" << Html(h.name) << "</td><td>" << Number(Loss(h.sent,h.received)) << "</td><td>" << h.sent << "</td><td>" << h.received << "</td><td>" << h.timeouts << "</td><td>" << h.errors << "</td>";
        if(h.received) out << "<td>" << h.best << "</td><td>" << h.average << "</td><td>" << h.worst << "</td><td>" << h.last << "</td>";
        else out << "<td>N/A</td><td>N/A</td><td>N/A</td><td>N/A</td>";
        out << "</tr>";
    }
    out << "</tbody></table><h2>Probe timelines</h2><p>Each TTL is shown separately. A TTL row is not necessarily the destination. The host is a summary name, not a per-probe route record.</p>";
    for(size_t i=0;i<r.hops.size();++i) {
        const Hop& h=r.hops[i]; out << "<h3>TTL " << h.ttl << ": " << Html(h.name) << "</h3>";
        if(!h.samples.empty()) out << "<p>Retained completion times (UTC): " << Html(h.samples.front().utc) << " - " << Html(h.samples.back().utc) << "</p>";
        out << Svg(h,sharedStart,sharedEnd);
    }
    out << "</body></html>"; return out.str();
}
}
#endif
