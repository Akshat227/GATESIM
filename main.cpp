#include <unistd.h>
#include "raylib.h"
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <map>
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;

constexpr Color BG_COLOR    = {  45,  45,  48, 255 };
constexpr Color BAR_COLOR   = {  30,  30,  32, 255 };
constexpr Color BTN_COLOR   = {  60,  63,  65, 255 };
constexpr Color BTN_HOVER   = {  80,  83,  86, 255 };
constexpr Color BTN_OUTLINE = { 120, 120, 125, 255 };
constexpr Color PANEL_BG    = {  28,  28,  30, 255 };
constexpr Color PANEL_CARD  = {  40,  40,  43, 255 };
constexpr Color SEL_COLOR   = { 255, 220,  50, 255 };

constexpr int FOOTER_H  = 64;
constexpr int PANEL_W   = 250;
constexpr int GATE_W    = 110;
constexpr int GATE_H    = 70;
constexpr int BTN_W     = 100;
constexpr int BTN_H     = 42;
constexpr int BTN_GAP   = 14;
constexpr int N_BTNS    = 5;
constexpr int NODE_SZ   = 12;
constexpr int NODE_HIT  = 14;
constexpr int CHKBOX_SZ = 14;
constexpr int MAX_NAME  = 31;

constexpr float ZOOM_MIN  = 0.15f;
constexpr float ZOOM_MAX  = 4.0f;
constexpr float ZOOM_STEP = 0.12f;

std::string CIRCUITS_DIR;

enum class GateType { NOT, AND, OR, INPUT, OUTPUT, CUSTOM };

struct ToolBtn { const char* label; GateType type; Color accent; };
const ToolBtn TOOLBAR[N_BTNS] = {
    { "NOT",    GateType::NOT,    {  70, 130, 180, 255 } },
    { "AND",    GateType::AND,    {  60, 160,  80, 255 } },
    { "OR",     GateType::OR,     { 180, 100,  50, 255 } },
    { "INPUT",  GateType::INPUT,  {  90,  90, 180, 255 } },
    { "OUTPUT", GateType::OUTPUT, { 180,  60,  60, 255 } },
};

struct Node { Vector2 pos; bool isOutput=false; bool signal=false; };

struct Wire { int gateA,nodeA,gateB,nodeB; };

struct Gate
{
    GateType  type;
    Rectangle rect;
    bool      dragging=false;
    Vector2   dragOffset={};
    bool      checked=false;
    bool      outputSig=false;
    bool      renaming=false;
    char      name[MAX_NAME+1]={};
    std::vector<Node> nodes;

    // CUSTOM gate internals
    std::string           circuitLabel;
    std::vector<Gate>     intGates;
    std::vector<Wire>     intWires;
    std::vector<int>      inPins;
    std::vector<int>      outPins;

    Color bodyColor() const
    {
        switch(type)
        {
            case GateType::NOT:    return {  70,130,180,255};
            case GateType::AND:    return {  60,160, 80,255};
            case GateType::OR:     return {180,100, 50,255};
            case GateType::INPUT:  return { 90, 90,180,255};
            case GateType::OUTPUT: return outputSig?Color{50,210,90,255}:Color{180,60,60,255};
            case GateType::CUSTOM:  return { 80, 60,120,255};
        }
        return GRAY;
    }

    const char* defaultLabel() const
    {
        switch(type)
        {
            case GateType::NOT:    return "NOT";
            case GateType::AND:    return "AND";
            case GateType::OR:     return "OR";
            case GateType::INPUT:  return "INPUT";
            case GateType::OUTPUT: return "OUTPUT";
            case GateType::CUSTOM:  return circuitLabel.c_str();
        }
        return "";
    }

    const char* displayName() const { return name[0]?name:defaultLabel(); }

    void rebuildNodes()
    {
        if(type==GateType::CUSTOM){ rebuildCustomNodes(); return; }
        std::vector<bool> sigs(nodes.size(),false);
        for(int i=0;i<(int)nodes.size();i++) sigs[i]=nodes[i].signal;
        nodes.clear();
        float x=rect.x,y=rect.y,w=rect.width,h=rect.height;
        switch(type)
        {
            case GateType::INPUT:
                nodes.push_back({{x+w,y+h/2},true}); break;
            case GateType::OUTPUT:
                nodes.push_back({{x,y+h/2},false}); break;
            case GateType::NOT:
                nodes.push_back({{x,y+h/2},false});
                nodes.push_back({{x+w,y+h/2},true}); break;
            case GateType::AND: case GateType::OR:
                nodes.push_back({{x,y+h/3},false});
                nodes.push_back({{x,y+h*2/3},false});
                nodes.push_back({{x+w,y+h/2},true}); break;
        }
        for(int i=0;i<(int)nodes.size()&&i<(int)sigs.size();i++) nodes[i].signal=sigs[i];
    }
    void rebuildCustomNodes()
    {
        std::vector<bool> sigs(nodes.size(),false);
        for(int i=0;i<(int)nodes.size();i++) sigs[i]=nodes[i].signal;
        nodes.clear();
        int nIn=(int)inPins.size(), nOut=(int)outPins.size();
        int maxP=std::max(std::max(nIn,nOut),1);
        rect.height=std::max(70.f,(float)maxP*34.f+20.f);
        float x2=rect.x,y2=rect.y,w2=rect.width,h2=rect.height;
        for(int k=0;k<nIn;k++)
        {
            float fy=y2+(k+1)*h2/(nIn+1);
            bool sig=((int)sigs.size()>k)?sigs[k]:false;
            nodes.push_back({{x2,fy},false,sig});
        }
        for(int k=0;k<nOut;k++)
        {
            float fy=y2+(k+1)*h2/(nOut+1);
            bool sig=((int)sigs.size()>nIn+k)?sigs[nIn+k]:false;
            nodes.push_back({{x2+w2,fy},true,sig});
        }
    }

    Rectangle checkboxRect() const
    {
        return {rect.x+rect.width/2-CHKBOX_SZ/2,rect.y-CHKBOX_SZ-4,(float)CHKBOX_SZ,(float)CHKBOX_SZ};
    }

    Rectangle deleteRect() const
    {
        return {rect.x+rect.width-16,rect.y-14,14,14};
    }
};


struct SavedCircuit
{
    std::string name;
    struct GateData { GateType type; float relX,relY,w,h; char name[MAX_NAME+1]; bool checked; };
    struct WireData { int gA,nA,gB,nB; };
    std::vector<GateData> gates;
    std::vector<WireData> wires;
};

static void saveCircuitToFile(const SavedCircuit& sc)
{
    fs::create_directories(CIRCUITS_DIR);
    std::string safe=sc.name;
    for(auto& c:safe) if(c=='/'||c=='\\'||c=='|'||c=='\n') c='_';
    std::ofstream f(CIRCUITS_DIR+safe+".lgc");
    if(!f) return;
    f<<"CIRCUIT|"<<sc.name<<"\n";
    f<<"GATECOUNT|"<<sc.gates.size()<<"\n";
    for(auto& g:sc.gates)
        f<<"GATE|"<<(int)g.type<<"|"<<g.relX<<"|"<<g.relY<<"|"<<g.w<<"|"<<g.h<<"|"<<(int)g.checked<<"|"<<g.name<<"\n";
    f<<"WIRECOUNT|"<<sc.wires.size()<<"\n";
    for(auto& w:sc.wires)
        f<<"WIRE|"<<w.gA<<"|"<<w.nA<<"|"<<w.gB<<"|"<<w.nB<<"\n";
}

static bool loadCircuitFromFile(const std::string& path, SavedCircuit& sc)
{
    std::ifstream f(path);
    if(!f) return false;
    std::string line;
    while(std::getline(f,line))
    {
        std::istringstream ss(line);
        std::string tok;
        std::vector<std::string> parts;
        while(std::getline(ss,tok,'|')) parts.push_back(tok);
        if(parts.empty()) continue;
        if(parts[0]=="CIRCUIT"&&parts.size()>=2)
        {
            sc.name=parts[1];
        }
        else if(parts[0]=="GATE"&&parts.size()>=7)
        {
            try
            {
                SavedCircuit::GateData gd={};
                gd.type    = (GateType)std::stoi(parts[1]);
                gd.relX    = std::stof(parts[2]);
                gd.relY    = std::stof(parts[3]);
                gd.w       = std::stof(parts[4]);
                gd.h       = std::stof(parts[5]);
                gd.checked = (bool)std::stoi(parts[6]);
                if(parts.size()>=8) strncpy(gd.name, parts[7].c_str(), MAX_NAME);
                gd.name[MAX_NAME] = '\0';
                if(gd.w<=0||gd.h<=0){ gd.w=(float)GATE_W; gd.h=(float)GATE_H; }
                sc.gates.push_back(gd);
            }
            catch(...){}
        }
        else if(parts[0]=="WIRE"&&parts.size()>=5)
        {
            try
            {
                SavedCircuit::WireData wd={};
                wd.gA=std::stoi(parts[1]); wd.nA=std::stoi(parts[2]);
                wd.gB=std::stoi(parts[3]); wd.nB=std::stoi(parts[4]);
                sc.wires.push_back(wd);
            }
            catch(...){}
        }
    }
    int gcount=(int)sc.gates.size();
    sc.wires.erase(
        std::remove_if(sc.wires.begin(),sc.wires.end(),
            [gcount](const SavedCircuit::WireData& w){
                return w.gA<0||w.gA>=gcount||w.gB<0||w.gB>=gcount;
            }),
        sc.wires.end());
    return !sc.name.empty()&&!sc.gates.empty();
}

static std::vector<SavedCircuit> loadAllCircuits()
{
    std::vector<SavedCircuit> result;
    if(!fs::exists(CIRCUITS_DIR)) return result;
    for(auto& entry:fs::directory_iterator(CIRCUITS_DIR))
        if(entry.path().extension()==".lgc")
        {
            SavedCircuit sc;
            if(loadCircuitFromFile(entry.path().string(),sc)) result.push_back(sc);
        }
    return result;
}

static void placeCircuit(const SavedCircuit& sc,Vector2 centre,
                          std::vector<Gate>& gates,std::vector<Wire>& wires,std::vector<bool>& selected)
{
    int base=(int)gates.size();
    for(auto& gd:sc.gates)
    {
        Gate g; g.type=gd.type;
        g.rect={centre.x+gd.relX,centre.y+gd.relY,gd.w,gd.h};
        g.checked=gd.checked; strncpy(g.name,gd.name,MAX_NAME);
        g.rebuildNodes(); gates.push_back(g); selected.push_back(true);
    }
    for(auto& wd:sc.wires) wires.push_back({base+wd.gA,wd.nA,base+wd.gB,wd.nB});
}

static void placeAsNode(const SavedCircuit& sc,Vector2 centre,
                        std::vector<Gate>& gates,std::vector<bool>& selected)
{
    Gate g;
    g.type=GateType::CUSTOM;
    g.circuitLabel=sc.name;

    for(auto& gd:sc.gates)
    {
        Gate ig; ig.type=gd.type;
        ig.rect={gd.relX,gd.relY,gd.w,gd.h};
        ig.checked=gd.checked;
        strncpy(ig.name,gd.name,MAX_NAME); ig.name[MAX_NAME]='\0';
        ig.rebuildNodes();
        g.intGates.push_back(ig);
    }
    for(auto& wd:sc.wires)
        g.intWires.push_back({wd.gA,wd.nA,wd.gB,wd.nB});

    for(int i=0;i<(int)g.intGates.size();i++)
    {
        if(g.intGates[i].type==GateType::INPUT)  g.inPins.push_back(i);
        if(g.intGates[i].type==GateType::OUTPUT) g.outPins.push_back(i);
    }

    int maxP=std::max(std::max((int)g.inPins.size(),(int)g.outPins.size()),1);
    float h=std::max(70.f,(float)maxP*34.f+20.f);
    g.rect={centre.x-70.f,centre.y-h/2.f,140.f,h};
    g.rebuildCustomNodes();

    gates.push_back(g);
    selected.push_back(false);
}

static void deleteCircuitFile(const std::string& name)
{
    std::string safe=name;
    for(auto& c:safe) if(c=='/'||c=='\\'||c=='|'||c=='\n') c='_';
    fs::remove(CIRCUITS_DIR+safe+".lgc");
}

static void buildSavePayload(const std::vector<Gate>& gates,const std::vector<bool>& selected,
                              const std::vector<Wire>& wires,const char* cname,SavedCircuit& out)
{
    std::vector<int> selIdx;
    for(int i=0;i<(int)gates.size();i++) if(selected[i]) selIdx.push_back(i);
    if(selIdx.empty()) return;
    float cx=0,cy=0;
    for(int i:selIdx){ cx+=gates[i].rect.x+gates[i].rect.width/2; cy+=gates[i].rect.y+gates[i].rect.height/2; }
    cx/=selIdx.size(); cy/=selIdx.size();
    out.name=cname;
    std::map<int,int> idxMap;
    for(int si=0;si<(int)selIdx.size();si++)
    {
        int gi=selIdx[si]; idxMap[gi]=si;
        SavedCircuit::GateData gd={};
        gd.type=gates[gi].type;
        gd.relX=gates[gi].rect.x-cx; gd.relY=gates[gi].rect.y-cy;
        gd.w=gates[gi].rect.width;   gd.h=gates[gi].rect.height;
        gd.checked=gates[gi].checked; strncpy(gd.name,gates[gi].name,MAX_NAME);
        out.gates.push_back(gd);
    }
    for(const auto& w:wires)
        if(idxMap.count(w.gateA)&&idxMap.count(w.gateB))
            out.wires.push_back({idxMap[w.gateA],w.nodeA,idxMap[w.gateB],w.nodeB});
}

static Vector2 screenToWorld(Vector2 s,Camera2D cam){ return GetScreenToWorld2D(s,cam); }

static void DrawBezier(Vector2 a,Vector2 b,Color col,float thick)
{
    float dx=std::max(fabsf(b.x-a.x)*0.5f,60.f);
    Vector2 cp1={a.x+dx,a.y},cp2={b.x-dx,b.y},prev=a;
    for(int i=1;i<=40;i++)
    {
        float t=i/40.f,it=1-t;
        Vector2 cur={it*it*it*a.x+3*it*it*t*cp1.x+3*it*t*t*cp2.x+t*t*t*b.x,
                     it*it*it*a.y+3*it*it*t*cp1.y+3*it*t*t*cp2.y+t*t*t*b.y};
        DrawLineEx(prev,cur,thick,col); prev=cur;
    }
}

static float v2dist(Vector2 a,Vector2 b){float dx=a.x-b.x,dy=a.y-b.y;return sqrtf(dx*dx+dy*dy);}

static float pSegDist(Vector2 p,Vector2 a,Vector2 b)
{
    float dx=b.x-a.x,dy=b.y-a.y,l2=dx*dx+dy*dy;
    if(l2<1e-6f) return v2dist(p,a);
    float t=std::max(0.f,std::min(1.f,((p.x-a.x)*dx+(p.y-a.y)*dy)/l2));
    return v2dist(p,{a.x+t*dx,a.y+t*dy});
}

static float bezierDist(Vector2 from,Vector2 to,Vector2 p)
{
    float dx=std::max(fabsf(to.x-from.x)*0.5f,60.f);
    Vector2 cp1={from.x+dx,from.y},cp2={to.x-dx,to.y},prev=from; float mn=1e9f;
    for(int i=1;i<=40;i++)
    {
        float t=i/40.f,it=1-t;
        Vector2 cur={it*it*it*from.x+3*it*it*t*cp1.x+3*it*t*t*cp2.x+t*t*t*to.x,
                     it*it*it*from.y+3*it*it*t*cp1.y+3*it*t*t*cp2.y+t*t*t*to.y};
        mn=std::min(mn,pSegDist(p,prev,cur)); prev=cur;
    }
    return mn;
}

static float nodeDist(Vector2 a,Vector2 b){float dx=a.x-b.x,dy=a.y-b.y;return sqrtf(dx*dx+dy*dy);}

static void propagate(std::vector<Gate>& gates,const std::vector<Wire>& wires)
{
    for(auto& g:gates) for(auto& n:g.nodes) n.signal=false;
    for(auto& g:gates)
        if(g.type==GateType::INPUT){g.outputSig=g.checked;if(!g.nodes.empty())g.nodes[0].signal=g.checked;}
    for(int pass=0;pass<(int)gates.size()+1;pass++)
    {
        for(const auto& w:wires) gates[w.gateB].nodes[w.nodeB].signal=gates[w.gateA].nodes[w.nodeA].signal;
        for(auto& g:gates)
        {
            switch(g.type)
            {
                case GateType::NOT:
                    if(g.nodes.size()>=2){g.outputSig=!g.nodes[0].signal;g.nodes[1].signal=g.outputSig;} break;
                case GateType::AND:
                    if(g.nodes.size()>=3){g.outputSig=g.nodes[0].signal&&g.nodes[1].signal;g.nodes[2].signal=g.outputSig;} break;
                case GateType::OR:
                    if(g.nodes.size()>=3){g.outputSig=g.nodes[0].signal||g.nodes[1].signal;g.nodes[2].signal=g.outputSig;} break;
                case GateType::OUTPUT:
                    if(!g.nodes.empty()) g.outputSig=g.nodes[0].signal; break;
                case GateType::CUSTOM:
                {
                    for(int k=0;k<(int)g.inPins.size()&&k<(int)g.nodes.size();k++)
                        g.intGates[g.inPins[k]].checked=g.nodes[k].signal;
                    for(auto& ig:g.intGates) for(auto& n:ig.nodes) n.signal=false;
                    for(auto& ig:g.intGates)
                        if(ig.type==GateType::INPUT){ig.outputSig=ig.checked;if(!ig.nodes.empty())ig.nodes[0].signal=ig.checked;}
                    for(int p=0;p<(int)g.intGates.size()+1;p++)
                    {
                        for(const auto& w:g.intWires) g.intGates[w.gateB].nodes[w.nodeB].signal=g.intGates[w.gateA].nodes[w.nodeA].signal;
                        for(auto& ig:g.intGates)
                        {
                            switch(ig.type)
                            {
                                case GateType::NOT: if(ig.nodes.size()>=2){ig.outputSig=!ig.nodes[0].signal;ig.nodes[1].signal=ig.outputSig;} break;
                                case GateType::AND: if(ig.nodes.size()>=3){ig.outputSig=ig.nodes[0].signal&&ig.nodes[1].signal;ig.nodes[2].signal=ig.outputSig;} break;
                                case GateType::OR:  if(ig.nodes.size()>=3){ig.outputSig=ig.nodes[0].signal||ig.nodes[1].signal;ig.nodes[2].signal=ig.outputSig;} break;
                                case GateType::OUTPUT: if(!ig.nodes.empty())ig.outputSig=ig.nodes[0].signal; break;
                                default: break;
                            }
                        }
                    }
                    int nIn=(int)g.inPins.size();
                    for(int k=0;k<(int)g.outPins.size();k++)
                        if(nIn+k<(int)g.nodes.size())
                            g.nodes[nIn+k].signal=g.intGates[g.outPins[k]].outputSig;
                    break;
                }
                default: break;
            }
        }
    }
}

static void DrawGate(const Gate& g,bool hovered,bool sel)
{
    Color body=g.bodyColor();
    Color dark={(unsigned char)(body.r/2),(unsigned char)(body.g/2),(unsigned char)(body.b/2),255};
    DrawRectangle((int)g.rect.x+4,(int)g.rect.y+4,(int)g.rect.width,(int)g.rect.height,{0,0,0,80});
    DrawRectangleRec(g.rect,body);
    DrawRectangle((int)g.rect.x,(int)g.rect.y,(int)g.rect.width,4,{255,255,255,40});
    Color border=sel?SEL_COLOR:(hovered?Color{255,80,80,220}:dark);
    DrawRectangleLinesEx(g.rect,sel?3.f:2.f,border);
    if(sel) DrawRectangleLinesEx({g.rect.x-3,g.rect.y-3,g.rect.width+6,g.rect.height+6},1,{255,220,50,80});

    const char* lbl=g.displayName();
    int fs=17,tw=MeasureText(lbl,fs);
    while(tw>(int)g.rect.width-6&&fs>9){fs--;tw=MeasureText(lbl,fs);}
    DrawText(lbl,(int)(g.rect.x+g.rect.width/2-tw/2),(int)(g.rect.y+g.rect.height/2-fs/2),fs,WHITE);

    if(g.renaming)
    {
        DrawRectangleLinesEx(g.rect,2,YELLOW);
        if((int)(GetTime()*2)%2==0)
            DrawRectangle((int)(g.rect.x+g.rect.width/2+tw/2+2),(int)(g.rect.y+g.rect.height/2-fs/2),2,fs,YELLOW);
    }

    Rectangle dr=g.deleteRect();
    bool dHov=CheckCollisionPointRec(GetMousePosition(),dr);
    DrawRectangleRec(dr,dHov?Color{255,60,60,255}:Color{100,40,40,200});
    DrawRectangleLinesEx(dr,1,{200,80,80,255});
    int xs=8;
    DrawText("x",(int)(dr.x+(dr.width-MeasureText("x",xs))/2),(int)(dr.y+(dr.height-xs)/2),xs,WHITE);

    if(g.type==GateType::INPUT)
    {
        Rectangle cb=g.checkboxRect();
        DrawRectangleRec(cb,g.checked?Color{80,220,80,255}:Color{60,60,62,255});
        DrawRectangleLinesEx(cb,1,LIGHTGRAY);
        if(g.checked)
        {
            DrawLineEx({cb.x+2,cb.y+cb.height*0.55f},{cb.x+cb.width*0.42f,cb.y+cb.height-2},2,WHITE);
            DrawLineEx({cb.x+cb.width*0.42f,cb.y+cb.height-2},{cb.x+cb.width-2,cb.y+3},2,WHITE);
        }
    }

    if(g.type==GateType::OUTPUT)
    {
        const char* badge=g.outputSig?"1":"0";
        Color bc=g.outputSig?Color{50,230,90,255}:Color{200,60,60,255};
        int bfs=14,btw=MeasureText(badge,bfs);
        Rectangle pill={g.rect.x+g.rect.width/2-btw/2-6,g.rect.y-22,(float)(btw+12),18};
        DrawRectangleRec(pill,bc); DrawRectangleLinesEx(pill,1,dark);
        DrawText(badge,(int)(pill.x+(pill.width-btw)/2),(int)(pill.y+2),bfs,WHITE);
    }

    if(g.type==GateType::CUSTOM)
    {
        // pin labels
        int nIn=(int)g.inPins.size(),nOut=(int)g.outPins.size();
        int pfs=10;
        for(int k=0;k<nIn;k++)
        {
            const char* lbl2=g.intGates[g.inPins[k]].displayName();
            int tw2=MeasureText(lbl2,pfs);
            float fy=g.nodes[k].pos.y;
            DrawText(lbl2,(int)(g.rect.x+6),(int)(fy-pfs/2),pfs,{220,220,220,200});
        }
        for(int k=0;k<nOut;k++)
        {
            const char* lbl2=g.intGates[g.outPins[k]].displayName();
            int tw2=MeasureText(lbl2,pfs);
            float fy=g.nodes[nIn+k].pos.y;
            DrawText(lbl2,(int)(g.rect.x+g.rect.width-tw2-6),(int)(fy-pfs/2),pfs,{220,220,220,200});
        }
        // divider line
        DrawLine((int)(g.rect.x+g.rect.width/2),(int)(g.rect.y+4),
                 (int)(g.rect.x+g.rect.width/2),(int)(g.rect.y+g.rect.height-4),
                 {255,255,255,20});
    }

    for(const auto& n:g.nodes)
    {
        float half=NODE_SZ/2.f;
        Rectangle nr={n.pos.x-half,n.pos.y-half,(float)NODE_SZ,(float)NODE_SZ};
        DrawRectangleRec(nr,n.signal?YELLOW:LIGHTGRAY);
        DrawRectangleLinesEx(nr,1,{0,0,0,180});
    }
}

static bool DrawToolbarButton(int x,int y,int w,int h,const char* label,Color accent)
{
    Rectangle r={(float)x,(float)y,(float)w,(float)h};
    bool hov=CheckCollisionPointRec(GetMousePosition(),r);
    DrawRectangleRec(r,hov?BTN_HOVER:BTN_COLOR);
    DrawRectangle(x,y,4,h,accent);
    DrawRectangleLinesEx(r,1,BTN_OUTLINE);
    int fs=17,tw=MeasureText(label,fs);
    DrawText(label,x+(w-tw)/2,y+(h-fs)/2,fs,WHITE);
    return hov&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

static void DrawSavedPanel(int px,int py,int pw,int ph,
                            std::vector<SavedCircuit>& circuits,
                            float& scroll,int& placeIdx,int& deleteIdx)
{
    DrawRectangle(px,py,pw,ph,PANEL_BG);
    DrawLine(px,py,px,py+ph,{55,55,58,255});
    DrawRectangle(px,py,pw,38,{22,22,24,255});
    DrawLine(px,py+38,px+pw,py+38,{55,55,58,255});
    DrawText("  Saved Circuits",px+8,py+11,15,{180,180,185,255});

    int ly=py+42,lh=ph-42;
    if(circuits.empty())
    {
        DrawText("No saved circuits yet.",px+12,ly+14,13,{80,80,84,255});
        DrawText("Select gates, Ctrl+S to save.",px+12,ly+34,12,{70,70,74,255});
        return;
    }

    float cardH=58,cardGap=5;
    float total=circuits.size()*(cardH+cardGap);
    float maxScroll=std::max(0.f,total-lh);
    scroll=std::max(0.f,std::min(scroll,maxScroll));

    Vector2 mouse=GetMousePosition();
    if(CheckCollisionPointRec(mouse,{(float)px,(float)ly,(float)pw,(float)lh}))
    { scroll-=GetMouseWheelMove()*24; scroll=std::max(0.f,std::min(scroll,maxScroll)); }

    for(int i=0;i<(int)circuits.size();i++)
    {
        float cy=ly+i*(cardH+cardGap)-scroll;
        if(cy+cardH<ly||cy>ly+lh) continue;
        Rectangle card={(float)px+6,cy,(float)pw-12,cardH};
        DrawRectangleRec(card,PANEL_CARD);
        DrawRectangleLinesEx(card,1,{60,60,64,255});
        DrawText(circuits[i].name.c_str(),(int)card.x+8,(int)cy+7,14,WHITE);
        char info[48];
        sprintf(info,"%d gates  %d wires",(int)circuits[i].gates.size(),(int)circuits[i].wires.size());
        DrawText(info,(int)card.x+8,(int)cy+26,11,{110,110,115,255});

        Rectangle placeBtn={card.x+card.width-94,cy+15,60,26};
        bool ph2=CheckCollisionPointRec(mouse,placeBtn);
        DrawRectangleRec(placeBtn,ph2?Color{55,180,75,255}:Color{35,130,55,255});
        DrawRectangleLinesEx(placeBtn,1,{80,200,100,255});
        int pfs=13,ptw=MeasureText("Place",pfs);
        DrawText("Place",(int)(placeBtn.x+(placeBtn.width-ptw)/2),(int)(placeBtn.y+6),pfs,WHITE);
        if(ph2&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) placeIdx=i;

        Rectangle delBtn={card.x+card.width-30,cy+15,26,26};
        bool dh=CheckCollisionPointRec(mouse,delBtn);
        DrawRectangleRec(delBtn,dh?Color{220,55,55,255}:Color{130,35,35,255});
        DrawRectangleLinesEx(delBtn,1,{200,80,80,255});
        int xfs=13,xtw=MeasureText("x",xfs);
        DrawText("x",(int)(delBtn.x+(delBtn.width-xtw)/2),(int)(delBtn.y+6),xfs,WHITE);
        if(dh&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) deleteIdx=i;
    }

    if(total>lh)
    {
        float barH=(float)lh/total*(float)lh;
        float barY=ly+scroll/(total-lh)*(lh-barH);
        DrawRectangle(px+pw-5,(int)barY,4,(int)barH,{80,80,85,255});
    }
}

static void DrawSaveModal(int SW,int SH,char* buf,bool& cancelled,bool& confirmed)
{
    DrawRectangle(0,0,SW,SH,{0,0,0,160});
    int mw=420,mh=160,mx=SW/2-mw/2,my=SH/2-mh/2;
    DrawRectangle(mx,my,mw,mh,{32,32,35,255});
    DrawRectangleLinesEx({(float)mx,(float)my,(float)mw,(float)mh},2,{80,80,84,255});
    DrawRectangle(mx,my,mw,36,{22,22,25,255});
    DrawLine(mx,my+36,mx+mw,my+36,{60,60,64,255});
    DrawText("Save Circuit as Reusable Node",mx+12,my+10,16,WHITE);
    DrawText("Name:",mx+12,my+50,14,LIGHTGRAY);
    Rectangle inp={(float)mx+12,(float)my+70,(float)mw-24,30};
    DrawRectangleRec(inp,{22,22,25,255});
    DrawRectangleLinesEx(inp,2,YELLOW);
    DrawText(buf,(int)inp.x+8,(int)inp.y+7,16,WHITE);
    if((int)(GetTime()*2)%2==0)
    { int tw=MeasureText(buf,16); DrawRectangle((int)inp.x+8+tw,(int)inp.y+7,2,16,YELLOW); }

    Rectangle cancelR={(float)mx+12,(float)my+mh-40,90,28};
    bool ch=CheckCollisionPointRec(GetMousePosition(),cancelR);
    DrawRectangleRec(cancelR,ch?Color{70,70,74,255}:Color{50,50,54,255});
    DrawRectangleLinesEx(cancelR,1,{90,90,94,255});
    DrawText("Cancel",(int)(cancelR.x+10),(int)(cancelR.y+7),14,LIGHTGRAY);
    if(ch&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) cancelled=true;

    bool canSave=strlen(buf)>0;
    Rectangle saveR={(float)mx+mw-110,(float)my+mh-40,98,28};
    bool sh=CheckCollisionPointRec(GetMousePosition(),saveR)&&canSave;
    DrawRectangleRec(saveR,sh?Color{55,180,75,255}:(canSave?Color{35,140,58,255}:Color{30,50,35,255}));
    DrawRectangleLinesEx(saveR,1,canSave?Color{80,210,100,255}:Color{45,65,48,255});
    DrawText("Save",(int)(saveR.x+28),(int)(saveR.y+7),14,canSave?WHITE:Color{70,70,70,255});
    if(sh&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) confirmed=true;
    if(canSave&&IsKeyPressed(KEY_ENTER)) confirmed=true;
    if(IsKeyPressed(KEY_ESCAPE)) cancelled=true;
}

static void deleteGate(int idx,std::vector<Gate>& gates,std::vector<Wire>& wires,
                        std::vector<bool>& selected,
                        int& draggedIdx,int& renamingIdx,int& wireGateA,bool& wiring)
{
    wires.erase(std::remove_if(wires.begin(),wires.end(),
        [idx](const Wire& w){return w.gateA==idx||w.gateB==idx;}),wires.end());
    for(auto& w:wires){ if(w.gateA>idx)w.gateA--; if(w.gateB>idx)w.gateB--; }
    gates.erase(gates.begin()+idx);
    selected.erase(selected.begin()+idx);
    if(draggedIdx==idx) draggedIdx=-1;    else if(draggedIdx>idx)  draggedIdx--;
    if(renamingIdx==idx) renamingIdx=-1;  else if(renamingIdx>idx) renamingIdx--;
    if(wireGateA==idx){wiring=false;wireGateA=-1;} else if(wireGateA>idx) wireGateA--;
}

int main(int argc, char* argv[])
{
    {
        char buf[4096]={};
        ssize_t n=readlink("/proc/self/exe",buf,sizeof(buf)-1);
        if(n>0)
        {
            fs::path exe(buf);
            CIRCUITS_DIR=(exe.parent_path()/"circuits").string()+"/";
        }
        else
        {
            fs::path exe=fs::absolute(fs::path(argv[0]));
            CIRCUITS_DIR=(exe.parent_path()/"circuits").string()+"/";
        }

    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280,720,"Logic Gate Simulator");
    SetWindowMinSize(640,400);
    SetTargetFPS(60);

    Camera2D cam={};
    cam.zoom=1.f;
    cam.offset={(float)(GetScreenWidth()-PANEL_W)/2.f,(float)(GetScreenHeight()-FOOTER_H)/2.f};
    cam.target=cam.offset;

    bool    panning=false;
    Vector2 panStart={},camTargetAtPanStart={};

    std::vector<Gate> gates;
    std::vector<Wire> wires;
    std::vector<bool> selected;

    std::vector<SavedCircuit> savedCircuits=loadAllCircuits();
    float panelScroll=0;

    int    draggedIdx=-1,renamingIdx=-1;
    bool   wiring=false;
    int    wireGateA=-1,wireNodeA=-1;
    double lastClickTime=-1.0;
    int    lastClickGate=-1;
    bool   savingModal=false;
    char   circuitNameBuf[MAX_NAME+1]={};

    bool    dragSelecting=false;
    Vector2 dragSelStartW={};
    Vector2 dragSelCurW={};

    while(!WindowShouldClose())
    {
        const int SW      = GetScreenWidth();
        const int SH      = GetScreenHeight();
        const int canvasW = SW-PANEL_W;
        const int canvasH = SH-FOOTER_H;
        const int totalBarW=N_BTNS*BTN_W+(N_BTNS-1)*BTN_GAP;
        const int barStartX=(canvasW-totalBarW)/2;

        Vector2 mouseScreen=GetMousePosition();
        bool mouseInCanvas=(mouseScreen.x<canvasW&&mouseScreen.y<canvasH);
        Vector2 mouseWorld=screenToWorld(mouseScreen,cam);
        bool panKey=false,panBtn=false;

        if(savingModal)
        {
            int ch;
            while((ch=GetCharPressed())>0)
            { int len=(int)strlen(circuitNameBuf); if(len<MAX_NAME&&ch>=32){circuitNameBuf[len]=(char)ch;circuitNameBuf[len+1]='\0';} }
            if(IsKeyPressed(KEY_BACKSPACE))
            { int len=(int)strlen(circuitNameBuf); if(len>0) circuitNameBuf[len-1]='\0'; }
            bool cancelled=false,confirmed=false;
            DrawSaveModal(SW,SH,circuitNameBuf,cancelled,confirmed);
            if(confirmed)
            {
                SavedCircuit sc;
                buildSavePayload(gates,selected,wires,circuitNameBuf,sc);
                if(!sc.gates.empty())
                {
                    saveCircuitToFile(sc);
                    auto it=std::find_if(savedCircuits.begin(),savedCircuits.end(),[&](auto& s){return s.name==sc.name;});
                    if(it!=savedCircuits.end()) *it=sc; else savedCircuits.push_back(sc);
                }
                savingModal=false; memset(circuitNameBuf,0,sizeof(circuitNameBuf));
            }
            if(cancelled){ savingModal=false; memset(circuitNameBuf,0,sizeof(circuitNameBuf)); }
            goto draw;
        }

        if((IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL))&&IsKeyPressed(KEY_S))
        {
            bool any=std::any_of(selected.begin(),selected.end(),[](bool b){return b;});
            if(any){ savingModal=true; memset(circuitNameBuf,0,sizeof(circuitNameBuf)); }
        }

        if(IsKeyPressed(KEY_ESCAPE)&&!wiring&&!savingModal)
            std::fill(selected.begin(),selected.end(),false);

        if((IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL))&&IsKeyPressed(KEY_A))
            std::fill(selected.begin(),selected.end(),true);

        if(IsKeyPressed(KEY_DELETE)||IsKeyPressed(KEY_BACKSPACE))
            for(int i=(int)gates.size()-1;i>=0;i--)
                if(selected[i]) deleteGate(i,gates,wires,selected,draggedIdx,renamingIdx,wireGateA,wiring);

        if(mouseInCanvas)
        {
            float wheel=GetMouseWheelMove();
            if(IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL))
            { if(IsKeyPressed(KEY_EQUAL))wheel+=1; if(IsKeyPressed(KEY_MINUS))wheel-=1; }
            if(wheel!=0.f)
            {
                float nz=std::max(ZOOM_MIN,std::min(ZOOM_MAX,cam.zoom+wheel*ZOOM_STEP*cam.zoom));
                float r=cam.zoom/nz;
                cam.target.x=mouseWorld.x+(cam.target.x-mouseWorld.x)*r;
                cam.target.y=mouseWorld.y+(cam.target.y-mouseWorld.y)*r;
                cam.zoom=nz;
            }
        }

        panKey=IsKeyDown(KEY_SPACE); panBtn=IsMouseButtonDown(MOUSE_MIDDLE_BUTTON);
        if((panBtn||panKey)&&mouseInCanvas)
        {
            if(!panning){panning=true;panStart=mouseScreen;camTargetAtPanStart=cam.target;SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);}
            Vector2 d={(mouseScreen.x-panStart.x)/cam.zoom,(mouseScreen.y-panStart.y)/cam.zoom};
            cam.target.x=camTargetAtPanStart.x-d.x; cam.target.y=camTargetAtPanStart.y-d.y;
        }
        else if(panning){panning=false;SetMouseCursor(MOUSE_CURSOR_DEFAULT);}

        if(IsKeyPressed(KEY_R))
        {
            cam.zoom=1.f;
            cam.offset={(float)canvasW/2.f,(float)canvasH/2.f};
            cam.target=cam.offset;
        }

        if(renamingIdx>=0)
        {
            Gate& rg=gates[renamingIdx];
            int ch;
            while((ch=GetCharPressed())>0){int len=(int)strlen(rg.name);if(len<MAX_NAME&&ch>=32){rg.name[len]=(char)ch;rg.name[len+1]='\0';}}
            if(IsKeyPressed(KEY_BACKSPACE)){int len=(int)strlen(rg.name);if(len>0)rg.name[len-1]='\0';}
            if(IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_ESCAPE)){rg.renaming=false;renamingIdx=-1;}
            if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON)&&!CheckCollisionPointRec(mouseWorld,rg.rect)){rg.renaming=false;renamingIdx=-1;}
            goto draw;
        }

        if(mouseScreen.y>=canvasH&&mouseScreen.x<canvasW)
        {
            for(int i=0;i<N_BTNS;i++)
            {
                int bx=barStartX+i*(BTN_W+BTN_GAP);
                int by=SH-FOOTER_H+(FOOTER_H-BTN_H)/2;
                Rectangle r={(float)bx,(float)by,(float)BTN_W,(float)BTN_H};
                if(CheckCollisionPointRec(mouseScreen,r)&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                {
                    Vector2 c=screenToWorld({(float)canvasW/2.f,(float)canvasH/2.f},cam);
                    Gate g; g.type=TOOLBAR[i].type;
                    g.rect={c.x-GATE_W/2.f,c.y-GATE_H/2.f,(float)GATE_W,(float)GATE_H};
                    g.name[0]='\0'; g.rebuildNodes();
                    gates.push_back(g); selected.push_back(false);
                }
            }
        }

        if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON)&&mouseInCanvas&&!panning)
        {
            bool handled=false;
            bool shiftHeld=IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);

            for(int i=(int)gates.size()-1;i>=0;i--)
                if(CheckCollisionPointRec(mouseWorld,gates[i].deleteRect()))
                { deleteGate(i,gates,wires,selected,draggedIdx,renamingIdx,wireGateA,wiring); wireNodeA=-1; handled=true; break; }

            if(!handled)
            {
                double now=GetTime();
                for(int i=(int)gates.size()-1;i>=0;i--)
                    if(CheckCollisionPointRec(mouseWorld,gates[i].rect))
                    {
                        if(i==lastClickGate&&(now-lastClickTime)<0.35&&
                           (gates[i].type==GateType::INPUT||gates[i].type==GateType::OUTPUT))
                        { if(renamingIdx>=0)gates[renamingIdx].renaming=false; gates[i].renaming=true; renamingIdx=i; handled=true; }
                        lastClickTime=now; lastClickGate=i; break;
                    }
            }

            if(!handled)
                for(auto& g:gates)
                    if(g.type==GateType::INPUT&&CheckCollisionPointRec(mouseWorld,g.checkboxRect()))
                    { g.checked=!g.checked; handled=true; break; }

            if(!handled&&!shiftHeld)
            {
                int hitGate=-1,hitNode=-1; float hitR=NODE_HIT/cam.zoom;
                for(int gi=(int)gates.size()-1;gi>=0;gi--)
                    for(int ni=0;ni<(int)gates[gi].nodes.size();ni++)
                        if(nodeDist(mouseWorld,gates[gi].nodes[ni].pos)<=hitR)
                        { hitGate=gi;hitNode=ni;goto foundNode; }
                foundNode:
                if(hitGate>=0)
                {
                    if(!wiring){wiring=true;wireGateA=hitGate;wireNodeA=hitNode;}
                    else
                    {
                        if(!(hitGate==wireGateA&&hitNode==wireNodeA))
                        {
                            bool aOut=gates[wireGateA].nodes[wireNodeA].isOutput;
                            bool bOut=gates[hitGate].nodes[hitNode].isOutput;
                            if(aOut!=bOut)
                            {
                                Wire w=aOut?Wire{wireGateA,wireNodeA,hitGate,hitNode}:Wire{hitGate,hitNode,wireGateA,wireNodeA};
                                wires.push_back(w);
                            }
                        }
                        wiring=false; wireGateA=wireNodeA=-1;
                    }
                    handled=true;
                }
            }

            if(!handled&&shiftHeld)
                for(int i=(int)gates.size()-1;i>=0;i--)
                    if(CheckCollisionPointRec(mouseWorld,gates[i].rect))
                    { selected[i]=!selected[i]; handled=true; break; }

            if(!handled)
            {
                if(wiring){wiring=false;wireGateA=wireNodeA=-1;}
                else
                {
                    bool hitGate2=false;
                    for(int i=(int)gates.size()-1;i>=0;i--)
                        if(CheckCollisionPointRec(mouseWorld,gates[i].rect))
                        {
                            draggedIdx=i; gates[i].dragging=true;
                            gates[i].dragOffset={mouseWorld.x-gates[i].rect.x,mouseWorld.y-gates[i].rect.y};
                            hitGate2=true; break;
                        }
                    if(!hitGate2)
                    {
                        dragSelecting=true;
                        dragSelStartW=mouseWorld;
                        dragSelCurW=mouseWorld;
                        if(!shiftHeld) std::fill(selected.begin(),selected.end(),false);
                    }
                }
            }
        }

        if(dragSelecting&&IsMouseButtonDown(MOUSE_LEFT_BUTTON)&&mouseInCanvas)
            dragSelCurW=mouseWorld;

        if(dragSelecting&&IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
        {
            dragSelecting=false;
            float rx=std::min(dragSelStartW.x,dragSelCurW.x);
            float ry=std::min(dragSelStartW.y,dragSelCurW.y);
            float rw=fabsf(dragSelCurW.x-dragSelStartW.x);
            float rh=fabsf(dragSelCurW.y-dragSelStartW.y);
            if(rw>4&&rh>4)
            {
                Rectangle selRect={rx,ry,rw,rh};
                for(int i=0;i<(int)gates.size();i++)
                    if(CheckCollisionRecs(selRect,gates[i].rect))
                        selected[i]=true;
            }
        }

        if(IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)&&mouseInCanvas&&!panning)
        {
            bool dg=false;
            for(int i=(int)gates.size()-1;i>=0;i--)
                if(CheckCollisionPointRec(mouseWorld,gates[i].rect))
                { deleteGate(i,gates,wires,selected,draggedIdx,renamingIdx,wireGateA,wiring); wireNodeA=-1; dg=true; break; }
            if(!dg)
            {
                int bw=-1; float bd=10.f/cam.zoom;
                for(int wi=0;wi<(int)wires.size();wi++)
                { float d=bezierDist(gates[wires[wi].gateA].nodes[wires[wi].nodeA].pos,gates[wires[wi].gateB].nodes[wires[wi].nodeB].pos,mouseWorld); if(d<bd){bd=d;bw=wi;} }
                if(bw>=0) wires.erase(wires.begin()+bw);
            }
        }

        if(IsMouseButtonDown(MOUSE_LEFT_BUTTON)&&draggedIdx>=0&&!panning)
        {
            Gate& g=gates[draggedIdx];
            g.rect.x=mouseWorld.x-g.dragOffset.x; g.rect.y=mouseWorld.y-g.dragOffset.y;
            g.rebuildNodes();
        }
        if(IsMouseButtonReleased(MOUSE_LEFT_BUTTON)&&draggedIdx>=0){gates[draggedIdx].dragging=false;draggedIdx=-1;}

        propagate(gates,wires);

        draw:
        BeginDrawing();
        ClearBackground(BG_COLOR);

        cam.offset={(float)canvasW/2.f,(float)canvasH/2.f};

        BeginMode2D(cam);
        {
            Vector2 tl=screenToWorld({0,0},cam),br=screenToWorld({(float)canvasW,(float)canvasH},cam);
            int gs=30,gx0=((int)tl.x/gs-1)*gs,gy0=((int)tl.y/gs-1)*gs;
            for(int gx=gx0;gx<(int)br.x+gs;gx+=gs)
                for(int gy=gy0;gy<(int)br.y+gs;gy+=gs) DrawPixel(gx,gy,{75,75,78,255});
        }

        int hovWire=-1;
        { float bd=10.f/cam.zoom; for(int wi=0;wi<(int)wires.size();wi++){ float d=bezierDist(gates[wires[wi].gateA].nodes[wires[wi].nodeA].pos,gates[wires[wi].gateB].nodes[wires[wi].nodeB].pos,mouseWorld); if(d<bd){bd=d;hovWire=wi;} } }

        for(int wi=0;wi<(int)wires.size();wi++)
        {
            const Wire& w=wires[wi];
            Vector2 from=gates[w.gateA].nodes[w.nodeA].pos,to=gates[w.gateB].nodes[w.nodeB].pos;
            bool sig=gates[w.gateA].nodes[w.nodeA].signal,hov=(wi==hovWire);
            Color wc=hov?Color{255,80,80,255}:sig?Color{255,220,50,255}:Color{120,120,125,255};
            DrawBezier(from,to,wc,hov||sig?3.f:2.f); DrawCircleV(from,4,wc); DrawCircleV(to,4,wc);
        }
        if(wiring&&wireGateA>=0){ DrawBezier(gates[wireGateA].nodes[wireNodeA].pos,mouseWorld,{200,200,200,160},1.5f); DrawCircleV(gates[wireGateA].nodes[wireNodeA].pos,4,WHITE); }

        int hovGate=-1;
        if(!panning&&renamingIdx<0&&mouseInCanvas)
            for(int i=(int)gates.size()-1;i>=0;i--)
                if(CheckCollisionPointRec(mouseWorld,gates[i].rect)){hovGate=i;break;}

        for(int i=0;i<(int)gates.size();i++)
            DrawGate(gates[i],i==hovGate,i<(int)selected.size()&&selected[i]);

        if(dragSelecting)
        {
            float rx=std::min(dragSelStartW.x,dragSelCurW.x);
            float ry=std::min(dragSelStartW.y,dragSelCurW.y);
            float rw=fabsf(dragSelCurW.x-dragSelStartW.x);
            float rh=fabsf(dragSelCurW.y-dragSelStartW.y);
            DrawRectangle((int)rx,(int)ry,(int)rw,(int)rh,{255,220,50,25});
            DrawRectangleLinesEx({rx,ry,rw,rh},1.5f/cam.zoom,{255,220,50,200});
        }

        EndMode2D();

        DrawRectangle(canvasW,0,PANEL_W,SH,PANEL_BG);
        DrawRectangle(0,canvasH,canvasW,FOOTER_H,BAR_COLOR);
        DrawLine(0,canvasH,canvasW,canvasH,{70,70,72,255});

        for(int i=0;i<N_BTNS;i++)
        {
            int bx=barStartX+i*(BTN_W+BTN_GAP),by=SH-FOOTER_H+(FOOTER_H-BTN_H)/2;
            DrawToolbarButton(bx,by,BTN_W,BTN_H,TOOLBAR[i].label,TOOLBAR[i].accent);
        }

        int placeIdx=-1,deleteIdx=-1;
        DrawSavedPanel(canvasW,0,PANEL_W,SH,savedCircuits,panelScroll,placeIdx,deleteIdx);

        if(placeIdx>=0&&placeIdx<(int)savedCircuits.size())
        {
            Vector2 c=screenToWorld({(float)canvasW/2.f,(float)canvasH/2.f},cam);
            std::fill(selected.begin(),selected.end(),false);
            placeAsNode(savedCircuits[placeIdx],c,gates,selected);
        }
        if(deleteIdx>=0&&deleteIdx<(int)savedCircuits.size())
        {
            deleteCircuitFile(savedCircuits[deleteIdx].name);
            savedCircuits.erase(savedCircuits.begin()+deleteIdx);
        }

        { char zt[32]; sprintf(zt,"%.0f%%",(double)(cam.zoom*100.f)); int zfs=16,ztw=MeasureText(zt,zfs); DrawRectangle(canvasW-ztw-22,6,ztw+16,22,{30,30,32,200}); DrawText(zt,canvasW-ztw-14,10,zfs,{160,160,163,255}); }

        int selCount=(int)std::count(selected.begin(),selected.end(),true);
        if(selCount>0)
        {
            char sb[64]; sprintf(sb,"%d selected  |  Ctrl+S to save  |  Del to delete",selCount);
            int sfs=14,stw=MeasureText(sb,sfs);
            DrawRectangle(canvasW/2-stw/2-10,canvasH-30,stw+20,22,{30,30,32,220});
            DrawText(sb,canvasW/2-stw/2,canvasH-26,sfs,SEL_COLOR);
        }

        if(renamingIdx>=0)       DrawText("Typing name — ENTER to confirm",10,10,16,YELLOW);
        else if(wiring)          DrawText("Click node to finish wire  |  Click canvas to cancel",10,10,16,{240,200,80,255});
        else if(panning)         DrawText("Panning...",10,10,16,{100,200,255,255});
        else if(dragSelecting)   DrawText("Release to select  |  Shift=add to selection",10,10,14,{255,220,50,255});
        else                     DrawText("Drag=select  Shift+Click=add  Ctrl+S=save  Ctrl+A=all  Del=delete  R=reset  RClick=remove",10,10,13,{100,100,104,255});

        if(savingModal)
        {
            bool cancelled=false,confirmed=false;
            DrawSaveModal(SW,SH,circuitNameBuf,cancelled,confirmed);
            if(confirmed)
            {
                SavedCircuit sc;
                buildSavePayload(gates,selected,wires,circuitNameBuf,sc);
                if(!sc.gates.empty())
                {
                    saveCircuitToFile(sc);
                    auto it=std::find_if(savedCircuits.begin(),savedCircuits.end(),[&](auto& s){return s.name==sc.name;});
                    if(it!=savedCircuits.end())*it=sc; else savedCircuits.push_back(sc);
                }
                savingModal=false; memset(circuitNameBuf,0,sizeof(circuitNameBuf));
            }
            if(cancelled){savingModal=false;memset(circuitNameBuf,0,sizeof(circuitNameBuf));}
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}