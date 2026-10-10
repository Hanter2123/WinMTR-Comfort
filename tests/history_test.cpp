#include "../src/WinMTRHistory.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    assert(std::fabs(mtr::Loss(198,196)-1.0101010101)<0.000001);
    assert(mtr::Number(mtr::Loss(198,196))=="1.01");
    assert(mtr::Loss(0,0)==0 && mtr::Loss(500,481)==3.8);
    assert(mtr::Html("<x & \"a\">'")=="&lt;x &amp; &quot;a&quot;&gt;&#39;");
    assert(mtr::Csv("a\"b")=="\"a\"\"b\"");
    assert(mtr::Csv("=cmd()")=="\"'=cmd()\"");
    std::deque<mtr::Sample> history;
    for(int i=0;i<3602;++i) {mtr::Sample p={"2026-10-10T00:00:00.000Z",(double)i,i,0};mtr::Append(history,p);}
    assert(history.size()==3600 && history.front().seconds==2 && history.back().seconds==3601);
    mtr::Report r;r.target="example.test";r.started="2026-10-10T12:00:00.000Z";r.ended="2026-10-10T12:00:05.000Z";
    r.captured=r.ended;r.duration=5;r.interval=1;r.packetSize=64;r.running=false;
    mtr::Hop h;h.ttl=1;h.name="router <test>";h.sent=5;h.received=3;h.best=0;h.average=37;h.worst=110;h.last=110;
    mtr::Sample p={"2026-10-10T12:00:00.000Z",0,0,0};h.samples.push_back(p);
    p.utc="2026-10-10T12:00:01.000Z";p.seconds=1;p.rtt=-1;p.status=11010;h.samples.push_back(p);
    p.utc="2026-10-10T12:00:02.000Z";p.seconds=2;p.rtt=2;p.status=0;h.samples.push_back(p);
    p.utc="2026-10-10T12:00:03.000Z";p.seconds=3;p.rtt=110;h.samples.push_back(p);
    p.utc="2026-10-10T12:00:04.000Z";p.seconds=4;p.rtt=-1;p.status=11010;h.samples.push_back(p);
    r.hops.push_back(h);
    h.ttl=2;h.name="=unsafe()";h.sent=1;h.received=0;h.samples.clear();h.samples.push_back(p);r.hops.push_back(h);
    std::string csv=mtr::SamplesCsv(r),html=mtr::HtmlReport(r),text=mtr::Text(r);
    assert(csv.find(",,timeout,11010")!=std::string::npos);
    assert(csv.find(",0,reply,0")!=std::string::npos);
    assert(csv.find("\"'=unsafe()\"")!=std::string::npos);
    assert(html.find("router &lt;test&gt;")!=std::string::npos);
    assert(html.find("<td>N/A</td>")!=std::string::npos);
    assert(text.find("40.00")!=std::string::npos);
    assert(text.find("Stopped UTC: 2026-10-10T12:00:05.000Z")!=std::string::npos);
    assert(mtr::Svg(mtr::Hop()).find("No completed probes")!=std::string::npos);
    if(argc>1) {std::ofstream f(argv[1]);f<<html;}
    std::cout<<"PASS: exact loss, empty samples, ring limit, time metadata, valid zero RTT, timeout gaps, escaping, CSV formula protection, HTML report.\n";
}
