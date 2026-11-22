import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
public class Export extends GhidraScript {
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
    PrintWriter w = new PrintWriter(new FileWriter(out));
    int n=0;
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      DecompileResults r = di.decompileFunction(f, 60, monitor);
      w.println("// ===== " + f.getName() + " @ " + f.getEntryPoint() + " size " + f.getBody().getNumAddresses());
      w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed: "+r.getErrorMessage());
      n++;
    }
    w.close(); println("exported "+n+" functions");
  }
}
