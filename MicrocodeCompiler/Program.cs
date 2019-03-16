using System;

namespace MicrocodeCompiler {
    class Program {
        static void Main(string[] args) {
            if(args.Length < 1) {
                Console.WriteLine("file name required");
                return;
            } else if(args.Length > 1) {
                Console.WriteLine("only one file name can be provided");
                return;
            }

            var s = new FileStream(args[0]);
            var tokens = new TokenStream(s);
            new Parser(tokens).Parse().Print();
        }
    }
}
