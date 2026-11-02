// Lista quem chama (ou referencia) cada endereço dado, com a função que contém a chamada.
// Uso: -postScript FindCallers.java <arquivo_saida> <addr1> [addr2 ...]
//@category SoDCoop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.io.*;

public class FindCallers extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		PrintWriter out = new PrintWriter(new FileWriter(args[0]));
		for (int i = 1; i < args.length; i++) {
			Address a = toAddr(Long.parseLong(args[i].replace("0x", ""), 16));
			out.println("##### " + args[i]);
			for (Reference r : getReferencesTo(a)) {
				Function f = getFunctionContaining(r.getFromAddress());
				out.println("  " + r.getFromAddress() + " " + r.getReferenceType() + " in " +
						(f == null ? "<no func>" : f.getName() + "@" + f.getEntryPoint()));
			}
		}
		out.close();
	}
}
