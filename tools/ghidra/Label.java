import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
public class Label extends GhidraScript {
  String cstr(long a) throws Exception {
    StringBuilder b=new StringBuilder(); Memory m=currentProgram.getMemory();
    for(int i=0;i<80;i++){byte c=m.getByte(toAddr(a+i)); if(c==0)break; b.append((char)c);} return b.toString();
  }
  public void run() throws Exception {
    Memory m=currentProgram.getMemory();
    for(long e=0x08007330L;e<0x0800754cL;e+=12){
      String name=cstr(m.getInt(toAddr(e))&0xffffffffL).replaceAll("[^A-Za-z0-9_]","_");
      long h=m.getInt(toAddr(e+4))&0xffffffffL; Address a=toAddr(h&~1L);
      Function f=getFunctionAt(a);
      if(f==null){ disassemble(a); f=createFunction(a,"cmd_"+name);} else f.setName("cmd_"+name, SourceType.USER_DEFINED);
      println(String.format("%s %s",a,name));
    }
    // define strings so decompile shows them
    for (long a=0x080071fcL; a<0x08007db8L; ) { String s=cstr(a); if(s.length()>0){ try{ createAsciiString(toAddr(a)); }catch(Exception x){} a+=s.length()+1;} else a++; }
  }
}
