#include "../src/Session.h"
#include <cassert>
#include <iostream>
int main(int argc,char** argv){
    assert(argc==2);std::filesystem::path file=argv[1];
    comfort::Session s;s.id="test-2026";
    mtr::Report r;r.target="example.test";r.duration=4;
    mtr::Hop h;h.ttl=1;h.name="<router>";h.sent=4;h.received=2;h.timeouts=1;h.errors=1;h.best=0;h.worst=h.last=10;h.average=5;h.totalRtt=10;
    h.samples={{"utc",0,0,0},{"utc",1,-1,11010},{"utc",2,-1,11050},{"utc",3,10,0}};r.hops.push_back(h);s.reports.push_back(r);
    comfort::Write(s,file);auto copy=comfort::Read(file);assert(copy.reports[0].hops[0].totalRtt==10);
    auto slice=comfort::Slice(r,0,2);auto a=slice.hops[0];assert(a.sent==3&&a.received==1&&a.timeouts==1&&a.errors==1&&a.best==0&&a.average==0);
    slice=comfort::Slice(r,3,3);assert(slice.hops[0].received==1&&slice.hops[0].average==10);
    slice=comfort::Slice(r,20,30);assert(slice.hops[0].sent==0);
    auto rejects=[&](){try{comfort::Read(file);return false;}catch(...){return true;}};
    s.id="../escape";comfort::Write(s,file);assert(rejects());s.id="valid";
    s.reports[0].hops[0].errors=2;comfort::Write(s,file);assert(rejects());s.reports[0].hops[0].errors=1;
    comfort::Write(s,file);std::filesystem::resize_file(file,20);assert(rejects());
    comfort::Settings settings;settings.interval=0;assert(!comfort::ValidSettings(settings));settings.interval=0.2;assert(comfort::ValidSettings(settings));
    auto html=comfort::Html(s,true);assert(html.find("&lt;router&gt;")!=std::string::npos&&html.find("Timeouts")!=std::string::npos);
    assert(comfort::Csv(s).find("icmp_error,11050")!=std::string::npos);
    std::cout<<"PASS: persistence, zero RTT, inclusive selection, timeout/error counts, corrupt files, path traversal, settings, exports\n";
}
