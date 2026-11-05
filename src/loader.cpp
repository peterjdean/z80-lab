#include "loader.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace z80emu;
static int nib(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static std::string trim(std::string s){auto a=s.find_first_not_of(" \r\n\t");if(a==std::string::npos)return "";auto b=s.find_last_not_of(" \r\n\t");return s.substr(a,b-a+1);}
LoadResult z80emu::loadFile(Cpu& cpu,const std::string& path,std::optional<uint16_t> origin,std::string format){
 std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot open: "+path);
 std::string raw((std::istreambuf_iterator<char>(in)),{});
 return loadData(cpu,raw,origin,format);
}
LoadResult z80emu::loadData(Cpu& cpu,std::string_view data,std::optional<uint16_t> origin,std::string format){
 std::string raw(data);if(raw.empty())throw std::runtime_error("Empty input file");
 if(format=="auto"){
  std::string content=trim(raw);if(!content.empty()&&content[0]==':')format="ihex";
  else{bool looks_text=true;for(unsigned char c:raw){if(!std::isprint(c)&&!std::isspace(c)){looks_text=false;break;}}format=looks_text?"hex":"bin";}
 }
 LoadResult result;result.entry=origin.value_or(0);
 if(format=="bin"){
  uint32_t pos=origin.value_or(0);if(pos+raw.size()>65536)throw std::runtime_error("Binary exceeds 64K RAM");
  for(unsigned char b:raw)cpu.mem[pos++]=b;
  result.bytes=raw.size();result.has_entry=origin.has_value();return result;
 }
 if(format=="hex"){
  std::istringstream ss(raw);std::string line;uint32_t address=origin.value_or(0);int lineno=0;
  while(std::getline(ss,line)){++lineno;auto comment=line.find_first_of(";#");if(comment!=std::string::npos)line.resize(comment);line=trim(line);if(line.empty())continue;
   // Allow whitespace/comma-separated byte pairs, but never silently skip bad tokens.
   for(char& c:line)if(c==',')c=' ';
   std::istringstream ls(line);std::string token;
   while(ls>>token){if(token.size()==4&&token.substr(0,2)=="0x")token=token.substr(2);if(token.empty()||token.size()%2)throw std::runtime_error("Invalid hex text on line "+std::to_string(lineno)+": "+token);
    for(size_t t=0;t<token.size();t+=2){
     if(nib(token[t])<0||nib(token[t+1])<0)throw std::runtime_error("Invalid hex byte on line "+std::to_string(lineno)+": "+token);
     if(address>65535)throw std::runtime_error("Hex data exceeds 64K RAM");
     cpu.mem[address++]=uint8_t(nib(token[t])*16+nib(token[t+1]));++result.bytes;
    }
   }
  }
  if(!result.bytes)throw std::runtime_error("No bytes found in hex file");
  result.has_entry=origin.has_value();return result;
 }
 if(format=="ihex"){
  std::istringstream ss(raw);std::string line;uint32_t base=0;bool eof=false,first=true;int lineno=0;
  while(std::getline(ss,line)){++lineno;line=trim(line);if(line.empty())continue;if(eof)throw std::runtime_error("Data after Intel HEX EOF");
   if(line[0]!=':'||(line.size()-1)%2)throw std::runtime_error("Malformed Intel HEX line "+std::to_string(lineno));
   std::vector<uint8_t> b;for(size_t j=1;j<line.size();j+=2){if(nib(line[j])<0||nib(line[j+1])<0)throw std::runtime_error("Bad Intel HEX digit");b.push_back(uint8_t(nib(line[j])*16+nib(line[j+1])));}
   if(b.size()<5||b.size()!=size_t(b[0])+5)throw std::runtime_error("Intel HEX record length mismatch");
   int sum=0;for(uint8_t v:b)sum+=v;if(sum&255)throw std::runtime_error("Intel HEX checksum error on line "+std::to_string(lineno));
   uint32_t addr=uint32_t(b[1])<<8|b[2];uint8_t type=b[3];
   if(type==0){if(base+addr+b[0]>65536)throw std::runtime_error("Intel HEX address outside 64K RAM");if(b[0]&&first&&!origin){result.entry=uint16_t(base+addr);result.has_entry=true;first=false;}for(int j=0;j<b[0];++j){cpu.mem[base+addr+j]=b[4+j];++result.bytes;}}
   else if(type==1){if(b[0]!=0)throw std::runtime_error("Invalid EOF record");eof=true;}
   else if(type==2||type==4){if(b[0]!=2)throw std::runtime_error("Invalid extended address record");uint32_t v=(uint32_t(b[4])<<8)|b[5];base=type==2?v<<4:v<<16;if(base>65535)throw std::runtime_error("Intel HEX segment outside 64K RAM");}
   else if(type==3||type==5){if(b[0]!=4)throw std::runtime_error("Invalid entry-address record");if(!origin){uint32_t address=type==5?(uint32_t(b[4])<<24|uint32_t(b[5])<<16|uint32_t(b[6])<<8|b[7]):(uint32_t((b[4]<<8)|b[5])<<4)+uint32_t((b[6]<<8)|b[7]);if(address>65535)throw std::runtime_error("Entry address outside 64K RAM");result.entry=uint16_t(address);result.has_entry=true;}}
   else throw std::runtime_error("Unsupported Intel HEX record type "+std::to_string(type));
  }
  if(!eof)throw std::runtime_error("Missing Intel HEX EOF record");
  if(!result.bytes)throw std::runtime_error("No Intel HEX data records");
  if(origin){result.entry=*origin;result.has_entry=true;}return result;
 }
 throw std::runtime_error("Unknown format: "+format+" (use auto, hex, ihex or bin)");
}
