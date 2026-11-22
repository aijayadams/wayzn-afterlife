import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
public class Prep extends GhidraScript {
  public void run() throws Exception {
    Memory m = currentProgram.getMemory();
    AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
    long[][] regs = {{0x20000000L,0x2000},{0x40000000L,0x8000},{0x40010000L,0x8000},{0x40020000L,0x4000},{0x48000000L,0x1800},{0xE000E000L,0x1000},{0x1FFFF000L,0x1000}};
    String[] names = {"RAM","APB1","APB2","AHB1","GPIO","SCS","SYSMEM"};
    for (int i=0;i<regs.length;i++) {
      MemoryBlock b = m.createUninitializedBlock(names[i], sp.getAddress(regs[i][0]), regs[i][1], false);
      b.setRead(true); b.setWrite(true); if (i>0) b.setVolatile(true);
    }
    Address base = sp.getAddress(0x08000000L);
    for (int i=1;i<48;i++) {
      long v = m.getInt(base.add(4*i)) & 0xffffffffL;
      if (v==0 || v==0xffffffffL || (v & 1)==0) continue;
      Address a = sp.getAddress(v & ~1L);
      currentProgram.getProgramContext().setValue(currentProgram.getProgramContext().getRegister("TMode"), a, a, java.math.BigInteger.ONE);
      createFunction(a, i==1 ? "Reset_Handler" : "vec_"+i);
      disassemble(a);
    }
    Address s2 = sp.getAddress(0x08008400L);
    currentProgram.getProgramContext().setValue(currentProgram.getProgramContext().getRegister("TMode"), s2, sp.getAddress(0x0800CBFFL), java.math.BigInteger.ONE);
    disassemble(s2); createFunction(s2, "region2_start");
  }
}
