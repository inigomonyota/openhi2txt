#include "io/FilesystemReader.h"
#include "io/ChdReader.h"
#include "xml/XmlParser.h"
#include "app/InputProcessor.h"
#include "core/InputPlanner.h"
#include "io/Utils.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <iterator>
using namespace openhi2txt;
void require(bool value,const char* message) { if(!value) {std::cerr<<message<<'\n';std::exit(1);} }
void put16(std::vector<uint8_t>& b,size_t p,unsigned v){b[p]=uint8_t(v);b[p+1]=uint8_t(v>>8);}
void put32(std::vector<uint8_t>& b,size_t p,unsigned v){put16(b,p,v);put16(b,p+2,v>>16);}
size_t at(unsigned block){return 512+block*1024;}
std::vector<uint8_t> fixture() {
    std::vector<uint8_t> b(at(64),0);
    put16(b,510,0xaa55);b[450]=0x83;put32(b,454,1);put32(b,458,128);
    size_t s=at(1);put32(b,s,16);put32(b,s+4,64);put32(b,s+20,1);
    put32(b,s+32,64);put32(b,s+40,16);put16(b,s+56,0xef53);
    put32(b,at(2)+8,5);
    auto node=[&](unsigned n,unsigned mode,unsigned size,unsigned block){
        size_t p=at(5)+(n-1)*128;put16(b,p,mode);put32(b,p+4,size);put16(b,p+26,1);put32(b,p+40,block);
    };
    node(2,0x4000,1024,7);node(3,0x4000,1024,8);node(4,0x8000,4,9);
    auto entry=[&](unsigned block,unsigned inode,const std::string& name){
        size_t p=at(block);put32(b,p,inode);put16(b,p+4,1024);put16(b,p+6,unsigned(name.size()));
        std::copy(name.begin(),name.end(),b.begin()+p+8);
    };
    entry(7,3,"hiscore");entry(8,4,"score.dat");put32(b,at(9),3800);put32(b,at(10),3550);
    return b;
}
std::unique_ptr<FilesystemReader> open(std::vector<uint8_t>& b,std::string& error) {
    return FilesystemReader::open("ext2",1,b.size(),[&](uint64_t off,size_t n,std::vector<uint8_t>& out,std::string& e){
        if(off>b.size() || n>b.size()-off){e="out of bounds";return false;}
        out.assign(b.begin()+size_t(off),b.begin()+size_t(off)+n);return true;
    },error);
}
int main(int argc,char** argv) {
    std::string error;std::vector<uint8_t> out;
    auto b=fixture();auto fs=open(b,error);require(bool(fs),error.c_str());
    require(fs->readFile("/hiscore/score.dat",out,error) && out[0]==0xd8,"normal ext2 lookup");
    put32(b,at(5)+3*128+40,10);fs=open(b,error);
    require(fs->readFile("/hiscore/score.dat",out,error) && out[0]==0xde,"relocated inode must select new data");
    require(b[at(9)]==0xd8,"old score remains physically present");
    for(size_t i=at(1);i<at(2);++i)b[i]^=255;
    fs=open(b,error);require(bool(fs) && fs->readFile("/hiscore/score.dat",out,error),"inverted superblock");
    require(!fs->readFile("/hiscore/missing",out,error),"missing file rejected");
    require(!fs->readFile("/hiscore/../score.dat",out,error),"traversal rejected");
    b=fixture();put32(b,at(5)+3*128+40,64);fs=open(b,error);
    require(!fs->readFile("/hiscore/score.dat",out,error),"out-of-bounds block rejected");
    b=fixture();put16(b,at(8)+4,0);fs=open(b,error);
    require(!fs->readFile("/hiscore/score.dat",out,error),"zero directory record rejected");
    b=fixture();put32(b,at(1)+76,1);put16(b,at(1)+88,128);put32(b,at(1)+96,0x40);
    require(!open(b,error),"extents feature rejected");
    b=fixture();size_t node=at(5)+3*128;put32(b,node+4,13*1024);
    for(int i=0;i<12;++i)put32(b,node+40+i*4,9);
    put32(b,node+88,11);put32(b,at(11),10);fs=open(b,error);
    require(fs->readFile("/hiscore/score.dat",out,error) && out[12*1024]==0xde,"single indirect blocks");
    put32(b,node+40,0);fs=open(b,error);
    require(fs->readFile("/hiscore/score.dat",out,error) && out[0]==0,"sparse hole");
    b=fixture();put32(b,at(1)+76,1);put16(b,at(1)+88,128);put32(b,at(1)+96,2);
    b[at(7)+7]=2;b[at(8)+7]=1;fs=open(b,error);
    require(bool(fs) && fs->readFile("/hiscore/score.dat",out,error),"revision one typed directories");
    b=fixture();put32(b,at(5)+2*128+12,0x46d07580);put32(b,at(5)+2*128+32,0x46d07580);
    fs=open(b,error);require(!fs->readFile("/hiscore/score.dat",out,error),"ordinary ext2 rejects timestamp flags");
    for(size_t i=at(1);i<at(2);++i)b[i]^=255;
    fs=open(b,error);require(fs->readFile("/hiscore/score.dat",out,error),"inverted variant timestamp flags");
    b=fixture();node=at(5)+3*128;put32(b,node+4,(12+256+1)*1024);
    put32(b,node+92,11);put32(b,at(11),12);put32(b,at(12),10);fs=open(b,error);
    require(fs->readFile("/hiscore/score.dat",out,error) && out[(12+256)*1024]==0xde,"double indirect blocks");
    const std::string xml=R"(<openhi2txt requires="0.6.0"><structure file="dif" filesystem="ext2" partition="1"><source file="/hiscore/score.dat"><check><size>4</size></check><elt id="SCORE" type="int" size="4" endianness="little_endian"/></source></structure></openhi2txt>)";
    auto parsed=XmlParser::parseWithDiagnostics(xml);require(parsed.ok,"filesystem XML parses");
    require(parsed.def.structures[0].sources.size()==1 && parsed.def.structures[0].sources[0].items.size()==1,"source decoding preserved");
    auto bad=xml;bad.replace(bad.find("partition=\"1\""),13,"partition=\"0\"");
    require(!XmlParser::parseWithDiagnostics(bad).ok,"partition zero rejected");
    bad=xml;bad.insert(bad.find(" filesystem")," offset=\"0\" length=\"4\"");
    require(!XmlParser::parseWithDiagnostics(bad).ok,"mixed input modes rejected");
    bool rejected=false;try {InputPlanner::plan(parsed.def);}catch(...){rejected=true;}
    require(rejected,"static live planning explicitly rejected");
    if(argc==5) {
        std::ifstream file(argv[1]);std::string text((std::istreambuf_iterator<char>(file)),{});
        auto game=XmlParser::parseWithDiagnostics(text);require(game.ok,game.error.c_str());
        auto result=InputProcessor::process(argv[2],"carnking",game.def,argv[3],nullptr);
        require(result.ok,result.error.c_str());
        require(result.rows.size()==11,"expected eleven combined rows");
        const bool newer=std::string(argv[4])=="new";
        require(std::get<std::string>(result.rows[10].at("ARCADIA_NAME"))==(newer ? "ZZZ":"RWM"),"Arcadia snapshot name");
        require(std::get<int64_t>(result.rows[10].at("ARCADIA_SCORE"))==(newer ? 3550:3200),"Arcadia snapshot score");
        require(std::get<std::string>(result.rows[2].at("AKNOCK_NAME"))==(newer ? "ZZZ":"CES"),"Knock snapshot name");
        require(std::get<int64_t>(result.rows[2].at("AKNOCK_SCORE"))==(newer ? 3550:800),"Knock snapshot score");
        for(const auto& row:result.rows) {
            for(const auto& field:row) {
                if(field.first=="ARCADIA_NAME" || field.first=="ARCADIA_SCORE" || field.first=="AKNOCK_NAME" || field.first=="AKNOCK_SCORE") {
                    std::cout<<field.first<<'=';
                    std::visit([](const auto& v){using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,std::string> || std::is_same_v<T,int64_t>)std::cout<<v;},field.second);
                    std::cout<<' ';
                }
            }
            std::cout<<'\n';
        }
    }
    std::cout<<"Filesystem tests passed\n";
}
