// Descompila as funções que contêm os endereços dados e grava o C em um arquivo.
// Se não houver função no endereço, cria uma ali (só na memória; o projeto é
// aberto read-only pelo run_ghidra_script.ps1).
// Uso: -postScript DecompileFuncs.java <arquivo_saida> <addr1> [addr2 ...]
//@category SoDCoop
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.*;

public class DecompileFuncs extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		PrintWriter out = new PrintWriter(new FileWriter(args[0]));
		DecompInterface di = new DecompInterface();
		di.openProgram(currentProgram);
		for (int i = 1; i < args.length; i++) {
			ghidra.program.model.address.Address a = toAddr(Long.parseLong(args[i].replace("0x", ""), 16));
			Function f = getFunctionContaining(a);
			if (f == null) {
				disassemble(a);
				f = createFunction(a, null);
			}
			out.println("// ================= " + args[i] + " -> " + (f == null ? "no function" : f.getName() + " @ " + f.getEntryPoint()));
			if (f == null) continue;
			DecompileResults r = di.decompileFunction(f, 120, monitor);
			out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed: " + r.getErrorMessage());
		}
		di.dispose();
		out.close();
	}
}
