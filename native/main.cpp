// WinMTR Comfort 2.0; GPL-2.0. Windows ICMP per-TTL approach inspired by WinMTR Redux.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <memory>
#include <iomanip>
#include "Engine.h"

const int StartId=101,NewId=102,TextId=103,HtmlId=104,CsvId=105,ProviderId=106,RestoreId=107,ClearId=108,CompareId=109,TargetId=110,ListId=111,FamilyId=112,IntervalId=113,TimeoutId=114;
HWND mainWindow,inputs[3],startButton,targetCombo,listWindow,graphWindow,infoLabel,rangeLabel,intervalEdit,timeoutEdit,familyCombo,compareCheck;
HFONT uiFont;
std::unique_ptr<Engine> engine;
std::filesystem::path dataFolder;
std::vector<std::wstring> history;
comfort::Session displayed,restored;
bool uiRunning=false,closing=false,loaded=false,selfTest=false;
int selectedTarget=0,selectedTtl=-1,testPhase=0,testResult=0;
std::wstring testPrefix;
double rangeFirst=-1,rangeLast=-1,axisFirst=0,axisLast=1,dragFirst=0,phaseBase=0;
bool dragging=false;
RECT graphArea={};
void Refresh();void StartStop();void Save(int format);void SetControls(bool active);

std::wstring TextOf(HWND h){int n=GetWindowTextLengthW(h);std::wstring text(n+1,0);GetWindowTextW(h,&text[0],n+1);text.resize(n);return text;}
void Put(HWND h,const std::wstring& s){SetWindowTextW(h,s.c_str());}
std::wstring Trim(std::wstring s){auto begin=s.find_first_not_of(L" \r\n\t"),end=s.find_last_not_of(L" \r\n\t");return begin==std::wstring::npos?L"":s.substr(begin,end-begin+1);}
std::string Gateway(){MIB_IPFORWARDROW route={};if(GetBestRoute(inet_addr("1.1.1.1"),0,&route)==NO_ERROR&&route.dwForwardNextHop){sockaddr_in a={};a.sin_family=AF_INET;a.sin_addr.s_addr=route.dwForwardNextHop;char host[64]={};getnameinfo((sockaddr*)&a,sizeof(a),host,sizeof(host),NULL,0,NI_NUMERICHOST);return host;}return "";}
comfort::Settings Settings(){
    auto interval=Utf8(Trim(TextOf(intervalEdit))),timeout=Utf8(Trim(TextOf(timeoutEdit)));std::replace(interval.begin(),interval.end(),',','.');size_t used=0;comfort::Settings s;s.interval=std::stod(interval,&used);if(used!=interval.size())throw std::runtime_error("Interval");s.timeout=(unsigned int)std::stoul(timeout,&used);if(used!=timeout.size())throw std::runtime_error("Timeout");s.family=(int)SendMessageW(familyCombo,CB_GETCURSEL,0,0);if(!comfort::ValidSettings(s))throw std::runtime_error("Range");return s;
}
std::vector<std::string> Targets(){std::vector<std::string> out;for(auto input:inputs){auto text=Trim(TextOf(input));if(text.size()>253)throw std::runtime_error("Target too long");if(!text.empty())out.push_back(Utf8(text));}return out;}
void HistoryAdd(const std::wstring& value){if(value.empty())return;history.erase(std::remove(history.begin(),history.end(),value),history.end());history.insert(history.begin(),value);if(history.size()>20)history.resize(20);}
void FillHistory(){for(auto input:inputs){auto text=TextOf(input);SendMessageW(input,CB_RESETCONTENT,0,0);for(const auto& value:history)SendMessageW(input,CB_ADDSTRING,0,(LPARAM)value.c_str());Put(input,text);}}
void SaveConfig(){
    try{std::filesystem::create_directories(dataFolder);auto temporary=dataFolder/L"settings.tmp";std::ofstream f(temporary);auto s=Settings();f<<s.interval<<' '<<s.timeout<<' '<<s.family<<'\n';for(auto input:inputs)f<<std::quoted(Utf8(TextOf(input)))<<'\n';f<<history.size()<<'\n';for(const auto& value:history)f<<std::quoted(Utf8(value))<<'\n';f.close();if(f)MoveFileExW(temporary.c_str(),(dataFolder/L"settings.txt").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);}catch(...){}
}
void LoadConfig(){
    try{auto p=dataFolder/L"settings.txt";if(!std::filesystem::exists(p)||std::filesystem::file_size(p)>32768)return;std::ifstream f(p);comfort::Settings s;f>>s.interval>>s.timeout>>s.family;if(!f||!comfort::ValidSettings(s))return;std::string targets[3];for(auto& value:targets)f>>std::quoted(value);size_t n=0;f>>n;if(!f||n>20)return;std::vector<std::wstring> values;for(size_t i=0;i<n;++i){std::string value;f>>std::quoted(value);if(!f||value.size()>2048)return;values.push_back(Wide(value));}history=values;Put(intervalEdit,Wide(mtr::Number(s.interval,2)));Put(timeoutEdit,std::to_wstring(s.timeout));SendMessageW(familyCombo,CB_SETCURSEL,s.family,0);FillHistory();for(int i=0;i<3;++i)Put(inputs[i],Wide(targets[i]));}catch(...){}
}
void ApplyRestored(comfort::Session s){
    SendMessageW(targetCombo,CB_RESETCONTENT,0,0);restored=s;displayed=s;loaded=true;for(auto& r:displayed.reports){r.running=false;r.note+=" | Saved session opened; press Continue to resume";}
    for(int i=0;i<3;++i)Put(inputs[i],i<(int)s.reports.size()?Wide(s.reports[i].target):L"");Put(intervalEdit,Wide(mtr::Number(s.settings.interval,2)));Put(timeoutEdit,std::to_wstring(s.settings.timeout));SendMessageW(familyCombo,CB_SETCURSEL,s.settings.family,0);selectedTarget=0;selectedTtl=-1;rangeFirst=rangeLast=-1;Put(startButton,L"Продолжить");Refresh();
}
void LoadLatest(){
    try{if(!std::filesystem::exists(dataFolder))return;std::vector<std::pair<std::filesystem::file_time_type,std::filesystem::path>> paths;for(const auto& entry:std::filesystem::directory_iterator(dataFolder))if(entry.path().extension()==L".wmtr")paths.push_back({entry.last_write_time(),entry.path()});std::sort(paths.rbegin(),paths.rend());for(size_t i=0;i<std::min<size_t>(paths.size(),20);++i){try{ApplyRestored(comfort::Read(paths[i].second));break;}catch(...){}}}catch(...){}
}
void Restore(){if(uiRunning)return;wchar_t path[32768]={};OPENFILENAMEW ofn={};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=mainWindow;ofn.lpstrFilter=L"Сеансы WinMTR (*.wmtr)\0*.wmtr\0\0";ofn.lpstrFile=path;ofn.nMaxFile=32768;ofn.lpstrInitialDir=dataFolder.c_str();ofn.Flags=OFN_FILEMUSTEXIST|OFN_EXPLORER;if(!GetOpenFileNameW(&ofn))return;try{ApplyRestored(comfort::Read(path));}catch(...){MessageBoxW(mainWindow,L"Сеанс повреждён или имеет неподдерживаемый формат.",L"WinMTR Comfort",MB_ICONERROR);}}
void NewSession(){if(uiRunning)return;loaded=false;restored=comfort::Session();displayed=comfort::Session();rangeFirst=rangeLast=-1;selectedTtl=-1;ListView_DeleteAllItems(listWindow);Put(startButton,L"Начать");Put(infoLabel,L"Новый сеанс. Автосохранение каждые 10 секунд и при остановке.");Refresh();}
void SetControls(bool active){
    for(auto input:inputs)EnableWindow(input,!active);for(auto control:{intervalEdit,timeoutEdit,familyCombo})EnableWindow(control,!active);for(int id:{NewId,RestoreId})EnableWindow(GetDlgItem(mainWindow,id),!active);Put(startButton,active?L"Стоп":loaded?L"Продолжить":L"Начать");
}
void StartStop(){
    if(uiRunning){engine->Stop();EnableWindow(startButton,FALSE);Put(startButton,L"Остановка…");return;}
    try{auto settings=Settings();auto targets=Targets();if(targets.empty())throw std::runtime_error("No targets");bool resume=loaded&&restored.reports.size()==targets.size()&&restored.settings.interval==settings.interval&&restored.settings.timeout==settings.timeout&&restored.settings.family==settings.family;for(size_t i=0;resume&&i<targets.size();++i)resume=restored.reports[i].target==targets[i];
        for(const auto& value:targets)HistoryAdd(Wide(value));FillHistory();SaveConfig();rangeFirst=rangeLast=-1;selectedTtl=-1;selectedTarget=0;SendMessageW(targetCombo,CB_RESETCONTENT,0,0);uiRunning=true;SetControls(true);engine->Start(targets,settings,resume?&restored:nullptr);loaded=false;Refresh();
    }catch(...){uiRunning=false;SetControls(false);MessageBoxW(mainWindow,L"Укажи 1–3 адреса. Интервал: 0,2–60 секунд; тайм-аут: 200–30000 мс.",L"Параметры замера",MB_ICONERROR);}
}
comfort::Session ExportSession(){auto s=displayed;if(rangeFirst>=0)for(auto& r:s.reports)r=comfort::Slice(r,rangeFirst,rangeLast);return s;}
bool WriteText(const std::filesystem::path& path,const std::string& text){try{std::ofstream f(path,std::ios::binary);f.write("\xEF\xBB\xBF",3);f.write(text.data(),text.size());f.flush();return (bool)f;}catch(...){return false;}}
void Save(int format){
    auto s=ExportSession();if(s.reports.empty()){MessageBoxW(mainWindow,L"Нет данных для экспорта.",L"WinMTR Comfort",MB_OK);return;}
    wchar_t file[32768]={};auto name=format==3?L"Отчёт_для_провайдера.html":format==0?L"WinMTR.txt":format==1?L"WinMTR.html":L"WinMTR.csv";wcscpy(file,name);const wchar_t* ext=format==0?L"txt":format==2?L"csv":L"html";OPENFILENAMEW ofn={};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=mainWindow;ofn.lpstrFilter=format==0?L"TXT\0*.txt\0\0":format==2?L"CSV\0*.csv\0\0":L"HTML\0*.html\0\0";ofn.lpstrFile=file;ofn.nMaxFile=32768;ofn.lpstrDefExt=ext;ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_EXPLORER;if(!GetSaveFileNameW(&ofn))return;
    auto content=format==0?comfort::Text(s):format==2?comfort::Csv(s):comfort::Html(s,format==3);bool ok=WriteText(file,content);if(ok&&format==3)ShellExecuteW(mainWindow,L"open",file,NULL,NULL,SW_SHOWNORMAL);else MessageBoxW(mainWindow,ok?L"Отчёт сохранён.":L"Ошибка записи отчёта.",L"WinMTR Comfort",ok?MB_OK:MB_ICONERROR);
}
void SetCell(int row,int col,const std::wstring& value){ListView_SetItemText(listWindow,row,col,const_cast<wchar_t*>(value.c_str()));}
void Refresh(){
    if(uiRunning)displayed=engine->Snapshot();
    int current=(int)SendMessageW(targetCombo,CB_GETCURSEL,0,0);if(current>=0)selectedTarget=current;
    if(SendMessageW(targetCombo,CB_GETCOUNT,0,0)!=(LRESULT)displayed.reports.size()){
        SendMessageW(targetCombo,CB_RESETCONTENT,0,0);for(const auto& r:displayed.reports)SendMessageW(targetCombo,CB_ADDSTRING,0,(LPARAM)Wide(r.target).c_str());selectedTarget=std::min(selectedTarget,std::max(0,(int)displayed.reports.size()-1));SendMessageW(targetCombo,CB_SETCURSEL,selectedTarget,0);
    }
    if(!displayed.reports.empty()){
        const auto& r=displayed.reports[0];Put(infoLabel,L"Начало: "+Wide(r.localStarted)+L" "+Wide(r.timezone)+L" | "+Wide(mtr::Number(r.duration,1))+L" с | "+(uiRunning?engine->Stopping()?L"остановка":L"измерение":loaded?L"сохранённый сеанс":L"завершено")+(engine->saveOk.load()?L" | автосохранение":L" | ОШИБКА АВТОСОХРАНЕНИЯ"));
        auto shown=rangeFirst<0?displayed.reports[selectedTarget]:comfort::Slice(displayed.reports[selectedTarget],rangeFirst,rangeLast);
        while(ListView_GetItemCount(listWindow)>(int)shown.hops.size())ListView_DeleteItem(listWindow,ListView_GetItemCount(listWindow)-1);
        for(size_t i=0;i<shown.hops.size();++i){const auto& h=shown.hops[i];if((int)i>=ListView_GetItemCount(listWindow)){LVITEMW item={};item.mask=LVIF_TEXT;item.iItem=(int)i;item.pszText=const_cast<wchar_t*>(L"");ListView_InsertItem(listWindow,&item);}SetCell(i,0,std::to_wstring(h.ttl));SetCell(i,1,Wide(h.name));SetCell(i,2,Wide(mtr::Number(mtr::Loss(h.sent,h.received))));SetCell(i,3,std::to_wstring(h.sent));SetCell(i,4,std::to_wstring(h.received));SetCell(i,5,std::to_wstring(h.timeouts));SetCell(i,6,std::to_wstring(h.errors));int values[]={h.best,h.average,h.worst,h.last};for(int j=0;j<4;++j)SetCell(i,j+7,h.received?std::to_wstring(values[j]):L"—");LVITEMW item={};item.mask=LVIF_PARAM;item.iItem=i;item.lParam=h.ttl;ListView_SetItem(listWindow,&item);}
    }
    Put(rangeLabel,rangeFirst<0?L"Выдели участок графика мышью: таблица и экспорт покажут только этот период. История: 3600 запросов / TTL.":L"Выбран период: "+Wide(mtr::Number(rangeFirst,1))+L"–"+Wide(mtr::Number(rangeLast,1))+L" с от начала. Статистика рассчитана по сохранённым запросам.");InvalidateRect(graphWindow,NULL,FALSE);
}
std::vector<std::pair<size_t,const mtr::Hop*>> Graphs(){
    std::vector<std::pair<size_t,const mtr::Hop*>> graphs;if(IsDlgButtonChecked(mainWindow,CompareId)==BST_CHECKED){for(size_t i=0;i<displayed.reports.size();++i){const auto& r=displayed.reports[i];if(!r.hops.empty())graphs.push_back({i,&r.hops.back()});}}
    else if(selectedTarget<(int)displayed.reports.size()){const auto& r=displayed.reports[selectedTarget];const mtr::Hop* hop=nullptr;for(const auto& h:r.hops)if(h.ttl==selectedTtl)hop=&h;if(!hop&&!r.hops.empty())hop=&r.hops.back();if(hop)graphs.push_back({selectedTarget,hop});}return graphs;
}
double XTime(int x){double value=axisFirst+(x-graphArea.left)*(axisLast-axisFirst)/std::max(1L,graphArea.right-graphArea.left);return std::clamp(value,axisFirst,axisLast);}
LRESULT CALLBACK GraphProc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_LBUTTONDOWN){int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);if(x>=graphArea.left&&x<=graphArea.right&&y>=graphArea.top&&y<=graphArea.bottom){dragging=true;dragFirst=XTime(x);rangeFirst=rangeLast=dragFirst;SetCapture(window);}return 0;}
    if(msg==WM_MOUSEMOVE&&dragging){double current=XTime(GET_X_LPARAM(lp));rangeFirst=std::min(dragFirst,current);rangeLast=std::max(dragFirst,current);InvalidateRect(window,NULL,FALSE);return 0;}
    if(msg==WM_LBUTTONUP&&dragging){dragging=false;ReleaseCapture();if(rangeLast-rangeFirst<0.1)rangeFirst=rangeLast=-1;Refresh();return 0;}
    if(msg==WM_CAPTURECHANGED){dragging=false;return 0;}
    if(msg!=WM_PAINT)return DefWindowProcW(window,msg,wp,lp);
    PAINTSTRUCT paint;HDC screen=BeginPaint(window,&paint);RECT b;GetClientRect(window,&b);if(b.right<100||b.bottom<100){EndPaint(window,&paint);return 0;}
    HDC dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,b.right,b.bottom);auto oldBitmap=SelectObject(dc,bitmap);HBRUSH background=CreateSolidBrush(RGB(246,248,251));FillRect(dc,&b,background);DeleteObject(background);auto oldFont=SelectObject(dc,uiFont);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(23,32,51));
    auto graphs=Graphs();COLORREF colors[]={RGB(37,99,235),RGB(5,150,105),RGB(124,58,237)};
    int maxRtt=1;axisFirst=1e30;axisLast=0;
    for(const auto& entry:graphs){for(const auto& p:entry.second->samples){axisFirst=std::min(axisFirst,p.seconds);axisLast=std::max(axisLast,p.seconds);maxRtt=std::max(maxRtt,p.rtt);}}
    if(axisFirst==1e30)axisFirst=0;axisLast=std::max(axisFirst+1,axisLast);
    graphArea={70,65,b.right-20,b.bottom-52};
    if(rangeFirst>=0){RECT selection={graphArea.left+(int)((rangeFirst-axisFirst)*(graphArea.right-graphArea.left)/(axisLast-axisFirst)),graphArea.top,graphArea.left+(int)((rangeLast-axisFirst)*(graphArea.right-graphArea.left)/(axisLast-axisFirst)),graphArea.bottom};selection.left=std::max(selection.left,graphArea.left);selection.right=std::min(selection.right,graphArea.right);if(selection.right>selection.left){HBRUSH brush=CreateSolidBrush(RGB(219,234,254));FillRect(dc,&selection,brush);DeleteObject(brush);}}
    HPEN axes=CreatePen(PS_SOLID,1,RGB(148,163,184));auto oldPen=SelectObject(dc,axes);MoveToEx(dc,graphArea.left,graphArea.top,NULL);LineTo(dc,graphArea.left,graphArea.bottom);LineTo(dc,graphArea.right,graphArea.bottom);
    std::wstring scale=std::to_wstring(maxRtt)+L" мс";TextOutW(dc,5,graphArea.top,scale.c_str(),scale.size());TextOutW(dc,5,graphArea.bottom-13,L"0 мс",4);
    for(size_t n=0;n<graphs.size();++n){auto entry=graphs[n];const auto& h=*entry.second;COLORREF color=colors[entry.first%3];std::wstring title=Wide(displayed.reports[entry.first].target)+L" / TTL "+std::to_wstring(h.ttl);SetTextColor(dc,color);RECT label={12+(int)n*(b.right-24)/3,8,12+(int)(n+1)*(b.right-24)/3-8,32};DrawTextW(dc,title.c_str(),-1,&label,DT_SINGLELINE|DT_END_ELLIPSIS);HPEN line=CreatePen(PS_SOLID,2,color),red=CreatePen(PS_SOLID,1,RGB(220,38,38)),orange=CreatePen(PS_SOLID,1,RGB(217,119,6));bool joined=false;
        for(const auto& p:h.samples){int x=graphArea.left+(int)((p.seconds-axisFirst)*(graphArea.right-graphArea.left)/(axisLast-axisFirst));if(p.rtt<0){SelectObject(dc,comfort::Timeout(p.status)?red:orange);MoveToEx(dc,x,graphArea.top,NULL);LineTo(dc,x,graphArea.bottom);joined=false;}else{int y=graphArea.bottom-(int)(p.rtt*(double)(graphArea.bottom-graphArea.top)/maxRtt);SelectObject(dc,line);if(joined)LineTo(dc,x,y);else MoveToEx(dc,x,y,NULL);RECT dot={x-1,y-1,x+2,y+2};HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&dot,brush);DeleteObject(brush);joined=true;}}
        SelectObject(dc,axes);DeleteObject(line);DeleteObject(red);DeleteObject(orange);
    }
    SetTextColor(dc,RGB(23,32,51));std::wstring legend=L"Красный: тайм-аут • Оранжевый: ошибка ICMP • Цветные линии: задержка • Общая шкала времени";TextOutW(dc,12,35,legend.c_str(),legend.size());auto left=Wide(mtr::Number(axisFirst,1))+L" с",right=Wide(mtr::Number(axisLast,1))+L" с";TextOutW(dc,graphArea.left,graphArea.bottom+3,left.c_str(),left.size());TextOutW(dc,graphArea.right-100,graphArea.bottom+3,right.c_str(),right.size());const std::wstring foot=L"Выделение мышью — статистика участка. Отсутствие ответа не равно потере транзитного трафика.";TextOutW(dc,12,b.bottom-22,foot.c_str(),foot.size());
    SelectObject(dc,oldPen);SelectObject(dc,oldFont);BitBlt(screen,0,0,b.right,b.bottom,dc,0,0,SRCCOPY);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteObject(axes);DeleteDC(dc);EndPaint(window,&paint);return 0;
}
HWND Child(const wchar_t* cls,const wchar_t* text,DWORD style,int id){HWND h=CreateWindowExW(std::wstring(cls)==L"EDIT"?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,mainWindow,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);SendMessageW(h,WM_SETFONT,(WPARAM)uiFont,TRUE);return h;}
void Layout(){RECT r;GetClientRect(mainWindow,&r);int w=r.right,h=r.bottom;for(int i=0;i<3;++i)MoveWindow(inputs[i],12+i*(w-150)/3,12,(w-174)/3,200,TRUE);MoveWindow(startButton,w-126,12,114,28,TRUE);int x=12;int ids[]={NewId,RestoreId,TextId,HtmlId,CsvId,ProviderId};int widths[]={90,130,100,115,110,170};for(int i=0;i<6;++i){MoveWindow(GetDlgItem(mainWindow,ids[i]),x,52,widths[i],28,TRUE);x+=widths[i]+8;}
    MoveWindow(GetDlgItem(mainWindow,120),12,93,100,22,TRUE);MoveWindow(intervalEdit,116,88,70,26,TRUE);MoveWindow(GetDlgItem(mainWindow,121),202,93,118,22,TRUE);MoveWindow(timeoutEdit,320,88,80,26,TRUE);MoveWindow(familyCombo,416,88,118,150,TRUE);MoveWindow(compareCheck,552,88,240,26,TRUE);MoveWindow(GetDlgItem(mainWindow,ClearId),w-156,88,144,26,TRUE);
    MoveWindow(infoLabel,12,123,w-24,23,TRUE);MoveWindow(targetCombo,12,151,300,160,TRUE);MoveWindow(rangeLabel,324,151,w-336,34,TRUE);MoveWindow(listWindow,12,190,w-24,std::max(100,h-495),TRUE);MoveWindow(graphWindow,12,h-292,w-24,275,TRUE);
}
void CaptureWindow(HWND window,const std::filesystem::path& path){RECT rect;GetWindowRect(window,&rect);int w=rect.right-rect.left,h=rect.bottom-rect.top;HDC screen=GetDC(window),memory=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,w,h);auto previous=SelectObject(memory,bitmap);RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);PrintWindow(window,memory,2);BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;std::vector<unsigned char> pixels(w*h*4);SelectObject(memory,previous);GetDIBits(memory,bitmap,0,h,pixels.data(),&info,DIB_RGB_COLORS);BITMAPFILEHEADER header={};header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+pixels.size();std::ofstream out(path,std::ios::binary);out.write((char*)&header,sizeof(header));out.write((char*)&info.bmiHeader,sizeof(info.bmiHeader));out.write((char*)pixels.data(),pixels.size());DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,screen);}
void TestDone(){
    auto session=engine->Snapshot();bool ok=session.reports.size()==3&&engine->saveOk.load();for(const auto& r:session.reports)ok=ok&&!r.running&&!r.ended.empty()&&r.hops.size()==1&&r.hops[0].received>=2&&r.hops[0].received==r.hops[0].sent;
    try{auto read=comfort::Read(dataFolder/Wide(session.id+".wmtr"));ok=ok&&read.reports.size()==3&&read.reports[0].hops[0].sent==session.reports[0].hops[0].sent;restored=read;}catch(...){ok=false;}
    if(!ok){testResult=2;DestroyWindow(mainWindow);return;}
    if(testPhase==0){testPhase=1;phaseBase=session.reports[0].duration;ApplyRestored(restored);StartStop();return;}
    testResult=0;WriteText(testPrefix+L".txt",comfort::Text(session));WriteText(testPrefix+L".csv",comfort::Csv(session));WriteText(testPrefix+L".html",comfort::Html(session,true));CaptureWindow(mainWindow,testPrefix+L".bmp");DestroyWindow(mainWindow);
}
LRESULT CALLBACK MainProc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{mainWindow=window;NONCLIENTMETRICSW metrics={};metrics.cbSize=sizeof(metrics);SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0);uiFont=CreateFontIndirectW(&metrics.lfMessageFont);for(int i=0;i<3;++i)inputs[i]=Child(L"COMBOBOX",L"",CBS_DROPDOWN|CBS_AUTOHSCROLL|WS_VSCROLL|WS_TABSTOP,130+i);Put(inputs[0],selfTest?L"127.0.0.1":Wide(Gateway()));Put(inputs[1],selfTest?L"127.0.0.2":L"");Put(inputs[2],selfTest?L"127.0.0.3":L"1.1.1.1");startButton=Child(L"BUTTON",L"Начать",BS_PUSHBUTTON|WS_TABSTOP,StartId);
        Child(L"BUTTON",L"Новый",BS_PUSHBUTTON|WS_TABSTOP,NewId);Child(L"BUTTON",L"Восстановить",BS_PUSHBUTTON|WS_TABSTOP,RestoreId);Child(L"BUTTON",L"TXT",BS_PUSHBUTTON|WS_TABSTOP,TextId);Child(L"BUTTON",L"HTML",BS_PUSHBUTTON|WS_TABSTOP,HtmlId);Child(L"BUTTON",L"CSV",BS_PUSHBUTTON|WS_TABSTOP,CsvId);Child(L"BUTTON",L"Для провайдера",BS_PUSHBUTTON|WS_TABSTOP,ProviderId);
        Child(L"STATIC",L"Интервал, с:",SS_LEFT,120);intervalEdit=Child(L"EDIT",selfTest?L"0.5":L"1",ES_AUTOHSCROLL|WS_TABSTOP,IntervalId);Child(L"STATIC",L"Тайм-аут, мс:",SS_LEFT,121);timeoutEdit=Child(L"EDIT",selfTest?L"500":L"5000",ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,TimeoutId);familyCombo=Child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,FamilyId);for(const wchar_t* value:{L"Авто",L"IPv4",L"IPv6"})SendMessageW(familyCombo,CB_ADDSTRING,0,(LPARAM)value);SendMessageW(familyCombo,CB_SETCURSEL,selfTest?1:0,0);compareCheck=Child(L"BUTTON",L"Сравнить все цели",BS_AUTOCHECKBOX|WS_TABSTOP,CompareId);CheckDlgButton(window,CompareId,BST_CHECKED);Child(L"BUTTON",L"Весь период",BS_PUSHBUTTON|WS_TABSTOP,ClearId);
        infoLabel=Child(L"STATIC",L"Автосохранение каждые 10 секунд и при остановке. Данные сохраняются в папке data рядом с программой.",SS_LEFT,0);targetCombo=Child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,TargetId);rangeLabel=Child(L"STATIC",L"",SS_LEFT,0);listWindow=Child(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,ListId);ListView_SetExtendedListViewStyle(listWindow,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);const wchar_t* columns[]={L"TTL",L"IP-адрес",L"Нет ответа %",L"Отпр.",L"Получ.",L"Тайм-аут",L"Ошибки",L"Мин. мс",L"Сред. мс",L"Макс. мс",L"Посл. мс"};int widths[]={40,205,104,62,62,80,78,72,76,76,76};for(int i=0;i<11;++i){LVCOLUMNW c={};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<wchar_t*>(columns[i]);c.cx=widths[i];ListView_InsertColumn(listWindow,i,&c);}
        graphWindow=CreateWindowExW(0,L"ComfortGraph",L"",WS_CHILD|WS_VISIBLE|WS_BORDER,0,0,10,10,window,NULL,GetModuleHandleW(NULL),NULL);engine=std::make_unique<Engine>(window,dataFolder);Layout();if(!selfTest){LoadConfig();LoadLatest();}SetTimer(window,1,500,NULL);Refresh();if(selfTest)PostMessageW(window,WM_COMMAND,StartId,0);return 0;}
    case WM_SIZE:Layout();return 0;
    case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize={1060,720};return 0;
    case WM_COMMAND:{int id=LOWORD(wp);if(id==StartId)StartStop();else if(id==NewId)NewSession();else if(id==RestoreId)Restore();else if(id==TextId)Save(0);else if(id==HtmlId)Save(1);else if(id==CsvId)Save(2);else if(id==ProviderId)Save(3);else if(id==ClearId){rangeFirst=rangeLast=-1;Refresh();}else if(id==CompareId)InvalidateRect(graphWindow,NULL,FALSE);else if(id==TargetId&&HIWORD(wp)==CBN_SELCHANGE){selectedTarget=SendMessageW(targetCombo,CB_GETCURSEL,0,0);selectedTtl=-1;Refresh();}return 0;}
    case WM_NOTIFY:{auto hdr=(NMHDR*)lp;if(hdr->idFrom==ListId&&hdr->code==LVN_ITEMCHANGED){int row=ListView_GetNextItem(listWindow,-1,LVNI_SELECTED);if(row>=0){LVITEMW item={};item.mask=LVIF_PARAM;item.iItem=row;ListView_GetItem(listWindow,&item);selectedTtl=item.lParam;CheckDlgButton(window,CompareId,BST_UNCHECKED);InvalidateRect(graphWindow,NULL,FALSE);}}return 0;}
    case WM_APP+1:engine->Join();displayed=engine->Snapshot();uiRunning=false;EnableWindow(startButton,TRUE);SetControls(false);Refresh();if(closing){DestroyWindow(window);return 0;}if(selfTest)TestDone();return 0;
    case WM_TIMER:Refresh();if(selfTest&&uiRunning&&!engine->Stopping()&&!displayed.reports.empty()&&displayed.reports[0].duration-phaseBase>=(testPhase==0?11.0:1.5))StartStop();return 0;
    case WM_CLOSE:SaveConfig();if(uiRunning){closing=true;engine->Stop();EnableWindow(window,FALSE);return 0;}DestroyWindow(window);return 0;
    case WM_DESTROY:KillTimer(window,1);engine.reset();DeleteObject(uiFont);PostQuitMessage(0);return 0;
    }return DefWindowProcW(window,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR command,int show){
    std::wstring prefix=L"--self-test=";if(std::wstring(command).find(prefix)==0){selfTest=true;testPrefix=std::wstring(command).substr(prefix.size());dataFolder=std::filesystem::path(testPrefix).parent_path()/L"v2-test-data";}else{wchar_t path[32768];GetModuleFileNameW(NULL,path,32768);dataFolder=std::filesystem::path(path).parent_path()/L"data";}
    SetProcessDPIAware();WSADATA sockets;if(WSAStartup(MAKEWORD(2,2),&sockets))return 1;INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);WNDCLASSW graph={};graph.lpfnWndProc=GraphProc;graph.hInstance=instance;graph.lpszClassName=L"ComfortGraph";graph.hCursor=LoadCursor(NULL,IDC_CROSS);RegisterClassW(&graph);WNDCLASSW main={};main.lpfnWndProc=MainProc;main.hInstance=instance;main.lpszClassName=L"ComfortMain";main.hCursor=LoadCursor(NULL,IDC_ARROW);main.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);main.hIcon=LoadIcon(NULL,IDI_APPLICATION);RegisterClassW(&main);HWND window=CreateWindowExW(0,L"ComfortMain",L"WinMTR Comfort 2.0 — сравнение, история и отчёты",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1180,840,NULL,NULL,instance,NULL);if(!window){WSACleanup();return 1;}if(selfTest){SetWindowPos(window,NULL,-30000,-30000,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);ShowWindow(window,SW_SHOWNOACTIVATE);}else ShowWindow(window,show);UpdateWindow(window);MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}WSACleanup();return testResult;
}
