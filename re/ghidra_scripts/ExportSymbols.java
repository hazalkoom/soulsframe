// Headless Ghidra postScript that exports function names and RVAs to JSON.
// Usage: analyzeHeadless ... -postScript ExportSymbols.java /work/re/db/ds3_functions.json
//@category SoulsFrame

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.io.FileWriter;
import java.io.PrintWriter;

public class ExportSymbols extends GhidraScript {

  private static String escapeJson(String s) {
    return s.replace("\\", "\\\\").replace("\"", "\\\"");
  }

  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 1) {
      printerr("Usage: ExportSymbols.java <output_json_path>");
      return;
    }

    long base = currentProgram.getImageBase().getOffset();
    FunctionIterator funcs = currentProgram.getFunctionManager().getFunctions(true);

    try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
      out.println("{");
      out.printf("  \"program\": \"%s\",%n", escapeJson(currentProgram.getName()));
      out.printf("  \"image_base\": \"0x%x\",%n", base);
      out.println("  \"functions\": [");
      boolean first = true;
      while (funcs.hasNext() && !monitor.isCancelled()) {
        Function fn = funcs.next();
        long addr = fn.getEntryPoint().getOffset();
        long rva = addr - base;
        if (!first) {
          out.println(",");
        }
        first = false;
        out.printf(
            "    {\"name\": \"%s\", \"rva\": \"0x%x\", \"size\": %d}",
            escapeJson(fn.getName()), rva, fn.getBody().getNumAddresses());
      }
      out.println();
      out.println("  ]");
      out.println("}");
    }
  }
}
